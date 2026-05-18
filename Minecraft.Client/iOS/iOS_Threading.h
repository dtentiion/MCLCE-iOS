// iOS_Threading.h - pthread/cond-backed Win32 threading + event shim.
//
// Upstream (LevelRenderer, ServerChunkCache, GameNetworkManager, etc.)
// uses Win32 CreateThread / CreateEventA / SetEvent / ResetEvent /
// WaitForSingleObject / ResumeThread / TerminateThread to drive worker
// threads. The HANDLE type is opaque (void*), so we back each handle
// with our own heap struct guarded by a magic tag so WaitForSingleObject
// can disambiguate event waits from thread joins.
//
// CREATE_SUSPENDED is honored by deferring pthread_create until
// ResumeThread is called. C4JThread.cpp constructs threads suspended
// then allocates their activation event in between, so spawning the
// pthread before Run() races against null event pointers.

#pragma once

// Forward-declared as extern "C" so the static inline shims in
// iOS_WinCompat.h can call into the .cpp without needing the full
// pthread headers everywhere.
#ifdef __cplusplus
extern "C" {
#endif

void* mcle_event_create(int manualReset, int initialSignaled);
void  mcle_event_destroy(void* handle);
void  mcle_event_set(void* handle);
void  mcle_event_reset(void* handle);
unsigned int mcle_event_wait(void* handle, unsigned int timeoutMs);

typedef unsigned int (*MCLE_ThreadStartFn)(void*);
void* mcle_thread_create(MCLE_ThreadStartFn startFn, void* arg,
                          unsigned int stackSize, int startSuspended,
                          unsigned int* outId);
unsigned int mcle_thread_resume(void* handle);
unsigned int mcle_thread_join(void* handle, unsigned int timeoutMs);
void  mcle_thread_destroy(void* handle);

// WaitForSingleObject dispatch helper - inspects the handle's magic
// tag and routes to the event or thread wait path.
unsigned int mcle_handle_wait(void* handle, unsigned int timeoutMs);

#ifdef __cplusplus
}
#endif
