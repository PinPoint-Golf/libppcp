/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * The limit-enforcing CBOR decoder of PPCP-ENC §4 and §8.
 *
 * ENC 3a requires a receiver to reject an oversized item *without allocating
 * for it*.  This decoder satisfies that in the strongest available way: it
 * never allocates and never copies.  Every string it reports is a pointer into
 * the caller's buffer, and every length is checked against §8 the moment its
 * head is read — before a single byte of the body is touched.
 *
 * What it refuses, and why each refusal is load-bearing:
 *
 *   integer keys      ENC 4a.  The one extension mechanism the protocol gave
 *                     up, so that a wire can be read in a hex dump.
 *   duplicate keys    ENC 4d.  Two spellings of one field is two meanings.
 *   `null`            ENC 4c.  Absence is the absence of the key, and several
 *                     absences are load-bearing (I28, `evidence_ref`).
 *   tags              ENC 4d.  Nothing in PPCP is tagged; a tag is a decoder
 *                     from another protocol.
 *   indefinite length ENC 4d.  A length that arrives after the data defeats
 *                     the before-allocation rule this decoder exists for.
 *
 * What it accepts: half (0xF9) and single (0xFA) floats, widened to double.
 * ENC §4 forbids emitting them and requires accepting them, which is the
 * ordinary Postel split and the one place this decoder is deliberately lax.
 */
#include "ppcp/cbor.h"
#include "ppcp/frame.h"

#include <string.h>

ppcp_cbor_limits ppcp_cbor_limits_for_channel(uint8_t channel)
{
    ppcp_cbor_limits l;
    l.max_bytes    = ppcp_channel_frame_limit(channel);
    l.max_text     = PPCP_CBOR_MAX_TEXT;
    l.max_depth    = PPCP_CBOR_MAX_DEPTH;
    l.max_elements = PPCP_CBOR_MAX_ELEMENTS;
    return l;
}

void ppcp_cbor_reader_init(ppcp_cbor_reader *r, const uint8_t *buf, size_t len,
                           ppcp_cbor_limits lim)
{
    memset(r, 0, sizeof(*r));
    r->buf = buf;
    r->len = (buf == NULL) ? 0 : len;
    r->lim = lim;
    if (r->lim.max_depth == 0 || r->lim.max_depth > PPCP_CBOR_MAX_DEPTH)
        r->lim.max_depth = PPCP_CBOR_MAX_DEPTH;
    if (r->lim.max_elements == 0 || r->lim.max_elements > PPCP_CBOR_MAX_ELEMENTS)
        r->lim.max_elements = PPCP_CBOR_MAX_ELEMENTS;
    if (r->lim.max_text == 0 || r->lim.max_text > PPCP_CBOR_MAX_TEXT)
        r->lim.max_text = PPCP_CBOR_MAX_TEXT;
    if (r->lim.max_bytes == 0)
        r->lim.max_bytes = PPCP_LIMIT_CONTROL_FRAME;
}

/* Reads the initial byte and its argument.  `ai` is the additional information
 * field, returned so the caller can distinguish 24..27 from an immediate
 * value and reject 28..31. */
static ppcp_result read_head(ppcp_cbor_reader *r, uint8_t *major, uint8_t *ai, uint64_t *arg)
{
    uint8_t ib;
    size_t  need;
    uint64_t v = 0;
    size_t   i;

    if (r->pos >= r->len)
        return PPCP_ERR_TRUNCATED;
    ib = r->buf[r->pos];
    *major = (uint8_t)(ib >> 5);
    *ai    = (uint8_t)(ib & 0x1fu);
    r->pos++;

    if (*ai < 24u) {
        *arg = *ai;
        return PPCP_OK;
    }
    if (*ai == 31u) {
        /* Indefinite length, or a break.  ENC 4d. */
        return PPCP_ERR_MALFORMED;
    }
    if (*ai > 27u)
        return PPCP_ERR_MALFORMED;      /* 28..30 are reserved */

    need = (size_t)1u << (*ai - 24u);   /* 1, 2, 4, 8 */
    if (r->len - r->pos < need)
        return PPCP_ERR_TRUNCATED;
    for (i = 0; i < need; i++)
        v = (v << 8) | r->buf[r->pos + i];
    r->pos += need;
    *arg = v;
    return PPCP_OK;
}

