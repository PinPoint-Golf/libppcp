/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * PPCP-ENC §5.
 */
#include "ppcp/envelope.h"
#include "ppcp/frame.h"

#include <string.h>

/* ENC §5's four reserved keys, in RFC 8949 §4.2.1 order — which for these four
 * is also the order the specification tabulates them in, because each is longer
 * than the last. */
static const char *const reserved_keys[4] = { "type", "msg_id", "reply_to", "session_id" };

void ppcp_msg_seq_init(ppcp_msg_seq *s)
{
    if (s != NULL)
        s->next = 1;   /* ENC §5: monotonically increasing from 1 */
}

uint64_t ppcp_msg_seq_next(ppcp_msg_seq *s)
{
    uint64_t v;
    if (s == NULL)
        return 0;
    if (s->next == 0)
        s->next = 1;
    v = s->next;
    s->next++;
    return v;
}

ppcp_result ppcp_envelope_init(ppcp_envelope *e, const char *type, uint64_t msg_id)
{
    size_t n;

    if (e == NULL || type == NULL)
        return PPCP_ERR_INVALID;
    n = strlen(type);
    if (n == 0 || n > PPCP_TYPE_MAX)
        return PPCP_ERR_INVALID;
    if (msg_id == 0)
        return PPCP_ERR_INVALID;   /* ENC §5: from 1 */

    memset(e, 0, sizeof(*e));
    memcpy(e->type, type, n);
    e->type[n]  = '\0';
    e->type_len = (uint8_t)n;
    e->msg_id   = msg_id;
    return PPCP_OK;
}

ppcp_result ppcp_envelope_set_reply_to(ppcp_envelope *e, uint64_t reply_to)
{
    if (e == NULL || reply_to == 0)
        return PPCP_ERR_INVALID;
    e->has_reply_to = true;
    e->reply_to     = reply_to;
    return PPCP_OK;
}

