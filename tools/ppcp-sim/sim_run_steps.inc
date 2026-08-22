/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * sim_run_steps.inc — the script a scenario runs, and the stored Session it
 * offers.  Included by sim_run.c rather than compiled separately because it is
 * one state machine over one `sim` and splitting it would mean exporting the
 * whole of that struct for no gain.
 *
 * The script starts when this peer has what it needs to act — a counterpart's
 * declaration for a host, a Session for a capture peer that opens Streams — and
 * every step carries its own delay, so "a host delayed past the mint deadline"
 * is a number in the scenario table rather than a branch in here.
 */

/* --------------------------------------------------- the stored Session
 *
 * ENC 7a: a bundle is the frames the live link would have carried, in the
 * order it would have carried them.  So this is built with the same message
 * constructors the wire uses, and `ppcp_bundle_replay` puts it back on a
 * socket unchanged (MSG §9.1).
 */

#define SIM_BUNDLE_CAP (256u * 1024u)

static ppcp_result bundle_emit(ppcp_bundle_writer *w, sim *s, uint8_t channel,
                               const ppcp_msg *m)
{
    size_t      n  = 0;
    ppcp_result rc = ppcp_bundle_writer_append_msg(w, channel, m,
                                                   s->bundle + s->bundle_len,
                                                   SIM_BUNDLE_CAP - s->bundle_len, &n);
    if (rc == PPCP_OK)
        s->bundle_len += n;
    return rc;
}

