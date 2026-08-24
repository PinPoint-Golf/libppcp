/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * PPCP-RV §11.4 — the five bootstrap frames, over PPCP-ENC §3's framing with
 * the reserved channel byte.
 *
 * ⛔ THIS IS THE SEPARATE WRITE PATH, AND THAT IS THE WHOLE POINT OF THE FILE
 * (CA6, 11.4a).  It writes and reads the 8-byte header itself and NEVER calls
 * ppcp_channel_validate(), ppcp_frame_header_write() or ppcp_frame_write() —
 * all three reject channel 255, correctly, and must go on rejecting it.  That
 * rejection is 11.4a's fail-closed property: it is what stops a bootstrap
 * frame being half-understood on a PPCP link, and a PPCP frame from being
 * half-understood on a bootstrap connection.  Relaxing it to let a bootstrap
 * frame out would delete the safety argument in the course of implementing the
 * clause that relies on it, and every test in this repository would still
 * pass.  A bootstrap connection is not a PPCP link (1.3c1).
 *
 * ⛔ THE VOCABULARY IS CLOSED (11.4c1 / E46).  An unrecognised map key is
 * `malformed`, and §11 is the ONE place in the protocol set where that is so —
 * 3.3a, 4.2c and A4 all point the other way.  See the note in bootstrap.h for
 * what that costs and why it is accepted.
 */
#include "ppcp/bootstrap.h"

#include "ppcp/cbor.h"
#include "ppcp/frame.h"

#include <string.h>

/* Tight, and the frames never grow: 11.10a makes the five of §11.4 the entire
 * vocabulary of the connection. */
static ppcp_cbor_limits bs_limits(void)
{
    ppcp_cbor_limits l;
    l.max_bytes    = PPCP_RV_BS_CT_BYTES;  /* the longest bstr is ct/pk at 32 */
    l.max_text     = 16u;                  /* the longest key is "mac"        */
    l.max_depth    = 2u;
    l.max_elements = 8u;
    return l;
}

/* ------------------------------------------------------------------- write */

