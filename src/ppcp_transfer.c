/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * Captures, bulk transfer and the eviction rule — CORE §5.11.1–2, §5.14;
 * MSG §8; ENC §6.  Work package L7.  See include/ppcp/transfer.h.
 */
#include "ppcp/transfer.h"
#include "ppcp_codec.h"

#include <string.h>

/* ====================================================== ENC §6 — the payload */

ppcp_result ppcp_payload_digest(const uint8_t *data, size_t len, ppcp_digest *out)
{
    uint8_t v[PPCP_SHA256_BYTES];
    if ((data == NULL && len != 0) || out == NULL)
        return PPCP_ERR_INVALID;
    /* 6e: over the payload bytes, never over the CBOR of the enclosing
     * message, so a re-encode does not change a Capture's identity. */
    ppcp_sha256_hash(data, len, v);
    return ppcp_digest_set(out, v);
}

ppcp_result ppcp_payload_chunk_count(uint64_t bytes, uint32_t chunk_bytes,
                                     uint32_t *out_count)
{
    uint64_t n;
    if (out_count == NULL || chunk_bytes == 0 || chunk_bytes > PPCP_LIMIT_CHUNK_BYTES)
        return PPCP_ERR_INVALID;   /* ENC 6f */
    n = (bytes + chunk_bytes - 1u) / chunk_bytes;
    if (n > 0xffffffffu)
        return PPCP_ERR_LIMIT;     /* `index` is a uint32 */
    *out_count = (uint32_t)n;
    return PPCP_OK;
}

ppcp_result ppcp_payload_chunk_at(const uint8_t *data, size_t len, uint32_t chunk_bytes,
                                  uint32_t index, const uint8_t **out_data,
                                  size_t *out_len, uint64_t *out_offset,
                                  ppcp_digest *out_digest)
{
    uint64_t offset;
    size_t   n;

    if (data == NULL || out_data == NULL || out_len == NULL || out_offset == NULL ||
        out_digest == NULL)
        return PPCP_ERR_INVALID;
    if (chunk_bytes == 0 || chunk_bytes > PPCP_LIMIT_CHUNK_BYTES)
        return PPCP_ERR_INVALID;
    /* ENC 6b: `offset` equals `index x chunk_bytes` for EVERY chunk.  It is
     * computed here and never carried by the caller, so the two cannot
     * disagree. */
    offset = (uint64_t)index * (uint64_t)chunk_bytes;
    if (offset >= (uint64_t)len)
        return PPCP_ERR_NOT_FOUND;
    n = (size_t)((uint64_t)len - offset);
    if (n > chunk_bytes)
        n = chunk_bytes;    /* 6b: exactly chunk_bytes except for the last */

    *out_data   = data + offset;
    *out_len    = n;
    *out_offset = offset;
    /* 6c: the chunk digest covers `data` ONLY. */
    return ppcp_payload_digest(data + offset, n, out_digest);
}

ppcp_result ppcp_payload_receiver_begin(ppcp_payload_receiver *r,
                                        const ppcp_body_payload_begin *b)
{
    if (r == NULL || b == NULL)
        return PPCP_ERR_INVALID;
    if (!b->digest.present)
        return PPCP_ERR_MALFORMED;   /* 8.1e: present by payload_begin */
    if (b->chunk_bytes == 0 || b->chunk_bytes > PPCP_LIMIT_CHUNK_BYTES)
        return PPCP_ERR_MALFORMED;   /* ENC 6f */
    memset(r, 0, sizeof(*r));
    r->capture_id  = b->capture_id;
    r->bytes       = b->bytes;
    r->chunk_bytes = b->chunk_bytes;
    r->expect      = b->digest;
    ppcp_sha256_init(&r->running);
    return PPCP_OK;
}

