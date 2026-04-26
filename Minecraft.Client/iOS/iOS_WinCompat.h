// Portable fallbacks for the handful of Win32 typedefs that leak through
// upstream public headers (4J_Input.h, 4J_Storage.h, etc.) when compiled on
// non-Windows platforms.
//
// Include this BEFORE any upstream header that expects <windows.h> to be
// available. Keep this list in sync with upstream includes: every new type
// the build complains about goes here.

#pragma once

#if !defined(_WIN32) && !defined(_WIN64)

#include <stdint.h>
#include <stddef.h>
#include <wchar.h>
#include <pthread.h>
#include <mach/mach_time.h>

// Basic integer aliases.
typedef int8_t   INT8;
typedef uint8_t  UINT8;
typedef int16_t  INT16;
typedef uint16_t UINT16;
typedef int32_t  INT32;
typedef uint32_t UINT32;
typedef int64_t  INT64;
typedef uint64_t UINT64;

typedef uint8_t  BYTE;
typedef uint8_t* PBYTE;
typedef uint8_t* LPBYTE;
typedef int8_t   CHAR;
typedef uint8_t  UCHAR;
typedef uint16_t WORD;
typedef int16_t  SHORT;
typedef uint16_t USHORT;
typedef uint32_t DWORD;
typedef uint64_t QWORD;
typedef int32_t  LONG;
typedef uint32_t ULONG;
typedef int64_t  LONGLONG;
typedef uint64_t ULONGLONG;

// MSVC integer aliases. __int64 is widely used in upstream IO code
// (ByteArrayInputStream, FileInputStream) instead of long long /
// int64_t. clang on iOS does not predefine these names.
typedef int64_t  __int64;
typedef uint64_t __uint64;
typedef int32_t  __int32;
typedef uint32_t __uint32;
typedef int16_t  __int16;
typedef uint16_t __uint16;
typedef int8_t   __int8;
typedef uint8_t  __uint8;

// BOOL: Apple's Objective-C runtime defines BOOL as signed char. Only define
// the Windows-style int BOOL when Obj-C is NOT active (i.e. pure C++ TUs that
// are compiling upstream code that expects int BOOL semantics).
#ifndef __OBJC__
typedef int32_t  BOOL;
#endif
typedef int32_t  INT;
typedef uint32_t UINT;

typedef float    FLOAT;

// Pointer-sized integers. Matches the Win32 semantic meaning; size depends on
// the target arch. iOS is always 64-bit, so these are all 64-bit.
typedef size_t    SIZE_T;
typedef ssize_t   SSIZE_T;
typedef uintptr_t ULONG_PTR;
typedef intptr_t  LONG_PTR;
typedef uintptr_t DWORD_PTR;

// VOID is Microsoft's all-caps spelling of void. Used in function pointer
// typedefs and callbacks in upstream headers.
#ifndef VOID
#  define VOID void
#endif
typedef void* PVOID;

// File-time related. The upstream uses FILETIME as an opaque 64-bit stamp in
// most places. A struct of two DWORDs keeps binary layout identical to the
// Windows declaration so sizeof checks do not break.
typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;

