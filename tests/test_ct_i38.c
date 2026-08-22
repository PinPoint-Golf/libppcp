/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * Captures, bulk transfer and the eviction rule — work package L7's evidence.
 *
 * Rows this file carries:
 *
 *   CT-I38   each of 5.14g's four exits INDEPENDENTLY, and then the refusals:
 *            an owner that never received `capture_committed` neither evicts
 *            nor sets `confirmed` itself, and its own retention policy does
 *            not make shot-anchored payload evictable (5.14g1)
 *   CT-I30   `achieved_frames` rides `payload_begin`, and `capture_update`
 *            carries it ONLY for a Capture whose transfer is `failed`
 *   CT-I36   the coverage rule: (a) a missing middle segment is a defect in
 *            any Session, (b) an `absent` segment with an interval and a
 *            reason SATISFIES coverage, (c) a truncated tail in a `partial`
 *            Session is the declared incompleteness, (d) the same tail in a
 *            `complete` Session is a defect
 *   CT-I36a  preview is live-only: never announced `transfer: pending`, and
 *            never written to a bundle
 *   ENC §6   chunking, offsets, per-chunk and whole-payload SHA-256, and
 *            resumption from the chunk AFTER the last acknowledged index
 */
#include "ppcp/transfer.h"
#include "ppcp/bundle.h"

#include "test_util.h"

static ppcp_instant inst(const char *tb, int64_t ns)
{
    ppcp_instant i;
    memset(&i, 0, sizeof(i));
    if (ppcp_instant_make_z(&i, tb, ns) != PPCP_OK) abort();
    return i;
}

static ppcp_interval ivl(const char *tb, int64_t a, int64_t b)
{
    ppcp_interval v;
    memset(&v, 0, sizeof(v));
    if (ppcp_interval_make(&v, tb, strlen(tb), a, b) != PPCP_OK) abort();
    return v;
}

static ppcp_id id_of(const char *s)
{
    ppcp_id i;
    if (ppcp_id_set_z(&i, s) != PPCP_OK) abort();
    return i;
}

/* ============================================================ ENC §6 */

