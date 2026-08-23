/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * The checked-in fixture bundles of PPCP-CONF §2b, read back from disk.
 * Work package L15.
 *
 * ⚠ WHY FROM DISK, AND WHY THAT MATTERS.
 *
 * Every other bundle test in this suite builds its bytes in memory and reads
 * them back in the same process, which proves the codec agrees with itself.
 * These bytes were written by `tools/ppcp-fixtures` at an earlier commit and
 * are under version control, so this file proves something else: that the
 * format did not move.  A codec change that alters them fails here AND fails
 * `L15-fixtures-stable`, which regenerates into a temporary directory and
 * compares byte for byte — deterministic encoding (ENC 4e) makes that a fair
 * question to ask.
 *
 * Rows carried here:
 *
 *   CT-I2    a Series with a dropped sample: no timestamp is derived from
 *            position, because there is no position-derived form to derive one
 *            from and the gap survives the round trip
 *   CT-I11   an explicit `gap` on a `continuous` Stream, distinct from an
 *            `absent` segment nobody retained
 *   CT-I12   video-only, IMU-only and empty-Stream Sessions all load
 *   CT-I13   an unknown message type, unknown keys at three nesting levels, an
 *            unknown `Source.kind` and an unknown `Candidate.basis`: none is
 *            fatal and the surrounding data survives
 *   CT-I15   a `wall` timebase that steps: no interval is computed on it
 *   CT-I34   the same bundle imported twice: each Capture once, keyed on
 *            identity and NOT on digest
 *   CT-I36   (c) and (d): a truncated tail asserted `partial` is the declared
 *            incompleteness; the same truncation asserted `complete` is a
 *            defect, and the two are distinguishable
 */
#include "ppcp/ppcp.h"

#include "test_util.h"

#ifndef PPCP_FIXTURE_DIR
#define PPCP_FIXTURE_DIR "tests/fixtures"
#endif

static uint8_t g_bytes[262144];

static size_t load(const char *name)
{
    char   path[1024];
    FILE  *f;
    size_t n;
    snprintf(path, sizeof(path), "%s/%s", PPCP_FIXTURE_DIR, name);
    f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "FAIL cannot open fixture %s\n", path);
        exit(EXIT_FAILURE);
    }
    n = fread(g_bytes, 1, sizeof(g_bytes), f);
    fclose(f);
    return n;
}

static ppcp_peer *new_sink(void **storage)
{
    static const char *const all[] = {
        PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE, PPCP_PROFILE_DETECT,
        PPCP_PROFILE_MINT, PPCP_PROFILE_ARBITRATE, PPCP_PROFILE_LIVE,
        PPCP_PROFILE_MARKUP, PPCP_PROFILE_OFFLINE
    };
    ppcp_peer_config cfg;
    ppcp_peer       *p = NULL;
    void            *mem = malloc(ppcp_peer_sizeof());

    if (mem == NULL) abort();
    memset(&cfg, 0, sizeof(cfg));
    cfg.role          = PPCP_ROLE_HOST;
    cfg.peer_id       = "peer:importer";
    cfg.profiles      = all;
    cfg.profile_count = 8;
    cfg.listener      = true;
    cfg.health_report = ppcp_test_health;
    if (ppcp_peer_new(mem, ppcp_peer_sizeof(), &cfg, &p) != PPCP_OK) abort();
    *storage = mem;
    return p;
}

static ppcp_bundle_reader *new_reader(void **storage, ppcp_peer *sink)
{
    ppcp_bundle_reader *r = NULL;
    void *mem = malloc(ppcp_bundle_reader_sizeof());
    if (mem == NULL) abort();
    if (ppcp_bundle_reader_new(mem, ppcp_bundle_reader_sizeof(), sink, &r) != PPCP_OK)
        abort();
    *storage = mem;
    return r;
}

/* ================================================================= CT-I12 */

