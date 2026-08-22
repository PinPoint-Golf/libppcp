/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 */
#include "ppcp/common.h"
#include "ppcp/version.h"

#include <string.h>

const char *ppcp_result_str(ppcp_result r)
{
    switch (r) {
    case PPCP_OK:                  return "ok";
    case PPCP_ERR_MALFORMED:       return "malformed";
    case PPCP_ERR_LIMIT:           return "limit exceeded";
    case PPCP_ERR_FATAL_LIMIT:     return "frame length beyond channel limit (fatal)";
    case PPCP_ERR_TRUNCATED:       return "truncated";
    case PPCP_ERR_NOSPACE:         return "output buffer too small";
    case PPCP_ERR_INVALID:         return "invalid argument";
    case PPCP_ERR_NOT_FOUND:       return "not found";
    case PPCP_ERR_VERSION_NEWER:   return "a newer version of the application is required";
    case PPCP_ERR_EXPIRED:         return "expired";
    case PPCP_ERR_UNIMPLEMENTED:   return "not yet implemented";
    }
    return "unknown";
}

const char *ppcp_library_version(void) { return PPCP_LIB_VERSION_STRING; }
const char *ppcp_wire_version(void)    { return PPCP_WIRE_VERSION; }

ppcp_result ppcp_id_set(ppcp_id *id, const char *s, size_t len)
{
    if (id == NULL)
        return PPCP_ERR_INVALID;
    /* CORE 5.1: 1..64 bytes.  An empty Id is refused here rather than at the
     * encoder, because it is the empty `tb` that I1 exists to make
     * unwriteable and the earliest refusal is the one an implementer sees. */
    if (s == NULL || len == 0 || len > PPCP_ID_MAX) {
        memset(id, 0, sizeof(*id));
        return PPCP_ERR_INVALID;
    }
    memcpy(id->v, s, len);
    id->v[len] = '\0';
    id->len = (uint8_t)len;
    return PPCP_OK;
}

ppcp_result ppcp_id_set_z(ppcp_id *id, const char *s)
{
    return ppcp_id_set(id, s, s ? strlen(s) : 0);
}

bool ppcp_id_is_set(const ppcp_id *id)
{
    return id != NULL && id->len > 0 && id->len <= PPCP_ID_MAX;
}

bool ppcp_id_equal(const ppcp_id *a, const ppcp_id *b)
{
    if (a == NULL || b == NULL)
        return false;
    if (a->len != b->len)
        return false;
    return memcmp(a->v, b->v, a->len) == 0;
}

bool ppcp_ct_equal(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    unsigned char diff = 0;
    size_t i;

    if (a == NULL || b == NULL)
        return false;
    /* No early exit: RV 5.3d asks that a wrong key and an unresolvable
     * identity be indistinguishable in timing as well as in content. */
    for (i = 0; i < n; i++)
        diff = (unsigned char)(diff | (x[i] ^ y[i]));
    return diff == 0;
}

/* ------------------------------------------------------------------ arena */

void ppcp_arena_init(ppcp_arena *a, void *buf, size_t cap)
{
    if (a == NULL)
        return;
    a->buf  = (uint8_t *)buf;
    a->cap  = (buf == NULL) ? 0 : cap;
    a->used = 0;
}

void ppcp_arena_reset(ppcp_arena *a)
{
    if (a != NULL)
        a->used = 0;
}

size_t ppcp_arena_used(const ppcp_arena *a)
{
    return (a == NULL) ? 0 : a->used;
}

void *ppcp_arena_take(ppcp_arena *a, size_t count, size_t elem_size, size_t align)
{
    size_t off, need;
    uint8_t *p;

    if (a == NULL || a->buf == NULL || align == 0)
        return NULL;
    if (count != 0 && elem_size > (size_t)-1 / count)
        return NULL;                       /* overflow */
    need = count * elem_size;

    /* Aligned on the ABSOLUTE address, not on the offset within the region.
     * The caller owns the buffer and may hand in any address it likes — a
     * member of a struct, a slice of a larger pool — and aligning the offset
     * alone leaves every allocation as misaligned as the base was.  Found by
     * the L5 catalogue test under UBSan decoding a `declare` into an arena
     * whose buffer began at an odd offset inside the test's own struct. */
    off = a->used;
    {
        size_t misalign = (size_t)((uintptr_t)(a->buf + off) % align);
        if (misalign != 0) {
            size_t pad = align - misalign;
            if (pad > a->cap - off)
                return NULL;
            off += pad;
        }
    }
    if (need > a->cap - off)
        return NULL;

    p = a->buf + off;
    a->used = off + need;
    /* Zeroed, so an aggregate whose optional members were never written reads
     * as absent rather than as whatever the caller's buffer held before. */
    { size_t i; for (i = 0; i < need; i++) p[i] = 0; }
    return p;
}
