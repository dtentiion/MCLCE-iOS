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
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <pthread.h>
#include <mach/mach_time.h>

#ifdef __cplusplus
#include <string>
#endif

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

// Probe-only stubs. The main app build pulls real Win64 4J_Input.h
// which has its own STRING_VERIFY_RESPONSE definition; only the
// probe target has these defines.
#ifdef MCLE_PROBE_BUILD
typedef struct _STRING_VERIFY_RESPONSE {
    int             dwResult;
    DWORD           cchString;
    const WCHAR*    pszString;
} STRING_VERIFY_RESPONSE;
#endif

// Xbox-style invalid XUID sentinel.
#ifndef INVALID_XUID
#  define INVALID_XUID ((uint64_t)0xFFFFFFFFFFFFFFFFull)
#endif

// Net protocol cap. Real value differs per platform (PS4/Switch use
// 16, X1 uses 8). Probe never sends/receives so the constant just
// needs to exist and be reasonable.
#ifndef MINECRAFT_NET_MAX_PLAYERS
#  define MINECRAFT_NET_MAX_PLAYERS 8
#endif

// Console world-size constants from upstream Minecraft.Client/MinecraftServer.h.
// Real values match the Console Edition's legacy 864-block worlds and
// classic 256-block worlds. Used as compile-time array sizes / loop
// bounds so they need to be the right magnitude.
#ifndef LEVEL_LEGACY_WIDTH
#  define LEVEL_LEGACY_WIDTH    864
#endif
#ifndef LEVEL_WIDTH_CLASSIC
#  define LEVEL_WIDTH_CLASSIC   256
#endif
#ifndef LEVEL_MAX_WIDTH
#  define LEVEL_MAX_WIDTH       1024
#endif

// Console nether-scale ratio for legacy worlds.
#ifndef HELL_LEVEL_LEGACY_SCALE
#  define HELL_LEVEL_LEGACY_SCALE 3
#endif

// Console world-size SMALL/MEDIUM constants; used alongside LEGACY/
// CLASSIC in world picker / server bounds.
#ifndef LEVEL_WIDTH_SMALL
#  define LEVEL_WIDTH_SMALL     384
#endif
#ifndef LEVEL_WIDTH_MEDIUM
#  define LEVEL_WIDTH_MEDIUM    640
#endif

// Console nether-scale ratio - max value used for size cap.
#ifndef HELL_LEVEL_MAX_SCALE
#  define HELL_LEVEL_MAX_SCALE  4
#endif

// Win32 wait sentinel - infinite timeout.
#ifndef INFINITE
#  define INFINITE              0xFFFFFFFF
#endif

// Win32 file open hints. Probe never opens a real file.
#ifndef FILE_FLAG_SEQUENTIAL_SCAN
#  define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#endif
#ifndef FILE_FLAG_RANDOM_ACCESS
#  define FILE_FLAG_RANDOM_ACCESS   0x10000000
#endif

// Net protocol revision. Probe never sends a packet; constant just
// needs to exist for compile.
#ifndef MINECRAFT_NET_VERSION
#  define MINECRAFT_NET_VERSION 0
#endif

// Win32 thread-exit-code sentinel for "still running". Probe never
// inspects the value.
#ifndef STILL_ACTIVE
#  define STILL_ACTIVE          259
#endif

// Win32 file-share mask flags. Probe never opens files; constants
// just need to exist for compile.
#ifndef GENERIC_READ
#  define GENERIC_READ          0x80000000
#endif
#ifndef GENERIC_WRITE
#  define GENERIC_WRITE         0x40000000
#endif
#ifndef OPEN_EXISTING
#  define OPEN_EXISTING         3
#endif
#ifndef OPEN_ALWAYS
#  define OPEN_ALWAYS           4
#endif
#ifndef CREATE_NEW
#  define CREATE_NEW            1
#endif
#ifndef CREATE_ALWAYS
#  define CREATE_ALWAYS         2
#endif
#ifndef FILE_ATTRIBUTE_NORMAL
#  define FILE_ATTRIBUTE_NORMAL 0x00000080
#endif
#ifndef FILE_SHARE_READ
#  define FILE_SHARE_READ       0x00000001
#endif

// Xbox-style memset intrinsics. Real platforms use SIMD-aligned memset.
// Map to the standard memset on iOS - probe never executes these.
#ifndef XMemSet
#  define XMemSet(dst, val, len)    memset((dst), (val), (len))
#endif
#ifndef XMemSet128
#  define XMemSet128(dst, val, len) memset((dst), (val), (len))
#endif

// Win32 secure-CRT printf variants. Upstream uses these for thread-name
// formatting and a couple of debug strings. Map to vsnprintf - probe
// only needs the symbols to exist.
#include <stdio.h>
#ifndef sprintf_s
#  define sprintf_s(buf, sz, ...)   snprintf((buf), (sz), __VA_ARGS__)
#endif
#ifndef swprintf_s
#  define swprintf_s(buf, sz, ...)  swprintf((buf), (sz), __VA_ARGS__)
#endif

