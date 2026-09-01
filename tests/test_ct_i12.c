/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * The bundle container — work package L8's evidence, and the fixture format
 * every later fixture test is written in (CONF 2b).
 *
 * Rows this file carries:
 *
 *   CT-I12   any subset of Streams is a valid bundle — video-only, IMU-only,
 *            and a Session with no Streams at all
 *   CT-I15   (container half) a bundle whose frames the reader replays
 *   CT-I34   re-import is idempotent on `Capture.id` scoped by session and
 *            owning peer, with `digest` NOT the key
 *   CT-I36   (c) and (d): a truncated tail in a Session asserted `partial` is
 *            the declared incompleteness; the same truncation in a Session
 *            asserted `complete` is a defect, and the two are distinguishable
 *   ENC 7c   `session_manifest` before any payload frame
 *   ENC 7d   truncation, and the rule that completeness is never inferred up
 *   ENC 7f   an unknown `minor` is tolerated
 *   ENC 7g   no `link_bind` in a bundle
 *   CORE 7.3b  no `arm` in a hostless bundle
 */
#include "ppcp/bundle.h"

#include "test_util.h"

/* ------------------------------------------------------------------ rigging */

typedef struct buf {
    uint8_t b[262144];
    size_t  n;
} buf;

static ppcp_bundle_writer *new_writer(void **storage)
{
    ppcp_bundle_writer *w = NULL;
    void *mem = malloc(ppcp_bundle_writer_sizeof());
    if (mem == NULL) abort();
    if (ppcp_bundle_writer_new(mem, ppcp_bundle_writer_sizeof(), &w) != PPCP_OK) abort();
    *storage = mem;
    return w;
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

static ppcp_peer *new_sink(void **storage)
{
    /* All eight profiles, so nothing in a bundle meets a C3 refusal on the way
     * in: the reference implementation claims the lot. */
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
    /* F-H5-3: Live is refused without one, and every rig here declares Live. */
    cfg.health_report = ppcp_test_health;
    if (ppcp_peer_new(mem, ppcp_peer_sizeof(), &cfg, &p) != PPCP_OK) abort();
    *storage = mem;
    return p;
}

static ppcp_result emit(ppcp_bundle_writer *w, buf *out, uint8_t channel, const ppcp_msg *m)
{
    size_t      n  = 0;
    ppcp_result rc = ppcp_bundle_writer_append_msg(w, channel, m, out->b + out->n,
                                                   sizeof(out->b) - out->n, &n);
    if (rc == PPCP_OK)
        out->n += n;
    return rc;
}

static ppcp_instant inst(const char *tb, int64_t ns)
{
    ppcp_instant i;
    memset(&i, 0, sizeof(i));
    if (ppcp_instant_make_z(&i, tb, ns) != PPCP_OK) abort();
    return i;
}

static ppcp_digest dig(uint8_t fill)
{
    ppcp_digest d;
    uint8_t     v[PPCP_SHA256_BYTES];
    memset(&d, 0, sizeof(d));
    memset(v, fill, sizeof(v));
    if (ppcp_digest_set(&d, v) != PPCP_OK) abort();
    return d;
}

typedef struct decl {
    ppcp_id              profiles[4];
    ppcp_timebase        tb[1];
    ppcp_capture_profile cp[1];
    ppcp_source          src[1];
    ppcp_peer_desc       peer;
} decl;

static void build_decl(decl *d, const char *peer_id, const char *tb_id)
{
    static const char *const prof[] = {
        PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE, PPCP_PROFILE_MINT, PPCP_PROFILE_OFFLINE
    };
    ppcp_timing   timing;
    ppcp_geometry geom;
    size_t        i;

    memset(d, 0, sizeof(*d));
    for (i = 0; i < 4; i++)
        if (ppcp_id_set_z(&d->profiles[i], prof[i]) != PPCP_OK) abort();
    if (ppcp_timebase_make(&d->tb[0], tb_id, strlen(tb_id), PPCP_TB_CONTINUOUS, true, 1000)
        != PPCP_OK) abort();
    if (ppcp_timing_make_nominal_frame_start(&timing, 120000, PPCP_PROV_ASSUMED) != PPCP_OK)
        abort();
    if (ppcp_geometry_make_rolling_shutter(&geom, 8000000, PPCP_PROV_ASSUMED,
                                           PPCP_ROLL_TOP_TO_BOTTOM, 1080) != PPCP_OK) abort();
    if (ppcp_capture_profile_make(&d->cp[0], "cp:1", &timing) != PPCP_OK) abort();
    if (ppcp_capture_profile_set_camera(&d->cp[0], &geom, PPCP_INTR_PER_FRAME) != PPCP_OK)
        abort();
    if (ppcp_source_make(&d->src[0], "src:1", peer_id, "camera", tb_id, true, d->cp, 1)
        != PPCP_OK) abort();
    if (ppcp_peer_desc_make(&d->peer, peer_id, PPCP_ROLE_CAPTURE, "1.0", d->profiles, 4,
                            d->tb, 1) != PPCP_OK) abort();
    if (ppcp_peer_desc_set_sources(&d->peer, d->src, 1) != PPCP_OK) abort();
}

/* Writes the bundle a hostless capture device produces: no `arm`, no
 * arbitration parameters, and the manifest before the payload.
 *
 * `stream_kind` is NULL for the empty-Session case (I12). */
static void write_session(buf *out, const char *stream_kind, bool with_payload,
                          bool assert_state, ppcp_completeness asserted)
{
    void               *wm = NULL;
    ppcp_bundle_writer *w  = new_writer(&wm);
    ppcp_session        sess;
    ppcp_msg            m;
    decl                d;
    size_t              n = 0;

    out->n = 0;
    CHECK_EQ_I(ppcp_bundle_writer_begin(w, out->b, sizeof(out->b), &n), PPCP_OK);
    out->n += n;
    CHECK_EQ_I(n, PPCP_BUNDLE_HEADER_BYTES);

    /* CORE 4.1b: the hostless peer records `session_open` itself, with its own
     * timebase as `timebase_ref` and WITHOUT the two arbitration parameters. */
    ppcp_instant opened_at_167;
    CHECK_EQ_I(ppcp_instant_make_z(&opened_at_167, "tb:dev", 0), PPCP_OK);
    CHECK_EQ_I(ppcp_session_make_hostless(&sess, "sess:1", "tb:dev", &opened_at_167), PPCP_OK);
    CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_SESSION_OPEN, 1), PPCP_OK);
    m.body.session_open.session_id   = sess.id;
    m.body.session_open.timebase_ref = sess.timebase_ref;
    CHECK_EQ_I(emit(w, out, PPCP_CHANNEL_CONTROL, &m), PPCP_OK);
    CHECK(ppcp_bundle_writer_is_hostless(w));

    build_decl(&d, "peer:dev", "tb:dev");
    CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_DECLARE, 2), PPCP_OK);
    m.body.declare.generation = 1;
    m.body.declare.peer       = d.peer;
    CHECK_EQ_I(emit(w, out, PPCP_CHANNEL_CONTROL, &m), PPCP_OK);

    if (stream_kind != NULL) {
        ppcp_stream  st;
        ppcp_instant at = inst("tb:dev", 1000);
        CHECK_EQ_I(ppcp_stream_make(&st, "st:1", "sess:1", "src:1", stream_kind, "cp:1",
                                    "tb:dev", PPCP_SHOT_WINDOWED, &at), PPCP_OK);
        CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_STREAM_OPEN, 3), PPCP_OK);
        m.body.stream_open.stream = st;
        CHECK_EQ_I(emit(w, out, PPCP_CHANNEL_CONTROL, &m), PPCP_OK);

        /* 7.3c: `readiness` is conferred by Capture, so a hostless bundle
         * records it — which is the half of 7.3b that is easy to get wrong in
         * the other direction. */
        CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_READINESS, 4), PPCP_OK);
        CHECK_EQ_I(ppcp_readiness_settled(&m.body.readiness.readiness), PPCP_OK);
        CHECK_EQ_I(emit(w, out, PPCP_CHANNEL_CONTROL, &m), PPCP_OK);

        /* CT-I34's two awkward Captures, in the bundle rather than beside it:
         * a `complete` Capture whose transfer is still `pending` and which
         * therefore has NO digest yet, and an `absent` one that will never
         * have one.  An importer keyed on the digest duplicates both. */
        CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_CAPTURE_ANNOUNCE, 5), PPCP_OK);
        CHECK_EQ_I(ppcp_msg_set_session_id(&m, "sess:1"), PPCP_OK);
        CHECK_EQ_I(ppcp_capture_make_shot(&m.body.capture_announce.capture, "cap:1",
                                          "shot:1", "st:1", PPCP_COMPLETE), PPCP_OK);
        CHECK_EQ_I(m.body.capture_announce.capture.transfer, PPCP_TRANSFER_PENDING);
        CHECK(!m.body.capture_announce.capture.digest.present);
        CHECK_EQ_I(emit(w, out, PPCP_CHANNEL_CONTROL, &m), PPCP_OK);

        CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_CAPTURE_ANNOUNCE, 6), PPCP_OK);
        CHECK_EQ_I(ppcp_msg_set_session_id(&m, "sess:1"), PPCP_OK);
        CHECK_EQ_I(ppcp_capture_make_shot(&m.body.capture_announce.capture, "cap:gone",
                                          "shot:2", "st:1", PPCP_ABSENT), PPCP_OK);
        CHECK_EQ_I(ppcp_capture_set_absent_reason(&m.body.capture_announce.capture,
                                                  PPCP_ABSENT_OUTSIDE_BUFFER), PPCP_OK);
        CHECK_EQ_I(emit(w, out, PPCP_CHANNEL_CONTROL, &m), PPCP_OK);
    }

    if (assert_state) {
        CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_SESSION_STATE, 6), PPCP_OK);
        m.body.session_state.session_id   = sess.id;
        m.body.session_state.state        = PPCP_SESSION_CLOSED;
        m.body.session_state.completeness = asserted;
        CHECK_EQ_I(emit(w, out, PPCP_CHANNEL_CONTROL, &m), PPCP_OK);
    }

    /* ENC 7c: the manifest before any payload frame. */
    CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_SESSION_MANIFEST, 7), PPCP_OK);
    m.body.session_manifest.session_id = sess.id;
    if (stream_kind != NULL) {
        CHECK_EQ_I(ppcp_id_set_z(&m.body.session_manifest.streams[0], "st:1"), PPCP_OK);
        m.body.session_manifest.stream_count = 1;
        CHECK_EQ_I(ppcp_id_set_z(&m.body.session_manifest.captures[0].capture_id, "cap:1"),
                   PPCP_OK);
        m.body.session_manifest.captures[0].digest = dig(0x33);
        m.body.session_manifest.captures[0].bytes  = 6;
        CHECK_EQ_I(ppcp_id_set_z(&m.body.session_manifest.captures[0].stream_id, "st:1"),
                   PPCP_OK);
        m.body.session_manifest.capture_count = 1;
        m.body.session_manifest.count_captures = 1;
    }
    /* `unknown` here so the container test is not accidentally asserting
     * completeness through the manifest; the state frame above is where the
     * assertion is made when the case wants one. */
    m.body.session_manifest.completeness = PPCP_UNKNOWN;
    CHECK_EQ_I(emit(w, out, PPCP_CHANNEL_CONTROL, &m), PPCP_OK);
    CHECK(ppcp_bundle_writer_has_manifest(w));

    if (with_payload) {
        static const uint8_t clip[] = { 1, 2, 3, 4, 5, 6 };
        CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_PAYLOAD_BEGIN, 8), PPCP_OK);
        CHECK_EQ_I(ppcp_id_set_z(&m.body.payload_begin.capture_id, "cap:1"), PPCP_OK);
        m.body.payload_begin.bytes       = sizeof(clip);
        m.body.payload_begin.digest      = dig(0x33);
        m.body.payload_begin.chunk_bytes = PPCP_DEFAULT_CHUNK_BYTES;
        CHECK_EQ_I(emit(w, out, PPCP_CHANNEL_BULK, &m), PPCP_OK);

        CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_PAYLOAD_CHUNK, 9), PPCP_OK);
        CHECK_EQ_I(ppcp_id_set_z(&m.body.payload_chunk.capture_id, "cap:1"), PPCP_OK);
        m.body.payload_chunk.index    = 0;
        m.body.payload_chunk.offset   = 0;
        m.body.payload_chunk.data     = clip;
        m.body.payload_chunk.data_len = sizeof(clip);
        m.body.payload_chunk.digest   = dig(0x44);
        CHECK_EQ_I(emit(w, out, PPCP_CHANNEL_BULK, &m), PPCP_OK);

        CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_PAYLOAD_END, 10), PPCP_OK);
        CHECK_EQ_I(ppcp_id_set_z(&m.body.payload_end.capture_id, "cap:1"), PPCP_OK);
        m.body.payload_end.digest = dig(0x33);
        CHECK_EQ_I(emit(w, out, PPCP_CHANNEL_BULK, &m), PPCP_OK);
    }

    CHECK_EQ_I(ppcp_bundle_writer_finish(w), PPCP_OK);
    free(wm);
}

