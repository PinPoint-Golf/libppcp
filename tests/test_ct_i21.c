/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * Clock synchronisation and liveness, driven end to end — work package L9's
 * evidence.  Two engines back to back through byte buffers, with the
 * simulated clock of CONF 2a supplying every instant, so an offset and a skew
 * are injected rather than waited for.
 *
 * Rows this file carries:
 *
 *   CT-I18 / CT-S5   relations are measured, never composed.  A peer holding
 *                    A→B and B→C does not emit A→C; asked for it, it measures
 *                    it, and a fresh probe sequence is observable on the wire.
 *   CT-I21           a HOST with two timebases runs a probe sequence per
 *                    timebase and declares both relations.  Asserted against
 *                    the host, which is the half CT-S5 assertion 4 exists for.
 *   CT-S4 (7)        the host-unreachable half: three missed heartbeats is a
 *                    lost link, and Session.timebase_ref, coincidence_window_ns
 *                    and issue_hold_ns are unchanged across it (8.3g).
 */
#include "ppcp/peer.h"

#include "test_util.h"

/* ------------------------------------------------------------------ rigging */

/* A clock the test drives.  Several timebases from one object, each with its
 * own injected offset and skew, so a peer with three clocks is three entries
 * rather than three objects. */
#define RIG_MAX_TB 5

typedef struct rig_clock {
    struct {
        char    id[32];
        int64_t base_ns;
        double  skew_ppm;
    } tb[RIG_MAX_TB];
    size_t  count;
    int64_t now_ns;      /* the rig's own tick, advanced by the test */
} rig_clock;

static void rig_add(rig_clock *c, const char *id, int64_t base_ns, double skew_ppm)
{
    size_t n = c->count++;
    if (n >= RIG_MAX_TB)
        abort();
    memset(c->tb[n].id, 0, sizeof(c->tb[n].id));
    memcpy(c->tb[n].id, id, strlen(id));
    c->tb[n].base_ns  = base_ns;
    c->tb[n].skew_ppm = skew_ppm;
}

static ppcp_result rig_now(void *ctx, const char *timebase_id, int64_t *out_ns)
{
    rig_clock *c = (rig_clock *)ctx;
    size_t     i;
    for (i = 0; i < c->count; i++) {
        if (strcmp(c->tb[i].id, timebase_id) == 0) {
            double drift = (double)c->now_ns * c->tb[i].skew_ppm * 1.0e-6;
            *out_ns = c->tb[i].base_ns + c->now_ns +
                      ((drift >= 0.0) ? (int64_t)(drift + 0.5) : -(int64_t)(-drift + 0.5));
            return PPCP_OK;
        }
    }
    return PPCP_ERR_NOT_FOUND;
}

static ppcp_clock rig_interface(rig_clock *c)
{
    ppcp_clock k;
    k.now = rig_now;
    k.ctx = c;
    return k;
}

static ppcp_result health_ok(void *ctx, ppcp_health *out)
{
    (void)ctx;
    memset(out, 0, sizeof(*out));
    out->thermal            = PPCP_THERMAL_NOMINAL;
    out->storage_free_bytes = 42u * 1024u * 1024u * 1024u;
    out->has_battery_pct    = true;
    out->battery_pct        = 88;
    return PPCP_OK;
}

typedef struct rig_peer {
    void      *mem;
    ppcp_peer *p;
    rig_clock  clock;
} rig_peer;

static void rig_peer_new(rig_peer *rp, ppcp_role role, const char *id,
                         const char *const *profiles, size_t nprof,
                         const char *sync_tb, bool with_health)
{
    ppcp_peer_config cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.role           = role;
    cfg.peer_id        = id;
    cfg.profiles       = profiles;
    cfg.profile_count  = nprof;
    cfg.clock          = rig_interface(&rp->clock);
    cfg.sync_timebase  = sync_tb;
    cfg.health_report  = with_health ? health_ok : NULL;

    rp->mem = malloc(ppcp_peer_sizeof());
    if (rp->mem == NULL)
        abort();
    if (ppcp_peer_new(rp->mem, ppcp_peer_sizeof(), &cfg, &rp->p) != PPCP_OK)
        abort();
}

static void rig_peer_free(rig_peer *rp)
{
    ppcp_peer_free(rp->p);
    free(rp->mem);
}

/* The transport: everything `from` has queued on `ch`, handed to `to`. */
static size_t pump(ppcp_peer *from, ppcp_peer *to, uint8_t ch)
{
    static uint8_t buf[131072];
    size_t         n = 0, consumed = 0, total = 0;

    while (ppcp_peer_pending(from, ch) > 0) {
        if (ppcp_peer_drain(from, ch, buf, sizeof(buf), &n) != PPCP_OK || n == 0)
            break;
        (void)ppcp_peer_feed(to, ch, buf, n, &consumed);
        total += n;
    }
    return total;
}

static void drop_events(ppcp_peer *p)
{
    ppcp_event e;
    while (ppcp_peer_next_event(p, &e) == PPCP_OK) { }
}

static size_t count_probes(ppcp_peer *from, uint8_t ch)
{
    /* Counts `sync_probe` frames in what is queued, WITHOUT consuming them —
     * "a fresh probe sequence is observable on the wire" (CT-S5 assertion 2)
     * is an assertion about the wire, so it is made on the bytes. */
    const uint8_t *bytes = NULL;
    size_t         len = 0, off = 0, probes = 0;
    if (ppcp_peer_drain_peek(from, ch, &bytes, &len) != PPCP_OK)
        return 0;
    while (off + PPCP_FRAME_HEADER_BYTES <= len) {
        ppcp_frame_header hdr;
        const uint8_t    *payload = NULL;
        size_t            consumed = 0;
        ppcp_msg          m;
        if (ppcp_frame_header_parse(bytes + off, &hdr) != PPCP_OK)
            break;
        if (ppcp_frame_read(bytes + off, len - off, &hdr, &payload, &consumed) != PPCP_OK)
            break;
        memset(&m, 0, sizeof(m));
        if (ppcp_msg_decode(payload, hdr.payload_len,
                            ppcp_cbor_limits_for_channel(hdr.channel), NULL, &m) == PPCP_OK &&
            m.type == PPCP_MT_SYNC_PROBE)
            probes++;
        off += consumed;
    }
    return probes;
}

