// Mach exception handler. Catches kernel-level exceptions BEFORE the
// kernel converts them into POSIX signals (or skips signals entirely
// for EXC_RESOURCE / EXC_GUARD / EXC_CRASH on iOS).
//
// v2: uses the system MIG-generated mach_exc_server() to decode the
// incoming exception message and format the reply correctly. The
// hand-rolled message ABI in v1 caused the kernel to leave the
// faulting thread suspended forever waiting for a reply it couldn't
// parse, producing a "frozen player" symptom where look-around (on
// the render thread) still worked but walking (on the sim thread)
// died. mach_exc_server is the only safe way to do this on iOS.
//
// Forward-to-original semantics: we save the previously-installed
// exception ports for our mask set, log the catch on our side, and
// then forward the exception to whatever was registered before us by
// calling mach_exception_raise on the saved port. The previous
// handler then runs (in practice this is the kernel's default which
// delivers a signal or SIGKILLs). If no previous handler exists, we
// return KERN_FAILURE and the kernel falls back to signal dispatch.
//
// The catch_mach_exception_raise_state* stubs return KERN_FAILURE
// because we register with EXCEPTION_DEFAULT (no state included),
// but mach_exc_server requires all three symbols to link.

#include <mach/mach.h>
#include <mach/exception_types.h>
#include <mach/mach_exc.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

extern "C" int g_crash_log_fd;