static void test_payload_codec(void)
{
    static uint8_t payload[700];
    ppcp_digest    whole, chunk_d;
    uint32_t       count = 0, i;
    size_t         n;

    for (i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)(i * 7u + 3u);

    TEST("ENC 6b — offset is index x chunk_bytes for EVERY chunk, and only the "
         "last chunk is short");
    CHECK_EQ_I(ppcp_payload_chunk_count(sizeof(payload), 256, &count), PPCP_OK);
    CHECK_EQ_I(count, 3);
    for (i = 0; i < count; i++) {
        const uint8_t *p = NULL;
        uint64_t       off = 0;
        CHECK_EQ_I(ppcp_payload_chunk_at(payload, sizeof(payload), 256, i, &p, &n, &off,
                                         &chunk_d), PPCP_OK);
        CHECK_EQ_I(off, (uint64_t)i * 256u);
        CHECK_EQ_I(n, (i == count - 1u) ? (sizeof(payload) - 512u) : 256u);
        CHECK(p == payload + off);
        CHECK(chunk_d.present);
    }
    CHECK_EQ_I(ppcp_payload_chunk_at(payload, sizeof(payload), 256, count, NULL, NULL,
                                     NULL, NULL), PPCP_ERR_INVALID);

    TEST("ENC 6c/6d — the receiver verifies each chunk on arrival and the whole "
         "payload at payload_end");
    CHECK_EQ_I(ppcp_payload_digest(payload, sizeof(payload), &whole), PPCP_OK);
    {
        ppcp_payload_receiver     r;
        ppcp_body_payload_begin   b;
        ppcp_body_payload_end     e;
        memset(&b, 0, sizeof(b));
        b.capture_id  = id_of("cap:1");
        b.bytes       = sizeof(payload);
        b.digest      = whole;
        b.chunk_bytes = 256;
        CHECK_EQ_I(ppcp_payload_receiver_begin(&r, &b), PPCP_OK);
        CHECK_EQ_I(ppcp_payload_receiver_resume_index(&r), 0);

        for (i = 0; i < count; i++) {
            ppcp_body_payload_chunk c;
            const uint8_t          *p = NULL;
            uint64_t                off = 0;
            memset(&c, 0, sizeof(c));
            CHECK_EQ_I(ppcp_payload_chunk_at(payload, sizeof(payload), 256, i, &p, &n,
                                             &off, &c.digest), PPCP_OK);
            c.capture_id = b.capture_id;
            c.index      = i;
            c.offset     = off;
            c.data       = p;
            c.data_len   = n;
            CHECK_EQ_I(ppcp_payload_receiver_chunk(&r, &c), PPCP_OK);
            /* 8.3d: the index a `payload_resume` would name is the one AFTER
             * the last acknowledged, never the beginning. */
            CHECK_EQ_I(ppcp_payload_receiver_resume_index(&r), i + 1u);
        }
        memset(&e, 0, sizeof(e));
        e.capture_id = b.capture_id;
        e.digest     = whole;
        CHECK_EQ_I(ppcp_payload_receiver_end(&r, &e), PPCP_OK);
    }

    TEST("ENC 6d — a corrupt chunk is caught AT THE CHUNK, not at the end");
    {
        ppcp_payload_receiver   r;
        ppcp_body_payload_begin b;
        ppcp_body_payload_chunk c;
        const uint8_t          *p = NULL;
        uint64_t                off = 0;
        memset(&b, 0, sizeof(b));
        b.capture_id  = id_of("cap:1");
        b.bytes       = sizeof(payload);
        b.digest      = whole;
        b.chunk_bytes = 256;
        CHECK_EQ_I(ppcp_payload_receiver_begin(&r, &b), PPCP_OK);
        memset(&c, 0, sizeof(c));
        CHECK_EQ_I(ppcp_payload_chunk_at(payload, sizeof(payload), 256, 0, &p, &n, &off,
                                         &c.digest), PPCP_OK);
        c.capture_id = b.capture_id;
        c.index      = 0;
        c.offset     = off;
        c.data       = p + 1;      /* the digest now describes other bytes */
        c.data_len   = n - 1;
        CHECK_EQ_I(ppcp_payload_receiver_chunk(&r, &c), PPCP_ERR_MALFORMED);

        TEST("ENC 6b — an offset that disagrees with the index is malformed");
        c.data     = p;
        c.data_len = n;
        c.offset   = 1;
        CHECK_EQ_I(ppcp_payload_receiver_chunk(&r, &c), PPCP_ERR_MALFORMED);

        TEST("8.3a — chunks arrive in ascending index; out of order is malformed");
        c.offset = off;
        c.index  = 2;
        CHECK_EQ_I(ppcp_payload_receiver_chunk(&r, &c), PPCP_ERR_MALFORMED);
    }

    TEST("8.1e — payload_begin without a digest is refused before a byte moves");
    {
        ppcp_payload_receiver   r;
        ppcp_body_payload_begin b;
        memset(&b, 0, sizeof(b));
        b.capture_id  = id_of("cap:1");
        b.bytes       = 10;
        b.chunk_bytes = 256;
        CHECK_EQ_I(ppcp_payload_receiver_begin(&r, &b), PPCP_ERR_MALFORMED);
        b.digest      = whole;
        b.chunk_bytes = 0;                       /* ENC 6f */
        CHECK_EQ_I(ppcp_payload_receiver_begin(&r, &b), PPCP_ERR_MALFORMED);
        b.chunk_bytes = PPCP_LIMIT_CHUNK_BYTES + 1u;
        CHECK_EQ_I(ppcp_payload_receiver_begin(&r, &b), PPCP_ERR_MALFORMED);
    }
}

/* ============================================================== CT-I38 */

static void add(ppcp_transfer_table *t, const char *id, ppcp_anchor_kind kind,
                ppcp_completeness comp, bool preview)
{
    ppcp_capture c;
    ppcp_digest  d;
    uint8_t      v[PPCP_SHA256_BYTES];

    if (kind == PPCP_ANCHOR_SHOT) {
        if (ppcp_capture_make_shot(&c, id, "shot:1", "st:1", comp) != PPCP_OK) abort();
    } else if (kind == PPCP_ANCHOR_CANDIDATE) {
        if (ppcp_capture_make_candidate(&c, id, "cand:1", "st:aud", comp) != PPCP_OK)
            abort();
    } else {
        ppcp_interval iv = ivl("tb:dev", 0, 1000);
        if (ppcp_capture_make_segment(&c, id, preview ? "st:prev" : "st:meta", comp, &iv)
            != PPCP_OK) abort();
    }
    if (comp == PPCP_ABSENT) {
        if (ppcp_capture_set_absent_reason(&c, PPCP_ABSENT_NOT_RETAINED) != PPCP_OK)
            abort();
    } else {
        memset(v, 0x5a, sizeof(v));
        if (ppcp_digest_set(&d, v) != PPCP_OK) abort();
        if (ppcp_capture_set_digest(&c, &d, 700) != PPCP_OK) abort();
    }
    /* 8.1i: a preview Capture is never announced `pending`, so anything the
     * test announces as preview is already past that state. */
    if (preview && comp != PPCP_ABSENT) {
        if (ppcp_capture_set_transfer(&c, PPCP_TRANSFER_PRESENT) != PPCP_OK) abort();
    }
    if (ppcp_transfer_observe_announce(t, &c, preview) != PPCP_OK) abort();
}

