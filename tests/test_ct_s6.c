/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * CT-S6 assertion 4 — "every message type in PPCP-MSG §11 decodes on a peer
 * declaring only Core", plus the catalogue and error-code obligations of
 * MSG §2 and §10.  Work package L5's evidence.
 *
 * The decoder takes no profile parameter, which is I24 expressed as a
 * signature rather than as a promise: comprehension is unconditional and only
 * ORIGINATION is gated.  So assertion 4 is discharged by building all
 * forty-five messages, encoding each on the channel its catalogue row names,
 * decoding it back, and asserting the type and a representative field survive.
 * The origination half is asserted alongside it against a Core-only profile
 * list — the peer that parses `candidate` and must never send one.
 *
 * The four things asserted here:
 *
 *   1. the catalogue is complete and self-consistent (45 rows, no duplicate
 *      name, no duplicate id, every row reachable both ways);
 *   2. every message round-trips encode -> decode;
 *   3. MSG §2's two channel rules refuse the two violations they name, and
 *      `link_bind` and `error` are legal on both;
 *   4. MSG §10's seventeen codes, with exactly two fatal, an unknown code
 *      NOT fatal, and `unsupported_version` refused without `detail.supported`
 *      (10.1f).
 */
#include "ppcp/message.h"
#include "ppcp/frame.h"

#include "test_util.h"

/* ------------------------------------------------------------------ scratch
 *
 * Everything the bodies hold by pointer.  One instance, on the stack of
 * main(), because nothing in the library allocates and a test should not
 * pretend otherwise. */
typedef struct scratch {
    ppcp_id                profiles[2];
    ppcp_timebase          timebases[1];
    ppcp_timebase_relation relations[1];
    ppcp_capture_profile   capture_profiles[1];
    ppcp_source            sources[1];
    ppcp_peer_desc         peer;
    int64_t                frames_ns[3];
    uint8_t                empty_map[1];   /* CBOR a0 — an empty map */
    uint8_t                blob[16];
    uint8_t                arena_buf[16384];
    ppcp_arena             arena;
} scratch;

static void scratch_init(scratch *s)
{
    memset(s, 0, sizeof(*s));
    s->empty_map[0] = 0xa0;
    memset(s->blob, 0x5a, sizeof(s->blob));
    s->frames_ns[0] = 1000;
    s->frames_ns[1] = 2000;
    s->frames_ns[2] = 3000;
    ppcp_arena_init(&s->arena, s->arena_buf, sizeof(s->arena_buf));
}

static ppcp_instant inst(const char *tb, int64_t ns)
{
    ppcp_instant i;
    memset(&i, 0, sizeof(i));
    if (ppcp_instant_make_z(&i, tb, ns) != PPCP_OK)
        abort();
    return i;
}

static ppcp_interval ivl(const char *tb, int64_t a, int64_t b)
{
    ppcp_interval v;
    memset(&v, 0, sizeof(v));
    if (ppcp_interval_make(&v, tb, strlen(tb), a, b) != PPCP_OK)
        abort();
    return v;
}

static ppcp_digest dig(uint8_t fill)
{
    ppcp_digest d;
    uint8_t     v[PPCP_SHA256_BYTES];
    memset(&d, 0, sizeof(d));
    memset(v, fill, sizeof(v));
    if (ppcp_digest_set(&d, v) != PPCP_OK)
        abort();
    return d;
}

static void set_id(ppcp_id *id, const char *s)
{
    if (ppcp_id_set_z(id, s) != PPCP_OK)
        abort();
}

/* A Peer declaration good enough for `declare`: one timebase, one Source with
 * one camera CaptureProfile on it, the Core profile, and the identity rules of
 * MSG 3.3b satisfied. */
static void build_peer(scratch *s)
{
    ppcp_timing   timing;
    ppcp_geometry geom;

    if (ppcp_timebase_make(&s->timebases[0], "tb:dev", 6, PPCP_TB_CONTINUOUS, true, 1000)
        != PPCP_OK) abort();
    if (ppcp_timing_make_nominal_frame_start(&timing, 120000, PPCP_PROV_ASSUMED) != PPCP_OK)
        abort();
    if (ppcp_geometry_make_rolling_shutter(&geom, 8000000, PPCP_PROV_ASSUMED,
                                           PPCP_ROLL_TOP_TO_BOTTOM, 1080) != PPCP_OK) abort();
    if (ppcp_capture_profile_make(&s->capture_profiles[0], "cp:1080p150", &timing) != PPCP_OK)
        abort();
    if (ppcp_capture_profile_set_camera(&s->capture_profiles[0], &geom, PPCP_INTR_PER_FRAME)
        != PPCP_OK) abort();
    if (ppcp_source_make(&s->sources[0], "src:back", "peer:dev", "camera", "tb:dev", true,
                         s->capture_profiles, 1) != PPCP_OK) abort();
    set_id(&s->profiles[0], PPCP_PROFILE_CORE);
    set_id(&s->profiles[1], PPCP_PROFILE_CAPTURE);
    if (ppcp_peer_desc_make(&s->peer, "peer:dev", PPCP_ROLE_CAPTURE, "1.0",
                            s->profiles, 2, s->timebases, 1) != PPCP_OK) abort();
    if (ppcp_peer_desc_set_sources(&s->peer, s->sources, 1) != PPCP_OK) abort();
    {
        ppcp_instant at = inst("tb:dev", 500);
        if (ppcp_relation_make_affine(&s->relations[0], "tb:dev", "tb:host", 12345, 3.0,
                                      50.0, 0.5, PPCP_RELM_MEASURED, &at) != PPCP_OK) abort();
    }
    if (ppcp_peer_desc_set_relations(&s->peer, s->relations, 1) != PPCP_OK) abort();
}

