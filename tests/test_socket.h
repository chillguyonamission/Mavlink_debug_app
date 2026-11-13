#pragma once
// The few BSD-socket calls the loopback tests need, on POSIX and Winsock. Test-only.

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using test_socket_t = SOCKET;
inline void testSocketInit()
{
    static WSADATA w;
    static const int rc = WSAStartup(MAKEWORD(2, 2), &w);
    (void)rc;
}
inline void closeTestSocket(test_socket_t s) { closesocket(s); }
inline void setRecvTimeout(test_socket_t s, int ms)
{
    DWORD t = DWORD(ms);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&t, sizeof t);
}
inline int recvNow(test_socket_t s, void *b, int n)  // non-blocking read
{
    u_long on = 1, off = 0;
    ioctlsocket(s, FIONBIO, &on);
    const int r = recv(s, (char *)b, n, 0);
    ioctlsocket(s, FIONBIO, &off);
    return r;
}
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
using test_socket_t = int;
inline void testSocketInit() {}
inline void closeTestSocket(test_socket_t s) { close(s); }
inline void setRecvTimeout(test_socket_t s, int ms)
{
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
}
inline int recvNow(test_socket_t s, void *b, int n) { return int(recv(s, b, size_t(n), MSG_DONTWAIT)); }
#endif