ppcp_result ppcp_bs_frame_write(const ppcp_bs_frame *f, uint8_t *out, size_t cap,
                                size_t *out_len)
{
    uint8_t          payload[PPCP_BS_MAX_PAYLOAD];
    ppcp_cbor_writer w;
    ppcp_result      rc;
    size_t           n = 0;

    if (f == NULL || out == NULL || out_len == NULL)
        return PPCP_ERR_INVALID;

    /* 4.3a — deterministic, always, so a given frame is byte-identical
     * everywhere.  The writer rejects a key that does not sort strictly after
     * its predecessor, which makes 11.4d's "`v` is the first key" a property
     * of the encoder rather than a discipline at five call sites: under RFC
     * 8949 §4.2.1 a one-character key sorts before every two-character one,
     * and ty/ct/pk/mac/rc are all longer. */
    ppcp_cbor_writer_init(&w, payload, sizeof(payload));

    switch (f->ty) {
    case PPCP_BS_OFFER:
        /* 11.5b — `ct` and NOT pk_i.  The initiator commits to its key
         * without revealing it, and that ordering is the whole of what stops
         * an attacker choosing the digits (11.5c, §11.8). */
        if (f->v == 0u)
            return PPCP_ERR_MALFORMED;              /* 11.4h1 — v is 1..255 */
        (void)ppcp_cbor_write_map(&w, 3);
        (void)ppcp_cbor_write_text_z(&w, "v");
        (void)ppcp_cbor_write_uint(&w, f->v);
        (void)ppcp_cbor_write_text_z(&w, "ct");
        (void)ppcp_cbor_write_bytes(&w, f->ct, PPCP_RV_BS_CT_BYTES);
        (void)ppcp_cbor_write_text_z(&w, "ty");
        (void)ppcp_cbor_write_uint(&w, (uint64_t)PPCP_BS_OFFER);
        break;

    case PPCP_BS_ACCEPT:
        /* 11.4h1 — the acceptor ECHOES the `v` it received, or aborts with
         * unsupported_version.  It never substitutes a different one, which
         * is what makes 11.6c's transcript unambiguous. */
        if (f->v == 0u)
            return PPCP_ERR_MALFORMED;
        (void)ppcp_cbor_write_map(&w, 3);
        (void)ppcp_cbor_write_text_z(&w, "v");
        (void)ppcp_cbor_write_uint(&w, f->v);
        (void)ppcp_cbor_write_text_z(&w, "pk");
        (void)ppcp_cbor_write_bytes(&w, f->pk, PPCP_RV_BS_KEY_BYTES);
        (void)ppcp_cbor_write_text_z(&w, "ty");
        (void)ppcp_cbor_write_uint(&w, (uint64_t)PPCP_BS_ACCEPT);
        break;

    case PPCP_BS_REVEAL:
        (void)ppcp_cbor_write_map(&w, 2);
        (void)ppcp_cbor_write_text_z(&w, "pk");
        (void)ppcp_cbor_write_bytes(&w, f->pk, PPCP_RV_BS_KEY_BYTES);
        (void)ppcp_cbor_write_text_z(&w, "ty");
        (void)ppcp_cbor_write_uint(&w, (uint64_t)PPCP_BS_REVEAL);
        break;

    case PPCP_BS_CONFIRM:
        (void)ppcp_cbor_write_map(&w, 2);
        (void)ppcp_cbor_write_text_z(&w, "ty");
        (void)ppcp_cbor_write_uint(&w, (uint64_t)PPCP_BS_CONFIRM);
        (void)ppcp_cbor_write_text_z(&w, "mac");
        (void)ppcp_cbor_write_bytes(&w, f->mac, PPCP_RV_BS_MAC_BYTES);
        break;

    case PPCP_BS_ABORT:
        /* 11.4g — `rc` and nothing else.  No message, no diagnostic string,
         * no peer name, and there is nowhere here to put one. */
        if ((int)f->rc < (int)PPCP_BS_RC_UNSUPPORTED_VERSION ||
            (int)f->rc > (int)PPCP_BS_RC_MALFORMED)
            return PPCP_ERR_INVALID;
        (void)ppcp_cbor_write_map(&w, 2);
        (void)ppcp_cbor_write_text_z(&w, "rc");
        (void)ppcp_cbor_write_uint(&w, (uint64_t)f->rc);
        (void)ppcp_cbor_write_text_z(&w, "ty");
        (void)ppcp_cbor_write_uint(&w, (uint64_t)PPCP_BS_ABORT);
        break;

    default:
        return PPCP_ERR_INVALID;
    }

    rc = ppcp_cbor_writer_finish(&w, &n);
    if (rc != PPCP_OK)
        return rc;

    if (cap < PPCP_FRAME_HEADER_BYTES + n)
        return PPCP_ERR_NOSPACE;

    /* ⛔ THE HEADER IS WRITTEN HERE, BY HAND, AND NOT BY frame.h.
     * ppcp_frame_header_write() would refuse channel 255 and it is RIGHT to
     * refuse it — see the file comment.  ENC §3: payload_len big-endian, then
     * channel, then flags and reserved, both zero in ppcp/1.0 (3b). */
    out[0] = (uint8_t)(n >> 24);
    out[1] = (uint8_t)(n >> 16);
    out[2] = (uint8_t)(n >> 8);
    out[3] = (uint8_t)n;
    out[4] = (uint8_t)PPCP_BS_CHANNEL;
    out[5] = 0u;
    out[6] = 0u;
    out[7] = 0u;
    memcpy(out + PPCP_FRAME_HEADER_BYTES, payload, n);

    *out_len = PPCP_FRAME_HEADER_BYTES + n;
    return PPCP_OK;
}

/* -------------------------------------------------------------------- read */

/* Which keys a frame of each type carries, per 11.4b's table.  A frame is
 * accepted only when the set it carries is EXACTLY the set its `ty` names:
 * a missing key, a repeated one, a key belonging to another frame type and a
 * key belonging to no frame type are all `malformed` (11.4c, 11.4c1). */
#define K_V   0x01u
#define K_TY  0x02u
#define K_CT  0x04u
#define K_PK  0x08u
#define K_MAC 0x10u
#define K_RC  0x20u