static void test_eviction(void)
{
    ppcp_transfer_table t;
    ppcp_id             id;
    ppcp_body_capture_committed cm;
    ppcp_digest         d;
    uint8_t             v[PPCP_SHA256_BYTES];

    memset(v, 0x5a, sizeof(v));
    CHECK_EQ_I(ppcp_digest_set(&d, v), PPCP_OK);
    ppcp_transfer_table_init(&t);

    TEST("5.14g exit 1 — a `confirmed` Capture is evictable, and `confirmed` "
         "arrives, it is not set");
    add(&t, "cap:shot", PPCP_ANCHOR_SHOT, PPCP_COMPLETE, false);
    id = id_of("cap:shot");
    CHECK(!ppcp_transfer_is_evictable(&t, &id));
    /* 8.4b: the owner cannot say it, and this is the call it would have made */
    CHECK_EQ_I(ppcp_transfer_set(&t, &id, PPCP_TRANSFER_CONFIRMED), PPCP_ERR_INVALID);
    CHECK(!ppcp_transfer_is_evictable(&t, &id));
    memset(&cm, 0, sizeof(cm));
    cm.capture_id = id;
    cm.digest     = d;
    CHECK_EQ_I(ppcp_transfer_on_committed(&t, &cm), PPCP_OK);
    CHECK_EQ_I(ppcp_transfer_find(&t, &id)->transfer, PPCP_TRANSFER_CONFIRMED);
    CHECK(ppcp_transfer_is_evictable(&t, &id));

    TEST("... and a commit naming a payload the owner does not recognise "
         "confirms nothing");
    {
        ppcp_transfer_table t2;
        ppcp_digest         other;
        uint8_t             w[PPCP_SHA256_BYTES];
        ppcp_transfer_table_init(&t2);
        add(&t2, "cap:shot", PPCP_ANCHOR_SHOT, PPCP_COMPLETE, false);
        memset(w, 0x11, sizeof(w));
        CHECK_EQ_I(ppcp_digest_set(&other, w), PPCP_OK);
        memset(&cm, 0, sizeof(cm));
        cm.capture_id = id_of("cap:shot");
        cm.digest     = other;
        CHECK_EQ_I(ppcp_transfer_on_committed(&t2, &cm), PPCP_ERR_MALFORMED);
        CHECK(!ppcp_transfer_is_evictable(&t2, &cm.capture_id));
    }

    TEST("5.14g exit 2 — an `absent` Capture is evictable with no commit possible");
    add(&t, "cap:absent", PPCP_ANCHOR_SHOT, PPCP_ABSENT, false);
    id = id_of("cap:absent");
    CHECK(ppcp_transfer_is_evictable(&t, &id));

    TEST("5.14g exit 3 — the receiver answered `already_present`");
    add(&t, "cap:dup", PPCP_ANCHOR_SHOT, PPCP_COMPLETE, false);
    id = id_of("cap:dup");
    CHECK(!ppcp_transfer_is_evictable(&t, &id));
    CHECK_EQ_I(ppcp_transfer_on_already_present(&t, &id), PPCP_OK);
    CHECK(ppcp_transfer_is_evictable(&t, &id));
    /* it is NOT `confirmed`: the owner's record of what it sent stays honest */
    CHECK_EQ_I(ppcp_transfer_find(&t, &id)->transfer, PPCP_TRANSFER_PRESENT);

    TEST("5.14g exit 4 — a discarded preview segment is permitted and announced absent");
    add(&t, "cap:prev", PPCP_ANCHOR_STREAM, PPCP_ABSENT, true);
    id = id_of("cap:prev");
    CHECK(ppcp_transfer_is_evictable(&t, &id));

    TEST("5.14g1 — a peer's own retention policy does NOT make shot-anchored "
         "payload evictable");
    add(&t, "cap:pressure", PPCP_ANCHOR_SHOT, PPCP_COMPLETE, false);
    id = id_of("cap:pressure");
    /* This is the call a policy under storage pressure would make. */
    CHECK_EQ_I(ppcp_transfer_mark_shed(&t, &id), PPCP_ERR_INVALID);
    CHECK(!ppcp_transfer_is_evictable(&t, &id));

    TEST("5.12.1b — candidate evidence, by contrast, MAY be shed");
    add(&t, "cap:evidence", PPCP_ANCHOR_CANDIDATE, PPCP_COMPLETE, false);
    id = id_of("cap:evidence");
    CHECK(!ppcp_transfer_is_evictable(&t, &id));
    CHECK_EQ_I(ppcp_transfer_mark_shed(&t, &id), PPCP_OK);
    CHECK(ppcp_transfer_is_evictable(&t, &id));

    TEST("the entity-only predicate knows exits 1 and 2, and says so by saying no");
    {
        ppcp_capture c;
        CHECK_EQ_I(ppcp_capture_make_shot(&c, "cap:x", "shot:1", "st:1", PPCP_COMPLETE),
                   PPCP_OK);
        CHECK(!ppcp_capture_is_evictable(&c));
        /* 5.14f made structural one layer down: the L4 setter refuses
         * `confirmed` outright, so an owner cannot even reach the state whose
         * eviction this predicate would then permit. */
        CHECK_EQ_I(ppcp_capture_set_transfer(&c, PPCP_TRANSFER_CONFIRMED),
                   PPCP_ERR_INVALID);
        /* A DECODED Capture can carry it, because the receiver said so on the
         * wire; that is the shape the predicate exists to answer about. */
        c.transfer = PPCP_TRANSFER_CONFIRMED;
        CHECK(ppcp_capture_is_evictable(&c));
        CHECK_EQ_I(ppcp_capture_make_shot(&c, "cap:y", "shot:1", "st:1", PPCP_ABSENT),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_capture_set_absent_reason(&c, PPCP_ABSENT_OUTSIDE_BUFFER),
                   PPCP_OK);
        CHECK(ppcp_capture_is_evictable(&c));
    }
}