/* ================================================== what the writer refuses */

static void test_writer_refusals(void)
{
    void               *wm = NULL;
    ppcp_bundle_writer *w  = new_writer(&wm);
    static buf          out;
    ppcp_msg            m;
    ppcp_session        sess;
    size_t              n = 0;

    out.n = 0;
    CHECK_EQ_I(ppcp_bundle_writer_begin(w, out.b, sizeof(out.b), &n), PPCP_OK);
    out.n += n;

    TEST("ENC 7g / 2.1e — a bundle never contains `link_bind`");
    CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_LINK_BIND, 1), PPCP_OK);
    memset(m.body.link_bind.link_id, 0x11, PPCP_LINK_ID_BYTES);
    m.body.link_bind.channel = PPCP_CHANNEL_CONTROL;
    CHECK_EQ_I(emit(w, &out, PPCP_CHANNEL_CONTROL, &m), PPCP_ERR_INVALID);
    CHECK_EQ_I(ppcp_bundle_writer_frame_count(w), 0);

    TEST("ENC 7c — a payload frame before `session_manifest` is refused");
    CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_PAYLOAD_BEGIN, 1), PPCP_OK);
    CHECK_EQ_I(ppcp_id_set_z(&m.body.payload_begin.capture_id, "cap:1"), PPCP_OK);
    m.body.payload_begin.bytes       = 6;
    m.body.payload_begin.digest      = dig(0x33);
    m.body.payload_begin.chunk_bytes = PPCP_DEFAULT_CHUNK_BYTES;
    CHECK_EQ_I(emit(w, &out, PPCP_CHANNEL_BULK, &m), PPCP_ERR_INVALID);

    TEST("CORE 7.3b — no `arm` once a hostless `session_open` is recorded");
    ppcp_instant opened_at_306;
    CHECK_EQ_I(ppcp_instant_make_z(&opened_at_306, "tb:dev", 0), PPCP_OK);
    CHECK_EQ_I(ppcp_session_make_hostless(&sess, "sess:1", "tb:dev", &opened_at_306), PPCP_OK);
    CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_SESSION_OPEN, 1), PPCP_OK);
    m.body.session_open.session_id   = sess.id;
    m.body.session_open.timebase_ref = sess.timebase_ref;
    CHECK_EQ_I(emit(w, &out, PPCP_CHANNEL_CONTROL, &m), PPCP_OK);
    CHECK(ppcp_bundle_writer_is_hostless(w));
    CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_ARM, 2), PPCP_OK);
    CHECK_EQ_I(emit(w, &out, PPCP_CHANNEL_CONTROL, &m), PPCP_ERR_INVALID);
    CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_DISARM, 2), PPCP_OK);
    CHECK_EQ_I(emit(w, &out, PPCP_CHANNEL_CONTROL, &m), PPCP_ERR_INVALID);
    free(wm);

    TEST("... and a HOSTED bundle records both, because somebody sent them");
    {
        void               *w2m = NULL;
        ppcp_bundle_writer *w2  = new_writer(&w2m);
        static buf          o2;
        o2.n = 0;
        CHECK_EQ_I(ppcp_bundle_writer_begin(w2, o2.b, sizeof(o2.b), &n), PPCP_OK);
        o2.n += n;
        ppcp_instant opened_at_326;
        CHECK_EQ_I(ppcp_instant_make_z(&opened_at_326, "tb:host", 0), PPCP_OK);
        CHECK_EQ_I(ppcp_session_make_hosted(&sess, "sess:1", "tb:host", &opened_at_326,
                                            PPCP_DEFAULT_COINCIDENCE_WINDOW_NS,
                                            PPCP_DEFAULT_ISSUE_HOLD_NS), PPCP_OK);
        CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_SESSION_OPEN, 1), PPCP_OK);
        m.body.session_open.session_id            = sess.id;
        m.body.session_open.timebase_ref          = sess.timebase_ref;
        m.body.session_open.has_arbitration       = true;
        m.body.session_open.coincidence_window_ns = sess.coincidence_window_ns;
        m.body.session_open.issue_hold_ns         = sess.issue_hold_ns;
        CHECK_EQ_I(emit(w2, &o2, PPCP_CHANNEL_CONTROL, &m), PPCP_OK);
        CHECK(!ppcp_bundle_writer_is_hostless(w2));
        CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_ARM, 2), PPCP_OK);
        CHECK_EQ_I(emit(w2, &o2, PPCP_CHANNEL_CONTROL, &m), PPCP_OK);
        free(w2m);
    }

    TEST("ENC 7e — finish writes no footer, and refuses a later append");
    {
        void               *w3m = NULL;
        ppcp_bundle_writer *w3  = new_writer(&w3m);
        static buf          o3;
        size_t              before;
        o3.n = 0;
        CHECK_EQ_I(ppcp_bundle_writer_begin(w3, o3.b, sizeof(o3.b), &n), PPCP_OK);
        o3.n += n;
        before = o3.n;
        CHECK_EQ_I(ppcp_bundle_writer_finish(w3), PPCP_OK);
        CHECK_EQ_I(o3.n, before);
        CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_HEARTBEAT, 1), PPCP_OK);
        CHECK_EQ_I(emit(w3, &o3, PPCP_CHANNEL_CONTROL, &m), PPCP_ERR_INVALID);
        free(w3m);
    }
}

