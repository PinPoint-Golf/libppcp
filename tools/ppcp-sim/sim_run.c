/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * sim_run.c — the scenarios of PPCP-CONF §4 and §5, driven over real sockets.
 *
 * THE SPLIT.  The DECLARATION is data (sim_decl.c, the JSON under tools/scenarios); the
 * BEHAVIOUR is a scenario in the table below.  "A host that never answers a
 * Candidate" is `--scenario silent-host`, over any host declaration; "a peer
 * with a foreign convention" is any scenario over `foreign-capture.json`.
 * Neither is a code change, which is what lets PinPointStudio and
 * PinPointCapture drive this from their own suites without patching it.
 *
 * WHAT IT REFUSES TO DO.  It holds no threshold that the library refuses to
 * hold (I14): its promotion policy and its arbitration policy are one line each
 * and they are the SIMULATOR's, not the protocol's.  It never composes a
 * relation, never revises a `t0`, and never originates a message its declared
 * profiles do not confer — the engine would refuse it anyway, and that refusal
 * is one of the things the simulator exists to demonstrate.
 */
#include "sim.h"
#include "sim_platform.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

/* ------------------------------------------------------------- violations */

static char g_violation[512];

void sim_violation(const char *fmt, ...)
{
    va_list ap;
    if (g_violation[0] != '\0')
        return;                       /* the FIRST reason is the useful one */
    va_start(ap, fmt);
    vsnprintf(g_violation, sizeof(g_violation), fmt, ap);
    va_end(ap);
}

bool sim_had_violation(void) { return g_violation[0] != '\0'; }
const char *sim_violation_reason(void) { return g_violation; }

/* -------------------------------------------------------- scenario table */

static const sim_scenario g_scenarios[] = {
    { "reference-host", "host",
      "IOP-1, IOP-3, CT-I7, CT-I8, CT-I12, CT-I20, CT-I21, CT-I34, CT-S5",
      "The well-behaved host: opens the Session, syncs per timebase, arms, "
      "arbitrates, accepts offered Sessions.",
      SIM_F_SESSION_OPEN | SIM_F_SYNC | SIM_F_HEARTBEAT | SIM_F_ARM |
      SIM_F_ARBITRATE | SIM_F_ACCEPT_OFFER,
      0, 0, 0, 0 },

    { "reference-capture", "capture",
      "IOP-1, CT-I18, CT-I21, CT-S5",
      "The well-behaved capture peer: opens a Stream per camera Source, syncs, "
      "nominates, mints what a host never answers, announces and transfers a Capture.",
      SIM_F_STREAMS | SIM_F_SYNC | SIM_F_NOMINATE | SIM_F_MINT | SIM_F_CAPTURES,
      1, 0, 0, 0 },

    { "observer", "observer",
      "IOP-4, CT-S6 (2), CT-I24",
      "A Core + Live observer. Parses everything and originates nothing past "
      "`hello`, `declare` and its heartbeat acks.",
      SIM_F_OBSERVER, 0, 0, 0, 0 },

    { "arbiter-no-detect", "host",
      "CT-S6 (1), CT-I24",
      "Core + Arbitrate + Live + Offline and NOT Detect: parses `candidate` "
      "completely, arbitrates over the result, never originates one.",
      SIM_F_SESSION_OPEN | SIM_F_SYNC | SIM_F_HEARTBEAT | SIM_F_ARM | SIM_F_ARBITRATE,
      0, 0, 0, 0 },

    { "silent-host", "host",
      "IOP-7, CT-S4 (6), CT-I32",
      "A host that receives every Candidate and never issues a Shot, so the "
      "nominating peer's 8.2i deadline is the only thing that fires.",
      SIM_F_SESSION_OPEN | SIM_F_SYNC | SIM_F_HEARTBEAT | SIM_F_ARM |
      SIM_F_ARBITRATE | SIM_F_NEVER_ISSUE,
      0, 0, 0, 0 },

    { "late-host", "host",
      "IOP-8, CT-I35, CT-I7",
      "A host delayed past the mint deadline: it arbitrates, but only after the "
      "device was entitled to mint, so 8.2k's attach-rather-than-issue fires.",
      SIM_F_SESSION_OPEN | SIM_F_SYNC | SIM_F_HEARTBEAT | SIM_F_ARM | SIM_F_ARBITRATE,
      0, 0, 0, 3000 },

    { "acoustic-host", "host",
      "IOP-6, CT-I8",
      "A host owning its own acoustic Source, nominating alongside the device: "
      "two Candidates of the same `basis` from different peers, both retained.",
      SIM_F_SESSION_OPEN | SIM_F_SYNC | SIM_F_HEARTBEAT | SIM_F_ARM |
      SIM_F_ARBITRATE | SIM_F_NOMINATE,
      1, 0, 0, 0 },

    { "nominating-capture", "capture",
      "IOP-2, IOP-5, IOP-7, IOP-8, CT-S3 (2), CT-S7 (4)",
      "A capture peer that nominates and mints, and nothing else. The "
      "declaration file decides what is foreign about it.",
      SIM_F_SYNC | SIM_F_NOMINATE | SIM_F_MINT,
      1, 0, 0, 0 },

    { "requesting-host", "host",
      "CT-I22 (device half), CT-I17, CORE 8.4",
      "A host that arbitrates and then ASKS for the clip: a `capture_request` "
      "per issued Shot, with a window expressed in the host's own convention. "
      "It is what drives 8.4a on the peer under test — converting that window "
      "into its own buffer's timebase — and what makes 8.4b's `absent` / "
      "`outside_buffer` answer observable.",
      SIM_F_SESSION_OPEN | SIM_F_SYNC | SIM_F_HEARTBEAT | SIM_F_ARM |
      SIM_F_ARBITRATE | SIM_F_REQUEST,
      0, 0, 0, 0 },

    { "arbitrate-as-capture", "capture",
      "CT-I20",
      "A capture peer asked to arbitrate. It cannot: I20 gives arbitration to a "
      "peer with `role: host` and to no other, so this scenario exists to be "
      "REFUSED and the tool exits non-zero before a socket is opened.",
      SIM_F_ARBITRATE, 0, 0, 0, 0 },

    { "unrelated-capture", "capture",
      "IOP-5, CORE 8.2i1, CT-I3",
      "A peer whose declaration says its clock is `unrelated` to the host's. It "
      "declares that relation, nominates, and mints NOTHING — there is no "
      "reading of `timebase_ref` and no zero is substituted for one.",
      SIM_F_NOMINATE | SIM_F_MINT,
      1, 0, 0, 0 },

    { "late-candidate-capture", "capture",
      "CT-I7",
      "Two Candidates for one event, the second emitted long after the host "
      "issued: it attaches, and `t0` does not move.",
      SIM_F_SYNC | SIM_F_NOMINATE | SIM_F_LATE_NOMINATE,
      2, 700, 5000000, 0 },

    { "preview-capture", "capture",
      "IOP-9, CT-I36, CT-I36a",
      "A capture peer with a `continuous` metadata Stream and a live-only "
      "`preview` Stream alongside its shot-windowed video.",
      SIM_F_STREAMS | SIM_F_PREVIEW | SIM_F_SYNC | SIM_F_CAPTURES,
      0, 0, 0, 0 },

    { "offer-session", "capture",
      "IOP-3, IOP-10, CT-I12",
      "A device that offers a stored Session and replays its bundle onto the "
      "live link when the host accepts (MSG §9.1, ENC 7a).",
      SIM_F_OFFER, 0, 0, 0, 0 },

    { "offer-session-twice", "capture",
      "CT-I34",
      "The same offer, replayed twice. An importer keyed on `Capture.id` scoped "
      "by session and peer sees every Capture once.",
      SIM_F_OFFER | SIM_F_REPLAY_TWICE, 0, 0, 0, 0 }
};