/* ============================================================ CT-I36a */

static void test_preview_live_only(void)
{
    ppcp_transfer_table t;
    ppcp_capture        c;
    ppcp_interval       iv = ivl("tb:dev", 0, 1000);

    ppcp_transfer_table_init(&t);

    TEST("8.1i / 5.11j — a preview Capture is never announced `transfer: pending`");
    CHECK_EQ_I(ppcp_capture_make_segment(&c, "cap:p1", "st:prev", PPCP_COMPLETE, &iv),
               PPCP_OK);
    CHECK_EQ_I(c.transfer, PPCP_TRANSFER_PENDING);      /* the default */
    CHECK_EQ_I(ppcp_transfer_observe_announce(&t, &c, true), PPCP_ERR_INVALID);
    /* the same Capture on a non-preview Stream is ordinary and fine */
    CHECK_EQ_I(ppcp_transfer_observe_announce(&t, &c, false), PPCP_OK);

    TEST("5.11c3 / 8.1h — what was discarded is an `absent` segment with "
         "`not_retained`, never a gap");
    CHECK_EQ_I(ppcp_capture_make_segment(&c, "cap:p2", "st:prev", PPCP_ABSENT, &iv),
               PPCP_OK);
    CHECK_EQ_I(ppcp_capture_set_absent_reason(&c, PPCP_ABSENT_NOT_RETAINED), PPCP_OK);
    CHECK_EQ_I(ppcp_transfer_observe_announce(&t, &c, true), PPCP_OK);
    {
        ppcp_id id = id_of("cap:p2");
        CHECK_EQ_I(ppcp_transfer_find(&t, &id)->completeness, PPCP_ABSENT);
        /* 5.14g exit 4 follows from being preview at all: it was never going
         * to be sent, so no receiver will ever confirm it. */
        CHECK(ppcp_transfer_is_evictable(&t, &id));
    }
}