/* Runs `rounds` complete exchanges between a prober and a responder, advancing
 * the rig clock by `step_ns` between them. */
#define RIG_LEG_NS 200000     /* 200 us each way — a symmetric link */

static void advance(rig_peer *a, rig_peer *b, int64_t d)
{
    a->clock.now_ns += d;
    b->clock.now_ns  = a->clock.now_ns;
}

static int rig_round;

static void exchange(rig_peer *prober, rig_peer *responder, int rounds, int64_t step_ns)
{
    int i;
    for (i = 0; i < rounds; i++) {
        /* Jitter on both legs, symmetric in the mean.  Without it the link is
         * noiseless, minimum-RTT filtering has nothing to filter, and the
         * declared sigmas are honestly zero — which is true of no network. */
        int64_t out_leg = RIG_LEG_NS + (int64_t)(rig_round % 7) * 40000;
        int64_t in_leg  = RIG_LEG_NS + (int64_t)((rig_round * 3) % 5) * 40000;
        size_t  probes  = 0;
        rig_round++;
        (void)ppcp_peer_sync_pump(prober->p, prober->clock.now_ns, &probes);
        advance(prober, responder, out_leg);
        pump(prober->p, responder->p, PPCP_CHANNEL_CONTROL);
        advance(prober, responder, in_leg);
        pump(responder->p, prober->p, PPCP_CHANNEL_CONTROL);
        advance(prober, responder, step_ns - out_leg - in_leg);
        drop_events(prober->p);
        drop_events(responder->p);
    }
}

/* ================================================== CT-I18 / CT-S5 / CT-I21 */

