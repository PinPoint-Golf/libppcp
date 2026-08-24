/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * rl_net.c — the sockets.  POSIX C11, loopback and the local network only.
 *
 * This is a test instrument, not a product: it binds and dials, it does not
 * advertise or browse.  A bootstrap connection is a plain TCP connection to
 * the port a window published (3.7f), carrying the frames of §11.4 and
 * nothing else — no TLS, because there is no key yet, which is the whole
 * reason §11 exists.
 */
#include "relay.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

int64_t rl_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void rl_close(int fd)
{
    if (fd >= 0)
        close(fd);
}

static void set_nodelay(int fd)
{
    int on = 1;
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
}

int rl_listen(int port, int *out_port, char *err, size_t errlen)
{
    struct sockaddr_in sa;
    socklen_t          sl = sizeof(sa);
    int                fd, on = 1;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        snprintf(err, errlen, "socket: %s", strerror(errno));
        return -1;
    }
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    memset(&sa, 0, sizeof(sa));
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port        = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        snprintf(err, errlen, "bind %d: %s", port, strerror(errno));
        close(fd);
        return -1;
    }
    if (listen(fd, 4) != 0) {
        snprintf(err, errlen, "listen: %s", strerror(errno));
        close(fd);
        return -1;
    }
    if (out_port != NULL) {
        memset(&sa, 0, sizeof(sa));
        if (getsockname(fd, (struct sockaddr *)&sa, &sl) == 0)
            *out_port = ntohs(sa.sin_port);
        else
            *out_port = port;
    }
    return fd;
}

int rl_accept(int lfd, int timeout_ms, char *err, size_t errlen)
{
    struct pollfd p;
    int           fd;

    p.fd = lfd; p.events = POLLIN; p.revents = 0;
    for (;;) {
        int r = poll(&p, 1, timeout_ms);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            snprintf(err, errlen, "poll: %s", strerror(errno));
            return -1;
        }
        if (r == 0) {
            snprintf(err, errlen, "no connection within %d ms", timeout_ms);
            return -1;
        }
        break;
    }
    fd = accept(lfd, NULL, NULL);
    if (fd < 0) {
        snprintf(err, errlen, "accept: %s", strerror(errno));
        return -1;
    }
    set_nodelay(fd);
    return fd;
}

int rl_connect(const char *host, int port, int timeout_ms, char *err, size_t errlen)
{
    struct addrinfo  hints, *res = NULL, *ai;
    char             portstr[16];
    int              fd = -1, rc;
    int64_t          deadline = rl_now_ms() + timeout_ms;

    snprintf(portstr, sizeof(portstr), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    rc = getaddrinfo(host, portstr, &hints, &res);
    if (rc != 0) {
        snprintf(err, errlen, "resolve %s: %s", host, gai_strerror(rc));
        return -1;
    }
    for (ai = res; ai != NULL; ai = ai->ai_next) {
        int flags;
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        flags = fcntl(fd, F_GETFL, 0);
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            goto connected;
        if (errno == EINPROGRESS) {
            struct pollfd p;
            int           left = (int)(deadline - rl_now_ms());
            p.fd = fd; p.events = POLLOUT; p.revents = 0;
            if (left < 0)
                left = 0;
            if (poll(&p, 1, left) == 1) {
                int       serr = 0;
                socklen_t sl   = sizeof(serr);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &serr, &sl) == 0 && serr == 0)
                    goto connected;
                errno = serr != 0 ? serr : ETIMEDOUT;
            } else {
                errno = ETIMEDOUT;
            }
        }
        snprintf(err, errlen, "connect %s:%d: %s", host, port, strerror(errno));
        close(fd);
        fd = -1;
        continue;
    connected:
        (void)fcntl(fd, F_SETFL, flags);
        set_nodelay(fd);
        freeaddrinfo(res);
        return fd;
    }
    freeaddrinfo(res);
    if (fd < 0 && err[0] == '\0')
        snprintf(err, errlen, "connect %s:%d failed", host, port);
    return -1;
}

bool rl_write_all(int fd, const uint8_t *buf, size_t len, char *err, size_t errlen)
{
    while (len > 0) {
        ssize_t w = send(fd, buf, len, 0);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            snprintf(err, errlen, "send: %s", strerror(errno));
            return false;
        }
        buf += (size_t)w;
        len -= (size_t)w;
    }
    return true;
}