static void test_preview_never_in_a_bundle(void)
{
    void               *wm = NULL;
    ppcp_bundle_writer *w  = NULL;
    static uint8_t      out[16384];
    size_t              used = 0, n = 0;
    ppcp_msg            m;
    ppcp_stream         st;
    ppcp_instant        at = inst("tb:dev", 0);
    ppcp_interval       iv = ivl("tb:dev", 0, 1000);

    wm = malloc(ppcp_bundle_writer_sizeof());
    if (wm == NULL) abort();
    CHECK_EQ_I(ppcp_bundle_writer_new(wm, ppcp_bundle_writer_sizeof(), &w), PPCP_OK);
    CHECK_EQ_I(ppcp_bundle_writer_begin(w, out, sizeof(out), &n), PPCP_OK);
    used += n;

    TEST("5.11j — a preview Stream may be recorded; its Captures may not");
    CHECK_EQ_I(ppcp_stream_make(&st, "st:prev", "sess:1", "src:1",
                                PPCP_STREAM_KIND_PREVIEW, "cp:prev", "tb:dev",
                                PPCP_CONTINUOUS, &at), PPCP_OK);
    CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_STREAM_OPEN, 1), PPCP_OK);
    m.body.stream_open.stream = st;
    CHECK_EQ_I(ppcp_bundle_writer_append_msg(w, PPCP_CHANNEL_CONTROL, &m, out + used,
                                             sizeof(out) - used, &n), PPCP_OK);
    used += n;

    CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_CAPTURE_ANNOUNCE, 2), PPCP_OK);
    CHECK_EQ_I(ppcp_capture_make_segment(&m.body.capture_announce.capture, "cap:p",
                                         "st:prev", PPCP_COMPLETE, &iv), PPCP_OK);
    CHECK_EQ_I(ppcp_bundle_writer_append_msg(w, PPCP_CHANNEL_CONTROL, &m, out + used,
                                             sizeof(out) - used, &n), PPCP_ERR_INVALID);

    TEST("... and a segment on an ordinary continuous Stream is written normally");
    CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_CAPTURE_ANNOUNCE, 3), PPCP_OK);
    CHECK_EQ_I(ppcp_capture_make_segment(&m.body.capture_announce.capture, "cap:m",
                                         "st:meta", PPCP_COMPLETE, &iv), PPCP_OK);
    CHECK_EQ_I(ppcp_bundle_writer_append_msg(w, PPCP_CHANNEL_CONTROL, &m, out + used,
                                             sizeof(out) - used, &n), PPCP_OK);
    free(wm);
}

/* ============================================================ CT-I36 */

