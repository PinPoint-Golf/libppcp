/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * sim_tls.c — a TLS 1.3 ClientHello that offers `psk_ke` and nothing else.
 *
 * WHY THIS EXISTS.  RT-4 asks that the strongest mode be negotiated, never
 * plaintext, and the outcome surfaced.  The half a host cannot assert about
 * itself is the REFUSAL: `RV` 5.2f and 5.4b2 say a peer offers PSK with
 * (EC)DHE and does not fall back, so a host meeting a counterpart that offers
 * `psk_ke` alone — PSK with no ephemeral key share, and therefore no forward
 * secrecy — must refuse.  Nothing in PinPointStudio or PinPointCapture can
 * produce that counterpart, which is why H asked for it in S1.
 *
 * WHY IT IS HAND-BUILT AND NOT OpenSSL.  It was written against OpenSSL first,
 * and OpenSSL will not do it: its client always advertises `psk_dhe_ke`, and
 * `SSL_set1_groups_list(ssl, "")` makes the LOCAL stack refuse to construct a
 * ClientHello ("no suitable groups") before a byte reaches the wire.  That
 * produced a handshake failure that looked exactly like the host's refusal and
 * would have been recorded as RT-4 evidence.  It is not evidence: nothing was
 * ever offered.  So the ClientHello is built here, byte by byte, and the
 * library's own HKDF-SHA256 and HMAC-SHA256 compute the PSK binder — which
 * also means this tool has no dependencies at all, and `libppcp` still has
 * none either.
 *
 * ⚠ WHAT IT PROVES AND WHAT IT DOES NOT.  A ServerHello carrying
 * `pre_shared_key` and no `key_share` means the peer ACCEPTED PSK-only key
 * exchange, and that is an unambiguous RT-4 failure.  A refusal is reported
 * with the peer's alert description, because the description is what tells a
 * reviewer whether the peer refused the MODE (`handshake_failure`,
 * `illegal_parameter`, `missing_extension`) or refused the offer for some other
 * reason — an unknown identity, or a binder this tool computed wrongly
 * (`decrypt_error`).  The exit code says refused-or-not; the alert is what the
 * claim file should quote.
 *
 * ⚠ THE EXIT CODE IS INVERTED ON PURPOSE.  A refused handshake is the PASS.
 *
 * RFC 8446 §4.1.2 (ClientHello), §4.2.11 (pre_shared_key), §4.2.9
 * (psk_key_exchange_modes), §4.2.11.2 (the binder), §7.1 (HKDF-Expand-Label).
 */
#include "sim.h"
#include "sim_platform.h"

#include "ppcp/hash.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

/* The mode needs no library that this repository does not already contain. */
bool sim_tls_available(void) { return true; }

#define TLS_MAX_HELLO   1024
#define TLS_HASH_BYTES  PPCP_SHA256_BYTES

typedef struct tls_buf {
    uint8_t b[TLS_MAX_HELLO];
    size_t  n;
    bool    overflow;
} tls_buf;

static void put(tls_buf *w, const void *src, size_t len)
{
    if (w->n + len > sizeof(w->b)) {
        w->overflow = true;
        return;
    }
    memcpy(w->b + w->n, src, len);
    w->n += len;
}

static void put_u8(tls_buf *w, uint8_t v)  { put(w, &v, 1); }

static void put_u16(tls_buf *w, uint16_t v)
{
    uint8_t t[2];
    t[0] = (uint8_t)(v >> 8);
    t[1] = (uint8_t)(v & 0xFFu);
    put(w, t, 2);
}

static void put_u24(tls_buf *w, uint32_t v)
{
    uint8_t t[3];
    t[0] = (uint8_t)((v >> 16) & 0xFFu);
    t[1] = (uint8_t)((v >> 8) & 0xFFu);
    t[2] = (uint8_t)(v & 0xFFu);
    put(w, t, 3);
}

static void put_u32(tls_buf *w, uint32_t v)
{
    put_u16(w, (uint16_t)(v >> 16));
    put_u16(w, (uint16_t)(v & 0xFFFFu));
}