/* ================================================= round trip through a peer */

static void test_round_trip(void)
{
    static buf          bundle;
    void               *sm = NULL, *rm = NULL;
    ppcp_peer          *sink;
    ppcp_bundle_reader *r;
    size_t              consumed = 0;
    ppcp_completeness   c;

    TEST("CORE 9a — the bundle replays through the same feed a socket drives");
    write_session(&bundle, PPCP_STREAM_KIND_VIDEO, true, false, PPCP_UNKNOWN);
    sink = new_sink(&sm);
    r    = new_reader(&rm, sink);
    /* F-L13-1: with a live sink the reader stops when the sink's event queue
     * is full, so the replay is a drain-and-continue loop.  Ten frames past a
     * four-deep ring is exactly the shape that used to lose events. */
    CHECK_EQ_I(ppcp_test_reader_feed_all(r, sink, bundle.b, bundle.n, &consumed), PPCP_OK);
    CHECK_EQ_I(consumed, bundle.n);
    CHECK_EQ_I(ppcp_bundle_reader_frame_count(r), 10);
    CHECK(ppcp_bundle_reader_manifest_ordered(r));
    CHECK(!ppcp_bundle_reader_truncated(r));

    /* The Session, the declaration and the Stream reached the engine: there is
     * no importer, and this is what that means in practice. */
    CHECK(ppcp_peer_session_id(sink) != NULL);
    CHECK(ppcp_cbor_key_is(ppcp_peer_timebase_ref(sink)->v,
                           ppcp_peer_timebase_ref(sink)->len, "tb:dev"));
    CHECK(ppcp_peer_counterpart(sink) != NULL);
    CHECK_EQ_I(ppcp_peer_counterpart(sink)->source_count, 1);
    CHECK_EQ_I(ppcp_peer_stream_count(sink), 1);
    CHECK(ppcp_peer_stream_find(sink, "st:1") != NULL);

    TEST("ENC 7d / I10 — an unasserted, untruncated bundle is `unknown`, not `complete`");
    CHECK_EQ_I(ppcp_bundle_reader_finish(r, &c), PPCP_OK);
    CHECK_EQ_I(c, PPCP_UNKNOWN);
    CHECK(!ppcp_bundle_reader_asserted(r, NULL));

    TEST("ENC 7f — a bundle whose `minor` exceeds the reader's own is accepted");
    {
        void               *r2m = NULL;
        ppcp_bundle_reader *r2;
        bundle.b[10] = 0;
        bundle.b[11] = 9;                /* minor = 9 */
        r2 = new_reader(&r2m, NULL);
        CHECK_EQ_I(ppcp_bundle_reader_feed(r2, bundle.b, bundle.n, &consumed), PPCP_OK);
        CHECK_EQ_I(consumed, bundle.n);
        CHECK_EQ_I(ppcp_bundle_reader_minor(r2), 9);
        free(r2m);
        bundle.b[11] = PPCP_BUNDLE_MINOR;
    }

    TEST("ENC §7 — a differing MAJOR is refused, and so is a wrong magic");
    {
        void               *r3m = NULL;
        ppcp_bundle_reader *r3 = new_reader(&r3m, NULL);
        uint8_t             hdr[PPCP_BUNDLE_HEADER_BYTES];
        memcpy(hdr, bundle.b, sizeof(hdr));
        hdr[9] = 2;                      /* major = 2 */
        CHECK_EQ_I(ppcp_bundle_reader_feed(r3, hdr, sizeof(hdr), &consumed),
                   PPCP_ERR_MALFORMED);
        free(r3m);
        r3 = new_reader(&r3m, NULL);
        memcpy(hdr, bundle.b, sizeof(hdr));
        hdr[0] = 'X';
        CHECK_EQ_I(ppcp_bundle_reader_feed(r3, hdr, sizeof(hdr), &consumed),
                   PPCP_ERR_MALFORMED);
        free(r3m);
    }

    TEST("the reader consumes whole frames only, one byte at a time");
    {
        void               *r4m = NULL;
        ppcp_bundle_reader *r4 = new_reader(&r4m, NULL);
        size_t              fed = 0, total = 0;
        while (fed < bundle.n) {
            size_t took = 0;
            fed++;
            CHECK_EQ_I(ppcp_bundle_reader_feed(r4, bundle.b + total, fed - total, &took),
                       PPCP_OK);
            total += took;
        }
        CHECK_EQ_I(total, bundle.n);
        CHECK_EQ_I(ppcp_bundle_reader_frame_count(r4), 10);
        free(r4m);
    }

    ppcp_peer_free(sink);
    free(sm);
    free(rm);
}

