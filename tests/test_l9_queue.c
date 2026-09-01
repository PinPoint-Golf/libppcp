/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * The four API gaps teams H and D reported in session S2, and the offline
 * session offer the user decided on — work package L9's queue (plan §9).
 *
 *   1. `ppcp_peer_drain()` had no partial-write counterpart, so a short socket
 *      write under CORE T2 backpressure lost bytes the engine considered sent.
 *   2. `session_manifest` had no originator, and there was no way to put a
 *      stored bundle onto a live link at all.
 *   3. `ppcp_link_binder_offer` asked a stream-per-connection listener for a
 *      channel number it does not have.  (Covered in test_ct_i24.)
 *   4. `ppcp_msg` is 48 KB and Swift imports its union as computed members.
 *      Documentation, in message.h; nothing to assert in C.
 *
 * Plus the flow they exist for: a connected device OFFERS its stored Sessions
 * and the host chooses (plan §9, 22 August 2026), and the accepted Session's
 * frames are replayed onto the live link honouring `have_digests` (9.1a).
 */
#include "ppcp/ppcp.h"

#include "test_util.h"

static ppcp_peer *make_peer(void **storage, ppcp_role role, const char *id,
                            const char *const *profiles, size_t nprof)
{
    ppcp_peer_config cfg;
    ppcp_peer       *p = NULL;
    void            *mem;

    memset(&cfg, 0, sizeof(cfg));
    cfg.role          = role;
    cfg.peer_id       = id;
    cfg.profiles      = profiles;
    cfg.profile_count = nprof;
    /* F-H5-3: Live is refused without one, and every rig here declares Live. */
    cfg.health_report = ppcp_test_health;

    mem = malloc(ppcp_peer_sizeof());
    if (mem == NULL)
        abort();
    if (ppcp_peer_new(mem, ppcp_peer_sizeof(), &cfg, &p) != PPCP_OK)
        abort();
    *storage = mem;
    return p;
}

static const char *const dev_profiles[] = {
    PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE, PPCP_PROFILE_OFFLINE, PPCP_PROFILE_LIVE
};
static const char *const host_profiles[] = {
    PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE, PPCP_PROFILE_OFFLINE, PPCP_PROFILE_ARBITRATE
};

/* ==================================== 1 — a short write must lose nothing */