/* IEEE-754 binary16 -> double.  ENC §4 requires a decoder to accept it. */
static double half_to_double(uint16_t h)
{
    int      sign = (h >> 15) & 1;
    int      exp  = (h >> 10) & 0x1f;
    uint32_t man  = (uint32_t)(h & 0x3ffu);
    double   val;

    if (exp == 0) {
        val = (double)man * (1.0 / 16777216.0);      /* 2^-24 */
    } else if (exp == 31) {
        /* Infinity or NaN.  Built rather than named, so no math.h is needed
         * and the purity gate stays closed. */
        if (man == 0) {
            val = 1e308 * 10.0;                       /* +inf */
        } else {
            double inf = 1e308 * 10.0;
            val = inf - inf;                          /* NaN */
        }
        return sign ? -val : val;
    } else {
        double scale = 1.0;
        int    e     = exp - 25;                      /* 15 bias + 10 mantissa */
        int    k;
        if (e >= 0)
            for (k = 0; k < e; k++) scale *= 2.0;
        else
            for (k = 0; k < -e; k++) scale *= 0.5;
        val = (double)(man | 0x400u) * scale;
    }
    return sign ? -val : val;
}

static double single_to_double(uint32_t b)
{
    union { uint32_t u; float f; } cv;
    cv.u = b;
    return (double)cv.f;
}

