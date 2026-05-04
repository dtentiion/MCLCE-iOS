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
#include <cstddef>
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
typedef int64_t  LONG64;
typedef uint64_t ULONG64;

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

// D3DX9 image info struct used by upstream BufferedImage.cpp on Win64.
// On iOS the same struct shape lets the upstream code compile; the
// actual fields are filled by C4JRenderStub::LoadTextureData (which
// has its own iOS PNG decode path, not a D3D one).
#ifndef D3DXIMAGE_INFO_DEFINED
#define D3DXIMAGE_INFO_DEFINED
typedef struct _D3DXIMAGE_INFO {
    unsigned int Width;
    unsigned int Height;
    unsigned int Depth;
    unsigned int MipLevels;
    unsigned int Format;
    unsigned int ResourceType;
    unsigned int ImageFileFormat;
} D3DXIMAGE_INFO;
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
#ifndef PIXAddNamedCounter
#  define PIXAddNamedCounter(value, name) ((void)0)
#endif
#ifndef PIXSetMarkerDeprecated
#  define PIXSetMarkerDeprecated(...) ((void)0)
#endif
#ifndef PIXBeginEvent
#  define PIXBeginEvent(...) ((void)0)
#endif
#ifndef PIXEndEvent
#  define PIXEndEvent() ((void)0)
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

// C4JThread.cpp records the current thread id + handle on entry. The Win32
// values are opaque thread tokens; mach lets us hand back the pthread_self
// pointer truncated to DWORD as a stand-in. Two different pthreads will
// always produce different IDs, which is the only invariant upstream code
// depends on.
static inline DWORD GetCurrentThreadId(void) {
    return (DWORD)(uintptr_t)pthread_self();
}
static inline HANDLE GetCurrentThread(void) {
    return (HANDLE)pthread_self();
}
static inline DWORD ResumeThread(HANDLE)              { return 0; }
static inline DWORD SuspendThread(HANDLE)             { return 0; }
static inline BOOL  SetThreadAffinityMask(HANDLE, DWORD_PTR) { return TRUE; }
static inline BOOL  SetThreadPriority(HANDLE, int)    { return TRUE; }
static inline int   GetThreadPriority(HANDLE)         { return 0; }
static inline DWORD WaitForSingleObject(HANDLE, DWORD) { return 0; }
static inline DWORD WaitForMultipleObjects(DWORD, const HANDLE*, BOOL, DWORD) { return 0; }
static inline BOOL  TerminateThread(HANDLE, DWORD)    { return TRUE; }
// INVALID_HANDLE_VALUE is defined further down in this header so we
// can't use it as the sentinel here without a forward macro. Return
// nullptr - upstream callers don't compare events to that sentinel,
// only to NULL.
static inline HANDLE CreateEventA(void*, BOOL, BOOL, const char*)    { return (HANDLE)0; }
static inline HANDLE CreateEventW(void*, BOOL, BOOL, const wchar_t*) { return (HANDLE)0; }
#  ifdef UNICODE
#    define CreateEvent CreateEventW
#  else
#    define CreateEvent CreateEventA
#  endif
static inline BOOL  SetEvent(HANDLE)   { return TRUE; }
static inline BOOL  ResetEvent(HANDLE) { return TRUE; }

// Critical sections backed by pthread mutexes. Upstream uses these
// for the global-lock pattern around C4JThread, command dispatch,
// etc. Compile-only support: we only need calls to typecheck. The
// CRITICAL_SECTION type is already declared in this header above.
static inline void InitializeCriticalSection(LPCRITICAL_SECTION cs) {
    if (!cs) return;
    // Win32 CRITICAL_SECTION is recursive: the same thread can Enter
    // the same section multiple times without deadlocking. Upstream
    // ConsoleSaveFileOriginal::closeHandle relies on this (it calls
    // LockSaveAccess, then finalizeWrite, which calls LockSaveAccess
    // again). Default pthread mutex isn't recursive - explicitly opt
    // in.
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&cs->_mu, &attr);
    pthread_mutexattr_destroy(&attr);
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
    DWORD           wNumStrings;   // upstream SignTileEntity reads this
    HRESULT*        pStringResult;
} STRING_VERIFY_RESPONSE;
#endif

// Xbox locale enums DLCSkinFile / DLCAudioFile switch on. Values pulled
// from the upstream Xbox SDK headers; iOS doesn't need to match them
// exactly, just be unique so the switch compiles.
#ifndef XC_LANGUAGE_ENGLISH
enum {
    XC_LANGUAGE_ENGLISH    = 1,
    XC_LANGUAGE_JAPANESE   = 2,
    XC_LANGUAGE_GERMAN     = 3,
    XC_LANGUAGE_FRENCH     = 4,
    XC_LANGUAGE_SPANISH    = 5,
    XC_LANGUAGE_ITALIAN    = 6,
    XC_LANGUAGE_KOREAN     = 7,
    XC_LANGUAGE_TCHINESE   = 8,
    XC_LANGUAGE_PORTUGUESE = 9,
    XC_LANGUAGE_SCHINESE   = 10,
    XC_LANGUAGE_POLISH     = 11,
    XC_LANGUAGE_RUSSIAN    = 12,
};
#endif

// Xbox helper for fetching the system locale id. iOS hands back English.
static inline int XGetLanguage(void) { return XC_LANGUAGE_ENGLISH; }
static inline int XGetLocale(void)   { return XC_LANGUAGE_ENGLISH; }

// Xbox-style invalid XUID sentinel.
#ifndef INVALID_XUID
#  define INVALID_XUID ((uint64_t)0xFFFFFFFFFFFFFFFFull)
#endif