ppcp_result ppcp_envelope_set_session_id(ppcp_envelope *e, const char *sid, size_t len)
{
    ppcp_result rc;
    if (e == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_id_set(&e->session_id, sid, len);
    if (rc != PPCP_OK)
        return rc;
    e->has_session_id = true;
    return PPCP_OK;
}

ppcp_result ppcp_envelope_validate(const ppcp_envelope *e)
{
    if (e == NULL || e->type_len == 0 || e->type_len > PPCP_TYPE_MAX)
        return PPCP_ERR_INVALID;
    if (e->msg_id == 0)
        return PPCP_ERR_INVALID;
    if (e->has_reply_to && e->reply_to == 0)
        return PPCP_ERR_INVALID;
    if (e->has_session_id && !ppcp_id_is_set(&e->session_id))
        return PPCP_ERR_INVALID;
    return PPCP_OK;
}

bool ppcp_envelope_is_reserved_key(const char *key, size_t len)
{
    unsigned i;
    for (i = 0; i < 4; i++) {
        size_t n = strlen(reserved_keys[i]);
        if (len == n && memcmp(key, reserved_keys[i], n) == 0)
            return true;
    }
    return false;
}

/* How many reserved keys this envelope actually carries. */
static unsigned reserved_count(const ppcp_envelope *e)
{
    unsigned n = 2;                    /* type, msg_id */
    if (e->has_reply_to)     n++;
    if (e->has_session_id)   n++;
    return n;
}

/* Emits reserved key `idx` (0..3), skipping the absent optional ones. */
static ppcp_result emit_reserved(ppcp_cbor_writer *w, const ppcp_envelope *e, unsigned idx)
{
    switch (idx) {
    case 0:
        if (ppcp_cbor_write_text_z(w, "type") != PPCP_OK) return ppcp_cbor_writer_status(w);
        return ppcp_cbor_write_text(w, e->type, e->type_len);
    case 1:
        if (ppcp_cbor_write_text_z(w, "msg_id") != PPCP_OK) return ppcp_cbor_writer_status(w);
        return ppcp_cbor_write_uint(w, e->msg_id);
    case 2:
        if (!e->has_reply_to) return PPCP_OK;
        if (ppcp_cbor_write_text_z(w, "reply_to") != PPCP_OK) return ppcp_cbor_writer_status(w);
        return ppcp_cbor_write_uint(w, e->reply_to);
    case 3:
        if (!e->has_session_id) return PPCP_OK;
        if (ppcp_cbor_write_text_z(w, "session_id") != PPCP_OK) return ppcp_cbor_writer_status(w);
        return ppcp_cbor_write_text(w, e->session_id.v, e->session_id.len);
    default:
        return PPCP_ERR_INVALID;
    }
}

ppcp_result ppcp_envelope_open(ppcp_cbor_writer *w, ppcp_envelope_writer *ew,
                               const ppcp_envelope *e, size_t body_fields)
{
    ppcp_result rc;

    if (w == NULL || ew == NULL || e == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_envelope_validate(e);
    if (rc != PPCP_OK)
        return rc;

    ew->e       = e;
    ew->next    = 0;
    ew->literal = (w->order == PPCP_CBOR_ORDER_LITERAL);

    rc = ppcp_cbor_write_map(w, reserved_count(e) + body_fields);
    if (rc != PPCP_OK)
        return rc;

    if (ew->literal) {
        unsigned i;
        for (i = 0; i < 4; i++) {
            rc = emit_reserved(w, e, i);
            if (rc != PPCP_OK)
                return rc;
        }
        ew->next = 4;
    }
    return PPCP_OK;
}

ppcp_result ppcp_envelope_before(ppcp_cbor_writer *w, ppcp_envelope_writer *ew,
                                 const char *key, size_t key_len)
{
    if (w == NULL || ew == NULL || key == NULL || key_len == 0)
        return PPCP_ERR_INVALID;
    /* ENC 5a. */
    if (ppcp_envelope_is_reserved_key(key, key_len))
        return PPCP_ERR_INVALID;

    while (ew->next < 4) {
        const char *rk = reserved_keys[ew->next];
        size_t      rn = strlen(rk);
        ppcp_result rc;
        if (ppcp_cbor_key_cmp(rk, rn, key, key_len) > 0)
            break;                       /* the body key comes first */
        rc = emit_reserved(w, ew->e, ew->next);
        if (rc != PPCP_OK)
            return rc;
        ew->next++;
    }
    return PPCP_OK;
}

ppcp_result ppcp_envelope_close(ppcp_cbor_writer *w, ppcp_envelope_writer *ew)
{
    if (w == NULL || ew == NULL)
        return PPCP_ERR_INVALID;
    while (ew->next < 4) {
        ppcp_result rc = emit_reserved(w, ew->e, ew->next);
        if (rc != PPCP_OK)
            return rc;
        ew->next++;
    }
    return PPCP_OK;
}

static ppcp_result message_encode(uint8_t *out, size_t cap, uint8_t channel,
                                  const ppcp_envelope *e, size_t body_fields,
                                  ppcp_body_writer body, void *ctx,
                                  ppcp_cbor_order order, size_t *out_written)
{
    ppcp_cbor_writer     w;
    ppcp_envelope_writer ew;
    ppcp_result          rc;
    size_t               payload_len = 0;

    if (out == NULL)
        return PPCP_ERR_INVALID;
    if (cap < PPCP_FRAME_HEADER_BYTES)
        return PPCP_ERR_NOSPACE;

    /* Encoded in place, straight after the header, so the whole frame is one
     * buffer and nothing is copied. */
    ppcp_cbor_writer_init_order(&w, out + PPCP_FRAME_HEADER_BYTES,
                                cap - PPCP_FRAME_HEADER_BYTES, order);

    rc = ppcp_envelope_open(&w, &ew, e, body_fields);
    if (rc != PPCP_OK)
        return rc;
    if (body != NULL) {
        rc = body(&w, &ew, ctx);
        if (rc != PPCP_OK)
            return rc;
    }
    rc = ppcp_envelope_close(&w, &ew);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_cbor_writer_finish(&w, &payload_len);
    if (rc != PPCP_OK)
        return rc;

    rc = ppcp_frame_check_length(channel, (uint32_t)payload_len);
    if (rc != PPCP_OK)
        return rc;
    {
        ppcp_frame_header h;
        h.payload_len = (uint32_t)payload_len;
        h.channel     = channel;
        h.flags       = 0;
        h.reserved    = 0;
        rc = ppcp_frame_header_write(out, &h);
        if (rc != PPCP_OK)
            return rc;
    }
    if (out_written != NULL)
        *out_written = PPCP_FRAME_HEADER_BYTES + payload_len;
    return PPCP_OK;
}

ppcp_result ppcp_message_encode(uint8_t *out, size_t cap, uint8_t channel,
                                const ppcp_envelope *e, size_t body_fields,
                                ppcp_body_writer body, void *ctx, size_t *out_written)
{
    return message_encode(out, cap, channel, e, body_fields, body, ctx,
                          PPCP_CBOR_ORDER_DETERMINISTIC, out_written);
}

ppcp_result ppcp_message_encode_literal(uint8_t *out, size_t cap, uint8_t channel,
                                        const ppcp_envelope *e, size_t body_fields,
                                        ppcp_body_writer body, void *ctx, size_t *out_written)
{
    return message_encode(out, cap, channel, e, body_fields, body, ctx,
                          PPCP_CBOR_ORDER_LITERAL, out_written);
}

ppcp_result ppcp_envelope_decode(const uint8_t *payload, size_t len, ppcp_cbor_limits lim,
                                 ppcp_envelope *out, uint32_t *out_pairs)
{
    ppcp_cbor_reader r;
    ppcp_cbor_item   it;
    ppcp_result      rc;
    uint32_t         i, pairs;
    bool             seen_type = false, seen_msg_id = false;

    if (payload == NULL || out == NULL)
        return PPCP_ERR_INVALID;

    /* One validating pass first: ENC 4a, 4c and 4d at every depth, and every
     * §8 limit.  The field walk below can then be simple, because anything it
     * meets has already been proved well-formed. */
    rc = ppcp_cbor_validate(payload, len, lim, NULL);
    if (rc != PPCP_OK)
        return rc;

    ppcp_cbor_reader_init(&r, payload, len, lim);
    rc = ppcp_cbor_read(&r, &it);
    if (rc != PPCP_OK)
        return rc;
    if (it.type != PPCP_CBOR_MAP)
        return PPCP_ERR_MALFORMED;   /* ENC §5: the payload is a map */
    pairs = it.count;

    memset(out, 0, sizeof(*out));

    for (i = 0; i < pairs; i++) {
        const char *k;
        size_t      klen;
        rc = ppcp_cbor_read_key(&r, &k, &klen);
        if (rc != PPCP_OK)
            return rc;

        if (ppcp_cbor_key_is(k, klen, "type")) {
            rc = ppcp_cbor_read(&r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_TEXT || it.len == 0 || it.len > PPCP_TYPE_MAX)
                return PPCP_ERR_MALFORMED;
            memcpy(out->type, it.bytes, it.len);
            out->type[it.len] = '\0';
            out->type_len     = (uint8_t)it.len;
            seen_type = true;
        } else if (ppcp_cbor_key_is(k, klen, "msg_id")) {
            rc = ppcp_cbor_read(&r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_UINT || it.i <= 0)
                return PPCP_ERR_MALFORMED;
            out->msg_id = (uint64_t)it.i;
            seen_msg_id = true;
        } else if (ppcp_cbor_key_is(k, klen, "reply_to")) {
            rc = ppcp_cbor_read(&r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_UINT || it.i <= 0)
                return PPCP_ERR_MALFORMED;
            out->reply_to     = (uint64_t)it.i;
            out->has_reply_to = true;
        } else if (ppcp_cbor_key_is(k, klen, "session_id")) {
            rc = ppcp_cbor_read(&r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_TEXT)
                return PPCP_ERR_MALFORMED;
            if (ppcp_id_set(&out->session_id, (const char *)it.bytes, it.len) != PPCP_OK)
                return PPCP_ERR_MALFORMED;
            out->has_session_id = true;
        } else {
            /* I13 — an unknown key at any depth is skipped, never fatal.  This
             * is the mechanism by which a MINOR version adds a field. */
            rc = ppcp_cbor_skip(&r);
            if (rc != PPCP_OK)
                return rc;
        }
    }

    if (!seen_type || !seen_msg_id)
        return PPCP_ERR_MALFORMED;
    if (out_pairs != NULL)
        *out_pairs = pairs;
    return PPCP_OK;
}

ppcp_result ppcp_envelope_recover_msg_id(const uint8_t *payload, size_t len,
                                         ppcp_cbor_limits lim, uint64_t *out_msg_id)
{
    ppcp_cbor_reader r;
    ppcp_cbor_item   it;
    ppcp_result      rc;
    uint32_t         i, pairs;

    if (payload == NULL || out_msg_id == NULL)
        return PPCP_ERR_INVALID;

    /* Deliberately does NOT validate first: this is the ENC 5d path, reached
     * because the payload failed to decode.  It reads only as far as it can. */
    ppcp_cbor_reader_init(&r, payload, len, lim);
    rc = ppcp_cbor_read(&r, &it);
    if (rc != PPCP_OK || it.type != PPCP_CBOR_MAP)
        return PPCP_ERR_MALFORMED;
    pairs = it.count;

    for (i = 0; i < pairs; i++) {
        const char *k;
        size_t      klen;
        if (ppcp_cbor_read_key(&r, &k, &klen) != PPCP_OK)
            return PPCP_ERR_NOT_FOUND;
        if (ppcp_cbor_key_is(k, klen, "msg_id")) {
            if (ppcp_cbor_read(&r, &it) != PPCP_OK)
                return PPCP_ERR_NOT_FOUND;
            if (it.type != PPCP_CBOR_UINT || it.i <= 0)
                return PPCP_ERR_NOT_FOUND;
            *out_msg_id = (uint64_t)it.i;
            return PPCP_OK;
        }
        if (ppcp_cbor_skip(&r) != PPCP_OK)
            return PPCP_ERR_NOT_FOUND;
    }
    return PPCP_ERR_NOT_FOUND;
}