static bool sim_build_bundle(sim *s)
{
    void               *wm = NULL;
    ppcp_bundle_writer *w  = NULL;
    ppcp_session        sess;
    ppcp_msg            m;
    ppcp_stream         st;
    ppcp_instant        at;
    size_t              n = 0;
    char                sid[80], stid[80], capid[80];
    static const uint8_t clip[] = { 0x50, 0x50, 0x43, 0x50, 0x01, 0x02 };
    uint8_t             digest_bytes[PPCP_SHA256_BYTES];
    ppcp_digest         digest;

    s->bundle = (uint8_t *)malloc(SIM_BUNDLE_CAP);
    if (s->bundle == NULL)
        return false;
    wm = malloc(ppcp_bundle_writer_sizeof());
    if (wm == NULL || ppcp_bundle_writer_new(wm, ppcp_bundle_writer_sizeof(), &w) != PPCP_OK) {
        free(wm);
        return false;
    }
    if (ppcp_bundle_writer_begin(w, s->bundle, SIM_BUNDLE_CAP, &n) != PPCP_OK) {
        free(wm);
        return false;
    }
    s->bundle_len = n;

    snprintf(sid,   sizeof(sid),   "%s/stored", s->d->peer_id);
    snprintf(stid,  sizeof(stid),  "%s/stored/st0", s->d->peer_id);
    snprintf(capid, sizeof(capid), "%s/stored/cap0", s->d->peer_id);
    ppcp_sha256_hash(clip, sizeof(clip), digest_bytes);
    if (ppcp_digest_set(&digest, digest_bytes) != PPCP_OK)
        goto fail;

    /* CORE 4.1b / 7.3b — a hostless Session records `session_open` without the
     * two arbitration parameters and NO `arm`: nobody was controlling. */
    if (ppcp_session_make_hostless(&sess, sid, s->d->tb[0].id.v) != PPCP_OK)
        goto fail;
    if (ppcp_msg_init(&m, PPCP_MT_SESSION_OPEN, 1) != PPCP_OK)
        goto fail;
    m.body.session_open.session_id   = sess.id;
    m.body.session_open.timebase_ref = sess.timebase_ref;
    if (bundle_emit(w, s, PPCP_CHANNEL_CONTROL, &m) != PPCP_OK)
        goto fail;

    /* CORE 8.5c scopes Capture identity by the minting peer, and a bundle
     * states that nowhere else — so `declare` goes in before any Capture
     * (erratum queued by H in S2, plan §9). */
    if (ppcp_msg_init(&m, PPCP_MT_DECLARE, 2) != PPCP_OK)
        goto fail;
    m.body.declare.generation = 1;
    m.body.declare.peer       = s->d->desc;
    if (bundle_emit(w, s, PPCP_CHANNEL_CONTROL, &m) != PPCP_OK)
        goto fail;

    if (ppcp_instant_make_z(&at, s->d->tb[0].id.v, 1000) != PPCP_OK)
        goto fail;
    if (ppcp_stream_make(&st, stid, sid,
                         s->d->src_count > 0 ? s->d->src[0].id.v : "src:0",
                         PPCP_STREAM_KIND_VIDEO,
                         s->d->src_count > 0 ? s->d->cp[0][0].id.v : "cp:0",
                         s->d->tb[0].id.v, PPCP_SHOT_WINDOWED, &at) != PPCP_OK)
        goto fail;
    if (ppcp_msg_init(&m, PPCP_MT_STREAM_OPEN, 3) != PPCP_OK)
        goto fail;
    m.body.stream_open.stream = st;
    if (bundle_emit(w, s, PPCP_CHANNEL_CONTROL, &m) != PPCP_OK)
        goto fail;

    if (ppcp_msg_init(&m, PPCP_MT_CAPTURE_ANNOUNCE, 4) != PPCP_OK)
        goto fail;
    if (ppcp_msg_set_session_id(&m, sid) != PPCP_OK)
        goto fail;
    if (ppcp_capture_make_shot(&m.body.capture_announce.capture, capid,
                               "shot:stored:1", stid, PPCP_COMPLETE) != PPCP_OK)
        goto fail;
    if (ppcp_capture_set_digest(&m.body.capture_announce.capture, &digest,
                                sizeof(clip)) != PPCP_OK)
        goto fail;
    if (bundle_emit(w, s, PPCP_CHANNEL_CONTROL, &m) != PPCP_OK)
        goto fail;

    /* ENC 7c — the manifest precedes any payload frame. */
    if (ppcp_msg_init(&m, PPCP_MT_SESSION_MANIFEST, 5) != PPCP_OK)
        goto fail;
    if (ppcp_id_set_z(&m.body.session_manifest.session_id, sid) != PPCP_OK)
        goto fail;
    if (ppcp_id_set_z(&m.body.session_manifest.streams[0], stid) != PPCP_OK)
        goto fail;
    m.body.session_manifest.stream_count = 1;
    if (ppcp_id_set_z(&m.body.session_manifest.captures[0].capture_id, capid) != PPCP_OK)
        goto fail;
    m.body.session_manifest.captures[0].digest = digest;
    m.body.session_manifest.captures[0].bytes  = sizeof(clip);
    if (ppcp_id_set_z(&m.body.session_manifest.captures[0].stream_id, stid) != PPCP_OK)
        goto fail;
    m.body.session_manifest.capture_count  = 1;
    m.body.session_manifest.count_captures = 1;
    m.body.session_manifest.completeness   = PPCP_COMPLETE;
    if (bundle_emit(w, s, PPCP_CHANNEL_CONTROL, &m) != PPCP_OK)
        goto fail;

    if (ppcp_msg_init(&m, PPCP_MT_PAYLOAD_BEGIN, 6) != PPCP_OK)
        goto fail;
    if (ppcp_id_set_z(&m.body.payload_begin.capture_id, capid) != PPCP_OK)
        goto fail;
    m.body.payload_begin.bytes       = sizeof(clip);
    m.body.payload_begin.digest      = digest;
    m.body.payload_begin.chunk_bytes = PPCP_DEFAULT_CHUNK_BYTES;
    if (bundle_emit(w, s, PPCP_CHANNEL_BULK, &m) != PPCP_OK)
        goto fail;

    if (ppcp_msg_init(&m, PPCP_MT_PAYLOAD_CHUNK, 7) != PPCP_OK)
        goto fail;
    if (ppcp_id_set_z(&m.body.payload_chunk.capture_id, capid) != PPCP_OK)
        goto fail;
    m.body.payload_chunk.index    = 0;
    m.body.payload_chunk.offset   = 0;
    m.body.payload_chunk.data     = clip;
    m.body.payload_chunk.data_len = sizeof(clip);
    m.body.payload_chunk.digest   = digest;
    if (bundle_emit(w, s, PPCP_CHANNEL_BULK, &m) != PPCP_OK)
        goto fail;

    if (ppcp_msg_init(&m, PPCP_MT_PAYLOAD_END, 8) != PPCP_OK)
        goto fail;
    if (ppcp_id_set_z(&m.body.payload_end.capture_id, capid) != PPCP_OK)
        goto fail;
    m.body.payload_end.digest = digest;
    if (bundle_emit(w, s, PPCP_CHANNEL_BULK, &m) != PPCP_OK)
        goto fail;

    if (ppcp_bundle_writer_finish(w) != PPCP_OK)
        goto fail;
    free(wm);
    return true;

fail:
    free(wm);
    return false;
}

