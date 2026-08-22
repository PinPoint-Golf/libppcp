/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * PPCP-RV §4 — the pairing code.  "The one part of this specification that
 * cannot be changed after release."
 *
 * The payload is CBOR through the same codec as everything else (RV A4), with
 * deterministic encoding required rather than recommended (4.3a), so a given
 * pairing reproduces a byte-identical code.  `v` is the first key by
 * construction: every other top-level key is at least two characters (4.3b) and
 * RFC 8949 §4.2.1 sorts a one-character key ahead of all of them.
 *
 * That construction is why 4.2a can be relied on.  Draft 1's display name was
 * `n`, which encodes 61 6e against `v`'s 61 76 — so every code carrying a
 * display name put the name first, and a parser reading the first key to find
 * the version read a display name instead.  The field is `dn` for that reason,
 * and the 133-octet all-fields vector of §10.3 is the test that it worked.
 */
#include "ppcp/rv.h"
#include "ppcp/cbor.h"
#include "ppcp/version.h"

#include <string.h>

void ppcp_rv_payload_init(ppcp_rv_payload *p)
{
    if (p == NULL)
        return;
    memset(p, 0, sizeof(*p));
    p->v = PPCP_RV_PAYLOAD_VERSION;
}

ppcp_result ppcp_rv_payload_add_endpoint(ppcp_rv_payload *p, const char *host,
                                         size_t host_len, uint16_t port)
{
    if (p == NULL || host == NULL || host_len == 0)
        return PPCP_ERR_INVALID;
    if (p->ep_count >= PPCP_RV_MAX_ENDPOINTS)
        return PPCP_ERR_LIMIT;
    if (port == 0)
        return PPCP_ERR_INVALID;
    /* 4.3: most preferred first, and 4.3c has the scanner try them in order
     * and stop at the first that completes the handshake. */
    p->ep[p->ep_count].h     = host;
    p->ep[p->ep_count].h_len = host_len;
    p->ep[p->ep_count].p     = port;
    p->ep_count++;
    return PPCP_OK;
}

ppcp_result ppcp_rv_payload_set_secret(ppcp_rv_payload *p, const uint8_t *psk,
                                       size_t psk_len, const uint8_t sid[PPCP_RV_SID_BYTES])
{
    if (p == NULL || psk == NULL || sid == NULL)
        return PPCP_ERR_INVALID;
    /* 4.3g: at least 16 bytes from a CSPRNG.  The library checks the width and
     * cannot check the source — that is RT-12, a review method. */
    if (psk_len != 16u && psk_len != 32u)
        return PPCP_ERR_INVALID;
    memcpy(p->psk, psk, psk_len);
    p->psk_len = psk_len;
    memcpy(p->sid, sid, PPCP_RV_SID_BYTES);
    return PPCP_OK;
}

ppcp_result ppcp_rv_payload_set_display_name(ppcp_rv_payload *p, const char *dn,
                                             size_t dn_len)
{
    if (p == NULL || dn == NULL || dn_len == 0)
        return PPCP_ERR_INVALID;
    if (dn_len > PPCP_RV_DN_MAX)
        return PPCP_ERR_INVALID;
    p->dn     = dn;
    p->dn_len = dn_len;
    p->has_dn = true;
    return PPCP_OK;
}

ppcp_result ppcp_rv_payload_set_max_uses(ppcp_rv_payload *p, uint64_t mu)
{
    if (p == NULL || mu == 0)
        return PPCP_ERR_INVALID;
    p->mu     = mu;
    p->has_mu = true;
    return PPCP_OK;
}

ppcp_result ppcp_rv_payload_set_expiry(ppcp_rv_payload *p, uint64_t exp_unix_s)
{
    if (p == NULL)
        return PPCP_ERR_INVALID;
    p->exp     = exp_unix_s;
    p->has_exp = true;
    return PPCP_OK;
}

ppcp_result ppcp_rv_payload_set_wifi(ppcp_rv_payload *p, const ppcp_rv_wifi *w)
{
    if (p == NULL || w == NULL)
        return PPCP_ERR_INVALID;
    if (w->s == NULL || w->s_len == 0)
        return PPCP_ERR_INVALID;              /* RV §6: `s` is mandatory */
    if (w->has_k && (w->k == NULL || w->k_len == 0))
        return PPCP_ERR_INVALID;              /* absent means open, not empty */
    p->wifi     = *w;
    p->has_wifi = true;
    return PPCP_OK;
}