static void test_i12(void)
{
    static const struct { const char *file; size_t streams; } cases[] = {
        { "ct-i12-video.ppcpb", 1 },
        { "ct-i12-imu.ppcpb",   1 },
        { "ct-i12-empty.ppcpb", 0 }
    };
    size_t i;

    TEST("CT-I12 — video-only, IMU-only and empty-Stream Sessions all load, from disk");
    for (i = 0; i < 3; i++) {
        void      *sm = NULL, *rm = NULL;
        ppcp_peer *sink = new_sink(&sm);
        ppcp_bundle_reader *r = new_reader(&rm, sink);
        size_t     len = load(cases[i].file), consumed = 0;

        CHECK(len > PPCP_BUNDLE_HEADER_BYTES);
        CHECK_EQ_I(ppcp_test_reader_feed_all(r, sink, g_bytes, len, &consumed), PPCP_OK);
        CHECK_EQ_I(consumed, len);
        CHECK(!ppcp_bundle_reader_truncated(r));
        CHECK(ppcp_bundle_reader_manifest_ordered(r));
        CHECK(ppcp_peer_session_id(sink) != NULL);
        CHECK_EQ_I(ppcp_peer_stream_count(sink), cases[i].streams);
        /* 4.1b: the device recorded its own hostless `session_open`, and the
         * absence of the arbitration parameters IS the statement (F-D6-3). */
        CHECK(ppcp_peer_session_params(sink) != NULL);
        CHECK(!ppcp_peer_session_params(sink)->has_arbitration);
        CHECK(ppcp_peer_zero_host(sink));
        ppcp_peer_free(sink);
        free(sm);
        free(rm);
    }
}

/* ================================================================= CT-I34 */

static void test_i34(void)
{
    void      *sm = NULL, *rm = NULL, *r2m = NULL;
    ppcp_peer *sink = new_sink(&sm);
    ppcp_bundle_reader *r, *r2;
    size_t     len, consumed = 0;
    ppcp_capture_index *idx;

    TEST("CT-I34 — the same bundle imported twice: each Capture once, digest is not the key");
    len = load("ct-i12-video.ppcpb");
    r = new_reader(&rm, sink);
    CHECK_EQ_I(ppcp_test_reader_feed_all(r, sink, g_bytes, len, &consumed), PPCP_OK);
    idx = ppcp_bundle_reader_index(r);
    CHECK(idx != NULL);
    CHECK_EQ_I(ppcp_capture_index_count(idx), 2);

    /* A SECOND reader carrying the FIRST one's index: re-import is what a user
     * does when they plug the phone in again, and I34 scopes identity by
     * session and owning peer — not by digest, which one of these two Captures
     * does not have at all and the other never will. */
    r2  = new_reader(&r2m, sink);
    *ppcp_bundle_reader_index(r2) = *idx;
    idx = ppcp_bundle_reader_index(r2);
    consumed = 0;
    CHECK_EQ_I(ppcp_test_reader_feed_all(r2, sink, g_bytes, len, &consumed), PPCP_OK);
    CHECK_EQ_I(consumed, len);
    CHECK_EQ_I(ppcp_capture_index_count(idx), 2);   /* still two, not four */

    ppcp_peer_free(sink);
    free(sm); free(rm); free(r2m);
}

/* ============================================================ CT-I2, CT-I11 */

