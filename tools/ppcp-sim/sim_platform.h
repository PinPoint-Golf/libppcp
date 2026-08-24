/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * sim_platform.h — the POSIX/Winsock seam.
 *
 * sim_net.c, sim_run.c, sim_tls.c and main.c are written once, as POSIX C11:
 * plain `int` file descriptors, `close()`, `poll()`, `recv()`/`send()`,
 * `strtok_r()`. This header is the only thing in the tool that knows Windows
 * exists — on POSIX it is nearly empty, and on Windows it makes the same
 * source compile and behave the same way, through small wrapper functions
 * rather than through changes at the call sites.
 *
 * WHY WRAPPERS AND NOT A `SOCKET`-TYPED PORT.  `SOCKET` is a 64-bit handle on
 * Win64 and every struct and signature in this tool holds a socket as `int`
 * (sim_chan.fd, every function parameter). Re-typing all of it was more
 * surface than the tool needed to change to run on Windows. `int` holds every
 * value a `SOCKET` from this process actually takes in practice, and
 * INVALID_SOCKET truncates to exactly -1 when cast down — which is the
 * sentinel this code already checks for — so the wrappers narrow at the one
 * seam (the return from socket()/accept()) and widen back out at every call,
 * and nothing above this file needs to know a SOCKET was ever wider than an
 * int.
 *
 * WHY errno AND NOT WSAGetLastError().  Every failure check in this tool
 * reads `errno` (EINTR, EAGAIN, EWOULDBLOCK, ECONNRESET, EPIPE) because that
 * is what POSIX recv/send/poll set. Winsock sets neither errno nor those
 * constants — it sets its own WSAE* codes via WSAGetLastError(). Redefining
 * the constants to their WSAE equivalents and having each wrapper below copy
 * WSAGetLastError() into errno on failure means every existing `errno ==`
 * comparison in the three networking files is correct unchanged.
 */
#ifndef PPCP_SIM_PLATFORM_H
#define PPCP_SIM_PLATFORM_H

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if !defined(_SSIZE_T_DEFINED)
typedef int ssize_t;
#define _SSIZE_T_DEFINED
#endif
typedef ULONG nfds_t;
/* struct pollfd, WSAPOLLFD, POLLIN/POLLHUP/POLLERR: winsock2.h already
 * defines all of these under the POSIX names, deliberately, for exactly this
 * kind of port. Nothing to shim. */

/* Winsock failures never set these to anything; every wrapper below sets
 * errno to WSAGetLastError() on failure, so the constants have to name the
 * same numbers WSAGetLastError() returns. */
#undef EINTR
#define EINTR       WSAEINTR
#undef EAGAIN
#define EAGAIN      WSAEWOULDBLOCK
#undef EWOULDBLOCK
#define EWOULDBLOCK WSAEWOULDBLOCK
#undef ECONNRESET
#define ECONNRESET  WSAECONNRESET
#undef EPIPE
#define EPIPE       WSAECONNABORTED  /* nearest Winsock analogue: a send to a
                                        socket the peer already reset */

/* One-time startup, called by whichever of sim_listen()/sim_connect()/the
 * TLS dial() opens the process's first socket. Never torn down: the process
 * exits through this tool's normal `return` from main(), and WSACleanup()
 * has nothing left to clean up more usefully than process exit already does. */
void sim_win_wsa_ensure(void);

int     sim_win_close(int fd);
int     sim_win_socket(int family, int type, int proto);
int     sim_win_accept(int fd, struct sockaddr *addr, socklen_t *addrlen);
ssize_t sim_win_recv(int fd, void *buf, size_t len, int flags);
ssize_t sim_win_send(int fd, const void *buf, size_t len, int flags);
int     sim_win_poll(struct pollfd *fds, nfds_t n, int timeout_ms);
/* Windows' optval is `const char *`, not `const void *` — a hard type error
 * on MSVC, not just a warning, at every existing setsockopt(fd, ..., &int)
 * call site. */
int     sim_win_setsockopt(int fd, int level, int optname, const void *optval, size_t optlen);

#define close(fd)                   sim_win_close(fd)
#define socket(f, t, p)             sim_win_socket((f), (t), (p))
#define accept(fd, a, l)            sim_win_accept((fd), (a), (l))
#define recv(fd, b, n, f)           sim_win_recv((fd), (b), (n), (f))
#define send(fd, b, n, f)           sim_win_send((fd), (b), (n), (f))
#define poll(fds, n, to)            sim_win_poll((fds), (n), (to))
#define setsockopt(fd, l, o, v, n)  sim_win_setsockopt((fd), (l), (o), (v), (n))

/* MSG_DONTWAIT is Linux-only; sim_win_recv's ONE caller that passes it
 * (sim_run.c's pump_rx) gets the same effect by toggling FIONBIO around that
 * one call rather than leaving the socket non-blocking for send() too. */
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x1000000
#endif

/* strtok_s takes the same three arguments in the same order. */
#define strtok_r(s, d, ctx) strtok_s((s), (d), (ctx))

/* sim_decl.c's sim_now_ns() calls this directly on Windows rather than
 * routing through a faked clock_gettime()/CLOCK_MONOTONIC: nothing else in
 * this tool touches those POSIX names, so there is nothing to gain from
 * pretending they exist. QueryPerformanceCounter is this platform's
 * monotonic clock. */
int64_t sim_win_monotonic_ns(void);

/* sim_net.c's link_id and sim_tls.c's ClientHello randoms both read
 * /dev/urandom directly; Windows has no such device. BCryptGenRandom with the
 * system-preferred generator is the analogue: CNG's own pool, no seeding,
 * fills the buffer or fails outright. */
bool sim_win_random_bytes(void *out, size_t n);

/* strerror() itself is unchanged — thread-unsafe on every platform alike,
 * which nothing here cares about — but MSVC flags the plain call C4996 in
 * favour of strerror_s(), a Microsoft/Annex-K extension with no Linux/macOS
 * equivalent. One suppressed call site beats the same #pragma pair repeated
 * at every strerror(errno) in this tool. */
const char *sim_win_strerror(int e);
#define strerror(e) sim_win_strerror(e)

#else /* POSIX */

/* Nothing to shim; sim_net.c, sim_run.c, sim_tls.c and main.c already
 * #include the POSIX headers they need directly. */

#endif

#endif /* PPCP_SIM_PLATFORM_H */
