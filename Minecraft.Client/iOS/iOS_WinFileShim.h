// POSIX-backed Win32 file API shims so upstream File.cpp + ConsoleSaveFileOriginal.cpp
// see real backing storage. Pulled in from iOS_stdafx.h after iOS_WinCompat.h
// (which already provides DWORD, BOOL, HANDLE, INVALID_HANDLE_VALUE, the
// FILE_ATTRIBUTE_* macros and INVALID_FILE_ATTRIBUTES).

#pragma once

#if !defined(_WIN32) && !defined(_WIN64)

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifdef __cplusplus
#include <string>

// wchar_t -> UTF-8 (ASCII fast path; iOS sandbox paths are ASCII-only).
static inline std::string ios_wcs_to_utf8(const wchar_t *p) {
    std::string out;
    if (!p) return out;
    while (*p) {
        if (*p < 0x80)        out.push_back(static_cast<char>(*p));
        else if (*p < 0x800) {
            out.push_back(static_cast<char>(0xC0 | ((*p >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (*p & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | ((*p >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((*p >> 6)  & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (*p & 0x3F)));
        }
        ++p;
    }
    return out;
}
#endif

// Non-UNICODE shape: upstream File.cpp's #else branch is what we hit on iOS,
// and its filenametowstring helper takes `const char *`. So cFileName is char.
typedef struct _WIN32_FIND_DATA {
    DWORD   dwFileAttributes;
    DWORD   _ftCreationTime[2];
    DWORD   _ftLastAccessTime[2];
    struct  { DWORD dwHighDateTime; DWORD dwLowDateTime; } ftLastWriteTime;
    DWORD   nFileSizeHigh;
    DWORD   nFileSizeLow;
    DWORD   dwReserved0;
    DWORD   dwReserved1;
    char    cFileName[260];
    char    cAlternateFileName[14];
} WIN32_FIND_DATA, *LPWIN32_FIND_DATA;

typedef struct _WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes;
    DWORD ftCreationTime[2];
    DWORD ftLastAccessTime[2];
    struct { DWORD dwHighDateTime; DWORD dwLowDateTime; } ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA, *LPWIN32_FILE_ATTRIBUTE_DATA;

typedef enum _GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard = 0 } GET_FILEEX_INFO_LEVELS;

typedef struct _IosFindHandle {
    void *dir;            // DIR*
} _IosFindHandle;

#ifdef __cplusplus

static inline DWORD GetFileAttributesA(const char *path) {
    if (!path) return INVALID_FILE_ATTRIBUTES;
    struct stat st;
    if (stat(path, &st) != 0) return INVALID_FILE_ATTRIBUTES;
    return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}
static inline DWORD GetFileAttributesW(const wchar_t *path) {
    if (!path) return INVALID_FILE_ATTRIBUTES;
    return GetFileAttributesA(ios_wcs_to_utf8(path).c_str());
}
#  ifdef UNICODE
#    define GetFileAttributes GetFileAttributesW
#  else
#    define GetFileAttributes GetFileAttributesA
#  endif

static inline BOOL GetFileAttributesExA(const char *path, GET_FILEEX_INFO_LEVELS, void *out) {
    if (!path || !out) return FALSE;
    struct stat st;
    if (stat(path, &st) != 0) return FALSE;
    LPWIN32_FILE_ATTRIBUTE_DATA d = (LPWIN32_FILE_ATTRIBUTE_DATA)out;
    d->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    d->nFileSizeHigh = (DWORD)((uint64_t)st.st_size >> 32);
    d->nFileSizeLow  = (DWORD)((uint64_t)st.st_size & 0xFFFFFFFFu);
    uint64_t mtime100ns = (uint64_t)st.st_mtime * 10000000ull;
    d->ftLastWriteTime.dwHighDateTime = (DWORD)(mtime100ns >> 32);
    d->ftLastWriteTime.dwLowDateTime  = (DWORD)(mtime100ns & 0xFFFFFFFFu);
    return TRUE;
}
static inline BOOL GetFileAttributesExW(const wchar_t *path, GET_FILEEX_INFO_LEVELS lvl, void *out) {
    if (!path) return FALSE;
    return GetFileAttributesExA(ios_wcs_to_utf8(path).c_str(), lvl, out);
}
#  ifdef UNICODE
#    define GetFileAttributesEx GetFileAttributesExW
#  else
#    define GetFileAttributesEx GetFileAttributesExA
#  endif

static inline BOOL MoveFileA(const char *src, const char *dst) {
    if (!src || !dst) return FALSE;
    return rename(src, dst) == 0 ? TRUE : FALSE;
}
static inline BOOL MoveFileW(const wchar_t *src, const wchar_t *dst) {
    if (!src || !dst) return FALSE;
    return MoveFileA(ios_wcs_to_utf8(src).c_str(), ios_wcs_to_utf8(dst).c_str());
}
#  ifdef UNICODE
#    define MoveFile MoveFileW
#  else
#    define MoveFile MoveFileA
#  endif

// Upstream pattern is "<dir>\*" or "<dir>/*"; trim the trailing "\*"/"/*"
// before passing to opendir. Skip "." and "..".
static inline bool _ios_fill_find_data_(WIN32_FIND_DATA *out, struct dirent *de) {
    out->dwFileAttributes = (de->d_type == DT_DIR) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    size_t i = 0;
    for (; de->d_name[i] && i < 259; ++i) out->cFileName[i] = de->d_name[i];
    out->cFileName[i] = 0;
    return true;
}

static inline HANDLE FindFirstFileA(const char *pattern, LPWIN32_FIND_DATA out) {
    if (!pattern || !out) return INVALID_HANDLE_VALUE;
    std::string dir = pattern;
    size_t slash = dir.find_last_of("\\/");
    if (slash != std::string::npos) dir.resize(slash);
    DIR *d = opendir(dir.c_str());
    if (!d) return INVALID_HANDLE_VALUE;
    _IosFindHandle *h = (_IosFindHandle *)calloc(1, sizeof(_IosFindHandle));
    if (!h) { closedir(d); return INVALID_HANDLE_VALUE; }
    h->dir = d;
    struct dirent *de;
    while ((de = readdir((DIR*)h->dir)) != nullptr) {
        if (de->d_name[0] == '.' && (de->d_name[1] == 0 || (de->d_name[1] == '.' && de->d_name[2] == 0))) continue;
        _ios_fill_find_data_(out, de);
        return (HANDLE)h;
    }
    closedir((DIR*)h->dir);
    free(h);
    return INVALID_HANDLE_VALUE;
}
static inline HANDLE FindFirstFileW(const wchar_t *pattern, LPWIN32_FIND_DATA out) {
    if (!pattern) return INVALID_HANDLE_VALUE;
    return FindFirstFileA(ios_wcs_to_utf8(pattern).c_str(), out);
}
static inline BOOL FindNextFileA(HANDLE h, LPWIN32_FIND_DATA out) {
    if (h == INVALID_HANDLE_VALUE || !out) return FALSE;
    _IosFindHandle *fh = (_IosFindHandle *)h;
    struct dirent *de;
    while ((de = readdir((DIR*)fh->dir)) != nullptr) {
        if (de->d_name[0] == '.' && (de->d_name[1] == 0 || (de->d_name[1] == '.' && de->d_name[2] == 0))) continue;
        _ios_fill_find_data_(out, de);
        return TRUE;
    }
    return FALSE;
}
static inline BOOL FindNextFileW(HANDLE h, LPWIN32_FIND_DATA out) { return FindNextFileA(h, out); }
static inline BOOL FindClose(HANDLE h) {
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    _IosFindHandle *fh = (_IosFindHandle *)h;
    if (fh->dir) closedir((DIR*)fh->dir);
    free(fh);
    return TRUE;
}
#  ifdef UNICODE
#    define FindFirstFile FindFirstFileW
#    define FindNextFile  FindNextFileW
#  else
#    define FindFirstFile FindFirstFileA
#    define FindNextFile  FindNextFileA
#  endif

#endif // __cplusplus

#endif // !_WIN32 && !_WIN64
