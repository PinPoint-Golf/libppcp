/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * INTERNAL — see ppcp_codec.h for why this exists.
 */
#include "ppcp_codec.h"

#include <string.h>

/* -------------------------------------------------------------- enum maps */

const char *ppcp_enum_to_text(const ppcp_enum_map *m, int v)
{
    size_t i;
    for (i = 0; m[i].name != NULL; i++)
        if (m[i].value == v)
            return m[i].name;
    return NULL;
}

ppcp_result ppcp_enum_from_text(const ppcp_enum_map *m, const char *s, size_t len, int *out)
{
    size_t i;
    for (i = 0; m[i].name != NULL; i++) {
        if (ppcp_cbor_key_is(s, len, m[i].name)) {
            *out = m[i].value;
            return PPCP_OK;
        }
    }
    /* A closed enumeration.  CORE 10.3a's "unknown values are ignored, never
     * fatal" binds the eight OPEN registries of §10.3, which are carried as
     * strings and never reach this function. */
    return PPCP_ERR_MALFORMED;
}

/* ---------------------------------------------------------------- writer */

static ppcp_wfield wf_base(const char *key, ppcp_f_kind kind)
{
    ppcp_wfield f;
    memset(&f, 0, sizeof(f));
    f.key  = key;
    f.kind = kind;
    return f;
}

ppcp_wfield ppcp_wf_id(const char *key, const ppcp_id *id)
{
    ppcp_wfield f = wf_base(key, PPCP_F_ID);
    f.text     = id->v;
    f.text_len = id->len;
    return f;
}

ppcp_wfield ppcp_wf_text(const char *key, const char *s, size_t len)
{
    ppcp_wfield f = wf_base(key, PPCP_F_TEXT);
    f.text     = s;
    f.text_len = len;
    return f;
}

ppcp_wfield ppcp_wf_int(const char *key, int64_t v)
{
    ppcp_wfield f = wf_base(key, PPCP_F_INT);
    f.i = v;
    return f;
}

ppcp_wfield ppcp_wf_uint(const char *key, uint64_t v)
{
    ppcp_wfield f = wf_base(key, PPCP_F_UINT);
    f.u = v;
    return f;
}

ppcp_wfield ppcp_wf_bool(const char *key, bool v)
{
    ppcp_wfield f = wf_base(key, PPCP_F_BOOL);
    f.b = v;
    return f;
}

ppcp_wfield ppcp_wf_double(const char *key, double v)
{
    ppcp_wfield f = wf_base(key, PPCP_F_DOUBLE);
    f.f = v;
    return f;
}

ppcp_wfield ppcp_wf_bytes(const char *key, const uint8_t *p, size_t n)
{
    ppcp_wfield f = wf_base(key, PPCP_F_BYTES);
    f.bytes     = p;
    f.bytes_len = n;
    return f;
}

ppcp_wfield ppcp_wf_enum(const char *key, const ppcp_enum_map *m, int v)
{
    ppcp_wfield f  = wf_base(key, PPCP_F_ENUM);
    const char *s  = ppcp_enum_to_text(m, v);
    f.text         = s;
    f.text_len     = s ? strlen(s) : 0;
    return f;
}

ppcp_wfield ppcp_wf_sub(const char *key, ppcp_sub_write fn, const void *ctx)
{
    ppcp_wfield f = wf_base(key, PPCP_F_SUB);
    f.sub = fn;
    f.ctx = ctx;
    return f;
}