const sim_scenario *sim_scenario_find(const char *name)
{
    size_t i;
    for (i = 0; i < sizeof(g_scenarios) / sizeof(g_scenarios[0]); i++) {
        if (strcmp(g_scenarios[i].name, name) == 0)
            return &g_scenarios[i];
    }
    return NULL;
}

const sim_scenario *sim_scenario_at(size_t index)
{
    return (index < sim_scenario_count()) ? &g_scenarios[index] : NULL;
}

size_t sim_scenario_count(void)
{
    return sizeof(g_scenarios) / sizeof(g_scenarios[0]);
}

/* ---------------------------------------------------------------- the sim */

#define SIM_MAX_SHOTS 32

typedef struct sim {
    const sim_opts     *o;
    sim_decl           *d;
    const sim_scenario *sc;

    void      *peer_mem;
    ppcp_peer *p;
    void      *mint_mem;
    ppcp_mint *mint;
    void       *arb_mem;
    ppcp_arbiter *arb;

    sim_link    link;
    sim_counter c;

    bool    declared_self;
    bool    counterpart_declared;
    bool    joined;
    /* F-S5-3: the live Session's ref as it was when the Session opened.  A
     * replayed bundle used to rebind it silently. */
    bool    live_ref_seen;
    /* F-S5-2 — Shots this host has already asked for a Capture of, so 8.4a is
     * driven once per Shot rather than on every tick. */
    ppcp_id requested[SIM_MAX_SHOTS];
    size_t  requested_count;
    ppcp_id live_session_id;
    ppcp_id live_timebase_ref;
    bool    script_started;
    int     step;
    int64_t next_step_ns;
    int     nominated;
    int64_t candidate_base_ns;
    bool    candidate_base_set;
    int64_t last_hb_ns;
    int64_t last_publish_ns;
    int64_t start_ns;

    struct {
        ppcp_id id;
        int64_t t0_ns;
    } shots[SIM_MAX_SHOTS];
    size_t shot_count;

    bool   captures_open;
    bool   segments_announced;
    size_t announced_shots;

    ppcp_capture_index cap_index;

    unsigned id_seq;

    /* the offered Session */
    uint8_t    *bundle;
    size_t      bundle_len;
    bool        offer_sent;
    int         replays_done;
    ppcp_digest have[PPCP_MAX_HAVE_DIGESTS];
    size_t      have_count;
    bool        accept_arrived;

    ppcp_id probed[SIM_MAX_TB];
    size_t  probed_count;
} sim;

/* --------------------------------------------------------------- callbacks */

static ppcp_result sim_id_fn(void *ctx, ppcp_id *out)
{
    sim *s = (sim *)ctx;
    char buf[80];
    snprintf(buf, sizeof(buf), "%s/shot/%u", s->d->peer_id, ++s->id_seq);
    return ppcp_id_set_z(out, buf);
}