/* ============================================================ CT-I12 */

static void test_any_subset(void)
{
    static buf          bundle;
    void               *sm = NULL, *rm = NULL;
    ppcp_peer          *sink;
    ppcp_bundle_reader *r;
    size_t              consumed = 0;
    size_t              i;
    static const char *const kinds[] = {
        PPCP_STREAM_KIND_VIDEO, PPCP_STREAM_KIND_IMU, NULL
    };

    TEST("CT-I12 — video-only, IMU-only and an empty-Stream Session all load");
    for (i = 0; i < 3; i++) {
        write_session(&bundle, kinds[i], kinds[i] != NULL, false, PPCP_UNKNOWN);
        sink = new_sink(&sm);
        r    = new_reader(&rm, sink);
        CHECK_EQ_I(ppcp_test_reader_feed_all(r, sink, bundle.b, bundle.n, &consumed),
                   PPCP_OK);
        CHECK_EQ_I(consumed, bundle.n);
        CHECK(!ppcp_bundle_reader_truncated(r));
        CHECK(ppcp_peer_session_id(sink) != NULL);
        /* I12: the Stream count is 1, 1 and 0, and none of them is an error. */
        CHECK_EQ_I(ppcp_peer_stream_count(sink), (kinds[i] != NULL) ? 1 : 0);
        ppcp_peer_free(sink);
        free(sm);
        free(rm);
    }
}

