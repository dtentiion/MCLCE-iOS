// Mach exception handler. Catches kernel-level exceptions BEFORE the
// kernel converts them into POSIX signals (or skips signals entirely
// for EXC_RESOURCE / EXC_GUARD / EXC_CRASH on iOS). The existing
// sigaction-based handler is plenty for most C++ faults but misses
// the cases where iOS SIGKILLs us directly with no signal dispatch -
// notably CPU/memory/wakeup quota kills, sandbox violations, and the
// catch-all EXC_CRASH wrapper iOS uses for some lifecycle terminations.
//
// Implementation pattern follows what PLCrashReporter / KSCrash do:
//  1. Allocate a Mach port + insert a send right.
//  2. task_set_exception_ports redirects the chosen masks to our port.
//  3. A dedicated pthread runs a mach_msg receive loop forever.
//  4. On each message we decode the exception type/code, write to the
//     pre-opened crash fd via async-signal-safe write(), then send a
//     KERN_FAILURE reply so the kernel falls back to its default
//     handler (signal dispatch or SIGKILL). The process still dies but
//     we've logged the kernel-side reason first.
//
// Uses the MACH_EXCEPTION_CODES variant (64-bit codes) so EXC_CRASH /
// EXC_RESOURCE / EXC_GUARD codes don't get truncated.

#include <mach/mach.h>
#include <mach/exception_types.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