static void test_partial_write(void)
{
    void      *mem = NULL;
    ppcp_peer *p   = make_peer(&mem, PPCP_ROLE_CAPTURE, "peer:dev", dev_profiles, 4);
    const uint8_t *view = NULL;
    size_t     len = 0, first = 0, n = 0;
    uint8_t    wire[65536];
    size_t     wire_len = 0;
    void      *rmem = NULL;
    ppcp_peer *rx;
    size_t     consumed = 0, events = 0;
    ppcp_event ev;

    TEST("H (S2) — the queue can be peeked without being dequeued");
    /* Three frames, so a short write can land inside the second. */
    CHECK_EQ_I(ppcp_peer_hello(p), PPCP_OK);
    {
        ppcp_session s;
        ppcp_instant opened_at_75;
        CHECK_EQ_I(ppcp_instant_make_z(&opened_at_75, "tb:dev", 0), PPCP_OK);
        CHECK_EQ_I(ppcp_session_make_hostless(&s, "sess:1", "tb:dev", &opened_at_75), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_session_open(p, &s), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_session_state(p, PPCP_SESSION_OPEN, PPCP_COMPLETE), PPCP_OK);
    }
    CHECK_EQ_I(ppcp_peer_drain_peek(p, PPCP_CHANNEL_CONTROL, &view, &len), PPCP_OK);
    CHECK(len > 0);
    CHECK_EQ_I(ppcp_peer_pending(p, PPCP_CHANNEL_CONTROL), len);
    /* Peek does not dequeue: the same bytes are still there. */
    {
        const uint8_t *again = NULL;
        size_t         again_len = 0;
        CHECK_EQ_I(ppcp_peer_drain_peek(p, PPCP_CHANNEL_CONTROL, &again, &again_len), PPCP_OK);
        CHECK_EQ_I(again_len, len);
        CHECK(again == view);
    }

    TEST("H (S2) — a short write commits exactly what was written, mid-frame");
    /* The transport took an awkward number of bytes: not zero, not everything,
     * and not a frame boundary. */
    first = 11;
    memcpy(wire, view, first);
    wire_len = first;
    CHECK_EQ_I(ppcp_peer_drain_commit(p, PPCP_CHANNEL_CONTROL, first), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_pending(p, PPCP_CHANNEL_CONTROL), len - first);
    CHECK(ppcp_peer_drain_is_partial(p, PPCP_CHANNEL_CONTROL));

    TEST("the two paths do not mix: ppcp_peer_drain() refuses a half-written head");
    {
        uint8_t scratch[64];
        size_t  got = 0;
        CHECK_EQ_I(ppcp_peer_drain(p, PPCP_CHANNEL_CONTROL, scratch, sizeof(scratch), &got),
                   PPCP_ERR_INVALID);
    }

    TEST("committing more than is queued is refused, not silently clamped");
    CHECK_EQ_I(ppcp_peer_drain_commit(p, PPCP_CHANNEL_CONTROL,
                                      ppcp_peer_pending(p, PPCP_CHANNEL_CONTROL) + 1),
               PPCP_ERR_INVALID);

    TEST("the remainder follows, and the counterpart sees three whole frames");
    while (ppcp_peer_pending(p, PPCP_CHANNEL_CONTROL) > 0) {
        size_t take;
        CHECK_EQ_I(ppcp_peer_drain_peek(p, PPCP_CHANNEL_CONTROL, &view, &len), PPCP_OK);
        take = (len > 7) ? 7 : len;          /* a stubbornly short transport */
        memcpy(wire + wire_len, view, take);
        wire_len += take;
        CHECK_EQ_I(ppcp_peer_drain_commit(p, PPCP_CHANNEL_CONTROL, take), PPCP_OK);
    }
    CHECK(!ppcp_peer_drain_is_partial(p, PPCP_CHANNEL_CONTROL));

    rx = make_peer(&rmem, PPCP_ROLE_HOST, "peer:host", host_profiles, 4);
    /* F-L13-1: three frames raise four events past a four-deep ring, so the
     * feed stops and the caller drains.  Counting them here is the point of
     * the check below, so the loop is written out rather than borrowed. */
    {
        size_t off = 0;
        while (off < wire_len) {
            CHECK_EQ_I(ppcp_peer_feed(rx, PPCP_CHANNEL_CONTROL, wire + off,
                                      wire_len - off, &consumed), PPCP_OK);
            off += consumed;
            while (ppcp_peer_next_event(rx, &ev) == PPCP_OK)
                events++;
            if (consumed == 0 && !ppcp_peer_feed_stalled(rx))
                break;
        }
        CHECK_EQ_I(off, wire_len);           /* nothing lost, nothing duplicated */
    }
    CHECK(events >= 3);

    TEST("after a whole write, drain() works again");
    CHECK_EQ_I(ppcp_peer_session_state(p, PPCP_SESSION_CLOSED, PPCP_COMPLETE), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_drain(p, PPCP_CHANNEL_CONTROL, wire, sizeof(wire), &n), PPCP_OK);
    CHECK(n > 0);

    ppcp_peer_free(rx);
    free(rmem);
    ppcp_peer_free(p);
    free(mem);
}

/* ======================= 2 — the offer, the manifest, and the bundle replay */

/* Builds a bundle in memory the way a hostless device would have recorded one:
 * hostless `session_open`, a Stream, a `session_manifest` (ENC 7c), two
 * `capture_announce`s and a payload for each. */
typedef struct made_bundle {
    uint8_t     bytes[32768];
    size_t      len;
    ppcp_digest d0, d1;
} made_bundle;

static void digest_of(ppcp_digest *d, uint8_t fill)
{
    uint8_t raw[PPCP_SHA256_BYTES];
    memset(raw, fill, sizeof(raw));
    if (ppcp_digest_set(d, raw) != PPCP_OK)
        abort();
}