// Xbox-style user name size constant from XUSER_API.
#ifndef XUSER_NAME_SIZE
#  define XUSER_NAME_SIZE 16
#endif

// SessionID type from upstream's Sony SQRNetworkManager. iOS doesn't
// ship PSN, so we provide an opaque 16-byte buffer matching the Sony
// layout. SessionInfo.h declares fields of this type.
#ifdef __cplusplus
struct SessionID {
    uint8_t bytes[16];
    SessionID() { for (int i = 0; i < 16; ++i) bytes[i] = 0; }
    bool operator==(const SessionID& o) const {
        for (int i = 0; i < 16; ++i) if (bytes[i] != o.bytes[i]) return false;
        return true;
    }
    bool operator!=(const SessionID& o) const { return !(*this == o); }
};
#endif

// Xbox-style content / device descriptors. Probe never opens XContent;
// these typedefs satisfy DLCTexturePack / DLCManager headers that
// declare members of these types.
typedef uint32_t XCONTENTDEVICEID;
#ifndef INVALID_DEVICE_ID
#  define INVALID_DEVICE_ID ((XCONTENTDEVICEID)0xFFFFFFFF)
#endif

// Direct3D 11 viewport descriptor. GameRenderer.h declares
// `ComputeViewportForPlayer(int, D3D11_VIEWPORT&)`. iOS uses its own
// renderer (Phase D); for compile we just need a same-shape struct.
typedef struct D3D11_VIEWPORT {
    float TopLeftX;
    float TopLeftY;
    float Width;
    float Height;
    float MinDepth;
    float MaxDepth;
} D3D11_VIEWPORT;

// Net protocol cap. Real value differs per platform (PS4/Switch use
// 16, X1 uses 8). Probe never sends/receives so the constant just
// needs to exist and be reasonable.
#ifndef MINECRAFT_NET_MAX_PLAYERS
#  define MINECRAFT_NET_MAX_PLAYERS 8
#endif

// 4J game-rules save filename. Defined in upstream
// Common/GameRules/GameRuleManager.h:18 - we mirror the string so
// callers that use the constant directly without pulling that header
// (MinecraftServer.cpp does) compile cleanly.
#ifndef GAME_RULE_SAVENAME
#  define GAME_RULE_SAVENAME L"requiredGameRules.grf"
#endif

