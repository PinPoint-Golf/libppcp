/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * Detect and Mint — work package L10's evidence for the device half.
 *
 * Rows this file carries:
 *
 *   CT-S4 (1)-(6) / CT-I23   the zero-host path: no window, one Candidate per
 *                            Shot, `authority: device`, every Candidate
 *                            emitted and retained either way; the same two
 *                            Candidates in a hosted Session with a 50 ms
 *                            window produce ONE Shot carrying BOTH.
 *   CT-I26                   a Candidate names a declared Source on a declared
 *                            Timebase, or it is refused.
 *   CT-I32                   a host that never answers: no mint before
 *                            `issue_hold_ns + heartbeat_interval_ms`, a mint
 *                            after it ONLY for a Candidate the peer's own
 *                            policy would have promoted, and NOTHING at all
 *                            from a peer whose timebases are `unrelated`.
 *   CT-I33                   `Candidate.at` is canonical, converted by the
 *                            nominator, and converting again is a discrepancy
 *                            of `frame_start_to_exposure_offset_ns + d/2`.
 */
#include "ppcp/ppcp.h"

#include "test_util.h"

/* ------------------------------------------------------------------ rigging */

#define OFFSET_NS   120000        /* frame_start_to_exposure_offset_ns */
#define EXPOSURE_NS 2000000       /* d — 2 ms */

typedef struct rig {
    void      *mem;
    ppcp_peer *p;

    ppcp_id              profiles[8];
    ppcp_timebase        tb[1];
    ppcp_capture_profile cp[2];    /* [0] camera, [1] microphone (no format) */
    ppcp_source          src[2];
    ppcp_peer_desc       desc;
} rig;

static void rig_declare(rig *r, const char *peer_id, ppcp_role role, const char *tb_id,
                        const char *const *profiles, size_t nprof)
{
    ppcp_timing   timing, mic_timing;
    ppcp_geometry geom;
    size_t        i;

    for (i = 0; i < nprof; i++)
        if (ppcp_id_set_z(&r->profiles[i], profiles[i]) != PPCP_OK) abort();
    if (ppcp_timebase_make(&r->tb[0], tb_id, strlen(tb_id), PPCP_TB_CONTINUOUS, true, 1000)
        != PPCP_OK) abort();

    /* A camera Source: `nominal_frame_start`, which is what every AVFoundation
     * source declares and therefore the default path for the whole mobile
     * side (6.1). */
    if (ppcp_timing_make_nominal_frame_start(&timing, OFFSET_NS, PPCP_PROV_ASSUMED)
        != PPCP_OK) abort();
    if (ppcp_geometry_make_rolling_shutter(&geom, 8000000, PPCP_PROV_ASSUMED,
                                           PPCP_ROLL_TOP_TO_BOTTOM, 1080) != PPCP_OK) abort();
    if (ppcp_capture_profile_make(&r->cp[0], "cp:cam", &timing) != PPCP_OK) abort();
    if (ppcp_capture_profile_set_camera(&r->cp[0], &geom, PPCP_INTR_PER_FRAME) != PPCP_OK)
        abort();
    if (ppcp_capture_profile_set_format(&r->cp[0], "h264", 1920, 1080, "nv12") != PPCP_OK)
        abort();

    /* A microphone Source: no `format`, so 6.1d fixes `convention: mid` and
     * the canonical instant is the raw instant. */
    if (ppcp_timing_make(&mic_timing, PPCP_CONV_MID) != PPCP_OK) abort();
    if (ppcp_capture_profile_make(&r->cp[1], "cp:mic", &mic_timing) != PPCP_OK) abort();

    if (ppcp_source_make(&r->src[0], "src:cam", peer_id, "camera", tb_id, true,
                         &r->cp[0], 1) != PPCP_OK) abort();
    if (ppcp_source_make(&r->src[1], "src:mic", peer_id, "microphone", tb_id, true,
                         &r->cp[1], 1) != PPCP_OK) abort();
    if (ppcp_peer_desc_make(&r->desc, peer_id, role, "1.0", r->profiles, nprof, r->tb, 1)
        != PPCP_OK) abort();
    if (ppcp_peer_desc_set_sources(&r->desc, r->src, 2) != PPCP_OK) abort();
}

static void rig_new(rig *r, ppcp_role role, const char *id, const char *tb_id,
                    const char *const *profiles, size_t nprof)
{
    ppcp_peer_config cfg;

    memset(r, 0, sizeof(*r));
    memset(&cfg, 0, sizeof(cfg));
    cfg.role          = role;
    cfg.peer_id       = id;
    cfg.profiles      = profiles;
    cfg.profile_count = nprof;

    r->mem = malloc(ppcp_peer_sizeof());
    if (r->mem == NULL) abort();
    if (ppcp_peer_new(r->mem, ppcp_peer_sizeof(), &cfg, &r->p) != PPCP_OK) abort();
    rig_declare(r, id, role, tb_id, profiles, nprof);
    if (ppcp_peer_declare(r->p, &r->desc) != PPCP_OK) abort();
}

static void rig_free(rig *r)
{
    ppcp_peer_free(r->p);
    free(r->mem);
}

static void pump(ppcp_peer *from, ppcp_peer *to, uint8_t ch)
{
    static uint8_t buf[131072];
    size_t         n = 0, consumed = 0;
    while (ppcp_peer_pending(from, ch) > 0) {
        if (ppcp_peer_drain(from, ch, buf, sizeof(buf), &n) != PPCP_OK || n == 0)
            break;
        (void)ppcp_peer_feed(to, ch, buf, n, &consumed);
    }
}

static void drop_events(ppcp_peer *p)
{
    ppcp_event e;
    while (ppcp_peer_next_event(p, &e) == PPCP_OK) { }
}

/* Shot ids the embedding mints — 8.3e: the library has no random source. */
typedef struct id_seq { const char *prefix; unsigned n; } id_seq;

static ppcp_result next_id(void *ctx, ppcp_id *out)
{
    id_seq *s = (id_seq *)ctx;
    char    buf[64];
    size_t  i = 0;
    const char *pfx = s->prefix;
    unsigned v = ++s->n;
    while (*pfx && i < sizeof(buf) - 4)
        buf[i++] = *pfx++;
    buf[i++] = (char)('0' + (v / 10) % 10);
    buf[i++] = (char)('0' + v % 10);
    buf[i]   = '\0';
    return ppcp_id_set_z(out, buf);
}

/* The promotion policy: I14 and CONF §6 keep the threshold out of the library,
 * so here it is, in the "embedding", as a confidence floor.  The library never
 * sees the number. */
typedef struct promo { double floor; size_t calls; } promo;

static bool promote_above(void *ctx, const ppcp_candidate *c)
{
    promo *pp = (promo *)ctx;
    pp->calls++;
    return c->confidence >= pp->floor;
}

