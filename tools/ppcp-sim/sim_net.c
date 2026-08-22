/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * sim_net.c — two TCP connections, and the link binding that tells them apart.
 *
 * Plan A6: a peer pair is two TCP connections, channel 0 and channel 1.  Which
 * accepted socket is which is not a property of the socket and never was —
 * erratum E1 made it explicit `link_bind` traffic (ENC §2.1), and this file is
 * the listener half using ppcp_link_binder to do it.  Nothing here guesses from
 * arrival order, which is the rule the erratum withdrew.
 */
#include "sim.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static void chan_init(sim_chan *c)
{
    c->fd     = -1;
    c->rx     = (uint8_t *)malloc(SIM_RX_CAP);
    c->rx_len = 0;
    c->open   = false;
}

void sim_link_close(sim_link *l)
{
    size_t i;
    for (i = 0; i < SIM_CH_COUNT; i++) {
        if (l->ch[i].fd >= 0)
            close(l->ch[i].fd);
        l->ch[i].fd = -1;
        free(l->ch[i].rx);
        l->ch[i].rx = NULL;
        l->ch[i].open = false;
    }
}

static void set_nodelay(int fd)
{
    int one = 1;
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

/* --------------------------------------------------------------- the log */

static const char *g_who   = "sim";
static bool        g_quiet = false;

void sim_log_configure(const char *who, bool quiet)
{
    if (who != NULL)
        g_who = who;
    g_quiet = quiet;
}

void sim_log_frames(const char *who, const char *dir, uint8_t channel,
                    const uint8_t *bytes, size_t len)
{
    size_t off = 0;

    if (g_quiet)
        return;
    if (who == NULL)
        who = g_who;

    while (off + PPCP_FRAME_HEADER_BYTES <= len) {
        ppcp_frame_header hdr;
        const uint8_t    *payload = NULL;
        size_t            consumed = 0;
        ppcp_msg          m;
        const char       *name = "?";

        if (ppcp_frame_header_parse(bytes + off, &hdr) != PPCP_OK)
            return;
        if (ppcp_frame_read(bytes + off, len - off, &hdr, &payload, &consumed) != PPCP_OK)
            return;
        memset(&m, 0, sizeof(m));
        if (ppcp_msg_decode(payload, hdr.payload_len,
                            ppcp_cbor_limits_for_channel(hdr.channel), NULL, &m) == PPCP_OK &&
            ppcp_id_is_set(&m.type_name)) {
            name = m.type_name.v;
        }
        /* ENC 2c — the channel byte in the header against the stream the
         * frame arrived on.  `channel` is 255 while a stream is still being
         * bound, which is the one moment the embedding does not yet know it. */
        if (channel != PPCP_CHANNEL_RESERVED && hdr.channel != channel)
            fprintf(stderr, "%s %s ch%u *** ENC 2c: header says channel %u\n",
                    who, dir, (unsigned)channel, (unsigned)hdr.channel);
        fprintf(stderr, "%s %s ch%u %-18s %6u bytes  msg_id=%llu\n",
                who, dir, (unsigned)hdr.channel, name, (unsigned)hdr.payload_len,
                (unsigned long long)m.env.msg_id);
        off += consumed;
    }
}

/* ------------------------------------------------------------- the dialler */

static int dial_once(const char *host, int port, char *err, size_t err_len)
{
    struct addrinfo hints, *res = NULL, *ai;
    char            portbuf[16];
    int             fd = -1;
    int             rc;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portbuf, sizeof(portbuf), "%d", port);
    rc = getaddrinfo(host, portbuf, &hints, &res);
    if (rc != 0) {
        snprintf(err, err_len, "cannot resolve %s: %s", host, gai_strerror(rc));
        return -1;
    }
    for (ai = res; ai != NULL; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0)
        snprintf(err, err_len, "cannot connect to %s:%d: %s", host, port, strerror(errno));
    else
        set_nodelay(fd);
    return fd;
}

/* Writes everything the engine has queued on `channel` down `fd`, using the
 * peek/commit path: a short write on a socket is normal and ppcp_peer_drain()
 * would have forgotten the bytes it did not take (plan §9, H, 22 Aug 2026). */