extern "C" {

// Set up by MCLEGameLoop.cpp's mcle_open_crash_log_fd. We re-extern it
// here so this TU can write directly without going through MCLE_LOG
// (which uses snprintf + NSLog + fprintf - none async-signal-safe).
extern int g_crash_log_fd;

// Format helpers shared with the signal handler. Both write the same
// fd so log lines from both sources interleave cleanly.
static size_t mach_safe_itoa(char *buf, size_t cap, long v) {
    if (cap == 0) return 0;
    if (v < 0) {
        if (cap < 2) return 0;
        buf[0] = '-';
        return 1 + mach_safe_itoa(buf + 1, cap - 1, -v);
    }
    char tmp[24]; size_t t = 0;
    if (v == 0) { tmp[t++] = '0'; }
    while (v > 0 && t < sizeof(tmp)) { tmp[t++] = '0' + (char)(v % 10); v /= 10; }
    size_t n = 0;
    while (t > 0 && n < cap) { buf[n++] = tmp[--t]; }
    return n;
}

static size_t mach_safe_hex(char *buf, size_t cap, uint64_t v) {
    if (cap < 3) return 0;
    buf[0] = '0'; buf[1] = 'x';
    size_t n = 2;
    char tmp[20]; size_t t = 0;
    if (v == 0) { tmp[t++] = '0'; }
    while (v > 0 && t < sizeof(tmp)) {
        int d = (int)(v & 0xF);
        tmp[t++] = (char)(d < 10 ? '0' + d : 'a' + (d - 10));
        v >>= 4;
    }
    while (t > 0 && n < cap) { buf[n++] = tmp[--t]; }
    return n;
}

static const char* exc_type_name(int t) {
    switch (t) {
        case EXC_BAD_ACCESS:       return "EXC_BAD_ACCESS";
        case EXC_BAD_INSTRUCTION:  return "EXC_BAD_INSTRUCTION";
        case EXC_ARITHMETIC:       return "EXC_ARITHMETIC";
        case EXC_EMULATION:        return "EXC_EMULATION";
        case EXC_SOFTWARE:         return "EXC_SOFTWARE";
        case EXC_BREAKPOINT:       return "EXC_BREAKPOINT";
        case EXC_SYSCALL:          return "EXC_SYSCALL";
        case EXC_MACH_SYSCALL:     return "EXC_MACH_SYSCALL";
        case EXC_RPC_ALERT:        return "EXC_RPC_ALERT";
        case EXC_CRASH:            return "EXC_CRASH";
        case EXC_RESOURCE:         return "EXC_RESOURCE";
        case EXC_GUARD:            return "EXC_GUARD";
        default:                    return "EXC_UNKNOWN";
    }
}

static mach_port_t g_exception_port = MACH_PORT_NULL;

// Message format for an exception_raise call with MACH_EXCEPTION_CODES
// (64-bit). Two port descriptors (thread, task), NDR record, then the
// exception type + code count + code array. Matches the kernel-side
// MIG definition for the mach_exc subsystem.
struct MclEMachExcRequest {
    mach_msg_header_t Head;
    mach_msg_body_t   msgh_body;
    mach_msg_port_descriptor_t thread;
    mach_msg_port_descriptor_t task;
    NDR_record_t      NDR;
    exception_type_t  exception;
    mach_msg_type_number_t codeCnt;
    int64_t           code[2];
};

struct MclEMachExcReply {
    mach_msg_header_t Head;
    NDR_record_t      NDR;
    kern_return_t     RetCode;
};

static void* mach_exception_thread_main(void* /*unused*/) {
    // Unique-ish fd write so log lines are recognisable.
    auto log_line = [](const char* tag, exception_type_t exc,
                       int64_t code0, int64_t code1) {
        if (g_crash_log_fd < 0) return;
        char buf[256]; size_t n = 0;
        const char* pfx = "[mcle] MACH_EXC ";
        size_t p = strlen(pfx);
        for (size_t i = 0; i < p && n < sizeof(buf); ++i) buf[n++] = pfx[i];
        size_t t = strlen(tag);
        for (size_t i = 0; i < t && n < sizeof(buf); ++i) buf[n++] = tag[i];
        if (n < sizeof(buf)) buf[n++] = ' ';
        const char* name = exc_type_name(exc);
        size_t nl = strlen(name);
        for (size_t i = 0; i < nl && n < sizeof(buf); ++i) buf[n++] = name[i];
        const char* mid = " type=";
        size_t ml = strlen(mid);
        for (size_t i = 0; i < ml && n < sizeof(buf); ++i) buf[n++] = mid[i];
        n += mach_safe_itoa(buf + n, sizeof(buf) - n, exc);
        const char* c0 = " code0=";
        size_t cl0 = strlen(c0);
        for (size_t i = 0; i < cl0 && n < sizeof(buf); ++i) buf[n++] = c0[i];
        n += mach_safe_hex(buf + n, sizeof(buf) - n, (uint64_t)code0);
        const char* c1 = " code1=";
        size_t cl1 = strlen(c1);
        for (size_t i = 0; i < cl1 && n < sizeof(buf); ++i) buf[n++] = c1[i];
        n += mach_safe_hex(buf + n, sizeof(buf) - n, (uint64_t)code1);
        if (n < sizeof(buf)) buf[n++] = '\n';
        ssize_t r = write(g_crash_log_fd, buf, n);
        (void)r;
        fsync(g_crash_log_fd);
    };

    for (;;) {
        MclEMachExcRequest req;
        memset(&req, 0, sizeof(req));
        req.Head.msgh_local_port = g_exception_port;
        req.Head.msgh_size = sizeof(req);

        mach_msg_return_t r = mach_msg(
            &req.Head,
            MACH_RCV_MSG | MACH_RCV_LARGE,
            0,
            sizeof(req),
            g_exception_port,
            MACH_MSG_TIMEOUT_NONE,
            MACH_PORT_NULL);

        if (r != MACH_MSG_SUCCESS) {
            // Don't busy-loop on persistent receive errors.
            usleep(1000);
            continue;
        }

        int64_t c0 = (req.codeCnt >= 1) ? req.code[0] : 0;
        int64_t c1 = (req.codeCnt >= 2) ? req.code[1] : 0;
        log_line("CAUGHT", req.exception, c0, c1);

        // Reply with KERN_FAILURE so the kernel falls through to the
        // next handler in the chain (eventually delivering a signal or
        // SIGKILL'ing). We've already logged what we needed.
        MclEMachExcReply reply;
        memset(&reply, 0, sizeof(reply));
        reply.Head.msgh_bits = MACH_MSGH_BITS(
            MACH_MSGH_BITS_REMOTE(req.Head.msgh_bits), 0);
        reply.Head.msgh_size        = sizeof(reply);
        reply.Head.msgh_remote_port = req.Head.msgh_remote_port;
        reply.Head.msgh_local_port  = MACH_PORT_NULL;
        reply.Head.msgh_id          = req.Head.msgh_id + 100;
        reply.NDR     = req.NDR;
        reply.RetCode = KERN_FAILURE;

        mach_msg(&reply.Head, MACH_SEND_MSG, sizeof(reply), 0,
                 MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    }
    return nullptr;
}

void mcle_install_mach_exception_handler(void) {
    if (g_exception_port != MACH_PORT_NULL) return;

    kern_return_t kr;
    kr = mach_port_allocate(mach_task_self(),
                             MACH_PORT_RIGHT_RECEIVE,
                             &g_exception_port);
    if (kr != KERN_SUCCESS) {
        g_exception_port = MACH_PORT_NULL;
        return;
    }

    kr = mach_port_insert_right(mach_task_self(),
                                 g_exception_port,
                                 g_exception_port,
                                 MACH_MSG_TYPE_MAKE_SEND);
    if (kr != KERN_SUCCESS) {
        mach_port_deallocate(mach_task_self(), g_exception_port);
        g_exception_port = MACH_PORT_NULL;
        return;
    }

    // Catch everything we can. Each mask name corresponds to one of
    // the kernel exception types in exception_types.h.
    exception_mask_t mask =
          EXC_MASK_BAD_ACCESS
        | EXC_MASK_BAD_INSTRUCTION
        | EXC_MASK_ARITHMETIC
        | EXC_MASK_BREAKPOINT
        | EXC_MASK_SOFTWARE
        | EXC_MASK_CRASH
        | EXC_MASK_RESOURCE
        | EXC_MASK_GUARD;

    kr = task_set_exception_ports(
        mach_task_self(),
        mask,
        g_exception_port,
        EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES,
        THREAD_STATE_NONE);
    if (kr != KERN_SUCCESS) {
        // Some masks (notably EXC_MASK_CRASH) may need an entitlement
        // we don't have. Retry with the safer subset so at least bad
        // access / instruction / arithmetic still get caught.
        exception_mask_t safe_mask =
              EXC_MASK_BAD_ACCESS
            | EXC_MASK_BAD_INSTRUCTION
            | EXC_MASK_ARITHMETIC
            | EXC_MASK_BREAKPOINT;
        kr = task_set_exception_ports(
            mach_task_self(),
            safe_mask,
            g_exception_port,
            EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES,
            THREAD_STATE_NONE);
        if (kr != KERN_SUCCESS) {
            mach_port_deallocate(mach_task_self(), g_exception_port);
            g_exception_port = MACH_PORT_NULL;
            return;
        }
    }

    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &attr, mach_exception_thread_main, nullptr);
    pthread_attr_destroy(&attr);

    // Best-effort note that we got installed. If the fd is open the
    // write happens; if not it's a silent no-op. We can't use MCLE_LOG
    // from this TU without bringing in the whole logging machinery.
    if (g_crash_log_fd >= 0) {
        const char* msg = "[mcle] MACH_EXC handler installed\n";
        ssize_t r = write(g_crash_log_fd, msg, strlen(msg));
        (void)r;
        fsync(g_crash_log_fd);
    }
}

} // extern "C"