/* ============================================================ CT-I33 / Detect */

static void test_canonical(void)
{
    rig d;
    static const char *const prof[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE,
                                        PPCP_PROFILE_DETECT, PPCP_PROFILE_MINT };
    ppcp_candidate cam, mic;
    const int64_t  raw = 1000000000;

    rig_new(&d, PPCP_ROLE_CAPTURE, "peer:dev", "tb:dev", prof, 4);

    TEST("I33 / 5.12e — a `motion` Candidate is emitted CANONICAL, by the nominator");
    CHECK_EQ_I(ppcp_candidate_make_canonical(&cam, "cand:cam", &d.src[0], &d.cp[0],
                                             "motion", raw, EXPOSURE_NS, 0.9, NULL),
               PPCP_OK);
    /* nominal_frame_start: t + frame_start_to_exposure_offset_ns + d/2 */
    CHECK_EQ_I(cam.at.ns, raw + OFFSET_NS + EXPOSURE_NS / 2);
    CHECK(ppcp_cbor_key_is(cam.at.tb.v, cam.at.tb.len, "tb:dev"));

    TEST("5.12f — the correction it applied is reported, so the raw instant is recoverable");
    CHECK(cam.has_canonical_correction);
    CHECK_EQ_I(cam.canonical_correction_ns, OFFSET_NS + EXPOSURE_NS / 2);
    CHECK_EQ_I(cam.at.ns - cam.canonical_correction_ns, raw);

    TEST("I33 — a consumer converting a second time is off by offset + d/2");
    {
        int64_t twice = 0;
        CHECK_EQ_I(ppcp_canonical_instant(&d.cp[0].timing, cam.at.ns, EXPOSURE_NS, &twice),
                   PPCP_OK);
        CHECK_EQ_I(twice - cam.at.ns, OFFSET_NS + EXPOSURE_NS / 2);
    }

    TEST("6.1d — an acoustic Candidate from a Source with no `format` is unaffected");
    CHECK_EQ_I(ppcp_candidate_make_canonical(&mic, "cand:mic", &d.src[1], &d.cp[1],
                                             "acoustic", raw, EXPOSURE_NS, 0.9, NULL),
               PPCP_OK);
    CHECK_EQ_I(mic.at.ns, raw);
    CHECK_EQ_I(mic.canonical_correction_ns, 0);
    /* Reported even at zero: "no correction was applied" and "the field was
     * omitted" are different statements and only one is checkable. */
    CHECK(mic.has_canonical_correction);

    TEST("I29 / 8.1d — `tof_correction` is an Estimate, so it cannot lose its sigma");
    {
        ppcp_estimate tof;
        ppcp_candidate c;
        CHECK_EQ_I(ppcp_estimate_make(&tof, -5800000, 900000.0), PPCP_OK);
        CHECK_EQ_I(ppcp_candidate_make_canonical(&c, "cand:tof", &d.src[1], &d.cp[1],
                                                 "acoustic", raw, 0, 0.9, &tof), PPCP_OK);
        CHECK(c.has_tof_correction);
        CHECK_EQ_I(c.tof_correction.value_ns, -5800000);
        CHECK(c.tof_correction.sigma_ns > 0.0);
        CHECK_EQ_I(ppcp_candidate_validate(&c), PPCP_OK);
    }

    TEST("6.1d — a profile with no `format` declaring anything but `mid` is unconstructible");
    {
        ppcp_timing            bad;
        ppcp_capture_profile   badp;
        ppcp_candidate         c;
        CHECK_EQ_I(ppcp_timing_make(&bad, PPCP_CONV_START), PPCP_OK);
        CHECK_EQ_I(ppcp_capture_profile_make(&badp, "cp:bad", &bad), PPCP_OK);
        CHECK_EQ_I(ppcp_candidate_make_canonical(&c, "cand:bad", &d.src[1], &badp,
                                                 "acoustic", raw, 0, 0.5, NULL),
                   PPCP_ERR_INVALID);
    }

    /* ---------------------------------------------------------------- CT-I26 */

    TEST("I26 / 7.1a — a Candidate naming a Source this peer did not declare is refused");
    {
        ppcp_candidate c = cam;
        CHECK_EQ_I(ppcp_id_set_z(&c.source_id, "src:nowhere"), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_nominate(d.p, &c), PPCP_ERR_NOT_FOUND);
    }

    TEST("I26 — a Candidate stamped in a timebase that is not the Source's is refused");
    {
        ppcp_candidate c = cam;
        CHECK_EQ_I(ppcp_instant_make_z(&c.at, "tb:elsewhere", raw), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_nominate(d.p, &c), PPCP_ERR_INVALID);
    }

    TEST("5.2a — a Candidate carrying another peer's id is refused");
    {
        ppcp_candidate c = cam;
        CHECK_EQ_I(ppcp_id_set_z(&c.peer_id, "peer:someone-else"), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_nominate(d.p, &c), PPCP_ERR_INVALID);
    }

    TEST("I26 / 7.1c / 8.1b — a record with no timebase cannot BECOME a Candidate");
    {
        /* The launch-monitor row: no peer, no timebase, no clock relation.
         * There is no constructor that takes a bare number, because
         * ppcp_candidate_make takes an Instant and ppcp_instant_make refuses a
         * missing or empty `tb` (I1).  So the shape 8.1e forbids a peer from
         * synthesising has no representation to synthesise INTO — it is
         * associated by `shot_link` and never enters arbitration. */
        ppcp_instant   nowhere;
        ppcp_candidate c;
        CHECK_EQ_I(ppcp_instant_make(&nowhere, NULL, 0, raw), PPCP_ERR_INVALID);
        CHECK_EQ_I(ppcp_instant_make(&nowhere, "", 0, raw), PPCP_ERR_INVALID);
        memset(&nowhere, 0, sizeof(nowhere));
        nowhere.ns = raw;                       /* a timestamp with no clock */
        CHECK_EQ_I(ppcp_candidate_make(&c, "cand:file", "peer:dev", "src:mic", "external",
                                       &nowhere, 0.9), PPCP_ERR_INVALID);
    }

    TEST("CT-I29 — a Candidate whose tof_correction lost its sigma is refused, both ways");
    {
        ppcp_candidate c = mic;
        double         inf = 1e308 * 10.0;
        double         nan = inf - inf;
        uint8_t        buf[4096];
        size_t         n = 0;
        ppcp_msg       m;

        c.has_tof_correction     = true;
        c.tof_correction.value_ns = 5800000;
        c.tof_correction.sigma_ns = nan;        /* a dispersion that is not one */
        CHECK_EQ_I(ppcp_candidate_validate(&c), PPCP_ERR_INVALID);
        CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_CANDIDATE, 1), PPCP_OK);
        m.body.candidate.candidate = c;
        CHECK(ppcp_msg_encode(buf, sizeof(buf), PPCP_CHANNEL_CONTROL, &m, &n) != PPCP_OK);
        CHECK_EQ_I(ppcp_peer_nominate(d.p, &c), PPCP_ERR_INVALID);
        /* And the reverse: a sigma with no value is not expressible either,
         * because ppcp_estimate_make takes both or neither. */
        CHECK_EQ_I(ppcp_estimate_make(&c.tof_correction, 0, 900000.0), PPCP_OK);
        CHECK_EQ_I(ppcp_candidate_validate(&c), PPCP_OK);
    }

    TEST("7.1d — a well-formed Candidate is emitted, whatever anyone thinks of it");
    CHECK_EQ_I(ppcp_peer_nominate(d.p, &cam), PPCP_OK);
    CHECK(ppcp_peer_pending(d.p, PPCP_CHANNEL_CONTROL) > 0);

    rig_free(&d);
}