// Win32 thread error reporting. iOS exposes errno; for the probe a
// constant zero satisfies the call sites.
static inline DWORD GetLastError(void) { return 0; }
static inline void  SetLastError(DWORD) {}

// Win32 thread sleep. iOS exposes usleep; map ms to us. Probe never
// runs the call.
#include <unistd.h>
static inline void Sleep(DWORD ms) { usleep(ms * 1000u); }

// Wide string in-place find/replace helper. StringHelpers exposes
// `replaceAll` on other platforms; the probe target needs the symbol
// for compile but never executes the path.
#ifdef __cplusplus
static inline void replaceAll(std::wstring& s, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::wstring::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}
static inline void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}
#endif

// Win32 atomic intrinsics for upstream's lock-free paths. iOS clang
// supports the GCC __sync builtins which match the semantics. The
// _Release variants drop the full barrier - probe never races so any
// of the __sync flavours works.
#ifndef InterlockedCompareExchangeRelease64
#  define InterlockedCompareExchangeRelease64(dst, exch, comp) \
       __sync_val_compare_and_swap((volatile long long*)(dst), (long long)(comp), (long long)(exch))
#endif
#ifndef InterlockedCompareExchange64
#  define InterlockedCompareExchange64(dst, exch, comp) \
       __sync_val_compare_and_swap((volatile long long*)(dst), (long long)(comp), (long long)(exch))
#endif
#ifndef InterlockedIncrement
#  define InterlockedIncrement(p)   __sync_add_and_fetch((p), 1)
#endif
#ifndef InterlockedDecrement
#  define InterlockedDecrement(p)   __sync_sub_and_fetch((p), 1)
#endif

// Win32 spin-count critical-section initializer. The spin parameter
// is a Win-only optimization; on iOS we just init like a regular CS.
static inline BOOL InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION cs, DWORD) {
    if (cs) pthread_mutex_init(&cs->_mu, NULL);
    return TRUE;
}

// Wide string equalsIgnoreCase helper. AnvilMenu uses it to compare
// the user-typed item name with the existing hover name. Probe never
// runs the rename path; an exact-equality fallback typechecks fine.
#ifdef __cplusplus
static inline bool equalsIgnoreCase(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        wchar_t ca = a[i], cb = b[i];
        if (ca >= L'A' && ca <= L'Z') ca = (wchar_t)(ca + 32);
        if (cb >= L'A' && cb <= L'Z') cb = (wchar_t)(cb + 32);
        if (ca != cb) return false;
    }
    return true;
}
#endif

// CRT integer-to-wide-string. Upstream uses for HUD numerics. Map to
// swprintf - the probe never inspects the buffer.
static inline wchar_t* _itow(int v, wchar_t* buf, int radix) {
    if (!buf) return buf;
    if (radix == 16) swprintf(buf, 12, L"%x", v);
    else             swprintf(buf, 12, L"%d", v);
    return buf;
}

// PLONG = long*. MS pointer-aliased.
#ifndef _PLONG_DEFINED
#define _PLONG_DEFINED
typedef LONG*  PLONG;
typedef DWORD* LPDWORD;
typedef DWORD* PDWORD;
typedef WORD*  LPWORD;
typedef WORD*  PWORD;
typedef BYTE*  PBYTE2;
#endif

// Win32 BOOL aliased "boolean" (ms-style lowercase wide). Some
// upstream code uses both interchangeably.
#ifndef __OBJC__
typedef int boolean;
#endif

// Xbox concurrent free-list stack template. Used in upstream
// SparseLightStorage as a thread-safe stack. Stub: empty class
// templated on T - methods absorbed by variadic template.
template<class T> class XLockFreeStack {
public:
    template<class... A> XLockFreeStack(A...) {}
    template<class... A> void Initialize(A...) {}
    template<class... A> bool push(A...)    { return false; }
    template<class... A> bool Push(A...)    { return false; }
    template<class... A> T*   pop(A...)     { return nullptr; }
    template<class... A> T*   Pop(A...)     { return nullptr; }
    template<class... A> bool empty(A...) const { return true; }
};

// Xbox memcompression contexts. Used by compression.cpp's PE-only
// branch which we do not enter on iOS, but the type still needs
// to exist for header parses. Opaque struct fits.
typedef struct _XMEMCOMPRESSION_CONTEXT { int dummy; } XMEMCOMPRESSION_CONTEXT;
typedef struct _XMEMDECOMPRESSION_CONTEXT { int dummy; } XMEMDECOMPRESSION_CONTEXT;

// Win32 SetFilePointer move-method constants. Upstream RegionFile /
// ConsoleSaveFile* use them via the platform setFilePointer abstraction.
#ifndef FILE_BEGIN
#  define FILE_BEGIN   0
#endif
#ifndef FILE_CURRENT
#  define FILE_CURRENT 1
#endif
#ifndef FILE_END
#  define FILE_END     2
#endif

// Win32 invalid handle sentinel. FileOutputStream / FileInputStream use
// it as the unset-state marker for HANDLE-typed members.
#ifndef INVALID_HANDLE_VALUE
#  define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#endif