/* ==================================================== ENC 7d and CT-I36 (c)(d) */

static void test_truncation(void)
{
    static buf          bundle;
    void               *rm = NULL;
    ppcp_bundle_reader *r;
    size_t              consumed = 0;
    ppcp_completeness   c;

    TEST("ENC 7d — a truncated tail with no assertion is `partial`");
    write_session(&bundle, PPCP_STREAM_KIND_VIDEO, true, false, PPCP_UNKNOWN);
    r = new_reader(&rm, NULL);
    /* Cut the last frame in half: the bundle stops mid-frame, which is what a
     * transfer that died looks like. */
    CHECK_EQ_I(ppcp_bundle_reader_feed(r, bundle.b, bundle.n - 6, &consumed), PPCP_OK);
    CHECK(consumed < bundle.n - 6);
    CHECK(ppcp_bundle_reader_truncated(r));
    CHECK_EQ_I(ppcp_bundle_reader_finish(r, &c), PPCP_OK);
    CHECK_EQ_I(c, PPCP_PARTIAL);
    free(rm);

    TEST("CT-I36 (c) — the same truncation in a Session asserted `partial` is the "
         "declared incompleteness, not a defect");
    write_session(&bundle, PPCP_STREAM_KIND_VIDEO, true, true, PPCP_PARTIAL);
    r = new_reader(&rm, NULL);
    CHECK_EQ_I(ppcp_bundle_reader_feed(r, bundle.b, bundle.n - 6, &consumed), PPCP_OK);
    CHECK(ppcp_bundle_reader_truncated(r));
    CHECK(ppcp_bundle_reader_asserted(r, &c));
    CHECK_EQ_I(c, PPCP_PARTIAL);
    CHECK_EQ_I(ppcp_bundle_reader_finish(r, &c), PPCP_OK);
    CHECK_EQ_I(c, PPCP_PARTIAL);
    free(rm);

    TEST("CT-I36 (d) — the same truncation in a Session asserted `complete` is a "
         "defect, and the two facts stay separable");
    write_session(&bundle, PPCP_STREAM_KIND_VIDEO, true, true, PPCP_COMPLETE);
    r = new_reader(&rm, NULL);
    CHECK_EQ_I(ppcp_bundle_reader_feed(r, bundle.b, bundle.n - 6, &consumed), PPCP_OK);
    /* I10: the owner asserted `complete`, and the reader does not overrule it
     * — but it does not hide the truncation either, which is what makes the
     * contradiction detectable rather than silently resolved. */
    CHECK(ppcp_bundle_reader_truncated(r));
    CHECK(ppcp_bundle_reader_asserted(r, &c));
    CHECK_EQ_I(c, PPCP_COMPLETE);
    CHECK_EQ_I(ppcp_bundle_reader_finish(r, &c), PPCP_OK);
    CHECK_EQ_I(c, PPCP_COMPLETE);
    free(rm);

    TEST("ENC 7d — a partial Session is never upgraded by a whole file");
    write_session(&bundle, PPCP_STREAM_KIND_VIDEO, true, true, PPCP_PARTIAL);
    r = new_reader(&rm, NULL);
    CHECK_EQ_I(ppcp_bundle_reader_feed(r, bundle.b, bundle.n, &consumed), PPCP_OK);
    CHECK_EQ_I(consumed, bundle.n);
    CHECK(!ppcp_bundle_reader_truncated(r));
    CHECK_EQ_I(ppcp_bundle_reader_finish(r, &c), PPCP_OK);
    CHECK_EQ_I(c, PPCP_PARTIAL);
    free(rm);
}