static void test_sync(void)
{
    static const char *const prof[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_LIVE };
    rig_peer host, device;
    const ppcp_sync_estimator *e;
    ppcp_timebase_relation rel;
    size_t i;

    /* The host owns THREE clocks — CT-I18 asks for three timebases and CT-I21
     * asks that the assertion be made against a HOST, because I21 binds every
     * multi-clock peer and hosts included (5.4.1b).  `tb:hostA` is also the
     * clock its network stack stamps replies on, which is CT-S5 assertion 3:
     * the camera is on B (and C) while the network is on A, and the peer runs
     * the exchange per timebase rather than composing B→A with A→dev. */
    memset(&host, 0, sizeof(host));
    rig_add(&host.clock, "tb:hostA", 1000000000, 0.0);
    rig_add(&host.clock, "tb:hostB", 7000000000, 3.0);
    rig_add(&host.clock, "tb:hostC", 3000000000, -7.0);
    rig_peer_new(&host, PPCP_ROLE_HOST, "peer:host", prof, 2, "tb:hostA", true);

    memset(&device, 0, sizeof(device));
    /* +12.5 ms of offset and +20 ppm of skew — the measured cross-device
     * figure CORE §6.3 quotes. */
    rig_add(&device.clock, "tb:dev", 1012500000, 20.0);
    rig_peer_new(&device, PPCP_ROLE_CAPTURE, "peer:dev", prof, 2, "tb:dev", true);

    TEST("CT-I18 / 6.1d — one probe sequence per LOCAL timebase, three of them, on a HOST");
    CHECK_EQ_I(ppcp_peer_sync_add_timebase(host.p, "tb:hostA", NULL), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_sync_add_timebase(host.p, "tb:hostB", NULL), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_sync_add_timebase(host.p, "tb:hostC", NULL), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_sync_count(host.p), 3);
    /* A second sequence for one timebase is not a second measurement. */
    CHECK_EQ_I(ppcp_peer_sync_add_timebase(host.p, "tb:hostA", NULL), PPCP_ERR_INVALID);

    TEST("6.3c — a connect burst is 10-20 exchanges, and both timebases get one");
    CHECK_EQ_I(ppcp_peer_sync_trigger(host.p, PPCP_SYNC_ON_CONNECT), PPCP_OK);
    CHECK(PPCP_SYNC_BURST >= 10u && PPCP_SYNC_BURST <= 20u);
    {
        size_t probes = 0;
        (void)ppcp_peer_sync_pump(host.p, host.clock.now_ns, &probes);
        CHECK_EQ_I(probes, 3);                 /* one per timebase, not one in total */
        CHECK_EQ_I(count_probes(host.p, PPCP_CHANNEL_CONTROL), 3);
    }
    advance(&host, &device, RIG_LEG_NS);
    pump(host.p, device.p, PPCP_CHANNEL_CONTROL);
    advance(&host, &device, RIG_LEG_NS);
    pump(device.p, host.p, PPCP_CHANNEL_CONTROL);
    drop_events(host.p);
    drop_events(device.p);

    TEST("6.3a — a one-shot handshake yields no relation: rate as well as offset");
    e = ppcp_peer_sync_estimator_for(host.p, "tb:hostA");
    CHECK(e != NULL);
    CHECK_EQ_I(ppcp_sync_estimator_count(e), 1);
    CHECK_EQ_I(ppcp_sync_estimator_relation(e, &rel), PPCP_ERR_NOT_FOUND);

    TEST("6.3a/6.3f — offset and skew are both measured, from the exchange alone");
    exchange(&host, &device, 40, 2000000000);  /* 40 exchanges over 80 s */
    e = ppcp_peer_sync_estimator_for(host.p, "tb:hostA");
    CHECK(ppcp_sync_estimator_has_estimate(e));
    CHECK_EQ_I(ppcp_sync_estimator_relation(e, &rel), PPCP_OK);
    CHECK_EQ_I(rel.cls, PPCP_REL_AFFINE);
    CHECK(ppcp_cbor_key_is(rel.from.v, rel.from.len, "tb:hostA"));
    CHECK(ppcp_cbor_key_is(rel.to.v, rel.to.len, "tb:dev"));
    CHECK_EQ_I(rel.method, PPCP_RELM_ESTIMATED_ONLINE);
    /* The injected offset is 12.5 ms at the rig's origin and grows at 20 ppm,
     * so the expected value depends on WHEN the relation says it was observed
     * — which is the whole reason `observed_at` is on the wire.  Recovering it
     * to within a millisecond is the estimator working; the exact value is not
     * the assertion, and CONF §6 says accuracy is not what this suite tests. */
    {
        int64_t elapsed  = rel.observed_at.ns - 1000000000;   /* tb:hostA base */
        int64_t expected = 12500000 + (int64_t)((double)elapsed * 20.0e-6);
        int64_t err      = rel.offset_ns - expected;
        CHECK(err > -1000000 && err < 1000000);
        CHECK(rel.skew_ppm > 15.0 && rel.skew_ppm < 25.0);
    }

    TEST("5.4a / I3 — both sigmas travel, and both say what the link did");
    /* A jittered link has a dispersion and both sigmas carry it.  A noiseless
     * link would honestly declare zero, which is why the assertion is that
     * they are MEASURED rather than that they are large. */
    CHECK(rel.offset_sigma_ns > 0.0);
    CHECK(rel.skew_sigma_ppm > 0.0);
    CHECK_EQ_I(ppcp_relation_validate(&rel), PPCP_OK);
    /* 5.4: `observed_at` is expressed in `from`. */
    CHECK(ppcp_id_equal(&rel.observed_at.tb, &rel.from));

    TEST("6.3e — the estimate is filtered, never stepped");
    {
        int64_t before = rel.offset_ns;
        int64_t after;
        /* One exchange claiming an offset 40 ms away from every one before
         * it.  A stepping estimator adopts it; this one moves a fraction of
         * the way — partly because the fit is over a window and partly
         * because the published value is filtered on top of the fit — and
         * that fraction is what keeps a discontinuity out of fused output. */
        ppcp_peer_sync_observe(host.p, "tb:hostA",
                               host.clock.now_ns + 1000000,
                               host.clock.now_ns + 1000000 + 12500000 + 40000000,
                               host.clock.now_ns + 1000000 + 12500000 + 40000000,
                               host.clock.now_ns + 2000000);
        e = ppcp_peer_sync_estimator_for(host.p, "tb:hostA");
        CHECK_EQ_I(ppcp_sync_estimator_relation(e, &rel), PPCP_OK);
        after = rel.offset_ns;
        CHECK((after - before) < 40000000 / 2);
    }

    TEST("CT-S5 (1) / I18 — A->B and B->C held; A->C is NOT emitted");
    {
        /* The host holds tb:hostA -> tb:dev (measured above).  Give it
         * tb:dev -> tb:other as well, which is B->C. */
        ppcp_relation_set *rs = ppcp_peer_relations(host.p);
        ppcp_timebase_relation bc;
        ppcp_instant           at;
        ppcp_instant           a_now, out;
        CHECK_EQ_I(ppcp_instant_make_z(&at, "tb:dev", 0), PPCP_OK);
        CHECK_EQ_I(ppcp_relation_make_affine(&bc, "tb:dev", "tb:other", 500, 0.0, 1.0, 0.1,
                                             PPCP_RELM_MEASURED, &at), PPCP_OK);
        CHECK_EQ_I(ppcp_relations_put(rs, &rel), PPCP_OK);
        CHECK_EQ_I(ppcp_relations_put(rs, &bc), PPCP_OK);
        /* Directly: there is no A->C in the set and nothing derives one. */
        {
            ppcp_id a, c;
            CHECK_EQ_I(ppcp_id_set_z(&a, "tb:hostA"), PPCP_OK);
            CHECK_EQ_I(ppcp_id_set_z(&c, "tb:other"), PPCP_OK);
            CHECK(ppcp_relations_find(rs, &a, &c) == NULL);
            CHECK_EQ_I(ppcp_instant_make_z(&a_now, "tb:hostA", 5000000000), PPCP_OK);
            /* CT-S5 (2): asked for A->C the honest answer is "I have not
             * measured it", and PPCP_ERR_NOT_FOUND is `relation_missing`. */
            CHECK_EQ_I(ppcp_relations_convert(rs, &a_now, &c, &out), PPCP_ERR_NOT_FOUND);
        }
    }

    TEST("CT-S5 (2) — asked for A->C, a fresh probe sequence appears on the wire");
    {
        size_t probes = 0;
        /* Registering the timebase and triggering is what a peer does when it
         * needs a relation it does not hold: it measures.  The observable is a
         * probe sequence, and it is the third one. */
        CHECK_EQ_I(ppcp_peer_sync_count(host.p), 3);
        CHECK_EQ_I(ppcp_peer_sync_trigger(host.p, PPCP_SYNC_ON_NETWORK_CHANGE), PPCP_OK);
        (void)ppcp_peer_drain(host.p, PPCP_CHANNEL_CONTROL, NULL, 0, &probes);
        drop_events(host.p);
        (void)ppcp_peer_sync_pump(host.p, host.clock.now_ns, &probes);
        CHECK_EQ_I(probes, 3);
        CHECK(count_probes(host.p, PPCP_CHANNEL_CONTROL) >= 3);
    }

    TEST("CT-S5 (4) / I21 — both of the HOST'S timebases end in a declared relation");
    {
        size_t published = 0;
        /* tb:hostB has had one burst probe and no sustained sequence; give it
         * one, so both timebases can be published. */
        drop_events(host.p);
        (void)ppcp_peer_drain(host.p, PPCP_CHANNEL_CONTROL, NULL, 0, &published);
        exchange(&host, &device, 40, 2000000000);
        for (i = 0; i < ppcp_peer_sync_count(host.p); i++) {
            const ppcp_sync_estimator *ei = ppcp_peer_sync_estimator_at(host.p, i);
            ppcp_timebase_relation     ri;
            CHECK_EQ_I(ppcp_sync_estimator_relation(ei, &ri), PPCP_OK);
            /* Each relation names the timebase it was measured FROM: directly
             * declared, never derived from the other one (5.4.1a). */
            CHECK(ppcp_id_equal(&ri.from, ppcp_sync_estimator_local_tb(ei)));
        }
        CHECK_EQ_I(ppcp_peer_publish_relations(host.p, &published), PPCP_OK);
        /* CT-I18 — three timebases, three DIRECTLY measured relations, and no
         * fourth: nothing is published that was not measured. */
        CHECK_EQ_I(published, 3);
        CHECK_EQ_I(ppcp_relations_count(ppcp_peer_relations(host.p)), 4);   /* +the B->C
                                                                              the test put
                                                                              in by hand */
        pump(host.p, device.p, PPCP_CHANNEL_CONTROL);
    }

    TEST("6.1f — the device received both relations, and holds them uncomposed");
    {
        ppcp_relation_set *rs = ppcp_peer_relations(device.p);
        ppcp_id a, b, cc, d;
        CHECK_EQ_I(ppcp_id_set_z(&a, "tb:hostA"), PPCP_OK);
        CHECK_EQ_I(ppcp_id_set_z(&b, "tb:hostB"), PPCP_OK);
        CHECK_EQ_I(ppcp_id_set_z(&cc, "tb:hostC"), PPCP_OK);
        CHECK_EQ_I(ppcp_id_set_z(&d, "tb:dev"), PPCP_OK);
        CHECK(ppcp_relations_find(rs, &a, &d) != NULL);
        CHECK(ppcp_relations_find(rs, &b, &d) != NULL);
        CHECK(ppcp_relations_find(rs, &cc, &d) != NULL);
        /* A→B, A→C and B→C are exactly what composition would have produced
         * from those three, and they are exactly what is absent. */
        CHECK(ppcp_relations_find(rs, &a, &b) == NULL);
        CHECK(ppcp_relations_find(rs, &a, &cc) == NULL);
        CHECK(ppcp_relations_find(rs, &b, &cc) == NULL);
    }

    TEST("I4 — identity is identity, and is never asserted as a relation");
    {
        ppcp_relation_set *rs = ppcp_peer_relations(device.p);
        ppcp_instant       in, out;
        ppcp_id            same;
        CHECK_EQ_I(ppcp_id_set_z(&same, "tb:dev"), PPCP_OK);
        CHECK_EQ_I(ppcp_instant_make_z(&in, "tb:dev", 123456789), PPCP_OK);
        CHECK_EQ_I(ppcp_relations_convert(rs, &in, &same, &out), PPCP_OK);
        CHECK_EQ_I(out.ns, 123456789);
        CHECK(ppcp_relations_find(rs, &same, &same) == NULL);
    }

    TEST("5.4b / 8.2i1 — `unrelated` refuses to convert rather than assuming zero");
    {
        ppcp_relation_set rs;
        ppcp_instant      at, in, out;
        ppcp_timebase_relation u;
        ppcp_id           to;
        ppcp_relations_init(&rs);
        CHECK_EQ_I(ppcp_instant_make_z(&at, "tb:x", 0), PPCP_OK);
        CHECK_EQ_I(ppcp_relation_make_unrelated(&u, "tb:x", "tb:y", PPCP_RELM_DECLARED, &at),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_relations_put(&rs, &u), PPCP_OK);
        CHECK_EQ_I(ppcp_id_set_z(&to, "tb:y"), PPCP_OK);
        CHECK_EQ_I(ppcp_instant_make_z(&in, "tb:x", 99), PPCP_OK);
        CHECK_EQ_I(ppcp_relations_convert(&rs, &in, &to, &out), PPCP_ERR_INVALID);
    }

    rig_peer_free(&host);
    rig_peer_free(&device);
}