static void test_coverage(void)
{
    ppcp_stream   st;
    ppcp_instant  at = inst("tb:dev", 1000);
    ppcp_coverage cov;
    ppcp_capture  c;
    ppcp_interval hole;

    CHECK_EQ_I(ppcp_stream_make(&st, "st:meta", "sess:1", "src:1",
                                PPCP_STREAM_KIND_METADATA, "cp:1", "tb:dev",
                                PPCP_CONTINUOUS, &at), PPCP_OK);

    TEST("5.11b — coverage is a `continuous` Stream's obligation and no other's");
    {
        ppcp_stream sw;
        ppcp_coverage bad;
        CHECK_EQ_I(ppcp_stream_make(&sw, "st:1", "sess:1", "src:1",
                                    PPCP_STREAM_KIND_VIDEO, "cp:1", "tb:dev",
                                    PPCP_SHOT_WINDOWED, &at), PPCP_OK);
        CHECK_EQ_I(ppcp_coverage_init(&bad, &sw), PPCP_ERR_INVALID);
    }

    TEST("CT-I36 — contiguous segments account for the whole open interval");
    CHECK_EQ_I(ppcp_coverage_init(&cov, &st), PPCP_OK);
    {
        ppcp_interval a = ivl("tb:dev", 1000, 2000);
        ppcp_interval b = ivl("tb:dev", 2000, 3000);
        CHECK_EQ_I(ppcp_capture_make_segment(&c, "seg:1", "st:meta", PPCP_COMPLETE, &a),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_coverage_add(&cov, &c), PPCP_OK);
        CHECK_EQ_I(ppcp_capture_make_segment(&c, "seg:2", "st:meta", PPCP_COMPLETE, &b),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_coverage_add(&cov, &c), PPCP_OK);
    }
    CHECK_EQ_I(ppcp_coverage_check(&cov, 3000, PPCP_COMPLETE, &hole), PPCP_OK);

    TEST("5.14e — segments abut or leave a declared gap; they never OVERLAP");
    {
        ppcp_interval o = ivl("tb:dev", 1500, 2500);
        CHECK_EQ_I(ppcp_capture_make_segment(&c, "seg:x", "st:meta", PPCP_COMPLETE, &o),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_coverage_add(&cov, &c), PPCP_ERR_MALFORMED);
    }

    TEST("CT-I36 (a) — a segment removed from the MIDDLE is a defect in any Session");
    {
        ppcp_coverage cov2;
        ppcp_interval a = ivl("tb:dev", 1000, 2000);
        ppcp_interval d = ivl("tb:dev", 3000, 4000);
        CHECK_EQ_I(ppcp_coverage_init(&cov2, &st), PPCP_OK);
        CHECK_EQ_I(ppcp_capture_make_segment(&c, "seg:1", "st:meta", PPCP_COMPLETE, &a),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_coverage_add(&cov2, &c), PPCP_OK);
        CHECK_EQ_I(ppcp_capture_make_segment(&c, "seg:3", "st:meta", PPCP_COMPLETE, &d),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_coverage_add(&cov2, &c), PPCP_OK);
        CHECK_EQ_I(ppcp_coverage_check(&cov2, 4000, PPCP_COMPLETE, &hole),
                   PPCP_ERR_MALFORMED);
        CHECK_EQ_I(hole.start_ns, 2000);
        CHECK_EQ_I(hole.end_ns, 3000);
        /* 5.11c1: "nothing truncates a bundle in the middle" — so the same
         * hole is a defect in a `partial` Session too. */
        CHECK_EQ_I(ppcp_coverage_check(&cov2, 4000, PPCP_PARTIAL, &hole),
                   PPCP_ERR_MALFORMED);
        CHECK_EQ_I(ppcp_coverage_check(&cov2, 4000, PPCP_UNKNOWN, &hole),
                   PPCP_ERR_MALFORMED);
    }

    TEST("CT-I36 (b) — an `absent` segment with an interval and a reason SATISFIES "
         "coverage rather than breaching it");
    {
        ppcp_coverage cov3;
        ppcp_interval a = ivl("tb:dev", 1000, 2000);
        ppcp_interval g = ivl("tb:dev", 2000, 3000);
        ppcp_interval d = ivl("tb:dev", 3000, 4000);
        CHECK_EQ_I(ppcp_coverage_init(&cov3, &st), PPCP_OK);
        CHECK_EQ_I(ppcp_capture_make_segment(&c, "seg:1", "st:meta", PPCP_COMPLETE, &a),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_coverage_add(&cov3, &c), PPCP_OK);
        CHECK_EQ_I(ppcp_capture_make_segment(&c, "seg:2", "st:meta", PPCP_ABSENT, &g),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_capture_set_absent_reason(&c, PPCP_ABSENT_STORAGE_FULL), PPCP_OK);
        CHECK_EQ_I(ppcp_coverage_add(&cov3, &c), PPCP_OK);
        CHECK_EQ_I(ppcp_capture_make_segment(&c, "seg:3", "st:meta", PPCP_COMPLETE, &d),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_coverage_add(&cov3, &c), PPCP_OK);
        CHECK_EQ_I(ppcp_coverage_check(&cov3, 4000, PPCP_COMPLETE, &hole), PPCP_OK);
    }

    TEST("CT-I36 (c)(d) — an unaccounted TAIL is the declared incompleteness in a "
         "`partial` Session and a defect in a `complete` one");
    {
        ppcp_coverage cov4;
        ppcp_interval a = ivl("tb:dev", 1000, 2000);
        CHECK_EQ_I(ppcp_coverage_init(&cov4, &st), PPCP_OK);
        CHECK_EQ_I(ppcp_capture_make_segment(&c, "seg:1", "st:meta", PPCP_COMPLETE, &a),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_coverage_add(&cov4, &c), PPCP_OK);
        CHECK_EQ_I(ppcp_coverage_check(&cov4, 9000, PPCP_PARTIAL, &hole), PPCP_OK);
        CHECK_EQ_I(ppcp_coverage_check(&cov4, 9000, PPCP_UNKNOWN, &hole), PPCP_OK);
        CHECK_EQ_I(ppcp_coverage_check(&cov4, 9000, PPCP_COMPLETE, &hole),
                   PPCP_ERR_MALFORMED);
        CHECK_EQ_I(hole.start_ns, 2000);
        CHECK_EQ_I(hole.end_ns, 9000);
        /* and a closed Stream bounds the obligation at `closed_at` */
        CHECK_EQ_I(ppcp_coverage_close(&cov4, 2000), PPCP_OK);
        CHECK_EQ_I(ppcp_coverage_check(&cov4, 9000, PPCP_COMPLETE, &hole), PPCP_OK);
    }

    TEST("5.11d — accounting is over ANNOUNCED Captures, not over payload arrived");
    {
        ppcp_coverage cov5;
        ppcp_interval a = ivl("tb:dev", 1000, 2000);
        CHECK_EQ_I(ppcp_coverage_init(&cov5, &st), PPCP_OK);
        CHECK_EQ_I(ppcp_capture_make_segment(&c, "seg:1", "st:meta", PPCP_COMPLETE, &a),
                   PPCP_OK);
        /* still `pending`: nothing has transferred, and coverage does not care */
        CHECK_EQ_I(c.transfer, PPCP_TRANSFER_PENDING);
        CHECK_EQ_I(ppcp_coverage_add(&cov5, &c), PPCP_OK);
        CHECK_EQ_I(ppcp_coverage_check(&cov5, 2000, PPCP_COMPLETE, &hole), PPCP_OK);
    }
}