/* ======================================== CT-S4 (1)-(5) / CT-I23 — zero host */

static void nominate_at(rig *d, ppcp_mint *m, const char *id, int64_t raw_ns,
                        double confidence)
{
    ppcp_candidate c;
    CHECK_EQ_I(ppcp_candidate_make_canonical(&c, id, &d->src[1], &d->cp[1], "acoustic",
                                             raw_ns, 0, confidence, NULL), PPCP_OK);
    /* 7.1d — emitted first, always, and only then offered to Mint. */
    CHECK_EQ_I(ppcp_peer_nominate(d->p, &c), PPCP_OK);
    CHECK_EQ_I(ppcp_mint_observe_own(m, &c), PPCP_OK);
}

static void test_zero_host(void)
{
    static const char *const prof[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE,
                                        PPCP_PROFILE_DETECT, PPCP_PROFILE_MINT,
                                        PPCP_PROFILE_OFFLINE };
    rig          d;
    ppcp_session s;
    void        *mm = malloc(ppcp_mint_sizeof());
    ppcp_mint   *m  = NULL;
    id_seq       ids = { "shot:", 0 };
    promo        pol = { 0.5, 0 };
    size_t       minted = 0;
    const int64_t t_first  = 1000000000;
    const int64_t t_second = 1010000000;   /* 10 ms later — CT-S4 assertion 2 */

    rig_new(&d, PPCP_ROLE_CAPTURE, "peer:dev", "tb:dev", prof, 5);

    TEST("CT-S4 (5) / 8.3d — a peer that does not declare Mint has no minting engine");
    {
        static const char *const nomint[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE,
                                              PPCP_PROFILE_DETECT };
        rig        nm;
        void      *nmm = malloc(ppcp_mint_sizeof());
        ppcp_mint *bad = NULL;
        rig_new(&nm, PPCP_ROLE_CAPTURE, "peer:nomint", "tb:dev", nomint, 3);
        CHECK_EQ_I(ppcp_mint_new(nmm, ppcp_mint_sizeof(), nm.p, next_id, &ids, &bad),
                   PPCP_ERR_INVALID);
        rig_free(&nm);
        free(nmm);
    }

    TEST("CT-S4 (1) / 4.1d — a hostless Session carries NEITHER arbitration parameter");
    CHECK_EQ_I(ppcp_session_make_hostless(&s, "sess:solo", "tb:dev"), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_session_open(d.p, &s), PPCP_OK);
    /* F-H5-2 / F-D6-3 — the ORIGINATOR reads its own parameters back.  Until
     * S4 it did not: `has_session_params` was set on the receiving path only,
     * so the swap below was the only way to assert 4.1b's hostless form and
     * ppcp_peer_zero_host() was true here by accident rather than by the
     * absence of the arbitration parameters.  Both ends, one record. */
    CHECK(ppcp_peer_session_params(d.p) != NULL);
    CHECK(!ppcp_peer_session_params(d.p)->has_arbitration);
    CHECK(ppcp_cbor_key_is(ppcp_peer_session_params(d.p)->timebase_ref.v,
                           ppcp_peer_session_params(d.p)->timebase_ref.len, "tb:dev"));
    CHECK(ppcp_peer_zero_host(d.p));
    /* The engine learns the Session from the frame, exactly as it would from a
     * bundle read — which is what makes "a file is a transport" true. */
    {
        uint8_t buf[8192];
        size_t  n = 0, consumed = 0;
        rig     sink;
        rig_new(&sink, PPCP_ROLE_CAPTURE, "peer:dev", "tb:dev", prof, 5);
        CHECK_EQ_I(ppcp_peer_drain(d.p, PPCP_CHANNEL_CONTROL, buf, sizeof(buf), &n),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_peer_feed(sink.p, PPCP_CHANNEL_CONTROL, buf, n, &consumed),
                   PPCP_OK);
        rig_free(&d);
        d = sink;
    }
    CHECK(ppcp_peer_session_params(d.p) != NULL);
    CHECK(!ppcp_peer_session_params(d.p)->has_arbitration);
    CHECK(ppcp_peer_zero_host(d.p));

    CHECK(mm != NULL);
    CHECK_EQ_I(ppcp_mint_new(mm, ppcp_mint_sizeof(), d.p, next_id, &ids, &m), PPCP_OK);
    CHECK_EQ_I(ppcp_mint_set_promotion_policy(m, promote_above, &pol), PPCP_OK);

    TEST("CT-S4 (2) / I23 — two Candidates 10 ms apart, no window applied");
    nominate_at(&d, m, "cand:1", t_first, 0.9);    /* the peer believes this one */
    nominate_at(&d, m, "cand:2", t_second, 0.2);   /* and not this one */
    CHECK_EQ_I(ppcp_mint_pump(m, t_second, &minted), PPCP_OK);
    /* 8.3a: no deadline in the zero-host regime, so both were considered at
     * once, and 8.3b: promotion is the detector's, so only one became a Shot. */
    CHECK_EQ_I(minted, 1);
    CHECK_EQ_I(ppcp_mint_minted_count(m), 1);
    CHECK_EQ_I(pol.calls, 2);                      /* both were offered */

    TEST("CT-S4 (2) / I8 — BOTH Candidates were emitted and both are retained");
    {
        /* Emission: two `candidate` frames left this peer before any Shot did.
         * Retention: the unpromoted one is still held, with no Shot. */
        CHECK_EQ_I(ppcp_mint_retained_count(m), 1);
    }

    TEST("CT-S4 (2)(3) / I23 — one Candidate per Shot, `authority: device`");
    {
        /* The Shot is on the wire; decode it back rather than trusting the
         * engine's own account of what it sent. */
        uint8_t buf[8192];
        size_t  n = 0, off = 0, shots = 0;
        CHECK_EQ_I(ppcp_peer_drain(d.p, PPCP_CHANNEL_CONTROL, buf, sizeof(buf), &n),
                   PPCP_OK);
        while (off + PPCP_FRAME_HEADER_BYTES <= n) {
            ppcp_frame_header hdr;
            const uint8_t    *payload = NULL;
            size_t            consumed = 0;
            ppcp_msg          msg;
            CHECK_EQ_I(ppcp_frame_header_parse(buf + off, &hdr), PPCP_OK);
            CHECK_EQ_I(ppcp_frame_read(buf + off, n - off, &hdr, &payload, &consumed),
                       PPCP_OK);
            memset(&msg, 0, sizeof(msg));
            if (ppcp_msg_decode(payload, hdr.payload_len,
                                ppcp_cbor_limits_for_channel(hdr.channel), NULL,
                                &msg) == PPCP_OK && msg.type == PPCP_MT_SHOT) {
                shots++;
                CHECK_EQ_I(msg.body.shot.shot.authority, PPCP_AUTHORITY_DEVICE);
                CHECK_EQ_I(msg.body.shot.shot.candidate_count, 1);
                /* 5.13c — `t0` in `Session.timebase_ref`, which here is the
                 * device's own clock, so the conversion is the identity and no
                 * relation was needed or invented (I4). */
                CHECK(ppcp_cbor_key_is(msg.body.shot.shot.t0.tb.v,
                                       msg.body.shot.shot.t0.tb.len, "tb:dev"));
                CHECK_EQ_I(msg.body.shot.shot.t0.ns, t_first);
                CHECK(ppcp_cbor_key_is(msg.body.shot.shot.candidates[0].v,
                                       msg.body.shot.shot.candidates[0].len, "cand:1"));
            }
            off += consumed;
        }
        CHECK_EQ_I(shots, 1);
    }

    TEST("I6 — every Shot references at least one Candidate, and cannot not");
    {
        ppcp_shot    sh;
        ppcp_instant t0;
        CHECK_EQ_I(ppcp_instant_make_z(&t0, "tb:dev", t_first), PPCP_OK);
        /* There is no constructor that produces a Shot with no Candidate: the
         * first one is a parameter. */
        CHECK_EQ_I(ppcp_shot_make(&sh, "shot:x", "sess:solo", &t0, PPCP_AUTHORITY_DEVICE,
                                  "peer:dev", NULL), PPCP_ERR_INVALID);
    }

    free(mm);
    rig_free(&d);
}

