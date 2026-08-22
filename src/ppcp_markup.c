/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * Annotations — CORE §5.18; MSG §9.0.  Work package L11.  See
 * include/ppcp/markup.h for the contract.
 *
 * Every function in this file either compares two annotations, checks where
 * one is anchored, copies one's bytes, or puts one on a wire.  None of them
 * reads `body`, and none of them produces a value another part of the model
 * could consume.  That is I37, and it is easier to keep by having nothing
 * here to reach for than by remembering not to.
 */
#include "ppcp/markup.h"

#include <string.h>

/* ============================================================ 5.18e — the order */

static int annot_id_cmp(const ppcp_id *a, const ppcp_id *b)
{
    size_t n = (a->len < b->len) ? a->len : b->len;
    int    r = (n == 0) ? 0 : memcmp(a->v, b->v, n);
    if (r != 0)
        return r;
    if (a->len == b->len)
        return 0;
    return (a->len < b->len) ? -1 : 1;
}

int ppcp_annotation_supersedes(const ppcp_annotation *a, const ppcp_annotation *b)
{
    int author;

    if (a == NULL || b == NULL)
        return 0;
    /* Different annotations are not in each other's order at all.  Answering
     * anything else here would let a caller supersede one annotation with a
     * completely different one, which is not a tiebreak but a deletion. */
    if (!ppcp_id_equal(&a->id, &b->id))
        return 0;

    if (a->revision > b->revision)
        return 1;
    if (a->revision < b->revision)
        return -1;

    /* Equal revisions.  The comparison is BYTEWISE on `author_peer_id`, which
     * is mandatory on every Annotation, so the order is total and identical at
     * both ends and two peers editing concurrently converge without either
     * needing to know who acted first. */
    author = annot_id_cmp(&a->author_peer_id, &b->author_peer_id);
    if (author > 0)
        return 1;
    if (author < 0)
        return -1;
    return 0;
}

/* ================================================== 5.18g / 5.18j — placement */

ppcp_result ppcp_annotation_validate_placement(const ppcp_annotation *a,
                                               const ppcp_id *timebase_ref,
                                               const ppcp_stream *stream)
{
    ppcp_kind_view view;
    ppcp_result    rc;

    if (a == NULL || timebase_ref == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_annotation_validate(a);
    if (rc != PPCP_OK)
        return rc;

    view = ppcp_annotation_kind_view(a->kind.v, a->kind.len);

    /* 5.18j — presence of `stream_id` FOLLOWS `kind`, which is on the wire and
     * therefore statically checkable.  The rule it replaced turned on whether
     * `body` was "interpreted in image coordinates", and `body` is opaque, so
     * no peer and no test could decide it. */
    if (view == PPCP_KIND_VIEW_SPECIFIC && !a->has_stream_id)
        return PPCP_ERR_INVALID;
    if (view == PPCP_KIND_NOT_VIEW_SPECIFIC && a->has_stream_id)
        return PPCP_ERR_INVALID;
    /* An unrecognised `kind` is view-specific if and only if `stream_id` is
     * present — the conservative default, since a consumer then renders it
     * only on the Stream named, or not at all. */

    if (a->has_stream_id) {
        if (stream == NULL)
            return PPCP_ERR_NOT_FOUND;      /* a dangling reference, not a placement */
        if (!ppcp_id_equal(&stream->id, &a->stream_id))
            return PPCP_ERR_INVALID;
        /* 5.18g — `at` is in THAT Stream's timebase.  Converting through a
         * relation instead can land a drawn line on the neighbouring frame,
         * which is why the anchor is exact rather than converted. */
        if (!ppcp_id_equal(&a->at.tb, &stream->timebase_id))
            return PPCP_ERR_INVALID;
        return PPCP_OK;
    }

    /* 5.18g — not view-specific, so `at` is in `Session.timebase_ref`. */
    if (!ppcp_id_equal(&a->at.tb, timebase_ref))
        return PPCP_ERR_INVALID;
    return PPCP_OK;
}

ppcp_result ppcp_peer_annotate(ppcp_peer *p, const ppcp_annotation *a)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || a == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_annotation_validate(a);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_msg_init(&m, PPCP_MT_ANNOTATION, 1);
    if (rc != PPCP_OK)
        return rc;
    m.body.annotation.annotation = *a;
    /* 9.0e — on the CONTROL channel, where I30 already keeps far smaller
     * things off, which is the reason for the 8 KiB cap rather than a
     * consequence of it.  ppcp_peer_send() checks the channel from the
     * catalogue, so that is not restated here. */
    return ppcp_peer_send(p, PPCP_CHANNEL_CONTROL, &m);
}