/* ------------------------------------------------------------ the replay */

typedef struct sim_replay_state {
    void               *mem;
    ppcp_bundle_replay *rp;
    size_t              off;
    bool                running;
} sim_replay_state;

static sim_replay_state g_replay;

static void replay_start(sim *s)
{
    if (g_replay.mem == NULL)
        g_replay.mem = malloc(ppcp_bundle_replay_sizeof());
    if (g_replay.mem == NULL)
        return;
    if (ppcp_bundle_replay_new(g_replay.mem, ppcp_bundle_replay_sizeof(), s->p,
                               s->have_count > 0 ? s->have : NULL, s->have_count,
                               &g_replay.rp) != PPCP_OK) {
        sim_violation("the engine refused to replay the accepted Session");
        return;
    }
    g_replay.off     = 0;
    g_replay.running = true;
}

static void replay_pump(sim *s)
{
    size_t consumed = 0;

    if (!g_replay.running)
        return;
    if (ppcp_bundle_replay_feed(g_replay.rp, s->bundle + g_replay.off,
                                s->bundle_len - g_replay.off, &consumed) != PPCP_OK) {
        sim_violation("the stored Session could not be replayed onto the link");
        g_replay.running = false;
        return;
    }
    g_replay.off += consumed;
    if (g_replay.off >= s->bundle_len) {
        g_replay.running = false;
        s->replays_done++;
        s->c.replays++;
        if ((s->sc->flags & SIM_F_REPLAY_TWICE) != 0 && s->replays_done < 2)
            replay_start(s);
    }
}

/* -------------------------------------------------------------- the script */

typedef enum sim_step_id {
    STEP_SESSION_OPEN = 0,
    STEP_SYNC,
    STEP_STREAMS,
    STEP_ARM,
    STEP_OFFER,
    STEP_NOMINATE,
    STEP_CAPTURES,
    STEP_DONE
} sim_step_id;