/* ============================================================ CT-I30 */

static void test_achieved_frames_placement(void)
{
    ppcp_msg  m;
    int64_t   frames[3] = { 1000, 2000, 3000 };
    uint8_t   buf[8192];
    size_t    written = 0;
    ppcp_achieved_frames af;

    CHECK_EQ_I(ppcp_achieved_frames_make(&af, "tb:dev", frames, 3), PPCP_OK);

    TEST("I30 — `capture_announce` has nowhere to put AchievedFrames, by type");
    /* The assertion is the absence: ppcp_capture has no achieved_frames member
     * and ppcp_body_capture_announce has none either, so 8.1b is not a rule
     * that can be broken here. */
    CHECK_EQ_I(sizeof(((ppcp_body_capture_announce *)0)->capture),
               sizeof(ppcp_capture));

    TEST("8.2b — `capture_update` carries it ONLY for a `failed` transfer");
    CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_CAPTURE_UPDATE, 1), PPCP_OK);
    m.body.capture_update.capture_id          = id_of("cap:1");
    m.body.capture_update.has_achieved_frames = true;
    m.body.capture_update.achieved_frames     = af;
    CHECK_EQ_I(ppcp_msg_encode(buf, sizeof(buf), PPCP_CHANNEL_CONTROL, &m, &written),
               PPCP_ERR_INVALID);
    m.body.capture_update.has_transfer = true;
    m.body.capture_update.transfer     = PPCP_TRANSFER_PRESENT;
    CHECK_EQ_I(ppcp_msg_encode(buf, sizeof(buf), PPCP_CHANNEL_CONTROL, &m, &written),
               PPCP_ERR_INVALID);
    m.body.capture_update.transfer = PPCP_TRANSFER_FAILED;
    CHECK_EQ_I(ppcp_msg_encode(buf, sizeof(buf), PPCP_CHANNEL_CONTROL, &m, &written),
               PPCP_OK);

    TEST("8.3g — and it rides `payload_begin`, on bulk, with the frames it describes");
    CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_PAYLOAD_BEGIN, 2), PPCP_OK);
    m.body.payload_begin.capture_id = id_of("cap:1");
    m.body.payload_begin.bytes      = 700;
    CHECK_EQ_I(ppcp_payload_digest((const uint8_t *)"x", 1,
                                   &m.body.payload_begin.digest), PPCP_OK);
    m.body.payload_begin.chunk_bytes          = PPCP_DEFAULT_CHUNK_BYTES;
    m.body.payload_begin.has_achieved_frames  = true;
    m.body.payload_begin.achieved_frames      = af;
    CHECK_EQ_I(ppcp_msg_encode(buf, sizeof(buf), PPCP_CHANNEL_BULK, &m, &written),
               PPCP_OK);
    /* MSG 2b: and it cannot be moved to control even by trying */
    CHECK_EQ_I(ppcp_msg_encode(buf, sizeof(buf), PPCP_CHANNEL_CONTROL, &m, &written),
               PPCP_ERR_MALFORMED);
}

int main(void)
{
    test_payload_codec();
    test_eviction();
    test_preview_live_only();
    test_preview_never_in_a_bundle();
    test_coverage();
    test_achieved_frames_placement();
    TEST_MAIN_END();
}