/* ============================================================== CT-I34 */

static void test_reimport(void)
{
    static buf          bundle;
    void               *rm = NULL, *r2m = NULL;
    ppcp_bundle_reader *r, *r2;
    size_t              consumed = 0;
    ppcp_capture_index  ix;
    ppcp_capture_key    key;
    bool                is_new = false;

    TEST("CT-I34 — the same bundle imported twice duplicates neither Capture");
    /* The bundle holds exactly the two the row names: a `complete` Capture
     * with `transfer: pending` and no digest, and an `absent` one that will
     * never have a digest at all. */
    write_session(&bundle, PPCP_STREAM_KIND_VIDEO, true, false, PPCP_UNKNOWN);
    r = new_reader(&rm, NULL);
    CHECK_EQ_I(ppcp_bundle_reader_feed(r, bundle.b, bundle.n, &consumed), PPCP_OK);
    CHECK_EQ_I(ppcp_capture_index_count(ppcp_bundle_reader_index(r)), 2);
    /* Feeding the very same bytes through the very same index again is the
     * "users connect twice" case, and it is a no-op. */
    r2 = new_reader(&r2m, NULL);
    *ppcp_bundle_reader_index(r2) = *ppcp_bundle_reader_index(r);
    CHECK_EQ_I(ppcp_bundle_reader_feed(r2, bundle.b, bundle.n, &consumed), PPCP_OK);
    CHECK_EQ_I(ppcp_capture_index_count(ppcp_bundle_reader_index(r2)), 2);
    free(rm);
    free(r2m);

    TEST("CT-I34 — identity is `Capture.id` scoped by session and owning peer");
    ppcp_capture_index_init(&ix);
    memset(&key, 0, sizeof(key));
    CHECK_EQ_I(ppcp_id_set_z(&key.session_id, "sess:1"), PPCP_OK);
    CHECK_EQ_I(ppcp_id_set_z(&key.peer_id, "peer:dev"), PPCP_OK);
    CHECK_EQ_I(ppcp_id_set_z(&key.capture_id, "cap:1"), PPCP_OK);
    CHECK_EQ_I(ppcp_capture_index_observe(&ix, &key, &is_new), PPCP_OK);
    CHECK(is_new);
    CHECK_EQ_I(ppcp_capture_index_observe(&ix, &key, &is_new), PPCP_OK);
    CHECK(!is_new);
    /* the same Capture id from a DIFFERENT peer is a different Capture ... */
    CHECK_EQ_I(ppcp_id_set_z(&key.peer_id, "peer:other"), PPCP_OK);
    CHECK_EQ_I(ppcp_capture_index_observe(&ix, &key, &is_new), PPCP_OK);
    CHECK(is_new);
    /* ... and so is the same id in a different Session */
    CHECK_EQ_I(ppcp_id_set_z(&key.session_id, "sess:2"), PPCP_OK);
    CHECK_EQ_I(ppcp_capture_index_observe(&ix, &key, &is_new), PPCP_OK);
    CHECK(is_new);
    CHECK_EQ_I(ppcp_capture_index_count(&ix), 3);

    TEST("CT-I34 — the digest is not the key: a Capture with no digest still "
         "de-duplicates");
    {
        /* An `absent` Capture never gets a digest and a `complete` + `pending`
         * one does not have it yet.  Keying on the digest would import both
         * twice, which is exactly what the row replays. */
        ppcp_capture a;
        CHECK_EQ_I(ppcp_capture_make_shot(&a, "cap:absent", "shot:1", "st:1",
                                          PPCP_ABSENT), PPCP_OK);
        CHECK_EQ_I(ppcp_capture_set_absent_reason(&a, PPCP_ABSENT_OUTSIDE_BUFFER),
                   PPCP_OK);
        CHECK(!a.digest.present);
        CHECK_EQ_I(ppcp_id_set_z(&key.capture_id, "cap:absent"), PPCP_OK);
        CHECK_EQ_I(ppcp_capture_index_observe(&ix, &key, &is_new), PPCP_OK);
        CHECK(is_new);
        CHECK_EQ_I(ppcp_capture_index_observe(&ix, &key, &is_new), PPCP_OK);
        CHECK(!is_new);
    }
}