ppcp_result ppcp_rec_write(ppcp_cbor_writer *w, ppcp_wfield *f, size_t n)
{
    size_t i, j;

    if (w == NULL || (n > 0 && f == NULL))
        return PPCP_ERR_INVALID;

    /* Insertion sort into RFC 8949 §4.2.1 order.  n is a message's field
     * count — under thirty everywhere in PPCP — so this is cheaper than the
     * branch that would otherwise decide it at the source level, and it is
     * what makes ENC 4e a property of this function rather than a discipline
     * at sixty call sites. */
    for (i = 1; i < n; i++) {
        ppcp_wfield key = f[i];
        j = i;
        while (j > 0 &&
               ppcp_cbor_key_cmp(f[j - 1].key, strlen(f[j - 1].key),
                                 key.key, strlen(key.key)) > 0) {
            f[j] = f[j - 1];
            j--;
        }
        f[j] = key;
    }
    for (i = 1; i < n; i++) {
        if (ppcp_cbor_key_cmp(f[i - 1].key, strlen(f[i - 1].key),
                              f[i].key, strlen(f[i].key)) == 0)
            return PPCP_ERR_INVALID;      /* ENC 4d: never emit a duplicate */
    }

    if (ppcp_cbor_write_map(w, n) != PPCP_OK)
        return ppcp_cbor_writer_status(w);

    for (i = 0; i < n; i++) {
        ppcp_result rc;
        if (ppcp_cbor_write_text_z(w, f[i].key) != PPCP_OK)
            return ppcp_cbor_writer_status(w);
        switch (f[i].kind) {
        case PPCP_F_ID:
        case PPCP_F_TEXT:
        case PPCP_F_ENUM:
            if (f[i].text == NULL)
                return PPCP_ERR_INVALID;
            rc = ppcp_cbor_write_text(w, f[i].text, f[i].text_len);
            break;
        case PPCP_F_INT:    rc = ppcp_cbor_write_int(w, f[i].i); break;
        case PPCP_F_UINT:   rc = ppcp_cbor_write_uint(w, f[i].u); break;
        case PPCP_F_BOOL:   rc = ppcp_cbor_write_bool(w, f[i].b); break;
        case PPCP_F_DOUBLE: rc = ppcp_cbor_write_double(w, f[i].f); break;
        case PPCP_F_BYTES:  rc = ppcp_cbor_write_bytes(w, f[i].bytes, f[i].bytes_len); break;
        case PPCP_F_SUB:
            if (f[i].sub == NULL)
                return PPCP_ERR_INVALID;
            rc = f[i].sub(w, f[i].ctx);
            break;
        default:
            return PPCP_ERR_INVALID;
        }
        if (rc != PPCP_OK)
            return rc;
    }
    return ppcp_cbor_writer_status(w);
}

/* ---------------------------------------------------------------- reader */

ppcp_rfield ppcp_rf(const char *key, ppcp_f_kind kind, void *dst, bool *seen)
{
    ppcp_rfield f;
    memset(&f, 0, sizeof(f));
    f.key  = key;
    f.kind = kind;
    f.dst  = dst;
    f.seen = seen;
    return f;
}

ppcp_rfield ppcp_rf_enum(const char *key, const ppcp_enum_map *m, int *dst, bool *seen)
{
    ppcp_rfield f = ppcp_rf(key, PPCP_F_ENUM, dst, seen);
    /* The map is const data; the field carries it as the callback context. */
    f.ctx = (void *)(size_t)(const void *)m;
    return f;
}

ppcp_rfield ppcp_rf_sub(const char *key, ppcp_sub_read fn, void *dst, void *ctx, bool *seen)
{
    ppcp_rfield f = ppcp_rf(key, PPCP_F_SUB, dst, seen);
    f.sub = fn;
    f.ctx = ctx;
    return f;
}