/* ================================================ CT-S4 assertion 7 — 7.4, 8.3g */

static void test_liveness(void)
{
    static const char *const prof[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE,
                                        PPCP_PROFILE_LIVE, PPCP_PROFILE_MINT };
    static const char *const hprof[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE,
                                         PPCP_PROFILE_LIVE, PPCP_PROFILE_ARBITRATE };
    rig_peer  host, device;
    ppcp_session s;
    ppcp_id   ref_before, ref_after;
    int64_t   window_before, hold_before;
    int64_t   t = 0;
    const int64_t beat = 1000000000;   /* CORE 7.4a default, 1000 ms */

    memset(&host, 0, sizeof(host));
    rig_add(&host.clock, "tb:host", 0, 0.0);
    rig_peer_new(&host, PPCP_ROLE_HOST, "peer:host", hprof, 4, "tb:host", true);

    memset(&device, 0, sizeof(device));
    rig_add(&device.clock, "tb:dev", 0, 0.0);
    rig_peer_new(&device, PPCP_ROLE_CAPTURE, "peer:dev", prof, 4, "tb:dev", true);

    TEST("4.1d / 5.10e — a hosted Session carries both arbitration parameters");
    CHECK_EQ_I(ppcp_session_make_hosted(&s, "sess:1", "tb:host",
                                        PPCP_DEFAULT_COINCIDENCE_WINDOW_NS,
                                        PPCP_DEFAULT_ISSUE_HOLD_NS), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_session_open(host.p, &s), PPCP_OK);
    pump(host.p, device.p, PPCP_CHANNEL_CONTROL);
    pump(device.p, host.p, PPCP_CHANNEL_CONTROL);
    drop_events(host.p);
    drop_events(device.p);
    CHECK(ppcp_peer_session_params(device.p) != NULL);
    CHECK(ppcp_peer_session_params(device.p)->has_arbitration);
    ref_before    = *ppcp_peer_timebase_ref(device.p);
    window_before = ppcp_peer_session_params(device.p)->coincidence_window_ns;
    hold_before   = ppcp_peer_session_params(device.p)->issue_hold_ns;

    TEST("8.3g — a Session WITH a host is not in the zero-host regime");
    CHECK(!ppcp_peer_zero_host(device.p));
    CHECK_EQ_I(ppcp_peer_link_state(device.p), PPCP_LINK_LIVE);

    TEST("7.4a/7.4b — the host beats, and the ack carries thermal, storage, battery");
    (void)ppcp_peer_liveness_pump(host.p, t);
    pump(host.p, device.p, PPCP_CHANNEL_CONTROL);
    {
        ppcp_event ev;
        bool       saw_beat = false;
        while (ppcp_peer_next_event(device.p, &ev) == PPCP_OK)
            if (ev.kind == PPCP_EVENT_HEARTBEAT)
                saw_beat = true;
        CHECK(saw_beat);
    }
    (void)ppcp_peer_liveness_pump(device.p, t);
    pump(device.p, host.p, PPCP_CHANNEL_CONTROL);
    {
        ppcp_event ev;
        bool       saw_ack = false;
        while (ppcp_peer_next_event(host.p, &ev) == PPCP_OK) {
            if (ev.kind == PPCP_EVENT_HEARTBEAT && ev.msg != NULL &&
                ev.msg->type == PPCP_MT_HEARTBEAT_ACK) {
                saw_ack = true;
                CHECK_EQ_I(ev.msg->body.heartbeat_ack.thermal, PPCP_THERMAL_NOMINAL);
                CHECK(ev.msg->body.heartbeat_ack.storage_free_bytes > 0);
                CHECK(ev.msg->body.heartbeat_ack.has_battery_pct);
            }
        }
        CHECK(saw_ack);
    }
    (void)ppcp_peer_liveness_pump(host.p, t);
    CHECK_EQ_I(ppcp_peer_link_state(host.p), PPCP_LINK_LIVE);

    TEST("7.4c — one and two missed intervals are not a loss");
    t += beat + beat / 2;
    (void)ppcp_peer_liveness_pump(device.p, t);
    CHECK_EQ_I(ppcp_peer_missed_heartbeats(device.p), 1);
    CHECK_EQ_I(ppcp_peer_link_state(device.p), PPCP_LINK_LIVE);
    t += beat;
    (void)ppcp_peer_liveness_pump(device.p, t);
    CHECK_EQ_I(ppcp_peer_missed_heartbeats(device.p), 2);
    CHECK_EQ_I(ppcp_peer_link_state(device.p), PPCP_LINK_LIVE);

    TEST("7.4c — three consecutive missed intervals IS a loss, and it is an event");
    t += beat;
    (void)ppcp_peer_liveness_pump(device.p, t);
    CHECK_EQ_I(ppcp_peer_missed_heartbeats(device.p), 3);
    CHECK_EQ_I(ppcp_peer_link_state(device.p), PPCP_LINK_LOST);
    {
        ppcp_event ev;
        bool       lost = false;
        while (ppcp_peer_next_event(device.p, &ev) == PPCP_OK)
            if (ev.kind == PPCP_EVENT_LINK_LOST)
                lost = true;
        CHECK(lost);
    }

    TEST("CT-S4 (7) / 8.3g — the peer enters the zero-host regime, the Session does not change");
    CHECK(ppcp_peer_zero_host(device.p));
    ref_after = *ppcp_peer_timebase_ref(device.p);
    CHECK(ppcp_id_equal(&ref_before, &ref_after));
    CHECK(ppcp_peer_session_params(device.p)->has_arbitration);
    CHECK_EQ_I(ppcp_peer_session_params(device.p)->coincidence_window_ns, window_before);
    CHECK_EQ_I(ppcp_peer_session_params(device.p)->issue_hold_ns, hold_before);
    CHECK(ppcp_peer_session_id(device.p) != NULL);

    TEST("7.4 — a heartbeat after the loss restores the link, and says so");
    (void)ppcp_peer_liveness_pump(host.p, t);
    pump(host.p, device.p, PPCP_CHANNEL_CONTROL);
    (void)ppcp_peer_liveness_pump(device.p, t);
    CHECK_EQ_I(ppcp_peer_link_state(device.p), PPCP_LINK_LIVE);
    CHECK(!ppcp_peer_zero_host(device.p));
    {
        ppcp_event ev;
        bool       back = false;
        while (ppcp_peer_next_event(device.p, &ev) == PPCP_OK)
            if (ev.kind == PPCP_EVENT_LINK_RESTORED)
                back = true;
        CHECK(back);
    }

    TEST("8.3g — a HOSTLESS session is the other entry, and needs no link loss");
    {
        rig_peer solo;
        ppcp_session hs;
        memset(&solo, 0, sizeof(solo));
        rig_add(&solo.clock, "tb:dev", 0, 0.0);
        rig_peer_new(&solo, PPCP_ROLE_CAPTURE, "peer:solo", prof, 4, "tb:dev", true);
        CHECK_EQ_I(ppcp_session_make_hostless(&hs, "sess:2", "tb:dev"), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_session_open(solo.p, &hs), PPCP_OK);
        /* The peer that RECORDED the hostless session_open is the one that
         * must mint; it learns that from its own frame arriving back through a
         * bundle read, so drive it the way a bundle would. */
        {
            uint8_t buf[8192];
            size_t  n = 0, consumed = 0;
            rig_peer sink;
            memset(&sink, 0, sizeof(sink));
            rig_add(&sink.clock, "tb:dev", 0, 0.0);
            rig_peer_new(&sink, PPCP_ROLE_CAPTURE, "peer:sink", prof, 4, "tb:dev", true);
            CHECK_EQ_I(ppcp_peer_drain(solo.p, PPCP_CHANNEL_CONTROL, buf, sizeof(buf), &n),
                       PPCP_OK);
            CHECK_EQ_I(ppcp_peer_feed(sink.p, PPCP_CHANNEL_CONTROL, buf, n, &consumed),
                       PPCP_OK);
            CHECK(ppcp_peer_session_params(sink.p) != NULL);
            CHECK(!ppcp_peer_session_params(sink.p)->has_arbitration);
            CHECK(ppcp_peer_zero_host(sink.p));
            CHECK_EQ_I(ppcp_peer_link_state(sink.p), PPCP_LINK_LIVE);  /* nothing is lost */
            rig_peer_free(&sink);
        }
        rig_peer_free(&solo);
    }

    TEST("F-H5-3 / 2.2.2 — Live with no health source is refused at construction");
    {
        /* The profile is a promise about behaviour, and with no health source
         * this engine cannot keep this one: every `heartbeat` would be answered
         * `profile_not_supported`, §7.4 would never run and the link would
         * never go live.  Nothing said so until a heartbeat came back refused,
         * which cost an hour and two wrongly raised defects in S3. */
        ppcp_peer_config bad;
        void            *mem = malloc(ppcp_peer_sizeof());
        ppcp_peer       *np  = NULL;
        rig_clock        c;
        memset(&c, 0, sizeof(c));
        rig_add(&c, "tb:dev", 0, 0.0);
        memset(&bad, 0, sizeof(bad));
        bad.role          = PPCP_ROLE_CAPTURE;
        bad.peer_id       = "peer:nohealth";
        bad.profiles      = prof;                 /* includes Live */
        bad.profile_count = 4;
        bad.clock         = rig_interface(&c);
        bad.sync_timebase = "tb:dev";
        bad.health_report = NULL;
        CHECK(mem != NULL);
        CHECK_EQ_I(ppcp_peer_new(mem, ppcp_peer_sizeof(), &bad, &np), PPCP_ERR_INVALID);
        /* The same peer with a health source is accepted; nothing else moved. */
        bad.health_report = health_ok;
        CHECK_EQ_I(ppcp_peer_new(mem, ppcp_peer_sizeof(), &bad, &np), PPCP_OK);
        ppcp_peer_free(np);
        free(mem);
    }

    TEST("7.4b / C3 — a peer that never declared Live refuses rather than inventing `nominal`");
    {
        /* Not a defect in the peer: a `heartbeat` reaching a peer whose
         * declaration does not confer Live is answered `profile_not_supported`,
         * which MSG §10 makes non-fatal.  This is the honest shape of the case
         * the check above replaced — the refusal is about the DECLARATION, not
         * about a missing callback. */
        static const char *const noLive[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE,
                                              PPCP_PROFILE_MINT };
        rig_peer mute;
        memset(&mute, 0, sizeof(mute));
        rig_add(&mute.clock, "tb:dev", 0, 0.0);
        rig_peer_new(&mute, PPCP_ROLE_CAPTURE, "peer:mute", noLive, 3, "tb:dev", false);
        (void)ppcp_peer_liveness_pump(host.p, t + 10 * beat);
        pump(host.p, mute.p, PPCP_CHANNEL_CONTROL);
        {
            ppcp_event ev;
            bool       refused = false;
            while (ppcp_peer_next_event(mute.p, &ev) == PPCP_OK) { (void)ev; }
            /* The refusal is on the wire, not in an event: `error` /
             * `profile_not_supported`, which MSG §10 makes non-fatal. */
            pump(mute.p, host.p, PPCP_CHANNEL_CONTROL);
            while (ppcp_peer_next_event(host.p, &ev) == PPCP_OK)
                if (ev.kind == PPCP_EVENT_ERROR && ev.msg != NULL &&
                    ev.msg->type == PPCP_MT_ERROR &&
                    ppcp_cbor_key_is(ev.msg->body.error.code.v,
                                     ev.msg->body.error.code.len,
                                     PPCP_ERRCODE_PROFILE_NOT_SUPPORTED))
                    refused = true;
            CHECK(refused);
            CHECK(ppcp_peer_get_state(host.p) != PPCP_PEER_CLOSED);
        }
        rig_peer_free(&mute);
    }

    rig_peer_free(&host);
    rig_peer_free(&device);
}

