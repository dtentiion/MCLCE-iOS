// iOS_Threading.cpp - real pthread + cond implementations behind the
// Win32 threading shim. Used to make C4JThread::Event and worker-
// thread spawn paths in upstream (LevelRenderer rebuild threads etc)
// actually do work on iOS.

#include "iOS_Threading.h"

#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>

namespace {

// First field of every handle struct so mcle_handle_wait can dispatch
// without UB. Picked arbitrary 32-bit constants that won't collide
// with random heap bytes.
constexpr uint32_t kMagicEvent  = 0x4D45564Eu; // 'MEVN'
constexpr uint32_t kMagicThread = 0x4D544854u; // 'MTHT'

constexpr unsigned int kWaitObject0 = 0x00000000u;
constexpr unsigned int kWaitTimeout = 258u;
constexpr unsigned int kWaitFailed  = 0xFFFFFFFFu;
constexpr unsigned int kInfinite    = 0xFFFFFFFFu;

struct EventHandle {
    uint32_t        magic;
    pthread_mutex_t mu;
    pthread_cond_t  cv;
    bool            signaled;
    bool            manualReset;
};

struct ThreadHandle {
    uint32_t            magic;
    pthread_t           tid;
    MCLE_ThreadStartFn  startFn;
    void*               arg;
    bool                started;
    bool                exited;
    unsigned int        exitCode;
    pthread_mutex_t     joinMu;
    pthread_cond_t      joinCv;
};

void compute_deadline(struct timespec* out, unsigned int timeoutMs) {
    struct timeval now;
    gettimeofday(&now, nullptr);
    long sec = now.tv_sec + (long)(timeoutMs / 1000u);
    long nsec = now.tv_usec * 1000L + (long)(timeoutMs % 1000u) * 1000000L;
    if (nsec >= 1000000000L) { sec += 1; nsec -= 1000000000L; }
    out->tv_sec = sec;
    out->tv_nsec = nsec;
}

void* thread_entry(void* raw) {
    auto* t = static_cast<ThreadHandle*>(raw);
    unsigned int code = 0;
    if (t->startFn) code = t->startFn(t->arg);
    pthread_mutex_lock(&t->joinMu);
    t->exitCode = code;
    t->exited   = true;
    pthread_cond_broadcast(&t->joinCv);
    pthread_mutex_unlock(&t->joinMu);
    return nullptr;
}

} // namespace

extern "C" void* mcle_event_create(int manualReset, int initialSignaled) {
    auto* e = new EventHandle;
    e->magic = kMagicEvent;
    pthread_mutex_init(&e->mu, nullptr);
    pthread_cond_init(&e->cv, nullptr);
    e->signaled    = (initialSignaled != 0);
    e->manualReset = (manualReset != 0);
    return e;
}

extern "C" void mcle_event_destroy(void* handle) {
    auto* e = static_cast<EventHandle*>(handle);
    if (!e || e->magic != kMagicEvent) return;
    pthread_cond_destroy(&e->cv);
    pthread_mutex_destroy(&e->mu);
    e->magic = 0;
    delete e;
}

extern "C" void mcle_event_set(void* handle) {
    auto* e = static_cast<EventHandle*>(handle);
    if (!e || e->magic != kMagicEvent) return;
    pthread_mutex_lock(&e->mu);
    e->signaled = true;
    if (e->manualReset) pthread_cond_broadcast(&e->cv);
    else                pthread_cond_signal(&e->cv);
    pthread_mutex_unlock(&e->mu);
}

extern "C" void mcle_event_reset(void* handle) {
    auto* e = static_cast<EventHandle*>(handle);
    if (!e || e->magic != kMagicEvent) return;
    pthread_mutex_lock(&e->mu);
    e->signaled = false;
    pthread_mutex_unlock(&e->mu);
}