static ppcp_result read_one(ppcp_cbor_reader *r, const ppcp_rfield *f)
{
    ppcp_cbor_item it;
    ppcp_result    rc;

    if (f->kind == PPCP_F_SUB) {
        if (f->sub == NULL)
            return PPCP_ERR_INVALID;
        return f->sub(r, f->dst, f->ctx);
    }

    rc = ppcp_cbor_read(r, &it);
    if (rc != PPCP_OK)
        return rc;

    switch (f->kind) {
    case PPCP_F_ID:
        if (it.type != PPCP_CBOR_TEXT)
            return PPCP_ERR_MALFORMED;
        if (ppcp_id_set((ppcp_id *)f->dst, (const char *)it.bytes, it.len) != PPCP_OK)
            return PPCP_ERR_MALFORMED;
        return PPCP_OK;
    case PPCP_F_TEXT: {
        ppcp_text_ref *t = (ppcp_text_ref *)f->dst;
        if (it.type != PPCP_CBOR_TEXT)
            return PPCP_ERR_MALFORMED;
        t->p   = (const char *)it.bytes;
        t->len = it.len;
        return PPCP_OK;
    }
    case PPCP_F_INT:
        if (it.type != PPCP_CBOR_UINT && it.type != PPCP_CBOR_NINT)
            return PPCP_ERR_MALFORMED;
        *(int64_t *)f->dst = it.i;
        return PPCP_OK;
    case PPCP_F_UINT:
        if (it.type != PPCP_CBOR_UINT)
            return PPCP_ERR_MALFORMED;
        *(uint64_t *)f->dst = (uint64_t)it.i;
        return PPCP_OK;
    case PPCP_F_BOOL:
        if (it.type != PPCP_CBOR_BOOL)
            return PPCP_ERR_MALFORMED;
        *(bool *)f->dst = it.b;
        return PPCP_OK;
    case PPCP_F_DOUBLE:
        /* ENC §4: an encoder emits doubles, a decoder accepts half and single
         * too — which ppcp_cbor_read has already widened.  An integer where a
         * float belongs is malformed rather than promoted, because a sigma
         * that arrived as an integer is a different claim. */
        if (it.type != PPCP_CBOR_DOUBLE)
            return PPCP_ERR_MALFORMED;
        *(double *)f->dst = it.f;
        return PPCP_OK;
    case PPCP_F_BYTES: {
        ppcp_bytes_ref *b = (ppcp_bytes_ref *)f->dst;
        if (it.type != PPCP_CBOR_BYTES)
            return PPCP_ERR_MALFORMED;
        b->p   = it.bytes;
        b->len = it.len;
        return PPCP_OK;
    }
    case PPCP_F_ENUM: {
        const ppcp_enum_map *m = (const ppcp_enum_map *)f->ctx;
        if (it.type != PPCP_CBOR_TEXT)
            return PPCP_ERR_MALFORMED;
        return ppcp_enum_from_text(m, (const char *)it.bytes, it.len, (int *)f->dst);
    }
    default:
        return PPCP_ERR_INVALID;
    }
}

ppcp_result ppcp_rec_read(ppcp_cbor_reader *r, ppcp_rfield *f, size_t n)
{
    ppcp_cbor_item it;
    ppcp_result    rc;
    uint32_t       i, pairs;

    if (r == NULL || (n > 0 && f == NULL))
        return PPCP_ERR_INVALID;

    rc = ppcp_cbor_read(r, &it);
    if (rc != PPCP_OK)
        return rc;
    if (it.type != PPCP_CBOR_MAP)
        return PPCP_ERR_MALFORMED;
    pairs = it.count;

    for (i = 0; i < pairs; i++) {
        const char *k;
        size_t      klen, j;
        bool        matched = false;

        rc = ppcp_cbor_read_key(r, &k, &klen);
        if (rc != PPCP_OK)
            return rc;

        for (j = 0; j < n; j++) {
            if (!ppcp_cbor_key_is(k, klen, f[j].key))
                continue;
            rc = read_one(r, &f[j]);
            if (rc != PPCP_OK)
                return rc;
            if (f[j].seen != NULL)
                *f[j].seen = true;
            matched = true;
            break;
        }
        if (!matched) {
            /* I13 / ENC 4b — the mechanism by which a MINOR version adds a
             * field.  Skipping is recursive over the value's whole subtree. */
            rc = ppcp_cbor_skip(r);
            if (rc != PPCP_OK)
                return rc;
        }
    }
    return PPCP_OK;
}

/* ----------------------------------------------------------------- lists */

ppcp_result ppcp_rec_read_array(ppcp_cbor_reader *r, void *base, size_t elem_size,
                                size_t cap, size_t *out_count,
                                ppcp_sub_read fn, void *ctx)
{
    ppcp_cbor_item it;
    ppcp_result    rc;
    uint32_t       i;

    if (r == NULL || out_count == NULL || fn == NULL)
        return PPCP_ERR_INVALID;

    rc = ppcp_cbor_read(r, &it);
    if (rc != PPCP_OK)
        return rc;
    if (it.type != PPCP_CBOR_ARRAY)
        return PPCP_ERR_MALFORMED;
    if (it.count > cap)
        return PPCP_ERR_LIMIT;

    for (i = 0; i < it.count; i++) {
        rc = fn(r, (uint8_t *)base + (size_t)i * elem_size, ctx);
        if (rc != PPCP_OK)
            return rc;
    }
    *out_count = it.count;
    return PPCP_OK;
}