ppcp_result ppcp_rv_payload_validate(const ppcp_rv_payload *p)
{
    if (p == NULL)
        return PPCP_ERR_INVALID;
    if (p->v != PPCP_RV_PAYLOAD_VERSION)
        return PPCP_ERR_VERSION_NEWER;
    if (p->ep_count == 0 || p->ep_count > PPCP_RV_MAX_ENDPOINTS)
        return PPCP_ERR_INVALID;          /* 4.3: `ep` is 1..n */
    if (p->psk_len != 16u && p->psk_len != 32u)
        return PPCP_ERR_INVALID;
    if (p->has_dn && p->dn_len > PPCP_RV_DN_MAX)
        return PPCP_ERR_INVALID;          /* 4.3 / 4.4d: at most 64 bytes */
    if (p->has_mu && p->mu == 0)
        return PPCP_ERR_INVALID;          /* zero successful pairings is not a code */
    if (p->has_wifi && (p->wifi.s == NULL || p->wifi.s_len == 0))
        return PPCP_ERR_INVALID;          /* RV §6: `s` is mandatory in `wifi` */
    return PPCP_OK;
}

bool ppcp_rv_may_persist(const ppcp_rv_payload *p)
{
    if (p == NULL)
        return false;
    /* 7.4f: a pairing established from a multi-use code is session-scoped,
     * because every peer that scanned that code derives identical key material
     * — so "paired" would name a group rather than an identity. */
    return !(p->has_mu && p->mu > 1u);
}

/* --------------------------------------------------------------- encoding */