ppcp_result ppcp_payload_receiver_chunk(ppcp_payload_receiver *r,
                                        const ppcp_body_payload_chunk *c)
{
    ppcp_digest got;
    ppcp_result rc;

    if (r == NULL || c == NULL)
        return PPCP_ERR_INVALID;
    if (r->done)
        return PPCP_ERR_INVALID;
    if (!ppcp_id_equal(&r->capture_id, &c->capture_id))
        return PPCP_ERR_MALFORMED;
    /* 8.3a: chunks for one Capture arrive in ascending index on one bulk
     * channel.  Out of order is a desynchronised sender, not a reorder to
     * absorb: the channel guarantees ordering within itself (ENC 2b). */
    if (c->index != r->next_index)
        return PPCP_ERR_MALFORMED;
    /* ENC 6b, checked rather than assumed. */
    if (c->offset != (uint64_t)c->index * (uint64_t)r->chunk_bytes)
        return PPCP_ERR_MALFORMED;
    if (c->data == NULL || c->data_len == 0 || c->data_len > r->chunk_bytes)
        return PPCP_ERR_MALFORMED;

    /* ENC 6d: verified ON ARRIVAL, so a corrupt chunk is caught at the chunk
     * rather than at the end of a several-megabyte transfer. */
    rc = ppcp_payload_digest(c->data, c->data_len, &got);
    if (rc != PPCP_OK)
        return rc;
    if (!ppcp_digest_equal(&got, &c->digest))
        return PPCP_ERR_MALFORMED;

    ppcp_sha256_update(&r->running, c->data, c->data_len);
    r->received += c->data_len;
    r->next_index = c->index + 1u;
    r->has_acked  = true;
    r->acked_index = c->index;
    return PPCP_OK;
}

ppcp_result ppcp_payload_receiver_end(ppcp_payload_receiver *r,
                                      const ppcp_body_payload_end *e)
{
    uint8_t     v[PPCP_SHA256_BYTES];
    ppcp_digest whole;
    ppcp_result rc;

    if (r == NULL || e == NULL)
        return PPCP_ERR_INVALID;
    if (!ppcp_id_equal(&r->capture_id, &e->capture_id))
        return PPCP_ERR_MALFORMED;
    if (r->received != r->bytes)
        return PPCP_ERR_MALFORMED;
    ppcp_sha256_final(&r->running, v);
    rc = ppcp_digest_set(&whole, v);
    if (rc != PPCP_OK)
        return rc;
    /* 8.3b: `payload_begin.digest` and `payload_end.digest` cover the whole
     * payload and MUST be equal, so both are checked against what arrived. */
    if (!ppcp_digest_equal(&whole, &r->expect))
        return PPCP_ERR_MALFORMED;
    if (!ppcp_digest_equal(&whole, &e->digest))
        return PPCP_ERR_MALFORMED;
    r->done = true;
    return PPCP_OK;
}

uint32_t ppcp_payload_receiver_resume_index(const ppcp_payload_receiver *r)
{
    if (r == NULL || !r->has_acked)
        return 0;
    /* 8.3d: from the chunk AFTER the last acknowledged index, not from the
     * beginning.  A resume that restarted at 0 would re-send the megabytes
     * that already arrived, which is the failure the field exists to avoid. */
    return r->acked_index + 1u;
}

/* =============================================== CORE 5.14 — the owner's view */

void ppcp_transfer_table_init(ppcp_transfer_table *t)
{
    if (t != NULL)
        memset(t, 0, sizeof(*t));
}

size_t ppcp_transfer_table_count(const ppcp_transfer_table *t)
{
    return (t == NULL) ? 0 : t->count;
}

static ppcp_transfer_entry *find_mut(ppcp_transfer_table *t, const ppcp_id *id)
{
    size_t i;
    if (t == NULL || id == NULL)
        return NULL;
    for (i = 0; i < t->count; i++)
        if (ppcp_id_equal(&t->entries[i].capture_id, id))
            return &t->entries[i];
    return NULL;
}

const ppcp_transfer_entry *ppcp_transfer_find(const ppcp_transfer_table *t,
                                              const ppcp_id *capture_id)
{
    return find_mut((ppcp_transfer_table *)t, capture_id);
}