ppcp_result ppcp_rec_read_array_arena(ppcp_cbor_reader *r, ppcp_arena *a,
                                      size_t elem_size, size_t align,
                                      void **out_base, size_t *out_count,
                                      ppcp_sub_read fn, void *ctx)
{
    ppcp_cbor_item it;
    ppcp_result    rc;
    uint32_t       i;
    void          *base;

    if (r == NULL || out_base == NULL || out_count == NULL || fn == NULL)
        return PPCP_ERR_INVALID;

    rc = ppcp_cbor_read(r, &it);
    if (rc != PPCP_OK)
        return rc;
    if (it.type != PPCP_CBOR_ARRAY)
        return PPCP_ERR_MALFORMED;

    *out_base  = NULL;
    *out_count = 0;
    if (it.count == 0)
        return PPCP_OK;

    base = ppcp_arena_take(a, it.count, elem_size, align);
    if (base == NULL)
        return PPCP_ERR_LIMIT;    /* ENC 3a: the caller's region is the limit */

    for (i = 0; i < it.count; i++) {
        rc = fn(r, (uint8_t *)base + (size_t)i * elem_size, ctx);
        if (rc != PPCP_OK)
            return rc;
    }
    *out_base  = base;
    *out_count = it.count;
    return PPCP_OK;
}

ppcp_result ppcp_rec_write_array(ppcp_cbor_writer *w, const void *base, size_t elem_size,
                                 size_t count, ppcp_elem_write fn)
{
    size_t i;
    if (w == NULL || fn == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_cbor_write_array(w, count) != PPCP_OK)
        return ppcp_cbor_writer_status(w);
    for (i = 0; i < count; i++) {
        ppcp_result rc = fn(w, (const uint8_t *)base + i * elem_size);
        if (rc != PPCP_OK)
            return rc;
    }
    return ppcp_cbor_writer_status(w);
}

/* ---------------------------------------------- shared sub-codecs (L2 types) */

ppcp_result ppcp_sub_read_instant(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    (void)ctx;
    return ppcp_instant_decode(r, (ppcp_instant *)dst);
}

ppcp_result ppcp_sub_read_interval(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    (void)ctx;
    return ppcp_interval_decode(r, (ppcp_interval *)dst);
}

ppcp_result ppcp_sub_read_estimate(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    (void)ctx;
    return ppcp_estimate_decode(r, (ppcp_estimate *)dst);
}

ppcp_result ppcp_sub_read_relation(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    (void)ctx;
    return ppcp_relation_decode(r, (ppcp_timebase_relation *)dst);
}

ppcp_result ppcp_sub_read_timebase(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    (void)ctx;
    return ppcp_timebase_decode(r, (ppcp_timebase *)dst);
}

ppcp_result ppcp_sub_write_instant(ppcp_cbor_writer *w, const void *ctx)
{
    return ppcp_instant_encode(w, (const ppcp_instant *)ctx);
}

ppcp_result ppcp_sub_write_interval(ppcp_cbor_writer *w, const void *ctx)
{
    return ppcp_interval_encode(w, (const ppcp_interval *)ctx);
}

ppcp_result ppcp_sub_write_estimate(ppcp_cbor_writer *w, const void *ctx)
{
    return ppcp_estimate_encode(w, (const ppcp_estimate *)ctx);
}

ppcp_result ppcp_elem_write_relation(ppcp_cbor_writer *w, const void *elem)
{
    return ppcp_relation_encode(w, (const ppcp_timebase_relation *)elem);
}

ppcp_result ppcp_elem_write_timebase(ppcp_cbor_writer *w, const void *elem)
{
    return ppcp_timebase_encode(w, (const ppcp_timebase *)elem);
}

ppcp_result ppcp_elem_write_interval(ppcp_cbor_writer *w, const void *elem)
{
    return ppcp_interval_encode(w, (const ppcp_interval *)elem);
}

ppcp_result ppcp_elem_write_id(ppcp_cbor_writer *w, const void *elem)
{
    const ppcp_id *id = (const ppcp_id *)elem;
    return ppcp_cbor_write_text(w, id->v, id->len);
}

ppcp_result ppcp_sub_read_id_elem(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_cbor_item it;
    ppcp_result    rc;
    (void)ctx;
    rc = ppcp_cbor_read(r, &it);
    if (rc != PPCP_OK)
        return rc;
    if (it.type != PPCP_CBOR_TEXT)
        return PPCP_ERR_MALFORMED;
    if (ppcp_id_set((ppcp_id *)dst, (const char *)it.bytes, it.len) != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}
