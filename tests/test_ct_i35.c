/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * Arbitration — work package L10's evidence for the host half.
 *
 * Rows this file carries:
 *
 *   CT-I6    every Shot references at least one Candidate, and a Shot with
 *            zero Candidates FROM ONE PEER is legal.
 *   CT-I7    a Candidate delivered after issue attaches, and `t0` is
 *            byte-identical before and after.
 *   CT-I8    a Candidate excluded for an over-wide sigma is present in
 *            `Shot.candidates` with its evidence reference intact; two
 *            Candidates of the SAME `basis` from DIFFERENT peers are both
 *            retained and both appear.
 *   CT-I9    no operation merges or rewrites a Shot; every `confirmed: true`
 *            link carries `confirmed_by`; a retrospective basis is never
 *            `confirmed_by: observer`.
 *   CT-I20   a peer that is not `role: host` cannot arbitrate.
 *   CT-I35   a device-minted `shot` is attached to, not competed with; a
 *            forced collision is linked with `basis: shared_candidate`; two
 *            extensions in either order converge on the same list.
 *   CORE 8.4 a `capture_request` for an interval that is gone is answered with
 *            a Capture, not an `error`.
 */
#include "ppcp/ppcp.h"

#include "test_util.h"

/* ------------------------------------------------------------------ rigging */

typedef struct rig {
    void      *mem;
    ppcp_peer *p;
    ppcp_id              profiles[8];
    ppcp_timebase        tb[1];
    ppcp_capture_profile cp[1];
    ppcp_source          src[1];
    ppcp_peer_desc       desc;
} rig;

static void rig_new(rig *r, ppcp_role role, const char *id, const char *tb_id,
                    const char *const *profiles, size_t nprof)
{
    ppcp_peer_config cfg;
    ppcp_timing      timing;
    size_t           i;

    memset(r, 0, sizeof(*r));
    memset(&cfg, 0, sizeof(cfg));
    cfg.role          = role;
    cfg.peer_id       = id;
    cfg.profiles      = profiles;
    cfg.profile_count = nprof;
    /* F-H5-3: Live is refused without one, and every rig here declares Live. */
    cfg.health_report = ppcp_test_health;
    r->mem = malloc(ppcp_peer_sizeof());
    if (r->mem == NULL) abort();
    if (ppcp_peer_new(r->mem, ppcp_peer_sizeof(), &cfg, &r->p) != PPCP_OK) abort();

    for (i = 0; i < nprof; i++)
        if (ppcp_id_set_z(&r->profiles[i], profiles[i]) != PPCP_OK) abort();
    if (ppcp_timebase_make(&r->tb[0], tb_id, strlen(tb_id), PPCP_TB_CONTINUOUS, true, 1000)
        != PPCP_OK) abort();
    if (ppcp_timing_make(&timing, PPCP_CONV_MID) != PPCP_OK) abort();
    if (ppcp_capture_profile_make(&r->cp[0], "cp:mic", &timing) != PPCP_OK) abort();
    if (ppcp_source_make(&r->src[0], "src:mic", id, "microphone", tb_id, true, r->cp, 1)
        != PPCP_OK) abort();
    if (ppcp_peer_desc_make(&r->desc, id, role, "1.0", r->profiles, nprof, r->tb, 1)
        != PPCP_OK) abort();
    if (ppcp_peer_desc_set_sources(&r->desc, r->src, 1) != PPCP_OK) abort();
    if (ppcp_peer_declare(r->p, &r->desc) != PPCP_OK) abort();
}

static void rig_free(rig *r) { ppcp_peer_free(r->p); free(r->mem); }

/* F-L13-1: the receiver stops feeding when its event queue is full, so a pump
 * that DEQUEUED first would throw away the frames it could not deliver.  It
 * peeks and commits exactly what was taken instead — the socket idiom of
 * peer.h — so an undelivered frame stays queued for the next pump, after the
 * caller has drained.  Nothing is lost either way. */
static void pump(ppcp_peer *from, ppcp_peer *to, uint8_t ch)
{
    while (ppcp_peer_pending(from, ch) > 0) {
        const uint8_t *view = NULL;
        size_t         len = 0, consumed = 0;
        if (ppcp_peer_drain_peek(from, ch, &view, &len) != PPCP_OK || len == 0)
            break;
        if (ppcp_peer_feed(to, ch, view, len, &consumed) != PPCP_OK || consumed == 0)
            break;
        if (ppcp_peer_drain_commit(from, ch, consumed) != PPCP_OK)
            break;
    }
}