static void poke_u16(tls_buf *w, size_t at, uint16_t v)
{
    if (at + 2 > w->n)
        return;
    w->b[at]     = (uint8_t)(v >> 8);
    w->b[at + 1] = (uint8_t)(v & 0xFFu);
}

static void poke_u24(tls_buf *w, size_t at, uint32_t v)
{
    if (at + 3 > w->n)
        return;
    w->b[at]     = (uint8_t)((v >> 16) & 0xFFu);
    w->b[at + 1] = (uint8_t)((v >> 8) & 0xFFu);
    w->b[at + 2] = (uint8_t)(v & 0xFFu);
}

/* RFC 8446 §7.1 — HKDF-Expand-Label over the library's own HKDF. */
static void expand_label(const uint8_t secret[TLS_HASH_BYTES], const char *label,
                         const uint8_t *context, size_t context_len,
                         uint8_t *out, size_t out_len)
{
    uint8_t info[2 + 1 + 255 + 1 + 255];
    size_t  n = 0;
    size_t  label_len = strlen(label);

    info[n++] = (uint8_t)(out_len >> 8);
    info[n++] = (uint8_t)(out_len & 0xFFu);
    info[n++] = (uint8_t)(6u + label_len);      /* "tls13 " + label */
    memcpy(info + n, "tls13 ", 6);
    n += 6;
    memcpy(info + n, label, label_len);
    n += label_len;
    info[n++] = (uint8_t)context_len;
    if (context_len > 0) {
        memcpy(info + n, context, context_len);
        n += context_len;
    }
    (void)ppcp_hkdf_expand(secret, info, n, out, out_len);
}

static bool random_bytes(uint8_t *out, size_t n)
{
#if defined(_WIN32)
    return sim_win_random_bytes(out, n);
#else
    FILE  *f = fopen("/dev/urandom", "rb");
    size_t got;
    if (f == NULL)
        return false;
    got = fread(out, 1, n, f);
    fclose(f);
    return got == n;
#endif
}

static size_t unhex(const char *hex, uint8_t *out, size_t cap)
{
    size_t n = 0;
    while (hex != NULL && hex[0] != '\0' && hex[1] != '\0' && n < cap) {
        unsigned v = 0;
        int      scanned;
#ifdef _MSC_VER
        /* sscanf() is portable C, correct here, and the only choice that
           stays true on every platform this file builds on; sscanf_s() is a
           Microsoft/Annex-K extension with no Linux/macOS equivalent. */
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        scanned = sscanf(hex, "%2x", &v);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
        if (scanned != 1)
            break;
        out[n++] = (uint8_t)v;
        hex += 2;
    }
    return n;
}

/* ------------------------------------------------------------ the message */

/* Builds the whole handshake message and fills in the binder.  Returns the
 * number of bytes, or 0. */