/* ================================================ CT-S4 (4) — the same pair, hosted */

static void test_hosted_pair(void)
{
    static const char *const dprof[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE,
                                         PPCP_PROFILE_DETECT, PPCP_PROFILE_MINT,
                                         PPCP_PROFILE_LIVE };
    static const char *const hprof[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE,
                                         PPCP_PROFILE_DETECT, PPCP_PROFILE_ARBITRATE,
                                         PPCP_PROFILE_LIVE };
    rig           dev, host;
    ppcp_session  s;
    void         *am = malloc(ppcp_arbiter_sizeof());
    ppcp_arbiter *arb = NULL;
    id_seq        ids = { "shot:h", 0 };
    ppcp_candidate c1, c2;
    size_t        issued = 0;
    const int64_t t_first  = 1000000000;
    const int64_t t_second = 1010000000;

    rig_new(&dev,  PPCP_ROLE_CAPTURE, "peer:dev",  "tb:dev",  dprof, 5);
    rig_new(&host, PPCP_ROLE_HOST,    "peer:host", "tb:host", hprof, 5);

    /* 5.10 default window is 50 ms and the two Candidates are 10 ms apart. */
    CHECK_EQ_I(ppcp_session_make_hosted(&s, "sess:live", "tb:host",
                                        PPCP_DEFAULT_COINCIDENCE_WINDOW_NS,
                                        PPCP_DEFAULT_ISSUE_HOLD_NS), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_session_open(host.p, &s), PPCP_OK);
    pump(host.p, dev.p, PPCP_CHANNEL_CONTROL);
    pump(dev.p, host.p, PPCP_CHANNEL_CONTROL);
    drop_events(host.p);
    drop_events(dev.p);
    /* F-H5-2 — the host's own arbiter needs the Session parameters, and until
     * S4 a peer learned them from a `session_open` FRAME and not from having
     * composed one.  This block used to stand up a SECOND host engine of the
     * same identity and feed it the frame the first had emitted, purely to get
     * the parameters back; that was the defect, written down as a workaround.
     * The host that opened the Session reads them directly now. */
    CHECK(ppcp_peer_session_params(host.p) != NULL);
    CHECK(ppcp_peer_session_params(host.p)->has_arbitration);
    CHECK_EQ_I(ppcp_peer_session_params(host.p)->coincidence_window_ns,
               PPCP_DEFAULT_COINCIDENCE_WINDOW_NS);
    CHECK_EQ_I(ppcp_peer_session_params(host.p)->issue_hold_ns,
               PPCP_DEFAULT_ISSUE_HOLD_NS);
    CHECK(!ppcp_peer_zero_host(host.p));

    TEST("I4 — the host's own Candidates need no relation: identity is not a relation");
    CHECK(am != NULL);
    CHECK_EQ_I(ppcp_arbiter_new(am, ppcp_arbiter_sizeof(), host.p, next_id, &ids, &arb),
               PPCP_OK);

    TEST("8.2a — the device's Candidates convert through a DECLARED relation");
    {
        ppcp_timebase_relation rel;
        ppcp_instant           at;
        CHECK_EQ_I(ppcp_instant_make_z(&at, "tb:dev", 0), PPCP_OK);
        CHECK_EQ_I(ppcp_relation_make_affine(&rel, "tb:dev", "tb:host", 0, 0.0,
                                             50000.0, 1.0, PPCP_RELM_ESTIMATED_ONLINE,
                                             &at), PPCP_OK);
        CHECK_EQ_I(ppcp_relations_put(ppcp_peer_relations(host.p), &rel), PPCP_OK);
    }

    TEST("CT-S4 (4) / 8.2b — the same two Candidates, hosted, produce ONE Shot with BOTH");
    CHECK_EQ_I(ppcp_candidate_make_canonical(&c1, "cand:1", &dev.src[1], &dev.cp[1],
                                             "acoustic", t_first, 0, 0.9, NULL), PPCP_OK);
    CHECK_EQ_I(ppcp_candidate_make_canonical(&c2, "cand:2", &dev.src[1], &dev.cp[1],
                                             "acoustic", t_second, 0, 0.2, NULL), PPCP_OK);
    CHECK_EQ_I(ppcp_arbiter_observe(arb, &c1, NULL), PPCP_OK);
    CHECK_EQ_I(ppcp_arbiter_observe(arb, &c2, NULL), PPCP_OK);
    CHECK_EQ_I(ppcp_arbiter_group_count(arb), 1);      /* 10 ms is inside 50 ms */

    /* 8.2h — no earlier than `issue_hold_ns` after the earliest contributing
     * Candidate.  Before that, nothing. */
    CHECK_EQ_I(ppcp_arbiter_pump(arb, t_first + PPCP_DEFAULT_ISSUE_HOLD_NS - 1, &issued),
               PPCP_OK);
    CHECK_EQ_I(issued, 0);
    CHECK_EQ_I(ppcp_arbiter_pump(arb, t_first + PPCP_DEFAULT_ISSUE_HOLD_NS, &issued),
               PPCP_OK);
    CHECK_EQ_I(issued, 1);
    CHECK_EQ_I(ppcp_arbiter_late_count(arb), 0);
    {
        const ppcp_shot *sh = ppcp_arbiter_shot_at(arb, 0);
        CHECK(sh != NULL);
        CHECK_EQ_I(sh->candidate_count, 2);            /* BOTH, which is the assertion */
        CHECK_EQ_I(sh->authority, PPCP_AUTHORITY_HOST);
        CHECK(ppcp_cbor_key_is(sh->t0.tb.v, sh->t0.tb.len, "tb:host"));
    }

    free(am);
    rig_free(&dev);
    rig_free(&host);
}