// Legacy OpenGL constants. Upstream Minecraft.cpp / LevelRenderer.cpp
// use these against the OpenGL 1.x display-list / immediate-mode API.
// iOS doesn't ship legacy GL; the calls eventually route through our
// Metal-backed C4JRender_iOS in Phase D2. For compile we just need
// the integer constants to exist and the gl* prototypes to declare.
// stubs.h has its own copies plus a `class Color` that conflicts with
// Minecraft.World/Color.h, so we ship the GL-only subset here.
#ifndef GL_CONSTANTS_DEFINED
#define GL_CONSTANTS_DEFINED
#  define GL_BYTE                    0x1400
#  define GL_UNSIGNED_BYTE           0x1401
#  define GL_SHORT                   0x1402
#  define GL_UNSIGNED_SHORT          0x1403
#  define GL_INT                     0x1404
#  define GL_UNSIGNED_INT            0x1405
#  define GL_FLOAT                   0x1406
#  define GL_TEXTURE_2D              0x0DE1
#  define GL_TEXTURE_BINDING_2D      0x8069
#  define GL_TEXTURE_MIN_FILTER      0x2801
#  define GL_TEXTURE_MAG_FILTER      0x2800
#  define GL_TEXTURE_WRAP_S          0x2802
#  define GL_TEXTURE_WRAP_T          0x2803
#  define GL_NEAREST                 0x2600
#  define GL_LINEAR                  0x2601
#  define GL_COMPILE                 0x1300
#  define GL_COMPILE_AND_EXECUTE     0x1301
#  define GL_RGB                     0x1907
#  define GL_RGBA                    0x1908
#  define GL_LUMINANCE               0x1909
#  define GL_LUMINANCE_ALPHA         0x190A
#  define GL_DEPTH_TEST              0x0B71
#  define GL_DEPTH_BUFFER_BIT        0x00000100
#  define GL_COLOR_BUFFER_BIT        0x00004000
#  define GL_STENCIL_BUFFER_BIT      0x00000400
#  define GL_BLEND                   0x0BE2
#  define GL_CULL_FACE               0x0B44
#  define GL_LIGHTING                0x0B50
#  define GL_FOG                     0x0B60
#  define GL_ALPHA_TEST              0x0BC0
#  define GL_VERTEX_ARRAY            0x8074
#  define GL_COLOR_ARRAY             0x8076
#  define GL_NORMAL_ARRAY            0x8075
#  define GL_TEXTURE_COORD_ARRAY     0x8078
#  define GL_TRIANGLES               0x0004
#  define GL_TRIANGLE_STRIP          0x0005
#  define GL_QUADS                   0x0007
#  define GL_PROJECTION              0x1701
#  define GL_MODELVIEW               0x1700
#  define GL_TEXTURE                 0x1702
#  define GL_PROJECTION_MATRIX       0x0BA7
#  define GL_MODELVIEW_MATRIX        0x0BA6
#  define GL_TEXTURE_MATRIX          0x0BA8
#  define GL_VIEWPORT                0x0BA2
#  define GL_RESCALE_NORMAL          0x803A
#  define GL_NORMALIZE               0x0BA1
#  define GL_T                       0x2001  // texgen S/T enum group
#  define GL_S                       0x2000
#  define GL_R                       0x2002
#  define GL_Q                       0x2003
#  define GL_SMOOTH                  0x1D01
#  define GL_FLAT                    0x1D00
#  define GL_TRUE                    1
#  define GL_FALSE                   0
#  define GL_LESS                    0x0201
#  define GL_LEQUAL                  0x0203
#  define GL_GREATER                 0x0204
#  define GL_GEQUAL                  0x0206
#  define GL_NEVER                   0x0200
#  define GL_ALWAYS                  0x0207
#  define GL_EQUAL                   0x0202
#  define GL_NOTEQUAL                0x0205
#  define GL_SRC_ALPHA               0x0302
#  define GL_ONE_MINUS_SRC_ALPHA     0x0303
#  define GL_ONE                     1
#  define GL_ZERO                    0
#  define GL_SRC_COLOR               0x0300
#  define GL_ONE_MINUS_SRC_COLOR     0x0301
#  define GL_DST_COLOR               0x0306
#  define GL_ONE_MINUS_DST_COLOR     0x0307
#  define GL_DST_ALPHA               0x0304
#  define GL_ONE_MINUS_DST_ALPHA     0x0305
#  define GL_CONSTANT_COLOR          0x8001
#  define GL_ONE_MINUS_CONSTANT_COLOR 0x8002
#  define GL_CONSTANT_ALPHA          0x8003
#  define GL_ONE_MINUS_CONSTANT_ALPHA 0x8004
#  define GL_POLYGON_OFFSET_FILL     0x8037
#  define GL_POLYGON_OFFSET_LINE     0x2A02
#  define GL_POLYGON_OFFSET_POINT    0x2A01
#  define GL_DEPTH_COMPONENT         0x1902
#  define GL_RED                     0x1903
#  define GL_GREEN                   0x1904
#  define GL_BLUE                    0x1905
#  define GL_ALPHA                   0x1906
#  define GL_BGR                     0x80E0
#  define GL_BGRA                    0x80E1
#  define GL_FUNC_ADD                0x8006
#  define GL_FUNC_SUBTRACT           0x800A
#  define GL_FUNC_REVERSE_SUBTRACT   0x800B
#  define GL_MAX                     0x8008
#  define GL_MIN                     0x8007
#  define GL_PACK_ALIGNMENT          0x0D05
#  define GL_UNPACK_ALIGNMENT        0x0CF5
#  define GL_MAX_TEXTURE_SIZE        0x0D33
#  define GL_VIEWPORT                0x0BA2
#  define GL_LINE_BIT                0x00000004
#  define GL_POLYGON_BIT             0x00000008
#  define GL_LIGHTING_BIT            0x00000040
#  define GL_FOG_BIT                 0x00000080
#  define GL_DEPTH_BUFFER_BIT2       0x00000100
#  define GL_CURRENT_BIT             0x00000001
#  define GL_VERTEX_BUFFER           0x8074
#  define GL_INDEX_BUFFER            0x8075
#  define GL_REPEAT                  0x2901
#  define GL_CLAMP                   0x2900
#  define GL_CLAMP_TO_EDGE           0x812F
#  define GL_NEAREST_MIPMAP_NEAREST  0x2700
#  define GL_LINEAR_MIPMAP_NEAREST   0x2701
#  define GL_NEAREST_MIPMAP_LINEAR   0x2702
#  define GL_LINEAR_MIPMAP_LINEAR    0x2703
#  define GL_BACK                    0x0405
#  define GL_FRONT                   0x0404
#  define GL_FRONT_AND_BACK          0x0408
#  define GL_CW                      0x0900
#  define GL_CCW                     0x0901
#  define GL_NORMALIZE               0x0BA1
#  define GL_LIGHT0                  0x4000
#  define GL_LIGHT1                  0x4001
#  define GL_LIGHT2                  0x4002
#  define GL_LIGHT3                  0x4003
#  define GL_AMBIENT                 0x1200
#  define GL_DIFFUSE                 0x1201
#  define GL_SPECULAR                0x1202
#  define GL_POSITION                0x1203
#  define GL_FOG_MODE                0x0B65
#  define GL_FOG_DENSITY             0x0B62
#  define GL_FOG_START               0x0B63
#  define GL_FOG_END                 0x0B64
#  define GL_FOG_COLOR               0x0B66
#  define GL_LINEAR_FOG              0x2601
#  define GL_EXP                     0x0800
#  define GL_EXP2                    0x0801
#  define GL_FASTEST                 0x1101
#  define GL_NICEST                  0x1102
#  define GL_DONT_CARE               0x1100
#  define GL_PERSPECTIVE_CORRECTION_HINT 0x0C50
#  define GL_FOG_HINT                0x0C54
#  define GL_LINE_SMOOTH_HINT        0x0C52
#  define GL_POLYGON_SMOOTH_HINT     0x0C53
#  define GL_INVALID_ENUM            0x0500
#  define GL_INVALID_VALUE           0x0501
#  define GL_INVALID_OPERATION       0x0502
#  define GL_OUT_OF_MEMORY           0x0505
#  define GL_LINES                   0x0001
#  define GL_LINE_STRIP              0x0003
#  define GL_LINE_LOOP               0x0002
#  define GL_POINTS                  0x0000
#  define GL_QUAD_STRIP              0x0008
#  define GL_POLYGON                 0x0009
#  define GL_TRIANGLE_FAN            0x0006
#  define GL_TEXTURE0                0x84C0
#  define GL_TEXTURE1                0x84C1
#  define GL_TEXTURE2                0x84C2
#  define GL_TEXTURE3                0x84C3
// Legacy GL function prototypes. Bodies live in
// WorldProbe/probe_stub.cpp as no-ops; real Metal backend lands in
// Phase D2 and replaces these via the C4JRender_iOS adapter layer.
//
// Declared with C++ linkage (NOT extern "C") so upstream-wrapper
// overloads like glGenTextures() / glLight(...,FloatBuffer*) /
// glCallLists(IntBuffer*) can coexist with the standard signatures.
void glEnable(unsigned int);
void glDisable(unsigned int);
void glClear(unsigned int);
void glClearColor(float, float, float, float);
void glViewport(int, int, int, int);
void glPushMatrix(void);
void glPopMatrix(void);
void glLoadIdentity(void);
void glMatrixMode(unsigned int);
void glTranslatef(float, float, float);
void glRotatef(float, float, float, float);
void glScalef(float, float, float);
void glColor4f(float, float, float, float);
void glBegin(unsigned int);
void glEnd(void);
void glVertex3f(float, float, float);
void glTexCoord2f(float, float);
void glNewList(int, int);
void glEndList(void);
void glCallList(int);
void glDeleteLists(int, int);
int  glGenLists(int);
void glBindTexture(unsigned int, unsigned int);
void glTexParameteri(unsigned int, unsigned int, int);
void glDepthFunc(unsigned int);
void glAlphaFunc(unsigned int, float);
void glBlendFunc(unsigned int, unsigned int);
void glShadeModel(unsigned int);
void glDepthMask(unsigned char);
void glColorMask(unsigned char, unsigned char, unsigned char, unsigned char);
void glFrontFace(unsigned int);
void glCullFace(unsigned int);
void glPointSize(float);
void glLineWidth(float);
void glHint(unsigned int, unsigned int);
void glPolygonOffset(float, float);
void glScissor(int, int, int, int);
void glOrtho(double, double, double, double, double, double);
void glFrustum(double, double, double, double, double, double);
void glStencilFunc(unsigned int, int, unsigned int);
void glStencilOp(unsigned int, unsigned int, unsigned int);
void glStencilMask(unsigned int);
void glClearStencil(int);
void glClearDepth(double);
unsigned int glGetError(void);
void glGenTextures(int, unsigned int*);
void glDeleteTextures(int, const unsigned int*);
void glTexImage2D(unsigned int, int, int, int, int, int, unsigned int, unsigned int, const void*);
void glTexSubImage2D(unsigned int, int, int, int, int, int, unsigned int, unsigned int, const void*);
void glPixelStorei(unsigned int, int);
void glReadPixels(int, int, int, int, unsigned int, unsigned int, void*);
void glEnableClientState(unsigned int);
void glDisableClientState(unsigned int);
void glVertexPointer(int, unsigned int, int, const void*);
void glColorPointer(int, unsigned int, int, const void*);
void glTexCoordPointer(int, unsigned int, int, const void*);
void glNormalPointer(unsigned int, int, const void*);
void glDrawArrays(unsigned int, int, int);
void glDrawElements(unsigned int, int, unsigned int, const void*);
void glColor3f(float, float, float);
void glColor3ub(unsigned char, unsigned char, unsigned char);
void glColor4ub(unsigned char, unsigned char, unsigned char, unsigned char);
void glColor4ubv(const unsigned char*);
void glVertex2f(float, float);
void glVertex2i(int, int);
void glNormal3f(float, float, float);
void glLightfv(unsigned int, unsigned int, const float*);
void glMaterialfv(unsigned int, unsigned int, const float*);
void glFogf(unsigned int, float);
void glFogi(unsigned int, int);
void glFogfv(unsigned int, const float*);
void glMultMatrixf(const float*);
void glLoadMatrixf(const float*);
void glGetFloatv(unsigned int, float*);
void glGetIntegerv(unsigned int, int*);
// Upstream Frustum.cpp uses the older non-vector spelling.
static inline void glGetFloat(unsigned int pname, float *out) { glGetFloatv(pname, out); }
// Frustum.cpp also calls glGetFloat with a FloatBuffer* (real impl in
// glWrapper.cpp). Forward-declare and stub so the call resolves.
class FloatBuffer;
void glGetFloat(int type, FloatBuffer *buff);
void glColorMaterial(unsigned int face, unsigned int mode);
void glLightModelfv(unsigned int pname, const float *params);
void glMultiTexCoord2f(unsigned int, float, float);
void glMultiTexCoord2fv(unsigned int, const float*);
void glActiveTexture(unsigned int);
void glClientActiveTexture(unsigned int);
// Old fixed-function texture-coord generation. Real impl lives on
// desktop GL; on iOS Metal these become no-ops at the leaf calls
// (Phase G will swap the gl* layer for real Metal anyway).
void glTexGeni(unsigned int, unsigned int, int);
void glTexGenf(unsigned int, unsigned int, float);
void glTexGenfv(unsigned int, unsigned int, const float*);
// Fixed-function lighting (set/query light parameters).
void glLighti(unsigned int, unsigned int, int);
void glLightf(unsigned int, unsigned int, float);
// Display lists - upstream uses these for batched draw command capture.
void glCallLists(int, unsigned int, const void*);