/* I14 — a policy, not a threshold in the protocol layer.  It is the
 * SIMULATOR's rule and it is one line so that nobody mistakes it for one. */
static bool sim_promote(void *ctx, const ppcp_candidate *c)
{
    (void)ctx;
    return c->confidence >= 0.5;
}

static bool sim_arbitrate(void *ctx, const ppcp_candidate *c,
                          const ppcp_timebase_relation *rel, double sigma_ns)
{
    (void)ctx; (void)c; (void)rel; (void)sigma_ns;
    return true;      /* 8.2d: this simulator excludes nothing on uncertainty */
}

/* MSG 3.4 / I14 — the acceptance decision is the embedding's, and this
 * embedding accepts every declaration.  A simulator that refused would be
 * asserting a threshold it has no business holding. */
static bool sim_ingest(void *ctx, const ppcp_peer_desc *counterpart, ppcp_id *reason)
{
    (void)ctx; (void)counterpart; (void)reason;
    return true;
}

static ppcp_result sim_readiness(void *ctx, ppcp_readiness *out)
{
    (void)ctx;
    return ppcp_readiness_settled(out);
}

static ppcp_result sim_health(void *ctx, ppcp_health *out)
{
    (void)ctx;
    memset(out, 0, sizeof(*out));
    out->thermal            = PPCP_THERMAL_NOMINAL;
    out->storage_free_bytes = 32ull * 1024ull * 1024ull * 1024ull;
    out->has_battery_pct    = true;
    out->battery_pct        = 90;
    return PPCP_OK;
}

/* ------------------------------------------------------------------ helpers */

static int64_t now_in(sim *s, const char *tb)
{
    int64_t ns = 0;
    if (sim_clock_now(&s->d->clock, tb, &ns) != PPCP_OK)
        return 0;
    return ns;
}

static bool has_profile(const sim_decl *d, const char *name)
{
    size_t i;
    for (i = 0; i < d->profile_count; i++) {
        if (strcmp(d->profile_text[i], name) == 0)
            return true;
    }
    return false;
}

/* A reading of `Session.timebase_ref`, which is the frame the mint deadline and
 * every `t0` live in (5.13c, 8.2i).  A peer whose own clock has no relation to
 * it has no reading, and that is 8.2i1: it mints nothing rather than assuming
 * a zero offset. */
static bool now_ref(sim *s, int64_t *out)
{
    const ppcp_id *ref = ppcp_peer_timebase_ref(s->p);
    ppcp_instant   here, there;
    char           refbuf[PPCP_ID_MAX + 1];

    if (ref == NULL) {
        *out = now_in(s, s->d->timebase_ref);
        return true;
    }
    memcpy(refbuf, ref->v, (size_t)ref->len);
    refbuf[ref->len] = '\0';
    if (sim_clock_now(&s->d->clock, refbuf, out) == PPCP_OK)
        return true;                        /* it is one of our own clocks */
    if (ppcp_instant_make_z(&here, s->d->sync_tb, now_in(s, s->d->sync_tb)) != PPCP_OK)
        return false;
    if (ppcp_relations_convert(ppcp_peer_relations(s->p), &here, ref, &there) != PPCP_OK)
        return false;
    *out = there.ns;
    return true;
}

/* The Source this scenario nominates from: an acoustic one where the
 * declaration has it, the first Source otherwise. */
static const ppcp_source *nominating_source(sim *s, size_t *out_index)
{
    size_t i;
    for (i = 0; i < s->d->src_count; i++) {
        if (ppcp_cbor_key_is(s->d->src[i].kind.v, s->d->src[i].kind.len, "microphone")) {
            *out_index = i;
            return &s->d->src[i];
        }
    }
    if (s->d->src_count == 0)
        return NULL;
    *out_index = 0;
    return &s->d->src[0];
}

/* ----------------------------------------------------------------- the wire */

static bool flush_tx(sim *s, uint8_t ch)
{
    sim_chan *c = &s->link.ch[ch];

    if (!c->open)
        return true;
    for (;;) {
        const uint8_t *bytes = NULL;
        size_t         len = 0;
        ssize_t        wrote;

        if (ppcp_peer_drain_peek(s->p, ch, &bytes, &len) != PPCP_OK || len == 0)
            return true;
        wrote = send(c->fd, bytes, len, 0);
        if (wrote < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return true;
            sim_violation("write on channel %u failed: %s", (unsigned)ch, strerror(errno));
            return false;
        }
        if (wrote == 0)
            return true;
        sim_log_frames(NULL, "TX", ch, bytes, (size_t)wrote);
        s->c.frames_tx++;
        if (ppcp_peer_drain_commit(s->p, ch, (size_t)wrote) != PPCP_OK) {
            sim_violation("the engine refused a commit of %lld bytes", (long long)wrote);
            return false;
        }
    }
}

static bool sim_already_requested(const sim *s, const ppcp_id *id)
{
    size_t i;
    for (i = 0; i < s->requested_count; i++)
        if (ppcp_id_equal(&s->requested[i], id))
            return true;
    return false;
}

static void sim_note_requested(sim *s, const ppcp_id *id)
{
    if (s->requested_count < SIM_MAX_SHOTS)
        s->requested[s->requested_count++] = *id;
}

static void drain_events(sim *s);

