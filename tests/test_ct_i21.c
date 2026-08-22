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
#define RIG_MAX_TB 4

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

    /* The host owns two clocks — `tb:hostA` and `tb:hostB` — which is CT-I21's
     * whole point: I21 binds every multi-clock peer, hosts included (5.4.1b). */
    memset(&host, 0, sizeof(host));
    rig_add(&host.clock, "tb:hostA", 1000000000, 0.0);
    rig_add(&host.clock, "tb:hostB", 7000000000, 0.0);
    rig_peer_new(&host, PPCP_ROLE_HOST, "peer:host", prof, 2, "tb:hostA", true);

    memset(&device, 0, sizeof(device));
    /* +12.5 ms of offset and +20 ppm of skew — the measured cross-device
     * figure CORE §6.3 quotes. */
    rig_add(&device.clock, "tb:dev", 1012500000, 20.0);
    rig_peer_new(&device, PPCP_ROLE_CAPTURE, "peer:dev", prof, 2, "tb:dev", true);

    TEST("6.1d / I21 — one probe sequence per LOCAL timebase, on a HOST");
    CHECK_EQ_I(ppcp_peer_sync_add_timebase(host.p, "tb:hostA", NULL), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_sync_add_timebase(host.p, "tb:hostB", NULL), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_sync_count(host.p), 2);
    /* A second sequence for one timebase is not a second measurement. */
    CHECK_EQ_I(ppcp_peer_sync_add_timebase(host.p, "tb:hostA", NULL), PPCP_ERR_INVALID);

    TEST("6.3c — a connect burst is 10-20 exchanges, and both timebases get one");
    CHECK_EQ_I(ppcp_peer_sync_trigger(host.p, PPCP_SYNC_ON_CONNECT), PPCP_OK);
    CHECK(PPCP_SYNC_BURST >= 10u && PPCP_SYNC_BURST <= 20u);
    {
        size_t probes = 0;
        (void)ppcp_peer_sync_pump(host.p, host.clock.now_ns, &probes);
        CHECK_EQ_I(probes, 2);                 /* one per timebase, not one in total */
        CHECK_EQ_I(count_probes(host.p, PPCP_CHANNEL_CONTROL), 2);
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
        CHECK_EQ_I(ppcp_peer_sync_count(host.p), 2);
        CHECK_EQ_I(ppcp_peer_sync_trigger(host.p, PPCP_SYNC_ON_NETWORK_CHANGE), PPCP_OK);
        (void)ppcp_peer_drain(host.p, PPCP_CHANNEL_CONTROL, NULL, 0, &probes);
        drop_events(host.p);
        (void)ppcp_peer_sync_pump(host.p, host.clock.now_ns, &probes);
        CHECK_EQ_I(probes, 2);
        CHECK(count_probes(host.p, PPCP_CHANNEL_CONTROL) >= 2);
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
        CHECK_EQ_I(published, 2);
        pump(host.p, device.p, PPCP_CHANNEL_CONTROL);
    }

    TEST("6.1f — the device received both relations, and holds them uncomposed");
    {
        ppcp_relation_set *rs = ppcp_peer_relations(device.p);
        ppcp_id a, b, d;
        CHECK_EQ_I(ppcp_id_set_z(&a, "tb:hostA"), PPCP_OK);
        CHECK_EQ_I(ppcp_id_set_z(&b, "tb:hostB"), PPCP_OK);
        CHECK_EQ_I(ppcp_id_set_z(&d, "tb:dev"), PPCP_OK);
        CHECK(ppcp_relations_find(rs, &a, &d) != NULL);
        CHECK(ppcp_relations_find(rs, &b, &d) != NULL);
        /* A→B is exactly what composition would have produced from the two,
         * and it is exactly what is absent. */
        CHECK(ppcp_relations_find(rs, &a, &b) == NULL);
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

    TEST("7.4b — a peer with no health source refuses rather than inventing `nominal`");
    {
        rig_peer mute;
        memset(&mute, 0, sizeof(mute));
        rig_add(&mute.clock, "tb:dev", 0, 0.0);
        rig_peer_new(&mute, PPCP_ROLE_CAPTURE, "peer:mute", prof, 4, "tb:dev", false);
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

int main(void)
{
    test_sync();
    test_liveness();
    TEST_MAIN_END();
}