/* ============================================================ CT-I32 — silence */

static void test_silent_host(void)
{
    static const char *const dprof[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE,
                                         PPCP_PROFILE_DETECT, PPCP_PROFILE_MINT,
                                         PPCP_PROFILE_LIVE };
    static const char *const hprof[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_LIVE,
                                         PPCP_PROFILE_ARBITRATE };
    rig           dev, host;
    ppcp_session  s;
    void         *mm = malloc(ppcp_mint_sizeof());
    ppcp_mint    *m  = NULL;
    id_seq        ids = { "shot:d", 0 };
    promo         pol = { 0.5, 0 };
    size_t        minted = 0;
    const int64_t t0_ns = 1000000000;
    int64_t       deadline;

    rig_new(&host, PPCP_ROLE_HOST,    "peer:host", "tb:host", hprof, 3);
    rig_new(&dev,  PPCP_ROLE_CAPTURE, "peer:dev",  "tb:dev",  dprof, 5);

    CHECK_EQ_I(ppcp_session_make_hosted(&s, "sess:silent", "tb:host",
                                        PPCP_DEFAULT_COINCIDENCE_WINDOW_NS,
                                        PPCP_DEFAULT_ISSUE_HOLD_NS), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_session_open(host.p, &s), PPCP_OK);
    pump(host.p, dev.p, PPCP_CHANNEL_CONTROL);
    drop_events(dev.p);
    CHECK(ppcp_peer_session_params(dev.p) != NULL);
    CHECK(!ppcp_peer_zero_host(dev.p));          /* the host is present and answering */

    /* The device holds an affine relation to `timebase_ref`, so 8.2i1 is
     * satisfied and the only thing left is the deadline and the policy. */
    {
        ppcp_timebase_relation rel;
        ppcp_instant           at;
        CHECK_EQ_I(ppcp_instant_make_z(&at, "tb:dev", 0), PPCP_OK);
        CHECK_EQ_I(ppcp_relation_make_affine(&rel, "tb:dev", "tb:host", 0, 0.0, 50000.0,
                                             1.0, PPCP_RELM_ESTIMATED_ONLINE, &at),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_relations_put(ppcp_peer_relations(dev.p), &rel), PPCP_OK);
    }

    CHECK(mm != NULL);
    CHECK_EQ_I(ppcp_mint_new(mm, ppcp_mint_sizeof(), dev.p, next_id, &ids, &m), PPCP_OK);
    CHECK_EQ_I(ppcp_mint_set_promotion_policy(m, promote_above, &pol), PPCP_OK);

    deadline = t0_ns + PPCP_DEFAULT_ISSUE_HOLD_NS +
               (int64_t)PPCP_DEFAULT_HEARTBEAT_MS * 1000000;

    TEST("CT-I32 / 8.2i — nothing is minted before issue_hold_ns + heartbeat_interval_ms");
    nominate_at(&dev, m, "cand:believed", t0_ns, 0.9);
    pump(dev.p, host.p, PPCP_CHANNEL_CONTROL);
    drop_events(host.p);                          /* the host receives it and says nothing */
    CHECK_EQ_I(ppcp_mint_pump(m, deadline - 1, &minted), PPCP_OK);
    CHECK_EQ_I(minted, 0);
    CHECK_EQ_I(ppcp_mint_pending_count(m), 1);

    TEST("CT-I32 — and after it, a Candidate the peer's own policy WOULD have promoted");
    CHECK_EQ_I(ppcp_mint_pump(m, deadline, &minted), PPCP_OK);
    CHECK_EQ_I(minted, 1);

    TEST("CT-I32 — the negative half: the same silence, a Candidate below the floor");
    nominate_at(&dev, m, "cand:doubted", t0_ns, 0.2);
    CHECK_EQ_I(ppcp_mint_pump(m, deadline + 1000000000, &minted), PPCP_OK);
    CHECK_EQ_I(minted, 0);
    /* I8 — declined is not discarded.  The Candidate is retained with no Shot,
     * which is a legal and honest state, and its evidence reference survives. */
    CHECK_EQ_I(ppcp_mint_retained_count(m), 1);
    CHECK_EQ_I(ppcp_mint_minted_count(m), 1);

    TEST("8.2i — a `shot` answering a Candidate stops its deadline entirely");
    {
        ppcp_shot    sh;
        ppcp_instant t0;
        size_t       before = ppcp_mint_minted_count(m);
        nominate_at(&dev, m, "cand:answered", t0_ns, 0.99);
        CHECK_EQ_I(ppcp_instant_make_z(&t0, "tb:host", t0_ns), PPCP_OK);
        CHECK_EQ_I(ppcp_shot_make(&sh, "shot:host1", "sess:silent", &t0,
                                  PPCP_AUTHORITY_HOST, "peer:host", "cand:answered"),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_mint_observe_shot(m, &sh), PPCP_OK);
        CHECK_EQ_I(ppcp_mint_pump(m, deadline + 10000000000, &minted), PPCP_OK);
        CHECK_EQ_I(minted, 0);
        CHECK_EQ_I(ppcp_mint_minted_count(m), before);
    }

    TEST("CT-I32 — two peers with the SAME declared parameters agree on whether a Shot exists");
    {
        /* The invariant is about agreement, not about the answer: given one
         * Candidate, one Session and one promotion policy, two independent
         * engines must reach the same verdict at the same instant.  A
         * disagreement here is two conformant implementations disagreeing
         * about whether an event happened, which is what 8.2i exists to
         * prevent. */
        rig        twin;
        void      *tm  = malloc(ppcp_mint_sizeof());
        ppcp_mint *tw  = NULL;
        promo      same = { 0.5, 0 };
        id_seq     tids = { "shot:t", 0 };
        uint8_t    buf[8192];
        size_t     n = 0, consumed = 0, a_before = 0, b_before = 0;
        ppcp_candidate c;

        rig_new(&twin, PPCP_ROLE_CAPTURE, "peer:dev", "tb:dev", dprof, 5);
        CHECK_EQ_I(ppcp_peer_session_open(host.p, &s), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_drain(host.p, PPCP_CHANNEL_CONTROL, buf, sizeof(buf), &n),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_peer_feed(twin.p, PPCP_CHANNEL_CONTROL, buf, n, &consumed), PPCP_OK);
        drop_events(twin.p);
        {
            ppcp_timebase_relation rel;
            ppcp_instant           at;
            CHECK_EQ_I(ppcp_instant_make_z(&at, "tb:dev", 0), PPCP_OK);
            CHECK_EQ_I(ppcp_relation_make_affine(&rel, "tb:dev", "tb:host", 0, 0.0,
                                                 50000.0, 1.0, PPCP_RELM_ESTIMATED_ONLINE,
                                                 &at), PPCP_OK);
            CHECK_EQ_I(ppcp_relations_put(ppcp_peer_relations(twin.p), &rel), PPCP_OK);
        }
        CHECK(tm != NULL);
        CHECK_EQ_I(ppcp_mint_new(tm, ppcp_mint_sizeof(), twin.p, next_id, &tids, &tw),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_mint_set_promotion_policy(tw, promote_above, &same), PPCP_OK);

        CHECK_EQ_I(ppcp_candidate_make_canonical(&c, "cand:agree", &twin.src[1],
                                                 &twin.cp[1], "acoustic",
                                                 t0_ns + 2000000000, 0, 0.9, NULL),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_mint_observe_own(m,  &c), PPCP_OK);
        CHECK_EQ_I(ppcp_mint_observe_own(tw, &c), PPCP_OK);

        /* One nanosecond before the shared deadline: neither. */
        a_before = ppcp_mint_minted_count(m);
        b_before = ppcp_mint_minted_count(tw);
        CHECK_EQ_I(ppcp_mint_pump(m,  t0_ns + 2000000000 + PPCP_DEFAULT_ISSUE_HOLD_NS +
                                      (int64_t)PPCP_DEFAULT_HEARTBEAT_MS * 1000000 - 1,
                                  &minted), PPCP_OK);
        CHECK_EQ_I(minted, 0);
        CHECK_EQ_I(ppcp_mint_pump(tw, t0_ns + 2000000000 + PPCP_DEFAULT_ISSUE_HOLD_NS +
                                      (int64_t)PPCP_DEFAULT_HEARTBEAT_MS * 1000000 - 1,
                                  &minted), PPCP_OK);
        CHECK_EQ_I(minted, 0);
        /* And at it: both. */
        CHECK_EQ_I(ppcp_mint_pump(m,  t0_ns + 2000000000 + PPCP_DEFAULT_ISSUE_HOLD_NS +
                                      (int64_t)PPCP_DEFAULT_HEARTBEAT_MS * 1000000,
                                  &minted), PPCP_OK);
        CHECK_EQ_I(minted, 1);
        CHECK_EQ_I(ppcp_mint_pump(tw, t0_ns + 2000000000 + PPCP_DEFAULT_ISSUE_HOLD_NS +
                                      (int64_t)PPCP_DEFAULT_HEARTBEAT_MS * 1000000,
                                  &minted), PPCP_OK);
        CHECK_EQ_I(minted, 1);
        CHECK_EQ_I(ppcp_mint_minted_count(m)  - a_before, 1);
        CHECK_EQ_I(ppcp_mint_minted_count(tw) - b_before, 1);
        free(tm);
        rig_free(&twin);
    }

    TEST("CT-I32 / 8.2i1 — a peer whose timebases are `unrelated` mints NOTHING");
    {
        static const char *const uprof[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE,
                                             PPCP_PROFILE_DETECT, PPCP_PROFILE_MINT,
                                             PPCP_PROFILE_LIVE };
        rig        u;
        void      *um = malloc(ppcp_mint_sizeof());
        ppcp_mint *um2 = NULL;
        promo      always = { 0.0, 0 };
        uint8_t    buf[8192];
        size_t     n = 0, consumed = 0;
        ppcp_timebase_relation unrel;
        ppcp_instant           at;

        rig_new(&u, PPCP_ROLE_CAPTURE, "peer:unrel", "tb:unrel", uprof, 5);
        CHECK_EQ_I(ppcp_peer_session_open(host.p, &s), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_drain(host.p, PPCP_CHANNEL_CONTROL, buf, sizeof(buf), &n),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_peer_feed(u.p, PPCP_CHANNEL_CONTROL, buf, n, &consumed), PPCP_OK);
        drop_events(u.p);

        /* 5.4b — `unrelated` is a legal, COMPLETE declaration.  The honest
         * consequence is that `t0` cannot be expressed in `timebase_ref`. */
        CHECK_EQ_I(ppcp_instant_make_z(&at, "tb:unrel", 0), PPCP_OK);
        CHECK_EQ_I(ppcp_relation_make_unrelated(&unrel, "tb:unrel", "tb:host",
                                                PPCP_RELM_DECLARED, &at), PPCP_OK);
        CHECK_EQ_I(ppcp_relations_put(ppcp_peer_relations(u.p), &unrel), PPCP_OK);

        CHECK(um != NULL);
        CHECK_EQ_I(ppcp_mint_new(um, ppcp_mint_sizeof(), u.p, next_id, &ids, &um2), PPCP_OK);
        CHECK_EQ_I(ppcp_mint_set_promotion_policy(um2, promote_above, &always), PPCP_OK);
        nominate_at(&u, um2, "cand:u1", t0_ns, 1.0);
        nominate_at(&u, um2, "cand:u2", t0_ns + 500000000, 1.0);
        CHECK_EQ_I(ppcp_mint_pump(um2, t0_ns + 100000000000, &minted), PPCP_OK);
        CHECK_EQ_I(minted, 0);
        CHECK_EQ_I(ppcp_mint_minted_count(um2), 0);
        /* Every Candidate retained, none discarded, no Shot invented and no
         * zero offset substituted (5.4b, I8). */
        CHECK_EQ_I(ppcp_mint_retained_count(um2), 2);
        /* The promotion policy was never even reached: 8.2i1 is decided before
         * policy, because a Shot the peer cannot express is not a decision it
         * gets to make. */
        CHECK_EQ_I(always.calls, 0);
        free(um);
        rig_free(&u);
    }

    free(mm);
    rig_free(&dev);
    rig_free(&host);
}

