/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * The deterministic CBOR encoder of PPCP-ENC §4.
 *
 * Deterministic encoding is a SHOULD in ENC 4e and a MUST in RV 4.3a, and the
 * difference between the two is a discipline nobody can hold across a hundred
 * call sites.  So the writer holds it: a map key that does not sort strictly
 * after its predecessor is refused, which delivers RFC 8949 §4.2.1 ordering and
 * ENC 4d's duplicate-key prohibition from one comparison.
 */
#include "ppcp/cbor.h"

#include <string.h>

/* ------------------------------------------------------------------ helpers */

static ppcp_result fail(ppcp_cbor_writer *w, ppcp_result r)
{
    if (w->err == PPCP_OK)
        w->err = r;
    return w->err;
}

static ppcp_result put(ppcp_cbor_writer *w, const void *p, size_t n)
{
    if (w->err != PPCP_OK)
        return w->err;
    if (n > w->cap - w->len)
        return fail(w, PPCP_ERR_NOSPACE);
    memcpy(w->buf + w->len, p, n);
    w->len += n;
    return PPCP_OK;
}

static ppcp_result put_byte(ppcp_cbor_writer *w, uint8_t b)
{
    return put(w, &b, 1);
}

/* Shortest-form head — RFC 8949 §4.2.1's first requirement. */
static ppcp_result put_head(ppcp_cbor_writer *w, uint8_t major, uint64_t arg)
{
    uint8_t  h[9];
    size_t   n;
    uint32_t mj = (uint32_t)major << 5;

    if (arg < 24u) {
        h[0] = (uint8_t)(mj | (uint8_t)arg);
        n = 1;
    } else if (arg <= 0xffu) {
        h[0] = (uint8_t)(mj | 24u);
        h[1] = (uint8_t)arg;
        n = 2;
    } else if (arg <= 0xffffu) {
        h[0] = (uint8_t)(mj | 25u);
        h[1] = (uint8_t)(arg >> 8);
        h[2] = (uint8_t)arg;
        n = 3;
    } else if (arg <= 0xffffffffu) {
        h[0] = (uint8_t)(mj | 26u);
        h[1] = (uint8_t)(arg >> 24);
        h[2] = (uint8_t)(arg >> 16);
        h[3] = (uint8_t)(arg >> 8);
        h[4] = (uint8_t)arg;
        n = 5;
    } else {
        h[0] = (uint8_t)(mj | 27u);
        h[1] = (uint8_t)(arg >> 56);
        h[2] = (uint8_t)(arg >> 48);
        h[3] = (uint8_t)(arg >> 40);
        h[4] = (uint8_t)(arg >> 32);
        h[5] = (uint8_t)(arg >> 24);
        h[6] = (uint8_t)(arg >> 16);
        h[7] = (uint8_t)(arg >> 8);
        h[8] = (uint8_t)arg;
        n = 9;
    }
    return put(w, h, n);
}

/* Called before every value (and before every key).  Maintains the container
 * bookkeeping that makes ppcp_cbor_writer_finish() able to say "this map is
 * still owed two items". */
static ppcp_result account_value(ppcp_cbor_writer *w)
{
    ppcp_cbor_level *l;

    if (w->err != PPCP_OK)
        return w->err;
    if (w->depth == 0) {
        /* The top-level item.  There is exactly one per payload (ENC §5: the
         * frame payload is a CBOR map), so a second one is a caller bug. */
        if (w->len > 0)
            return fail(w, PPCP_ERR_INVALID);
        return PPCP_OK;
    }
    l = &w->level[w->depth - 1];
    if (l->remaining == 0)
        return fail(w, PPCP_ERR_INVALID);
    if (l->is_map && l->expect_key)
        /* A non-text item where a key belongs.  ENC 4a: map keys are text. */
        return fail(w, PPCP_ERR_INVALID);
    l->remaining--;
    if (l->is_map)
        l->expect_key = true;
    /* Close every container that has been fully written. */
    while (w->depth > 0 && w->level[w->depth - 1].remaining == 0) {
        w->depth--;
        if (w->depth > 0) {
            ppcp_cbor_level *p = &w->level[w->depth - 1];
            if (p->remaining == 0)
                return fail(w, PPCP_ERR_INVALID);
            p->remaining--;
            if (p->is_map)
                p->expect_key = true;
        }
    }
    return PPCP_OK;
}

/* Opening a container consumes a slot in its parent only once it is complete,
 * so the parent's accounting is deferred to the closing walk above.  What is
 * accounted here is the key/value position the container itself occupies. */