/* ⚠ ONE FRAME PER FEED — ONCE A WORKAROUND, NOW A CHOICE.
 *
 * F-L13-1: ppcp_peer_feed() used to consume as many whole frames as the
 * caller's buffer held while the engine's four-deep event ring dropped the
 * OLDEST event with nothing the embedding could read to find out.  A single
 * socket read carrying a replayed bundle — session_open, declare, stream_open,
 * capture_announce, session_manifest, three payload frames — silently lost the
 * `capture_announce` here, which is how the defect was found.
 *
 * It is fixed (L15, S4): the feed now stops before a frame whose events would
 * not fit, reports what it consumed, and says so through
 * ppcp_peer_feed_stalled().  This loop keeps feeding one frame at a time
 * anyway, because that is what makes `sim_log_frames` able to print one line
 * per frame, and because a tool whose job is to observe the wire should meet
 * the wire a frame at a time.  It drains after each, so the stall never fires.
 */
static bool pump_rx(sim *s, uint8_t ch)
{
    sim_chan   *c = &s->link.ch[ch];
    ssize_t     got;
    size_t      consumed = 0;
    ppcp_result rc;

    if (!c->open)
        return true;
    got = recv(c->fd, c->rx + c->rx_len, SIM_RX_CAP - c->rx_len, MSG_DONTWAIT);
    if (got == 0) {
        c->open = false;
    } else if (got < 0) {
        /* ECONNRESET is the OTHER peer exiting at its own `--run-ms`, which is
         * the same deadline this one has: whichever process reaches it first
         * resets the other's socket and the loser reported a protocol
         * violation.  It made CT-I18 and CT-S5 fail roughly one run in three
         * under load and never when run alone, which is the shape of a harness
         * race rather than of a defect — so a reset is an orderly end of run,
         * and the channel closes.  A reset BEFORE the run has done its work is
         * still caught, by the --expect counters that then come up short. */
        if (errno == ECONNRESET || errno == EPIPE) {
            c->open = false;
        } else if (!(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            sim_violation("read on channel %u failed: %s", (unsigned)ch, strerror(errno));
            return false;
        }
    } else {
        c->rx_len += (size_t)got;
    }
    /* Bytes the LISTENER already holds — everything that arrived in the same
     * read as the `link_bind` — are fed here too, so a `hello` riding behind
     * its binding frame is not left waiting for a byte that never comes. */
    while (c->rx_len >= PPCP_FRAME_HEADER_BYTES) {
        ppcp_frame_header hdr;
        size_t            need;

        if (ppcp_frame_header_parse(c->rx, &hdr) != PPCP_OK) {
            sim_violation("an unreadable frame header arrived on channel %u (ENC §3)",
                          (unsigned)ch);
            return false;
        }
        need = PPCP_FRAME_HEADER_BYTES + (size_t)hdr.payload_len;
        if (need > c->rx_len)
            break;                       /* the tail is the caller's to keep */

        consumed = 0;
        rc = ppcp_peer_feed(s->p, ch, c->rx, need, &consumed);
        if (consumed > 0) {
            sim_log_frames(NULL, "RX", ch, c->rx, consumed);
            s->c.frames_rx++;
            memmove(c->rx, c->rx + consumed, c->rx_len - consumed);
            c->rx_len -= consumed;
        }
        if (rc == PPCP_ERR_MALFORMED) {
            sim_violation("a malformed frame arrived on channel %u (ENC §4)", (unsigned)ch);
            return false;
        }
        if (rc == PPCP_ERR_FATAL_LIMIT) {
            sim_violation("a frame past channel %u's ENC §8 limit arrived; the stream "
                          "cannot be resynchronised", (unsigned)ch);
            return false;
        }
        if (consumed == 0)
            break;
        drain_events(s);
    }
    if (c->rx_len == SIM_RX_CAP) {
        sim_violation("channel %u sent a frame larger than this simulator's buffer",
                      (unsigned)ch);
        return false;
    }
    return true;
}

/* -------------------------------------------------------------- the events */

static void note_shot(sim *s, const ppcp_shot *sh)
{
    const ppcp_peer_desc *cp = ppcp_peer_counterpart(s->p);
    size_t i;

    /* I20 / 8.3d — `authority: host` says a host arbitrated, and only a peer
     * with `role: host` may.  A Shot arriving from a counterpart that declared
     * itself `capture` or `observer` and claiming host authority is the
     * violation, and it is only checkable from the other end. */
    if (cp != NULL && cp->role != PPCP_ROLE_HOST && sh->authority == PPCP_AUTHORITY_HOST) {
        sim_violation("I20 violated: Shot `%s` carries `authority: host` from a peer "
                      "declaring role %s", sh->id.v, ppcp_role_str(cp->role));
    }

    /* 8.3d / CT-I6: minting is the Mint profile's, and the observable act is a
     * Shot the SENDER issued on its own authority.  A host attaching under 8.2k
     * re-sends the device's Shot with `issued_by` unchanged, so it is not one. */
    if (cp != NULL && sh->authority == PPCP_AUTHORITY_DEVICE &&
        ppcp_id_equal(&sh->issued_by, &cp->id))
        s->c.minted_shots_rx++;

    for (i = 0; i < s->shot_count; i++) {
        if (ppcp_id_equal(&s->shots[i].id, &sh->id)) {
            /* I7 — `t0` is never revised, not by the issuer and not by a peer
             * extending the candidate list.  A second `shot` naming the same
             * Shot with a different `t0` is the violation this simulator was
             * built to catch, and it catches it on the wire. */
            if (s->shots[i].t0_ns != sh->t0.ns) {
                s->c.t0_revisions++;
                sim_violation("I7 violated: Shot `%s` arrived with t0 %lld after "
                              "%lld — `t0` is fixed once issued",
                              sh->id.v, (long long)sh->t0.ns,
                              (long long)s->shots[i].t0_ns);
            }
            if ((int64_t)sh->candidate_count > s->c.shot_candidates_max)
                s->c.shot_candidates_max = (int64_t)sh->candidate_count;
            return;
        }
    }
    if (s->shot_count < SIM_MAX_SHOTS) {
        s->shots[s->shot_count].id    = sh->id;
        s->shots[s->shot_count].t0_ns = sh->t0.ns;
        s->shot_count++;
    }
    if ((int64_t)sh->candidate_count > s->c.shot_candidates_max)
        s->c.shot_candidates_max = (int64_t)sh->candidate_count;
}

/* CONF §1d / I24 from the OTHER side: a peer must not ORIGINATE a message no
 * declared profile confers.  The counterpart told us its profiles in `declare`,
 * so every frame after that is checkable — and this is the check a single
 * implementation talking to itself never makes. */
static void check_origination(sim *s, const ppcp_msg *m)
{
    const ppcp_peer_desc *cp = ppcp_peer_counterpart(s->p);

    if (cp == NULL || m == NULL || m->type == PPCP_MT_UNKNOWN)
        return;
    if (m->type == PPCP_MT_ERROR || m->type == PPCP_MT_LINK_BIND ||
        m->type == PPCP_MT_HELLO || m->type == PPCP_MT_HELLO_ACCEPT ||
        m->type == PPCP_MT_DECLARE)
        return;
    if (!ppcp_msg_profiles_confer(m->type, cp->profiles, cp->profile_count)) {
        sim_violation("I24 violated: `%s` originated by a peer declaring no profile "
                      "that confers it", ppcp_id_is_set(&m->type_name) ? m->type_name.v
                                                                       : "?");
    }
}

static void handle_event(sim *s, const ppcp_event *e)
{
    check_origination(s, e->msg);

    switch (e->kind) {
    case PPCP_EVENT_DECLARE:
        s->c.declares_rx++;
        s->counterpart_declared = true;
        break;

    case PPCP_EVENT_SESSION_OPEN:
    case PPCP_EVENT_SESSION_JOINED:
        s->joined = true;
        s->c.sessions_joined++;
        break;

    case PPCP_EVENT_STREAM_OPEN:
        s->c.streams_rx++;
        break;

    case PPCP_EVENT_ARM:
        s->c.arms_rx++;
        /* 5.2a / 7.3c — `arm` is answered with a measurement. */
        (void)ppcp_peer_readiness(s->p, NULL, NULL, 0);
        break;

    case PPCP_EVENT_RELATION_UPDATE:
        s->c.relations_rx++;
        /* 8.2d1 / erratum E29 (F-S5-6) — the relation set just changed, so the
         * Candidates excluded for want of one are reconsidered.  The library
         * owns no event loop and cannot call itself; THIS is the call site
         * every embedding is expected to have, and until F-S5-6 the simulator
         * did not have it either — which left a host that never reconsidered
         * indistinguishable, through `ppcp-conform`, from one that did. */
        if (s->arb != NULL)
            s->c.reconsidered += (int64_t)ppcp_arbiter_reconsider(s->arb);
        break;

    case PPCP_EVENT_HEARTBEAT:
        s->c.heartbeats_rx++;
        break;

    case PPCP_EVENT_SYNC:
        if (e->msg != NULL && e->msg->type == PPCP_MT_SYNC_REPLY)
            s->c.replies_rx++;
        break;

    case PPCP_EVENT_CANDIDATE:
        s->c.candidates_rx++;
        if (s->arb != NULL && e->msg != NULL) {
            bool excluded = false;
            /* CT-S6 assertion 1: a peer with Arbitrate and NOT Detect parses
             * `candidate` completely AND ARBITRATES OVER THE RESULT. */
            if (ppcp_arbiter_observe(s->arb, &e->msg->body.candidate.candidate,
                                     &excluded) == PPCP_OK)
                s->c.arbiter_observed++;
        }
        if (s->mint != NULL && e->msg != NULL) {
            /* Another peer's Candidate is not this peer's to mint; it is
             * recorded only so the counters say it arrived. */
        }
        break;

    case PPCP_EVENT_SHOT:
        s->c.shots_rx++;
        if (e->msg != NULL) {
            note_shot(s, &e->msg->body.shot.shot);
            if (s->mint != NULL)
                (void)ppcp_mint_observe_shot(s->mint, &e->msg->body.shot.shot);
            if (s->arb != NULL)
                (void)ppcp_arbiter_observe_shot(s->arb, &e->msg->body.shot.shot);
        }
        break;

    case PPCP_EVENT_CAPTURE:
        if (e->msg != NULL && e->msg->type == PPCP_MT_CAPTURE_ANNOUNCE) {
            ppcp_capture_key key;
            bool             is_new = false;
            const ppcp_peer_desc *cp = ppcp_peer_counterpart(s->p);

            s->c.captures_rx++;
            memset(&key, 0, sizeof(key));
            key.session_id = e->msg->env.session_id;
            if (cp != NULL)
                key.peer_id = cp->id;
            key.capture_id = e->msg->body.capture_announce.capture.id;
            if (ppcp_capture_index_observe(&s->cap_index, &key, &is_new) == PPCP_OK) {
                if (is_new)
                    s->c.captures_unique++;
                else
                    s->c.captures_duplicate++;
            }
        }
        break;

    case PPCP_EVENT_PAYLOAD:
        s->c.payload_frames_rx++;
        break;

    case PPCP_EVENT_SESSION_OFFER:
        s->c.offers_rx++;
        if ((s->sc->flags & SIM_F_ACCEPT_OFFER) != 0 && e->msg != NULL) {
            ppcp_body_session_accept acc;
            memset(&acc, 0, sizeof(acc));
            acc.session_id = e->msg->body.session_offer.session_id;
            acc.verdict    = PPCP_OFFER_ACCEPT;
            if (ppcp_peer_session_accept(s->p, &acc, e->msg->env.msg_id) != PPCP_OK)
                sim_violation("the engine refused to accept an offered Session");
        }
        break;

    case PPCP_EVENT_SESSION_ACCEPT:
        s->c.accepts_rx++;
        if (e->msg != NULL &&
            e->msg->body.session_accept.verdict == PPCP_OFFER_ACCEPT) {
            size_t i;
            s->have_count = e->msg->body.session_accept.have_digest_count;
            for (i = 0; i < s->have_count && i < PPCP_MAX_HAVE_DIGESTS; i++)
                s->have[i] = e->msg->body.session_accept.have_digests[i];
            s->accept_arrived = true;
        }
        break;

    case PPCP_EVENT_CAPTURE_REQUEST:
        /* 8.4b — an orphan request is answered with a result, never an error. */
        s->c.capture_requests_rx++;
        if (e->msg != NULL) {
            char id[80];
            snprintf(id, sizeof(id), "%s/cap/req%u", s->d->peer_id, ++s->id_seq);
            (void)ppcp_peer_capture_absent(s->p, id,
                                           e->msg->body.capture_request.shot_id.v,
                                           e->msg->body.capture_request.stream_id_count > 0
                                               ? e->msg->body.capture_request.stream_ids[0].v
                                               : "",
                                           PPCP_ABSENT_OUTSIDE_BUFFER,
                                           e->msg->env.msg_id);
        }
        break;

    case PPCP_EVENT_ERROR:
        s->c.errors_rx++;
        if (e->msg != NULL && e->status == PPCP_ERR_FATAL_LIMIT)
            sim_violation("a fatal `error` arrived: %s", e->msg->body.error.code.v);
        break;

    default:
        break;
    }
}

static void drain_events(sim *s)
{
    ppcp_event e;
    while (ppcp_peer_next_event(s->p, &e) == PPCP_OK) {
        if (e.imported)
            s->c.imported_frames_rx++;
        handle_event(s, &e);
    }
    /* F-S5-3 / CORE 4.1a / I16 — `timebase_ref` is immutable for the life of a
     * Session, and a Session offered over the live link (MSG §9.1) is a
     * DIFFERENT Session.  Checked after every drain, from outside the engine,
     * because the failure it catches is silent: the host went on issuing Shots
     * with `t0` in the exporting device's clock and nothing on the wire said
     * the reference had moved. */
    {
        const ppcp_id *sid = ppcp_peer_session_id(s->p);
        const ppcp_id *ref = ppcp_peer_timebase_ref(s->p);
        if (sid != NULL && ref != NULL) {
            if (!s->live_ref_seen) {
                s->live_ref_seen     = true;
                s->live_session_id   = *sid;
                s->live_timebase_ref = *ref;
            } else if (!ppcp_id_equal(&s->live_session_id, sid) ||
                       !ppcp_id_equal(&s->live_timebase_ref, ref)) {
                s->c.live_ref_rebound++;
                sim_violation("I16 violated: the live Session was rebound from "
                              "`%s`/`%s` to `%s`/`%s` — an offered Session is a "
                              "different Session (4.1a, F-S5-3)",
                              s->live_session_id.v, s->live_timebase_ref.v,
                              sid->v, ref->v);
                s->live_session_id   = *sid;
                s->live_timebase_ref = *ref;
            }
        }
    }
}

#include "sim_run_steps.inc"

/* ------------------------------------------------------------------- entry */

static int64_t counter_value(const sim_counter *c, const char *name)
{
#define ROW(n) if (strcmp(name, #n) == 0) return c->n
    ROW(frames_rx); ROW(frames_tx); ROW(declares_rx);
    ROW(candidates_rx); ROW(candidates_tx);
    ROW(shots_rx); ROW(shots_tx); ROW(minted_shots_rx);
    ROW(capture_requests_tx); ROW(capture_requests_rx); ROW(reconsidered);
    ROW(imported_frames_rx); ROW(live_ref_rebound);
    ROW(shot_candidates_max); ROW(t0_revisions);
    ROW(captures_rx); ROW(captures_unique); ROW(captures_duplicate);
    ROW(payload_frames_rx);
    ROW(relations_rx); ROW(relations_tx);
    ROW(probe_timebases); ROW(probes_tx); ROW(replies_rx);
    ROW(heartbeats_rx); ROW(errors_rx);
    ROW(minted); ROW(retained); ROW(issued); ROW(late_issues); ROW(arbiter_observed);
    ROW(offers_rx); ROW(offers_tx); ROW(accepts_rx); ROW(replays);
    ROW(sessions_joined); ROW(streams_rx); ROW(arms_rx); ROW(violations);
    ROW(relations_held); ROW(relations_composed);
#undef ROW
    return INT64_MIN;
}

static void report(const sim *s)
{
    fprintf(stderr,
            "%s report: frames rx/tx %lld/%lld  declares %lld  candidates rx/tx %lld/%lld  "
            "shots rx/tx %lld/%lld  max shot candidates %lld  minted %lld  retained %lld  "
            "issued %lld  late %lld  arbiter observed %lld  relations rx/tx %lld/%lld  "
            "probe timebases %lld  replies %lld  captures rx/unique/dup %lld/%lld/%lld  "
            "payload frames %lld  offers rx/tx %lld/%lld  replays %lld  errors %lld\n",
            s->o->log_prefix,
            (long long)s->c.frames_rx, (long long)s->c.frames_tx,
            (long long)s->c.declares_rx,
            (long long)s->c.candidates_rx, (long long)s->c.candidates_tx,
            (long long)s->c.shots_rx, (long long)s->c.shots_tx,
            (long long)s->c.shot_candidates_max,
            (long long)s->c.minted, (long long)s->c.retained,
            (long long)s->c.issued, (long long)s->c.late_issues,
            (long long)s->c.arbiter_observed,
            (long long)s->c.relations_rx, (long long)s->c.relations_tx,
            (long long)s->c.probe_timebases, (long long)s->c.replies_rx,
            (long long)s->c.captures_rx, (long long)s->c.captures_unique,
            (long long)s->c.captures_duplicate,
            (long long)s->c.payload_frames_rx,
            (long long)s->c.offers_rx, (long long)s->c.offers_tx,
            (long long)s->c.replays, (long long)s->c.errors_rx);
}

int sim_run(const sim_opts *o, sim_decl *d, const sim_scenario *sc)
{
    static sim      s;
    ppcp_peer_config cfg;
    char             err[SIM_ERR_LEN];
    int64_t          deadline;
    int              rc = 0;
    size_t           i;

    memset(&s, 0, sizeof(s));
    s.o  = o;
    s.d  = d;
    s.sc = sc;
    ppcp_capture_index_init(&s.cap_index);

    memset(&cfg, 0, sizeof(cfg));
    cfg.role          = d->role;
    cfg.peer_id       = d->peer_id;
    cfg.profiles      = d->profile_ptr;
    cfg.profile_count = d->profile_count;
    cfg.listener      = (o->listen_port >= 0);
    cfg.ingest_policy = sim_ingest;
    cfg.ctx           = &s;
    cfg.clock.now     = sim_clock_now;
    cfg.clock.ctx     = &d->clock;
    cfg.health        = sim_readiness;
    cfg.health_report = sim_health;
    cfg.sync_timebase = d->sync_tb;

    sim_log_configure(o->log_prefix, o->quiet);

    s.peer_mem = malloc(ppcp_peer_sizeof());
    if (s.peer_mem == NULL) {
        fprintf(stderr, "ppcp-sim: out of memory for the engine\n");
        return 1;
    }
    if (ppcp_peer_new(s.peer_mem, ppcp_peer_sizeof(), &cfg, &s.p) != PPCP_OK) {
        fprintf(stderr, "ppcp-sim: the engine refused this configuration "
                        "(does the profile list include core?)\n");
        free(s.peer_mem);
        return 1;
    }

    /* Relations the declaration states are held from the start: the `unrelated`
     * pairing of CONF §5 depends on the peer actually holding one. */
    for (i = 0; i < d->rel_count; i++)
        (void)ppcp_relations_put(ppcp_peer_relations(s.p), &d->rel[i]);

    if ((sc->flags & SIM_F_MINT) != 0 && has_profile(d, PPCP_PROFILE_MINT)) {
        s.mint_mem = malloc(ppcp_mint_sizeof());
        if (s.mint_mem == NULL ||
            ppcp_mint_new(s.mint_mem, ppcp_mint_sizeof(), s.p, sim_id_fn, &s, &s.mint)
                != PPCP_OK) {
            fprintf(stderr, "ppcp-sim: this peer cannot mint (8.3d: Mint confers it)\n");
            free(s.peer_mem);
            free(s.mint_mem);
            return 1;
        }
        (void)ppcp_mint_set_promotion_policy(s.mint, sim_promote, &s);
    }
    if ((sc->flags & SIM_F_ARBITRATE) != 0) {
        s.arb_mem = malloc(ppcp_arbiter_sizeof());
        if (s.arb_mem == NULL ||
            ppcp_arbiter_new(s.arb_mem, ppcp_arbiter_sizeof(), s.p, sim_id_fn, &s, &s.arb)
                != PPCP_OK) {
            /* I20 — arbitration is available to a host declaring Arbitrate and
             * to nobody else, and the engine is what says so. */
            fprintf(stderr, "ppcp-sim: this peer may not arbitrate (I20: role host "
                            "and the Arbitrate profile)\n");
            free(s.peer_mem);
            free(s.mint_mem);
            free(s.arb_mem);
            return 1;
        }
        (void)ppcp_arbiter_set_policy(s.arb, sim_arbitrate, &s);
    }
    if ((sc->flags & SIM_F_OFFER) != 0 && !sim_build_bundle(&s)) {
        fprintf(stderr, "ppcp-sim: could not build the stored Session to offer\n");
        rc = 1;
        goto out;
    }

    err[0] = '\0';
    if (o->listen_port >= 0) {
        if (!sim_listen(&s.link, o->listen_port, o->port_file, o->run_ms, s.p,
                        err, sizeof(err))) {
            fprintf(stderr, "ppcp-sim: %s\n", err);
            rc = 1;
            goto out;
        }
    } else {
        if (!sim_connect(&s.link, o->connect_host, o->connect_port, o->run_ms, s.p,
                         err, sizeof(err))) {
            fprintf(stderr, "ppcp-sim: %s\n", err);
            rc = 1;
            goto out;
        }
    }

    s.start_ns = sim_now_ns();
    deadline   = s.start_ns + o->run_ms * 1000000;

    while (sim_now_ns() < deadline && !sim_had_violation()) {
        struct pollfd pfd[SIM_CH_COUNT];
        nfds_t        n = 0;
        size_t        k;

        for (k = 0; k < SIM_CH_COUNT; k++) {
            if (s.link.ch[k].open) {
                pfd[n].fd     = s.link.ch[k].fd;
                pfd[n].events = POLLIN;
                n++;
            }
        }
        if (n == 0)
            break;
        (void)poll(pfd, n, 10);

        for (k = 0; k < SIM_CH_COUNT; k++) {
            if (!pump_rx(&s, (uint8_t)k))
                break;
        }
        drain_events(&s);
        sim_tick(&s);
        for (k = 0; k < SIM_CH_COUNT; k++) {
            if (!flush_tx(&s, (uint8_t)k))
                break;
        }
    }

    /* One last drain, so a frame that arrived in the final millisecond is
     * counted rather than lost to the clock. */
    for (i = 0; i < SIM_CH_COUNT; i++)
        (void)pump_rx(&s, (uint8_t)i);
    drain_events(&s);

    if (s.mint != NULL) {
        s.c.minted   = (int64_t)ppcp_mint_minted_count(s.mint);
        s.c.retained = (int64_t)ppcp_mint_retained_count(s.mint);
    }
    if (s.arb != NULL) {
        s.c.issued      = (int64_t)ppcp_arbiter_issued_count(s.arb);
        s.c.late_issues = (int64_t)ppcp_arbiter_late_count(s.arb);
        s.c.retained    = (int64_t)ppcp_arbiter_retained_count(s.arb);
    }
    s.c.relations_held = (int64_t)ppcp_relations_count(ppcp_peer_relations(s.p));
    /* I18 / 5.4c — nothing composes.  A relation between two clocks belonging
     * to ONE peer could only have come from composing two measured ones,
     * because no peer probes itself; a directly measured relation always spans
     * the two ends of a link.  So this counter is zero in a conformant run and
     * a composition would make it non-zero. */
    {
        const ppcp_relation_set *rs = ppcp_peer_relations(s.p);
        const ppcp_peer_desc    *cp = ppcp_peer_counterpart(s.p);
        size_t                   k;
        for (k = 0; k < ppcp_relations_count(rs); k++) {
            const ppcp_timebase_relation *r = &rs->r[k];
            size_t j;
            bool from_ours = false, to_ours = false, from_theirs = false, to_theirs = false;
            for (j = 0; j < d->tb_count; j++) {
                if (ppcp_id_equal(&r->from, &d->tb[j].id)) from_ours = true;
                if (ppcp_id_equal(&r->to,   &d->tb[j].id)) to_ours   = true;
            }
            if (cp != NULL) {
                for (j = 0; j < cp->timebase_count; j++) {
                    if (ppcp_id_equal(&r->from, &cp->timebases[j].id)) from_theirs = true;
                    if (ppcp_id_equal(&r->to,   &cp->timebases[j].id)) to_theirs   = true;
                }
            }
            if ((from_ours && to_ours) || (from_theirs && to_theirs))
                s.c.relations_composed++;
        }
    }
    s.c.violations     = sim_had_violation() ? 1 : 0;

    report(&s);

    for (i = 0; i < o->expect_count; i++) {
        int64_t got = counter_value(&s.c, o->expect[i].name);
        if (got == INT64_MIN) {
            fprintf(stderr, "ppcp-sim: --expect names no counter: %s\n", o->expect[i].name);
            rc = 1;
            continue;
        }
        {
            bool ok;
            const char *op;
            switch (o->expect[i].cmp) {
            case SIM_CMP_GE: ok = (got >= o->expect[i].value); op = ">="; break;
            case SIM_CMP_LE: ok = (got <= o->expect[i].value); op = "<="; break;
            default:         ok = (got == o->expect[i].value); op = "==";  break;
            }
            if (!ok) {
                fprintf(stderr, "ppcp-sim: expectation failed: %s is %lld, expected %s %lld\n",
                        o->expect[i].name, (long long)got, op,
                        (long long)o->expect[i].value);
                rc = 1;
            }
        }
    }

out:
    if (sim_had_violation()) {
        fprintf(stderr, "ppcp-sim: protocol violation: %s\n", sim_violation_reason());
        rc = 1;
    }
    sim_link_close(&s.link);
    if (s.p != NULL)
        ppcp_peer_free(s.p);
    free(s.peer_mem);
    free(s.mint_mem);
    free(s.arb_mem);
    free(s.bundle);
    return rc;
}