ppcp_result ppcp_cbor_read(ppcp_cbor_reader *r, ppcp_cbor_item *out)
{
    uint8_t     major, ai;
    uint64_t    arg;
    ppcp_result rc;

    if (r == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    rc = read_head(r, &major, &ai, &arg);
    if (rc != PPCP_OK)
        return rc;

    switch (major) {
    case 0:  /* unsigned */
        if (arg > (uint64_t)INT64_MAX)
            return PPCP_ERR_MALFORMED;   /* ENC §4: MUST fit in int64 */
        out->type = PPCP_CBOR_UINT;
        out->i    = (int64_t)arg;
        return PPCP_OK;

    case 1:  /* negative */
        if (arg > (uint64_t)INT64_MAX)
            return PPCP_ERR_MALFORMED;
        out->type = PPCP_CBOR_NINT;
        out->i    = -1 - (int64_t)arg;
        return PPCP_OK;

    case 2:  /* byte string */
        if (arg > r->lim.max_bytes)
            return PPCP_ERR_LIMIT;       /* checked before the body is touched */
        if (arg > r->len - r->pos)
            return PPCP_ERR_TRUNCATED;
        out->type  = PPCP_CBOR_BYTES;
        out->bytes = r->buf + r->pos;
        out->len   = (size_t)arg;
        r->pos    += (size_t)arg;
        return PPCP_OK;

    case 3:  /* text string */
        if (arg > r->lim.max_text)
            return PPCP_ERR_LIMIT;
        if (arg > r->len - r->pos)
            return PPCP_ERR_TRUNCATED;
        out->type  = PPCP_CBOR_TEXT;
        out->bytes = r->buf + r->pos;
        out->len   = (size_t)arg;
        r->pos    += (size_t)arg;
        return PPCP_OK;

    case 4:  /* array */
    case 5:  /* map */
        if (arg > r->lim.max_elements)
            return PPCP_ERR_LIMIT;
        /* An element count is a promise about how many items follow; a count
         * larger than the bytes remaining could not possibly be kept, and
         * refusing it here is what stops a two-byte header from asking for a
         * million allocations. */
        if (arg > (uint64_t)(r->len - r->pos))
            return PPCP_ERR_TRUNCATED;
        out->type  = (major == 4) ? PPCP_CBOR_ARRAY : PPCP_CBOR_MAP;
        out->count = (uint32_t)arg;
        return PPCP_OK;

    case 6:  /* tag — ENC 4d */
        return PPCP_ERR_MALFORMED;

    case 7:
        switch (ai) {
        case 20: out->type = PPCP_CBOR_BOOL; out->b = false; return PPCP_OK;
        case 21: out->type = PPCP_CBOR_BOOL; out->b = true;  return PPCP_OK;
        case 22: return PPCP_ERR_MALFORMED;  /* null — ENC 4c */
        case 23: return PPCP_ERR_MALFORMED;  /* undefined */
        case 25: out->type = PPCP_CBOR_DOUBLE; out->f = half_to_double((uint16_t)arg);   return PPCP_OK;
        case 26: out->type = PPCP_CBOR_DOUBLE; out->f = single_to_double((uint32_t)arg); return PPCP_OK;
        case 27: {
            union { uint64_t u; double d; } cv;
            cv.u = arg;
            out->type = PPCP_CBOR_DOUBLE;
            out->f    = cv.d;
            return PPCP_OK;
        }
        default:
            return PPCP_ERR_MALFORMED;       /* other simple values */
        }

    default:
        return PPCP_ERR_MALFORMED;
    }
}

/* Iterative, with an explicit counter stack: I13 requires an unknown key's
 * value to be skipped at any depth, and a recursive skip would put the C stack
 * at the mercy of the sender.  The depth limit of §8 would refuse a deep item
 * first, but the property should not depend on that ordering. */
ppcp_result ppcp_cbor_skip(ppcp_cbor_reader *r)
{
    uint64_t       pending[PPCP_CBOR_MAX_DEPTH];
    uint32_t       depth = 0;
    ppcp_cbor_item it;
    ppcp_result    rc;

    for (;;) {
        rc = ppcp_cbor_read(r, &it);
        if (rc != PPCP_OK)
            return rc;

        if (it.type == PPCP_CBOR_ARRAY || it.type == PPCP_CBOR_MAP) {
            uint64_t n = (it.type == PPCP_CBOR_MAP)
                             ? (uint64_t)it.count * 2u
                             : (uint64_t)it.count;
            if (n > 0) {
                if (depth >= r->lim.max_depth)
                    return PPCP_ERR_LIMIT;    /* ENC §8 nesting depth */
                pending[depth++] = n;
                continue;
            }
        }

        /* A complete item: settle it against every enclosing container. */
        while (depth > 0) {
            pending[depth - 1]--;
            if (pending[depth - 1] > 0)
                break;
            depth--;
        }
        if (depth == 0)
            return PPCP_OK;
    }
}

ppcp_result ppcp_cbor_read_key(ppcp_cbor_reader *r, const char **key, size_t *key_len)
{
    ppcp_cbor_item it;
    ppcp_result    rc = ppcp_cbor_read(r, &it);

    if (rc != PPCP_OK)
        return rc;
    if (it.type != PPCP_CBOR_TEXT)
        return PPCP_ERR_MALFORMED;   /* ENC 4a: map keys are text strings */
    *key     = (const char *)it.bytes;
    *key_len = it.len;
    return PPCP_OK;
}

bool ppcp_cbor_key_is(const char *key, size_t key_len, const char *lit)
{
    size_t n = strlen(lit);
    return key_len == n && memcmp(key, lit, n) == 0;
}

/* ------------------------------------------------------------- validate pass */

typedef struct span { const uint8_t *p; uint32_t n; } span;

typedef struct frame {
    uint64_t pending;   /* items still owed */
    bool     is_map;
    bool     expect_key;
    uint32_t keys_base; /* first slot of this map's key spans in the arena */
    uint32_t keys_used;
} frame;

ppcp_result ppcp_cbor_validate(const uint8_t *buf, size_t len, ppcp_cbor_limits lim,
                               size_t *consumed)
{
    ppcp_cbor_reader r;
    frame            st[PPCP_CBOR_MAX_DEPTH];
    span             arena[PPCP_CBOR_DUP_SCRATCH];
    uint32_t         arena_used = 0;
    uint32_t         depth      = 0;
    ppcp_cbor_item   it;
    ppcp_result      rc;

    ppcp_cbor_reader_init(&r, buf, len, lim);

    for (;;) {
        bool is_key = (depth > 0 && st[depth - 1].is_map && st[depth - 1].expect_key);

        rc = ppcp_cbor_read(&r, &it);
        if (rc != PPCP_OK)
            return rc;

        if (is_key) {
            frame   *f = &st[depth - 1];
            uint32_t i;
            if (it.type != PPCP_CBOR_TEXT)
                return PPCP_ERR_MALFORMED;          /* ENC 4a */
            for (i = 0; i < f->keys_used; i++) {
                const span *s = &arena[f->keys_base + i];
                if (s->n == it.len && (it.len == 0 || memcmp(s->p, it.bytes, it.len) == 0))
                    return PPCP_ERR_MALFORMED;      /* ENC 4d: duplicate key */
            }
            if (f->keys_base + f->keys_used >= PPCP_CBOR_DUP_SCRATCH)
                return PPCP_ERR_LIMIT;              /* more keys than we can check */
            arena[f->keys_base + f->keys_used].p = it.bytes;
            arena[f->keys_base + f->keys_used].n = (uint32_t)it.len;
            f->keys_used++;
            arena_used = f->keys_base + f->keys_used;
            f->expect_key = false;
            f->pending--;
            continue;
        }

        if (it.type == PPCP_CBOR_ARRAY || it.type == PPCP_CBOR_MAP) {
            uint64_t n = (it.type == PPCP_CBOR_MAP)
                             ? (uint64_t)it.count * 2u
                             : (uint64_t)it.count;
            if (n > 0) {
                if (depth >= r.lim.max_depth)
                    return PPCP_ERR_LIMIT;
                st[depth].pending    = n;
                st[depth].is_map     = (it.type == PPCP_CBOR_MAP);
                st[depth].expect_key = (it.type == PPCP_CBOR_MAP);
                st[depth].keys_base  = arena_used;
                st[depth].keys_used  = 0;
                depth++;
                continue;
            }
        }

        while (depth > 0) {
            frame *f = &st[depth - 1];
            f->pending--;
            if (f->is_map)
                f->expect_key = true;
            if (f->pending > 0)
                break;
            depth--;
            arena_used = (depth > 0) ? st[depth].keys_base : 0;
        }
        if (depth == 0)
            break;
    }

    if (consumed != NULL)
        *consumed = r.pos;
    return PPCP_OK;
}