/* Fills `m` with a minimal VALID body for `t`.  Minimal means "every mandatory
 * field and nothing else", except where a field exists to be asserted — the
 * `detail.supported` of 10.1f, the AchievedFrames of 8.3g. */
static ppcp_result build(ppcp_msg *m, ppcp_msg_type t, scratch *s)
{
    ppcp_result rc = ppcp_msg_init(m, t, 7);
    if (rc != PPCP_OK)
        return rc;

    switch (t) {
    case PPCP_MT_LINK_BIND:
        memset(m->body.link_bind.link_id, 0xa7, PPCP_LINK_ID_BYTES);
        m->body.link_bind.channel = PPCP_CHANNEL_CONTROL;
        break;
    case PPCP_MT_HELLO:
        set_id(&m->body.hello.versions[0], "1.0");
        m->body.hello.version_count = 1;
        set_id(&m->body.hello.peer_id, "peer:dev");
        m->body.hello.role = PPCP_ROLE_CAPTURE;
        set_id(&m->body.hello.profiles[0], PPCP_PROFILE_CORE);
        m->body.hello.profile_count = 1;
        break;
    case PPCP_MT_HELLO_ACCEPT:
        set_id(&m->body.hello_accept.version, "1.0");
        set_id(&m->body.hello_accept.min_version, "1.0");
        set_id(&m->body.hello_accept.peer_id, "peer:host");
        m->body.hello_accept.role = PPCP_ROLE_HOST;
        set_id(&m->body.hello_accept.profiles[0], PPCP_PROFILE_CORE);
        m->body.hello_accept.profile_count = 1;
        break;
    case PPCP_MT_DECLARE:
        m->body.declare.generation = 1;
        m->body.declare.peer       = s->peer;
        break;
    case PPCP_MT_DECLARE_ACK:
        m->body.declare_ack.generation = 1;
        m->body.declare_ack.verdict    = PPCP_VERDICT_ACCEPTED;
        set_id(&m->body.declare_ack.notes[0].source_id, "src:back");
        set_id(&m->body.declare_ack.notes[0].profile_id, "cp:1080p150");
        m->body.declare_ack.notes[0].verdict = PPCP_VERDICT_REJECTED;
        m->body.declare_ack.notes[0].has_reason = true;
        set_id(&m->body.declare_ack.notes[0].reason, "ingest_policy");
        m->body.declare_ack.note_count = 1;
        break;
    case PPCP_MT_RELATION_UPDATE:
        m->body.relation_update.relations[0]  = s->relations[0];
        m->body.relation_update.relation_count = 1;
        break;
    case PPCP_MT_CALIBRATION_UPDATE: {
        ppcp_instant at = inst("tb:dev", 900);
        rc = ppcp_calibration_make(&m->body.calibration_update.calibration, "cal:1",
                                   "src:back", "intrinsics",
                                   s->empty_map, sizeof(s->empty_map),
                                   s->empty_map, sizeof(s->empty_map),
                                   PPCP_CALM_FACTORY, &at);
        if (rc != PPCP_OK) return rc;
        break;
    }
    case PPCP_MT_DISCONTINUITY: {
        ppcp_instant at = inst("tb:host", 900);
        rc = ppcp_clock_discontinuity_make(&m->body.discontinuity.discontinuity,
                                           "tb:wall", &at, -250000000, "ntp_step");
        if (rc != PPCP_OK) return rc;
        break;
    }
    case PPCP_MT_SESSION_OPEN:
        set_id(&m->body.session_open.session_id, "sess:1");
        set_id(&m->body.session_open.timebase_ref, "tb:host");
        m->body.session_open.has_arbitration       = true;
        m->body.session_open.coincidence_window_ns = PPCP_DEFAULT_COINCIDENCE_WINDOW_NS;
        m->body.session_open.issue_hold_ns         = PPCP_DEFAULT_ISSUE_HOLD_NS;
        m->body.session_open.has_heartbeat_interval = true;
        m->body.session_open.heartbeat_interval_ms  = PPCP_DEFAULT_HEARTBEAT_MS;
        break;
    case PPCP_MT_SESSION_JOINED:
        set_id(&m->body.session_joined.session_id, "sess:1");
        set_id(&m->body.session_joined.peer_id, "peer:dev");
        m->body.session_joined.verdict = PPCP_JOINED;
        break;
    case PPCP_MT_SESSION_RESUME:
        set_id(&m->body.session_resume.session_id, "sess:1");
        set_id(&m->body.session_resume.peer_id, "peer:dev");
        set_id(&m->body.session_resume.minted_shots[0], "shot:9");
        m->body.session_resume.minted_shot_count = 1;
        set_id(&m->body.session_resume.pending[0].capture_id, "cap:1");
        m->body.session_resume.pending[0].digest          = dig(0x11);
        m->body.session_resume.pending[0].bytes           = 4096;
        m->body.session_resume.pending[0].has_acked_index = true;
        m->body.session_resume.pending[0].acked_index     = 3;
        m->body.session_resume.pending_count = 1;
        break;
    case PPCP_MT_SESSION_STATE:
        set_id(&m->body.session_state.session_id, "sess:1");
        m->body.session_state.state        = PPCP_SESSION_OPEN;
        m->body.session_state.completeness = PPCP_PARTIAL;
        break;
    case PPCP_MT_CONTEXT_CHANGE: {
        ppcp_instant at = inst("tb:host", 1200);
        rc = ppcp_context_change_make(&m->body.context_change.context, "ctx:1", &at,
                                      "club", "7i");
        if (rc != PPCP_OK) return rc;
        break;
    }
    case PPCP_MT_SESSION_CLOSE:
        set_id(&m->body.session_close.session_id, "sess:1");
        set_id(&m->body.session_close.reason, "user_ended");
        break;
    case PPCP_MT_STREAM_OPEN: {
        ppcp_instant at = inst("tb:dev", 2000);
        rc = ppcp_stream_make(&m->body.stream_open.stream, "st:1", "sess:1", "src:back",
                              PPCP_STREAM_KIND_VIDEO, "cp:1080p150", "tb:dev",
                              PPCP_SHOT_WINDOWED, &at);
        if (rc != PPCP_OK) return rc;
        break;
    }
    case PPCP_MT_STREAM_OPEN_ACK:
        set_id(&m->body.stream_open_ack.stream_id, "st:1");
        m->body.stream_open_ack.verdict       = PPCP_STREAM_OPENED;
        m->body.stream_open_ack.has_opened_at = true;
        m->body.stream_open_ack.opened_at     = inst("tb:dev", 2000);
        break;
    case PPCP_MT_STREAM_CLOSE:
        set_id(&m->body.stream_close.stream_id, "st:1");
        /* F-H4-2 — `closed_at` is optional on the wire: the owner stamps it,
         * a consumer closing a Stream whose clock it cannot read does not. */
        m->body.stream_close.has_closed_at = true;
        m->body.stream_close.closed_at     = inst("tb:dev", 9000);
        set_id(&m->body.stream_close.reason, "not_needed");
        break;
    case PPCP_MT_ARM:
    case PPCP_MT_DISARM:
        set_id(&m->body.arm.stream_ids[0], "st:1");
        m->body.arm.stream_id_count = 1;
        break;
    case PPCP_MT_READINESS:
        rc = ppcp_readiness_not_settled(&m->body.readiness.readiness, 250);
        if (rc != PPCP_OK) return rc;
        set_id(&m->body.readiness.stream_ids[0], "st:1");
        m->body.readiness.stream_id_count = 1;
        break;
    case PPCP_MT_INTERRUPTION:
        set_id(&m->body.interruption.kind, "phone_call");
        m->body.interruption.interval  = ivl("tb:dev", 1000, 2000);
        m->body.interruption.recovered = true;
        set_id(&m->body.interruption.stream_ids[0], "st:1");
        m->body.interruption.stream_id_count = 1;
        break;
    case PPCP_MT_HEARTBEAT:
        m->body.heartbeat.seq = 42;
        break;
    case PPCP_MT_HEARTBEAT_ACK:
        m->body.heartbeat_ack.seq                = 42;
        m->body.heartbeat_ack.thermal            = PPCP_THERMAL_ELEVATED;
        m->body.heartbeat_ack.storage_free_bytes = 123456789u;
        m->body.heartbeat_ack.has_battery_pct    = true;
        m->body.heartbeat_ack.battery_pct        = 71;
        m->body.heartbeat_ack.has_charging       = true;
        m->body.heartbeat_ack.charging           = false;
        break;
    case PPCP_MT_SYNC_PROBE:
        m->body.sync_probe.probe_seq = 3;
        set_id(&m->body.sync_probe.timebase_id, "tb:device");
        m->body.sync_probe.t1 = inst("tb:device", 1723000000000LL);
        break;
    case PPCP_MT_SYNC_REPLY:
        m->body.sync_reply.probe_seq = 3;
        m->body.sync_reply.t1 = inst("tb:host", 100);
        m->body.sync_reply.t2 = inst("tb:device", 200);
        m->body.sync_reply.t3 = inst("tb:device", 300);
        break;
    case PPCP_MT_SYNC_RESIDUAL:
        set_id(&m->body.sync_residual.shot_id, "shot:1");
        set_id(&m->body.sync_residual.timebase_id, "tb:dev");
        m->body.sync_residual.residual_ns = -400000;
        set_id(&m->body.sync_residual.basis, "acoustic");
        break;
    case PPCP_MT_CANDIDATE: {
        ppcp_instant at = inst("tb:host", 5000);
        ppcp_estimate tof;
        rc = ppcp_candidate_make(&m->body.candidate.candidate, "cand:1", "peer:dev",
                                 "src:back", "acoustic", &at, 0.82);
        if (rc != PPCP_OK) return rc;
        rc = ppcp_estimate_make(&tof, -8700000, 400000.0);
        if (rc != PPCP_OK) return rc;
        rc = ppcp_candidate_set_tof_correction(&m->body.candidate.candidate, &tof);
        if (rc != PPCP_OK) return rc;
        break;
    }
    case PPCP_MT_SHOT: {
        ppcp_instant t0 = inst("tb:host", 5000);
        rc = ppcp_shot_make(&m->body.shot.shot, "shot:1", "sess:1", &t0,
                            PPCP_AUTHORITY_HOST, "peer:host", "cand:1");
        if (rc != PPCP_OK) return rc;
        rc = ppcp_shot_add_capture(&m->body.shot.shot, "cap:1");
        if (rc != PPCP_OK) return rc;
        break;
    }
    case PPCP_MT_CAPTURE_REQUEST:
        set_id(&m->body.capture_request.shot_id, "shot:1");
        m->body.capture_request.t0 = inst("tb:host", 5000);
        set_id(&m->body.capture_request.stream_ids[0], "st:1");
        m->body.capture_request.stream_id_count = 1;
        m->body.capture_request.pre_ns  = 500000000;
        m->body.capture_request.post_ns = 1500000000;
        break;
    case PPCP_MT_CAPTURE_ANNOUNCE: {
        ppcp_interval iv = ivl("tb:dev", 4000, 9000);
        rc = ppcp_capture_make_shot(&m->body.capture_announce.capture, "cap:1", "shot:1",
                                    "st:1", PPCP_COMPLETE);
        if (rc != PPCP_OK) return rc;
        rc = ppcp_capture_set_interval(&m->body.capture_announce.capture, &iv);
        if (rc != PPCP_OK) return rc;
        m->body.capture_announce.has_thumbnail = true;
        set_id(&m->body.capture_announce.thumbnail_format, "image/jpeg");
        m->body.capture_announce.thumbnail     = s->blob;
        m->body.capture_announce.thumbnail_len = sizeof(s->blob);
        break;
    }
    case PPCP_MT_CAPTURE_UPDATE:
        set_id(&m->body.capture_update.capture_id, "cap:1");
        m->body.capture_update.has_completeness = true;
        m->body.capture_update.completeness     = PPCP_COMPLETE;
        m->body.capture_update.has_transfer     = true;
        m->body.capture_update.transfer         = PPCP_TRANSFER_PRESENT;
        m->body.capture_update.digest           = dig(0x22);
        break;
    case PPCP_MT_CAPTURE_COMMITTED:
        set_id(&m->body.capture_committed.capture_id, "cap:1");
        m->body.capture_committed.digest = dig(0x22);
        break;
    case PPCP_MT_ANNOTATION: {
        ppcp_instant at = inst("tb:host", 5200);
        ppcp_instant cr = inst("tb:host", 5300);
        rc = ppcp_annotation_make(&m->body.annotation.annotation, "ann:1", "sess:1",
                                  "shot:1", &at, "peer:host", PPCP_ANNOT_USER,
                                  "text", "text/plain", s->blob, sizeof(s->blob),
                                  &cr, 1);
        if (rc != PPCP_OK) return rc;
        break;
    }
    case PPCP_MT_PAYLOAD_BEGIN: {
        ppcp_per_frame_i64 exp;
        set_id(&m->body.payload_begin.capture_id, "cap:1");
        m->body.payload_begin.bytes       = 3u * 1024u;
        m->body.payload_begin.digest      = dig(0x33);
        m->body.payload_begin.chunk_bytes = PPCP_DEFAULT_CHUNK_BYTES;
        rc = ppcp_achieved_frames_make(&m->body.payload_begin.achieved_frames, "tb:dev",
                                       s->frames_ns, 3);
        if (rc != PPCP_OK) return rc;
        rc = ppcp_per_frame_i64_scalar(&exp, 500000);
        if (rc != PPCP_OK) return rc;
        rc = ppcp_achieved_frames_set_exposure(&m->body.payload_begin.achieved_frames,
                                               &exp, PPCP_EXP_LOCKED_CONSTANT);
        if (rc != PPCP_OK) return rc;
        m->body.payload_begin.has_achieved_frames = true;
        break;
    }
    case PPCP_MT_PAYLOAD_CHUNK:
        set_id(&m->body.payload_chunk.capture_id, "cap:1");
        m->body.payload_chunk.index    = 2;
        m->body.payload_chunk.offset   = 2u * PPCP_DEFAULT_CHUNK_BYTES;
        m->body.payload_chunk.data     = s->blob;
        m->body.payload_chunk.data_len = sizeof(s->blob);
        m->body.payload_chunk.digest   = dig(0x44);
        break;
    case PPCP_MT_PAYLOAD_ACK:
        set_id(&m->body.payload_ack.capture_id, "cap:1");
        m->body.payload_ack.index = 2;
        break;
    case PPCP_MT_PAYLOAD_END:
        set_id(&m->body.payload_end.capture_id, "cap:1");
        m->body.payload_end.digest = dig(0x33);
        break;
    case PPCP_MT_PAYLOAD_ABORT:
        set_id(&m->body.payload_abort.capture_id, "cap:1");
        set_id(&m->body.payload_abort.reason, "malformed");
        break;
    case PPCP_MT_PAYLOAD_RESUME:
        set_id(&m->body.payload_resume.capture_id, "cap:1");
        m->body.payload_resume.from_index = 3;
        break;
    case PPCP_MT_SESSION_OFFER:
        set_id(&m->body.session_offer.session_id, "sess:1");
        set_id(&m->body.session_offer.minting_peer_id, "peer:dev");
        m->body.session_offer.completeness       = PPCP_COMPLETE;
        m->body.session_offer.has_bytes_estimate = true;
        m->body.session_offer.bytes_estimate     = 987654321u;
        break;
    case PPCP_MT_SESSION_ACCEPT:
        set_id(&m->body.session_accept.session_id, "sess:1");
        m->body.session_accept.verdict            = PPCP_OFFER_ACCEPT;
        m->body.session_accept.have_digests[0]    = dig(0x55);
        m->body.session_accept.have_digest_count  = 1;
        break;
    case PPCP_MT_SESSION_MANIFEST:
        set_id(&m->body.session_manifest.session_id, "sess:1");
        set_id(&m->body.session_manifest.streams[0], "st:1");
        m->body.session_manifest.stream_count = 1;
        set_id(&m->body.session_manifest.captures[0].capture_id, "cap:1");
        m->body.session_manifest.captures[0].digest = dig(0x33);
        m->body.session_manifest.captures[0].bytes  = 3072;
        set_id(&m->body.session_manifest.captures[0].stream_id, "st:1");
        m->body.session_manifest.capture_count   = 1;
        m->body.session_manifest.completeness    = PPCP_COMPLETE;
        m->body.session_manifest.count_shots     = 1;
        m->body.session_manifest.count_candidates = 2;
        m->body.session_manifest.count_captures  = 1;
        break;
    case PPCP_MT_SHOT_LINK:
        rc = ppcp_shot_link_make(&m->body.shot_link.link, "slink:1", "shot:1", "shot:2",
                                 PPCP_LINK_SHARED_CANDIDATE, 0.9);
        if (rc != PPCP_OK) return rc;
        rc = ppcp_shot_link_confirm(&m->body.shot_link.link, PPCP_CONFIRMED_BY_OBSERVER);
        if (rc != PPCP_OK) return rc;
        break;
    case PPCP_MT_SESSION_LINK:
        rc = ppcp_session_link_make_affine(&m->body.session_link.link, "sl:1", "sess:1",
                                           "sess:2", "tb:dev", "tb:host", 1000, 2.0,
                                           50.0, 0.4, "interval_alignment", "peer:host");
        if (rc != PPCP_OK) return rc;
        break;
    case PPCP_MT_ERROR:
        set_id(&m->body.error.code, PPCP_ERRCODE_PROFILE_NOT_SUPPORTED);
        memcpy(m->body.error.message, "no detect", 9);
        m->body.error.message_len    = 9;
        m->body.error.has_in_reply_to = true;
        m->body.error.in_reply_to     = 6;
        break;
    default:
        return PPCP_ERR_INVALID;
    }
    return PPCP_OK;
}