// Win32 SetFilePointer entry. Probe stub; never called.
// Placed after PLONG/INVALID_HANDLE_VALUE/LPDWORD declarations above.
static inline DWORD SetFilePointer(HANDLE, LONG distLow, PLONG distHigh, DWORD) {
    if (distHigh) *distHigh = 0;
    return (DWORD)distLow;
}

// Win32 CreateFile / WriteFile / ReadFile / CloseHandle entries.
// Required for compile of File.cpp / RegionFile.cpp on the Console branch.
#ifndef _IOS_FILE_API_DECLARED
#define _IOS_FILE_API_DECLARED
static inline HANDLE CreateFileA(const char*, DWORD, DWORD, void*, DWORD, DWORD, HANDLE) {
    return INVALID_HANDLE_VALUE;
}
static inline HANDLE CreateFileW(const wchar_t*, DWORD, DWORD, void*, DWORD, DWORD, HANDLE) {
    return INVALID_HANDLE_VALUE;
}
#  ifdef UNICODE
#    define CreateFile CreateFileW
#  else
#    define CreateFile CreateFileA
#  endif
static inline BOOL ReadFile(HANDLE, void*, DWORD, LPDWORD got, void*) {
    if (got) *got = 0; return FALSE;
}
static inline BOOL WriteFile(HANDLE, const void*, DWORD, LPDWORD wrote, void*) {
    if (wrote) *wrote = 0; return FALSE;
}
static inline BOOL CloseHandle(HANDLE) { return TRUE; }
static inline BOOL DeleteFileA(const char*) { return TRUE; }
static inline BOOL DeleteFileW(const wchar_t*) { return TRUE; }
static inline DWORD GetFileSize(HANDLE, LPDWORD high) {
    if (high) *high = 0; return 0;
}
#  ifdef UNICODE
#    define DeleteFile DeleteFileW
#  else
#    define DeleteFile DeleteFileA
#  endif
#endif

// MS pointer-size max. Used as the third arg to XPhysicalAlloc by
// SparseDataStorage / CompressedTileStorage. Real value matches
// MAXSIZE_T on Win64.
#ifndef MAXULONG_PTR
#  define MAXULONG_PTR ((unsigned long long)-1)
#endif

// Win32 page-protect flag. Upstream XPhysicalAlloc calls pass
// PAGE_READWRITE; the macro just needs to expand to something.
#ifndef PAGE_READWRITE
#  define PAGE_READWRITE 0x04
#endif

// Xbox-style physical-memory allocator. Real one returns aligned
// pages; on iOS we fall back to malloc since the upstream code
// already accepts that fallback (PS3 / Vita branches do the same).
static inline void* XPhysicalAlloc(size_t size, unsigned long long, unsigned long, unsigned long) {
    return malloc(size);
}
static inline void XPhysicalFree(void* p) { free(p); }

// Xbox-style memory compression entry points. Probe never executes
// these (compression.cpp's iOS branch uses zlib via the patched
// path) but the call sites need to typecheck.
static inline long XMemCompress(void*, void*, size_t*, const void*, size_t) { return 0; }
static inline long XMemDecompress(void*, void*, size_t*, const void*, size_t) { return 0; }
static inline long XMemCreateCompressionContext(unsigned long, void*, unsigned long, void**) { return 0; }
static inline long XMemCreateDecompressionContext(unsigned long, void*, unsigned long, void**) { return 0; }
static inline void XMemDestroyCompressionContext(void*) {}
static inline void XMemDestroyDecompressionContext(void*) {}

// File.cpp / FileInputStream.cpp use this Win32 helper to convert a
// wstring path into an 8-bit char buffer suitable for POSIX APIs.
// The probe never calls it; an empty C-string keeps callers happy.
#ifdef __cplusplus
static inline const char* wstringtofilename(const std::wstring&) { return ""; }
#endif

// HtmlString.cpp passes its raw input through this to neutralize
// markup-significant characters. Probe build only needs the symbol
// to exist; pass-through is fine.
#ifdef __cplusplus
static inline std::wstring escapeXML(const std::wstring& s) { return s; }
inline std::wstring escapeXML(const wchar_t* s) { return s ? std::wstring(s) : std::wstring(); }
#endif

// SystemTimeToFileTime: Win32 conversion helper. Probe stubs to a
// no-op zero fill; we never read FILETIME on iOS.
static inline BOOL SystemTimeToFileTime(const SYSTEMTIME*, FILETIME* ft) {
    if (ft) { ft->dwLowDateTime = 0; ft->dwHighDateTime = 0; }
    return TRUE;
}

// GetSystemTime for upstream system.cpp. Fills SYSTEMTIME from
// localtime. Compile-only correctness; we never call this.
static inline void GetSystemTime(LPSYSTEMTIME st) {
    if (!st) return;
    st->wYear = 2026; st->wMonth = 1; st->wDayOfWeek = 0; st->wDay = 1;
    st->wHour = 0; st->wMinute = 0; st->wSecond = 0; st->wMilliseconds = 0;
}

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