/* ======================================= CT-I21, the REMOTE half (F-H5-1, E2)
 *
 * CT-I21 was previously asserted only against a peer with several clocks of
 * its OWN.  The other half — one clock probing two clocks of one counterpart —
 * was unreachable: 6.1d gives the prober a sequence per local timebase and
 * 6.1b lets the responder answer on whichever declared clock it chose, so every
 * reply came back on the device's single `sync_timebase` and the host could
 * measure only one relation to it.  Erratum E2 makes `sync_probe.timebase_id`
 * selectable: name a clock the responder DECLARED and it answers on that one.
 */
static void test_sync_remote_half(void)
{
    static const char *const prof[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_LIVE };
    rig_peer       host, device;
    ppcp_timebase  tbs[2];
    ppcp_id        dprof[2];
    ppcp_peer_desc desc;
    const ppcp_sync_estimator *cam, *aud;
    ppcp_timebase_relation rc_, ra_;

    memset(&host, 0, sizeof(host));
    rig_add(&host.clock, "tb:hostA", 1000000000, 0.0);
    rig_peer_new(&host, PPCP_ROLE_HOST, "peer:host", prof, 2, "tb:hostA", true);

    /* ONE device, TWO clocks, deliberately far apart: a camera clock 12.5 ms
     * and 20 ppm off the host's, and an audio clock 300 ms and -35 ppm off it.
     * If the responder answered on one clock for both sequences the two
     * relations would be indistinguishable, which is exactly what used to
     * happen. */
    memset(&device, 0, sizeof(device));
    rig_add(&device.clock, "tb:devCam", 1012500000, 20.0);
    rig_add(&device.clock, "tb:devAud", 1300000000, -35.0);
    rig_peer_new(&device, PPCP_ROLE_CAPTURE, "peer:dev", prof, 2, "tb:devCam", true);

    /* 6.1b — the responder answers only on a timebase it DECLARED, so the
     * declaration is what makes the second clock addressable at all. */
    if (ppcp_timebase_make(&tbs[0], "tb:devCam", 9, PPCP_TB_CONTINUOUS, true, 1000) != PPCP_OK)
        abort();
    if (ppcp_timebase_make(&tbs[1], "tb:devAud", 9, PPCP_TB_CONTINUOUS, true, 1000) != PPCP_OK)
        abort();
    if (ppcp_id_set_z(&dprof[0], PPCP_PROFILE_CORE) != PPCP_OK) abort();
    if (ppcp_id_set_z(&dprof[1], PPCP_PROFILE_LIVE) != PPCP_OK) abort();
    if (ppcp_peer_desc_make(&desc, "peer:dev", PPCP_ROLE_CAPTURE, "1.0",
                            dprof, 2, tbs, 2) != PPCP_OK) abort();
    CHECK_EQ_I(ppcp_peer_declare(device.p, &desc), PPCP_OK);
    pump(device.p, host.p, PPCP_CHANNEL_CONTROL);
    drop_events(host.p);

    TEST("CT-I21 (remote) / E2 — one local clock, a sequence per REMOTE clock");
    CHECK_EQ_I(ppcp_peer_sync_add_target(host.p, "tb:hostA", "tb:devCam"), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_sync_add_target(host.p, "tb:hostA", "tb:devAud"), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_sync_count(host.p), 2);
    /* The PAIR is the key: the same pair twice is still not a second
     * measurement, and a target naming no remote clock is not a target. */
    CHECK_EQ_I(ppcp_peer_sync_add_target(host.p, "tb:hostA", "tb:devCam"), PPCP_ERR_INVALID);
    CHECK_EQ_I(ppcp_peer_sync_add_target(host.p, "tb:hostA", NULL), PPCP_ERR_INVALID);

    TEST("6.3c — the burst runs both sequences, and both go out on the wire");
    CHECK_EQ_I(ppcp_peer_sync_trigger(host.p, PPCP_SYNC_ON_CONNECT), PPCP_OK);
    {
        size_t probes = 0;
        (void)ppcp_peer_sync_pump(host.p, host.clock.now_ns, &probes);
        CHECK_EQ_I(probes, 2);
        CHECK_EQ_I(count_probes(host.p, PPCP_CHANNEL_CONTROL), 2);
    }
    advance(&host, &device, RIG_LEG_NS);
    pump(host.p, device.p, PPCP_CHANNEL_CONTROL);
    advance(&host, &device, RIG_LEG_NS);
    pump(device.p, host.p, PPCP_CHANNEL_CONTROL);
    drop_events(host.p);
    drop_events(device.p);

    TEST("6.1b — each reply came back on the clock the probe named");
    cam = ppcp_peer_sync_estimator_for_pair(host.p, "tb:hostA", "tb:devCam");
    aud = ppcp_peer_sync_estimator_for_pair(host.p, "tb:hostA", "tb:devAud");
    CHECK(cam != NULL && aud != NULL);
    CHECK(cam != aud);
    CHECK_EQ_I(ppcp_sync_estimator_count(cam), 1);
    CHECK_EQ_I(ppcp_sync_estimator_count(aud), 1);

    TEST("I21 / 5.4.1a — two relations, MEASURED, not one measured and one composed");
    exchange(&host, &device, 40, 2000000000);
    cam = ppcp_peer_sync_estimator_for_pair(host.p, "tb:hostA", "tb:devCam");
    aud = ppcp_peer_sync_estimator_for_pair(host.p, "tb:hostA", "tb:devAud");
    CHECK_EQ_I(ppcp_sync_estimator_relation(cam, &rc_), PPCP_OK);
    CHECK_EQ_I(ppcp_sync_estimator_relation(aud, &ra_), PPCP_OK);
    CHECK(ppcp_cbor_key_is(rc_.from.v, rc_.from.len, "tb:hostA"));
    CHECK(ppcp_cbor_key_is(rc_.to.v, rc_.to.len, "tb:devCam"));
    CHECK(ppcp_cbor_key_is(ra_.from.v, ra_.from.len, "tb:hostA"));
    CHECK(ppcp_cbor_key_is(ra_.to.v, ra_.to.len, "tb:devAud"));
    CHECK_EQ_I(rc_.method, PPCP_RELM_ESTIMATED_ONLINE);
    CHECK_EQ_I(ra_.method, PPCP_RELM_ESTIMATED_ONLINE);
    /* The two clocks were injected 287.5 ms apart and 55 ppm apart, and the
     * two relations recover both — which they could not do if one responder
     * timebase had answered both sequences. */
    CHECK(rc_.skew_ppm > 15.0 && rc_.skew_ppm < 25.0);
    CHECK(ra_.skew_ppm > -40.0 && ra_.skew_ppm < -30.0);
    CHECK(ra_.offset_ns - rc_.offset_ns > 250000000);

    TEST("6.1f — both are published in ONE relation_update, neither composed");
    {
        size_t n = 0;
        CHECK_EQ_I(ppcp_peer_publish_relations(host.p, &n), PPCP_OK);
        CHECK_EQ_I(n, 2);
    }

    rig_peer_free(&host);
    rig_peer_free(&device);
}

