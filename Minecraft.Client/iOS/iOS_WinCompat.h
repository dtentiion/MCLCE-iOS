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
typedef uint32_t DWORD;
typedef uint64_t QWORD;
typedef int32_t  LONG;
typedef uint32_t ULONG;
typedef int64_t  LONGLONG;
typedef uint64_t ULONGLONG;

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

#endif // !_WIN32 && !_WIN64