/* The channel a message of this class travels on when it is legal. */
static uint8_t legal_channel(ppcp_msg_channel c)
{
    return (c == PPCP_MSGCH_BULK) ? PPCP_CHANNEL_BULK : PPCP_CHANNEL_CONTROL;
}

int main(void)
{
    static scratch  s;
    static ppcp_msg out;
    static ppcp_msg in;
    static uint8_t  buf[65536];
    size_t          i, j, written, consumed;

    scratch_init(&s);
    build_peer(&s);

    /* ---------------------------------------------------- the catalogue */

    TEST("MSG §11 — forty-five messages, no duplicate name, no duplicate id");
    CHECK_EQ_I(ppcp_msg_count(), PPCP_MSG_COUNT);
    CHECK_EQ_I(PPCP_MSG_COUNT, 45);
    for (i = 0; i < ppcp_msg_count(); i++) {
        const ppcp_msg_info *a = ppcp_msg_at(i);
        ppcp_msg_info        found;
        CHECK(a != NULL && a->type != NULL && a->section != NULL);
        if (a == NULL)
            continue;
        /* reachable by name and by id, and the two agree */
        CHECK_EQ_I(ppcp_msg_lookup(a->type, strlen(a->type), &found), PPCP_OK);
        CHECK_EQ_I(found.id, a->id);
        CHECK(ppcp_msg_for(a->id) == a);
        for (j = 0; j < i; j++) {
            const ppcp_msg_info *b = ppcp_msg_at(j);
            CHECK(strcmp(a->type, b->type) != 0);
            CHECK(a->id != b->id);
        }
    }
    {
        ppcp_msg_info found;
        TEST("MSG 1b / I13 — an unknown type is a lookup miss, never an error");
        CHECK_EQ_I(ppcp_msg_lookup("com.example.nope", 16, &found), PPCP_ERR_NOT_FOUND);
    }

    /* ------------------------------------- CT-S6.4 — all forty-five decode */

    for (i = 0; i < ppcp_msg_count(); i++) {
        const ppcp_msg_info *info = ppcp_msg_at(i);
        const uint8_t       *payload;
        ppcp_frame_header    hdr;
        uint8_t              ch;
        ppcp_result          rc;

        if (info == NULL)
            continue;
        ch = legal_channel(info->channel);
        TEST(info->type);

        rc = build(&out, info->id, &s);
        CHECK_EQ_I(rc, PPCP_OK);
        if (rc != PPCP_OK)
            continue;

        written = 0;
        rc = ppcp_msg_encode(buf, sizeof(buf), ch, &out, &written);
        CHECK_EQ_I(rc, PPCP_OK);
        if (rc != PPCP_OK)
            continue;
        CHECK(written > PPCP_FRAME_HEADER_BYTES);

        /* The frame the encoder wrote is the frame a reader reads: header
         * first, then the payload the decoder sees.  Same bytes on a socket
         * and in a bundle (ENC 7a), which is why this is one path. */
        rc = ppcp_frame_read(buf, written, &hdr, &payload, &consumed);
        CHECK_EQ_I(rc, PPCP_OK);
        if (rc != PPCP_OK)
            continue;
        CHECK_EQ_I(hdr.channel, ch);
        CHECK_EQ_I(consumed, written);

        ppcp_arena_reset(&s.arena);
        memset(&in, 0, sizeof(in));
        /* I24: no profile parameter.  A Core-only peer runs exactly this
         * call for a `candidate` it may never originate. */
        rc = ppcp_msg_decode(payload, hdr.payload_len,
                             ppcp_cbor_limits_for_channel(ch), &s.arena, &in);
        CHECK_EQ_I(rc, PPCP_OK);
        if (rc != PPCP_OK)
            continue;
        CHECK_EQ_I(in.type, info->id);
        CHECK_EQ_I(in.env.msg_id, 7);
        CHECK(ppcp_cbor_key_is(in.env.type, in.env.type_len, info->type));
    }

    /* A handful of representative fields, checked by value rather than by
     * "it decoded": a decoder that returned OK and wrote nothing would pass
     * the loop above. */
    {
        ppcp_frame_header hdr;
        const uint8_t    *payload;

        TEST("round-trip carries values, not just shapes");
        CHECK_EQ_I(build(&out, PPCP_MT_SYNC_PROBE, &s), PPCP_OK);
        CHECK_EQ_I(ppcp_msg_encode(buf, sizeof(buf), PPCP_CHANNEL_CONTROL, &out, &written),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_frame_read(buf, written, &hdr, &payload, &consumed), PPCP_OK);
        ppcp_arena_reset(&s.arena);
        memset(&in, 0, sizeof(in));
        CHECK_EQ_I(ppcp_msg_decode(payload, hdr.payload_len,
                                   ppcp_cbor_limits_for_channel(PPCP_CHANNEL_CONTROL),
                                   &s.arena, &in), PPCP_OK);
        CHECK_EQ_I(in.body.sync_probe.probe_seq, 3);
        CHECK_EQ_I(in.body.sync_probe.t1.ns, 1723000000000LL);
        CHECK(ppcp_cbor_key_is(in.body.sync_probe.timebase_id.v,
                               in.body.sync_probe.timebase_id.len, "tb:device"));

        TEST("declare survives with its Sources and its generation");
        CHECK_EQ_I(build(&out, PPCP_MT_DECLARE, &s), PPCP_OK);
        CHECK_EQ_I(ppcp_msg_encode(buf, sizeof(buf), PPCP_CHANNEL_CONTROL, &out, &written),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_frame_read(buf, written, &hdr, &payload, &consumed), PPCP_OK);
        ppcp_arena_reset(&s.arena);
        memset(&in, 0, sizeof(in));
        CHECK_EQ_I(ppcp_msg_decode(payload, hdr.payload_len,
                                   ppcp_cbor_limits_for_channel(PPCP_CHANNEL_CONTROL),
                                   &s.arena, &in), PPCP_OK);
        CHECK_EQ_I(in.body.declare.generation, 1);
        CHECK_EQ_I(in.body.declare.peer.source_count, 1);
        CHECK_EQ_I(in.body.declare.peer.timebase_count, 1);
        CHECK_EQ_I(in.body.declare.peer.relation_count, 1);
        CHECK(ppcp_id_equal(&in.body.declare.peer.id, &s.peer.id));

        TEST("payload_begin carries AchievedFrames on bulk (8.3g, I30)");
        CHECK_EQ_I(build(&out, PPCP_MT_PAYLOAD_BEGIN, &s), PPCP_OK);
        CHECK_EQ_I(ppcp_msg_encode(buf, sizeof(buf), PPCP_CHANNEL_BULK, &out, &written),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_frame_read(buf, written, &hdr, &payload, &consumed), PPCP_OK);
        ppcp_arena_reset(&s.arena);
        memset(&in, 0, sizeof(in));
        CHECK_EQ_I(ppcp_msg_decode(payload, hdr.payload_len,
                                   ppcp_cbor_limits_for_channel(PPCP_CHANNEL_BULK),
                                   &s.arena, &in), PPCP_OK);
        CHECK(in.body.payload_begin.has_achieved_frames);
        CHECK_EQ_I(in.body.payload_begin.achieved_frames.frame_count, 3);
        CHECK_EQ_I(in.body.payload_begin.achieved_frames.exposure_ns.form,
                   PPCP_PER_FRAME_SCALAR);
        CHECK_EQ_I(in.body.payload_begin.achieved_frames.exposure_ns.scalar, 500000);
    }

    /* --------------------------------------------- MSG §2 — channel rules */

    for (i = 0; i < ppcp_msg_count(); i++) {
        const ppcp_msg_info *info = ppcp_msg_at(i);
        if (info == NULL)
            continue;
        TEST(info->type);
        switch (info->channel) {
        case PPCP_MSGCH_CONTROL:
            /* 2b: no control message on a bulk channel. */
            CHECK_EQ_I(ppcp_msg_check_channel(info->id, PPCP_CHANNEL_CONTROL), PPCP_OK);
            CHECK_EQ_I(ppcp_msg_check_channel(info->id, PPCP_CHANNEL_BULK),
                       PPCP_ERR_MALFORMED);
            break;
        case PPCP_MSGCH_BULK:
            /* 2a: no payload_* on the control channel. */
            CHECK_EQ_I(ppcp_msg_check_channel(info->id, PPCP_CHANNEL_BULK), PPCP_OK);
            CHECK_EQ_I(ppcp_msg_check_channel(info->id, PPCP_CHANNEL_CONTROL),
                       PPCP_ERR_MALFORMED);
            break;
        case PPCP_MSGCH_ANY:
        default:
            /* ENC 2.1a / MSG 3.0a: `link_bind` is the first frame on EVERY
             * stream, and `error` must reach a peer on whichever channel
             * offended. */
            CHECK_EQ_I(ppcp_msg_check_channel(info->id, PPCP_CHANNEL_CONTROL), PPCP_OK);
            CHECK_EQ_I(ppcp_msg_check_channel(info->id, PPCP_CHANNEL_BULK), PPCP_OK);
            break;
        }
        /* ENC 2a: 255 is reserved and is nobody's channel. */
        CHECK(ppcp_msg_check_channel(info->id, PPCP_CHANNEL_RESERVED) != PPCP_OK);
    }

    TEST("MSG §2 — the encoder refuses the violation, it does not merely note it");
    CHECK_EQ_I(build(&out, PPCP_MT_PAYLOAD_CHUNK, &s), PPCP_OK);
    CHECK_EQ_I(ppcp_msg_encode(buf, sizeof(buf), PPCP_CHANNEL_CONTROL, &out, &written),
               PPCP_ERR_MALFORMED);
    CHECK_EQ_I(build(&out, PPCP_MT_HEARTBEAT, &s), PPCP_OK);
    CHECK_EQ_I(ppcp_msg_encode(buf, sizeof(buf), PPCP_CHANNEL_BULK, &out, &written),
               PPCP_ERR_MALFORMED);

    /* ------------------------------- C2 / I24 — origination on a Core peer */

    {
        ppcp_id core_only[1];
        set_id(&core_only[0], PPCP_PROFILE_CORE);
        for (i = 0; i < ppcp_msg_count(); i++) {
            const ppcp_msg_info *info = ppcp_msg_at(i);
            bool expect;
            if (info == NULL)
                continue;
            TEST(info->type);
            /* NULL means no profile confers it — `link_bind`, `hello`,
             * `hello_accept`, `error` — and those a Core peer may send. */
            expect = (info->originating_profile == NULL) ||
                     (strcmp(info->originating_profile, PPCP_PROFILE_CORE) == 0);
            CHECK_EQ_I(ppcp_msg_profiles_confer(info->id, core_only, 1), expect);
        }
        TEST("CT-S6.2 — a Core-only peer never originates `candidate`");
        CHECK(!ppcp_msg_profiles_confer(PPCP_MT_CANDIDATE, core_only, 1));
        TEST("CT-S6.1 — Core + Arbitrate + Live + Offline arbitrates without Detect");
        {
            ppcp_id set[4];
            set_id(&set[0], PPCP_PROFILE_CORE);
            set_id(&set[1], PPCP_PROFILE_ARBITRATE);
            set_id(&set[2], PPCP_PROFILE_LIVE);
            set_id(&set[3], PPCP_PROFILE_OFFLINE);
            CHECK(ppcp_msg_profiles_confer(PPCP_MT_SHOT, set, 4));
            CHECK(ppcp_msg_profiles_confer(PPCP_MT_CAPTURE_REQUEST, set, 4));
            CHECK(!ppcp_msg_profiles_confer(PPCP_MT_CANDIDATE, set, 4));
        }
    }

    /* ------------------------------------------------ MSG §10 — error codes */

    TEST("MSG §10 — seventeen codes, exactly two fatal");
    CHECK_EQ_I(ppcp_error_code_count(), 17);
    {
        size_t fatal = 0;
        for (i = 0; i < ppcp_error_code_count(); i++) {
            const ppcp_error_info *e = ppcp_error_code_at(i);
            CHECK(e != NULL && e->code != NULL);
            if (e == NULL)
                continue;
            if (e->fatal)
                fatal++;
            /* the table and the predicate are one answer, not two */
            CHECK_EQ_I(ppcp_msg_error_is_fatal(e->code, strlen(e->code)), e->fatal);
            for (j = 0; j < i; j++)
                CHECK(strcmp(e->code, ppcp_error_code_at(j)->code) != 0);
        }
        CHECK_EQ_I(fatal, 2);
    }
    CHECK(ppcp_msg_error_is_fatal(PPCP_ERRCODE_UNSUPPORTED_VERSION,
                                  strlen(PPCP_ERRCODE_UNSUPPORTED_VERSION)));
    CHECK(ppcp_msg_error_is_fatal(PPCP_ERRCODE_ROLE_CONFLICT,
                                  strlen(PPCP_ERRCODE_ROLE_CONFLICT)));
    TEST("MSG 10b / C3 — `profile_not_supported` never closes the transport");
    CHECK(!ppcp_msg_error_is_fatal(PPCP_ERRCODE_PROFILE_NOT_SUPPORTED,
                                   strlen(PPCP_ERRCODE_PROFILE_NOT_SUPPORTED)));
    CHECK(!ppcp_msg_error_is_fatal(PPCP_ERRCODE_MALFORMED, strlen(PPCP_ERRCODE_MALFORMED)));
    TEST("an unknown code is NOT fatal — otherwise every MINOR addition disconnects");
    CHECK(!ppcp_msg_error_is_fatal("com.example.unheard_of", 22));

    /* ------------------------------- 10.1f — unsupported_version and detail */

    {
        ppcp_frame_header hdr;
        const uint8_t    *payload;

        TEST("10.1f — `unsupported_version` without `detail.supported` is refused");
        CHECK_EQ_I(build(&out, PPCP_MT_ERROR, &s), PPCP_OK);
        set_id(&out.body.error.code, PPCP_ERRCODE_UNSUPPORTED_VERSION);
        CHECK_EQ_I(ppcp_msg_encode(buf, sizeof(buf), PPCP_CHANNEL_CONTROL, &out, &written),
                   PPCP_ERR_INVALID);

        TEST("10.1f — with the sender's full supported range it round-trips");
        out.body.error.has_detail_supported = true;
        set_id(&out.body.error.detail_supported[0], "1.0");
        set_id(&out.body.error.detail_supported[1], "1.1");
        out.body.error.detail_supported_count = 2;
        out.body.error.has_detail_reason = true;
        set_id(&out.body.error.detail_reason, "no_common_major");
        CHECK_EQ_I(ppcp_msg_encode(buf, sizeof(buf), PPCP_CHANNEL_CONTROL, &out, &written),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_frame_read(buf, written, &hdr, &payload, &consumed), PPCP_OK);
        ppcp_arena_reset(&s.arena);
        memset(&in, 0, sizeof(in));
        CHECK_EQ_I(ppcp_msg_decode(payload, hdr.payload_len,
                                   ppcp_cbor_limits_for_channel(PPCP_CHANNEL_CONTROL),
                                   &s.arena, &in), PPCP_OK);
        CHECK(in.body.error.has_detail_supported);
        CHECK_EQ_I(in.body.error.detail_supported_count, 2);
        CHECK(ppcp_cbor_key_is(in.body.error.detail_supported[0].v,
                               in.body.error.detail_supported[0].len, "1.0"));
        CHECK(ppcp_cbor_key_is(in.body.error.detail_supported[1].v,
                               in.body.error.detail_supported[1].len, "1.1"));
        CHECK(ppcp_msg_error_is_fatal(in.body.error.code.v, in.body.error.code.len));

        /* An `error` on the bulk channel is legal — that is what PPCP_MSGCH_ANY
         * is for, and a chunk digest mismatch has to be answerable there. */
        TEST("`error` reaches the peer on the channel that offended");
        CHECK_EQ_I(ppcp_msg_encode(buf, sizeof(buf), PPCP_CHANNEL_BULK, &out, &written),
                   PPCP_OK);
    }

    /* An unknown type decodes to PPCP_MT_UNKNOWN rather than failing: MSG 1b
     * forbids closing the transport on one and I13 makes it ignorable. */
    {
        ppcp_frame_header hdr;
        const uint8_t    *payload;
        ppcp_envelope     env;
        ppcp_result       rc;

        TEST("MSG 1b — an unknown message type decodes as UNKNOWN, not malformed");
        CHECK_EQ_I(ppcp_envelope_init(&env, "com.example.telemetry", 11), PPCP_OK);
        CHECK_EQ_I(ppcp_message_encode(buf, sizeof(buf), PPCP_CHANNEL_CONTROL, &env, 0,
                                       NULL, NULL, &written), PPCP_OK);
        CHECK_EQ_I(ppcp_frame_read(buf, written, &hdr, &payload, &consumed), PPCP_OK);
        ppcp_arena_reset(&s.arena);
        memset(&in, 0, sizeof(in));
        rc = ppcp_msg_decode(payload, hdr.payload_len,
                             ppcp_cbor_limits_for_channel(PPCP_CHANNEL_CONTROL),
                             &s.arena, &in);
        CHECK_EQ_I(rc, PPCP_OK);
        CHECK_EQ_I(in.type, PPCP_MT_UNKNOWN);
        CHECK(ppcp_cbor_key_is(in.type_name.v, in.type_name.len, "com.example.telemetry"));
    }

    TEST_MAIN_END();
}