/* ====================================== the writer takes what the engine sent */

static void test_frames_from_engine(void)
{
    static const char *const prof[] = {
        PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE, PPCP_PROFILE_MINT, PPCP_PROFILE_OFFLINE
    };
    ppcp_peer_config    cfg;
    ppcp_peer          *dev = NULL;
    void               *pm  = malloc(ppcp_peer_sizeof());
    void               *wm  = NULL;
    ppcp_bundle_writer *w;
    static buf          out;
    static uint8_t      drained[65536];
    ppcp_session        sess;
    size_t              n = 0, drained_len = 0;

    TEST("ENC 7a — the bytes a peer would have sent ARE the bundle's bytes");
    if (pm == NULL) abort();
    memset(&cfg, 0, sizeof(cfg));
    cfg.role          = PPCP_ROLE_CAPTURE;
    cfg.peer_id       = "peer:dev";
    cfg.profiles      = prof;
    cfg.profile_count = 4;
    CHECK_EQ_I(ppcp_peer_new(pm, ppcp_peer_sizeof(), &cfg, &dev), PPCP_OK);

    ppcp_instant opened_at_640;
    CHECK_EQ_I(ppcp_instant_make_z(&opened_at_640, "tb:dev", 0), PPCP_OK);
    CHECK_EQ_I(ppcp_session_make_hostless(&sess, "sess:1", "tb:dev", &opened_at_640), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_session_open(dev, &sess), PPCP_OK);
    {
        ppcp_readiness r;
        CHECK_EQ_I(ppcp_readiness_settled(&r), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_readiness(dev, &r, NULL, 0), PPCP_OK);
    }
    CHECK_EQ_I(ppcp_peer_drain(dev, PPCP_CHANNEL_CONTROL, drained, sizeof(drained),
                               &drained_len), PPCP_OK);
    CHECK(drained_len > 0);

    w = new_writer(&wm);
    out.n = 0;
    CHECK_EQ_I(ppcp_bundle_writer_begin(w, out.b, sizeof(out.b), &n), PPCP_OK);
    out.n += n;
    CHECK_EQ_I(ppcp_bundle_writer_append_frames(w, drained, drained_len, out.b + out.n,
                                                sizeof(out.b) - out.n, &n), PPCP_OK);
    /* byte-identical: the writer framed nothing, it admitted and copied */
    CHECK_EQ_I(n, drained_len);
    CHECK(memcmp(out.b + out.n, drained, drained_len) == 0);
    out.n += n;
    CHECK_EQ_I(ppcp_bundle_writer_frame_count(w), 2);
    CHECK(ppcp_bundle_writer_is_hostless(w));

    TEST("... and a half-frame from a partial drain is refused whole");
    CHECK_EQ_I(ppcp_bundle_writer_append_frames(w, drained, drained_len - 1,
                                                out.b + out.n, sizeof(out.b) - out.n, &n),
               PPCP_ERR_TRUNCATED);

    ppcp_peer_free(dev);
    free(pm);
    free(wm);
}

int main(void)
{
    test_writer_refusals();
    test_round_trip();
    test_any_subset();
    test_truncation();
    test_reimport();
    test_frames_from_engine();
    TEST_MAIN_END();
}
