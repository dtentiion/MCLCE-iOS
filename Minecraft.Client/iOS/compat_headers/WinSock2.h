// <WinSock2.h> for Windows64/Network/WinsockNetLayer: the Winsock names it
// is written with, over BSD sockets.

#pragma once

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <wchar.h>

typedef int SOCKET;

#ifndef INVALID_SOCKET
#  define INVALID_SOCKET (-1)
#endif
#ifndef SOCKET_ERROR
#  define SOCKET_ERROR (-1)
#endif
#ifndef MAKEWORD
#  define MAKEWORD(a, b) ((unsigned short)(((unsigned char)(a)) | ((unsigned short)((unsigned char)(b))) << 8))
#endif

#define WSAGetLastError() (errno)

struct WSADATA { int unused; };

// Winsock gives send an error for a peer that's gone. Here that's a SIGPIPE
// unless it's ignored.
static inline int WSAStartup(unsigned short, WSADATA *) {
    signal(SIGPIPE, SIG_IGN);
    return 0;
}
static inline int WSACleanup(void) { return 0; }

// Closing a socket on Windows also gets another thread out of accept or
// recv on it, which here takes the shutdown
static inline int closesocket(SOCKET s) {
    ::shutdown(s, SHUT_RDWR);
    return ::close(s);
}

// strncpy_s and wcsncpy_s as the net layer calls them, always with _TRUNCATE
#ifndef _TRUNCATE
#  define _TRUNCATE ((size_t)-1)
#endif
static inline int mcle_strncpy_s(char *dst, size_t size, const char *src, size_t) {
    if (!dst || size == 0) return 22;
    size_t n = 0;
    while (src && src[n] && n + 1 < size) { dst[n] = src[n]; ++n; }
    dst[n] = 0;
    return 0;
}
static inline int mcle_wcsncpy_s(wchar_t *dst, size_t size, const wchar_t *src, size_t) {
    if (!dst || size == 0) return 22;
    size_t n = 0;
    while (src && src[n] && n + 1 < size) { dst[n] = src[n]; ++n; }
    dst[n] = 0;
    return 0;
}
#ifndef strncpy_s
#  define strncpy_s mcle_strncpy_s
#endif
#ifndef wcsncpy_s
#  define wcsncpy_s mcle_wcsncpy_s
#endif