static unsigned required_keys(ppcp_bs_type ty)
{
    switch (ty) {
    case PPCP_BS_OFFER:   return K_V | K_TY | K_CT;
    case PPCP_BS_ACCEPT:  return K_V | K_TY | K_PK;
    case PPCP_BS_REVEAL:  return K_TY | K_PK;
    case PPCP_BS_CONFIRM: return K_TY | K_MAC;
    case PPCP_BS_ABORT:   return K_TY | K_RC;
    default:              return 0u;
    }
}

ppcp_result ppcp_bs_frame_read(const uint8_t *buf, size_t len, ppcp_bs_frame *out,
                               size_t *out_consumed)
{
    ppcp_cbor_reader r;
    ppcp_cbor_item   it;
    ppcp_result      rc;
    uint32_t         payload_len;
    unsigned         seen = 0u;
    uint32_t         count;
    uint32_t         i;
    bool             first_key_is_v = false;
    size_t           validated = 0;

    if (buf == NULL || out == NULL || out_consumed == NULL)
        return PPCP_ERR_INVALID;
    if (len < PPCP_FRAME_HEADER_BYTES)
        return PPCP_ERR_TRUNCATED;

    memset(out, 0, sizeof(*out));

    payload_len = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                  ((uint32_t)buf[2] << 8)  |  (uint32_t)buf[3];

    /* The mirror of the trap.  A PPCP frame arriving on a bootstrap connection
     * is rejected HERE, by the channel byte, which is the other half of what
     * 11.4a's reserved value buys.  11.3c is the acceptor's stronger rule for
     * a FIRST frame and belongs to the engine, which knows whether one has
     * been seen. */
    if (buf[4] != (uint8_t)PPCP_BS_CHANNEL)
        return PPCP_ERR_MALFORMED;

    /* ENC 3b — flags and reserved are reported, not refused, so a later minor
     * version may use those bits.  buf[5..7] are deliberately not checked. */

    /* ENC 3a, read as tightly as the vocabulary allows: refused when the head
     * is read, and nothing was reserved for it because nothing is ever
     * reserved at all.  `malformed` and not a limit code — 11.4c makes every
     * structural fault on this connection one thing, and there is nothing to
     * resynchronise to: the exchange is five frames long and a peer that has
     * lost its place has nowhere to recover to. */
    if (payload_len > PPCP_BS_MAX_PAYLOAD)
        return PPCP_ERR_MALFORMED;
    if (len - PPCP_FRAME_HEADER_BYTES < payload_len)
        return PPCP_ERR_TRUNCATED;

    /* Every rule of ENC §4 at every depth first — duplicate keys (4d), integer
     * keys (4a), `null` (4c), tags, indefinite lengths — so the field reads
     * below can be simple. */
    rc = ppcp_cbor_validate(buf + PPCP_FRAME_HEADER_BYTES, payload_len,
                            bs_limits(), &validated);
    if (rc != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    if (validated != payload_len)
        return PPCP_ERR_MALFORMED;      /* trailing bytes inside the frame */

    ppcp_cbor_reader_init(&r, buf + PPCP_FRAME_HEADER_BYTES, payload_len, bs_limits());

    if (ppcp_cbor_read(&r, &it) != PPCP_OK || it.type != PPCP_CBOR_MAP)
        return PPCP_ERR_MALFORMED;
    count = it.count;
    if (count < 2u || count > 3u)
        return PPCP_ERR_MALFORMED;

    for (i = 0; i < count; i++) {
        const char *key;
        size_t      key_len;

        if (ppcp_cbor_read_key(&r, &key, &key_len) != PPCP_OK)
            return PPCP_ERR_MALFORMED;
        if (ppcp_cbor_read(&r, &it) != PPCP_OK)
            return PPCP_ERR_MALFORMED;

        if (ppcp_cbor_key_is(key, key_len, "v")) {
            if (i == 0u)
                first_key_is_v = true;
            if (it.type != PPCP_CBOR_UINT)
                return PPCP_ERR_MALFORMED;
            /* 11.4h1 — `v` is 1..255 and one outside it is `malformed` under
             * 11.4c.  11.4b defines it as a CBOR unsigned integer, which
             * reaches 2^64-1, while 11.6c encodes it into the transcript as
             * ONE octet: the two never disagree today because `v` is 1, and
             * the transcript construction becomes undefined the first time
             * they do.  So the range is enforced at the door.
             *
             * ⚠ A `v` of 2 decodes CLEANLY here and is the engine's business,
             * not the decoder's: 11.4e requires the peer to abort with
             * unsupported_version and tell its USER the counterpart needs a
             * newer application, which is a different outcome from a
             * malformed frame and must stay distinguishable from one. */
            if (it.i < 1 || it.i > 255)
                return PPCP_ERR_MALFORMED;
            out->v = (uint8_t)it.i;
            seen |= K_V;
        } else if (ppcp_cbor_key_is(key, key_len, "ty")) {
            if (it.type != PPCP_CBOR_UINT)
                return PPCP_ERR_MALFORMED;
            /* 11.4c — a frame type it does not know is `malformed`. */
            if (it.i < (int64_t)PPCP_BS_OFFER || it.i > (int64_t)PPCP_BS_ABORT)
                return PPCP_ERR_MALFORMED;
            out->ty = (ppcp_bs_type)it.i;
            seen |= K_TY;
        } else if (ppcp_cbor_key_is(key, key_len, "ct")) {
            if (it.type != PPCP_CBOR_BYTES || it.len != PPCP_RV_BS_CT_BYTES)
                return PPCP_ERR_MALFORMED;
            memcpy(out->ct, it.bytes, PPCP_RV_BS_CT_BYTES);
            seen |= K_CT;
        } else if (ppcp_cbor_key_is(key, key_len, "pk")) {
            if (it.type != PPCP_CBOR_BYTES || it.len != PPCP_RV_BS_KEY_BYTES)
                return PPCP_ERR_MALFORMED;
            memcpy(out->pk, it.bytes, PPCP_RV_BS_KEY_BYTES);
            seen |= K_PK;
        } else if (ppcp_cbor_key_is(key, key_len, "mac")) {
            if (it.type != PPCP_CBOR_BYTES || it.len != PPCP_RV_BS_MAC_BYTES)
                return PPCP_ERR_MALFORMED;
            memcpy(out->mac, it.bytes, PPCP_RV_BS_MAC_BYTES);
            seen |= K_MAC;
        } else if (ppcp_cbor_key_is(key, key_len, "rc")) {
            if (it.type != PPCP_CBOR_UINT)
                return PPCP_ERR_MALFORMED;
            if (it.i < (int64_t)PPCP_BS_RC_UNSUPPORTED_VERSION ||
                it.i > (int64_t)PPCP_BS_RC_MALFORMED)
                return PPCP_ERR_MALFORMED;
            out->rc = (ppcp_bs_reason)it.i;
            seen |= K_RC;
        } else {
            /* ⛔ 11.4c1 / E46 — AN UNRECOGNISED KEY IS `malformed`.  Not
             * skipped, not ignored.  This is the one closed vocabulary in the
             * protocol set and the single line that makes it so. */
            return PPCP_ERR_MALFORMED;
        }
    }

    if ((seen & K_TY) == 0u)
        return PPCP_ERR_MALFORMED;          /* 11.4b — every frame carries ty */
    if (seen != required_keys(out->ty))
        return PPCP_ERR_MALFORMED;          /* missing, or belonging elsewhere */

    /* 11.4d — `v` is the FIRST key of bs_offer and bs_accept, so that a peer
     * which does not implement a later bootstrap version decodes far enough to
     * say so (11.4e, 4.2a's reason on this path).  It falls out of 4.3a's
     * deterministic ordering at the encoder and is checked here because the
     * clause is a MUST about the frame, not about one implementation. */
    if ((out->ty == PPCP_BS_OFFER || out->ty == PPCP_BS_ACCEPT) && !first_key_is_v)
        return PPCP_ERR_MALFORMED;

    *out_consumed = PPCP_FRAME_HEADER_BYTES + payload_len;
    return PPCP_OK;
}