static size_t build_client_hello(uint8_t *out, size_t cap,
                                 const uint8_t *psk, size_t psk_len,
                                 const uint8_t *identity, size_t identity_len)
{
    tls_buf w;
    uint8_t random32[32], session_id[32];
    size_t  hs_len_at, ext_len_at, psk_ext_len_at, identities_len_at;
    size_t  truncated_len, binders_len_at;
    uint8_t early_secret[TLS_HASH_BYTES];
    uint8_t binder_key[TLS_HASH_BYTES];
    uint8_t finished_key[TLS_HASH_BYTES];
    uint8_t zeros[TLS_HASH_BYTES];
    uint8_t empty_hash[TLS_HASH_BYTES];
    uint8_t transcript[TLS_HASH_BYTES];
    uint8_t binder[TLS_HASH_BYTES];

    memset(&w, 0, sizeof(w));
    if (!random_bytes(random32, sizeof(random32)) ||
        !random_bytes(session_id, sizeof(session_id)))
        return 0;

    put_u8(&w, 1);                       /* Handshake.msg_type = client_hello */
    hs_len_at = w.n;
    put_u24(&w, 0);                      /* Handshake.length, filled in below  */

    put_u16(&w, 0x0303);                 /* legacy_version                     */
    put(&w, random32, sizeof(random32));
    put_u8(&w, (uint8_t)sizeof(session_id));
    put(&w, session_id, sizeof(session_id));

    put_u16(&w, 2);                      /* cipher_suites: TLS_AES_128_GCM_SHA256 */
    put_u16(&w, 0x1301);
    put_u8(&w, 1);                       /* legacy_compression_methods: null   */
    put_u8(&w, 0);

    ext_len_at = w.n;
    put_u16(&w, 0);                      /* extensions length, filled in below */

    /* supported_versions (43): TLS 1.3 only. */
    put_u16(&w, 43);
    put_u16(&w, 3);
    put_u8(&w, 2);
    put_u16(&w, 0x0304);

    /* signature_algorithms (13).  A pure-PSK ClientHello does not need it —
     * RFC 8446 §4.2.3 requires it only where the client offers certificate
     * authentication — but a server that has not yet resolved the PSK reaches
     * its certificate path first and answers `missing_extension`.  That was
     * the SECOND false pass this mode produced: a refusal that had nothing to
     * do with the key exchange, reported as though it did.  So the offer
     * carries it, and a refusal is now about what it is supposed to be
     * about. */
    put_u16(&w, 13);
    put_u16(&w, 8);
    put_u16(&w, 6);
    put_u16(&w, 0x0403);                 /* ecdsa_secp256r1_sha256            */
    put_u16(&w, 0x0804);                 /* rsa_pss_rsae_sha256               */
    put_u16(&w, 0x0401);                 /* rsa_pkcs1_sha256                  */

    /* psk_key_exchange_modes (45): psk_ke (0) AND NOTHING ELSE.
     *
     * THIS IS THE WHOLE POINT OF THE MODE.  A conformant peer under RV 5.2f
     * has no acceptable mode to select and refuses.  There is deliberately no
     * key_share extension either: PSK-only key exchange carries no ephemeral
     * share, which is exactly the absence of forward secrecy RV 5.4b2 is
     * about. */
    put_u16(&w, 45);
    put_u16(&w, 2);
    put_u8(&w, 1);
    put_u8(&w, 0);                       /* psk_ke */

    /* pre_shared_key (41) — MUST be the last extension (RFC 8446 §4.2.11). */
    put_u16(&w, 41);
    psk_ext_len_at = w.n;
    put_u16(&w, 0);
    identities_len_at = w.n;
    put_u16(&w, 0);
    put_u16(&w, (uint16_t)identity_len);
    put(&w, identity, identity_len);
    put_u32(&w, 0);                      /* obfuscated_ticket_age: 0 for an external PSK */
    poke_u16(&w, identities_len_at, (uint16_t)(w.n - identities_len_at - 2));

    /* Everything so far is the transcript the binder is computed over: the
     * ClientHello "truncated" at the end of `identities` (§4.2.11.2).  The
     * LENGTH fields, though, must already describe the whole message including
     * the binders, so they are filled in first and the hash taken after. */
    truncated_len  = w.n;
    binders_len_at = w.n;
    put_u16(&w, 0);                      /* binders vector length              */
    put_u8(&w, (uint8_t)TLS_HASH_BYTES); /* one binder, 32 bytes               */
    put(&w, binder, TLS_HASH_BYTES);     /* placeholder, overwritten below     */

    poke_u16(&w, binders_len_at, (uint16_t)(1u + TLS_HASH_BYTES));
    poke_u16(&w, psk_ext_len_at, (uint16_t)(w.n - psk_ext_len_at - 2));
    poke_u16(&w, ext_len_at, (uint16_t)(w.n - ext_len_at - 2));
    poke_u24(&w, hs_len_at, (uint32_t)(w.n - hs_len_at - 3));
    if (w.overflow)
        return 0;

    /* early_secret = HKDF-Extract(0, PSK); binder_key = Derive-Secret(early,
     * "ext binder", ""); finished_key = HKDF-Expand-Label(binder_key,
     * "finished", "", 32); binder = HMAC(finished_key, Hash(truncated CH)). */
    memset(zeros, 0, sizeof(zeros));
    (void)ppcp_hkdf_extract(zeros, sizeof(zeros), psk, psk_len, early_secret);
    ppcp_sha256_hash(NULL, 0, empty_hash);
    expand_label(early_secret, "ext binder", empty_hash, sizeof(empty_hash),
                 binder_key, sizeof(binder_key));
    expand_label(binder_key, "finished", NULL, 0, finished_key, sizeof(finished_key));
    ppcp_sha256_hash(w.b, truncated_len, transcript);
    ppcp_hmac_sha256(finished_key, sizeof(finished_key), transcript, sizeof(transcript),
                     binder);
    memcpy(w.b + w.n - TLS_HASH_BYTES, binder, TLS_HASH_BYTES);

    if (w.n > cap)
        return 0;
    memcpy(out, w.b, w.n);
    return w.n;
}