static bool flush_channel(ppcp_peer *p, uint8_t channel, int fd,
                          char *err, size_t err_len)
{
    for (;;) {
        const uint8_t *bytes = NULL;
        size_t         len = 0;
        ssize_t        wrote;

        if (ppcp_peer_drain_peek(p, channel, &bytes, &len) != PPCP_OK || len == 0)
            return true;
        wrote = send(fd, bytes, len, 0);
        if (wrote < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return true;
            snprintf(err, err_len, "write on channel %u failed: %s",
                     (unsigned)channel, strerror(errno));
            return false;
        }
        if (wrote == 0)
            return true;
        sim_log_frames(NULL, "TX", channel, bytes, (size_t)wrote);
        if (ppcp_peer_drain_commit(p, channel, (size_t)wrote) != PPCP_OK) {
            snprintf(err, err_len, "the engine refused a commit of %zd bytes", wrote);
            return false;
        }
    }
}

bool sim_connect(sim_link *l, const char *host, int port, int64_t timeout_ms,
                 ppcp_peer *p, char *err, size_t err_len)
{
    uint8_t link_id[PPCP_LINK_ID_BYTES];
    size_t  i;
    FILE   *rnd;

    (void)timeout_ms;
    for (i = 0; i < SIM_CH_COUNT; i++)
        chan_init(&l->ch[i]);

    /* ENC 2.1a — 16 bytes from a CSPRNG, minted by the dialler.  The library
     * has no random source (ground rule 8), so the embedding supplies them;
     * here the embedding is this file. */
    rnd = fopen("/dev/urandom", "rb");
    if (rnd == NULL || fread(link_id, 1, sizeof(link_id), rnd) != sizeof(link_id)) {
        snprintf(err, err_len, "cannot read /dev/urandom for the link_id");
        if (rnd != NULL)
            fclose(rnd);
        return false;
    }
    fclose(rnd);
    if (ppcp_peer_set_link_id(p, link_id) != PPCP_OK) {
        snprintf(err, err_len, "the engine refused the link_id (is this peer a listener?)");
        return false;
    }

    for (i = 0; i < SIM_CH_COUNT; i++) {
        int fd = dial_once(host, port, err, err_len);
        if (fd < 0)
            return false;
        l->ch[i].fd   = fd;
        l->ch[i].open = true;
    }

    /* 2.1d — channel 0's `link_bind` precedes its `hello`; a bulk channel is
     * bound with the same `link_id` at any later point. */
    if (ppcp_peer_open_channel(p, PPCP_CHANNEL_CONTROL) != PPCP_OK ||
        ppcp_peer_hello(p) != PPCP_OK ||
        ppcp_peer_open_channel(p, PPCP_CHANNEL_BULK) != PPCP_OK) {
        snprintf(err, err_len, "the engine refused to open its channels");
        return false;
    }
    if (!flush_channel(p, PPCP_CHANNEL_CONTROL, l->ch[0].fd, err, err_len))
        return false;
    if (!flush_channel(p, PPCP_CHANNEL_BULK, l->ch[1].fd, err, err_len))
        return false;
    return true;
}

/* ------------------------------------------------------------ the listener */