// G2b: C++ overloads of GL wrappers upstream renderer code calls. Now
// that the gl* prototypes above are declared with C++ linkage, plain
// overloads work. Definitions live in WorldProbe/probe_stub.cpp.
#ifdef __cplusplus
class FloatBuffer;
class IntBuffer;
unsigned int glGenTextures(void);                                        // MemoryTracker
void         glDeleteTextures(unsigned int);                             // MemoryTracker
void         glLight(unsigned int, unsigned int, FloatBuffer *);         // Lighting
void         glLightModel(unsigned int, FloatBuffer *);                  // Lighting
void         glCallLists(IntBuffer *);                                   // OffsettedRenderList
#endif
#endif // GL_CONSTANTS_DEFINED

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

// Console nether-scale ratio for legacy worlds, plus min-scale floor.
#ifndef HELL_LEVEL_LEGACY_SCALE
#  define HELL_LEVEL_LEGACY_SCALE 3
#endif
#ifndef HELL_LEVEL_MIN_SCALE
#  define HELL_LEVEL_MIN_SCALE   1
#endif

// Console world-size SMALL/MEDIUM constants; used alongside LEGACY/
// CLASSIC in world picker / server bounds.
#ifndef LEVEL_WIDTH_SMALL
#  define LEVEL_WIDTH_SMALL     384
#endif
#ifndef LEVEL_WIDTH_MEDIUM
#  define LEVEL_WIDTH_MEDIUM    640
#endif
#ifndef LEVEL_MIN_WIDTH
#  define LEVEL_MIN_WIDTH       128
#endif