/* ------------------------------------------------------------ the exchange */

static const char *alert_name(uint8_t desc)
{
    switch (desc) {
    case 0:   return "close_notify";
    case 40:  return "handshake_failure";
    case 42:  return "bad_certificate";
    case 47:  return "illegal_parameter";
    case 48:  return "unknown_ca";
    case 50:  return "decode_error";
    case 51:  return "decrypt_error";
    case 70:  return "protocol_version";
    case 71:  return "insufficient_security";
    case 80:  return "internal_error";
    case 109: return "missing_extension";
    case 112: return "unrecognized_name";
    case 115: return "unknown_psk_identity";
    case 116: return "bad_certificate_hash_value";
    case 120: return "no_application_protocol";
    default:  return "an unnamed alert";
    }
}

static int dial(const char *host, int port)
{
    struct addrinfo hints, *res = NULL, *ai;
    char            portbuf[16];
    int             fd = -1;

#if defined(_WIN32)
    /* getaddrinfo() is a Winsock call too, and it is the FIRST one this dial
     * path makes — before any socket() call, whose wrapper is where
     * WSAStartup normally happens. Called explicitly here so this dialler
     * doesn't depend on call order to stay initialised. */
    sim_win_wsa_ensure();
#endif
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
        if (connect(fd, ai->ai_addr, (socklen_t)ai->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

/* Does the ServerHello at `body` carry `pre_shared_key` and NOT `key_share`?
 * That combination is the peer saying "PSK-only key exchange, agreed". */
static bool server_hello_accepted_psk_ke(const uint8_t *body, size_t len,
                                         bool *out_has_key_share)
{
    size_t off = 2 + 32;                 /* legacy_version, random            */
    size_t ext_end;
    bool   has_psk = false;

    *out_has_key_share = false;
    if (len < off + 1)
        return false;
    off += 1u + body[off];               /* legacy_session_id_echo            */
    off += 2;                            /* cipher_suite                      */
    off += 1;                            /* legacy_compression_method         */
    if (len < off + 2)
        return false;
    ext_end = off + 2 + (((size_t)body[off] << 8) | body[off + 1]);
    off += 2;
    if (ext_end > len)
        ext_end = len;
    while (off + 4 <= ext_end) {
        uint16_t type = (uint16_t)(((uint16_t)body[off] << 8) | body[off + 1]);
        size_t   elen = ((size_t)body[off + 2] << 8) | body[off + 3];
        if (type == 41)
            has_psk = true;
        if (type == 51)
            *out_has_key_share = true;
        off += 4 + elen;
    }
    return has_psk && !*out_has_key_share;
}

int sim_run_psk_ke_only(const sim_opts *o)
{
    uint8_t psk[64];
    size_t  psk_len;
    const char *identity;
    uint8_t hello[TLS_MAX_HELLO];
    uint8_t record[TLS_MAX_HELLO + 8];
    size_t  hello_len, record_len;
    uint8_t reply[4096];
    ssize_t got;
    int     fd;

    psk_len = unhex(o->psk_hex, psk, sizeof(psk));
    if (psk_len == 0) {
        /* No key given: a fixed 32-byte value, so the run is reproducible and
         * the peer's answer is about the MODE rather than about entropy. */
        memset(psk, 0x0b, 32);
        psk_len = 32;
    }
    identity = (o->psk_identity != NULL) ? o->psk_identity : "ppcp-sim-psk-ke-only";

    hello_len = build_client_hello(hello, sizeof(hello), psk, psk_len,
                                   (const uint8_t *)identity, strlen(identity));
    if (hello_len == 0) {
        fprintf(stderr, "ppcp-sim: could not build the psk_ke-only ClientHello\n");
        return 1;
    }
    record[0] = 22;                      /* handshake                         */
    record[1] = 0x03;
    record[2] = 0x01;                    /* legacy_record_version             */
    record[3] = (uint8_t)(hello_len >> 8);
    record[4] = (uint8_t)(hello_len & 0xFFu);
    memcpy(record + 5, hello, hello_len);
    record_len = hello_len + 5;

    fd = dial(o->connect_host, o->connect_port);
    if (fd < 0) {
        fprintf(stderr, "ppcp-sim: cannot connect to %s:%d for the psk_ke offer\n",
                o->connect_host, o->connect_port);
        return 1;
    }
    if (send(fd, record, record_len, 0) != (ssize_t)record_len) {
        fprintf(stderr, "ppcp-sim: could not put the psk_ke-only ClientHello on the wire\n");
        close(fd);
        return 1;
    }
    fprintf(stderr, "ppcp-sim: offered TLS 1.3 with psk_key_exchange_modes = { psk_ke } "
                    "and no key_share (%zu bytes), identity `%s`\n", record_len, identity);

    {
        struct pollfd pfd;
        pfd.fd     = fd;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 5000) <= 0) {
            fprintf(stderr, "ppcp-sim: RT-4 satisfied — the peer answered nothing and "
                            "completed no handshake\n");
            close(fd);
            return 0;
        }
    }
    got = recv(fd, reply, sizeof(reply), 0);
    close(fd);

    if (got <= 0) {
        fprintf(stderr, "ppcp-sim: RT-4 satisfied — the peer closed the connection "
                        "without completing a psk_ke-only handshake\n");
        return 0;
    }
    if (reply[0] == 21 && got >= 7) {
        fprintf(stderr, "ppcp-sim: RT-4 satisfied — the peer REFUSED the psk_ke-only "
                        "offer with alert %u (%s)\n",
                (unsigned)reply[6], alert_name(reply[6]));
        return 0;
    }
    if (reply[0] == 22 && got >= 10 && reply[5] == 2) {
        bool   has_key_share = false;
        size_t body_len = ((size_t)reply[6] << 16) | ((size_t)reply[7] << 8) | reply[8];
        if (body_len + 9u > (size_t)got)
            body_len = (size_t)got - 9u;
        if (server_hello_accepted_psk_ke(reply + 9, body_len, &has_key_share)) {
            /* RV 5.2f / 5.4b2 — the peer agreed to PSK-only key exchange, so
             * the link it told the user was secure has no forward secrecy.
             * That is the failure RT-4 exists to detect and it is only
             * detectable from outside. */
            fprintf(stderr, "ppcp-sim: RT-4 FAILED — the peer ACCEPTED psk_ke: its "
                            "ServerHello carries `pre_shared_key` and no `key_share`\n");
            return 1;
        }
        fprintf(stderr, "ppcp-sim: RT-4 inconclusive — the peer answered with a "
                        "ServerHello that neither accepted psk_ke (key_share %s) nor "
                        "sent an alert; read the exchange before recording a row\n",
                has_key_share ? "present" : "absent");
        return 1;
    }
    fprintf(stderr, "ppcp-sim: RT-4 inconclusive — the peer answered %lld bytes beginning "
                    "0x%02x, which is neither an alert nor a ServerHello (is this a TLS "
                    "listener at all?)\n", (long long)got, (unsigned)reply[0]);
    return 1;
}