static ppcp_result open_container(ppcp_cbor_writer *w, uint8_t major, size_t count)
{
    ppcp_cbor_level *l;

    if (w->err != PPCP_OK)
        return w->err;
    if (count > PPCP_CBOR_MAX_ELEMENTS)
        return fail(w, PPCP_ERR_LIMIT);
    if (w->depth == 0 && w->len > 0)
        return fail(w, PPCP_ERR_INVALID);
    if (w->depth > 0) {
        l = &w->level[w->depth - 1];
        if (l->remaining == 0)
            return fail(w, PPCP_ERR_INVALID);
        if (l->is_map && l->expect_key)
            return fail(w, PPCP_ERR_INVALID);   /* a map key may not be a container */
    }
    if (w->depth >= PPCP_CBOR_MAX_DEPTH)
        return fail(w, PPCP_ERR_LIMIT);         /* ENC §8 nesting depth */

    if (put_head(w, major, (uint64_t)count) != PPCP_OK)
        return w->err;

    if (count == 0) {
        /* An empty container is complete the moment it opens. */
        if (w->depth == 0)
            return PPCP_OK;
        return account_value(w);
    }

    l = &w->level[w->depth];
    l->remaining    = (uint32_t)(major == 5 ? count * 2u : count);
    l->is_map       = (major == 5);
    l->expect_key   = (major == 5);
    l->last_key_off = 0;
    l->last_key_len = 0;
    w->depth++;
    return PPCP_OK;
}

/* --------------------------------------------------------------------- API */

void ppcp_cbor_writer_init_order(ppcp_cbor_writer *w, uint8_t *buf, size_t cap,
                                 ppcp_cbor_order order)
{
    memset(w, 0, sizeof(*w));
    w->buf   = buf;
    w->cap   = cap;
    w->order = order;
    if (buf == NULL)
        w->err = PPCP_ERR_INVALID;
}

void ppcp_cbor_writer_init(ppcp_cbor_writer *w, uint8_t *buf, size_t cap)
{
    ppcp_cbor_writer_init_order(w, buf, cap, PPCP_CBOR_ORDER_DETERMINISTIC);
}

int ppcp_cbor_key_cmp(const char *a, size_t alen, const char *b, size_t blen)
{
    /* RFC 8949 §4.2.1 orders by the bytewise lexicographic order of the
     * *encoded* key.  For a text string the head is 0x60+len while len < 24, so
     * a shorter key sorts first; at 24 bytes and above the head grows and the
     * same rule still falls out of comparing heads first.  Comparing the head
     * byte(s) then the payload is exactly that, without materialising the
     * encoding.
     *
     * RV 4.3b is a claim about this ordering: `v` encodes 0x61 0x76 and every
     * other top-level key is at least two characters, so `v` sorts first by
     * construction, which is what makes RV 4.2a true. */
    size_t n;
    int    c;

    if (alen < 24u && blen < 24u) {
        if (alen != blen)
            return alen < blen ? -1 : 1;
    } else {
        /* Different head widths compare on the head. */
        uint8_t ha = alen < 24u ? (uint8_t)(0x60u + alen) : (alen <= 0xffu ? 0x78u : 0x79u);
        uint8_t hb = blen < 24u ? (uint8_t)(0x60u + blen) : (blen <= 0xffu ? 0x78u : 0x79u);
        if (ha != hb)
            return ha < hb ? -1 : 1;
        if (alen != blen)
            return alen < blen ? -1 : 1;
    }
    n = alen;
    c = (n == 0) ? 0 : memcmp(a, b, n);
    if (c != 0)
        return c < 0 ? -1 : 1;
    return 0;
}

ppcp_result ppcp_cbor_write_uint(ppcp_cbor_writer *w, uint64_t v)
{
    if (v > (uint64_t)INT64_MAX)
        return fail(w, PPCP_ERR_INVALID);   /* ENC §4: MUST fit in int64 */
    if (account_value(w) != PPCP_OK)
        return w->err;
    return put_head(w, 0, v);
}

ppcp_result ppcp_cbor_write_int(ppcp_cbor_writer *w, int64_t v)
{
    if (account_value(w) != PPCP_OK)
        return w->err;
    if (v >= 0)
        return put_head(w, 0, (uint64_t)v);
    /* -1 - n, computed on the unsigned side so INT64_MIN does not overflow. */
    return put_head(w, 1, ~(uint64_t)v);
}