static void drop_events(ppcp_peer *p)
{
    ppcp_event e;
    while (ppcp_peer_next_event(p, &e) == PPCP_OK) { }
}

typedef struct id_seq { const char *prefix; unsigned n; } id_seq;

static ppcp_result next_id(void *ctx, ppcp_id *out)
{
    id_seq     *s = (id_seq *)ctx;
    char        buf[64];
    size_t      i = 0;
    const char *pfx = s->prefix;
    unsigned    v = ++s->n;
    while (*pfx && i < sizeof(buf) - 4)
        buf[i++] = *pfx++;
    buf[i++] = (char)('0' + (v / 10) % 10);
    buf[i++] = (char)('0' + v % 10);
    buf[i]   = '\0';
    return ppcp_id_set_z(out, buf);
}

/* The host's exclusion policy: I14 keeps the number here, not in the library. */
typedef struct sigma_policy { double max_ns; size_t calls; } sigma_policy;

static bool sigma_under(void *ctx, const ppcp_candidate *c,
                        const ppcp_timebase_relation *rel, double sigma_ns)
{
    sigma_policy *sp = (sigma_policy *)ctx;
    (void)c; (void)rel;
    sp->calls++;
    return sigma_ns <= sp->max_ns;
}

static void make_cand(ppcp_candidate *c, const char *id, const char *peer, const char *src,
                      const char *tb, const char *basis, int64_t ns, double confidence)
{
    ppcp_instant at;
    if (ppcp_instant_make_z(&at, tb, ns) != PPCP_OK) abort();
    if (ppcp_candidate_make(c, id, peer, src, basis, &at, confidence) != PPCP_OK) abort();
}

/* A host engine that has LEARNED its Session from a `session_open` frame — the
 * arbiter needs the two declared parameters and a peer learns them from the
 * wire, not from having composed the message. */
static void host_with_session(rig *host, const char *const *hprof, size_t nprof,
                              const ppcp_session *s)
{
    rig     writer;
    uint8_t buf[8192];
    size_t  n = 0, consumed = 0;

    rig_new(&writer, PPCP_ROLE_HOST, "peer:host", "tb:host", hprof, nprof);
    rig_new(host,    PPCP_ROLE_HOST, "peer:host", "tb:host", hprof, nprof);
    if (ppcp_peer_session_open(writer.p, s) != PPCP_OK) abort();
    if (ppcp_peer_drain(writer.p, PPCP_CHANNEL_CONTROL, buf, sizeof(buf), &n) != PPCP_OK)
        abort();
    if (ppcp_peer_feed(host->p, PPCP_CHANNEL_CONTROL, buf, n, &consumed) != PPCP_OK)
        abort();
    drop_events(host->p);
    rig_free(&writer);
}

static const char *const HPROF[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE,
                                     PPCP_PROFILE_DETECT, PPCP_PROFILE_ARBITRATE,
                                     PPCP_PROFILE_LIVE };

/* A relation for the device's clock, with the sigma the test wants. */
static void put_relation(ppcp_peer *p, const char *from, const char *to, double sigma_ns)
{
    ppcp_timebase_relation rel;
    ppcp_instant           at;
    if (ppcp_instant_make_z(&at, from, 0) != PPCP_OK) abort();
    if (ppcp_relation_make_affine(&rel, from, to, 0, 0.0, sigma_ns, 0.0,
                                  PPCP_RELM_ESTIMATED_ONLINE, &at) != PPCP_OK) abort();
    if (ppcp_relations_put(ppcp_peer_relations(p), &rel) != PPCP_OK) abort();
}

/* =========================================================== CT-I20, CT-I8, I7 */