static void open_streams(sim *s)
{
    size_t i;
    int    n = 0;

    for (i = 0; i < s->d->src_count; i++) {
        ppcp_stream  st;
        ppcp_instant at;
        char         id[80];
        const char  *kind = PPCP_STREAM_KIND_VIDEO;

        if (!ppcp_source_kind_is_camera(&s->d->src[i]))
            continue;
        snprintf(id, sizeof(id), "%s/st/%d", s->d->peer_id, n++);
        if (ppcp_instant_make_z(&at, s->d->src[i].timebase_id.v,
                                now_in(s, s->d->src[i].timebase_id.v)) != PPCP_OK)
            continue;
        if (ppcp_stream_make(&st, id, s->d->session_id, s->d->src[i].id.v, kind,
                             s->d->cp[i][0].id.v, s->d->src[i].timebase_id.v,
                             PPCP_SHOT_WINDOWED, &at) != PPCP_OK)
            continue;
        if (ppcp_peer_stream_open(s->p, &st) != PPCP_OK)
            sim_violation("the engine refused to open Stream `%s`", id);

        if ((s->sc->flags & SIM_F_PREVIEW) != 0) {
            /* 5.11j — a preview Stream is live-only, `continuous`, and its
             * Captures never enter a bundle. */
            snprintf(id, sizeof(id), "%s/st/preview%d", s->d->peer_id, n);
            if (ppcp_stream_make(&st, id, s->d->session_id, s->d->src[i].id.v,
                                 PPCP_STREAM_KIND_PREVIEW, s->d->cp[i][0].id.v,
                                 s->d->src[i].timebase_id.v, PPCP_CONTINUOUS, &at)
                == PPCP_OK)
                (void)ppcp_peer_stream_open(s->p, &st);

            /* 5.11b — a `continuous` Stream carries stream-anchored segments,
             * which is what I36's coverage accounting is stated over. */
            snprintf(id, sizeof(id), "%s/st/meta%d", s->d->peer_id, n);
            if (ppcp_stream_make(&st, id, s->d->session_id, s->d->src[i].id.v,
                                 PPCP_STREAM_KIND_METADATA, s->d->cp[i][0].id.v,
                                 s->d->src[i].timebase_id.v, PPCP_CONTINUOUS, &at)
                == PPCP_OK)
                (void)ppcp_peer_stream_open(s->p, &st);
        }
    }
}

static void emit_candidate(sim *s)
{
    size_t                      index = 0;
    const ppcp_source          *src = nominating_source(s, &index);
    const ppcp_capture_profile *cp;
    ppcp_candidate              c;
    char                        id[96];
    int64_t                     raw;

    if (src == NULL) {
        sim_violation("this declaration owns no Source, so it cannot nominate (I26)");
        return;
    }
    /* 5.12a / I26 — the Candidate's Source is one this peer declared and its
     * instant is in that Source's Timebase.  The engine refuses anything else. */
    if (!s->candidate_base_set) {
        s->candidate_base_ns   = now_in(s, src->timebase_id.v);
        s->candidate_base_set  = true;
    }
    raw = s->candidate_base_ns + (int64_t)s->nominated * s->sc->candidate_step_ns;
    /* 6.1d — a Source with no `format` (a microphone, an IMU) has no exposure
     * and `convention: mid`, so no profile is passed. */
    cp = ppcp_source_kind_is_camera(src) ? &s->d->cp[index][0] : NULL;
    snprintf(id, sizeof(id), "%s/cand/%d", s->d->peer_id, s->nominated);
    if (ppcp_candidate_make_canonical(&c, id, src, cp, "acoustic", raw,
                                      cp != NULL ? 8000000 : 0, 0.9, NULL) != PPCP_OK) {
        sim_violation("could not build a Candidate from this declaration");
        return;
    }
    /* 7.1d — every nomination is emitted, promoted or not. */
    if (ppcp_peer_nominate(s->p, &c) != PPCP_OK) {
        sim_violation("the engine refused to nominate (does this peer declare Detect, "
                      "and is the Source's Timebase declared? I26)");
        return;
    }
    s->c.candidates_tx++;
    if (s->mint != NULL)
        (void)ppcp_mint_observe_own(s->mint, &c);
    if (s->arb != NULL) {
        bool excluded = false;
        (void)ppcp_arbiter_observe(s->arb, &c, &excluded);
    }
    s->nominated++;
}

/* 5.11b and 5.11c3 — the two Captures a Stream that is not shot-windowed
 * produces: a stream-anchored SEGMENT on the `continuous` Stream, which is what
 * I36's coverage accounting is stated over, and the discarded `preview`
 * segment, which 5.11c3 requires a peer to announce as `absent` /
 * `not_retained` and which 5.11j keeps out of any bundle.
 *
 * The preview one is also the shape MSG 8.1i cannot express (plan §9, L, S2):
 * it holds no payload, so it has no transfer state to carry, and the library
 * exempts it rather than making it unannounceable. */