namespace {

// Saved exception ports from before we installed ours. catch_*_raise
// forwards to these so the kernel's lifecycle handling still runs.
exception_mask_t       g_old_masks[EXC_TYPES_COUNT];
exception_handler_t    g_old_ports[EXC_TYPES_COUNT];
exception_behavior_t   g_old_behaviors[EXC_TYPES_COUNT];
thread_state_flavor_t  g_old_flavors[EXC_TYPES_COUNT];
mach_msg_type_number_t g_old_count = 0;

mach_port_t g_exc_port = MACH_PORT_NULL;

// Pre-baked once at install. The Mach receive loop needs a buffer
// large enough for the biggest exception message we expect. 4 KiB is
// plenty for the MACH_EXCEPTION_CODES variant.
constexpr size_t kMsgBufSize = 4096;

size_t safe_itoa(char *buf, size_t cap, long v) {
    if (cap == 0) return 0;
    if (v < 0) {
        if (cap < 2) return 0;
        buf[0] = '-';
        return 1 + safe_itoa(buf + 1, cap - 1, -v);
    }
    char tmp[24]; size_t t = 0;
    if (v == 0) { tmp[t++] = '0'; }
    while (v > 0 && t < sizeof(tmp)) { tmp[t++] = '0' + (char)(v % 10); v /= 10; }
    size_t n = 0;
    while (t > 0 && n < cap) { buf[n++] = tmp[--t]; }
    return n;
}

size_t safe_hex(char *buf, size_t cap, uint64_t v) {
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

const char* exc_name(int t) {
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

void write_log(const char* prefix, exception_type_t exc,
               mach_exception_data_t code, mach_msg_type_number_t codeCnt) {
    if (g_crash_log_fd < 0) return;
    char buf[256]; size_t n = 0;
    size_t p = strlen(prefix);
    for (size_t i = 0; i < p && n < sizeof(buf); ++i) buf[n++] = prefix[i];
    const char* name = exc_name(exc);
    size_t nl = strlen(name);
    for (size_t i = 0; i < nl && n < sizeof(buf); ++i) buf[n++] = name[i];
    const char* mid = " type=";
    size_t ml = strlen(mid);
    for (size_t i = 0; i < ml && n < sizeof(buf); ++i) buf[n++] = mid[i];
    n += safe_itoa(buf + n, sizeof(buf) - n, exc);
    const char* c0s = " code0=";
    size_t cl0 = strlen(c0s);
    for (size_t i = 0; i < cl0 && n < sizeof(buf); ++i) buf[n++] = c0s[i];
    n += safe_hex(buf + n, sizeof(buf) - n,
                  codeCnt >= 1 ? (uint64_t)code[0] : 0);
    const char* c1s = " code1=";
    size_t cl1 = strlen(c1s);
    for (size_t i = 0; i < cl1 && n < sizeof(buf); ++i) buf[n++] = c1s[i];
    n += safe_hex(buf + n, sizeof(buf) - n,
                  codeCnt >= 2 ? (uint64_t)code[1] : 0);
    if (n < sizeof(buf)) buf[n++] = '\n';
    ssize_t r = write(g_crash_log_fd, buf, n);
    (void)r;
    fsync(g_crash_log_fd);
}

// Walk the saved-port table for one that registered for this exception.
// Returns MACH_PORT_NULL if no previous handler.
mach_port_t find_old_port_for(exception_type_t exc) {
    for (mach_msg_type_number_t i = 0; i < g_old_count; ++i) {
        if (g_old_masks[i] & (1u << exc)) {
            return g_old_ports[i];
        }
    }
    return MACH_PORT_NULL;
}

} // namespace

// ===========================================================================
// MIG callbacks. mach_exc_server() (from the system header
// mach/mach_exc.h) dispatches incoming messages to these. Symbol names
// must match exactly or the link fails.
// ===========================================================================

extern "C" kern_return_t catch_mach_exception_raise(
    mach_port_t /*exception_port*/,
    mach_port_t thread,
    mach_port_t task,
    exception_type_t exception,
    mach_exception_data_t code,
    mach_msg_type_number_t codeCnt)
{
    write_log("[mcle] MACH_EXC CAUGHT ", exception, code, codeCnt);

    // Forward to the previously-installed handler so the kernel's
    // normal lifecycle continues (signal dispatch / SIGKILL / etc).
    mach_port_t old_port = find_old_port_for(exception);
    if (old_port != MACH_PORT_NULL) {
        return mach_exception_raise(old_port, thread, task,
                                    exception, code, codeCnt);
    }
    // No previous handler. Returning KERN_FAILURE makes the kernel
    // walk its own fallback chain (typically signal dispatch).
    return KERN_FAILURE;
}

extern "C" kern_return_t catch_mach_exception_raise_state(
    mach_port_t /*exception_port*/,
    exception_type_t /*exception*/,
    const mach_exception_data_t /*code*/,
    mach_msg_type_number_t /*codeCnt*/,
    int * /*flavor*/,
    const thread_state_t /*old_state*/,
    mach_msg_type_number_t /*old_stateCnt*/,
    thread_state_t /*new_state*/,
    mach_msg_type_number_t * /*new_stateCnt*/)
{
    // We register with EXCEPTION_DEFAULT (no state). This shouldn't
    // get called, but the symbol must exist for mach_exc_server to
    // link.
    return KERN_FAILURE;
}

extern "C" kern_return_t catch_mach_exception_raise_state_identity(
    mach_port_t /*exception_port*/,
    mach_port_t /*thread*/,
    mach_port_t /*task*/,
    exception_type_t /*exception*/,
    mach_exception_data_t /*code*/,
    mach_msg_type_number_t /*codeCnt*/,
    int * /*flavor*/,
    thread_state_t /*old_state*/,
    mach_msg_type_number_t /*old_stateCnt*/,
    thread_state_t /*new_state*/,
    mach_msg_type_number_t * /*new_stateCnt*/)
{
    // Same as above - symbol required for link.
    return KERN_FAILURE;
}

// Forward decl. mach_exc_server is implemented by libsystem_kernel; the
// header is mach/mach_exc.h which we've already included above.
extern "C" boolean_t mach_exc_server(mach_msg_header_t *request,
                                       mach_msg_header_t *reply);

namespace {

void* exception_thread_main(void* /*unused*/) {
    while (true) {
        char req_buf[kMsgBufSize];
        char rep_buf[kMsgBufSize];
        mach_msg_header_t *req = reinterpret_cast<mach_msg_header_t*>(req_buf);
        mach_msg_header_t *rep = reinterpret_cast<mach_msg_header_t*>(rep_buf);

        req->msgh_local_port = g_exc_port;
        req->msgh_size = sizeof(req_buf);

        kern_return_t kr = mach_msg(
            req,
            MACH_RCV_MSG | MACH_RCV_LARGE,
            0,
            sizeof(req_buf),
            g_exc_port,
            MACH_MSG_TIMEOUT_NONE,
            MACH_PORT_NULL);

        if (kr != KERN_SUCCESS) {
            // Avoid busy-spin on persistent receive errors.
            usleep(10000);
            continue;
        }

        // Let MIG decode the message and call into our catch_*_raise
        // callback. It also formats the reply for us.
        if (mach_exc_server(req, rep)) {
            mach_msg(rep, MACH_SEND_MSG, rep->msgh_size, 0,
                     MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE,
                     MACH_PORT_NULL);
        }
    }
    return nullptr;
}

} // namespace

extern "C" void mcle_install_mach_exception_handler(void) {
    if (g_exc_port != MACH_PORT_NULL) return;

    kern_return_t kr;

    kr = mach_port_allocate(mach_task_self(),
                             MACH_PORT_RIGHT_RECEIVE,
                             &g_exc_port);
    if (kr != KERN_SUCCESS) {
        g_exc_port = MACH_PORT_NULL;
        return;
    }
    kr = mach_port_insert_right(mach_task_self(),
                                 g_exc_port,
                                 g_exc_port,
                                 MACH_MSG_TYPE_MAKE_SEND);
    if (kr != KERN_SUCCESS) {
        mach_port_deallocate(mach_task_self(), g_exc_port);
        g_exc_port = MACH_PORT_NULL;
        return;
    }

    exception_mask_t mask =
          EXC_MASK_BAD_ACCESS
        | EXC_MASK_BAD_INSTRUCTION
        | EXC_MASK_ARITHMETIC
        | EXC_MASK_BREAKPOINT
        | EXC_MASK_SOFTWARE
        | EXC_MASK_CRASH
        | EXC_MASK_RESOURCE
        | EXC_MASK_GUARD;

    // Save the previously-installed handlers for these masks so the
    // catch path can forward to them.
    g_old_count = EXC_TYPES_COUNT;
    kr = task_get_exception_ports(
        mach_task_self(), mask,
        g_old_masks, &g_old_count,
        g_old_ports, g_old_behaviors, g_old_flavors);
    if (kr != KERN_SUCCESS) {
        // Forwarding will degrade to KERN_FAILURE-fall-through.
        g_old_count = 0;
    }

    kr = task_set_exception_ports(
        mach_task_self(),
        mask,
        g_exc_port,
        EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES,
        THREAD_STATE_NONE);
    if (kr != KERN_SUCCESS) {
        // Some masks (EXC_MASK_CRASH especially) may require an
        // entitlement we don't have. Retry with the safer subset.
        mach_msg_type_number_t safe_count = EXC_TYPES_COUNT;
        exception_mask_t safe_mask =
              EXC_MASK_BAD_ACCESS
            | EXC_MASK_BAD_INSTRUCTION
            | EXC_MASK_ARITHMETIC
            | EXC_MASK_BREAKPOINT;
        task_get_exception_ports(
            mach_task_self(), safe_mask,
            g_old_masks, &safe_count,
            g_old_ports, g_old_behaviors, g_old_flavors);
        g_old_count = safe_count;
        kr = task_set_exception_ports(
            mach_task_self(),
            safe_mask,
            g_exc_port,
            EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES,
            THREAD_STATE_NONE);
        if (kr != KERN_SUCCESS) {
            mach_port_deallocate(mach_task_self(), g_exc_port);
            g_exc_port = MACH_PORT_NULL;
            g_old_count = 0;
            return;
        }
    }

    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &attr, exception_thread_main, nullptr);
    pthread_attr_destroy(&attr);

    if (g_crash_log_fd >= 0) {
        const char* msg = "[mcle] MACH_EXC handler installed\n";
        ssize_t r = write(g_crash_log_fd, msg, strlen(msg));
        (void)r;
        fsync(g_crash_log_fd);
    }
}