ppcp_result ppcp_rv_payload_encode(const ppcp_rv_payload *p, uint8_t *out, size_t cap,
                                   size_t *out_len)
{
    ppcp_cbor_writer w;
    ppcp_result      rc;
    size_t           n, i;

    rc = ppcp_rv_payload_validate(p);
    if (rc != PPCP_OK)
        return rc;
    if (out == NULL)
        return PPCP_ERR_INVALID;

    /* 4.3a — deterministic, never literal.  The writer refuses an out-of-order
     * key, so the ordering below is checked rather than asserted. */
    ppcp_cbor_writer_init(&w, out, cap);

    n = 4u;   /* v, ep, psk, sid */
    if (p->has_dn)   n++;
    if (p->has_mu)   n++;
    if (p->has_exp)  n++;
    if (p->has_wifi) n++;

    if (ppcp_cbor_write_map(&w, n) != PPCP_OK) return ppcp_cbor_writer_status(&w);

    /* v (0x61 76) — first by construction, 4.3b. */
    if (ppcp_cbor_write_text_z(&w, "v") != PPCP_OK) return ppcp_cbor_writer_status(&w);
    if (ppcp_cbor_write_uint(&w, p->v) != PPCP_OK) return ppcp_cbor_writer_status(&w);

    if (p->has_dn) {
        if (ppcp_cbor_write_text_z(&w, "dn") != PPCP_OK) return ppcp_cbor_writer_status(&w);
        if (ppcp_cbor_write_text(&w, p->dn, p->dn_len) != PPCP_OK)
            return ppcp_cbor_writer_status(&w);
    }

    if (ppcp_cbor_write_text_z(&w, "ep") != PPCP_OK) return ppcp_cbor_writer_status(&w);
    if (ppcp_cbor_write_array(&w, p->ep_count) != PPCP_OK) return ppcp_cbor_writer_status(&w);
    for (i = 0; i < p->ep_count; i++) {
        /* 4.3b1 — nested maps are unconstrained, and `h`/`p` are correctly one
         * character each.  The rule exists only to fix the first key of the
         * top-level map. */
        if (ppcp_cbor_write_map(&w, 2) != PPCP_OK) return ppcp_cbor_writer_status(&w);
        if (ppcp_cbor_write_text_z(&w, "h") != PPCP_OK) return ppcp_cbor_writer_status(&w);
        if (ppcp_cbor_write_text(&w, p->ep[i].h, p->ep[i].h_len) != PPCP_OK)
            return ppcp_cbor_writer_status(&w);
        if (ppcp_cbor_write_text_z(&w, "p") != PPCP_OK) return ppcp_cbor_writer_status(&w);
        if (ppcp_cbor_write_uint(&w, p->ep[i].p) != PPCP_OK) return ppcp_cbor_writer_status(&w);
    }

    if (p->has_mu) {
        if (ppcp_cbor_write_text_z(&w, "mu") != PPCP_OK) return ppcp_cbor_writer_status(&w);
        if (ppcp_cbor_write_uint(&w, p->mu) != PPCP_OK) return ppcp_cbor_writer_status(&w);
    }
    if (p->has_exp) {
        if (ppcp_cbor_write_text_z(&w, "exp") != PPCP_OK) return ppcp_cbor_writer_status(&w);
        if (ppcp_cbor_write_uint(&w, p->exp) != PPCP_OK) return ppcp_cbor_writer_status(&w);
    }

    if (ppcp_cbor_write_text_z(&w, "psk") != PPCP_OK) return ppcp_cbor_writer_status(&w);
    if (ppcp_cbor_write_bytes(&w, p->psk, p->psk_len) != PPCP_OK)
        return ppcp_cbor_writer_status(&w);

    if (ppcp_cbor_write_text_z(&w, "sid") != PPCP_OK) return ppcp_cbor_writer_status(&w);
    if (ppcp_cbor_write_bytes(&w, p->sid, PPCP_RV_SID_BYTES) != PPCP_OK)
        return ppcp_cbor_writer_status(&w);

    if (p->has_wifi) {
        size_t wn = 1u + (p->wifi.has_h ? 1u : 0u) + (p->wifi.has_k ? 1u : 0u);
        if (ppcp_cbor_write_text_z(&w, "wifi") != PPCP_OK) return ppcp_cbor_writer_status(&w);
        if (ppcp_cbor_write_map(&w, wn) != PPCP_OK) return ppcp_cbor_writer_status(&w);
        if (p->wifi.has_h) {
            if (ppcp_cbor_write_text_z(&w, "h") != PPCP_OK) return ppcp_cbor_writer_status(&w);
            if (ppcp_cbor_write_bool(&w, p->wifi.h) != PPCP_OK) return ppcp_cbor_writer_status(&w);
        }
        if (p->wifi.has_k) {
            if (ppcp_cbor_write_text_z(&w, "k") != PPCP_OK) return ppcp_cbor_writer_status(&w);
            if (ppcp_cbor_write_text(&w, p->wifi.k, p->wifi.k_len) != PPCP_OK)
                return ppcp_cbor_writer_status(&w);
        }
        if (ppcp_cbor_write_text_z(&w, "s") != PPCP_OK) return ppcp_cbor_writer_status(&w);
        if (ppcp_cbor_write_text(&w, p->wifi.s, p->wifi.s_len) != PPCP_OK)
            return ppcp_cbor_writer_status(&w);
    }

    return ppcp_cbor_writer_finish(&w, out_len);
}

/* --------------------------------------------------------------- decoding */

static ppcp_result decode_wifi(ppcp_cbor_reader *r, ppcp_rv_wifi *out)
{
    ppcp_cbor_item it;
    ppcp_result    rc;
    uint32_t       i, pairs;

    rc = ppcp_cbor_read(r, &it);
    if (rc != PPCP_OK) return rc;
    if (it.type != PPCP_CBOR_MAP) return PPCP_ERR_MALFORMED;
    pairs = it.count;
    memset(out, 0, sizeof(*out));

    for (i = 0; i < pairs; i++) {
        const char *k; size_t klen;
        rc = ppcp_cbor_read_key(r, &k, &klen);
        if (rc != PPCP_OK) return rc;
        if (ppcp_cbor_key_is(k, klen, "s")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_TEXT) return PPCP_ERR_MALFORMED;
            out->s = (const char *)it.bytes; out->s_len = it.len;
        } else if (ppcp_cbor_key_is(k, klen, "k")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_TEXT) return PPCP_ERR_MALFORMED;
            out->k = (const char *)it.bytes; out->k_len = it.len; out->has_k = true;
        } else if (ppcp_cbor_key_is(k, klen, "h")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_BOOL) return PPCP_ERR_MALFORMED;
            out->h = it.b; out->has_h = true;
        } else {
            rc = ppcp_cbor_skip(r);   /* 4.2c — unknown keys at every nesting level */
            if (rc != PPCP_OK) return rc;
        }
    }
    if (out->s == NULL || out->s_len == 0)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

