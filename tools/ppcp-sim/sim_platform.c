/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * sim_platform.c — the Windows half of sim_platform.h. Empty on POSIX: the
 * three networking files call the real recv()/send()/accept()/close()/poll()
 * directly there, and this translation unit exists so it can be listed in
 * CMakeLists.txt unconditionally rather than the build needing to know, per
 * platform, which files to compile.
 */
#if defined(_WIN32)

#include "sim_platform.h"

#include <bcrypt.h>
#include <stdbool.h>

static bool g_wsa_ready = false;

void sim_win_wsa_ensure(void)
{
    if (!g_wsa_ready) {
        WSADATA wsa;
        /* 2.2 is what every Windows version since XP SP2 offers, and nothing
         * here needs anything newer. A failure here is unrecoverable for a
         * tool whose entire job is sockets, so it is not reported through the
         * usual err/err_len path — every caller would just fail the same way
         * one call later with a less clear message. */
        (void)WSAStartup(MAKEWORD(2, 2), &wsa);
        g_wsa_ready = true;
    }
}

int sim_win_close(int fd)
{
    int rc = closesocket((SOCKET)fd);
    if (rc != 0)
        errno = WSAGetLastError();
    return rc;
}

/* ⚠ EVERY WINSOCK CALL BELOW IS WRITTEN `(name)(args)`, NOT `name(args)`.
 * sim_platform.h #defines socket/accept/recv/send/setsockopt to the
 * sim_win_* wrapper below each one — including inside THIS file, which
 * includes that header too. Call `socket(...)` here and the macro rewrites
 * it to `sim_win_socket(...)`, i.e. the wrapper calling itself: infinite
 * recursion, caught only at runtime by a stack overflow (and by MSVC's own
 * C4717 warning, which is what caught it here). A macro only expands when
 * its name is immediately followed by `(`; `(socket)(args)` breaks that
 * adjacency and reaches the real Winsock function instead. */

int sim_win_socket(int family, int type, int proto)
{
    SOCKET s;
    sim_win_wsa_ensure();
    s = (socket)(family, type, proto);
    if (s == INVALID_SOCKET) {
        errno = WSAGetLastError();
        return -1;
    }
    return (int)s;
}

int sim_win_accept(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
    SOCKET s = (accept)((SOCKET)fd, addr, addrlen);
    if (s == INVALID_SOCKET) {
        errno = WSAGetLastError();
        return -1;
    }
    return (int)s;
}

ssize_t sim_win_recv(int fd, void *buf, size_t len, int flags)
{
    int     want_nonblock = (flags & MSG_DONTWAIT) != 0;
    ssize_t got;

    if (want_nonblock) {
        u_long mode = 1;
        ioctlsocket((SOCKET)fd, FIONBIO, &mode);
        flags &= ~MSG_DONTWAIT;
    }
    got = (ssize_t)(recv)((SOCKET)fd, (char *)buf, (int)len, flags);
    if (got < 0)
        errno = WSAGetLastError();
    if (want_nonblock) {
        u_long mode = 0;
        ioctlsocket((SOCKET)fd, FIONBIO, &mode);
    }
    return got;
}

ssize_t sim_win_send(int fd, const void *buf, size_t len, int flags)
{
    ssize_t wrote = (ssize_t)(send)((SOCKET)fd, (const char *)buf, (int)len, flags);
    if (wrote < 0)
        errno = WSAGetLastError();
    return wrote;
}

int sim_win_poll(struct pollfd *fds, nfds_t n, int timeout_ms)
{
    int rc = WSAPoll(fds, n, timeout_ms);
    if (rc < 0)
        errno = WSAGetLastError();
    return rc;
}

int sim_win_setsockopt(int fd, int level, int optname, const void *optval, size_t optlen)
{
    int rc = (setsockopt)((SOCKET)fd, level, optname, (const char *)optval, (int)optlen);
    if (rc != 0)
        errno = WSAGetLastError();
    return rc;
}

int64_t sim_win_monotonic_ns(void)
{
    static LARGE_INTEGER freq;
    static bool          have_freq = false;
    LARGE_INTEGER        now;

    if (!have_freq) {
        QueryPerformanceFrequency(&freq);
        have_freq = true;
    }
    QueryPerformanceCounter(&now);
    /* freq is ticks/second; scale to nanoseconds before dividing so a sub-tick
     * remainder isn't lost, same reasoning as any fixed-point conversion. */
    return (int64_t)((double)now.QuadPart * 1.0e9 / (double)freq.QuadPart);
}

bool sim_win_random_bytes(void *out, size_t n)
{
    return BCryptGenRandom(NULL, (PUCHAR)out, (ULONG)n,
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 /* STATUS_SUCCESS */;
}

const char *sim_win_strerror(int e)
{
#pragma warning(push)
#pragma warning(disable : 4996)
    return (strerror)(e);
#pragma warning(pop)
}

#else
typedef int sim_platform_c_is_not_empty_on_posix;
#endif