// Console nether-scale ratio - max value used for size cap.
#ifndef HELL_LEVEL_MAX_SCALE
#  define HELL_LEVEL_MAX_SCALE  4
#endif

// Win32 wait sentinel - infinite timeout.
#ifndef INFINITE
#  define INFINITE              0xFFFFFFFF
#endif
// Win32 WaitForSingleObject return - timeout elapsed without signal.
#ifndef WAIT_TIMEOUT
#  define WAIT_TIMEOUT          258
#endif
#ifndef WAIT_OBJECT_0
#  define WAIT_OBJECT_0         0
#endif
#ifndef WAIT_FAILED
#  define WAIT_FAILED           0xFFFFFFFF
#endif

// Win32 thread-creation flags. Probe never spawns a thread; constant
// just needs to exist for compile.
#ifndef CREATE_SUSPENDED
#  define CREATE_SUSPENDED      0x00000004
#endif

// Win32 thread priority levels. MinecraftServer.cpp sets above-normal
// priority on its post-update thread; iOS uses pthread priority
// classes so these are compile-only constants.
#ifndef THREAD_PRIORITY_NORMAL
#  define THREAD_PRIORITY_NORMAL        0
#endif
#ifndef THREAD_PRIORITY_ABOVE_NORMAL
#  define THREAD_PRIORITY_ABOVE_NORMAL  1
#endif
#ifndef THREAD_PRIORITY_BELOW_NORMAL
#  define THREAD_PRIORITY_BELOW_NORMAL (-1)
#endif
#ifndef THREAD_PRIORITY_HIGHEST
#  define THREAD_PRIORITY_HIGHEST       2
#endif
#ifndef THREAD_PRIORITY_LOWEST
#  define THREAD_PRIORITY_LOWEST       (-2)
#endif

// 4J QNet send-flags. Console-only; probe never sends a packet.
#ifndef NON_QNET_SENDDATA_ACK_REQUIRED
#  define NON_QNET_SENDDATA_ACK_REQUIRED 0
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
#ifndef FILE_ATTRIBUTE_DIRECTORY
#  define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#endif
#ifndef FILE_SHARE_READ
#  define FILE_SHARE_READ       0x00000001
#endif
#ifndef INVALID_FILE_ATTRIBUTES
#  define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif
#ifndef INVALID_SET_FILE_POINTER
#  define INVALID_SET_FILE_POINTER ((DWORD)-1)
#endif
#ifndef ERROR_SUCCESS
#  define ERROR_SUCCESS 0L
#endif

// Build identifier upstream embeds in the splash. Real Win64 build
// generates this from the perforce branch; iOS just hands back a
// static string.
#ifndef VER_BRANCHVERSION_STR_W
#  define VER_BRANCHVERSION_STR_W L"iOS"
#endif
#ifndef VER_BRANCHVERSION_STR
#  define VER_BRANCHVERSION_STR "iOS"
#endif
#ifndef VER_FILEVERSION_STR_W
#  define VER_FILEVERSION_STR_W L"0.0.1"
#endif
#ifndef VER_FILEVERSION_STR
#  define VER_FILEVERSION_STR "0.0.1"
#endif

// Xbox Live presence-context constants. Real values are in xbox-secrets;
// our shim just needs each name to resolve so switch/case statements
// parse. We never report presence on iOS.
#ifndef CONTEXT_PRESENCE_MULTIPLAYEROFFLINE
#  define CONTEXT_PRESENCE_MULTIPLAYEROFFLINE 0
#endif
#ifndef CONTEXT_PRESENCE_MULTIPLAYER
#  define CONTEXT_PRESENCE_MULTIPLAYER 3
#endif
#ifndef CONTEXT_PRESENCE_TUTORIAL
#  define CONTEXT_PRESENCE_TUTORIAL 4
#endif
#ifndef CONTEXT_PRESENCE_MENU
#  define CONTEXT_PRESENCE_MENU 5
#endif
#ifndef CONTEXT_PRESENCE_MULTIPLAYER_1POFFLINE
#  define CONTEXT_PRESENCE_MULTIPLAYER_1POFFLINE 6
#endif
#ifndef CONTEXT_PRESENCE_MULTIPLAYER_2POFFLINE
#  define CONTEXT_PRESENCE_MULTIPLAYER_2POFFLINE 7
#endif
#ifndef CONTEXT_PRESENCE_MULTIPLAYER_3POFFLINE
#  define CONTEXT_PRESENCE_MULTIPLAYER_3POFFLINE 8
#endif
#ifndef CONTEXT_PRESENCE_MULTIPLAYER_4POFFLINE
#  define CONTEXT_PRESENCE_MULTIPLAYER_4POFFLINE 9
#endif
#ifndef CONTEXT_PRESENCE_MULTIPLAYER_1P
#  define CONTEXT_PRESENCE_MULTIPLAYER_1P 10
#endif
#ifndef CONTEXT_PRESENCE_MULTIPLAYER_2P
#  define CONTEXT_PRESENCE_MULTIPLAYER_2P 11
#endif
#ifndef CONTEXT_PRESENCE_MULTIPLAYER_3P
#  define CONTEXT_PRESENCE_MULTIPLAYER_3P 12
#endif
#ifndef CONTEXT_PRESENCE_MULTIPLAYER_4P
#  define CONTEXT_PRESENCE_MULTIPLAYER_4P 13
#endif
#ifndef CONTEXT_PRESENCE_IDLE
#  define CONTEXT_PRESENCE_IDLE 14
#endif