bool sim_listen(sim_link *l, int port, const char *port_file, int64_t timeout_ms,
                ppcp_peer *p, char *err, size_t err_len)
{
    struct sockaddr_in6 addr;
    socklen_t           alen;
    int                 srv;
    int                 one = 1, off = 0;
    size_t              i;
    ppcp_link_binder    binder;
    int                 pending[SIM_CH_COUNT];
    size_t              pending_count = 0;
    int64_t             deadline = sim_now_ns() + timeout_ms * 1000000;

    for (i = 0; i < SIM_CH_COUNT; i++) {
        chan_init(&l->ch[i]);
        pending[i] = -1;
    }
    ppcp_link_binder_init(&binder);

    srv = socket(AF_INET6, SOCK_STREAM, 0);
    if (srv < 0) {
        snprintf(err, err_len, "cannot create a listening socket: %s", strerror(errno));
        return false;
    }
    (void)setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    (void)setsockopt(srv, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr   = in6addr_any;
    addr.sin6_port   = htons((uint16_t)port);
    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(srv, 8) != 0) {
        snprintf(err, err_len, "cannot listen on port %d: %s", port, strerror(errno));
        close(srv);
        return false;
    }
    alen = sizeof(addr);
    if (getsockname(srv, (struct sockaddr *)&addr, &alen) == 0)
        port = ntohs(addr.sin6_port);

    /* The port is written only after the socket is listening, so a driver that
     * waits for the file and then dials cannot lose the race. */
    if (port_file != NULL) {
        char  tmp[512];
        FILE *f;
        snprintf(tmp, sizeof(tmp), "%s.tmp", port_file);
        f = fopen(tmp, "w");
        if (f == NULL) {
            snprintf(err, err_len, "cannot write the port file %s", port_file);
            close(srv);
            return false;
        }
        fprintf(f, "%d\n", port);
        fclose(f);
        if (rename(tmp, port_file) != 0) {
            snprintf(err, err_len, "cannot rename the port file into place");
            close(srv);
            return false;
        }
    }
    fprintf(stderr, "sim listening on port %d\n", port);

    /* Accept until every channel of one link is bound.  Two connections and
     * two bound channels are separate conditions: which socket carries which
     * channel is what the `link_bind` says, not what the accept order was. */
    {
        size_t accepted = 0;

        while (!(l->ch[0].open && l->ch[1].open)) {
            struct pollfd pfd[SIM_CH_COUNT + 1];
            nfds_t        nfd = 0;
            size_t        k;
            int           rc;
            int64_t       left = (deadline - sim_now_ns()) / 1000000;

            if (left <= 0) {
                snprintf(err, err_len, "timed out waiting for two bound channels");
                close(srv);
                return false;
            }
            if (accepted < SIM_CH_COUNT) {
                pfd[nfd].fd     = srv;
                pfd[nfd].events = POLLIN;
                nfd++;
            }
            for (k = 0; k < pending_count; k++) {
                pfd[nfd].fd     = pending[k];
                pfd[nfd].events = POLLIN;
                nfd++;
            }
            rc = poll(pfd, nfd, (int)(left > 200 ? 200 : left));
            if (rc < 0) {
                if (errno == EINTR)
                    continue;
                snprintf(err, err_len, "poll failed: %s", strerror(errno));
                close(srv);
                return false;
            }
            for (k = 0; k < nfd; k++) {
                uint8_t     buf[4096];
                ssize_t     got;
                size_t      consumed = 0, link = 0, j;
                uint8_t     channel = 0;
                ppcp_result rc2;

                if ((pfd[k].revents & (POLLIN | POLLHUP | POLLERR)) == 0)
                    continue;
                if (pfd[k].fd == srv) {
                    int fd = accept(srv, NULL, NULL);
                    if (fd >= 0) {
                        set_nodelay(fd);
                        pending[pending_count++] = fd;
                        accepted++;
                    }
                    continue;
                }
                got = recv(pfd[k].fd, buf, sizeof(buf), 0);
                if (got <= 0) {
                    snprintf(err, err_len, "a peer closed a stream before binding it");
                    close(srv);
                    return false;
                }
                sim_log_frames(NULL, "RX", PPCP_CHANNEL_RESERVED, buf, (size_t)got);
                rc2 = ppcp_link_binder_offer(&binder, buf, (size_t)got, &consumed,
                                             &link, &channel);
                if (rc2 == PPCP_ERR_TRUNCATED)
                    continue;
                if (rc2 != PPCP_OK) {
                    snprintf(err, err_len,
                             "ENC 2.1c: the first frame on a stream was refused (%s)",
                             ppcp_result_str(rc2));
                    close(srv);
                    return false;
                }
                if (link != 0) {
                    snprintf(err, err_len,
                             "a second link_id was offered; this simulator binds one link");
                    close(srv);
                    return false;
                }
                if (channel >= SIM_CH_COUNT) {
                    snprintf(err, err_len, "channel %u is beyond this simulator's two",
                             (unsigned)channel);
                    close(srv);
                    return false;
                }
                l->ch[channel].fd   = pfd[k].fd;
                l->ch[channel].open = true;
                if ((size_t)got > consumed) {
                    size_t remain = (size_t)got - consumed;
                    memcpy(l->ch[channel].rx, buf + consumed, remain);
                    l->ch[channel].rx_len = remain;
                }
                for (j = 0; j < pending_count; j++) {
                    if (pending[j] == pfd[k].fd) {
                        pending[j] = pending[pending_count - 1];
                        pending_count--;
                        break;
                    }
                }
            }
        }
    }
    close(srv);
    (void)p;
    return l->ch[0].open && l->ch[1].open;
}