static void test_i2_i11(void)
{
    void      *sm = NULL, *rm = NULL;
    ppcp_peer *sink = new_sink(&sm);
    ppcp_bundle_reader *r = new_reader(&rm, sink);
    size_t     len = load("ct-i2-i11-series-gap.ppcpb");
    ppcp_event ev;
    bool       saw_gap = false, saw_frames = false;
    int64_t    prev = 0;
    size_t     uniform = 0, doubled = 0;

    TEST("CT-I2 / CT-I11 — an explicit Series with a dropped sample, and an explicit gap");
    /* Draining the sink's events here rather than with the helper, because the
     * events ARE the assertion. */
    {
        size_t off = 0;
        while (off <= len) {
            size_t took = 0;
            CHECK_EQ_I(ppcp_bundle_reader_feed(r, g_bytes + off, len - off, &took), PPCP_OK);
            off += took;
            while (ppcp_peer_next_event(sink, &ev) == PPCP_OK) {
                if (ev.msg == NULL)
                    continue;
                if (ev.msg->type == PPCP_MT_CAPTURE_ANNOUNCE) {
                    const ppcp_capture *c = &ev.msg->body.capture_announce.capture;
                    if (c->gap_count > 0) {
                        saw_gap = true;
                        /* I11: a gap is LOSS inside a segment that exists, and
                         * it is half-open [start, end) like every interval. */
                        CHECK_EQ_I(c->anchor.kind, PPCP_ANCHOR_STREAM);
                        CHECK_EQ_I(c->gap_count, 1);
                        CHECK(c->gaps[0].end_ns > c->gaps[0].start_ns);
                        CHECK(ppcp_cbor_key_is(c->gaps[0].tb.v, c->gaps[0].tb.len, "tb:dev"));
                        /* 5.11c3: this is NOT an absent segment.  The segment
                         * exists and is `partial`. */
                        CHECK_EQ_I(c->completeness, PPCP_PARTIAL);
                        CHECK(!c->has_absent_reason);
                    }
                }
                if (ev.msg->type == PPCP_MT_PAYLOAD_BEGIN &&
                    ev.msg->body.payload_begin.has_achieved_frames) {
                    const ppcp_achieved_frames *af =
                        &ev.msg->body.payload_begin.achieved_frames;
                    size_t i;
                    saw_frames = true;
                    CHECK_EQ_I(af->frame_count, 8);
                    /* I2 — the frame times are an EXPLICIT array.  Seven
                     * intervals: six of one frame period and one of two, which
                     * is the dropped frame.  A reader that reconstructed times
                     * from index and rate would produce seven equal intervals
                     * and be wrong from the gap onwards. */
                    for (i = 1; i < af->frame_count; i++) {
                        int64_t d = af->frames_ns[i] - af->frames_ns[i - 1];
                        if (d == 8333333LL)      uniform++;
                        else if (d == 16666666LL) doubled++;
                        prev = af->frames_ns[i];
                    }
                    CHECK_EQ_I(uniform, 6);
                    CHECK_EQ_I(doubled, 1);
                    CHECK(prev > af->frames_ns[0]);
                }
            }
            if (!ppcp_bundle_reader_stalled(r))
                break;
        }
        CHECK_EQ_I(off, len);
    }
    CHECK(saw_gap);
    CHECK(saw_frames);

    /* I2 by API surface as well as by data: there is no constructor that takes
     * a start and a rate, so a position-derived timestamp is not expressible. */
    {
        ppcp_series s;
        static const int64_t one[] = { 1000 };
        CHECK_EQ_I(ppcp_series_make(&s, "tb:dev", 6, one, 1), PPCP_OK);
        CHECK_EQ_I(s.count, 1);
        CHECK(s.ns == one);
    }

    ppcp_peer_free(sink);
    free(sm); free(rm);
}

/* ================================================================= CT-I13 */

static void test_i13(void)
{
    void      *sm = NULL, *rm = NULL;
    ppcp_peer *sink = new_sink(&sm);
    ppcp_bundle_reader *r = new_reader(&rm, sink);
    size_t     len = load("ct-i13-unknowns.ppcpb"), off = 0;
    ppcp_event ev;
    bool       saw_unknown = false, saw_foreign_source = false;
    bool       saw_foreign_basis = false, saw_after = false;

    TEST("CT-I13 — unknown type, unknown keys at three levels, unknown kind and basis");
    while (off <= len) {
        size_t took = 0;
        CHECK_EQ_I(ppcp_bundle_reader_feed(r, g_bytes + off, len - off, &took), PPCP_OK);
        off += took;
        while (ppcp_peer_next_event(sink, &ev) == PPCP_OK) {
            if (ev.kind == PPCP_EVENT_UNKNOWN)
                saw_unknown = true;      /* MSG 1b: carried, not dropped */
            if (ev.msg == NULL)
                continue;
            if (ev.msg->type == PPCP_MT_DECLARE) {
                const ppcp_peer_desc *d = &ev.msg->body.declare.peer;
                if (d->source_count == 1 &&
                    ppcp_cbor_key_is(d->sources[0].kind.v, d->sources[0].kind.len,
                                     "com.example.doppler_radar"))
                    saw_foreign_source = true;   /* 5.6: an OPEN registry */
            }
            if (ev.msg->type == PPCP_MT_CANDIDATE &&
                ppcp_cbor_key_is(ev.msg->body.candidate.candidate.basis.v,
                                 ev.msg->body.candidate.candidate.basis.len,
                                 "com.example.doppler_return"))
                saw_foreign_basis = true;
            if (ev.msg->type == PPCP_MT_CAPTURE_ANNOUNCE)
                saw_after = true;        /* the surrounding data survives */
        }
        if (!ppcp_bundle_reader_stalled(r))
            break;
    }
    CHECK_EQ_I(off, len);
    CHECK(saw_unknown);
    CHECK(saw_foreign_source);
    CHECK(saw_foreign_basis);
    CHECK(saw_after);
    /* None of it was fatal: the engine is still open and still in the Session. */
    CHECK(ppcp_peer_get_state(sink) != PPCP_PEER_CLOSED);
    CHECK(ppcp_peer_session_id(sink) != NULL);
    CHECK(!ppcp_bundle_reader_truncated(r));

    ppcp_peer_free(sink);
    free(sm); free(rm);
}