// More legacy GL constants for renderer files.
#ifndef GL_COLOR_MATERIAL
#  define GL_COLOR_MATERIAL          0x0B57
#endif
#ifndef GL_AMBIENT_AND_DIFFUSE
#  define GL_AMBIENT_AND_DIFFUSE     0x1602
#endif
#ifndef GL_LIGHT_MODEL_AMBIENT
#  define GL_LIGHT_MODEL_AMBIENT     0x0B53
#endif
#ifndef GL_LIGHT_MODEL_LOCAL_VIEWER
#  define GL_LIGHT_MODEL_LOCAL_VIEWER 0x0B51
#endif
#ifndef GL_OBJECT_LINEAR
#  define GL_OBJECT_LINEAR           0x2401
#endif
#ifndef GL_EYE_LINEAR
#  define GL_EYE_LINEAR              0x2400
#endif
#ifndef GL_OBJECT_PLANE
#  define GL_OBJECT_PLANE            0x2501
#endif
#ifndef GL_EYE_PLANE
#  define GL_EYE_PLANE               0x2502
#endif
#ifndef GL_TEXTURE_GEN_MODE
#  define GL_TEXTURE_GEN_MODE        0x2500
#endif
#ifndef GL_TEXTURE_GEN_S
#  define GL_TEXTURE_GEN_S           0x0C60
#endif
#ifndef GL_TEXTURE_GEN_T
#  define GL_TEXTURE_GEN_T           0x0C61
#endif

// Win32 thread-exit query. Returns the exit code of a finished thread;
// upstream uses it after WaitForSingleObject. iOS hands back 0.
static inline BOOL GetExitCodeThread(HANDLE, DWORD *out) {
    if (out) *out = 0;
    return TRUE;
}
#ifndef CONTEXT_PRESENCE_MULTIPLAYERONLINE
#  define CONTEXT_PRESENCE_MULTIPLAYERONLINE 1
#endif
#ifndef CONTEXT_PRESENCE_SINGLEPLAYER
#  define CONTEXT_PRESENCE_SINGLEPLAYER 2
#endif

// Xbox-specific thread-affinity helper. Maps to no-op on iOS.
static inline DWORD XSetThreadProcessor(HANDLE, DWORD) { return 0; }

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

// `replaceAll` is in upstream StringHelpers.h - the pre-include in
// iOS_stdafx.h brings its declaration into scope.

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

// Win32 spin-count critical-section initializer. Recursive on Win32 so
// match that here. ConsoleSaveFileOriginal uses this entrypoint and
// nests LockSaveAccess via finalizeWrite - non-recursive deadlocks.
static inline BOOL InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION cs, DWORD) {
    if (!cs) return FALSE;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&cs->_mu, &attr);
    pthread_mutexattr_destroy(&attr);
    return TRUE;
}

// `equalsIgnoreCase` lives in upstream's StringHelpers.h/cpp; the
// pre-include in iOS_stdafx.h surfaces the declaration for files
// that use it without including StringHelpers.h directly.

// CRT integer-to-wide-string. Upstream uses for HUD numerics. Map to
// swprintf - the probe never inspects the buffer.
static inline wchar_t* _itow(int v, wchar_t* buf, int radix) {
    if (!buf) return buf;
    if (radix == 16) swprintf(buf, 12, L"%x", v);
    else             swprintf(buf, 12, L"%d", v);
    return buf;
}
// Secure-CRT variant. Same signature plus a buffer-size arg.
static inline int _itow_s(int v, wchar_t* buf, size_t bufSize, int radix) {
    if (!buf || bufSize == 0) return -1;
    if (radix == 16) swprintf(buf, bufSize, L"%x", v);
    else             swprintf(buf, bufSize, L"%d", v);
    return 0;
}
// Three-arg helper variant. Upstream files call _itow_s with
// (value, buf, radix) when the buffer is a fixed-size wchar_t array.
// The C++ template deduces the array size; reroutes to the
// 4-arg path which actually does the formatting.
template<size_t N>
static inline int _itow_s(int v, wchar_t (&buf)[N], int radix) {
    return _itow_s(v, buf, N, radix);
}

// More secure-CRT variants upstream calls. wcscpy_s + swscanf_s map to
// the standard non-_s versions on POSIX (no buffer-size enforcement).
#ifndef wcscpy_s
#  define wcscpy_s(dst, n, src) (wcsncpy((dst), (src), (n)), (dst)[(n) - 1] = 0, 0)
#endif
#ifndef swscanf_s
#  define swscanf_s swscanf
#endif
#ifndef sscanf_s
#  define sscanf_s sscanf
#endif

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