ppcp_result ppcp_transfer_observe_announce(ppcp_transfer_table *t, const ppcp_capture *c,
                                           bool is_preview)
{
    ppcp_transfer_entry *e;
    ppcp_result          rc;

    if (t == NULL || c == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_capture_validate(c);
    if (rc != PPCP_OK)
        return rc;
    /* MSG 8.1i / CORE 5.11j — preview is LIVE-ONLY.  A consumer never sees
     * `transfer: pending` on a preview Capture, because the default state of
     * an announced Capture is `pending` and a queue told nothing else fills
     * with the cheapest data in the session and then writes it to a bundle. */
    /* An `absent` preview segment is exempt, and has to be: it is exactly what
     * 5.11c3 asks a peer to announce for what it discarded, it holds no
     * payload, and `pending` is the default state of every Capture.  Requiring
     * a transfer state for a payload that does not exist would be inventing an
     * obligation to satisfy a rule about queues. */
    if (is_preview && c->completeness != PPCP_ABSENT &&
        c->transfer == PPCP_TRANSFER_PENDING)
        return PPCP_ERR_INVALID;

    e = find_mut(t, &c->id);
    if (e == NULL) {
        if (t->count == PPCP_TRANSFER_MAX)
            return PPCP_ERR_LIMIT;
        e = &t->entries[t->count++];
        memset(e, 0, sizeof(*e));
        e->capture_id = c->id;
    }
    e->stream_id    = c->stream_id;
    e->anchor       = c->anchor;
    e->completeness = c->completeness;
    e->transfer     = c->transfer;
    e->digest       = c->digest;
    e->has_bytes    = c->has_bytes;
    e->bytes        = c->bytes;
    e->preview      = is_preview;
    /* 5.11j is also an eviction exit (5.14g, case 4): a preview segment was
     * never going to be sent, so no receiver will ever confirm it. */
    if (is_preview)
        e->shed_permitted = true;
    return PPCP_OK;
}

ppcp_result ppcp_transfer_set(ppcp_transfer_table *t, const ppcp_id *capture_id,
                              ppcp_transfer_state state)
{
    ppcp_transfer_entry *e = find_mut(t, capture_id);
    if (e == NULL)
        return PPCP_ERR_NOT_FOUND;
    /* 8.4b / 5.14f — an owner MUST NOT set `confirmed` on its own authority.
     * Only the receiver can say it, which is the whole reason
     * `capture_committed` exists, and this refusal is where that lives. */
    if (state == PPCP_TRANSFER_CONFIRMED)
        return PPCP_ERR_INVALID;
    if (e->preview && state == PPCP_TRANSFER_PENDING)
        return PPCP_ERR_INVALID;   /* 8.1i again, on the update path */
    e->transfer = state;
    return PPCP_OK;
}

ppcp_result ppcp_transfer_on_acked(ppcp_transfer_table *t, const ppcp_id *capture_id,
                                   uint32_t index)
{
    ppcp_transfer_entry *e = find_mut(t, capture_id);
    if (e == NULL)
        return PPCP_ERR_NOT_FOUND;
    if (e->has_acked_index && index < e->acked_index)
        return PPCP_ERR_MALFORMED;   /* 8.3a: ascending */
    e->has_acked_index = true;
    e->acked_index     = index;
    if (e->transfer == PPCP_TRANSFER_PENDING)
        e->transfer = PPCP_TRANSFER_IN_FLIGHT;
    return PPCP_OK;
}

ppcp_result ppcp_transfer_on_committed(ppcp_transfer_table *t,
                                       const ppcp_body_capture_committed *m)
{
    ppcp_transfer_entry *e;
    if (t == NULL || m == NULL)
        return PPCP_ERR_INVALID;
    e = find_mut(t, &m->capture_id);
    if (e == NULL)
        return PPCP_ERR_NOT_FOUND;
    /* The digest is checked as CONTENT, not used as a key (I34): a commit
     * naming a payload this owner does not recognise confirms nothing, and
     * quietly accepting it would release storage for data the receiver does
     * not actually hold. */
    if (e->digest.present && m->digest.present &&
        !ppcp_digest_equal(&e->digest, &m->digest))
        return PPCP_ERR_MALFORMED;
    e->transfer = PPCP_TRANSFER_CONFIRMED;
    return PPCP_OK;
}

ppcp_result ppcp_transfer_on_already_present(ppcp_transfer_table *t,
                                             const ppcp_id *capture_id)
{
    ppcp_transfer_entry *e = find_mut(t, capture_id);
    if (e == NULL)
        return PPCP_ERR_NOT_FOUND;
    /* 8.3c / 5.14g exit 3: the receiver demonstrably holds the payload
     * durably, and that answer is equivalent to a commit for eviction — but
     * NOT for `transfer`, which stays the owner's honest record of what it
     * sent.  `present` is what it is: sent, and held at the far end. */
    e->already_present = true;
    e->transfer        = PPCP_TRANSFER_PRESENT;
    return PPCP_OK;
}

ppcp_result ppcp_transfer_mark_shed(ppcp_transfer_table *t, const ppcp_id *capture_id)
{
    ppcp_transfer_entry *e = find_mut(t, capture_id);
    if (e == NULL)
        return PPCP_ERR_NOT_FOUND;
    /* 5.14g1 — SHOT-ANCHORED PAYLOAD IS NEVER SHEDDABLE BY POLICY.  Every one
     * of 5.14g's exits is a case where the protocol itself says no receiver
     * will ever confirm the payload; a policy exit would be exactly the
     * licence I38 exists to refuse.  This is the function a retention policy
     * under storage pressure would reach for, and it says no. */
    if (e->anchor.kind == PPCP_ANCHOR_SHOT)
        return PPCP_ERR_INVALID;
    e->shed_permitted = true;
    return PPCP_OK;
}

bool ppcp_capture_is_evictable(const ppcp_capture *c)
{
    if (c == NULL)
        return false;
    /* Exit 1: the receiver asserted it holds the payload durably. */
    if (c->transfer == PPCP_TRANSFER_CONFIRMED)
        return true;
    /* Exit 2: there is no payload to evict, and no digest for
     * `capture_committed` to name. */
    if (c->completeness == PPCP_ABSENT)
        return true;
    /* Exits 3 and 4 are facts about the conversation and are not visible from
     * the entity.  Answering "no" here is the safe half of the ambiguity, and
     * ppcp_transfer_is_evictable() is the one that can answer properly. */
    return false;
}

bool ppcp_transfer_is_evictable(const ppcp_transfer_table *t, const ppcp_id *capture_id)
{
    const ppcp_transfer_entry *e = ppcp_transfer_find(t, capture_id);
    if (e == NULL)
        return false;
    if (e->transfer == PPCP_TRANSFER_CONFIRMED)   /* exit 1 */
        return true;
    if (e->completeness == PPCP_ABSENT)           /* exit 2 */
        return true;
    if (e->already_present)                       /* exit 3 */
        return true;
    if (e->shed_permitted)                        /* exit 4 */
        return true;
    /* 5.14g / I38: everything else holds payload no receiver has confirmed,
     * and a peer under storage pressure refuses to arm rather than dropping
     * swings a consumer has not received. */
    return false;
}

/* ======================================= I36 — stream-anchored coverage */

ppcp_result ppcp_coverage_init(ppcp_coverage *cov, const ppcp_stream *s)
{
    ppcp_result rc;
    if (cov == NULL || s == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_stream_validate(s);
    if (rc != PPCP_OK)
        return rc;
    /* 5.11b: a stream-anchored Capture belongs to a `continuous` Stream, and
     * accounting is meaningless for a `shot_windowed` one — absence between
     * shots there is correct and expected. */
    if (s->continuity != PPCP_CONTINUOUS)
        return PPCP_ERR_INVALID;
    memset(cov, 0, sizeof(*cov));
    cov->stream_id = s->id;
    cov->tb        = s->timebase_id;
    cov->opened_ns = s->opened_at.ns;
    if (s->has_closed_at) {
        cov->has_closed = true;
        cov->closed_ns  = s->closed_at.ns;
    }
    return PPCP_OK;
}

ppcp_result ppcp_coverage_close(ppcp_coverage *cov, int64_t closed_ns)
{
    if (cov == NULL)
        return PPCP_ERR_INVALID;
    if (closed_ns < cov->opened_ns)
        return PPCP_ERR_INVALID;
    cov->has_closed = true;
    cov->closed_ns  = closed_ns;
    return PPCP_OK;
}

ppcp_result ppcp_coverage_add(ppcp_coverage *cov, const ppcp_capture *c)
{
    size_t      i, pos;
    ppcp_result rc;

    if (cov == NULL || c == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_capture_validate(c);
    if (rc != PPCP_OK)
        return rc;
    if (c->anchor.kind != PPCP_ANCHOR_STREAM)
        return PPCP_ERR_INVALID;
    if (!ppcp_id_equal(&c->stream_id, &cov->stream_id))
        return PPCP_ERR_INVALID;
    /* 5.14d: mandatory on a segment, absent or not — for a segment the
     * interval IS the claim.  ppcp_capture_validate already refused the
     * shape; this is the reminder of why. */
    if (!c->has_interval)
        return PPCP_ERR_INVALID;
    if (!ppcp_id_equal(&c->interval.tb, &cov->tb))
        return PPCP_ERR_INVALID;
    if (cov->segment_count == PPCP_COVERAGE_MAX)
        return PPCP_ERR_LIMIT;

    /* 5.14e: segments abut or leave a declared gap; they do not OVERLAP,
     * because two accounts of one interval are two answers to one question. */
    for (i = 0; i < cov->segment_count; i++) {
        if (c->interval.start_ns < cov->segments[i].end_ns &&
            cov->segments[i].start_ns < c->interval.end_ns)
            return PPCP_ERR_MALFORMED;
    }
    /* Kept sorted, so the check below is one pass and the first hole it finds
     * is the earliest one. */
    pos = cov->segment_count;
    for (i = 0; i < cov->segment_count; i++) {
        if (c->interval.start_ns < cov->segments[i].start_ns) {
            pos = i;
            break;
        }
    }
    for (i = cov->segment_count; i > pos; i--)
        cov->segments[i] = cov->segments[i - 1];
    cov->segments[pos] = c->interval;
    cov->segment_count++;
    return PPCP_OK;
}

ppcp_result ppcp_coverage_check(const ppcp_coverage *cov, int64_t up_to_ns,
                                ppcp_completeness session_completeness,
                                ppcp_interval *out_hole)
{
    int64_t cursor;
    size_t  i;
    int64_t end;

    if (cov == NULL)
        return PPCP_ERR_INVALID;
    end = up_to_ns;
    if (cov->has_closed && cov->closed_ns < end)
        end = cov->closed_ns;

    cursor = cov->opened_ns;
    for (i = 0; i < cov->segment_count; i++) {
        if (cov->segments[i].start_ns > cursor) {
            /* 5.11c1: a hole BETWEEN announced segments is a defect in either
             * kind of Session.  Nothing truncates a bundle in the middle. */
            if (out_hole != NULL) {
                *out_hole = cov->segments[i];
                out_hole->start_ns = cursor;
                out_hole->end_ns   = cov->segments[i].start_ns;
                out_hole->tb       = cov->tb;
            }
            return PPCP_ERR_MALFORMED;
        }
        if (cov->segments[i].end_ns > cursor)
            cursor = cov->segments[i].end_ns;
    }
    if (cursor < end) {
        /* 5.11c1: time AFTER the last announced Capture is the incompleteness
         * a `partial` or `unknown` Session already declares — CT-I36 (c) — and
         * a defect only where the Session was asserted `complete` — (d). */
        if (session_completeness != PPCP_COMPLETE)
            return PPCP_OK;
        if (out_hole != NULL) {
            out_hole->tb       = cov->tb;
            out_hole->start_ns = cursor;
            out_hole->end_ns   = end;
        }
        return PPCP_ERR_MALFORMED;
    }
    return PPCP_OK;
}