static void test_arbitration(void)
{
    static const char *const dprof[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE,
                                         PPCP_PROFILE_DETECT, PPCP_PROFILE_MINT,
                                         PPCP_PROFILE_LIVE };
    rig            host, dev;
    ppcp_session   s;
    void          *am = malloc(ppcp_arbiter_sizeof());
    ppcp_arbiter  *arb = NULL;
    id_seq         ids = { "shot:h", 0 };
    sigma_policy   pol = { 5000000.0, 0 };   /* 5 ms of clock uncertainty, host policy */
    ppcp_candidate hostmic, devmic, wide, late;
    ppcp_instant   t0_before;
    size_t         issued = 0;
    bool           excluded = false;
    const int64_t  t = 1000000000;

    CHECK_EQ_I(ppcp_session_make_hosted(&s, "sess:arb", "tb:host",
                                        PPCP_DEFAULT_COINCIDENCE_WINDOW_NS,
                                        PPCP_DEFAULT_ISSUE_HOLD_NS), PPCP_OK);
    host_with_session(&host, HPROF, 5, &s);
    rig_new(&dev, PPCP_ROLE_CAPTURE, "peer:dev", "tb:dev", dprof, 5);

    TEST("CT-I20 / I20 — a peer that is not `role: host` cannot arbitrate");
    {
        ppcp_arbiter *bad = NULL;
        void         *bm  = malloc(ppcp_arbiter_sizeof());
        CHECK_EQ_I(ppcp_arbiter_new(bm, ppcp_arbiter_sizeof(), dev.p, next_id, &ids, &bad),
                   PPCP_ERR_INVALID);
        free(bm);
    }

    CHECK(am != NULL);
    CHECK_EQ_I(ppcp_arbiter_new(am, ppcp_arbiter_sizeof(), host.p, next_id, &ids, &arb),
               PPCP_OK);
    CHECK_EQ_I(ppcp_arbiter_set_policy(arb, sigma_under, &pol), PPCP_OK);

    /* Two device clocks: one well synchronised, one whose relation is far too
     * wide for the host's policy. */
    put_relation(host.p, "tb:dev",   "tb:host", 200000.0);      /* 0.2 ms */
    put_relation(host.p, "tb:vague", "tb:host", 40000000.0);    /* 40 ms */

    TEST("CT-I8 — two Candidates of the SAME basis from DIFFERENT peers both appear");
    /* The host's own microphone and the device's microphone, 4 ms apart.  An
     * arbiter with one slot per modality silently drops the second, and the
     * failure is invisible — which is the whole reason this assertion exists. */
    make_cand(&hostmic, "cand:hostmic", "peer:host", "src:mic", "tb:host", "acoustic",
              t, 0.8);
    make_cand(&devmic,  "cand:devmic",  "peer:dev",  "src:mic", "tb:dev",  "acoustic",
              t + 4000000, 0.9);
    CHECK_EQ_I(ppcp_arbiter_observe(arb, &hostmic, &excluded), PPCP_OK);
    CHECK(!excluded);
    CHECK_EQ_I(ppcp_arbiter_observe(arb, &devmic, &excluded), PPCP_OK);
    CHECK(!excluded);
    CHECK_EQ_I(ppcp_arbiter_group_count(arb), 1);

    TEST("CT-I8 / 8.2d — a Candidate excluded for an over-wide sigma is RETAINED");
    make_cand(&wide, "cand:wide", "peer:vague", "src:mic", "tb:vague", "acoustic",
              t + 6000000, 0.7);
    CHECK_EQ_I(ppcp_candidate_set_evidence(&wide, "cap:audio-wide"), PPCP_OK);
    CHECK_EQ_I(ppcp_arbiter_observe(arb, &wide, &excluded), PPCP_OK);
    CHECK(excluded);
    CHECK_EQ_I(ppcp_arbiter_group_count(arb), 1);   /* it joined, it did not start one */

    TEST("8.2h — nothing is issued before `issue_hold_ns` after the earliest");
    CHECK_EQ_I(ppcp_arbiter_pump(arb, t + PPCP_DEFAULT_ISSUE_HOLD_NS - 1, &issued), PPCP_OK);
    CHECK_EQ_I(issued, 0);

    TEST("8.2f / CT-I8 — the issued Shot carries EVERY contributing and excluded Candidate");
    CHECK_EQ_I(ppcp_arbiter_pump(arb, t + PPCP_DEFAULT_ISSUE_HOLD_NS, &issued), PPCP_OK);
    CHECK_EQ_I(issued, 1);
    {
        const ppcp_shot *sh = ppcp_arbiter_shot_at(arb, 0);
        size_t i, found = 0;
        CHECK(sh != NULL);
        CHECK_EQ_I(sh->candidate_count, 3);
        for (i = 0; i < sh->candidate_count; i++)
            if (ppcp_cbor_key_is(sh->candidates[i].v, sh->candidates[i].len, "cand:wide"))
                found++;
        CHECK_EQ_I(found, 1);
        /* The excluded Candidate did not set `t0`: the host microphone's own
         * clock has sigma 0 (identity, I4) and is the least uncertain. */
        CHECK_EQ_I(sh->t0.ns, t);
        CHECK_EQ_I(sh->authority, PPCP_AUTHORITY_HOST);
        t0_before = sh->t0;
    }

    TEST("CT-I7 / 8.2e — a Candidate arriving AFTER issue attaches, and `t0` does not move");
    make_cand(&late, "cand:late", "peer:dev", "src:mic", "tb:dev", "acoustic",
              t + 8000000, 0.6);
    CHECK_EQ_I(ppcp_arbiter_observe(arb, &late, &excluded), PPCP_OK);
    {
        const ppcp_shot *sh = ppcp_arbiter_shot_at(arb, 0);
        CHECK(sh != NULL);
        CHECK_EQ_I(sh->candidate_count, 4);
        /* Byte-identical, which is what CT-I7 asks for. */
        CHECK(memcmp(&sh->t0, &t0_before, sizeof(t0_before)) == 0);
    }

    TEST("CT-I6 — a Shot may have zero Candidates from a given peer, and does");
    {
        const ppcp_shot *sh = ppcp_arbiter_shot_at(arb, 0);
        size_t i, from_other = 0;
        CHECK(sh->candidate_count >= 1);              /* I6's positive half */
        for (i = 0; i < sh->candidate_count; i++)
            if (ppcp_cbor_key_is(sh->candidates[i].v, sh->candidates[i].len, "cand:nobody"))
                from_other++;
        CHECK_EQ_I(from_other, 0);
    }

    TEST("8.2d — a Candidate with NO relation to `timebase_ref` is retained, not grouped");
    {
        ppcp_candidate orphan;
        size_t         before = ppcp_arbiter_retained_count(arb);
        make_cand(&orphan, "cand:orphan", "peer:x", "src:mic", "tb:nowhere", "acoustic",
                  t + 1000000, 0.9);
        CHECK_EQ_I(ppcp_arbiter_observe(arb, &orphan, &excluded), PPCP_OK);
        CHECK(excluded);
        CHECK_EQ_I(ppcp_arbiter_retained_count(arb), before + 1);
        CHECK_EQ_I(ppcp_arbiter_group_count(arb), 1);   /* it is not an event */
    }

    TEST("5.4b / 8.2d — an `unrelated` declaration is excluded and retained, not zeroed");
    {
        ppcp_candidate unrel;
        ppcp_timebase_relation u;
        ppcp_instant           at;
        size_t                 before = ppcp_arbiter_retained_count(arb);
        CHECK_EQ_I(ppcp_instant_make_z(&at, "tb:android", 0), PPCP_OK);
        CHECK_EQ_I(ppcp_relation_make_unrelated(&u, "tb:android", "tb:host",
                                                PPCP_RELM_DECLARED, &at), PPCP_OK);
        CHECK_EQ_I(ppcp_relations_put(ppcp_peer_relations(host.p), &u), PPCP_OK);
        make_cand(&unrel, "cand:android", "peer:a", "src:mic", "tb:android", "acoustic",
                  t + 2000000, 0.9);
        CHECK_EQ_I(ppcp_arbiter_observe(arb, &unrel, &excluded), PPCP_OK);
        CHECK(excluded);
        CHECK_EQ_I(ppcp_arbiter_retained_count(arb), before + 1);
    }

    /* F-S5-1 / erratum E29.  On a live link the relation ALWAYS arrives late —
     * §6.3's burst is still converging while the first swings are taken — and
     * before this the host left those Candidates retained for the whole
     * Session with no error, no Shot, and every Candidate present exactly as
     * 8.2d requires.  Invisible from both ends. */
    TEST("8.2d1 (E29) — a Candidate retained for want of a relation is reconsidered");
    {
        size_t before = ppcp_arbiter_retained_count(arb);
        size_t groups_before = ppcp_arbiter_group_count(arb);

        /* Nothing has changed yet, so nothing is re-admitted and nothing is
         * lost — reconsidering is not a way to discard evidence. */
        CHECK_EQ_I(ppcp_arbiter_reconsider(arb), 0);
        CHECK_EQ_I(ppcp_arbiter_retained_count(arb), before);

        /* The relation `cand:orphan` was waiting for.  Its instant falls inside
         * the coincidence window of the group already issued, so 8.2e's
         * attachment is what it gets — which is the half that matters: `t0` is
         * NOT revised (I7) and the Candidate is on the Shot. */
        {
            const ppcp_shot *sh = ppcp_arbiter_shot_at(arb, 0);
            ppcp_instant     t0_was;
            size_t           cands_before;
            CHECK(sh != NULL);
            t0_was       = sh->t0;
            cands_before = sh->candidate_count;

            put_relation(host.p, "tb:nowhere", "tb:host", 200000.0);
            CHECK_EQ_I(ppcp_arbiter_reconsider(arb), 1);
            CHECK_EQ_I(ppcp_arbiter_retained_count(arb), before - 1);
            CHECK_EQ_I(ppcp_arbiter_group_count(arb), groups_before);

            sh = ppcp_arbiter_shot_at(arb, 0);
            CHECK(sh != NULL);
            CHECK_EQ_I(sh->candidate_count, cands_before + 1);
            CHECK(memcmp(&sh->t0, &t0_was, sizeof(t0_was)) == 0);
        }

        /* `cand:android` declared its clock UNRELATED (5.4b), which is a
         * statement and not a gap: it stays retained however often this is
         * called, and no zero offset is substituted for it. */
        CHECK_EQ_I(ppcp_arbiter_reconsider(arb), 0);
        CHECK_EQ_I(ppcp_arbiter_retained_count(arb), before - 1);
    }

    free(am);
    rig_free(&host);
    rig_free(&dev);
}