/* ================================================================= CT-I15 */

static void test_i15(void)
{
    void      *sm = NULL, *rm = NULL;
    ppcp_peer *sink = new_sink(&sm);
    ppcp_bundle_reader *r = new_reader(&rm, sink);
    size_t     len = load("ct-i15-wall-step.ppcpb"), consumed = 0;
    const ppcp_peer_desc *d;
    size_t     i, wall = 0;

    TEST("CT-I15 — a `wall` timebase is declared, and nothing computes an interval on it");
    CHECK_EQ_I(ppcp_test_reader_feed_all(r, sink, g_bytes, len, &consumed), PPCP_OK);
    CHECK_EQ_I(consumed, len);
    d = ppcp_peer_counterpart(sink);
    CHECK(d != NULL);
    for (i = 0; i < d->timebase_count; i++)
        if (ppcp_timebase_is_wall(&d->timebases[i]))
            wall++;
    CHECK_EQ_I(wall, 1);

    /* 5.3b / I15 — the refusal is by API SURFACE, which is the only place it
     * can live in a library that computes no durations: there is no
     * ppcp_instant_diff, no elapsed(), no duration_between() anywhere in the
     * public headers, so no consumer can subtract two Instants THROUGH this
     * library at all, on the wall timebase or any other.  What the library owes
     * is the predicate a consumer asks before it subtracts on its own account,
     * and the rule that the Session's reference clock is never the wall one.
     *
     * ⚠ It is worth being precise about what that does and does not prove.  It
     * proves libppcp never computes an interval on a `wall` timebase.  It does
     * NOT prove an EMBEDDING will not — that is the embedding's row, and it is
     * why CT-I15 is a separate cell for each application. */
    CHECK(ppcp_cbor_key_is(ppcp_peer_timebase_ref(sink)->v,
                           ppcp_peer_timebase_ref(sink)->len, "tb:dev"));

    ppcp_peer_free(sink);
    free(sm); free(rm);
}

/* ============================================================ CT-I36 (c)(d) */

static void test_i36(void)
{
    static const struct {
        const char       *file;
        ppcp_completeness asserted;
    } cases[] = {
        { "ct-i36-truncated-partial.ppcpb",  PPCP_PARTIAL  },
        { "ct-i36-truncated-complete.ppcpb", PPCP_COMPLETE }
    };
    size_t i;

    TEST("CT-I36 (c)(d) — a truncated tail means different things under different assertions");
    for (i = 0; i < 2; i++) {
        void *rm = NULL;
        ppcp_bundle_reader *r = new_reader(&rm, NULL);
        size_t len = load(cases[i].file), consumed = 0;
        ppcp_completeness got = PPCP_UNKNOWN, asserted = PPCP_UNKNOWN;

        CHECK_EQ_I(ppcp_bundle_reader_feed(r, g_bytes, len, &consumed), PPCP_OK);
        CHECK(consumed < len);                       /* it stopped mid-frame */
        CHECK(ppcp_bundle_reader_truncated(r));
        CHECK(ppcp_bundle_reader_asserted(r, &asserted));
        CHECK_EQ_I(asserted, cases[i].asserted);
        CHECK_EQ_I(ppcp_bundle_reader_finish(r, &got), PPCP_OK);
        /* ENC 7d / I10: the owner's assertion stands whatever the bytes did.
         * The `complete` one is a DEFECT and the reader says so by reporting
         * `complete` alongside `truncated` — it does not quietly downgrade,
         * because inventing a verdict is how a defect stops being visible. */
        CHECK_EQ_I(got, cases[i].asserted);
        free(rm);
    }
}

int main(void)
{
    test_i12();
    test_i34();
    test_i2_i11();
    test_i13();
    test_i15();
    test_i36();
    TEST_MAIN_END();
}