static void announce_segments(sim *s)
{
    size_t k;

    for (k = 0; k < ppcp_peer_stream_count(s->p); k++) {
        const ppcp_stream *st = ppcp_peer_stream_at(s->p, k);
        ppcp_capture       cap;
        ppcp_interval      iv;
        char               id[96];
        int64_t            t;
        bool               preview;

        if (st == NULL || st->continuity != PPCP_CONTINUOUS)
            continue;
        preview = ppcp_stream_is_preview(st);
        t = now_in(s, st->timebase_id.v);
        if (ppcp_interval_make(&iv, st->timebase_id.v, st->timebase_id.len,
                               t - 1000000000, t) != PPCP_OK)
            continue;
        snprintf(id, sizeof(id), "%s/seg/%zu", s->d->peer_id, k);
        if (ppcp_capture_make_segment(&cap, id, st->id.v,
                                      preview ? PPCP_ABSENT : PPCP_COMPLETE, &iv) != PPCP_OK)
            continue;
        if (preview &&
            ppcp_capture_set_absent_reason(&cap, PPCP_ABSENT_NOT_RETAINED) != PPCP_OK)
            continue;
        if (ppcp_peer_capture_announce(s->p, &cap, preview, NULL, NULL, 0) != PPCP_OK)
            sim_violation("the engine refused a %s segment on Stream `%s`",
                          preview ? "preview" : "continuous", st->id.v);
    }
    s->segments_announced = true;
}