static void append(made_bundle *b, ppcp_bundle_writer *w, uint8_t ch, const ppcp_msg *m)
{
    size_t got = 0;
    if (ppcp_bundle_writer_append_msg(w, ch, m, b->bytes + b->len,
                                      sizeof(b->bytes) - b->len, &got) != PPCP_OK)
        abort();
    b->len += got;
}

static void build_bundle(made_bundle *b)
{
    void               *wmem = malloc(ppcp_bundle_writer_sizeof());
    ppcp_bundle_writer *w = NULL;
    ppcp_msg            m;
    ppcp_stream         st;
    ppcp_capture        cap;
    ppcp_interval       iv;
    size_t              got = 0;
    uint64_t            mid = 1;

    memset(b, 0, sizeof(*b));
    digest_of(&b->d0, 0xA0);
    digest_of(&b->d1, 0xB0);

    if (wmem == NULL || ppcp_bundle_writer_new(wmem, ppcp_bundle_writer_sizeof(), &w) != PPCP_OK)
        abort();
    if (ppcp_bundle_writer_begin(w, b->bytes, sizeof(b->bytes), &got) != PPCP_OK)
        abort();
    b->len = got;

    /* hostless `session_open` — 4.1d/5.10e: neither arbitration parameter */
    if (ppcp_msg_init(&m, PPCP_MT_SESSION_OPEN, mid++) != PPCP_OK) abort();
    if (ppcp_id_set_z(&m.body.session_open.session_id, "sess:stored") != PPCP_OK) abort();
    if (ppcp_id_set_z(&m.body.session_open.timebase_ref, "tb:dev") != PPCP_OK) abort();
    if (ppcp_msg_set_session_id(&m, "sess:stored") != PPCP_OK) abort();
    append(b, w, PPCP_CHANNEL_CONTROL, &m);

    /* ENC 7h (erratum E9): `declare` before any frame naming a Stream or a
     * Capture.  8.5c scopes Capture identity by the minting peer's `Peer.id`,
     * and a bundle states that nowhere else — so without this the file the
     * importer replays is unattributable and un-deduplicable (I34). */
    {
        static const char *const prof[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE,
                                            PPCP_PROFILE_MINT, PPCP_PROFILE_OFFLINE };
        ppcp_id        profiles[4];
        ppcp_timebase  tb;
        ppcp_peer_desc pd;
        size_t         i;
        for (i = 0; i < 4; i++)
            if (ppcp_id_set_z(&profiles[i], prof[i]) != PPCP_OK) abort();
        if (ppcp_timebase_make(&tb, "tb:dev", strlen("tb:dev"), PPCP_TB_CONTINUOUS,
                               true, 1000) != PPCP_OK) abort();
        if (ppcp_peer_desc_make(&pd, "peer:dev", PPCP_ROLE_CAPTURE, "1.0",
                                profiles, 4, &tb, 1) != PPCP_OK) abort();
        if (ppcp_msg_init(&m, PPCP_MT_DECLARE, mid++) != PPCP_OK) abort();
        m.body.declare.generation = 1;
        m.body.declare.peer       = pd;
        if (ppcp_msg_set_session_id(&m, "sess:stored") != PPCP_OK) abort();
        append(b, w, PPCP_CHANNEL_CONTROL, &m);
    }

    {
        ppcp_instant opened;
        if (ppcp_instant_make_z(&opened, "tb:dev", 0) != PPCP_OK) abort();
        if (ppcp_stream_make(&st, "stream:video", "sess:stored", "src:1",
                             PPCP_STREAM_KIND_VIDEO, "cp:1", "tb:dev",
                             PPCP_CONTINUOUS, &opened) != PPCP_OK) abort();
    }
    if (ppcp_msg_init(&m, PPCP_MT_STREAM_OPEN, mid++) != PPCP_OK) abort();
    m.body.stream_open.stream = st;
    if (ppcp_msg_set_session_id(&m, "sess:stored") != PPCP_OK) abort();
    append(b, w, PPCP_CHANNEL_CONTROL, &m);

    /* ENC 7c — the manifest before any payload frame. */
    if (ppcp_msg_init(&m, PPCP_MT_SESSION_MANIFEST, mid++) != PPCP_OK) abort();
    if (ppcp_id_set_z(&m.body.session_manifest.session_id, "sess:stored") != PPCP_OK) abort();
    if (ppcp_msg_set_session_id(&m, "sess:stored") != PPCP_OK) abort();
    if (ppcp_id_set_z(&m.body.session_manifest.streams[0], "stream:video") != PPCP_OK) abort();
    m.body.session_manifest.stream_count = 1;
    if (ppcp_id_set_z(&m.body.session_manifest.captures[0].capture_id, "cap:0") != PPCP_OK)
        abort();
    m.body.session_manifest.captures[0].digest = b->d0;
    m.body.session_manifest.captures[0].bytes  = 1024;
    if (ppcp_id_set_z(&m.body.session_manifest.captures[0].stream_id, "stream:video")
        != PPCP_OK) abort();
    if (ppcp_id_set_z(&m.body.session_manifest.captures[1].capture_id, "cap:1") != PPCP_OK)
        abort();
    m.body.session_manifest.captures[1].digest = b->d1;
    m.body.session_manifest.captures[1].bytes  = 2048;
    if (ppcp_id_set_z(&m.body.session_manifest.captures[1].stream_id, "stream:video")
        != PPCP_OK) abort();
    m.body.session_manifest.capture_count = 2;
    m.body.session_manifest.completeness  = PPCP_COMPLETE;
    m.body.session_manifest.count_captures = 2;
    append(b, w, PPCP_CHANNEL_CONTROL, &m);

    {
        size_t k;
        for (k = 0; k < 2; k++) {
            const char *cid = (k == 0) ? "cap:0" : "cap:1";
            if (ppcp_interval_make(&iv, "tb:dev", strlen("tb:dev"),
                                   (int64_t)k * 1000 + 1000,
                                   (int64_t)k * 1000 + 2000) != PPCP_OK)
                abort();
            if (ppcp_capture_make_segment(&cap, cid, "stream:video", PPCP_COMPLETE, &iv)
                != PPCP_OK) abort();
            if (ppcp_capture_set_digest(&cap, (k == 0) ? &b->d0 : &b->d1,
                                        (k == 0) ? 1024u : 2048u) != PPCP_OK) abort();
            if (ppcp_msg_init(&m, PPCP_MT_CAPTURE_ANNOUNCE, mid++) != PPCP_OK) abort();
            m.body.capture_announce.capture = cap;
            if (ppcp_msg_set_session_id(&m, "sess:stored") != PPCP_OK) abort();
            append(b, w, PPCP_CHANNEL_CONTROL, &m);

            if (ppcp_msg_init(&m, PPCP_MT_PAYLOAD_BEGIN, mid++) != PPCP_OK) abort();
            if (ppcp_id_set_z(&m.body.payload_begin.capture_id, cid) != PPCP_OK) abort();
            m.body.payload_begin.bytes       = (k == 0) ? 1024u : 2048u;
            m.body.payload_begin.digest      = (k == 0) ? b->d0 : b->d1;
            m.body.payload_begin.chunk_bytes = 1024;
            if (ppcp_msg_set_session_id(&m, "sess:stored") != PPCP_OK) abort();
            append(b, w, PPCP_CHANNEL_BULK, &m);

            if (ppcp_msg_init(&m, PPCP_MT_PAYLOAD_END, mid++) != PPCP_OK) abort();
            if (ppcp_id_set_z(&m.body.payload_end.capture_id, cid) != PPCP_OK) abort();
            m.body.payload_end.digest = (k == 0) ? b->d0 : b->d1;
            if (ppcp_msg_set_session_id(&m, "sess:stored") != PPCP_OK) abort();
            append(b, w, PPCP_CHANNEL_BULK, &m);
        }
    }
    if (!ppcp_bundle_writer_is_hostless(w)) abort();
    if (!ppcp_bundle_writer_has_manifest(w)) abort();
    free(wmem);
}