/* ==================================================================== CT-I35 */

static void test_device_minted(void)
{
    rig           host;
    ppcp_session  s;
    void         *am = malloc(ppcp_arbiter_sizeof());
    ppcp_arbiter *arb = NULL;
    id_seq        ids = { "shot:h", 0 };
    ppcp_candidate hostmic;
    ppcp_shot      devshot;
    ppcp_instant   dev_t0;
    const int64_t  t = 1000000000;

    CHECK_EQ_I(ppcp_session_make_hosted(&s, "sess:race", "tb:host",
                                        PPCP_DEFAULT_COINCIDENCE_WINDOW_NS,
                                        PPCP_DEFAULT_ISSUE_HOLD_NS), PPCP_OK);
    host_with_session(&host, HPROF, 5, &s);
    put_relation(host.p, "tb:dev", "tb:host", 200000.0);

    CHECK(am != NULL);
    CHECK_EQ_I(ppcp_arbiter_new(am, ppcp_arbiter_sizeof(), host.p, next_id, &ids, &arb),
               PPCP_OK);

    TEST("CT-I35 / 8.2k — a device-minted `shot` is ATTACHED to, never competed with");
    make_cand(&hostmic, "cand:hostmic", "peer:host", "src:mic", "tb:host", "acoustic",
              t, 0.8);
    CHECK_EQ_I(ppcp_arbiter_observe(arb, &hostmic, NULL), PPCP_OK);
    /* The device minted for a Candidate the host is also holding — here the
     * device's own, which the host received and grouped. */
    {
        ppcp_candidate devcand;
        make_cand(&devcand, "cand:devmic", "peer:dev", "src:mic", "tb:dev", "acoustic",
                  t + 3000000, 0.9);
        CHECK_EQ_I(ppcp_arbiter_observe(arb, &devcand, NULL), PPCP_OK);
    }
    CHECK_EQ_I(ppcp_instant_make_z(&dev_t0, "tb:host", t + 3000000), PPCP_OK);
    CHECK_EQ_I(ppcp_shot_make(&devshot, "shot:dev01", "sess:race", &dev_t0,
                              PPCP_AUTHORITY_DEVICE, "peer:dev", "cand:devmic"), PPCP_OK);
    drop_events(host.p);
    CHECK_EQ_I(ppcp_arbiter_observe_shot(arb, &devshot), PPCP_OK);

    {
        const ppcp_shot *sh = ppcp_arbiter_shot_at(arb, 0);
        CHECK(sh != NULL);
        TEST("CT-I35 — the attaching peer changes ONLY `candidates`");
        CHECK(ppcp_cbor_key_is(sh->id.v, sh->id.len, "shot:dev01"));
        CHECK_EQ_I(sh->authority, PPCP_AUTHORITY_DEVICE);
        CHECK(ppcp_cbor_key_is(sh->issued_by.v, sh->issued_by.len, "peer:dev"));
        CHECK(memcmp(&sh->t0, &dev_t0, sizeof(dev_t0)) == 0);   /* byte-identical */
        CHECK_EQ_I(sh->candidate_count, 2);                     /* the host's is attached */
    }

    TEST("CT-I35 / 7.2f — the host issues NO competing Shot, and re-sends the extension");
    {
        size_t  n = 0, off = 0, shots = 0, links = 0;
        uint8_t buf[16384];
        CHECK_EQ_I(ppcp_arbiter_issued_count(arb), 0);   /* it issued nothing of its own */
        CHECK_EQ_I(ppcp_peer_drain(host.p, PPCP_CHANNEL_CONTROL, buf, sizeof(buf), &n),
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
                                &msg) == PPCP_OK) {
                if (msg.type == PPCP_MT_SHOT) {
                    shots++;
                    CHECK(ppcp_cbor_key_is(msg.body.shot.shot.id.v,
                                           msg.body.shot.shot.id.len, "shot:dev01"));
                    CHECK_EQ_I(msg.body.shot.shot.candidate_count, 2);
                }
                if (msg.type == PPCP_MT_SHOT_LINK)
                    links++;
            }
            off += consumed;
        }
        CHECK_EQ_I(shots, 1);
        CHECK_EQ_I(links, 0);
    }

    TEST("CT-I35 / 8.2l — a forced collision LINKS, and withdraws neither");
    {
        /* A second group, this time issued by the host before the device's
         * `shot` for the same Candidate arrives: the two messages crossed. */
        ppcp_candidate c;
        ppcp_shot      crossed;
        ppcp_instant   ct0;
        size_t         issued = 0, n = 0, off = 0, links = 0;
        uint8_t        buf[16384];

        make_cand(&c, "cand:cross", "peer:dev", "src:mic", "tb:dev", "acoustic",
                  t + 5000000000, 0.9);
        CHECK_EQ_I(ppcp_arbiter_observe(arb, &c, NULL), PPCP_OK);
        CHECK_EQ_I(ppcp_arbiter_pump(arb, t + 5000000000 + PPCP_DEFAULT_ISSUE_HOLD_NS,
                                     &issued), PPCP_OK);
        CHECK_EQ_I(issued, 1);
        (void)ppcp_peer_drain(host.p, PPCP_CHANNEL_CONTROL, buf, sizeof(buf), &n);
        drop_events(host.p);

        CHECK_EQ_I(ppcp_instant_make_z(&ct0, "tb:host", t + 5000000000), PPCP_OK);
        CHECK_EQ_I(ppcp_shot_make(&crossed, "shot:dev02", "sess:race", &ct0,
                                  PPCP_AUTHORITY_DEVICE, "peer:dev", "cand:cross"),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_arbiter_observe_shot(arb, &crossed), PPCP_OK);

        n = 0; off = 0;
        CHECK_EQ_I(ppcp_peer_drain(host.p, PPCP_CHANNEL_CONTROL, buf, sizeof(buf), &n),
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
                                &msg) == PPCP_OK && msg.type == PPCP_MT_SHOT_LINK) {
                links++;
                CHECK(ppcp_cbor_key_is(msg.body.shot_link.link.basis.v,
                                       msg.body.shot_link.link.basis.len,
                                       PPCP_LINK_SHARED_CANDIDATE));
                CHECK(msg.body.shot_link.link.confirmed);
                CHECK(msg.body.shot_link.link.has_confirmed_by);
                CHECK_EQ_I(msg.body.shot_link.link.confirmed_by,
                           PPCP_CONFIRMED_BY_OBSERVER);
                CHECK(ppcp_cbor_key_is(msg.body.shot_link.link.foreign_shot_id.v,
                                       msg.body.shot_link.link.foreign_shot_id.len,
                                       "shot:dev02"));
            }
            off += consumed;
        }
        CHECK_EQ_I(links, 1);
        /* Neither Shot is withdrawn: the host's is still there, unchanged. */
        CHECK(ppcp_arbiter_shot_at(arb, 1) != NULL);
        CHECK_EQ_I(ppcp_arbiter_shot_at(arb, 1)->authority, PPCP_AUTHORITY_HOST);
    }

    free(am);
    rig_free(&host);
}