/* ============================== CT-S4 (1) — a hostless Session, end to end */

/* Everything the device queued, appended to the bundle exactly as it would
 * have been sent.  ENC 7a: live bytes ARE bundle bytes, and this function is
 * the whole of the claim. */
static void record(ppcp_peer *p, ppcp_bundle_writer *w, uint8_t ch, uint8_t *out,
                   size_t cap, size_t *len)
{
    uint8_t frames[65536];
    size_t  n = 0, got = 0;
    while (ppcp_peer_pending(p, ch) > 0) {
        CHECK_EQ_I(ppcp_peer_drain(p, ch, frames, sizeof(frames), &n), PPCP_OK);
        if (n == 0)
            break;
        CHECK_EQ_I(ppcp_bundle_writer_append_frames(w, frames, n, out + *len, cap - *len,
                                                    &got), PPCP_OK);
        *len += got;
    }
}

static void test_hostless_end_to_end(void)
{
    static const char *const prof[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE,
                                        PPCP_PROFILE_DETECT, PPCP_PROFILE_MINT,
                                        PPCP_PROFILE_OFFLINE };
    rig                 d, sink;
    ppcp_session        s;
    ppcp_stream         st;
    ppcp_readiness      ready;
    ppcp_capture        cap;
    ppcp_instant        opened;
    void               *wm  = malloc(ppcp_bundle_writer_sizeof());
    void               *rm  = malloc(ppcp_bundle_reader_sizeof());
    void               *mm  = malloc(ppcp_mint_sizeof());
    ppcp_bundle_writer *w   = NULL;
    ppcp_bundle_reader *rd  = NULL;
    ppcp_mint          *m   = NULL;
    id_seq              ids = { "shot:e", 0 };
    promo               pol = { 0.5, 0 };
    static uint8_t      bundle[131072];
    size_t              len = 0, got = 0, minted = 0;
    const int64_t       t   = 1000000000;

    CHECK(wm != NULL && rm != NULL && mm != NULL);
    rig_new(&d, PPCP_ROLE_CAPTURE, "peer:dev", "tb:dev", prof, 5);
    CHECK_EQ_I(ppcp_bundle_writer_new(wm, ppcp_bundle_writer_sizeof(), &w), PPCP_OK);
    CHECK_EQ_I(ppcp_bundle_writer_begin(w, bundle, sizeof(bundle), &got), PPCP_OK);
    len = got;

    TEST("CT-S4 (1) — declare, session_open, stream_open, readiness, candidates, shot");
    /* `declare` is already queued by rig_new(); it goes into the bundle first,
     * which is what makes the Captures below attributable (a finding H raised
     * in S2: ENC §7 does not require it, and it must). */
    record(d.p, w, PPCP_CHANNEL_CONTROL, bundle, sizeof(bundle), &len);

    CHECK_EQ_I(ppcp_session_make_hostless(&s, "sess:e2e", "tb:dev"), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_session_open(d.p, &s), PPCP_OK);
    record(d.p, w, PPCP_CHANNEL_CONTROL, bundle, sizeof(bundle), &len);
    CHECK(ppcp_bundle_writer_is_hostless(w));

    TEST("CORE 7.3b — and NO `arm`, because nobody is controlling");
    {
        /* CONF §4.4 assertion 1 lists `arm` in the hostless end-to-end run,
         * and 7.3b forbids recording one: `arm` is conferred by Live and a
         * hostless bundle carries the EFFECT — Streams, readiness, Captures —
         * not a command nobody sent.  Finding queued for L17. */
        ppcp_msg arm;
        size_t   n = 0;
        CHECK_EQ_I(ppcp_msg_init(&arm, PPCP_MT_ARM, 1), PPCP_OK);
        CHECK_EQ_I(ppcp_bundle_writer_append_msg(w, PPCP_CHANNEL_CONTROL, &arm,
                                                 bundle + len, sizeof(bundle) - len, &n),
                   PPCP_ERR_INVALID);
        /* The device could not have originated one either: 7.3a makes arming
         * host-controlled and this peer is not the host. */
        CHECK_EQ_I(ppcp_peer_arm(d.p, NULL, 0), PPCP_ERR_INVALID);
    }

    CHECK_EQ_I(ppcp_instant_make_z(&opened, "tb:dev", t - 100000000), PPCP_OK);
    CHECK_EQ_I(ppcp_stream_make(&st, "stream:video", "sess:e2e", "src:cam",
                                PPCP_STREAM_KIND_VIDEO, "cp:cam", "tb:dev",
                                PPCP_SHOT_WINDOWED, &opened), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_stream_open(d.p, &st), PPCP_OK);
    /* 5.15a — readiness is a MEASUREMENT; no state name crosses the wire. */
    CHECK_EQ_I(ppcp_readiness_settled(&ready), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_readiness(d.p, &ready, NULL, 0), PPCP_OK);
    record(d.p, w, PPCP_CHANNEL_CONTROL, bundle, sizeof(bundle), &len);

    CHECK_EQ_I(ppcp_mint_new(mm, ppcp_mint_sizeof(), d.p, next_id, &ids, &m), PPCP_OK);
    CHECK_EQ_I(ppcp_mint_set_promotion_policy(m, promote_above, &pol), PPCP_OK);
    nominate_at(&d, m, "cand:e1", t, 0.9);
    nominate_at(&d, m, "cand:e2", t + 10000000, 0.2);
    CHECK_EQ_I(ppcp_mint_pump(m, t + 10000000, &minted), PPCP_OK);
    CHECK_EQ_I(minted, 1);
    record(d.p, w, PPCP_CHANNEL_CONTROL, bundle, sizeof(bundle), &len);

    TEST("CT-S4 (1) — a Capture anchored to the Shot, then the manifest, then the bundle");
    {
        ppcp_id shot_id;
        CHECK_EQ_I(ppcp_id_set_z(&shot_id, "shot:e01"), PPCP_OK);
        CHECK_EQ_I(ppcp_capture_make_shot(&cap, "cap:e1", "shot:e01", "stream:video",
                                          PPCP_COMPLETE), PPCP_OK);
        {
            ppcp_interval iv;
            CHECK_EQ_I(ppcp_interval_make(&iv, "tb:dev", strlen("tb:dev"),
                                          t - 500000000, t + 500000000), PPCP_OK);
            CHECK_EQ_I(ppcp_capture_set_interval(&cap, &iv), PPCP_OK);
        }
        CHECK_EQ_I(ppcp_peer_capture_announce(d.p, &cap, false, NULL, NULL, 0), PPCP_OK);
    }

    TEST("CT-I27 / I11 — a `{stream: true}` Capture and gaps are refused on a "
         "shot_windowed Stream, at ORIGINATION");
    {
        /* F-D4-1 — the engine holds the Stream it opened, so 5.14d and I11 are
         * checked on the way out rather than left for a receiver to notice.
         * `stream:video` above is `shot_windowed`. */
        ppcp_capture  seg;
        ppcp_interval iv;
        CHECK_EQ_I(ppcp_interval_make(&iv, "tb:dev", strlen("tb:dev"), t, t + 1000000),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_capture_make_segment(&seg, "cap:seg", "stream:video",
                                             PPCP_COMPLETE, &iv), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_capture_announce(d.p, &seg, false, NULL, NULL, 0),
                   PPCP_ERR_INVALID);

        /* And a gap on a Capture that is otherwise legal here. */
        {
            ppcp_capture  gapped;
            ppcp_interval gap;
            CHECK_EQ_I(ppcp_capture_make_shot(&gapped, "cap:gap", "shot:e01",
                                              "stream:video", PPCP_PARTIAL), PPCP_OK);
            CHECK_EQ_I(ppcp_capture_set_interval(&gapped, &iv), PPCP_OK);
            CHECK_EQ_I(ppcp_interval_make(&gap, "tb:dev", strlen("tb:dev"),
                                          t + 100000, t + 200000), PPCP_OK);
            CHECK_EQ_I(ppcp_capture_add_gap(&gapped, &gap), PPCP_OK);
            CHECK_EQ_I(ppcp_peer_capture_announce(d.p, &gapped, false, NULL, NULL, 0),
                       PPCP_ERR_INVALID);
        }

        /* 5.11j — and the caller's `is_preview` must agree with the Stream it
         * named, rather than being a claim the engine takes on trust. */
        CHECK_EQ_I(ppcp_peer_capture_announce(d.p, &cap, true, NULL, NULL, 0),
                   PPCP_ERR_INVALID);
    }
    {
        ppcp_body_session_manifest man;
        memset(&man, 0, sizeof(man));
        CHECK_EQ_I(ppcp_id_set_z(&man.session_id, "sess:e2e"), PPCP_OK);
        CHECK_EQ_I(ppcp_id_set_z(&man.streams[0], "stream:video"), PPCP_OK);
        man.stream_count   = 1;
        CHECK_EQ_I(ppcp_id_set_z(&man.captures[0].capture_id, "cap:e1"), PPCP_OK);
        CHECK_EQ_I(ppcp_id_set_z(&man.captures[0].stream_id, "stream:video"), PPCP_OK);
        man.captures[0].bytes = 1024;
        man.capture_count  = 1;
        man.completeness   = PPCP_COMPLETE;
        man.count_shots    = 1;
        man.count_candidates = 2;
        man.count_captures = 1;
        CHECK_EQ_I(ppcp_peer_session_manifest(d.p, &man), PPCP_OK);
    }
    record(d.p, w, PPCP_CHANNEL_CONTROL, bundle, sizeof(bundle), &len);
    CHECK(ppcp_bundle_writer_has_manifest(w));
    CHECK_EQ_I(ppcp_bundle_writer_finish(w), PPCP_OK);

    TEST("CT-S4 (1) / ENC 7a — the same bytes read back through the ordinary feed");
    rig_new(&sink, PPCP_ROLE_CAPTURE, "peer:reader", "tb:reader", prof, 5);
    CHECK_EQ_I(ppcp_bundle_reader_new(rm, ppcp_bundle_reader_sizeof(), sink.p, &rd),
               PPCP_OK);
    {
        /* Fed in windows, with the sink's events drained between them.  The
         * event ring is four deep by design (a caller that is not draining has
         * already lost the earlier events' timeliness), so a reader that
         * swallowed a whole session in one call would be testing the ring
         * rather than the bundle. */
        size_t off = 0;
        bool   session = false, stream = false, shot = false, capture = false;
        size_t candidates = 0;

        while (off < len) {
            size_t win = 256, c = 0;
            for (;;) {
                if (off + win > len)
                    win = len - off;
                CHECK_EQ_I(ppcp_bundle_reader_feed(rd, bundle + off, win, &c), PPCP_OK);
                if (c > 0 || win == len - off)
                    break;
                win *= 2;
            }
            off += c;
            {
                ppcp_event ev;
                while (ppcp_peer_next_event(sink.p, &ev) == PPCP_OK) {
                    if (ev.msg == NULL)
                        continue;
                    switch (ev.msg->type) {
                    case PPCP_MT_SESSION_OPEN:
                        session = true;
                        /* 4.1d / 5.10e — neither arbitration parameter, which
                         * IS the bundle's statement that no arbitration
                         * occurred. */
                        CHECK(!ev.msg->body.session_open.has_arbitration);
                        break;
                    case PPCP_MT_STREAM_OPEN:      stream = true;  break;
                    case PPCP_MT_CANDIDATE:        candidates++;   break;
                    case PPCP_MT_SHOT:
                        shot = true;
                        CHECK_EQ_I(ev.msg->body.shot.shot.authority, PPCP_AUTHORITY_DEVICE);
                        CHECK_EQ_I(ev.msg->body.shot.shot.candidate_count, 1);
                        break;
                    case PPCP_MT_CAPTURE_ANNOUNCE: capture = true; break;
                    default: break;
                    }
                }
            }
            if (c == 0)
                break;
        }
        CHECK_EQ_I(off, len);

        TEST("CT-S4 (1)(3) — Session, Stream, Shot and Capture all arrive");
        CHECK(session && stream && shot && capture);
        /* 7.1d / I8 — BOTH Candidates reached the bundle, including the one
         * the detector did not promote. */
        CHECK_EQ_I(candidates, 2);
    }
    {
        ppcp_completeness c = PPCP_UNKNOWN;
        CHECK_EQ_I(ppcp_bundle_reader_finish(rd, &c), PPCP_OK);
        /* ENC 7d / I10 — the manifest ASSERTED `complete`, so that is what the
         * Session is.  Nothing was inferred from the bytes having all
         * arrived. */
        CHECK_EQ_I(c, PPCP_COMPLETE);
        CHECK(!ppcp_bundle_reader_truncated(rd));
        CHECK(ppcp_bundle_reader_manifest_ordered(rd));
    }
    CHECK(ppcp_peer_session_id(sink.p) != NULL);

    free(wm);
    free(rm);
    free(mm);
    rig_free(&d);
    rig_free(&sink);
}

int main(void)
{
    test_canonical();
    test_zero_host();
    test_hosted_pair();
    test_silent_host();
    test_hostless_end_to_end();
    TEST_MAIN_END();
}