static void test_offer_and_replay(void)
{
    void       *dmem = NULL, *hmem = NULL;
    ppcp_peer  *dev  = make_peer(&dmem, PPCP_ROLE_CAPTURE, "peer:dev", dev_profiles, 4);
    ppcp_peer  *host = make_peer(&hmem, PPCP_ROLE_HOST, "peer:host", host_profiles, 4);
    made_bundle bundle;
    ppcp_body_session_offer  offer;
    ppcp_body_session_accept accept;
    ppcp_event  ev;
    uint8_t     wire[131072];
    size_t      n = 0, consumed = 0;
    uint64_t    offer_msg_id = 0;

    build_bundle(&bundle);

    TEST("MSG 9.1 — the DEVICE offers a stored Session; the host chooses");
    memset(&offer, 0, sizeof(offer));
    CHECK_EQ_I(ppcp_id_set_z(&offer.session_id, "sess:stored"), PPCP_OK);
    CHECK_EQ_I(ppcp_id_set_z(&offer.minting_peer_id, "peer:dev"), PPCP_OK);
    offer.completeness       = PPCP_COMPLETE;
    offer.has_bytes_estimate = true;
    offer.bytes_estimate     = 3072;
    CHECK_EQ_I(ppcp_peer_session_offer(dev, &offer), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_drain(dev, PPCP_CHANNEL_CONTROL, wire, sizeof(wire), &n), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_feed(host, PPCP_CHANNEL_CONTROL, wire, n, &consumed), PPCP_OK);
    {
        bool saw = false;
        while (ppcp_peer_next_event(host, &ev) == PPCP_OK) {
            if (ev.kind == PPCP_EVENT_SESSION_OFFER && ev.msg != NULL) {
                saw          = true;
                offer_msg_id = ev.msg->env.msg_id;
                CHECK(ppcp_cbor_key_is(ev.msg->body.session_offer.minting_peer_id.v,
                                       ev.msg->body.session_offer.minting_peer_id.len,
                                       "peer:dev"));
            }
        }
        CHECK(saw);
    }

    TEST("9.1a — the host accepts and names the payloads it already holds");
    memset(&accept, 0, sizeof(accept));
    CHECK_EQ_I(ppcp_id_set_z(&accept.session_id, "sess:stored"), PPCP_OK);
    accept.verdict           = PPCP_OFFER_ACCEPT;
    accept.have_digests[0]   = bundle.d0;         /* cap:0 is already in the studio */
    accept.have_digest_count = 1;
    CHECK_EQ_I(ppcp_peer_session_accept(host, &accept, offer_msg_id), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_drain(host, PPCP_CHANNEL_CONTROL, wire, sizeof(wire), &n), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_feed(dev, PPCP_CHANNEL_CONTROL, wire, n, &consumed), PPCP_OK);
    {
        bool saw = false;
        while (ppcp_peer_next_event(dev, &ev) == PPCP_OK)
            if (ev.kind == PPCP_EVENT_SESSION_ACCEPT && ev.msg != NULL) {
                saw = true;
                CHECK_EQ_I(ev.msg->body.session_accept.verdict, PPCP_OFFER_ACCEPT);
                CHECK_EQ_I(ev.msg->body.session_accept.have_digest_count, 1);
                CHECK_EQ_I(ev.msg->env.reply_to, offer_msg_id);
            }
        CHECK(saw);
    }

    TEST("ENC 7a — the stored bundle's frames go onto the LIVE link, unchanged");
    {
        void               *rmem = malloc(ppcp_bundle_replay_sizeof());
        ppcp_bundle_replay *rp   = NULL;
        size_t              off = 0, fed = 0;
        size_t              got_manifest = 0, got_announce = 0, got_payload = 0;

        CHECK(rmem != NULL);
        CHECK_EQ_I(ppcp_bundle_replay_new(rmem, ppcp_bundle_replay_sizeof(), dev,
                                          accept.have_digests, accept.have_digest_count,
                                          &rp), PPCP_OK);
        /* Fed in slices, with a drain between them: `feed` reports what it
         * took and the caller re-presents the tail, exactly as
         * ppcp_peer_feed() does.  A whole session never sits in the engine. */
        while (off < bundle.len) {
            uint8_t ch;
            CHECK_EQ_I(ppcp_bundle_replay_feed(rp, bundle.bytes + off,
                                               bundle.len - off, &fed), PPCP_OK);
            CHECK(fed > 0);
            off += fed;
            for (ch = 0; ch < 2; ch++) {
                while (ppcp_peer_pending(dev, ch) > 0) {
                    size_t hoff = 0;
                    CHECK_EQ_I(ppcp_peer_drain(dev, ch, wire, sizeof(wire), &n), PPCP_OK);
                    /* F-L13-1: one drain can carry more frames than the event
                     * ring is deep, so feed and drain until the bytes are gone
                     * rather than letting the ring lose the earliest — which is
                     * how a replayed Session used to lose `capture_announce`,
                     * the very thing this test counts. */
                    while (hoff < n) {
                        CHECK_EQ_I(ppcp_peer_feed(host, ch, wire + hoff, n - hoff,
                                                  &consumed), PPCP_OK);
                        hoff += consumed;
                        while (ppcp_peer_next_event(host, &ev) == PPCP_OK) {
                            if (ev.msg == NULL)
                                continue;
                            if (ev.msg->type == PPCP_MT_SESSION_MANIFEST) got_manifest++;
                            if (ev.msg->type == PPCP_MT_CAPTURE_ANNOUNCE) got_announce++;
                            if (ev.msg->type == PPCP_MT_PAYLOAD_BEGIN ||
                                ev.msg->type == PPCP_MT_PAYLOAD_END)      got_payload++;
                        }
                        if (consumed == 0 && !ppcp_peer_feed_stalled(host))
                            break;
                    }
                }
            }
        }

        TEST("9.1a — the held Capture's PAYLOAD is skipped; its announce is not");
        CHECK_EQ_I(ppcp_bundle_replay_held_count(rp), 1);
        CHECK_EQ_I(ppcp_bundle_replay_skipped(rp), 2);   /* cap:0's begin and end */
        CHECK_EQ_I(got_manifest, 1);
        CHECK_EQ_I(got_announce, 2);                     /* both Captures still announced */
        CHECK_EQ_I(got_payload, 2);                      /* only cap:1's two frames */
        CHECK(ppcp_bundle_replay_sent(rp) > 0);

        TEST("the host holds both Captures, one of them without a re-transfer");
        {
            ppcp_id c0, c1;
            CHECK_EQ_I(ppcp_id_set_z(&c0, "cap:0"), PPCP_OK);
            CHECK_EQ_I(ppcp_id_set_z(&c1, "cap:1"), PPCP_OK);
            CHECK(ppcp_transfer_find(ppcp_peer_transfers(host), &c0) != NULL);
            CHECK(ppcp_transfer_find(ppcp_peer_transfers(host), &c1) != NULL);
        }
        free(rmem);
    }

    TEST("MSG 9.2 — `session_manifest` has an originator of its own now");
    {
        ppcp_body_session_manifest man;
        memset(&man, 0, sizeof(man));
        CHECK_EQ_I(ppcp_id_set_z(&man.session_id, "sess:stored"), PPCP_OK);
        man.completeness = PPCP_COMPLETE;
        CHECK_EQ_I(ppcp_peer_session_manifest(dev, &man), PPCP_OK);
        CHECK(ppcp_peer_pending(dev, PPCP_CHANNEL_CONTROL) > 0);
    }

    TEST("C2 — a peer that does not declare Offline cannot originate any of the three");
    {
        static const char *const core_only[] = { PPCP_PROFILE_CORE };
        void      *cmem = NULL;
        ppcp_peer *core = make_peer(&cmem, PPCP_ROLE_CAPTURE, "peer:core", core_only, 1);
        ppcp_body_session_manifest man;
        memset(&man, 0, sizeof(man));
        CHECK_EQ_I(ppcp_id_set_z(&man.session_id, "sess:stored"), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_session_offer(core, &offer), PPCP_ERR_INVALID);
        CHECK_EQ_I(ppcp_peer_session_manifest(core, &man), PPCP_ERR_INVALID);
        ppcp_peer_free(core);
        free(cmem);
    }

    ppcp_peer_free(dev);
    free(dmem);
    ppcp_peer_free(host);
    free(hmem);
}

int main(void)
{
    test_partial_write();
    test_offer_and_replay();
    TEST_MAIN_END();
}