/* ========================================= CT-I35 (convergence) and CT-I9 */

static void test_extension_and_no_merge(void)
{
    ppcp_shot    a, b, c;
    ppcp_instant t0, other;

    CHECK_EQ_I(ppcp_instant_make_z(&t0, "tb:host", 1000000000), PPCP_OK);

    TEST("5.13e / CT-I35 — two extensions in EITHER order converge on the same list");
    CHECK_EQ_I(ppcp_shot_make(&a, "shot:1", "sess:x", &t0, PPCP_AUTHORITY_HOST,
                              "peer:host", "cand:m"), PPCP_OK);
    b = a;
    {
        ppcp_id x, y;
        CHECK_EQ_I(ppcp_id_set_z(&x, "cand:a"), PPCP_OK);
        CHECK_EQ_I(ppcp_id_set_z(&y, "cand:z"), PPCP_OK);
        CHECK_EQ_I(ppcp_shot_attach_candidate_id(&a, &x), PPCP_OK);
        CHECK_EQ_I(ppcp_shot_attach_candidate_id(&a, &y), PPCP_OK);
        CHECK_EQ_I(ppcp_shot_attach_candidate_id(&b, &y), PPCP_OK);
        CHECK_EQ_I(ppcp_shot_attach_candidate_id(&b, &x), PPCP_OK);
    }
    /* Not merely the same SET: the same bytes, which is what makes 5.13e
     * checkable rather than argued. */
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    CHECK_EQ_I(a.candidate_count, 3);

    TEST("5.13e — attaching the same Candidate twice is a no-op");
    {
        ppcp_id x;
        CHECK_EQ_I(ppcp_id_set_z(&x, "cand:a"), PPCP_OK);
        CHECK_EQ_I(ppcp_shot_attach_candidate_id(&a, &x), PPCP_OK);
        CHECK_EQ_I(a.candidate_count, 3);
    }

    TEST("5.13d — adopting an extension grows `candidates` and touches nothing else");
    CHECK_EQ_I(ppcp_shot_adopt_extension(&b, &a), PPCP_OK);
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);

    TEST("CT-I9 / I7 — adoption REFUSES a different `t0`, `authority` or `issued_by`");
    CHECK_EQ_I(ppcp_instant_make_z(&other, "tb:host", 1000000001), PPCP_OK);
    c = a;
    c.t0 = other;
    CHECK_EQ_I(ppcp_shot_adopt_extension(&b, &c), PPCP_ERR_INVALID);
    c = a;
    c.authority = PPCP_AUTHORITY_DEVICE;
    CHECK_EQ_I(ppcp_shot_adopt_extension(&b, &c), PPCP_ERR_INVALID);
    c = a;
    CHECK_EQ_I(ppcp_id_set_z(&c.issued_by, "peer:other"), PPCP_OK);
    CHECK_EQ_I(ppcp_shot_adopt_extension(&b, &c), PPCP_ERR_INVALID);
    c = a;
    CHECK_EQ_I(ppcp_id_set_z(&c.id, "shot:2"), PPCP_OK);
    CHECK_EQ_I(ppcp_shot_adopt_extension(&b, &c), PPCP_ERR_INVALID);
    /* So there is no way to make one Shot out of two, which is I9. */

    TEST("CT-I9 / 5.16e-f — `confirmed_by` accompanies `confirmed`, and observer is refused"
         " on a retrospective basis");
    {
        ppcp_shot_link l;
        CHECK_EQ_I(ppcp_shot_link_make(&l, "link:1", "shot:1", "shot:2",
                                       PPCP_LINK_SEQUENCE_ALIGNMENT, 0.8), PPCP_OK);
        CHECK(!l.confirmed);
        CHECK(!l.has_confirmed_by);
        CHECK_EQ_I(ppcp_shot_link_confirm(&l, PPCP_CONFIRMED_BY_OBSERVER),
                   PPCP_ERR_INVALID);
        CHECK_EQ_I(ppcp_shot_link_confirm(&l, PPCP_CONFIRMED_BY_USER), PPCP_OK);
        CHECK(l.confirmed && l.has_confirmed_by);
    }
}