// Win32 SetFilePointer entry, POSIX-backed via lseek. HANDLE is the
// underlying fd. dwMoveMethod: FILE_BEGIN(0) FILE_CURRENT(1) FILE_END(2).
static inline DWORD SetFilePointer(HANDLE h, LONG distLow, PLONG distHigh, DWORD method) {
    if (h == INVALID_HANDLE_VALUE) return 0xFFFFFFFFu;
    int fd = (int)(intptr_t)h;
    int whence = SEEK_SET;
    if      (method == 1) whence = SEEK_CUR;
    else if (method == 2) whence = SEEK_END;
    int64_t off = (int64_t)distLow;
    if (distHigh) off |= ((int64_t)*distHigh) << 32;
    off_t r = ::lseek(fd, (off_t)off, whence);
    if (r == (off_t)-1) {
        if (distHigh) *distHigh = 0;
        return 0xFFFFFFFFu;
    }
    if (distHigh) *distHigh = (LONG)((uint64_t)r >> 32);
    return (DWORD)((uint64_t)r & 0xFFFFFFFFu);
}
// ZoneIo.cpp passes nullptr for the DWORD moveMethod arg. The nullptr_t
// overload approach made other call sites ambiguous because they pass
// `0` literals; patched in patch-upstream-stdafx.sh instead to use
// FILE_BEGIN explicitly.

// Win32 CreateFile / WriteFile / ReadFile / CloseHandle entries,
// POSIX-backed via open/read/write/close. HANDLE encodes the fd directly
// as (HANDLE)(intptr_t)fd so a fd of -1 (POSIX failure) maps cleanly to
// (HANDLE)(LONG_PTR)-1 == INVALID_HANDLE_VALUE.
#ifndef _IOS_FILE_API_DECLARED
#define _IOS_FILE_API_DECLARED
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef GENERIC_READ
#  define GENERIC_READ  0x80000000UL
#endif
#ifndef GENERIC_WRITE
#  define GENERIC_WRITE 0x40000000UL
#endif
#ifndef CREATE_NEW
#  define CREATE_NEW         1
#endif
#ifndef CREATE_ALWAYS
#  define CREATE_ALWAYS      2
#endif
#ifndef OPEN_EXISTING
#  define OPEN_EXISTING      3
#endif
#ifndef OPEN_ALWAYS
#  define OPEN_ALWAYS        4
#endif
#ifndef TRUNCATE_EXISTING
#  define TRUNCATE_EXISTING  5
#endif

static inline int _ios_oflags(DWORD access, DWORD disposition) {
    int flags = 0;
    const bool wantRead  = (access & GENERIC_READ)  != 0;
    const bool wantWrite = (access & GENERIC_WRITE) != 0;
    if      (wantRead && wantWrite) flags = O_RDWR;
    else if (wantWrite)             flags = O_WRONLY;
    else                            flags = O_RDONLY;
    switch (disposition) {
        case CREATE_NEW:        flags |= O_CREAT | O_EXCL;  break;
        case CREATE_ALWAYS:     flags |= O_CREAT | O_TRUNC; break;
        case OPEN_ALWAYS:       flags |= O_CREAT;           break;
        case TRUNCATE_EXISTING: flags |= O_TRUNC;           break;
        case OPEN_EXISTING:
        default: break;
    }
    return flags;
}

static inline HANDLE CreateFileA(const char *path, DWORD access, DWORD /*share*/,
                                  void* /*sec*/, DWORD disposition, DWORD /*flags*/, HANDLE /*tmpl*/) {
    if (!path) return INVALID_HANDLE_VALUE;
    int fd = ::open(path, _ios_oflags(access, disposition), 0644);
    return (HANDLE)(intptr_t)fd; // fd == -1 → INVALID_HANDLE_VALUE
}
#ifdef __cplusplus
extern "C++" {
static inline HANDLE CreateFileW(const wchar_t *path, DWORD access, DWORD share,
                                  void *sec, DWORD disposition, DWORD flags, HANDLE tmpl) {
    if (!path) return INVALID_HANDLE_VALUE;
    char buf[1024]; size_t i = 0;
    while (path[i] && i < sizeof(buf)-1) { buf[i] = (char)(path[i] & 0x7F); ++i; }
    buf[i] = 0;
    return CreateFileA(buf, access, share, sec, disposition, flags, tmpl);
}
}
#endif
#  ifdef UNICODE
#    define CreateFile CreateFileW
#  else
#    define CreateFile CreateFileA
#  endif
static inline BOOL ReadFile(HANDLE h, void *buf, DWORD toRead, LPDWORD got, void*) {
    if (got) *got = 0;
    if (h == INVALID_HANDLE_VALUE || !buf) return FALSE;
    int fd = (int)(intptr_t)h;
    ssize_t n = ::read(fd, buf, (size_t)toRead);
    if (n < 0) return FALSE;
    if (got) *got = (DWORD)n;
    return TRUE;
}
static inline BOOL WriteFile(HANDLE h, const void *buf, DWORD toWrite, LPDWORD wrote, void*) {
    if (wrote) *wrote = 0;
    if (h == INVALID_HANDLE_VALUE || !buf) return FALSE;
    int fd = (int)(intptr_t)h;
    ssize_t n = ::write(fd, buf, (size_t)toWrite);
    if (n < 0) return FALSE;
    if (wrote) *wrote = (DWORD)n;
    return TRUE;
}
static inline BOOL CloseHandle(HANDLE h) {
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    int fd = (int)(intptr_t)h;
    return ::close(fd) == 0 ? TRUE : FALSE;
}
static inline BOOL DeleteFileA(const char *path) {
    if (!path) return FALSE;
    return ::unlink(path) == 0 ? TRUE : FALSE;
}
#ifdef __cplusplus
extern "C++" {
static inline BOOL DeleteFileW(const wchar_t *path) {
    if (!path) return FALSE;
    char buf[1024]; size_t i = 0;
    while (path[i] && i < sizeof(buf)-1) { buf[i] = (char)(path[i] & 0x7F); ++i; }
    buf[i] = 0;
    return DeleteFileA(buf);
}
}
#endif
static inline DWORD GetFileSize(HANDLE h, LPDWORD high) {
    if (high) *high = 0;
    if (h == INVALID_HANDLE_VALUE) return 0xFFFFFFFFu; // INVALID_FILE_SIZE
    int fd = (int)(intptr_t)h;
    struct stat st;
    if (::fstat(fd, &st) != 0) return 0xFFFFFFFFu;
    if (high) *high = (DWORD)((uint64_t)st.st_size >> 32);
    return (DWORD)((uint64_t)st.st_size & 0xFFFFFFFFu);
}