ppcp_result ppcp_cbor_write_bytes(ppcp_cbor_writer *w, const uint8_t *p, size_t n)
{
    if (account_value(w) != PPCP_OK)
        return w->err;
    if (n > 0 && p == NULL)
        return fail(w, PPCP_ERR_INVALID);
    if (put_head(w, 2, (uint64_t)n) != PPCP_OK)
        return w->err;
    return put(w, p, n);
}

ppcp_result ppcp_cbor_write_text(ppcp_cbor_writer *w, const char *p, size_t n)
{
    ppcp_cbor_level *l;
    size_t           off;

    if (w->err != PPCP_OK)
        return w->err;
    if (n > 0 && p == NULL)
        return fail(w, PPCP_ERR_INVALID);
    if (n > PPCP_CBOR_MAX_TEXT)
        return fail(w, PPCP_ERR_LIMIT);

    l = (w->depth > 0) ? &w->level[w->depth - 1] : NULL;

    if (l != NULL && l->is_map && l->expect_key) {
        /* This is a key.  Order it against its predecessor. */
        if (l->last_key_len != 0) {
            const char *prev     = (const char *)w->buf + l->last_key_off;
            size_t      prev_len = l->last_key_len;
            int         c        = ppcp_cbor_key_cmp(prev, prev_len, p, n);
            if (c == 0)
                return fail(w, PPCP_ERR_INVALID);   /* ENC 4d: duplicate key */
            if (c > 0 && w->order == PPCP_CBOR_ORDER_DETERMINISTIC)
                return fail(w, PPCP_ERR_INVALID);   /* ENC 4e / RV 4.3a */
        }
        if (l->remaining == 0)
            return fail(w, PPCP_ERR_INVALID);
        if (put_head(w, 3, (uint64_t)n) != PPCP_OK)
            return w->err;
        off = w->len;
        if (put(w, p, n) != PPCP_OK)
            return w->err;
        l->last_key_off = off;
        l->last_key_len = n;
        l->remaining--;
        l->expect_key = false;
        return PPCP_OK;
    }

    if (account_value(w) != PPCP_OK)
        return w->err;
    if (put_head(w, 3, (uint64_t)n) != PPCP_OK)
        return w->err;
    return put(w, p, n);
}

ppcp_result ppcp_cbor_write_text_z(ppcp_cbor_writer *w, const char *s)
{
    return ppcp_cbor_write_text(w, s, s ? strlen(s) : 0);
}

ppcp_result ppcp_cbor_write_bool(ppcp_cbor_writer *w, bool v)
{
    if (account_value(w) != PPCP_OK)
        return w->err;
    return put_byte(w, v ? 0xf5u : 0xf4u);
}

ppcp_result ppcp_cbor_write_double(ppcp_cbor_writer *w, double v)
{
    /* ENC §4: floats are emitted as doubles.  Half and single precision MUST
     * NOT be emitted, so there is no shortening pass here — the one place where
     * RFC 8949's "preferred serialisation" is deliberately not followed,
     * because the protocol overrides it. */
    union { double d; uint64_t u; } cv;
    uint8_t b[9];

    if (account_value(w) != PPCP_OK)
        return w->err;
    cv.d = v;
    b[0] = 0xfbu;
    b[1] = (uint8_t)(cv.u >> 56);
    b[2] = (uint8_t)(cv.u >> 48);
    b[3] = (uint8_t)(cv.u >> 40);
    b[4] = (uint8_t)(cv.u >> 32);
    b[5] = (uint8_t)(cv.u >> 24);
    b[6] = (uint8_t)(cv.u >> 16);
    b[7] = (uint8_t)(cv.u >> 8);
    b[8] = (uint8_t)cv.u;
    return put(w, b, 9);
}

ppcp_result ppcp_cbor_write_array(ppcp_cbor_writer *w, size_t count)
{
    return open_container(w, 4, count);
}

ppcp_result ppcp_cbor_write_map(ppcp_cbor_writer *w, size_t count)
{
    return open_container(w, 5, count);
}

ppcp_result ppcp_cbor_writer_status(const ppcp_cbor_writer *w)
{
    return w->err;
}

ppcp_result ppcp_cbor_writer_finish(const ppcp_cbor_writer *w, size_t *out_len)
{
    if (w->err != PPCP_OK)
        return w->err;
    if (w->depth != 0)
        return PPCP_ERR_INVALID;   /* a container is still owed items */
    if (out_len != NULL)
        *out_len = w->len;
    return PPCP_OK;
}