static void announce_captures(sim *s)
{
    size_t i;

    if ((s->sc->flags & SIM_F_PREVIEW) != 0 && !s->segments_announced)
        announce_segments(s);

    for (i = s->announced_shots; i < s->shot_count; i++) {
        ppcp_capture    cap;
        char            id[96];
        const ppcp_stream *st = NULL;
        static const uint8_t clip[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        uint8_t         dbytes[PPCP_SHA256_BYTES];
        ppcp_digest     d;
        size_t          k;

        for (k = 0; k < ppcp_peer_stream_count(s->p); k++) {
            const ppcp_stream *cand = ppcp_peer_stream_at(s->p, k);
            if (cand != NULL &&
                ppcp_cbor_key_is(cand->kind.v, cand->kind.len, PPCP_STREAM_KIND_VIDEO)) {
                st = cand;
                break;
            }
        }
        if (st == NULL)
            return;                       /* no Stream yet; try again next tick */
        snprintf(id, sizeof(id), "%s/cap/%zu", s->d->peer_id, i);
        if (ppcp_capture_make_shot(&cap, id, s->shots[i].id.v, st->id.v, PPCP_COMPLETE)
            != PPCP_OK)
            continue;
        ppcp_sha256_hash(clip, sizeof(clip), dbytes);
        if (ppcp_digest_set(&d, dbytes) != PPCP_OK)
            continue;
        (void)ppcp_capture_set_digest(&cap, &d, sizeof(clip));
        if (ppcp_peer_capture_announce(s->p, &cap, false, NULL, NULL, 0) != PPCP_OK)
            continue;
        (void)ppcp_peer_payload_begin(s->p, PPCP_CHANNEL_BULK, id, sizeof(clip), &d,
                                      PPCP_DEFAULT_CHUNK_BYTES, NULL);
        (void)ppcp_peer_payload_chunk(s->p, PPCP_CHANNEL_BULK, id, 0,
                                      PPCP_DEFAULT_CHUNK_BYTES, clip, sizeof(clip));
        (void)ppcp_peer_payload_end(s->p, PPCP_CHANNEL_BULK, id, &d);
        s->announced_shots = i + 1;
    }
}

/* True when this peer has what the scenario needs before it acts. */
static bool script_ready(const sim *s)
{
    if (!s->counterpart_declared)
        return false;
    if (s->d->role == PPCP_ROLE_HOST)
        return true;
    if ((s->sc->flags & (SIM_F_STREAMS | SIM_F_NOMINATE | SIM_F_CAPTURES)) != 0)
        return s->joined;
    return true;
}

static void sim_tick(sim *s)
{
    int64_t now = sim_now_ns();

    /* MSG 3.3a — a complete snapshot, sent as soon as a wire version is agreed.
     * Both ends declare; neither waits for the other, because 3.3 makes the
     * declaration unsolicited and a peer that waited would deadlock against
     * another that waited. */
    if (!s->declared_self && ppcp_peer_get_state(s->p) >= PPCP_PEER_CONNECTED) {
        if (ppcp_peer_declare(s->p, &s->d->desc) != PPCP_OK)
            sim_violation("the engine refused this declaration");
        s->declared_self = true;
    }

    /* ------------------------------------------------------------- the script */
    if (!s->script_started) {
        if (!script_ready(s))
            return;
        s->script_started = true;
        s->next_step_ns   = now;
        s->step           = STEP_SESSION_OPEN;
    }

    while (s->step != STEP_DONE && now >= s->next_step_ns) {
        int64_t delay_ms = 0;

        switch (s->step) {
        case STEP_SESSION_OPEN:
            if ((s->sc->flags & SIM_F_SESSION_OPEN) != 0) {
                ppcp_session sess;
                /* 5.10e made structural: a hosted Session carries both
                 * arbitration parameters and a hostless one carries neither. */
                if (ppcp_session_make_hosted(&sess, s->d->session_id, s->d->timebase_ref,
                                             s->d->coincidence_window_ns,
                                             s->d->issue_hold_ns) != PPCP_OK ||
                    ppcp_session_set_heartbeat_interval(&sess, s->d->heartbeat_ms)
                        != PPCP_OK ||
                    ppcp_peer_session_open(s->p, &sess) != PPCP_OK) {
                    sim_violation("the engine refused to open the Session");
                } else {
                    s->joined = true;
                }
            }
            delay_ms = 30;
            break;

        case STEP_SYNC:
            /* 5.4b — a relation the declaration STATES is declared on the
             * wire, `unrelated` included.  That is the whole of the CONF §5
             * pairing "host <-> peer declaring `unrelated` timebases": the
             * honest degraded peer says so, and is not silently mishandled. */
            if (s->d->rel_count > 0 &&
                ppcp_peer_relation_update(s->p, s->d->rel, s->d->rel_count) == PPCP_OK)
                s->c.relations_tx += (int64_t)s->d->rel_count;
            if ((s->sc->flags & SIM_F_SYNC) != 0) {
                size_t i;
                /* I21 / 6.1d — one probe sequence per DECLARED timebase, and
                 * each relation declared directly.  Never composed (I18). */
                for (i = 0; i < s->d->tb_count; i++) {
                    if (ppcp_peer_sync_add_timebase(s->p, s->d->tb[i].id.v, NULL) == PPCP_OK)
                        s->c.probe_timebases++;
                }
                (void)ppcp_peer_sync_trigger(s->p, PPCP_SYNC_ON_CONNECT);
            }
            delay_ms = 40;
            break;

        case STEP_STREAMS:
            if ((s->sc->flags & SIM_F_STREAMS) != 0)
                open_streams(s);
            delay_ms = 60;
            break;

        case STEP_ARM:
            if ((s->sc->flags & SIM_F_ARM) != 0) {
                /* 7.3a — arming is host-controlled, and the engine refuses it
                 * from anyone else. */
                if (ppcp_peer_arm(s->p, NULL, 0) != PPCP_OK)
                    sim_violation("the engine refused `arm` (7.3a: host only)");
            }
            delay_ms = 40;
            break;

        case STEP_OFFER:
            if ((s->sc->flags & SIM_F_OFFER) != 0 && !s->offer_sent) {
                ppcp_body_session_offer offer;
                char                    sid[80];
                memset(&offer, 0, sizeof(offer));
                snprintf(sid, sizeof(sid), "%s/stored", s->d->peer_id);
                if (ppcp_id_set_z(&offer.session_id, sid) != PPCP_OK ||
                    ppcp_id_set_z(&offer.minting_peer_id, s->d->peer_id) != PPCP_OK) {
                    sim_violation("could not name the stored Session");
                } else {
                    offer.completeness     = PPCP_COMPLETE;
                    offer.has_bytes_estimate = true;
                    offer.bytes_estimate   = s->bundle_len;
                    if (ppcp_peer_session_offer(s->p, &offer) != PPCP_OK)
                        sim_violation("the engine refused `session_offer` "
                                      "(does this peer declare Offline?)");
                    else
                        s->c.offers_tx++;
                }
                s->offer_sent = true;
            }
            /* Nothing is nominated until the clocks have met.  8.2a converts a
             * Candidate with the relations the host holds AT THAT MOMENT, and a
             * host that has not yet measured one excludes and retains it (8.2d)
             * — correct behaviour, and not what a happy-path row is asserting.
             * So the script waits for the first exchange of `relation_update`. */
            delay_ms = 700;
            break;

        case STEP_NOMINATE:
            if ((s->sc->flags & SIM_F_NOMINATE) != 0 &&
                s->nominated < s->sc->nominate_count) {
                emit_candidate(s);
                if (s->nominated < s->sc->nominate_count) {
                    /* Stay on this step: the next Candidate is `nominate_gap_ms`
                     * later, which is how "the second arrives after the host
                     * already issued" (CT-I7) is expressed. */
                    s->next_step_ns = now + s->sc->nominate_gap_ms * 1000000;
                    return;
                }
            }
            delay_ms = 250;
            break;

        case STEP_CAPTURES:
            /* From here on, every Shot this peer learns of gets a Capture; the
             * step opens the gate rather than doing the work once, because a
             * Shot that arrives later is still a Shot to capture for. */
            s->captures_open = ((s->sc->flags & SIM_F_CAPTURES) != 0);
            delay_ms = 0;
            break;

        default:
            break;
        }
        s->step++;
        s->next_step_ns = now + delay_ms * 1000000;
    }

    /* ------------------------------------------------------- continuous pumps */

    if ((s->sc->flags & SIM_F_SYNC) != 0) {
        size_t probes = 0;
        if (ppcp_peer_sync_pump(s->p, now, &probes) == PPCP_OK)
            s->c.probes_tx += (int64_t)probes;
        if (now - s->last_publish_ns > 150000000) {
            size_t published = 0;
            /* 6.1f — filtered, never stepped, and declared DIRECTLY: there is
             * no function in this library that composes two relations (I18). */
            if (ppcp_peer_publish_relations(s->p, &published) == PPCP_OK)
                s->c.relations_tx += (int64_t)published;
            s->last_publish_ns = now;
        }
    }

    if ((s->sc->flags & SIM_F_HEARTBEAT) != 0 && s->joined) {
        int64_t interval = (int64_t)s->d->heartbeat_ms * 1000000;
        if (now - s->last_hb_ns >= interval) {
            (void)ppcp_peer_heartbeat(s->p);
            s->last_hb_ns = now;
        }
    }
    (void)ppcp_peer_liveness_pump(s->p, now);

    if (s->mint != NULL) {
        int64_t ref = 0;
        size_t  minted = 0;
        /* 8.2i1 — no reading of `timebase_ref` is no Shot.  Not a zero offset,
         * not a guess: the Candidate is retained and that is the honest state. */
        if (now_ref(s, &ref) && ppcp_mint_pump(s->mint, ref, &minted) == PPCP_OK)
            s->c.shots_tx += (int64_t)minted;
    }
    if (s->arb != NULL && (s->sc->flags & SIM_F_NEVER_ISSUE) == 0) {
        int64_t ref = 0;
        size_t  issued = 0;
        int64_t hold = s->sc->arb_delay_ms * 1000000;
        if (now - s->start_ns >= hold && now_ref(s, &ref) &&
            ppcp_arbiter_pump(s->arb, ref, &issued) == PPCP_OK)
            s->c.shots_tx += (int64_t)issued;
    }

    if (s->captures_open)
        announce_captures(s);

    if (s->accept_arrived && s->replays_done == 0 && !g_replay.running)
        replay_start(s);
    replay_pump(s);
}