/* ================================================================= the store */

typedef struct annot_slot {
    bool            in_use;
    ppcp_annotation a;
    uint8_t         body[PPCP_ANNOTATION_BODY_MAX];
} annot_slot;

struct ppcp_annotation_store {
    annot_slot slot[PPCP_ANNOTATION_STORE_MAX];
    size_t     count;
};

size_t ppcp_annotation_store_sizeof(void) { return sizeof(struct ppcp_annotation_store); }

ppcp_result ppcp_annotation_store_new(void *storage, size_t storage_len,
                                      ppcp_annotation_store **out)
{
    ppcp_annotation_store *s;

    if (storage == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    if (storage_len < sizeof(*s))
        return PPCP_ERR_NOSPACE;
    s = (ppcp_annotation_store *)storage;
    memset(s, 0, sizeof(*s));
    *out = s;
    return PPCP_OK;
}

/* 5.18a — `body` is copied in and returned unchanged.  Round-tripping IS the
 * requirement; interpreting the format is explicitly not one, and a store that
 * normalised or re-encoded would break the only promise the type makes. */
static ppcp_result annot_store_write(annot_slot *sl, const ppcp_annotation *a)
{
    if (a->body_len > PPCP_ANNOTATION_BODY_MAX)
        return PPCP_ERR_MALFORMED;      /* 5.18f */
    sl->a = *a;
    if (a->body_len > 0 && a->body != NULL)
        memcpy(sl->body, a->body, a->body_len);
    sl->a.body     = sl->body;
    sl->a.body_len = a->body_len;
    sl->in_use     = true;
    return PPCP_OK;
}

ppcp_result ppcp_annotation_store_observe(ppcp_annotation_store *s, const ppcp_annotation *a,
                                          bool *out_replaced)
{
    size_t i, free_slot = PPCP_ANNOTATION_STORE_MAX;

    if (s == NULL || a == NULL)
        return PPCP_ERR_INVALID;
    if (out_replaced != NULL)
        *out_replaced = false;

    for (i = 0; i < PPCP_ANNOTATION_STORE_MAX; i++) {
        if (!s->slot[i].in_use) {
            if (free_slot == PPCP_ANNOTATION_STORE_MAX)
                free_slot = i;
            continue;
        }
        if (!ppcp_id_equal(&s->slot[i].a.id, &a->id))
            continue;
        /* 9.0c — higher replaces, lower is IGNORED, equal replaces if and only
         * if the incoming author sorts higher bytewise.  There is no merge. */
        if (ppcp_annotation_supersedes(a, &s->slot[i].a) > 0) {
            ppcp_result rc = annot_store_write(&s->slot[i], a);
            if (rc != PPCP_OK)
                return rc;
            if (out_replaced != NULL)
                *out_replaced = true;
        }
        return PPCP_OK;
    }

    if (free_slot == PPCP_ANNOTATION_STORE_MAX)
        return PPCP_ERR_LIMIT;
    {
        ppcp_result rc = annot_store_write(&s->slot[free_slot], a);
        if (rc != PPCP_OK)
            return rc;
    }
    s->count++;
    if (out_replaced != NULL)
        *out_replaced = true;
    return PPCP_OK;
}

size_t ppcp_annotation_store_count(const ppcp_annotation_store *s)
{
    return (s == NULL) ? 0 : s->count;
}

const ppcp_annotation *ppcp_annotation_store_at(const ppcp_annotation_store *s, size_t index)
{
    size_t i, n = 0;
    if (s == NULL)
        return NULL;
    for (i = 0; i < PPCP_ANNOTATION_STORE_MAX; i++) {
        if (!s->slot[i].in_use)
            continue;
        if (n == index)
            return &s->slot[i].a;
        n++;
    }
    return NULL;
}

const ppcp_annotation *ppcp_annotation_store_find(const ppcp_annotation_store *s,
                                                  const ppcp_id *id)
{
    size_t i;
    if (s == NULL || id == NULL)
        return NULL;
    for (i = 0; i < PPCP_ANNOTATION_STORE_MAX; i++)
        if (s->slot[i].in_use && ppcp_id_equal(&s->slot[i].a.id, id))
            return &s->slot[i].a;
    return NULL;
}
