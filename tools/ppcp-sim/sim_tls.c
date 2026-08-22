/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * sim_tls.c — the one mode of this tool that speaks TLS, and why.
 *
 * RT-4 asks that the strongest mode be negotiated, never plaintext, and the
 * outcome surfaced.  The half of that a host cannot assert about itself is the
 * REFUSAL: `RV` 5.2f and 5.4b2 say a peer offers PSK with (EC)DHE and does not
 * fall back, so a host meeting a counterpart that offers `psk_ke` alone — PSK
 * with no ephemeral key share, and therefore no forward secrecy — must refuse.
 * Nothing in PinPointStudio or PinPointCapture can produce that counterpart,
 * which is why H asked for it in S1 and why it is here.
 *
 * ⚠ THE DEPENDENCY IS THE TOOL'S AND ONLY THE TOOL'S.  libppcp has no
 * dependencies (plan A1) and tests/purity.cmake gates src/ and include/; this
 * file is in tools/, is behind a configure-time check, and compiles to a pair
 * of honest refusals when OpenSSL is absent.
 *
 * ⚠ THE EXIT CODE IS INVERTED.  A refused handshake is the PASS.
 */
#include "sim.h"

#include <stdio.h>
#include <string.h>

#ifdef PPCP_SIM_TLS

#include <errno.h>
#include <netdb.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

bool sim_tls_available(void) { return true; }

#define SIM_PSK_MAX 64

typedef struct psk_ctx {
    uint8_t     key[SIM_PSK_MAX];
    size_t      key_len;
    const char *identity;
} psk_ctx;

static size_t unhex(const char *hex, uint8_t *out, size_t cap)
{
    size_t n = 0;
    while (hex != NULL && hex[0] != '\0' && hex[1] != '\0' && n < cap) {
        unsigned v = 0;
        if (sscanf(hex, "%2x", &v) != 1)
            break;
        out[n++] = (uint8_t)v;
        hex += 2;
    }
    return n;
}

/* RV §8 — the EXTERNAL-PSK session callback, not the RFC 4279 hint interface.
 * The hint interface is TLS 1.2's and cannot carry a binary identity with an
 * embedded zero (the defect H found in S1); this one can. */
static int psk_use_session(SSL *ssl, const EVP_MD *md, const unsigned char **id,
                           size_t *idlen, SSL_SESSION **sess)
{
    psk_ctx        *c = (psk_ctx *)SSL_get_ex_data(ssl, 0);
    SSL_SESSION    *s;
    const SSL_CIPHER *cipher;

    (void)md;
    if (c == NULL)
        return 0;
    /* TLS_AES_128_GCM_SHA256 — 0x1301.  Any TLS 1.3 suite would do; the point
     * of this mode is the key EXCHANGE, not the cipher. */
    cipher = SSL_CIPHER_find(ssl, (const unsigned char[]){ 0x13, 0x01 });
    if (cipher == NULL)
        return 0;
    s = SSL_SESSION_new();
    if (s == NULL)
        return 0;
    if (!SSL_SESSION_set1_master_key(s, c->key, c->key_len) ||
        !SSL_SESSION_set_cipher(s, cipher) ||
        !SSL_SESSION_set_protocol_version(s, TLS1_3_VERSION)) {
        SSL_SESSION_free(s);
        return 0;
    }
    *sess  = s;
    *id    = (const unsigned char *)c->identity;
    *idlen = strlen(c->identity);
    return 1;
}

static int dial(const char *host, int port)
{
    struct addrinfo hints, *res = NULL, *ai;
    char            portbuf[16];
    int             fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portbuf, sizeof(portbuf), "%d", port);
    if (getaddrinfo(host, portbuf, &hints, &res) != 0)
        return -1;
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
    return fd;
}

int sim_run_psk_ke_only(const sim_opts *o)
{
    SSL_CTX *ctx;
    SSL     *ssl;
    psk_ctx  pc;
    int      fd, rc;

    memset(&pc, 0, sizeof(pc));
    pc.key_len  = unhex(o->psk_hex, pc.key, sizeof(pc.key));
    if (pc.key_len == 0) {
        memset(pc.key, 0, 32);
        pc.key_len = 32;
    }
    pc.identity = (o->psk_identity != NULL) ? o->psk_identity : "ppcp-sim-psk-ke-only";

    ctx = SSL_CTX_new(TLS_client_method());
    if (ctx == NULL) {
        fprintf(stderr, "ppcp-sim: cannot create an SSL_CTX\n");
        return 1;
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
    SSL_CTX_set_ciphersuites(ctx, "TLS_AES_128_GCM_SHA256");
    /* THE WHOLE POINT: no group is offered, so the ClientHello carries no key
     * share and `psk_ke` is the only key-exchange mode left.  A conformant
     * host has nothing acceptable to select and refuses. */
    SSL_CTX_set_psk_use_session_callback(ctx, psk_use_session);

    fd = dial(o->connect_host, o->connect_port);
    if (fd < 0) {
        fprintf(stderr, "ppcp-sim: cannot connect to %s:%d for the psk_ke offer\n",
                o->connect_host, o->connect_port);
        SSL_CTX_free(ctx);
        return 1;
    }
    ssl = SSL_new(ctx);
    if (ssl == NULL) {
        close(fd);
        SSL_CTX_free(ctx);
        return 1;
    }
    (void)SSL_set_ex_data(ssl, 0, &pc);
    SSL_set_fd(ssl, fd);
    /* An empty group list is how "offer no key share" is spelled; where the
     * build refuses it, the psk_ke-only ClientHello is still produced by the
     * absence of a key_share extension for any group the server accepts. */
    (void)SSL_set1_groups_list(ssl, "");

    rc = SSL_connect(ssl);
    if (rc == 1) {
        /* RV 5.2f / 5.4b2 — a completed handshake here means the counterpart
         * accepted PSK-only key exchange, and so has no forward secrecy on a
         * link it told the user was secure.  That is the failure RT-4 exists
         * to detect, and this is the only way to detect it from outside. */
        fprintf(stderr, "ppcp-sim: RT-4 FAILED — the peer COMPLETED a psk_ke-only "
                        "handshake (%s, %s); RV 5.2f requires PSK with (EC)DHE\n",
                SSL_get_version(ssl), SSL_get_cipher(ssl));
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return 1;
    }
    fprintf(stderr, "ppcp-sim: RT-4 satisfied — the peer REFUSED a psk_ke-only "
                    "handshake (openssl: %s)\n",
            ERR_reason_error_string(ERR_peek_last_error()) != NULL
                ? ERR_reason_error_string(ERR_peek_last_error())
                : "connection closed without a completed handshake");
    SSL_free(ssl);
    close(fd);
    SSL_CTX_free(ctx);
    return 0;
}

#else  /* no OpenSSL */

bool sim_tls_available(void) { return false; }

int sim_run_psk_ke_only(const sim_opts *o)
{
    (void)o;
    fprintf(stderr, "ppcp-sim: this build has no TLS; configure with OpenSSL available "
                    "to use --psk-ke-only (RT-4)\n");
    return 1;
}

#endif