// Win32 thread spawn. Probe never runs threading; null handle is fine.
// Placed after LPDWORD/INVALID_HANDLE_VALUE so the signature parses.
typedef DWORD (*LPTHREAD_START_ROUTINE)(void*);
static inline HANDLE CreateThread(void*, size_t, LPTHREAD_START_ROUTINE, void*, DWORD, LPDWORD outId) {
    if (outId) *outId = 0;
    return INVALID_HANDLE_VALUE;
}
static inline BOOL CreateDirectoryA(const char*, void*) { return TRUE; }
static inline BOOL CreateDirectoryW(const wchar_t*, void*) { return TRUE; }
#  ifdef UNICODE
#    define CreateDirectory CreateDirectoryW
#  else
#    define CreateDirectory CreateDirectoryA
#  endif

// POSIX-backed file/directory shim lives in iOS_WinFileShim.h and is
// pulled in by iOS_stdafx.h after this file. Keeps the C/C++ extern-C
// scoping clean rather than weaving the C++ helpers through this block.

// Win32 memory status report struct. Compression code reads available
// physical memory before doing big allocations; probe never executes
// that path - empty struct compiles fine.
typedef struct _MEMORYSTATUS {
    DWORD dwLength;
    DWORD dwMemoryLoad;
    DWORD dwTotalPhys;
    DWORD dwAvailPhys;
    DWORD dwTotalPageFile;
    DWORD dwAvailPageFile;
    DWORD dwTotalVirtual;
    DWORD dwAvailVirtual;
} MEMORYSTATUS, *LPMEMORYSTATUS;
static inline void GlobalMemoryStatus(LPMEMORYSTATUS s) {
    if (!s) return;
    s->dwLength = sizeof(*s);
    s->dwMemoryLoad = 0;
    s->dwTotalPhys = 0; s->dwAvailPhys = 0;
    s->dwTotalPageFile = 0; s->dwAvailPageFile = 0;
    s->dwTotalVirtual = 0; s->dwAvailVirtual = 0;
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
// Win32 large-page hint. Level.cpp asks for 128KB pages on Xbox 360.
// iOS uses 16KB pages and the alloc falls back to malloc; constant
// just needs to exist.
#ifndef MEM_LARGE_PAGES
#  define MEM_LARGE_PAGES 0x20000000
#endif
// VirtualAlloc / VirtualFree flags used by ConsoleSaveFileOriginal to
// reserve a 2GB save heap on Win64 and commit pages incrementally.
// iOS doesn't have VirtualAlloc; the shims below fall back to malloc.
#ifndef MEM_RESERVE
#  define MEM_RESERVE  0x00002000
#endif
#ifndef MEM_COMMIT
#  define MEM_COMMIT   0x00001000
#endif
#ifndef MEM_DECOMMIT
#  define MEM_DECOMMIT 0x00004000
#endif
#ifndef MEM_RELEASE
#  define MEM_RELEASE  0x00008000
#endif
static inline void *VirtualAlloc(void *addr, size_t size, DWORD type, DWORD /*protect*/) {
    if (type & MEM_RESERVE) return malloc(size);
    return addr; // MEM_COMMIT on top of an existing reservation - already mapped
}
static inline BOOL VirtualFree(void *addr, size_t /*size*/, DWORD type) {
    if (type & MEM_RELEASE) free(addr);
    return TRUE;
}

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

// `wstringtofilename` and `escapeXML` are declared in upstream's
// StringHelpers.h and defined in StringHelpers.cpp. Pre-include of
// StringHelpers.h in iOS_stdafx.h brings the declarations into scope
// for the files that need them; we no longer ship inline stubs here
// because those collided with the real definitions when the probe
// library was actually linked.

// SystemTimeToFileTime: Win32 conversion helper. Probe stubs to a
// no-op zero fill; we never read FILETIME on iOS.
static inline BOOL SystemTimeToFileTime(const SYSTEMTIME*, FILETIME* ft) {
    if (ft) { ft->dwLowDateTime = 0; ft->dwHighDateTime = 0; }
    return TRUE;
}
static inline BOOL FileTimeToSystemTime(const FILETIME*, SYSTEMTIME* st) {
    if (!st) return FALSE;
    st->wYear = 2026; st->wMonth = 1; st->wDayOfWeek = 0; st->wDay = 1;
    st->wHour = 0; st->wMinute = 0; st->wSecond = 0; st->wMilliseconds = 0;
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