/* ============================================================ CORE §8.4 */

static void test_orphan_capture_request(void)
{
    static const char *const dprof[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE,
                                         PPCP_PROFILE_LIVE };
    rig          host, dev;
    ppcp_session s;
    ppcp_instant t0;
    ppcp_event   ev;
    uint64_t     req_id = 0;
    bool         saw_request = false, saw_absent = false, saw_error = false;

    CHECK_EQ_I(ppcp_session_make_hosted(&s, "sess:orphan", "tb:host",
                                        PPCP_DEFAULT_COINCIDENCE_WINDOW_NS,
                                        PPCP_DEFAULT_ISSUE_HOLD_NS), PPCP_OK);
    host_with_session(&host, HPROF, 5, &s);
    rig_new(&dev, PPCP_ROLE_CAPTURE, "peer:dev", "tb:dev", dprof, 3);

    TEST("MSG 7.3 — the host asks an owner for an interval it never nominated");
    /* The rigs' handshake left events queued on both peers; a full queue now
     * stops the feed rather than dropping the oldest (F-L13-1), so this test
     * drains what it does not care about before it asks for what it does. */
    drop_events(host.p);
    drop_events(dev.p);
    CHECK_EQ_I(ppcp_instant_make_z(&t0, "tb:host", 1000000000), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_capture_request(host.p, "shot:1", &t0, NULL, 0,
                                         500000000, 500000000), PPCP_OK);
    /* Pump and drain alternately: the handshake frames still queued ahead of
     * the request fill the four-deep ring, and since F-L13-1 a full ring stops
     * the feed rather than dropping the oldest.  Draining between pumps is the
     * loop peer.h documents, and it is the loop both applications run. */
    do {
    pump(host.p, dev.p, PPCP_CHANNEL_CONTROL);
    while (ppcp_peer_next_event(dev.p, &ev) == PPCP_OK) {
        if (ev.kind == PPCP_EVENT_CAPTURE_REQUEST && ev.msg != NULL) {
            saw_request = true;
            req_id = ev.msg->env.msg_id;
            /* 7.3a — the request carries `t0` in the HOST'S timebase; the
             * capture peer converts it into its own with the declared
             * relations, which is the embedding's job and not this test's. */
            CHECK(ppcp_cbor_key_is(ev.msg->body.capture_request.t0.tb.v,
                                   ev.msg->body.capture_request.t0.tb.len, "tb:host"));
        }
    }
    } while (ppcp_peer_pending(host.p, PPCP_CHANNEL_CONTROL) > 0);
    CHECK(saw_request);

    TEST("CT-I10 / 8.4b — the interval is gone: the answer is a Capture, not an `error`");
    drop_events(host.p);
    CHECK_EQ_I(ppcp_peer_capture_absent(dev.p, "cap:1", "shot:1", "stream:video",
                                        PPCP_ABSENT_OUTSIDE_BUFFER, req_id), PPCP_OK);
    do {
    pump(dev.p, host.p, PPCP_CHANNEL_CONTROL);
    while (ppcp_peer_next_event(host.p, &ev) == PPCP_OK) {
        if (ev.kind == PPCP_EVENT_ERROR)
            saw_error = true;
        if (ev.kind == PPCP_EVENT_CAPTURE && ev.msg != NULL &&
            ev.msg->type == PPCP_MT_CAPTURE_ANNOUNCE) {
            saw_absent = true;
            CHECK_EQ_I(ev.msg->body.capture_announce.capture.completeness, PPCP_ABSENT);
            CHECK(ev.msg->body.capture_announce.capture.has_absent_reason);
            CHECK(ppcp_cbor_key_is(ev.msg->body.capture_announce.capture.absent_reason.v,
                                   ev.msg->body.capture_announce.capture.absent_reason.len,
                                   PPCP_ABSENT_OUTSIDE_BUFFER));
            CHECK_EQ_I(ev.msg->env.reply_to, req_id);
        }
    }
    } while (ppcp_peer_pending(dev.p, PPCP_CHANNEL_CONTROL) > 0);
    CHECK(saw_absent);
    CHECK(!saw_error);
    CHECK(ppcp_peer_get_state(host.p) != PPCP_PEER_CLOSED);

    TEST("C2 — a capture peer cannot ORIGINATE a `capture_request`");
    CHECK_EQ_I(ppcp_peer_capture_request(dev.p, "shot:1", &t0, NULL, 0, 1, 1),
               PPCP_ERR_INVALID);

    rig_free(&host);
    rig_free(&dev);
}

int main(void)
{
    test_arbitration();
    test_device_minted();
    test_extension_and_no_merge();
    test_orphan_capture_request();
    TEST_MAIN_END();
}