extern "C" unsigned int mcle_event_wait(void* handle, unsigned int timeoutMs) {
    auto* e = static_cast<EventHandle*>(handle);
    if (!e || e->magic != kMagicEvent) return kWaitFailed;
    pthread_mutex_lock(&e->mu);
    unsigned int rv = kWaitObject0;
    if (timeoutMs == kInfinite) {
        while (!e->signaled) {
            pthread_cond_wait(&e->cv, &e->mu);
        }
    } else if (timeoutMs == 0) {
        if (!e->signaled) rv = kWaitTimeout;
    } else {
        struct timespec deadline;
        compute_deadline(&deadline, timeoutMs);
        while (!e->signaled) {
            int r = pthread_cond_timedwait(&e->cv, &e->mu, &deadline);
            if (r == ETIMEDOUT) { rv = kWaitTimeout; break; }
        }
    }
    if (rv == kWaitObject0 && !e->manualReset) {
        e->signaled = false;
    }
    pthread_mutex_unlock(&e->mu);
    return rv;
}

extern "C" void* mcle_thread_create(MCLE_ThreadStartFn startFn, void* arg,
                                     unsigned int stackSize, int startSuspended,
                                     unsigned int* outId) {
    auto* t = new ThreadHandle;
    t->magic    = kMagicThread;
    t->startFn  = startFn;
    t->arg      = arg;
    t->started  = false;
    t->exited   = false;
    t->exitCode = 0;
    pthread_mutex_init(&t->joinMu, nullptr);
    pthread_cond_init(&t->joinCv, nullptr);
    if (outId) *outId = 0;
    if (!startSuspended) {
        // Honor explicit stack size when given; otherwise let pthread
        // pick a sensible default.
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        if (stackSize > 0) {
            // Round up to PTHREAD_STACK_MIN.
            pthread_attr_setstacksize(&attr, stackSize < 16 * 1024 ? 16 * 1024 : stackSize);
        }
        pthread_create(&t->tid, &attr, thread_entry, t);
        pthread_attr_destroy(&attr);
        t->started = true;
    }
    return t;
}

extern "C" unsigned int mcle_thread_resume(void* handle) {
    auto* t = static_cast<ThreadHandle*>(handle);
    if (!t || t->magic != kMagicThread) return 0;
    if (t->started) return 0;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_create(&t->tid, &attr, thread_entry, t);
    pthread_attr_destroy(&attr);
    t->started = true;
    return 0;
}

extern "C" unsigned int mcle_thread_join(void* handle, unsigned int timeoutMs) {
    auto* t = static_cast<ThreadHandle*>(handle);
    if (!t || t->magic != kMagicThread) return kWaitFailed;
    pthread_mutex_lock(&t->joinMu);
    unsigned int rv = kWaitObject0;
    if (timeoutMs == kInfinite) {
        while (!t->exited) pthread_cond_wait(&t->joinCv, &t->joinMu);
    } else if (timeoutMs == 0) {
        if (!t->exited) rv = kWaitTimeout;
    } else {
        struct timespec deadline;
        compute_deadline(&deadline, timeoutMs);
        while (!t->exited) {
            int r = pthread_cond_timedwait(&t->joinCv, &t->joinMu, &deadline);
            if (r == ETIMEDOUT) { rv = kWaitTimeout; break; }
        }
    }
    pthread_mutex_unlock(&t->joinMu);
    return rv;
}

extern "C" void mcle_thread_destroy(void* handle) {
    auto* t = static_cast<ThreadHandle*>(handle);
    if (!t || t->magic != kMagicThread) return;
    pthread_cond_destroy(&t->joinCv);
    pthread_mutex_destroy(&t->joinMu);
    t->magic = 0;
    delete t;
}

extern "C" unsigned int mcle_handle_wait(void* handle, unsigned int timeoutMs) {
    if (!handle) return kWaitFailed;
    uint32_t magic = *static_cast<uint32_t*>(handle);
    if (magic == kMagicEvent)  return mcle_event_wait(handle, timeoutMs);
    if (magic == kMagicThread) return mcle_thread_join(handle, timeoutMs);
    return kWaitFailed;
}