ppcp_result ppcp_rv_payload_decode(const uint8_t *in, size_t in_len, ppcp_rv_payload *out)
{
    ppcp_cbor_reader r;
    ppcp_cbor_item   it;
    ppcp_cbor_limits lim;
    ppcp_result      rc;
    uint32_t         i, pairs;
    bool             seen_psk = false, seen_sid = false, seen_ep = false;

    if (in == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    if (in_len > PPCP_RV_MAX_PAYLOAD)
        return PPCP_ERR_LIMIT;

    memset(out, 0, sizeof(*out));

    lim.max_bytes    = PPCP_RV_MAX_PAYLOAD;
    lim.max_text     = PPCP_RV_MAX_PAYLOAD;
    lim.max_depth    = PPCP_CBOR_MAX_DEPTH;
    lim.max_elements = PPCP_CBOR_MAX_ELEMENTS;

    /* One validating pass, so an integer key, a duplicate key, a tag, a `null`
     * or an indefinite length is refused before anything is read out.  4.4b: a
     * peer that cannot decode the payload reports an invalid code and attempts
     * no connection. */
    rc = ppcp_cbor_validate(in, in_len, lim, NULL);
    if (rc != PPCP_OK)
        return rc;

    ppcp_cbor_reader_init(&r, in, in_len, lim);
    rc = ppcp_cbor_read(&r, &it);
    if (rc != PPCP_OK) return rc;
    if (it.type != PPCP_CBOR_MAP) return PPCP_ERR_MALFORMED;
    pairs = it.count;
    if (pairs == 0) return PPCP_ERR_MALFORMED;

    /* 4.2a: the first key is `v`.  Read it, and nothing else, until the version
     * is known — 4.2d forbids acting on any other field of a payload whose `v`
     * is not implemented. */
    {
        const char *k; size_t klen;
        rc = ppcp_cbor_read_key(&r, &k, &klen);
        if (rc != PPCP_OK) return rc;
        if (!ppcp_cbor_key_is(k, klen, "v"))
            return PPCP_ERR_MALFORMED;
        rc = ppcp_cbor_read(&r, &it);
        if (rc != PPCP_OK) return rc;
        if (it.type != PPCP_CBOR_UINT || it.i < 0)
            return PPCP_ERR_MALFORMED;
        out->v = (uint64_t)it.i;
        if (out->v != PPCP_RV_PAYLOAD_VERSION)
            /* 4.2b — a *version* report, not a generic failure.  The caller
             * tells the user the code requires a newer version of the
             * application, which is the one thing they can act on. */
            return PPCP_ERR_VERSION_NEWER;
    }

    for (i = 1; i < pairs; i++) {
        const char *k; size_t klen;
        rc = ppcp_cbor_read_key(&r, &k, &klen);
        if (rc != PPCP_OK) return rc;

        if (ppcp_cbor_key_is(k, klen, "ep")) {
            uint32_t j;
            rc = ppcp_cbor_read(&r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_ARRAY) return PPCP_ERR_MALFORMED;
            if (it.count == 0 || it.count > PPCP_RV_MAX_ENDPOINTS) return PPCP_ERR_MALFORMED;
            for (j = 0; j < it.count; j++) {
                ppcp_cbor_item e;
                uint32_t       m, mp;
                bool           got_h = false, got_p = false;
                rc = ppcp_cbor_read(&r, &e);
                if (rc != PPCP_OK) return rc;
                if (e.type != PPCP_CBOR_MAP) return PPCP_ERR_MALFORMED;
                mp = e.count;
                for (m = 0; m < mp; m++) {
                    const char *ek; size_t eklen;
                    rc = ppcp_cbor_read_key(&r, &ek, &eklen);
                    if (rc != PPCP_OK) return rc;
                    if (ppcp_cbor_key_is(ek, eklen, "h")) {
                        rc = ppcp_cbor_read(&r, &e);
                        if (rc != PPCP_OK) return rc;
                        if (e.type != PPCP_CBOR_TEXT || e.len == 0) return PPCP_ERR_MALFORMED;
                        out->ep[j].h     = (const char *)e.bytes;
                        out->ep[j].h_len = e.len;
                        got_h = true;
                    } else if (ppcp_cbor_key_is(ek, eklen, "p")) {
                        rc = ppcp_cbor_read(&r, &e);
                        if (rc != PPCP_OK) return rc;
                        if (e.type != PPCP_CBOR_UINT || e.i <= 0 || e.i > 65535)
                            return PPCP_ERR_MALFORMED;
                        out->ep[j].p = (uint16_t)e.i;
                        got_p = true;
                    } else {
                        rc = ppcp_cbor_skip(&r);
                        if (rc != PPCP_OK) return rc;
                    }
                }
                if (!got_h || !got_p) return PPCP_ERR_MALFORMED;
            }
            out->ep_count = it.count;
            seen_ep = true;
        } else if (ppcp_cbor_key_is(k, klen, "psk")) {
            rc = ppcp_cbor_read(&r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_BYTES) return PPCP_ERR_MALFORMED;
            if (it.len != 16u && it.len != 32u) return PPCP_ERR_MALFORMED;
            memcpy(out->psk, it.bytes, it.len);
            out->psk_len = it.len;
            seen_psk = true;
        } else if (ppcp_cbor_key_is(k, klen, "sid")) {
            rc = ppcp_cbor_read(&r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_BYTES || it.len != PPCP_RV_SID_BYTES)
                return PPCP_ERR_MALFORMED;   /* 4.3e: sixteen raw bytes of a UUID */
            memcpy(out->sid, it.bytes, PPCP_RV_SID_BYTES);
            seen_sid = true;
        } else if (ppcp_cbor_key_is(k, klen, "mu")) {
            rc = ppcp_cbor_read(&r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_UINT || it.i <= 0) return PPCP_ERR_MALFORMED;
            out->mu = (uint64_t)it.i; out->has_mu = true;
        } else if (ppcp_cbor_key_is(k, klen, "exp")) {
            rc = ppcp_cbor_read(&r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_UINT || it.i < 0) return PPCP_ERR_MALFORMED;
            out->exp = (uint64_t)it.i; out->has_exp = true;
        } else if (ppcp_cbor_key_is(k, klen, "dn")) {
            rc = ppcp_cbor_read(&r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_TEXT) return PPCP_ERR_MALFORMED;
            /* 4.4d: untrusted display text, truncated to at most 64 bytes and
             * never used as an identifier, a trust signal or a storage key.
             * Over-long is refused here rather than silently cut, so the
             * publisher's mistake is visible. */
            if (it.len > PPCP_RV_DN_MAX) return PPCP_ERR_MALFORMED;
            out->dn = (const char *)it.bytes; out->dn_len = it.len; out->has_dn = true;
        } else if (ppcp_cbor_key_is(k, klen, "wifi")) {
            rc = decode_wifi(&r, &out->wifi);
            if (rc != PPCP_OK) return rc;
            out->has_wifi = true;
        } else {
            /* 4.2c — a peer ignores payload keys it does not recognise, at
             * every nesting level, and does not treat them as an error. */
            rc = ppcp_cbor_skip(&r);
            if (rc != PPCP_OK) return rc;
        }
    }

    if (!seen_ep || !seen_psk || !seen_sid)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

/* -------------------------------------------------------------- URI (4.1) */

ppcp_result ppcp_rv_uri_encode(const ppcp_rv_payload *p, char *out, size_t cap,
                               size_t *out_len)
{
    uint8_t     buf[PPCP_RV_MAX_PAYLOAD];
    size_t      n = 0, b64 = 0;
    ppcp_result rc;

    if (out == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_rv_payload_encode(p, buf, sizeof(buf), &n);
    if (rc != PPCP_OK)
        return rc;
    if (cap < 5u)
        return PPCP_ERR_NOSPACE;

    /* 4.1b: the scheme is `ppcp` and does not change between payload versions.
     * 4.1c: never http(s) — the payload carries a secret, and an operating
     * system with no handler for the scheme opens such a URL in a browser,
     * which sends the secret to a web server and writes it to history. */
    memcpy(out, "ppcp:", 5);
    rc = ppcp_base64url_encode(buf, n, out + 5, cap - 5u, &b64);
    memset(buf, 0, sizeof(buf));
    if (rc != PPCP_OK)
        return rc;
    if (out_len != NULL)
        *out_len = 5u + b64;
    return PPCP_OK;
}

ppcp_result ppcp_rv_uri_decode(const char *uri, size_t uri_len, uint8_t *scratch,
                               size_t scratch_cap, ppcp_rv_payload *out)
{
    size_t      n = 0;
    ppcp_result rc;

    if (uri == NULL || scratch == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    if (uri_len < 5u || memcmp(uri, "ppcp:", 5) != 0)
        return PPCP_ERR_MALFORMED;

    rc = ppcp_base64url_decode(uri + 5, uri_len - 5u, scratch, scratch_cap, &n);
    if (rc != PPCP_OK)
        return rc;
    return ppcp_rv_payload_decode(scratch, n, out);
}

/* ---------------------------------------------------------- sid (4.3e) */

ppcp_result ppcp_rv_sid_to_session_id(const uint8_t sid[PPCP_RV_SID_BYTES],
                                      char out[PPCP_RV_SESSION_ID_CHARS])
{
    static const char hex[] = "0123456789abcdef";
    static const unsigned groups[5] = { 4, 2, 2, 2, 6 };
    unsigned g, i, b = 0, o = 0;

    if (sid == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    /* 4.3e: canonical LOWERCASE text — eight, four, four, four and twelve
     * hexadecimal digits separated by hyphens.  Hexadecimal, canonical UUID
     * text and base64url are all plausible and all wrong if the other end chose
     * differently, and CORE 8.5c keys idempotent re-import on Session.id. */
    for (g = 0; g < 5u; g++) {
        if (g > 0)
            out[o++] = '-';
        for (i = 0; i < groups[g]; i++) {
            out[o++] = hex[(sid[b] >> 4) & 0xfu];
            out[o++] = hex[sid[b] & 0xfu];
            b++;
        }
    }
    out[o] = '\0';
    return PPCP_OK;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

ppcp_result ppcp_rv_session_id_to_sid(const char *text, size_t len,
                                      uint8_t sid[PPCP_RV_SID_BYTES])
{
    static const unsigned hyphen_at[4] = { 8, 13, 18, 23 };
    unsigned i, b = 0, h = 0;

    if (text == NULL || sid == NULL)
        return PPCP_ERR_INVALID;
    if (len != 36u)
        return PPCP_ERR_MALFORMED;

    for (i = 0; i < 36u; i++) {
        if (h < 4u && i == hyphen_at[h]) {
            if (text[i] != '-')
                return PPCP_ERR_MALFORMED;
            h++;
            continue;
        }
        {
            int hi, lo;
            hi = hexval(text[i]);
            if (hi < 0 || i + 1u >= 36u)
                return PPCP_ERR_MALFORMED;
            lo = hexval(text[i + 1u]);
            if (lo < 0)
                return PPCP_ERR_MALFORMED;
            if (b >= PPCP_RV_SID_BYTES)
                return PPCP_ERR_MALFORMED;
            sid[b++] = (uint8_t)((hi << 4) | lo);
            i++;
        }
    }
    return (b == PPCP_RV_SID_BYTES) ? PPCP_OK : PPCP_ERR_MALFORMED;
}

/* ------------------------------------------------------------- expiry */

ppcp_result ppcp_rv_check_expiry(const ppcp_rv_payload *p, uint64_t now_unix_s,
                                 ppcp_rv_clock_trust trust, ppcp_rv_expiry *out)
{
    if (p == NULL || out == NULL)
        return PPCP_ERR_INVALID;

    /* 7.3c makes `exp` a SHOULD, so its absence is not a defect: 7.3a and 7.3b
     * — the use count and the session close — are the clock-free primary
     * defence, and `exp` is secondary because it depends on two wall clocks
     * agreeing. */
    if (!p->has_exp) {
        *out = PPCP_RV_EXPIRY_OK;
        return PPCP_OK;
    }
    if (now_unix_s < p->exp) {
        *out = PPCP_RV_EXPIRY_OK;
        return PPCP_OK;
    }
    if (trust == PPCP_RV_CLOCK_TRUSTED) {
        /* 4.4a — refuse, and report it as expired rather than as a failure to
         * connect. */
        *out = PPCP_RV_EXPIRY_EXPIRED;
        return PPCP_OK;
    }
    /* 4.4a1 — attempt the pairing anyway and report the code as *possibly*
     * expired.  A device with a wrong clock at a range has no network to
     * correct it, and refusing a valid code leaves the user with no path at
     * all; the publisher holds the authoritative clock and enforces `exp`
     * itself (7.3e). */
    *out = PPCP_RV_EXPIRY_POSSIBLY_EXPIRED;
    return PPCP_OK;
}