/* ============================== MSG 4.3 and 6.1c — F-D6-1 and F-D6-2 */

static void test_resume_and_stamps(void)
{
    static const char *const prof[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE,
                                        PPCP_PROFILE_LIVE, PPCP_PROFILE_MINT };
    static const char *const hprof[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_LIVE,
                                         PPCP_PROFILE_ARBITRATE };
    rig_peer      host, device;
    ppcp_session  s;
    ppcp_id       minted[2];
    ppcp_pending_capture pend[1];
    uint8_t       dv[PPCP_SHA256_BYTES];
    ppcp_event    ev;

    memset(&host, 0, sizeof(host));
    rig_add(&host.clock, "tb:host", 0, 0.0);
    rig_peer_new(&host, PPCP_ROLE_HOST, "peer:host", hprof, 3, "tb:host", true);
    memset(&device, 0, sizeof(device));
    rig_add(&device.clock, "tb:dev", 0, 0.0);
    rig_peer_new(&device, PPCP_ROLE_CAPTURE, "peer:dev", prof, 4, "tb:dev", true);

    CHECK_EQ_I(ppcp_session_make_hosted(&s, "sess:resume", "tb:host",
                                        PPCP_DEFAULT_COINCIDENCE_WINDOW_NS,
                                        PPCP_DEFAULT_ISSUE_HOLD_NS), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_sync_add_timebase(device.p, "tb:dev", NULL), PPCP_OK);

    TEST("MSG 4.3a / F-D6-1 — a reconnecting peer originates `session_resume`");
    CHECK_EQ_I(ppcp_id_set_z(&minted[0], "shot:offline-1"), PPCP_OK);
    CHECK_EQ_I(ppcp_id_set_z(&minted[1], "shot:offline-2"), PPCP_OK);
    memset(&pend[0], 0, sizeof(pend[0]));
    CHECK_EQ_I(ppcp_id_set_z(&pend[0].capture_id, "cap:7"), PPCP_OK);
    memset(dv, 0xA5, sizeof(dv));
    CHECK_EQ_I(ppcp_digest_set(&pend[0].digest, dv), PPCP_OK);
    pend[0].bytes           = 4000000;
    pend[0].has_acked_index = true;
    pend[0].acked_index     = 41;      /* MSG §12's own example */
    CHECK_EQ_I(ppcp_peer_session_resume(device.p, &s, minted, 2, pend, 1), PPCP_OK);

    /* The Session did not end, so the resuming peer is in it — and reads its
     * own parameters back, which F-H5-2 made true on the originating path. */
    CHECK(ppcp_peer_session_id(device.p) != NULL);
    CHECK(ppcp_peer_session_params(device.p) != NULL);
    CHECK(ppcp_peer_session_params(device.p)->has_arbitration);
    CHECK(!ppcp_peer_zero_host(device.p));

    TEST("4.3b — the burst is ARMED by the resume, not left to the embedding");
    {
        size_t probes = 0;
        (void)ppcp_peer_sync_pump(device.p, device.clock.now_ns, &probes);
        CHECK_EQ_I(probes, 1);
    }

    TEST("MSG §12 — the host answers `session_joined`, and reads the outage back");
    pump(device.p, host.p, PPCP_CHANNEL_CONTROL);
    {
        bool saw = false;
        while (ppcp_peer_next_event(host.p, &ev) == PPCP_OK) {
            if (ev.kind == PPCP_EVENT_SESSION_RESUME && ev.msg != NULL) {
                const ppcp_body_session_resume *b = &ev.msg->body.session_resume;
                saw = true;
                CHECK_EQ_I(b->minted_shot_count, 2);
                CHECK(ppcp_cbor_key_is(b->minted_shots[1].v, b->minted_shots[1].len,
                                       "shot:offline-2"));
                CHECK_EQ_I(b->pending_count, 1);
                CHECK(b->pending[0].has_acked_index);
                CHECK_EQ_I(b->pending[0].acked_index, 41);
                CHECK(ppcp_cbor_key_is(b->peer_id.v, b->peer_id.len, "peer:dev"));
            }
        }
        CHECK(saw);
    }
    pump(host.p, device.p, PPCP_CHANNEL_CONTROL);
    {
        bool joined = false;
        while (ppcp_peer_next_event(device.p, &ev) == PPCP_OK)
            if (ev.kind == PPCP_EVENT_SESSION_JOINED && ev.msg != NULL &&
                ev.msg->body.session_joined.verdict == PPCP_JOINED)
                joined = true;
        CHECK(joined);
    }

    TEST("4.3a — a list past PPCP_MAX_MINTED_SHOTS is refused, not truncated");
    CHECK_EQ_I(ppcp_peer_session_resume(device.p, &s, minted,
                                        PPCP_MAX_MINTED_SHOTS + 1, NULL, 0),
               PPCP_ERR_LIMIT);

    TEST("6.1c / F-D6-2 — the responder stamps t2/t3 near the socket");
    {
        /* The host probes; the device answers with stamps the embedding took
         * rather than with two clock reads at decode and build time. */
        const int64_t t2 = 4242000000, t3 = 4242000300;
        bool          seen = false;
        CHECK_EQ_I(ppcp_peer_sync_add_timebase(host.p, "tb:host", NULL), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_sync_reply_stamps(device.p, t2, t3), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_sync_probe(host.p, "tb:host"), PPCP_OK);
        pump(host.p, device.p, PPCP_CHANNEL_CONTROL);
        pump(device.p, host.p, PPCP_CHANNEL_CONTROL);
        while (ppcp_peer_next_event(host.p, &ev) == PPCP_OK) {
            if (ev.msg != NULL && ev.msg->type == PPCP_MT_SYNC_REPLY) {
                seen = true;
                CHECK_EQ_I(ev.msg->body.sync_reply.t2.ns, t2);
                CHECK_EQ_I(ev.msg->body.sync_reply.t3.ns, t3);
                CHECK(ppcp_id_equal(&ev.msg->body.sync_reply.t2.tb,
                                    &ev.msg->body.sync_reply.t3.tb));  /* 6.1b */
            }
        }
        CHECK(seen);
        drop_events(device.p);
    }

    TEST("6.1c — `t3 == t2` is a DECLARATION a responder can make, not a coincidence");
    {
        bool seen = false;
        CHECK(!ppcp_peer_sync_zero_residence(device.p));
        CHECK_EQ_I(ppcp_peer_sync_set_zero_residence(device.p, true), PPCP_OK);
        CHECK(ppcp_peer_sync_zero_residence(device.p));
        /* Supplied stamps that DO differ are overridden: the declaration is
         * about what this implementation can distinguish, and it says it
         * cannot. */
        CHECK_EQ_I(ppcp_peer_sync_reply_stamps(device.p, 5000000000, 5000009999), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_sync_probe(host.p, "tb:host"), PPCP_OK);
        pump(host.p, device.p, PPCP_CHANNEL_CONTROL);
        pump(device.p, host.p, PPCP_CHANNEL_CONTROL);
        while (ppcp_peer_next_event(host.p, &ev) == PPCP_OK) {
            if (ev.msg != NULL && ev.msg->type == PPCP_MT_SYNC_REPLY) {
                seen = true;
                CHECK_EQ_I(ev.msg->body.sync_reply.t3.ns, ev.msg->body.sync_reply.t2.ns);
                CHECK_EQ_I(ev.msg->body.sync_reply.t2.ns, 5000000000);
            }
        }
        CHECK(seen);
    }
    /* A reply sent before it arrived is a bug, not a clock. */
    CHECK_EQ_I(ppcp_peer_sync_reply_stamps(device.p, 100, 99), PPCP_ERR_INVALID);

    rig_peer_free(&host);
    rig_peer_free(&device);
}

int main(void)
{
    test_sync();
    test_sync_remote_half();
    test_liveness();
    test_resume_and_stamps();
    TEST_MAIN_END();
}