typedef struct _SYSTEMTIME {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;

// Lightweight stand-in for CRITICAL_SECTION. Backed by a pthread mutex so the
// Enter/Leave helpers in upstream code that the compat shims will eventually
// provide can delegate here. If upstream ever relies on the exact field
// layout, this will need revisiting; it does not today.
typedef struct _CRITICAL_SECTION {
    pthread_mutex_t _mu;
    // Padding so sizeof matches something reasonable on 64-bit targets.
    // Not binary-compatible with Win32 and not meant to be.
    char _pad[8];
} CRITICAL_SECTION, *PCRITICAL_SECTION, *LPCRITICAL_SECTION;

// String-ish.
typedef wchar_t  WCHAR;
typedef const wchar_t* LPCWSTR;
typedef wchar_t*       LPWSTR;
typedef const char*    LPCSTR;
typedef char*          LPSTR;

// Handle-ish.
typedef void*       HANDLE;
typedef void*       HWND;
typedef void*       HINSTANCE;
typedef HINSTANCE   HMODULE;
typedef void*       LPVOID;
typedef const void* LPCVOID;

// HRESULT convention. The upstream headers only use this for pointer-returning
// callbacks so the exact bit layout does not matter for compile-time use.
typedef int32_t HRESULT;

#ifndef S_OK
#  define S_OK ((HRESULT)0)
#endif
#ifndef E_FAIL
#  define E_FAIL ((HRESULT)-1)
#endif
#ifndef SUCCEEDED
#  define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#endif
#ifndef FAILED
#  define FAILED(hr)    (((HRESULT)(hr)) < 0)
#endif

// Macros that upstream headers sometimes expect to exist.
#ifndef TRUE
#  define TRUE  1
#endif
#ifndef FALSE
#  define FALSE 0
#endif

#ifndef IN
#  define IN
#endif
#ifndef OUT
#  define OUT
#endif
#ifndef CONST
#  define CONST const
#endif

// Calling-convention noise. On non-Windows these are no-ops.
#ifndef WINAPI
#  define WINAPI
#endif
#ifndef CALLBACK
#  define CALLBACK
#endif
#ifndef __stdcall
#  define __stdcall
#endif
#ifndef __cdecl
#  define __cdecl
#endif

// MSVC debug-break intrinsic. Upstream uses __debugbreak() to assert
// invariants in debug builds (CompoundTag.h:39 etc). On clang we map
// to __builtin_trap() so a hit still aborts the process under a
// debugger.
#ifndef __debugbreak
#  define __debugbreak() __builtin_trap()
#endif

// Win32 macros for zeroing / copying memory. Upstream uses these in
// FileHeader.h and similar serialization code. memset / memcpy from
// <cstring> (already in iOS_stdafx.h) cover both.
#ifndef ZeroMemory
#  define ZeroMemory(dest, size)       memset((dest), 0, (size))
#endif
// XMemCpy is the Xbox SDK alias for memcpy. Used in upstream IO code
// (ByteArrayInputStream etc) for what is otherwise a plain memcpy.
#ifndef XMemCpy
#  define XMemCpy(dest, src, size)     memcpy((dest), (src), (size))
#endif

// PIX (Performance Investigator for Xbox) profiling macros. Upstream
// uses these to mark named timing regions in chunk update / world
// gen / etc. No-op everywhere off-Xbox.
#ifndef PIXBeginNamedEvent
#  define PIXBeginNamedEvent(color, name, ...) ((void)0)
#endif
#ifndef PIXEndNamedEvent
#  define PIXEndNamedEvent() ((void)0)
#endif
#ifndef CopyMemory
#  define CopyMemory(dest, src, size)  memcpy((dest), (src), (size))
#endif
#ifndef MoveMemory
#  define MoveMemory(dest, src, size)  memmove((dest), (src), (size))
#endif
#ifndef FillMemory
#  define FillMemory(dest, size, fill) memset((dest), (fill), (size))
#endif

// LARGE_INTEGER is a 64-bit signed value with a union that exposes
// the high / low 32-bit halves. Upstream PerformanceTimer.h uses
// QuadPart for QueryPerformanceCounter timestamps.
#ifndef _LARGE_INTEGER_DEFINED
#define _LARGE_INTEGER_DEFINED
typedef union _LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG  HighPart;
    } u;
    struct {
        DWORD LowPart;
        LONG  HighPart;
    };
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef union _ULARGE_INTEGER {
    struct {
        DWORD LowPart;
        DWORD HighPart;
    } u;
    struct {
        DWORD LowPart;
        DWORD HighPart;
    };
    ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;
#endif // _LARGE_INTEGER_DEFINED

// Thread Local Storage Win32 API. Upstream code (Vec3.cpp, AABB.cpp,
// pool allocators) uses TlsAlloc / TlsGetValue / TlsSetValue / TlsFree
// to keep per-thread allocation arenas. On POSIX we back these with
// pthread keys so the upstream code compiles unchanged. The pthread
// key fits in a DWORD on iOS / macOS (unsigned long under the hood,
// truncated for the Win32 API surface). TLS_OUT_OF_INDEXES is the
// Win32 sentinel for allocation failure.

#ifndef TLS_OUT_OF_INDEXES
#  define TLS_OUT_OF_INDEXES ((DWORD)0xFFFFFFFFu)
#endif

static inline DWORD TlsAlloc(void) {
    pthread_key_t key;
    if (pthread_key_create(&key, NULL) != 0) {
        return TLS_OUT_OF_INDEXES;
    }
    return (DWORD)key;
}

static inline BOOL TlsFree(DWORD index) {
    return pthread_key_delete((pthread_key_t)index) == 0 ? TRUE : FALSE;
}

static inline LPVOID TlsGetValue(DWORD index) {
    return pthread_getspecific((pthread_key_t)index);
}

static inline BOOL TlsSetValue(DWORD index, LPVOID value) {
    return pthread_setspecific((pthread_key_t)index, value) == 0 ? TRUE : FALSE;
}

// Critical sections backed by pthread mutexes. Upstream uses these
// for the global-lock pattern around C4JThread, command dispatch,
// etc. Compile-only support: we only need calls to typecheck. The
// CRITICAL_SECTION type is already declared in this header above.
static inline void InitializeCriticalSection(LPCRITICAL_SECTION cs) {
    if (cs) pthread_mutex_init(&cs->_mu, NULL);
}
static inline void DeleteCriticalSection(LPCRITICAL_SECTION cs) {
    if (cs) pthread_mutex_destroy(&cs->_mu);
}
static inline void EnterCriticalSection(LPCRITICAL_SECTION cs) {
    if (cs) pthread_mutex_lock(&cs->_mu);
}
static inline void LeaveCriticalSection(LPCRITICAL_SECTION cs) {
    if (cs) pthread_mutex_unlock(&cs->_mu);
}
static inline BOOL TryEnterCriticalSection(LPCRITICAL_SECTION cs) {
    return (cs && pthread_mutex_trylock(&cs->_mu) == 0) ? TRUE : FALSE;
}

// Xbox / 4J-platform user-slot constants. Upstream code uses these
// at file scope in struct sizes (per-player arrays). Real values
// match the Xbox 360 SDK and the 4J Vita override (1).
#ifndef XUSER_MAX_COUNT
#  ifdef __PSVITA__
#    define XUSER_MAX_COUNT 1
#  else
#    define XUSER_MAX_COUNT 4
#  endif
#endif
#ifndef XUSER_INDEX_ANY
#  define XUSER_INDEX_ANY 255
#endif
#ifndef MAX_PATH
#  define MAX_PATH 260
#endif
#ifndef MAX_PATH_SIZE
#  define MAX_PATH_SIZE 256
#endif

// Durango (Xbox One) string verify return shape. SignTileEntity etc
// reference this type via 4J_Input.h. Stub small enough to satisfy
// declarations.
typedef struct _STRING_VERIFY_RESPONSE {
    int             dwResult;
    DWORD           cchString;
    const WCHAR*    pszString;
} STRING_VERIFY_RESPONSE;

// Xbox-style invalid XUID sentinel.
#ifndef INVALID_XUID
#  define INVALID_XUID ((uint64_t)0xFFFFFFFFFFFFFFFFull)
#endif

// PLONG = long*. MS pointer-aliased.
#ifndef _PLONG_DEFINED
#define _PLONG_DEFINED
typedef LONG* PLONG;
#endif

// GetTickCount returns ms since boot. Tie to mach for iOS.
static inline DWORD GetTickCount(void) {
    static mach_timebase_info_data_t s_tb = {0, 0};
    if (s_tb.denom == 0) mach_timebase_info(&s_tb);
    uint64_t t = mach_absolute_time();
    if (s_tb.denom != 0) t = (t * s_tb.numer) / s_tb.denom;
    return (DWORD)(t / 1000000ull);
}

// Win32 high-resolution timer API. Random.cpp seeds the RNG with
// QueryPerformanceCounter; PerformanceTimer.cpp uses both. On Apple
// platforms mach_absolute_time gives us a high-res monotonic counter
// in mach-time units, convertible to nanoseconds via mach_timebase_info.
// We expose these as nanosecond counts (frequency = 1e9) so QuadPart
// values across calls yield meaningful ns deltas without callers caring
// about the unit. (mach/mach_time.h included at top of this header.)

static inline BOOL QueryPerformanceFrequency(LARGE_INTEGER *out) {
    if (!out) return FALSE;
    out->QuadPart = 1000000000LL;  // 1 GHz pretend-frequency = nanoseconds
    return TRUE;
}

static inline BOOL QueryPerformanceCounter(LARGE_INTEGER *out) {
    if (!out) return FALSE;
    static mach_timebase_info_data_t s_tb = {0, 0};
    if (s_tb.denom == 0) {
        mach_timebase_info(&s_tb);
    }
    uint64_t mach_ns = mach_absolute_time();
    if (s_tb.denom != 0) {
        mach_ns = (mach_ns * s_tb.numer) / s_tb.denom;
    }
    out->QuadPart = (LONGLONG)mach_ns;
    return TRUE;
}

#endif // !_WIN32 && !_WIN64
