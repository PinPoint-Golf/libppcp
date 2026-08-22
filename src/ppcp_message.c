/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * PPCP-MSG §3–§11 — the forty-five messages and the seventeen error codes.
 * Work package L5.
 *
 * The catalogue below is the artefact CONF 5b1 asks for: "for every normative
 * clause that requires originating a message, the profile that binds the
 * clause confers that message.  This is mechanical — a script over the message
 * index and the clause list."  Making it a table a program can read is what
 * turns that audit from a review round into a test (work package L16).
 *
 * The codec decodes all forty-five unconditionally.  I24: profiles gate
 * ORIGINATION, not comprehension; every conformant peer parses the complete
 * type vocabulary.  An implementation that only ever talks to itself never
 * receives a message from a profile it lacks, which is why CT-S6 assertion 4
 * exists and why there is no profile parameter on ppcp_msg_decode().
 */
#include "ppcp/message.h"
#include "ppcp_codec.h"

#include <string.h>

/* ==================================================== the message index
 *
 * MSG §11, in its order.  `originating_profile` is NULL for the four messages
 * no profile confers: `link_bind` is the transport binding of ENC §2.1,
 * `hello` and `hello_accept` precede declaration, and `error` must remain
 * available to a peer that has declared nothing at all.
 */
static const ppcp_msg_info msg_table[PPCP_MSG_COUNT] = {
 { "link_bind",          PPCP_MT_LINK_BIND,          PPCP_MSG_EVENT,    PPCP_MSGCH_ANY,     NULL,                   "3.0" },
 { "hello",              PPCP_MT_HELLO,              PPCP_MSG_REQUEST,  PPCP_MSGCH_CONTROL, NULL,                   "3.1" },
 { "hello_accept",       PPCP_MT_HELLO_ACCEPT,       PPCP_MSG_RESPONSE, PPCP_MSGCH_CONTROL, NULL,                   "3.2" },
 { "declare",            PPCP_MT_DECLARE,            PPCP_MSG_REQUEST,  PPCP_MSGCH_CONTROL, PPCP_PROFILE_CORE,      "3.3" },
 { "declare_ack",        PPCP_MT_DECLARE_ACK,        PPCP_MSG_RESPONSE, PPCP_MSGCH_CONTROL, PPCP_PROFILE_CORE,      "3.4" },
 { "relation_update",    PPCP_MT_RELATION_UPDATE,    PPCP_MSG_EVENT,    PPCP_MSGCH_CONTROL, PPCP_PROFILE_CORE,      "3.5" },
 { "calibration_update", PPCP_MT_CALIBRATION_UPDATE, PPCP_MSG_EVENT,    PPCP_MSGCH_CONTROL, PPCP_PROFILE_CAPTURE,   "3.6" },
 { "discontinuity",      PPCP_MT_DISCONTINUITY,      PPCP_MSG_EVENT,    PPCP_MSGCH_CONTROL, PPCP_PROFILE_CORE,      "3.7" },
 { "session_open",       PPCP_MT_SESSION_OPEN,       PPCP_MSG_REQUEST,  PPCP_MSGCH_CONTROL, PPCP_PROFILE_CORE,      "4.1" },
 { "session_joined",     PPCP_MT_SESSION_JOINED,     PPCP_MSG_RESPONSE, PPCP_MSGCH_CONTROL, PPCP_PROFILE_CORE,      "4.2" },
 { "session_resume",     PPCP_MT_SESSION_RESUME,     PPCP_MSG_REQUEST,  PPCP_MSGCH_CONTROL, PPCP_PROFILE_LIVE,      "4.3" },
 { "session_state",      PPCP_MT_SESSION_STATE,      PPCP_MSG_EVENT,    PPCP_MSGCH_CONTROL, PPCP_PROFILE_CORE,      "4.4" },
 { "context_change",     PPCP_MT_CONTEXT_CHANGE,     PPCP_MSG_EVENT,    PPCP_MSGCH_CONTROL, PPCP_PROFILE_CORE,      "4.4" },
 { "session_close",      PPCP_MT_SESSION_CLOSE,      PPCP_MSG_EVENT,    PPCP_MSGCH_CONTROL, PPCP_PROFILE_CORE,      "4.4" },
 { "stream_open",        PPCP_MT_STREAM_OPEN,        PPCP_MSG_REQUEST,  PPCP_MSGCH_CONTROL, PPCP_PROFILE_CAPTURE,   "5.1" },
 { "stream_open_ack",    PPCP_MT_STREAM_OPEN_ACK,    PPCP_MSG_RESPONSE, PPCP_MSGCH_CONTROL, PPCP_PROFILE_CAPTURE,   "5.1" },
 { "stream_close",       PPCP_MT_STREAM_CLOSE,       PPCP_MSG_EVENT,    PPCP_MSGCH_CONTROL, PPCP_PROFILE_CAPTURE,   "5.1" },
 { "arm",                PPCP_MT_ARM,                PPCP_MSG_REQUEST,  PPCP_MSGCH_CONTROL, PPCP_PROFILE_LIVE,      "5.2" },
 { "disarm",             PPCP_MT_DISARM,             PPCP_MSG_REQUEST,  PPCP_MSGCH_CONTROL, PPCP_PROFILE_LIVE,      "5.2" },
 { "readiness",          PPCP_MT_READINESS,          PPCP_MSG_EVENT,    PPCP_MSGCH_CONTROL, PPCP_PROFILE_CAPTURE,   "5.2" },
 { "interruption",       PPCP_MT_INTERRUPTION,       PPCP_MSG_EVENT,    PPCP_MSGCH_CONTROL, PPCP_PROFILE_CAPTURE,   "5.3" },
 { "heartbeat",          PPCP_MT_HEARTBEAT,          PPCP_MSG_REQUEST,  PPCP_MSGCH_CONTROL, PPCP_PROFILE_LIVE,      "5.4" },
 { "heartbeat_ack",      PPCP_MT_HEARTBEAT_ACK,      PPCP_MSG_RESPONSE, PPCP_MSGCH_CONTROL, PPCP_PROFILE_LIVE,      "5.4" },
 { "sync_probe",         PPCP_MT_SYNC_PROBE,         PPCP_MSG_REQUEST,  PPCP_MSGCH_CONTROL, PPCP_PROFILE_LIVE,      "6.1" },
 { "sync_reply",         PPCP_MT_SYNC_REPLY,         PPCP_MSG_RESPONSE, PPCP_MSGCH_CONTROL, PPCP_PROFILE_LIVE,      "6.1" },
 { "sync_residual",      PPCP_MT_SYNC_RESIDUAL,      PPCP_MSG_EVENT,    PPCP_MSGCH_CONTROL, PPCP_PROFILE_LIVE,      "6.2" },
 { "candidate",          PPCP_MT_CANDIDATE,          PPCP_MSG_EVENT,    PPCP_MSGCH_CONTROL, PPCP_PROFILE_DETECT,    "7.1" },
 /* MSG §11 gives `shot` as "Mint / Arbitrate": a device mints under Mint, a
  * host issues under Arbitrate.  The table carries Mint and
  * ppcp_msg_profiles_confer() accepts either, because a peer with only one of
  * them may still originate it. */
 { "shot",               PPCP_MT_SHOT,               PPCP_MSG_EVENT,    PPCP_MSGCH_CONTROL, PPCP_PROFILE_MINT,      "7.2" },
 { "capture_request",    PPCP_MT_CAPTURE_REQUEST,    PPCP_MSG_REQUEST,  PPCP_MSGCH_CONTROL, PPCP_PROFILE_ARBITRATE, "7.3" },
 { "capture_announce",   PPCP_MT_CAPTURE_ANNOUNCE,   PPCP_MSG_EVENT,    PPCP_MSGCH_CONTROL, PPCP_PROFILE_CAPTURE,   "8.1" },
 { "capture_update",     PPCP_MT_CAPTURE_UPDATE,     PPCP_MSG_EVENT,    PPCP_MSGCH_CONTROL, PPCP_PROFILE_CAPTURE,   "8.2" },
 { "capture_committed",  PPCP_MT_CAPTURE_COMMITTED,  PPCP_MSG_EVENT,    PPCP_MSGCH_CONTROL, PPCP_PROFILE_CAPTURE,   "8.4" },
 { "annotation",         PPCP_MT_ANNOTATION,         PPCP_MSG_EVENT,    PPCP_MSGCH_CONTROL, PPCP_PROFILE_MARKUP,    "9.0" },
 { "payload_begin",      PPCP_MT_PAYLOAD_BEGIN,      PPCP_MSG_EVENT,    PPCP_MSGCH_BULK,    PPCP_PROFILE_CAPTURE,   "8.3" },
 { "payload_chunk",      PPCP_MT_PAYLOAD_CHUNK,      PPCP_MSG_EVENT,    PPCP_MSGCH_BULK,    PPCP_PROFILE_CAPTURE,   "8.3" },
 { "payload_ack",        PPCP_MT_PAYLOAD_ACK,        PPCP_MSG_EVENT,    PPCP_MSGCH_BULK,    PPCP_PROFILE_CAPTURE,   "8.3" },
 { "payload_end",        PPCP_MT_PAYLOAD_END,        PPCP_MSG_EVENT,    PPCP_MSGCH_BULK,    PPCP_PROFILE_CAPTURE,   "8.3" },
 { "payload_abort",      PPCP_MT_PAYLOAD_ABORT,      PPCP_MSG_EVENT,    PPCP_MSGCH_BULK,    PPCP_PROFILE_CAPTURE,   "8.3" },
 { "payload_resume",     PPCP_MT_PAYLOAD_RESUME,     PPCP_MSG_REQUEST,  PPCP_MSGCH_BULK,    PPCP_PROFILE_CAPTURE,   "8.3" },
 { "session_offer",      PPCP_MT_SESSION_OFFER,      PPCP_MSG_REQUEST,  PPCP_MSGCH_CONTROL, PPCP_PROFILE_OFFLINE,   "9.1" },
 { "session_accept",     PPCP_MT_SESSION_ACCEPT,     PPCP_MSG_RESPONSE, PPCP_MSGCH_CONTROL, PPCP_PROFILE_OFFLINE,   "9.1" },
 { "session_manifest",   PPCP_MT_SESSION_MANIFEST,   PPCP_MSG_EVENT,    PPCP_MSGCH_CONTROL, PPCP_PROFILE_OFFLINE,   "9.2" },
 /* 9.3g: documented in the reconciliation section but a CORE message.
  * `arrival_pairing` and `shared_candidate` links are asserted live, on a
  * socket, by peers that may implement no bundle handling at all. */
 { "shot_link",          PPCP_MT_SHOT_LINK,          PPCP_MSG_EVENT,    PPCP_MSGCH_CONTROL, PPCP_PROFILE_CORE,      "9.3" },
 { "session_link",       PPCP_MT_SESSION_LINK,       PPCP_MSG_EVENT,    PPCP_MSGCH_CONTROL, PPCP_PROFILE_OFFLINE,   "9.4" },
 { "error",              PPCP_MT_ERROR,             PPCP_MSG_EVENT,    PPCP_MSGCH_ANY,     NULL,                   "10"  }
};

size_t ppcp_msg_count(void) { return PPCP_MSG_COUNT; }

const ppcp_msg_info *ppcp_msg_at(size_t index)
{
    if (index >= PPCP_MSG_COUNT)
        return NULL;
    return &msg_table[index];
}

const ppcp_msg_info *ppcp_msg_for(ppcp_msg_type t)
{
    size_t i;
    for (i = 0; i < PPCP_MSG_COUNT; i++)
        if (msg_table[i].id == t)
            return &msg_table[i];
    return NULL;
}

ppcp_result ppcp_msg_lookup(const char *type, size_t len, ppcp_msg_info *out)
{
    size_t i;
    if (type == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    for (i = 0; i < PPCP_MSG_COUNT; i++) {
        if (ppcp_cbor_key_is(type, len, msg_table[i].type)) {
            *out = msg_table[i];
            return PPCP_OK;
        }
    }
    /* MSG 1b / I13: an unknown type is not fatal and does not close the
     * transport.  PPCP_ERR_NOT_FOUND is a lookup miss, never an error code. */
    return PPCP_ERR_NOT_FOUND;
}

ppcp_result ppcp_msg_check_channel(ppcp_msg_type t, uint8_t channel)
{
    const ppcp_msg_info *info = ppcp_msg_for(t);
    ppcp_result          rc   = ppcp_channel_validate(channel);
    if (rc != PPCP_OK)
        return rc;
    if (info == NULL)
        return PPCP_OK;    /* an unknown type is nobody's channel violation */
    switch (info->channel) {
    case PPCP_MSGCH_CONTROL:
        /* MSG 2b: no control message on a bulk channel. */
        return (channel == PPCP_CHANNEL_CONTROL) ? PPCP_OK : PPCP_ERR_MALFORMED;
    case PPCP_MSGCH_BULK:
        /* MSG 2a: no payload_chunk on the control channel. */
        return ppcp_channel_is_bulk(channel) ? PPCP_OK : PPCP_ERR_MALFORMED;
    case PPCP_MSGCH_ANY:
    default:
        return PPCP_OK;
    }
}

bool ppcp_msg_profiles_confer(ppcp_msg_type t, const ppcp_id *profiles, size_t profile_count)
{
    const ppcp_msg_info *info = ppcp_msg_for(t);
    size_t               i;

    if (info == NULL)
        return false;             /* a peer does not originate what it cannot name */
    if (info->originating_profile == NULL)
        return true;              /* link_bind, hello, hello_accept, error */

    for (i = 0; i < profile_count; i++) {
        if (ppcp_cbor_key_is(profiles[i].v, profiles[i].len, info->originating_profile))
            return true;
        /* MSG §11: `shot` is "Mint / Arbitrate" — a device mints under Mint,
         * a host issues under Arbitrate, and either confers it. */
        if (t == PPCP_MT_SHOT &&
            ppcp_cbor_key_is(profiles[i].v, profiles[i].len, PPCP_PROFILE_ARBITRATE))
            return true;
    }
    return false;
}

/* ======================================================== MSG §10 errors */

static const ppcp_error_info error_table[] = {
    /* Exactly two are fatal.  MSG 10b: sending or receiving an `error` does
     * NOT close the transport except for these — dropping a live capture
     * session because one field was too long would violate the principle that
     * capture degrades last (ENC 8b). */
    { PPCP_ERRCODE_UNSUPPORTED_VERSION,   true  },
    { PPCP_ERRCODE_ROLE_CONFLICT,         true  },
    { PPCP_ERRCODE_MALFORMED,             false },
    { PPCP_ERRCODE_PROFILE_NOT_SUPPORTED, false },
    { PPCP_ERRCODE_POLICY_REJECT,         false },
    { PPCP_ERRCODE_UNKNOWN_SESSION,       false },
    { PPCP_ERRCODE_UNKNOWN_STREAM,        false },
    { PPCP_ERRCODE_UNKNOWN_CAPTURE,       false },
    { PPCP_ERRCODE_NOT_DECLARED,          false },
    { PPCP_ERRCODE_RELATION_MISSING,      false },
    { PPCP_ERRCODE_RELATION_UNCERTAIN,    false },
    { PPCP_ERRCODE_NOT_ARMED,             false },
    { PPCP_ERRCODE_OUTSIDE_BUFFER,        false },
    { PPCP_ERRCODE_STORAGE_FULL,          false },
    { PPCP_ERRCODE_ALREADY_PRESENT,       false },
    { PPCP_ERRCODE_RESOURCE_EXHAUSTED,    false },
    { PPCP_ERRCODE_INTERNAL,              false }
};

#define PPCP_ERRCODE_COUNT (sizeof(error_table) / sizeof(error_table[0]))

size_t ppcp_error_code_count(void) { return PPCP_ERRCODE_COUNT; }

const ppcp_error_info *ppcp_error_code_at(size_t index)
{
    if (index >= PPCP_ERRCODE_COUNT)
        return NULL;
    return &error_table[index];
}

bool ppcp_msg_error_is_fatal(const char *code, size_t len)
{
    size_t i;
    if (code == NULL)
        return false;
    for (i = 0; i < PPCP_ERRCODE_COUNT; i++)
        if (ppcp_cbor_key_is(code, len, error_table[i].code))
            return error_table[i].fatal;
    /* An unrecognised code is NOT fatal.  A peer that closed on a code it did
     * not know would make every future MINOR addition a disconnection (I13). */
    return false;
}

/* ================================================== small shared codecs */

static const ppcp_enum_map verdict_map[] = {
    { "accepted", PPCP_VERDICT_ACCEPTED }, { "rejected", PPCP_VERDICT_REJECTED }, { NULL, 0 }
};
static const ppcp_enum_map join_verdict_map[] = {
    { "joined", PPCP_JOINED }, { "refused", PPCP_REFUSED }, { NULL, 0 }
};
static const ppcp_enum_map stream_verdict_map[] = {
    { "opened", PPCP_STREAM_OPENED }, { "refused", PPCP_STREAM_REFUSED }, { NULL, 0 }
};
static const ppcp_enum_map offer_verdict_map[] = {
    { "accept",       PPCP_OFFER_ACCEPT       },
    { "already_held", PPCP_OFFER_ALREADY_HELD },
    { "refuse",       PPCP_OFFER_REFUSE       },
    { NULL, 0 }
};

typedef struct idlist_c { const ppcp_id *v; size_t n; } idlist_c;
typedef struct idlist_d { ppcp_id *v; size_t cap; size_t *n; } idlist_d;

static ppcp_result idlist_w(ppcp_cbor_writer *w, const void *ctx)
{
    const idlist_c *l = (const idlist_c *)ctx;
    return ppcp_rec_write_array(w, l->v, sizeof(ppcp_id), l->n, ppcp_elem_write_id);
}

static ppcp_result idlist_r(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    idlist_d *l = (idlist_d *)dst;
    (void)ctx;
    return ppcp_rec_read_array(r, l->v, sizeof(ppcp_id), l->cap, l->n,
                               ppcp_sub_read_id_elem, NULL);
}

static ppcp_result digest_w(ppcp_cbor_writer *w, const void *ctx)
{
    return ppcp_digest_encode(w, (const ppcp_digest *)ctx);
}

static ppcp_result digest_r(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    (void)ctx;
    return ppcp_digest_decode(r, (ppcp_digest *)dst);
}

/* One-entity bodies: `candidate { candidate }`, `shot { shot }`, and so on. */
#define ENTITY_CODEC(name, type, enc, dec)                                     \
    static ppcp_result name##_w(ppcp_cbor_writer *w, const void *ctx)          \
    { return enc(w, (const type *)ctx); }                                      \
    static ppcp_result name##_r(ppcp_cbor_reader *r, void *dst, void *ctx)     \
    { (void)ctx; return dec(r, (type *)dst); }

ENTITY_CODEC(calib, ppcp_calibration, ppcp_calibration_encode, ppcp_calibration_decode)
ENTITY_CODEC(disc, ppcp_clock_discontinuity, ppcp_clock_discontinuity_encode,
             ppcp_clock_discontinuity_decode)
ENTITY_CODEC(ctxc, ppcp_context_change, ppcp_context_change_encode, ppcp_context_change_decode)
ENTITY_CODEC(strm, ppcp_stream, ppcp_stream_encode, ppcp_stream_decode)
ENTITY_CODEC(cand, ppcp_candidate, ppcp_candidate_encode, ppcp_candidate_decode)
ENTITY_CODEC(shot, ppcp_shot, ppcp_shot_encode, ppcp_shot_decode)
ENTITY_CODEC(rdy, ppcp_readiness, ppcp_readiness_encode, ppcp_readiness_decode)
ENTITY_CODEC(slink, ppcp_shot_link, ppcp_shot_link_encode, ppcp_shot_link_decode)
ENTITY_CODEC(sslink, ppcp_session_link, ppcp_session_link_encode, ppcp_session_link_decode)
ENTITY_CODEC(annot, ppcp_annotation, ppcp_annotation_encode, ppcp_annotation_decode)

static ppcp_result capture_w(ppcp_cbor_writer *w, const void *ctx)
{
    return ppcp_capture_encode(w, (const ppcp_capture *)ctx);
}

static ppcp_result capture_r(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    return ppcp_capture_decode_arena(r, (ppcp_arena *)ctx, (ppcp_capture *)dst);
}

static ppcp_result af_w(ppcp_cbor_writer *w, const void *ctx)
{
    return ppcp_achieved_frames_encode(w, (const ppcp_achieved_frames *)ctx);
}

static ppcp_result af_r(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    return ppcp_achieved_frames_decode(r, (ppcp_arena *)ctx, (ppcp_achieved_frames *)dst);
}

/* `relation_update { relations }` — a bounded list, unlike a Peer's. */
typedef struct rel_list_c { const ppcp_timebase_relation *v; size_t n; } rel_list_c;

static ppcp_result rel_list_w(ppcp_cbor_writer *w, const void *ctx)
{
    const rel_list_c *l = (const rel_list_c *)ctx;
    return ppcp_rec_write_array(w, l->v, sizeof(ppcp_timebase_relation), l->n,
                                ppcp_elem_write_relation);
}

static ppcp_result rel_list_r(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_body_relation_update *b = (ppcp_body_relation_update *)dst;
    (void)ctx;
    return ppcp_rec_read_array(r, b->relations, sizeof(ppcp_timebase_relation),
                               PPCP_MAX_RELATIONS, &b->relation_count,
                               ppcp_sub_read_relation, NULL);
}

/* --------------------------------------------------- declare_ack notes */

static ppcp_result note_elem_w(ppcp_cbor_writer *w, const void *elem)
{
    const ppcp_profile_note *n = (const ppcp_profile_note *)elem;
    ppcp_wfield f[4];
    size_t      k = 0;
    f[k++] = ppcp_wf_id("source_id", &n->source_id);
    f[k++] = ppcp_wf_id("profile_id", &n->profile_id);
    f[k++] = ppcp_wf_enum("verdict", verdict_map, (int)n->verdict);
    if (n->has_reason)
        f[k++] = ppcp_wf_id("reason", &n->reason);
    return ppcp_rec_write(w, f, k);
}

static ppcp_result note_elem_r(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_profile_note *n = (ppcp_profile_note *)dst;
    ppcp_rfield        f[4];
    bool               s_src = false, s_prof = false, s_verd = false;
    int                verd = 0;
    ppcp_result        rc;
    (void)ctx;
    f[0] = ppcp_rf("source_id", PPCP_F_ID, &n->source_id, &s_src);
    f[1] = ppcp_rf("profile_id", PPCP_F_ID, &n->profile_id, &s_prof);
    f[2] = ppcp_rf_enum("verdict", verdict_map, &verd, &s_verd);
    f[3] = ppcp_rf("reason", PPCP_F_ID, &n->reason, &n->has_reason);
    rc = ppcp_rec_read(r, f, 4);
    if (rc != PPCP_OK) return rc;
    if (!s_src || !s_prof || !s_verd) return PPCP_ERR_MALFORMED;
    n->verdict = (ppcp_verdict)verd;
    return PPCP_OK;
}

static ppcp_result notes_w(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_body_declare_ack *b = (const ppcp_body_declare_ack *)ctx;
    return ppcp_rec_write_array(w, b->notes, sizeof(ppcp_profile_note), b->note_count,
                                note_elem_w);
}

static ppcp_result notes_r(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_body_declare_ack *b = (ppcp_body_declare_ack *)dst;
    (void)ctx;
    return ppcp_rec_read_array(r, b->notes, sizeof(ppcp_profile_note), PPCP_MAX_NOTES,
                               &b->note_count, note_elem_r, NULL);
}

/* ------------------------------------------- session_resume pending list */

static ppcp_result pending_elem_w(ppcp_cbor_writer *w, const void *elem)
{
    const ppcp_pending_capture *p = (const ppcp_pending_capture *)elem;
    ppcp_wfield f[4];
    size_t      k = 0;
    f[k++] = ppcp_wf_id("capture_id", &p->capture_id);
    f[k++] = ppcp_wf_uint("bytes", p->bytes);
    if (p->digest.present)
        f[k++] = ppcp_wf_sub("digest", digest_w, &p->digest);
    /* 8.3d / 4.3: resumption restarts from the chunk AFTER the last
     * acknowledged index, so a peer that has acked nothing omits the field
     * rather than sending a zero that means "chunk 0 arrived". */
    if (p->has_acked_index)
        f[k++] = ppcp_wf_uint("acked_index", p->acked_index);
    return ppcp_rec_write(w, f, k);
}

static ppcp_result pending_elem_r(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_pending_capture *p = (ppcp_pending_capture *)dst;
    ppcp_rfield           f[4];
    bool                  s_id = false;
    uint64_t              idx = 0;
    ppcp_result           rc;
    (void)ctx;
    f[0] = ppcp_rf("capture_id", PPCP_F_ID, &p->capture_id, &s_id);
    f[1] = ppcp_rf("bytes", PPCP_F_UINT, &p->bytes, NULL);
    f[2] = ppcp_rf_sub("digest", digest_r, &p->digest, NULL, NULL);
    f[3] = ppcp_rf("acked_index", PPCP_F_UINT, &idx, &p->has_acked_index);
    rc = ppcp_rec_read(r, f, 4);
    if (rc != PPCP_OK) return rc;
    if (!s_id) return PPCP_ERR_MALFORMED;
    if (p->has_acked_index) {
        if (idx > 0xFFFFFFFFu) return PPCP_ERR_MALFORMED;
        p->acked_index = (uint32_t)idx;
    }
    return PPCP_OK;
}

static ppcp_result pending_w(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_body_session_resume *b = (const ppcp_body_session_resume *)ctx;
    return ppcp_rec_write_array(w, b->pending, sizeof(ppcp_pending_capture),
                                b->pending_count, pending_elem_w);
}

static ppcp_result pending_r(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_body_session_resume *b = (ppcp_body_session_resume *)dst;
    (void)ctx;
    return ppcp_rec_read_array(r, b->pending, sizeof(ppcp_pending_capture), PPCP_MAX_PENDING,
                               &b->pending_count, pending_elem_r, NULL);
}

/* -------------------------------------------------- session_manifest list */

static ppcp_result manifest_elem_w(ppcp_cbor_writer *w, const void *elem)
{
    const ppcp_manifest_entry *e = (const ppcp_manifest_entry *)elem;
    ppcp_wfield f[4];
    size_t      k = 0;
    f[k++] = ppcp_wf_id("capture_id", &e->capture_id);
    f[k++] = ppcp_wf_id("stream_id", &e->stream_id);
    f[k++] = ppcp_wf_uint("bytes", e->bytes);
    /* 8.5c / I34: `digest` is a CONTENT check where present, never the
     * identifier — an `absent` Capture has no payload and therefore no hash,
     * and a `complete` + `pending` Capture may reach a bundle before its digest
     * is computed. */
    if (e->digest.present)
        f[k++] = ppcp_wf_sub("digest", digest_w, &e->digest);
    return ppcp_rec_write(w, f, k);
}

static ppcp_result manifest_elem_r(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_manifest_entry *e = (ppcp_manifest_entry *)dst;
    ppcp_rfield          f[4];
    bool                 s_cid = false, s_sid = false;
    ppcp_result          rc;
    (void)ctx;
    f[0] = ppcp_rf("capture_id", PPCP_F_ID, &e->capture_id, &s_cid);
    f[1] = ppcp_rf("stream_id", PPCP_F_ID, &e->stream_id, &s_sid);
    f[2] = ppcp_rf("bytes", PPCP_F_UINT, &e->bytes, NULL);
    f[3] = ppcp_rf_sub("digest", digest_r, &e->digest, NULL, NULL);
    rc = ppcp_rec_read(r, f, 4);
    if (rc != PPCP_OK) return rc;
    if (!s_cid || !s_sid) return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

static ppcp_result manifest_caps_w(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_body_session_manifest *b = (const ppcp_body_session_manifest *)ctx;
    return ppcp_rec_write_array(w, b->captures, sizeof(ppcp_manifest_entry),
                                b->capture_count, manifest_elem_w);
}

static ppcp_result manifest_caps_r(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_body_session_manifest *b = (ppcp_body_session_manifest *)dst;
    (void)ctx;
    return ppcp_rec_read_array(r, b->captures, sizeof(ppcp_manifest_entry), PPCP_MAX_MANIFEST,
                               &b->capture_count, manifest_elem_r, NULL);
}

static ppcp_result counts_w(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_body_session_manifest *b = (const ppcp_body_session_manifest *)ctx;
    ppcp_wfield f[3];
    f[0] = ppcp_wf_uint("shots", b->count_shots);
    f[1] = ppcp_wf_uint("candidates", b->count_candidates);
    f[2] = ppcp_wf_uint("captures", b->count_captures);
    return ppcp_rec_write(w, f, 3);
}

static ppcp_result counts_r(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_body_session_manifest *b = (ppcp_body_session_manifest *)dst;
    ppcp_rfield f[3];
    (void)ctx;
    f[0] = ppcp_rf("shots", PPCP_F_UINT, &b->count_shots, NULL);
    f[1] = ppcp_rf("candidates", PPCP_F_UINT, &b->count_candidates, NULL);
    f[2] = ppcp_rf("captures", PPCP_F_UINT, &b->count_captures, NULL);
    return ppcp_rec_read(r, f, 3);
}

static ppcp_result have_digests_w(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_body_session_accept *b = (const ppcp_body_session_accept *)ctx;
    size_t i;
    if (ppcp_cbor_write_array(w, b->have_digest_count) != PPCP_OK)
        return ppcp_cbor_writer_status(w);
    for (i = 0; i < b->have_digest_count; i++) {
        ppcp_result rc = ppcp_digest_encode(w, &b->have_digests[i]);
        if (rc != PPCP_OK)
            return rc;
    }
    return PPCP_OK;
}

static ppcp_result have_digests_r(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_body_session_accept *b = (ppcp_body_session_accept *)dst;
    (void)ctx;
    return ppcp_rec_read_array(r, b->have_digests, sizeof(ppcp_digest),
                               PPCP_MAX_HAVE_DIGESTS, &b->have_digest_count,
                               digest_r, NULL);
}

/* -------------------------------------------------- capture_announce thumb */

typedef struct thumb_c { const ppcp_id *format; const uint8_t *p; size_t n; } thumb_c;

static ppcp_result thumb_w(ppcp_cbor_writer *w, const void *ctx)
{
    const thumb_c *t = (const thumb_c *)ctx;
    ppcp_wfield f[2];
    if (t->n > PPCP_THUMBNAIL_MAX)
        return PPCP_ERR_LIMIT;         /* MSG 8.1d / ENC §8: 64 KiB */
    f[0] = ppcp_wf_id("format", t->format);
    f[1] = ppcp_wf_bytes("bytes", t->p, t->n);
    return ppcp_rec_write(w, f, 2);
}

static ppcp_result thumb_r(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_body_capture_announce *b = (ppcp_body_capture_announce *)dst;
    ppcp_rfield                 f[2];
    ppcp_bytes_ref              by;
    bool                        s_fmt = false, s_by = false;
    ppcp_result                 rc;
    (void)ctx;
    memset(&by, 0, sizeof(by));
    f[0] = ppcp_rf("format", PPCP_F_ID, &b->thumbnail_format, &s_fmt);
    f[1] = ppcp_rf("bytes", PPCP_F_BYTES, &by, &s_by);
    rc = ppcp_rec_read(r, f, 2);
    if (rc != PPCP_OK) return rc;
    if (!s_fmt || !s_by) return PPCP_ERR_MALFORMED;
    if (by.len > PPCP_THUMBNAIL_MAX) return PPCP_ERR_LIMIT;
    b->thumbnail     = by.p;
    b->thumbnail_len = by.len;
    return PPCP_OK;
}

/* --------------------------------------------------------- error detail */

static ppcp_result detail_w(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_body_error *b = (const ppcp_body_error *)ctx;
    ppcp_wfield f[2];
    idlist_c    sup;
    size_t      k = 0;
    sup.v = b->detail_supported;
    sup.n = b->detail_supported_count;
    /* 10.1f: `unsupported_version` MUST carry the sender's full supported
     * range, so the receiving peer can tell its user which end is stale. */
    if (b->has_detail_supported)
        f[k++] = ppcp_wf_sub("supported", idlist_w, &sup);
    if (b->has_detail_reason)
        f[k++] = ppcp_wf_id("reason", &b->detail_reason);
    return ppcp_rec_write(w, f, k);
}

static ppcp_result detail_r(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_body_error *b = (ppcp_body_error *)dst;
    ppcp_rfield      f[2];
    idlist_d         sup;
    (void)ctx;
    sup.v = b->detail_supported;
    sup.cap = PPCP_MAX_VERSIONS;
    sup.n = &b->detail_supported_count;
    f[0] = ppcp_rf_sub("supported", idlist_r, &sup, NULL, &b->has_detail_supported);
    f[1] = ppcp_rf("reason", PPCP_F_ID, &b->detail_reason, &b->has_detail_reason);
    return ppcp_rec_read(r, f, 2);
}

/* ============================================================== decoding
 *
 * Every message decodes, whatever profiles the peer declares (I24, CT-S6
 * assertion 4).  The body is read by walking the SAME map the envelope
 * decoder already walked: the four reserved keys of ENC §5 are simply not in
 * the body's field table, so ppcp_rec_read skips them exactly as it skips a
 * key from a future MINOR version.
 */

typedef struct declare_ctx { ppcp_arena *arena; ppcp_peer_desc *peer; } declare_ctx;

static ppcp_result declare_peer_r(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    declare_ctx *c = (declare_ctx *)ctx;
    (void)dst;
    return ppcp_peer_head_decode(r, c->arena, c->peer);
}

static ppcp_result declare_tb_r(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    declare_ctx *c = (declare_ctx *)ctx;
    (void)dst;
    return ppcp_peer_timebases_read(r, c->arena, c->peer);
}

static ppcp_result declare_rel_r(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    declare_ctx *c = (declare_ctx *)ctx;
    (void)dst;
    return ppcp_peer_relations_read(r, c->arena, c->peer);
}

static ppcp_result declare_src_r(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    declare_ctx *c = (declare_ctx *)ctx;
    (void)dst;
    return ppcp_peer_sources_read(r, c->arena, c->peer);
}

static ppcp_result dec_connection(ppcp_cbor_reader *r, ppcp_arena *a, ppcp_msg *m)
{
    ppcp_rfield f[10];
    size_t      n = 0;
    ppcp_result rc;

    switch (m->type) {
    case PPCP_MT_LINK_BIND: {
        ppcp_body_link_bind *b = &m->body.link_bind;
        ppcp_bytes_ref       lid;
        uint64_t             ch = 0;
        bool                 s_lid = false, s_ch = false;
        memset(&lid, 0, sizeof(lid));
        f[n++] = ppcp_rf("link_id", PPCP_F_BYTES, &lid, &s_lid);
        f[n++] = ppcp_rf("channel", PPCP_F_UINT, &ch, &s_ch);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_lid || !s_ch) return PPCP_ERR_MALFORMED;
        /* ENC 2.1a: 16 bytes, fresh per link, from a CSPRNG.  The library never
         * generates them (RV 7.2a is the embedding's obligation) but it does
         * refuse a length that is not the one the clause names. */
        if (lid.len != PPCP_LINK_ID_BYTES) return PPCP_ERR_MALFORMED;
        if (ch > 254) return PPCP_ERR_MALFORMED;    /* ENC 2a: 255 is reserved */
        memcpy(b->link_id, lid.p, PPCP_LINK_ID_BYTES);
        b->channel = (uint8_t)ch;
        return PPCP_OK;
    }
    case PPCP_MT_HELLO: {
        ppcp_body_hello *b = &m->body.hello;
        idlist_d vers, prof, ext;
        bool s_vers = false, s_peer = false, s_role = false, s_prof = false;
        int  role = 0;
        vers.v = b->versions;   vers.cap = PPCP_MAX_VERSIONS;   vers.n = &b->version_count;
        prof.v = b->profiles;   prof.cap = PPCP_MAX_PROFILES;   prof.n = &b->profile_count;
        ext.v  = b->extensions; ext.cap  = PPCP_MAX_EXTENSIONS; ext.n  = &b->extension_count;
        f[n++] = ppcp_rf_sub("versions", idlist_r, &vers, NULL, &s_vers);
        f[n++] = ppcp_rf("peer_id", PPCP_F_ID, &b->peer_id, &s_peer);
        f[n++] = ppcp_rf_enum("role", ppcp_role_enum_map(), &role, &s_role);
        f[n++] = ppcp_rf_sub("profiles", idlist_r, &prof, NULL, &s_prof);
        f[n++] = ppcp_rf_sub("extensions", idlist_r, &ext, NULL, NULL);
        f[n++] = ppcp_rf_sub("product", ppcp_product_read, &b->product, NULL, NULL);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_vers || !s_peer || !s_role || !s_prof) return PPCP_ERR_MALFORMED;
        /* 3.1b: `versions` is ordered most-preferred first and contains at
         * least one entry. */
        if (b->version_count == 0) return PPCP_ERR_MALFORMED;
        b->role = (ppcp_role)role;
        return PPCP_OK;
    }
    case PPCP_MT_HELLO_ACCEPT: {
        ppcp_body_hello_accept *b = &m->body.hello_accept;
        idlist_d prof, ext;
        bool s_ver = false, s_min = false, s_peer = false, s_role = false, s_prof = false;
        int  role = 0;
        prof.v = b->profiles;   prof.cap = PPCP_MAX_PROFILES;   prof.n = &b->profile_count;
        ext.v  = b->extensions; ext.cap  = PPCP_MAX_EXTENSIONS; ext.n  = &b->extension_count;
        f[n++] = ppcp_rf("version", PPCP_F_ID, &b->version, &s_ver);
        f[n++] = ppcp_rf("min_version", PPCP_F_ID, &b->min_version, &s_min);
        f[n++] = ppcp_rf("peer_id", PPCP_F_ID, &b->peer_id, &s_peer);
        f[n++] = ppcp_rf_enum("role", ppcp_role_enum_map(), &role, &s_role);
        f[n++] = ppcp_rf_sub("profiles", idlist_r, &prof, NULL, &s_prof);
        f[n++] = ppcp_rf_sub("extensions", idlist_r, &ext, NULL, NULL);
        f[n++] = ppcp_rf_sub("product", ppcp_product_read, &b->product, NULL, NULL);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        /* 3.2d: `min_version` states the responder's support window.  A peer
         * whose own version is below it will be refused and is entitled to
         * know that before it is — so it is mandatory, not optional. */
        if (!s_ver || !s_min || !s_peer || !s_role || !s_prof) return PPCP_ERR_MALFORMED;
        b->role = (ppcp_role)role;
        return PPCP_OK;
    }
    case PPCP_MT_DECLARE: {
        ppcp_body_declare *b = &m->body.declare;
        declare_ctx        dc;
        bool               s_gen = false, s_peer = false;
        dc.arena = a;
        dc.peer  = &b->peer;
        f[n++] = ppcp_rf("generation", PPCP_F_UINT, &b->generation, &s_gen);
        f[n++] = ppcp_rf_sub("peer", declare_peer_r, NULL, &dc, &s_peer);
        f[n++] = ppcp_rf_sub("timebases", declare_tb_r, NULL, &dc, NULL);
        f[n++] = ppcp_rf_sub("relations", declare_rel_r, NULL, &dc, NULL);
        f[n++] = ppcp_rf_sub("sources", declare_src_r, NULL, &dc, NULL);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_gen || !s_peer) return PPCP_ERR_MALFORMED;
        if (b->generation == 0) return PPCP_ERR_MALFORMED;   /* 3.3: starts at 1 */
        /* The Peer is validated once assembled, which is where 3.3b's "every
         * timebase_id referenced by any Source appears in timebases" can
         * actually be checked. */
        if (ppcp_peer_desc_validate(&b->peer) != PPCP_OK) return PPCP_ERR_MALFORMED;
        return PPCP_OK;
    }
    case PPCP_MT_DECLARE_ACK: {
        ppcp_body_declare_ack *b = &m->body.declare_ack;
        bool s_gen = false, s_verd = false;
        int  verd = 0;
        f[n++] = ppcp_rf("generation", PPCP_F_UINT, &b->generation, &s_gen);
        f[n++] = ppcp_rf_enum("verdict", verdict_map, &verd, &s_verd);
        f[n++] = ppcp_rf("reason", PPCP_F_ID, &b->reason, &b->has_reason);
        f[n++] = ppcp_rf_sub("notes", notes_r, b, NULL, NULL);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_gen || !s_verd) return PPCP_ERR_MALFORMED;
        b->verdict = (ppcp_verdict)verd;
        /* 3.4a: a rejection carries a machine-readable `reason` — and does NOT
         * close the connection (7.2b).  3.4b: no threshold that drove the
         * rejection appears anywhere in this library (I14). */
        if (b->verdict == PPCP_VERDICT_REJECTED && !b->has_reason)
            return PPCP_ERR_MALFORMED;
        return PPCP_OK;
    }
    case PPCP_MT_RELATION_UPDATE:
        f[n++] = ppcp_rf_sub("relations", rel_list_r, &m->body.relation_update, NULL, NULL);
        return ppcp_rec_read(r, f, n);
    case PPCP_MT_CALIBRATION_UPDATE: {
        bool seen = false;
        f[n++] = ppcp_rf_sub("calibration", calib_r,
                             &m->body.calibration_update.calibration, NULL, &seen);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        return seen ? PPCP_OK : PPCP_ERR_MALFORMED;
    }
    case PPCP_MT_DISCONTINUITY: {
        bool seen = false;
        f[n++] = ppcp_rf_sub("discontinuity", disc_r,
                             &m->body.discontinuity.discontinuity, NULL, &seen);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        return seen ? PPCP_OK : PPCP_ERR_MALFORMED;
    }
    default:
        return PPCP_ERR_INVALID;
    }
}

static ppcp_result dec_session(ppcp_cbor_reader *r, ppcp_msg *m)
{
    ppcp_rfield f[10];
    size_t      n = 0;
    ppcp_result rc;

    switch (m->type) {
    case PPCP_MT_SESSION_OPEN: {
        ppcp_body_session_open *b = &m->body.session_open;
        bool     s_sid = false, s_tb = false, s_cw = false, s_ih = false;
        uint64_t hb = 0;
        f[n++] = ppcp_rf("session_id", PPCP_F_ID, &b->session_id, &s_sid);
        f[n++] = ppcp_rf("timebase_ref", PPCP_F_ID, &b->timebase_ref, &s_tb);
        f[n++] = ppcp_rf_sub("epoch", ppcp_session_epoch_read, &b->epoch, NULL, NULL);
        f[n++] = ppcp_rf("coincidence_window_ns", PPCP_F_INT, &b->coincidence_window_ns, &s_cw);
        f[n++] = ppcp_rf("issue_hold_ns", PPCP_F_INT, &b->issue_hold_ns, &s_ih);
        f[n++] = ppcp_rf("heartbeat_interval_ms", PPCP_F_UINT, &hb,
                         &b->has_heartbeat_interval);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_sid || !s_tb) return PPCP_ERR_MALFORMED;
        /* 4.1d / 5.10e: both or neither.  One without the other is malformed
         * whatever the roster says, because they are two halves of one
         * statement — a tolerance and a deadline (4.1c). */
        if (s_cw != s_ih) return PPCP_ERR_MALFORMED;
        b->has_arbitration = s_cw;
        if (b->has_heartbeat_interval) {
            if (hb == 0 || hb > 0xFFFFFFFFu) return PPCP_ERR_MALFORMED;
            b->heartbeat_interval_ms = (uint32_t)hb;
        }
        return PPCP_OK;
    }
    case PPCP_MT_SESSION_JOINED: {
        ppcp_body_session_joined *b = &m->body.session_joined;
        bool s_sid = false, s_pid = false, s_verd = false;
        int  verd = 0;
        f[n++] = ppcp_rf("session_id", PPCP_F_ID, &b->session_id, &s_sid);
        f[n++] = ppcp_rf("peer_id", PPCP_F_ID, &b->peer_id, &s_pid);
        f[n++] = ppcp_rf_enum("verdict", join_verdict_map, &verd, &s_verd);
        f[n++] = ppcp_rf("reason", PPCP_F_ID, &b->reason, &b->has_reason);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_sid || !s_pid || !s_verd) return PPCP_ERR_MALFORMED;
        b->verdict = (ppcp_join_verdict)verd;
        return PPCP_OK;
    }
    case PPCP_MT_SESSION_RESUME: {
        ppcp_body_session_resume *b = &m->body.session_resume;
        idlist_d minted;
        bool     s_sid = false, s_pid = false;
        minted.v = b->minted_shots; minted.cap = PPCP_MAX_MINTED_SHOTS;
        minted.n = &b->minted_shot_count;
        f[n++] = ppcp_rf("session_id", PPCP_F_ID, &b->session_id, &s_sid);
        f[n++] = ppcp_rf("peer_id", PPCP_F_ID, &b->peer_id, &s_pid);
        f[n++] = ppcp_rf_sub("minted_shots", idlist_r, &minted, NULL, NULL);
        f[n++] = ppcp_rf_sub("pending_captures", pending_r, b, NULL, NULL);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_sid || !s_pid) return PPCP_ERR_MALFORMED;
        return PPCP_OK;
    }
    case PPCP_MT_SESSION_STATE: {
        ppcp_body_session_state *b = &m->body.session_state;
        bool s_sid = false, s_state = false, s_comp = false;
        int  state = 0, comp = 0;
        f[n++] = ppcp_rf("session_id", PPCP_F_ID, &b->session_id, &s_sid);
        f[n++] = ppcp_rf_enum("state", ppcp_session_state_enum_map(), &state, &s_state);
        f[n++] = ppcp_rf_enum("completeness", ppcp_session_completeness_enum_map(),
                              &comp, &s_comp);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_sid || !s_state || !s_comp) return PPCP_ERR_MALFORMED;
        b->state        = (ppcp_session_state)state;
        b->completeness = (ppcp_completeness)comp;
        return PPCP_OK;
    }
    case PPCP_MT_CONTEXT_CHANGE: {
        bool seen = false;
        f[n++] = ppcp_rf_sub("context", ctxc_r, &m->body.context_change.context, NULL, &seen);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        return seen ? PPCP_OK : PPCP_ERR_MALFORMED;
    }
    case PPCP_MT_SESSION_CLOSE: {
        ppcp_body_session_close *b = &m->body.session_close;
        bool s_sid = false, s_reason = false;
        f[n++] = ppcp_rf("session_id", PPCP_F_ID, &b->session_id, &s_sid);
        f[n++] = ppcp_rf("reason", PPCP_F_ID, &b->reason, &s_reason);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_sid || !s_reason) return PPCP_ERR_MALFORMED;
        return PPCP_OK;
    }
    default:
        return PPCP_ERR_INVALID;
    }
}

static ppcp_result dec_stream(ppcp_cbor_reader *r, ppcp_msg *m)
{
    ppcp_rfield f[8];
    size_t      n = 0;
    ppcp_result rc;

    switch (m->type) {
    case PPCP_MT_STREAM_OPEN: {
        bool seen = false;
        f[n++] = ppcp_rf_sub("stream", strm_r, &m->body.stream_open.stream, NULL, &seen);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        return seen ? PPCP_OK : PPCP_ERR_MALFORMED;
    }
    case PPCP_MT_STREAM_OPEN_ACK: {
        ppcp_body_stream_open_ack *b = &m->body.stream_open_ack;
        bool s_sid = false, s_verd = false;
        int  verd = 0;
        f[n++] = ppcp_rf("stream_id", PPCP_F_ID, &b->stream_id, &s_sid);
        f[n++] = ppcp_rf_enum("verdict", stream_verdict_map, &verd, &s_verd);
        f[n++] = ppcp_rf("reason", PPCP_F_ID, &b->reason, &b->has_reason);
        f[n++] = ppcp_rf_sub("opened_at", ppcp_sub_read_instant, &b->opened_at, NULL,
                             &b->has_opened_at);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_sid || !s_verd) return PPCP_ERR_MALFORMED;
        b->verdict = (ppcp_stream_verdict)verd;
        return PPCP_OK;
    }
    case PPCP_MT_STREAM_CLOSE: {
        ppcp_body_stream_close *b = &m->body.stream_close;
        bool s_sid = false, s_at = false, s_reason = false;
        f[n++] = ppcp_rf("stream_id", PPCP_F_ID, &b->stream_id, &s_sid);
        f[n++] = ppcp_rf_sub("closed_at", ppcp_sub_read_instant, &b->closed_at, NULL, &s_at);
        f[n++] = ppcp_rf("reason", PPCP_F_ID, &b->reason, &s_reason);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        /* F-H4-2 — `closed_at` is tolerated absent.  See message.h: 5.1d lets a
         * consumer close a Stream whose timebase it cannot read, and refusing
         * the frame would make that close unexpressible rather than merely
         * unstamped. */
        b->has_closed_at = s_at;
        if (!s_sid || !s_reason) return PPCP_ERR_MALFORMED;
        return PPCP_OK;
    }
    case PPCP_MT_ARM:
    case PPCP_MT_DISARM: {
        ppcp_body_stream_ids *b = &m->body.arm;
        idlist_d ids;
        ids.v = b->stream_ids; ids.cap = PPCP_MAX_STREAM_IDS; ids.n = &b->stream_id_count;
        /* 5.2: an EMPTY list means every open capture Stream, so absence and
         * emptiness are the same thing here and neither is an error. */
        f[n++] = ppcp_rf_sub("stream_ids", idlist_r, &ids, NULL, NULL);
        return ppcp_rec_read(r, f, n);
    }
    case PPCP_MT_READINESS: {
        ppcp_body_readiness *b = &m->body.readiness;
        idlist_d ids;
        bool     seen = false;
        ids.v = b->stream_ids; ids.cap = PPCP_MAX_STREAM_IDS; ids.n = &b->stream_id_count;
        f[n++] = ppcp_rf_sub("readiness", rdy_r, &b->readiness, NULL, &seen);
        f[n++] = ppcp_rf_sub("stream_ids", idlist_r, &ids, NULL, NULL);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        return seen ? PPCP_OK : PPCP_ERR_MALFORMED;
    }
    case PPCP_MT_INTERRUPTION: {
        ppcp_body_interruption *b = &m->body.interruption;
        idlist_d ids;
        bool     s_kind = false, s_iv = false, s_rec = false;
        ids.v = b->stream_ids; ids.cap = PPCP_MAX_STREAM_IDS; ids.n = &b->stream_id_count;
        f[n++] = ppcp_rf("kind", PPCP_F_ID, &b->kind, &s_kind);
        f[n++] = ppcp_rf_sub("interval", ppcp_sub_read_interval, &b->interval, NULL, &s_iv);
        f[n++] = ppcp_rf("recovered", PPCP_F_BOOL, &b->recovered, &s_rec);
        f[n++] = ppcp_rf_sub("stream_ids", idlist_r, &ids, NULL, NULL);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        /* 5.3a: a platform interruption that cost capture is reported WITH the
         * interval it covered — the gap is additionally recorded on the
         * affected Captures (I11). */
        if (!s_kind || !s_iv || !s_rec) return PPCP_ERR_MALFORMED;
        return PPCP_OK;
    }
    case PPCP_MT_HEARTBEAT: {
        bool s_seq = false;
        f[n++] = ppcp_rf("seq", PPCP_F_UINT, &m->body.heartbeat.seq, &s_seq);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        return s_seq ? PPCP_OK : PPCP_ERR_MALFORMED;
    }
    case PPCP_MT_HEARTBEAT_ACK: {
        ppcp_body_heartbeat_ack *b = &m->body.heartbeat_ack;
        bool     s_seq = false, s_thermal = false, s_store = false;
        int      thermal = 0;
        uint64_t batt = 0;
        f[n++] = ppcp_rf("seq", PPCP_F_UINT, &b->seq, &s_seq);
        f[n++] = ppcp_rf_enum("thermal", ppcp_thermal_enum_map(), &thermal, &s_thermal);
        f[n++] = ppcp_rf("vendor_thermal_label", PPCP_F_ID, &b->vendor_thermal_label,
                         &b->has_vendor_label);
        f[n++] = ppcp_rf("storage_free_bytes", PPCP_F_UINT, &b->storage_free_bytes, &s_store);
        f[n++] = ppcp_rf("battery_pct", PPCP_F_UINT, &batt, &b->has_battery_pct);
        f[n++] = ppcp_rf("charging", PPCP_F_BOOL, &b->charging, &b->has_charging);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        /* 5.4b: thermal is a FIRST-CLASS field, so a host reports degradation
         * rather than silently accepting worse data. */
        if (!s_seq || !s_thermal || !s_store) return PPCP_ERR_MALFORMED;
        b->thermal = (ppcp_thermal_level)thermal;
        if (b->has_battery_pct) {
            if (batt > 100) return PPCP_ERR_MALFORMED;
            b->battery_pct = (uint32_t)batt;
        }
        return PPCP_OK;
    }
    default:
        return PPCP_ERR_INVALID;
    }
}

static ppcp_result dec_sync_detect(ppcp_cbor_reader *r, ppcp_msg *m)
{
    ppcp_rfield f[8];
    size_t      n = 0;
    ppcp_result rc;

    switch (m->type) {
    case PPCP_MT_SYNC_PROBE: {
        ppcp_body_sync_probe *b = &m->body.sync_probe;
        bool s_seq = false, s_tb = false, s_t1 = false;
        f[n++] = ppcp_rf("probe_seq", PPCP_F_UINT, &b->probe_seq, &s_seq);
        f[n++] = ppcp_rf("timebase_id", PPCP_F_ID, &b->timebase_id, &s_tb);
        f[n++] = ppcp_rf_sub("t1", ppcp_sub_read_instant, &b->t1, NULL, &s_t1);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_seq || !s_tb || !s_t1) return PPCP_ERR_MALFORMED;
        return PPCP_OK;
    }
    case PPCP_MT_SYNC_REPLY: {
        ppcp_body_sync_reply *b = &m->body.sync_reply;
        bool s_seq = false, s1 = false, s2 = false, s3 = false;
        f[n++] = ppcp_rf("probe_seq", PPCP_F_UINT, &b->probe_seq, &s_seq);
        f[n++] = ppcp_rf_sub("t1", ppcp_sub_read_instant, &b->t1, NULL, &s1);
        f[n++] = ppcp_rf_sub("t2", ppcp_sub_read_instant, &b->t2, NULL, &s2);
        f[n++] = ppcp_rf_sub("t3", ppcp_sub_read_instant, &b->t3, NULL, &s3);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_seq || !s1 || !s2 || !s3) return PPCP_ERR_MALFORMED;
        /* 6.1b: t2 and t3 are in the SAME responder timebase.  t4 is recorded
         * locally by the prober and never transmitted. */
        if (!ppcp_id_equal(&b->t2.tb, &b->t3.tb)) return PPCP_ERR_MALFORMED;
        return PPCP_OK;
    }
    case PPCP_MT_SYNC_RESIDUAL: {
        ppcp_body_sync_residual *b = &m->body.sync_residual;
        bool s_shot = false, s_tb = false, s_res = false, s_basis = false;
        f[n++] = ppcp_rf("shot_id", PPCP_F_ID, &b->shot_id, &s_shot);
        f[n++] = ppcp_rf("timebase_id", PPCP_F_ID, &b->timebase_id, &s_tb);
        f[n++] = ppcp_rf("residual_ns", PPCP_F_INT, &b->residual_ns, &s_res);
        f[n++] = ppcp_rf("basis", PPCP_F_ID, &b->basis, &s_basis);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_shot || !s_tb || !s_res || !s_basis) return PPCP_ERR_MALFORMED;
        return PPCP_OK;
    }
    case PPCP_MT_CANDIDATE: {
        bool seen = false;
        f[n++] = ppcp_rf_sub("candidate", cand_r, &m->body.candidate.candidate, NULL, &seen);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        return seen ? PPCP_OK : PPCP_ERR_MALFORMED;
    }
    case PPCP_MT_SHOT: {
        bool seen = false;
        f[n++] = ppcp_rf_sub("shot", shot_r, &m->body.shot.shot, NULL, &seen);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        return seen ? PPCP_OK : PPCP_ERR_MALFORMED;
    }
    case PPCP_MT_CAPTURE_REQUEST: {
        ppcp_body_capture_request *b = &m->body.capture_request;
        idlist_d ids;
        bool     s_shot = false, s_t0 = false, s_pre = false, s_post = false;
        ids.v = b->stream_ids; ids.cap = PPCP_MAX_STREAM_IDS; ids.n = &b->stream_id_count;
        f[n++] = ppcp_rf("shot_id", PPCP_F_ID, &b->shot_id, &s_shot);
        f[n++] = ppcp_rf_sub("t0", ppcp_sub_read_instant, &b->t0, NULL, &s_t0);
        f[n++] = ppcp_rf_sub("stream_ids", idlist_r, &ids, NULL, NULL);
        f[n++] = ppcp_rf("pre_ns", PPCP_F_INT, &b->pre_ns, &s_pre);
        f[n++] = ppcp_rf("post_ns", PPCP_F_INT, &b->post_ns, &s_post);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_shot || !s_t0 || !s_pre || !s_post) return PPCP_ERR_MALFORMED;
        return PPCP_OK;
    }
    case PPCP_MT_ANNOTATION: {
        bool seen = false;
        f[n++] = ppcp_rf_sub("annotation", annot_r, &m->body.annotation.annotation,
                             NULL, &seen);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        return seen ? PPCP_OK : PPCP_ERR_MALFORMED;
    }
    case PPCP_MT_SHOT_LINK: {
        bool seen = false;
        f[n++] = ppcp_rf_sub("link", slink_r, &m->body.shot_link.link, NULL, &seen);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        return seen ? PPCP_OK : PPCP_ERR_MALFORMED;
    }
    case PPCP_MT_SESSION_LINK: {
        bool seen = false;
        f[n++] = ppcp_rf_sub("link", sslink_r, &m->body.session_link.link, NULL, &seen);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        return seen ? PPCP_OK : PPCP_ERR_MALFORMED;
    }
    default:
        return PPCP_ERR_INVALID;
    }
}

static ppcp_result dec_capture(ppcp_cbor_reader *r, ppcp_arena *a, ppcp_msg *m)
{
    ppcp_rfield f[8];
    size_t      n = 0;
    ppcp_result rc;

    switch (m->type) {
    case PPCP_MT_CAPTURE_ANNOUNCE: {
        ppcp_body_capture_announce *b = &m->body.capture_announce;
        bool seen = false;
        f[n++] = ppcp_rf_sub("capture", capture_r, &b->capture, a, &seen);
        f[n++] = ppcp_rf_sub("thumbnail", thumb_r, b, NULL, &b->has_thumbnail);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!seen) return PPCP_ERR_MALFORMED;
        /* 8.1b / I30: there is no `achieved_frames` key in this message, so a
         * peer that sent one has it skipped as an unknown key rather than
         * accepted — the series travel with the payload they describe. */
        return PPCP_OK;
    }
    case PPCP_MT_CAPTURE_UPDATE: {
        ppcp_body_capture_update *b = &m->body.capture_update;
        bool s_id = false;
        int  comp = 0, xfer = 0;
        f[n++] = ppcp_rf("capture_id", PPCP_F_ID, &b->capture_id, &s_id);
        f[n++] = ppcp_rf_enum("completeness", ppcp_capture_completeness_enum_map(),
                              &comp, &b->has_completeness);
        f[n++] = ppcp_rf_enum("transfer", ppcp_transfer_enum_map(), &xfer, &b->has_transfer);
        f[n++] = ppcp_rf_sub("digest", digest_r, &b->digest, NULL, NULL);
        f[n++] = ppcp_rf_sub("achieved_frames", af_r, &b->achieved_frames, a,
                             &b->has_achieved_frames);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_id) return PPCP_ERR_MALFORMED;
        b->completeness = (ppcp_completeness)comp;
        b->transfer     = (ppcp_transfer_state)xfer;
        return PPCP_OK;
    }
    case PPCP_MT_CAPTURE_COMMITTED: {
        ppcp_body_capture_committed *b = &m->body.capture_committed;
        bool s_id = false, s_dig = false;
        f[n++] = ppcp_rf("capture_id", PPCP_F_ID, &b->capture_id, &s_id);
        f[n++] = ppcp_rf_sub("digest", digest_r, &b->digest, NULL, &s_dig);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_id || !s_dig) return PPCP_ERR_MALFORMED;
        return PPCP_OK;
    }
    case PPCP_MT_PAYLOAD_BEGIN: {
        ppcp_body_payload_begin *b = &m->body.payload_begin;
        bool     s_id = false, s_bytes = false, s_dig = false, s_chunk = false;
        uint64_t chunk = 0;
        f[n++] = ppcp_rf("capture_id", PPCP_F_ID, &b->capture_id, &s_id);
        f[n++] = ppcp_rf("bytes", PPCP_F_UINT, &b->bytes, &s_bytes);
        f[n++] = ppcp_rf_sub("digest", digest_r, &b->digest, NULL, &s_dig);
        f[n++] = ppcp_rf("chunk_bytes", PPCP_F_UINT, &chunk, &s_chunk);
        f[n++] = ppcp_rf_sub("achieved_frames", af_r, &b->achieved_frames, a,
                             &b->has_achieved_frames);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        /* 8.1e: `Capture.digest` MAY be absent from the announce and MUST be
         * present by `payload_begin`. */
        if (!s_id || !s_bytes || !s_dig || !s_chunk) return PPCP_ERR_MALFORMED;
        if (chunk == 0 || chunk > PPCP_LIMIT_CHUNK_BYTES) return PPCP_ERR_LIMIT; /* ENC 6f */
        b->chunk_bytes = (uint32_t)chunk;
        return PPCP_OK;
    }
    case PPCP_MT_PAYLOAD_CHUNK: {
        ppcp_body_payload_chunk *b = &m->body.payload_chunk;
        ppcp_bytes_ref data;
        bool     s_id = false, s_idx = false, s_off = false, s_data = false, s_dig = false;
        uint64_t idx = 0;
        memset(&data, 0, sizeof(data));
        f[n++] = ppcp_rf("capture_id", PPCP_F_ID, &b->capture_id, &s_id);
        f[n++] = ppcp_rf("index", PPCP_F_UINT, &idx, &s_idx);
        f[n++] = ppcp_rf("offset", PPCP_F_UINT, &b->offset, &s_off);
        f[n++] = ppcp_rf("data", PPCP_F_BYTES, &data, &s_data);
        f[n++] = ppcp_rf_sub("digest", digest_r, &b->digest, NULL, &s_dig);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_id || !s_idx || !s_off || !s_data || !s_dig) return PPCP_ERR_MALFORMED;
        if (idx > 0xFFFFFFFFu) return PPCP_ERR_MALFORMED;
        if (data.len > PPCP_LIMIT_CHUNK_BYTES) return PPCP_ERR_LIMIT;
        b->index    = (uint32_t)idx;
        b->data     = data.p;      /* zero-copy: points into the frame buffer */
        b->data_len = data.len;
        return PPCP_OK;
    }
    case PPCP_MT_PAYLOAD_ACK: {
        ppcp_body_payload_ack *b = &m->body.payload_ack;
        bool     s_id = false, s_idx = false;
        uint64_t idx = 0;
        f[n++] = ppcp_rf("capture_id", PPCP_F_ID, &b->capture_id, &s_id);
        f[n++] = ppcp_rf("index", PPCP_F_UINT, &idx, &s_idx);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_id || !s_idx || idx > 0xFFFFFFFFu) return PPCP_ERR_MALFORMED;
        b->index = (uint32_t)idx;
        return PPCP_OK;
    }
    case PPCP_MT_PAYLOAD_END: {
        ppcp_body_payload_end *b = &m->body.payload_end;
        bool s_id = false, s_dig = false;
        f[n++] = ppcp_rf("capture_id", PPCP_F_ID, &b->capture_id, &s_id);
        f[n++] = ppcp_rf_sub("digest", digest_r, &b->digest, NULL, &s_dig);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_id || !s_dig) return PPCP_ERR_MALFORMED;
        return PPCP_OK;
    }
    case PPCP_MT_PAYLOAD_ABORT: {
        ppcp_body_payload_abort *b = &m->body.payload_abort;
        bool s_id = false, s_reason = false;
        f[n++] = ppcp_rf("capture_id", PPCP_F_ID, &b->capture_id, &s_id);
        f[n++] = ppcp_rf("reason", PPCP_F_ID, &b->reason, &s_reason);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_id || !s_reason) return PPCP_ERR_MALFORMED;
        return PPCP_OK;
    }
    case PPCP_MT_PAYLOAD_RESUME: {
        ppcp_body_payload_resume *b = &m->body.payload_resume;
        bool     s_id = false, s_idx = false;
        uint64_t idx = 0;
        f[n++] = ppcp_rf("capture_id", PPCP_F_ID, &b->capture_id, &s_id);
        f[n++] = ppcp_rf("from_index", PPCP_F_UINT, &idx, &s_idx);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_id || !s_idx || idx > 0xFFFFFFFFu) return PPCP_ERR_MALFORMED;
        b->from_index = (uint32_t)idx;
        return PPCP_OK;
    }
    default:
        return PPCP_ERR_INVALID;
    }
}

static ppcp_result dec_offline(ppcp_cbor_reader *r, ppcp_msg *m)
{
    ppcp_rfield f[8];
    size_t      n = 0;
    ppcp_result rc;

    switch (m->type) {
    case PPCP_MT_SESSION_OFFER: {
        ppcp_body_session_offer *b = &m->body.session_offer;
        bool s_sid = false, s_mint = false, s_comp = false;
        int  comp = 0;
        f[n++] = ppcp_rf("session_id", PPCP_F_ID, &b->session_id, &s_sid);
        f[n++] = ppcp_rf("minting_peer_id", PPCP_F_ID, &b->minting_peer_id, &s_mint);
        f[n++] = ppcp_rf_sub("epoch", ppcp_session_epoch_read, &b->epoch, NULL, NULL);
        f[n++] = ppcp_rf_enum("completeness", ppcp_session_completeness_enum_map(),
                              &comp, &s_comp);
        f[n++] = ppcp_rf("bytes_estimate", PPCP_F_UINT, &b->bytes_estimate,
                         &b->has_bytes_estimate);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        /* 9.1a / 8.5c: session identity is `session_id` PLUS `minting_peer_id`,
         * which is why the second is mandatory and not a convenience (I34). */
        if (!s_sid || !s_mint || !s_comp) return PPCP_ERR_MALFORMED;
        b->completeness = (ppcp_completeness)comp;
        return PPCP_OK;
    }
    case PPCP_MT_SESSION_ACCEPT: {
        ppcp_body_session_accept *b = &m->body.session_accept;
        bool s_sid = false, s_verd = false;
        int  verd = 0;
        f[n++] = ppcp_rf("session_id", PPCP_F_ID, &b->session_id, &s_sid);
        f[n++] = ppcp_rf_enum("verdict", offer_verdict_map, &verd, &s_verd);
        f[n++] = ppcp_rf("reason", PPCP_F_ID, &b->reason, &b->has_reason);
        f[n++] = ppcp_rf_sub("have_digests", have_digests_r, b, NULL, NULL);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_sid || !s_verd) return PPCP_ERR_MALFORMED;
        b->verdict = (ppcp_offer_verdict)verd;
        return PPCP_OK;
    }
    case PPCP_MT_SESSION_MANIFEST: {
        ppcp_body_session_manifest *b = &m->body.session_manifest;
        idlist_d streams;
        bool     s_sid = false, s_comp = false;
        int      comp = 0;
        streams.v = b->streams; streams.cap = PPCP_MAX_STREAM_IDS;
        streams.n = &b->stream_count;
        f[n++] = ppcp_rf("session_id", PPCP_F_ID, &b->session_id, &s_sid);
        f[n++] = ppcp_rf_sub("streams", idlist_r, &streams, NULL, NULL);
        f[n++] = ppcp_rf_sub("captures", manifest_caps_r, b, NULL, NULL);
        f[n++] = ppcp_rf_enum("completeness", ppcp_session_completeness_enum_map(),
                              &comp, &s_comp);
        f[n++] = ppcp_rf_sub("counts", counts_r, b, NULL, NULL);
        rc = ppcp_rec_read(r, f, n);
        if (rc != PPCP_OK) return rc;
        if (!s_sid || !s_comp) return PPCP_ERR_MALFORMED;
        b->completeness = (ppcp_completeness)comp;
        return PPCP_OK;
    }
    case PPCP_MT_ERROR: {
        ppcp_body_error *b = &m->body.error;
        ppcp_text_ref    msg;
        bool             s_code = false;
        ppcp_result      rc2;
        memset(&msg, 0, sizeof(msg));
        f[n++] = ppcp_rf("code", PPCP_F_ID, &b->code, &s_code);
        f[n++] = ppcp_rf("message", PPCP_F_TEXT, &msg, NULL);
        f[n++] = ppcp_rf("in_reply_to", PPCP_F_UINT, &b->in_reply_to, &b->has_in_reply_to);
        f[n++] = ppcp_rf_sub("detail", detail_r, b, NULL, NULL);
        rc2 = ppcp_rec_read(r, f, n);
        if (rc2 != PPCP_OK) return rc2;
        if (!s_code) return PPCP_ERR_MALFORMED;
        if (msg.len > PPCP_ERROR_MESSAGE_MAX) return PPCP_ERR_LIMIT;
        if (msg.len > 0) memcpy(b->message, msg.p, msg.len);
        b->message[msg.len] = '\0';
        b->message_len      = msg.len;
        /* 10.1f: `unsupported_version` MUST carry `detail.supported`.  A
         * receiver that let it through would be told only that something
         * failed, which is exactly what the clause exists to prevent. */
        if (ppcp_cbor_key_is(b->code.v, b->code.len, PPCP_ERRCODE_UNSUPPORTED_VERSION) &&
            (!b->has_detail_supported || b->detail_supported_count == 0))
            return PPCP_ERR_MALFORMED;
        return PPCP_OK;
    }
    default:
        return PPCP_ERR_INVALID;
    }
}

ppcp_result ppcp_msg_decode(const uint8_t *payload, size_t len, ppcp_cbor_limits lim,
                            ppcp_arena *arena, ppcp_msg *out)
{
    ppcp_cbor_reader r;
    ppcp_msg_info    info;
    uint32_t         pairs = 0;
    ppcp_result      rc;

    if (payload == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    rc = ppcp_envelope_decode(payload, len, lim, &out->env, &pairs);
    if (rc != PPCP_OK)
        return rc;
    if (ppcp_id_set(&out->type_name, out->env.type, out->env.type_len) != PPCP_OK)
        return PPCP_ERR_MALFORMED;

    rc = ppcp_msg_lookup(out->env.type, out->env.type_len, &info);
    if (rc == PPCP_ERR_NOT_FOUND) {
        /* MSG 1b / I13: an unknown message type is not fatal, does not close
         * the transport, and is IGNORED where it is an event.  The envelope
         * decoded, so a peer can still answer `error`/`malformed` with a
         * `reply_to` if it wants to. */
        out->type = PPCP_MT_UNKNOWN;
        return PPCP_OK;
    }
    if (rc != PPCP_OK)
        return rc;
    out->type = info.id;

    ppcp_cbor_reader_init(&r, payload, len, lim);
    switch (info.id) {
    case PPCP_MT_LINK_BIND: case PPCP_MT_HELLO: case PPCP_MT_HELLO_ACCEPT:
    case PPCP_MT_DECLARE: case PPCP_MT_DECLARE_ACK: case PPCP_MT_RELATION_UPDATE:
    case PPCP_MT_CALIBRATION_UPDATE: case PPCP_MT_DISCONTINUITY:
        return dec_connection(&r, arena, out);
    case PPCP_MT_SESSION_OPEN: case PPCP_MT_SESSION_JOINED: case PPCP_MT_SESSION_RESUME:
    case PPCP_MT_SESSION_STATE: case PPCP_MT_CONTEXT_CHANGE: case PPCP_MT_SESSION_CLOSE:
        return dec_session(&r, out);
    case PPCP_MT_STREAM_OPEN: case PPCP_MT_STREAM_OPEN_ACK: case PPCP_MT_STREAM_CLOSE:
    case PPCP_MT_ARM: case PPCP_MT_DISARM: case PPCP_MT_READINESS:
    case PPCP_MT_INTERRUPTION: case PPCP_MT_HEARTBEAT: case PPCP_MT_HEARTBEAT_ACK:
        return dec_stream(&r, out);
    case PPCP_MT_SYNC_PROBE: case PPCP_MT_SYNC_REPLY: case PPCP_MT_SYNC_RESIDUAL:
    case PPCP_MT_CANDIDATE: case PPCP_MT_SHOT: case PPCP_MT_CAPTURE_REQUEST:
    case PPCP_MT_ANNOTATION: case PPCP_MT_SHOT_LINK: case PPCP_MT_SESSION_LINK:
        return dec_sync_detect(&r, out);
    case PPCP_MT_CAPTURE_ANNOUNCE: case PPCP_MT_CAPTURE_UPDATE:
    case PPCP_MT_CAPTURE_COMMITTED: case PPCP_MT_PAYLOAD_BEGIN:
    case PPCP_MT_PAYLOAD_CHUNK: case PPCP_MT_PAYLOAD_ACK: case PPCP_MT_PAYLOAD_END:
    case PPCP_MT_PAYLOAD_ABORT: case PPCP_MT_PAYLOAD_RESUME:
        return dec_capture(&r, arena, out);
    case PPCP_MT_SESSION_OFFER: case PPCP_MT_SESSION_ACCEPT:
    case PPCP_MT_SESSION_MANIFEST: case PPCP_MT_ERROR:
        return dec_offline(&r, out);
    default:
        return PPCP_ERR_INVALID;
    }
}


/* ============================================================== encoding
 *
 * Every body is described as a table of ppcp_wfield, which ppcp_rec_write_body
 * sorts into RFC 8949 order and merges with the envelope's four reserved keys
 * (ENC §5, 4e).  Nothing here chooses a key order by hand.
 */

#define PPCP_MSG_MAX_FIELDS 10

typedef struct msg_wctx {
    ppcp_wfield f[PPCP_MSG_MAX_FIELDS];
    size_t      n;
} msg_wctx;

static ppcp_result msg_body_write(ppcp_cbor_writer *w, ppcp_envelope_writer *ew, void *ctx)
{
    msg_wctx *c = (msg_wctx *)ctx;
    return ppcp_rec_write_body(w, ew, c->f, c->n);
}

/* Scratch for the list and sub-object contexts a ppcp_wfield holds by
 * pointer.  It lives for the duration of one encode, on the caller's stack. */
typedef struct enc_scratch {
    idlist_c   versions, profiles, extensions, stream_ids, minted, streams, supported;
    rel_list_c relations;
    thumb_c    thumb;
} enc_scratch;

static ppcp_result peer_head_w(ppcp_cbor_writer *w, const void *ctx)
{
    return ppcp_peer_head_encode(w, (const ppcp_peer_desc *)ctx);
}

static ppcp_result enc_connection(const ppcp_msg *m, msg_wctx *c, enc_scratch *s)
{
    ppcp_wfield *f = c->f;
    size_t       n = 0;

    switch (m->type) {
    case PPCP_MT_LINK_BIND: {
        const ppcp_body_link_bind *b = &m->body.link_bind;
        /* ENC 2.1a: `channel` equals the frame header's, which is what 2.1b
         * then takes each stream's channel from. */
        f[n++] = ppcp_wf_bytes("link_id", b->link_id, PPCP_LINK_ID_BYTES);
        f[n++] = ppcp_wf_uint("channel", b->channel);
        break;
    }
    case PPCP_MT_HELLO: {
        const ppcp_body_hello *b = &m->body.hello;
        if (b->version_count == 0) return PPCP_ERR_INVALID;   /* 3.1b */
        s->versions.v   = b->versions;   s->versions.n   = b->version_count;
        s->profiles.v   = b->profiles;   s->profiles.n   = b->profile_count;
        s->extensions.v = b->extensions; s->extensions.n = b->extension_count;
        f[n++] = ppcp_wf_sub("versions", idlist_w, &s->versions);
        f[n++] = ppcp_wf_id("peer_id", &b->peer_id);
        f[n++] = ppcp_wf_enum("role", ppcp_role_enum_map(), (int)b->role);
        f[n++] = ppcp_wf_sub("profiles", idlist_w, &s->profiles);
        f[n++] = ppcp_wf_sub("extensions", idlist_w, &s->extensions);
        if (b->product.present)
            f[n++] = ppcp_wf_sub("product", ppcp_product_write, &b->product);
        break;
    }
    case PPCP_MT_HELLO_ACCEPT: {
        const ppcp_body_hello_accept *b = &m->body.hello_accept;
        s->profiles.v   = b->profiles;   s->profiles.n   = b->profile_count;
        s->extensions.v = b->extensions; s->extensions.n = b->extension_count;
        f[n++] = ppcp_wf_id("version", &b->version);
        /* 3.2d / CORE 10.1e: the support window, always stated — a peer whose
         * own version is below it is entitled to know before it is refused. */
        f[n++] = ppcp_wf_id("min_version", &b->min_version);
        f[n++] = ppcp_wf_id("peer_id", &b->peer_id);
        f[n++] = ppcp_wf_enum("role", ppcp_role_enum_map(), (int)b->role);
        f[n++] = ppcp_wf_sub("profiles", idlist_w, &s->profiles);
        f[n++] = ppcp_wf_sub("extensions", idlist_w, &s->extensions);
        if (b->product.present)
            f[n++] = ppcp_wf_sub("product", ppcp_product_write, &b->product);
        break;
    }
    case PPCP_MT_DECLARE: {
        const ppcp_body_declare *b  = &m->body.declare;
        ppcp_result              rc = ppcp_peer_desc_validate(&b->peer);
        if (rc != PPCP_OK) return rc;
        if (b->generation == 0) return PPCP_ERR_INVALID;   /* 3.3: starts at 1 */
        /* 3.3a: a COMPLETE SNAPSHOT, not a delta.  Every list is written
         * unconditionally, including an empty `sources` — 3.3d requires a host
         * owning none to send the field rather than skip the message. */
        f[n++] = ppcp_wf_uint("generation", b->generation);
        f[n++] = ppcp_wf_sub("peer", peer_head_w, &b->peer);
        f[n++] = ppcp_wf_sub("timebases", ppcp_peer_timebases_write, &b->peer);
        f[n++] = ppcp_wf_sub("relations", ppcp_peer_relations_write, &b->peer);
        f[n++] = ppcp_wf_sub("sources", ppcp_peer_sources_write, &b->peer);
        break;
    }
    case PPCP_MT_DECLARE_ACK: {
        const ppcp_body_declare_ack *b = &m->body.declare_ack;
        if (b->verdict == PPCP_VERDICT_REJECTED && !b->has_reason)
            return PPCP_ERR_INVALID;    /* 3.4a: machine-readable reason */
        f[n++] = ppcp_wf_uint("generation", b->generation);
        f[n++] = ppcp_wf_enum("verdict", verdict_map, (int)b->verdict);
        if (b->has_reason)
            f[n++] = ppcp_wf_id("reason", &b->reason);
        if (b->note_count > 0)
            f[n++] = ppcp_wf_sub("notes", notes_w, b);
        break;
    }
    case PPCP_MT_RELATION_UPDATE: {
        const ppcp_body_relation_update *b = &m->body.relation_update;
        s->relations.v = b->relations;
        s->relations.n = b->relation_count;
        /* 3.5b: one relation per timebase pair the peer has MEASURED.
         * Relations are never composed (I18), and there is no function in this
         * library that could have composed one. */
        f[n++] = ppcp_wf_sub("relations", rel_list_w, &s->relations);
        break;
    }
    case PPCP_MT_CALIBRATION_UPDATE:
        f[n++] = ppcp_wf_sub("calibration", calib_w, &m->body.calibration_update.calibration);
        break;
    case PPCP_MT_DISCONTINUITY:
        f[n++] = ppcp_wf_sub("discontinuity", disc_w, &m->body.discontinuity.discontinuity);
        break;
    default:
        return PPCP_ERR_INVALID;
    }
    c->n = n;
    return PPCP_OK;
}

static ppcp_result enc_session(const ppcp_msg *m, msg_wctx *c, enc_scratch *s)
{
    ppcp_wfield *f = c->f;
    size_t       n = 0;

    switch (m->type) {
    case PPCP_MT_SESSION_OPEN: {
        const ppcp_body_session_open *b = &m->body.session_open;
        f[n++] = ppcp_wf_id("timebase_ref", &b->timebase_ref);
        if (b->epoch.present)
            f[n++] = ppcp_wf_sub("epoch", ppcp_session_epoch_write, &b->epoch);
        /* 4.1d / 5.10e: both if and only if the Session has a host.  A
         * hostless session recording this frame in its bundle omits them, and
         * their absence IS the statement that no arbitration occurs. */
        if (b->has_arbitration) {
            if (b->coincidence_window_ns <= 0 || b->issue_hold_ns <= 0)
                return PPCP_ERR_INVALID;
            f[n++] = ppcp_wf_int("coincidence_window_ns", b->coincidence_window_ns);
            f[n++] = ppcp_wf_int("issue_hold_ns", b->issue_hold_ns);
        }
        if (b->has_heartbeat_interval)
            f[n++] = ppcp_wf_uint("heartbeat_interval_ms", b->heartbeat_interval_ms);
        break;
    }
    case PPCP_MT_SESSION_JOINED: {
        const ppcp_body_session_joined *b = &m->body.session_joined;
        f[n++] = ppcp_wf_id("peer_id", &b->peer_id);
        f[n++] = ppcp_wf_enum("verdict", join_verdict_map, (int)b->verdict);
        if (b->has_reason)
            f[n++] = ppcp_wf_id("reason", &b->reason);
        break;
    }
    case PPCP_MT_SESSION_RESUME: {
        const ppcp_body_session_resume *b = &m->body.session_resume;
        s->minted.v = b->minted_shots;
        s->minted.n = b->minted_shot_count;
        f[n++] = ppcp_wf_id("peer_id", &b->peer_id);
        /* 4.3c: shots minted during the outage are reconciled through
         * `shot_link`.  They are not renumbered and their `authority` stays
         * `device` (I7, I9). */
        f[n++] = ppcp_wf_sub("minted_shots", idlist_w, &s->minted);
        f[n++] = ppcp_wf_sub("pending_captures", pending_w, b);
        break;
    }
    case PPCP_MT_SESSION_STATE: {
        const ppcp_body_session_state *b = &m->body.session_state;
        f[n++] = ppcp_wf_enum("state", ppcp_session_state_enum_map(), (int)b->state);
        /* 4.4a / I10: asserted by the peer that owns the data, never inferred
         * by the receiver from what has arrived. */
        f[n++] = ppcp_wf_enum("completeness", ppcp_session_completeness_enum_map(),
                              (int)b->completeness);
        break;
    }
    case PPCP_MT_CONTEXT_CHANGE:
        f[n++] = ppcp_wf_sub("context", ctxc_w, &m->body.context_change.context);
        break;
    case PPCP_MT_SESSION_CLOSE: {
        const ppcp_body_session_close *b = &m->body.session_close;
        f[n++] = ppcp_wf_id("reason", &b->reason);
        break;
    }
    default:
        return PPCP_ERR_INVALID;
    }
    c->n = n;
    return PPCP_OK;
}

static ppcp_result enc_stream(const ppcp_msg *m, msg_wctx *c, enc_scratch *s)
{
    ppcp_wfield *f = c->f;
    size_t       n = 0;

    switch (m->type) {
    case PPCP_MT_STREAM_OPEN:
        f[n++] = ppcp_wf_sub("stream", strm_w, &m->body.stream_open.stream);
        break;
    case PPCP_MT_STREAM_OPEN_ACK: {
        const ppcp_body_stream_open_ack *b = &m->body.stream_open_ack;
        f[n++] = ppcp_wf_id("stream_id", &b->stream_id);
        f[n++] = ppcp_wf_enum("verdict", stream_verdict_map, (int)b->verdict);
        if (b->has_reason)
            f[n++] = ppcp_wf_id("reason", &b->reason);
        if (b->has_opened_at)
            f[n++] = ppcp_wf_sub("opened_at", ppcp_sub_write_instant, &b->opened_at);
        break;
    }
    case PPCP_MT_STREAM_CLOSE: {
        const ppcp_body_stream_close *b = &m->body.stream_close;
        f[n++] = ppcp_wf_id("stream_id", &b->stream_id);
        if (b->has_closed_at)
            f[n++] = ppcp_wf_sub("closed_at", ppcp_sub_write_instant, &b->closed_at);
        /* 5.1d: EITHER peer may originate this — the owner because it can no
         * longer produce, the consumer because it no longer wants the data —
         * and `reason` says which. */
        f[n++] = ppcp_wf_id("reason", &b->reason);
        break;
    }
    case PPCP_MT_ARM:
    case PPCP_MT_DISARM: {
        const ppcp_body_stream_ids *b = &m->body.arm;
        s->stream_ids.v = b->stream_ids;
        s->stream_ids.n = b->stream_id_count;
        f[n++] = ppcp_wf_sub("stream_ids", idlist_w, &s->stream_ids);
        break;
    }
    case PPCP_MT_READINESS: {
        const ppcp_body_readiness *b = &m->body.readiness;
        s->stream_ids.v = b->stream_ids;
        s->stream_ids.n = b->stream_id_count;
        f[n++] = ppcp_wf_sub("readiness", rdy_w, &b->readiness);
        f[n++] = ppcp_wf_sub("stream_ids", idlist_w, &s->stream_ids);
        break;
    }
    case PPCP_MT_INTERRUPTION: {
        const ppcp_body_interruption *b = &m->body.interruption;
        s->stream_ids.v = b->stream_ids;
        s->stream_ids.n = b->stream_id_count;
        f[n++] = ppcp_wf_id("kind", &b->kind);
        f[n++] = ppcp_wf_sub("interval", ppcp_sub_write_interval, &b->interval);
        f[n++] = ppcp_wf_bool("recovered", b->recovered);
        f[n++] = ppcp_wf_sub("stream_ids", idlist_w, &s->stream_ids);
        break;
    }
    case PPCP_MT_HEARTBEAT:
        f[n++] = ppcp_wf_uint("seq", m->body.heartbeat.seq);
        break;
    case PPCP_MT_HEARTBEAT_ACK: {
        const ppcp_body_heartbeat_ack *b = &m->body.heartbeat_ack;
        f[n++] = ppcp_wf_uint("seq", b->seq);
        f[n++] = ppcp_wf_enum("thermal", ppcp_thermal_enum_map(), (int)b->thermal);
        f[n++] = ppcp_wf_uint("storage_free_bytes", b->storage_free_bytes);
        if (b->has_vendor_label)
            f[n++] = ppcp_wf_id("vendor_thermal_label", &b->vendor_thermal_label);
        if (b->has_battery_pct)
            f[n++] = ppcp_wf_uint("battery_pct", b->battery_pct);
        if (b->has_charging)
            f[n++] = ppcp_wf_bool("charging", b->charging);
        break;
    }
    default:
        return PPCP_ERR_INVALID;
    }
    c->n = n;
    return PPCP_OK;
}

static ppcp_result enc_sync_detect(const ppcp_msg *m, msg_wctx *c, enc_scratch *s)
{
    ppcp_wfield *f = c->f;
    size_t       n = 0;

    switch (m->type) {
    case PPCP_MT_SYNC_PROBE: {
        const ppcp_body_sync_probe *b = &m->body.sync_probe;
        f[n++] = ppcp_wf_uint("probe_seq", b->probe_seq);
        /* 6.1d / I21: a multi-timebase peer runs a separate probe sequence per
         * timebase, which is what `timebase_id` names. */
        f[n++] = ppcp_wf_id("timebase_id", &b->timebase_id);
        f[n++] = ppcp_wf_sub("t1", ppcp_sub_write_instant, &b->t1);
        break;
    }
    case PPCP_MT_SYNC_REPLY: {
        const ppcp_body_sync_reply *b = &m->body.sync_reply;
        if (!ppcp_id_equal(&b->t2.tb, &b->t3.tb))
            return PPCP_ERR_INVALID;    /* 6.1b */
        f[n++] = ppcp_wf_uint("probe_seq", b->probe_seq);
        f[n++] = ppcp_wf_sub("t1", ppcp_sub_write_instant, &b->t1);  /* 6.1a: echoed */
        f[n++] = ppcp_wf_sub("t2", ppcp_sub_write_instant, &b->t2);
        f[n++] = ppcp_wf_sub("t3", ppcp_sub_write_instant, &b->t3);
        break;
    }
    case PPCP_MT_SYNC_RESIDUAL: {
        const ppcp_body_sync_residual *b = &m->body.sync_residual;
        f[n++] = ppcp_wf_id("shot_id", &b->shot_id);
        f[n++] = ppcp_wf_id("timebase_id", &b->timebase_id);
        f[n++] = ppcp_wf_int("residual_ns", b->residual_ns);
        f[n++] = ppcp_wf_id("basis", &b->basis);
        break;
    }
    case PPCP_MT_CANDIDATE:
        /* 7.1d: a peer emits `candidate` for EVERY nomination — one it
         * promotes, one it does not, and one a host later excludes.  Losers
         * are sent, not withheld (I8). */
        f[n++] = ppcp_wf_sub("candidate", cand_w, &m->body.candidate.candidate);
        break;
    case PPCP_MT_SHOT:
        f[n++] = ppcp_wf_sub("shot", shot_w, &m->body.shot.shot);
        break;
    case PPCP_MT_CAPTURE_REQUEST: {
        const ppcp_body_capture_request *b = &m->body.capture_request;
        s->stream_ids.v = b->stream_ids;
        s->stream_ids.n = b->stream_id_count;
        f[n++] = ppcp_wf_id("shot_id", &b->shot_id);
        f[n++] = ppcp_wf_sub("t0", ppcp_sub_write_instant, &b->t0);
        f[n++] = ppcp_wf_sub("stream_ids", idlist_w, &s->stream_ids);
        f[n++] = ppcp_wf_int("pre_ns", b->pre_ns);
        f[n++] = ppcp_wf_int("post_ns", b->post_ns);
        break;
    }
    case PPCP_MT_ANNOTATION:
        f[n++] = ppcp_wf_sub("annotation", annot_w, &m->body.annotation.annotation);
        break;
    case PPCP_MT_SHOT_LINK:
        f[n++] = ppcp_wf_sub("link", slink_w, &m->body.shot_link.link);
        break;
    case PPCP_MT_SESSION_LINK:
        f[n++] = ppcp_wf_sub("link", sslink_w, &m->body.session_link.link);
        break;
    default:
        return PPCP_ERR_INVALID;
    }
    c->n = n;
    return PPCP_OK;
}

static ppcp_result enc_capture(const ppcp_msg *m, msg_wctx *c, enc_scratch *s)
{
    ppcp_wfield *f = c->f;
    size_t       n = 0;

    switch (m->type) {
    case PPCP_MT_CAPTURE_ANNOUNCE: {
        const ppcp_body_capture_announce *b = &m->body.capture_announce;
        f[n++] = ppcp_wf_sub("capture", capture_w, &b->capture);
        if (b->has_thumbnail) {
            if (b->thumbnail_len > PPCP_THUMBNAIL_MAX)
                return PPCP_ERR_LIMIT;      /* 8.1d */
            s->thumb.format = &b->thumbnail_format;
            s->thumb.p      = b->thumbnail;
            s->thumb.n      = b->thumbnail_len;
            f[n++] = ppcp_wf_sub("thumbnail", thumb_w, &s->thumb);
        }
        break;
    }
    case PPCP_MT_CAPTURE_UPDATE: {
        const ppcp_body_capture_update *b = &m->body.capture_update;
        f[n++] = ppcp_wf_id("capture_id", &b->capture_id);
        /* 8.2a: `completeness` and `transfer` are updated independently —
         * `complete` + `pending` and `partial` + `present` are both normal. */
        if (b->has_completeness)
            f[n++] = ppcp_wf_enum("completeness", ppcp_capture_completeness_enum_map(),
                                  (int)b->completeness);
        if (b->has_transfer)
            f[n++] = ppcp_wf_enum("transfer", ppcp_transfer_enum_map(), (int)b->transfer);
        if (b->digest.present)
            f[n++] = ppcp_wf_sub("digest", digest_w, &b->digest);
        if (b->has_achieved_frames) {
            /* 8.2b / I30's ONE exception: the series may ride here only for a
             * Capture whose payload will not transfer.  A `complete` + `failed`
             * clip is exactly a session whose link died, and the frame
             * timeline is what tells a consumer what it lost — so the encoder
             * refuses it in any other transfer state rather than opening the
             * control channel to the per-frame series generally. */
            if (!b->has_transfer || b->transfer != PPCP_TRANSFER_FAILED)
                return PPCP_ERR_INVALID;
            f[n++] = ppcp_wf_sub("achieved_frames", af_w, &b->achieved_frames);
        }
        break;
    }
    case PPCP_MT_CAPTURE_COMMITTED: {
        const ppcp_body_capture_committed *b = &m->body.capture_committed;
        f[n++] = ppcp_wf_id("capture_id", &b->capture_id);
        f[n++] = ppcp_wf_sub("digest", digest_w, &b->digest);
        break;
    }
    case PPCP_MT_PAYLOAD_BEGIN: {
        const ppcp_body_payload_begin *b = &m->body.payload_begin;
        if (!b->digest.present)
            return PPCP_ERR_INVALID;    /* 8.1e: present by payload_begin */
        if (b->chunk_bytes == 0 || b->chunk_bytes > PPCP_LIMIT_CHUNK_BYTES)
            return PPCP_ERR_INVALID;    /* ENC 6f */
        f[n++] = ppcp_wf_id("capture_id", &b->capture_id);
        f[n++] = ppcp_wf_uint("bytes", b->bytes);
        f[n++] = ppcp_wf_sub("digest", digest_w, &b->digest);
        f[n++] = ppcp_wf_uint("chunk_bytes", b->chunk_bytes);
        /* 8.3g / ENC 6a1 / I30: the per-frame series belong on THIS channel,
         * with the frames they describe, and never on control. */
        if (b->has_achieved_frames)
            f[n++] = ppcp_wf_sub("achieved_frames", af_w, &b->achieved_frames);
        break;
    }
    case PPCP_MT_PAYLOAD_CHUNK: {
        const ppcp_body_payload_chunk *b = &m->body.payload_chunk;
        if (b->data == NULL || b->data_len == 0)
            return PPCP_ERR_INVALID;
        if (!b->digest.present)
            return PPCP_ERR_INVALID;    /* ENC 6c: SHA-256 of `data` */
        f[n++] = ppcp_wf_id("capture_id", &b->capture_id);
        f[n++] = ppcp_wf_uint("index", b->index);
        f[n++] = ppcp_wf_uint("offset", b->offset);
        f[n++] = ppcp_wf_bytes("data", b->data, b->data_len);
        f[n++] = ppcp_wf_sub("digest", digest_w, &b->digest);
        break;
    }
    case PPCP_MT_PAYLOAD_ACK: {
        const ppcp_body_payload_ack *b = &m->body.payload_ack;
        f[n++] = ppcp_wf_id("capture_id", &b->capture_id);
        f[n++] = ppcp_wf_uint("index", b->index);
        break;
    }
    case PPCP_MT_PAYLOAD_END: {
        const ppcp_body_payload_end *b = &m->body.payload_end;
        if (!b->digest.present)
            return PPCP_ERR_INVALID;
        f[n++] = ppcp_wf_id("capture_id", &b->capture_id);
        f[n++] = ppcp_wf_sub("digest", digest_w, &b->digest);
        break;
    }
    case PPCP_MT_PAYLOAD_ABORT: {
        const ppcp_body_payload_abort *b = &m->body.payload_abort;
        f[n++] = ppcp_wf_id("capture_id", &b->capture_id);
        f[n++] = ppcp_wf_id("reason", &b->reason);
        break;
    }
    case PPCP_MT_PAYLOAD_RESUME: {
        const ppcp_body_payload_resume *b = &m->body.payload_resume;
        f[n++] = ppcp_wf_id("capture_id", &b->capture_id);
        /* 8.3d: resumption restarts from the chunk AFTER the last acknowledged
         * index, not from the beginning. */
        f[n++] = ppcp_wf_uint("from_index", b->from_index);
        break;
    }
    default:
        return PPCP_ERR_INVALID;
    }
    (void)s;
    c->n = n;
    return PPCP_OK;
}

static ppcp_result enc_offline(const ppcp_msg *m, msg_wctx *c, enc_scratch *s)
{
    ppcp_wfield *f = c->f;
    size_t       n = 0;

    switch (m->type) {
    case PPCP_MT_SESSION_OFFER: {
        const ppcp_body_session_offer *b = &m->body.session_offer;
        f[n++] = ppcp_wf_id("minting_peer_id", &b->minting_peer_id);
        f[n++] = ppcp_wf_enum("completeness", ppcp_session_completeness_enum_map(),
                              (int)b->completeness);
        if (b->epoch.present)
            f[n++] = ppcp_wf_sub("epoch", ppcp_session_epoch_write, &b->epoch);
        if (b->has_bytes_estimate)
            f[n++] = ppcp_wf_uint("bytes_estimate", b->bytes_estimate);
        break;
    }
    case PPCP_MT_SESSION_ACCEPT: {
        const ppcp_body_session_accept *b = &m->body.session_accept;
        f[n++] = ppcp_wf_enum("verdict", offer_verdict_map, (int)b->verdict);
        if (b->has_reason)
            f[n++] = ppcp_wf_id("reason", &b->reason);
        /* 9.1a: `have_digests` lets the exporter skip payloads the importer
         * already holds. */
        if (b->have_digest_count > 0)
            f[n++] = ppcp_wf_sub("have_digests", have_digests_w, b);
        break;
    }
    case PPCP_MT_SESSION_MANIFEST: {
        const ppcp_body_session_manifest *b = &m->body.session_manifest;
        s->streams.v = b->streams;
        s->streams.n = b->stream_count;
        f[n++] = ppcp_wf_sub("streams", idlist_w, &s->streams);
        f[n++] = ppcp_wf_sub("captures", manifest_caps_w, b);
        f[n++] = ppcp_wf_enum("completeness", ppcp_session_completeness_enum_map(),
                              (int)b->completeness);
        f[n++] = ppcp_wf_sub("counts", counts_w, b);
        break;
    }
    case PPCP_MT_ERROR: {
        const ppcp_body_error *b = &m->body.error;
        /* 10.1f: `unsupported_version` MUST carry the sender's full supported
         * range.  Refused at the encoder rather than noted, because a code
         * that is both fatal and uninformative tells a user only that
         * something failed. */
        if (ppcp_cbor_key_is(b->code.v, b->code.len, PPCP_ERRCODE_UNSUPPORTED_VERSION) &&
            (!b->has_detail_supported || b->detail_supported_count == 0))
            return PPCP_ERR_INVALID;
        f[n++] = ppcp_wf_id("code", &b->code);
        f[n++] = ppcp_wf_text("message", b->message, b->message_len);
        if (b->has_in_reply_to)
            f[n++] = ppcp_wf_uint("in_reply_to", b->in_reply_to);
        if (b->has_detail_supported || b->has_detail_reason)
            f[n++] = ppcp_wf_sub("detail", detail_w, b);
        break;
    }
    default:
        return PPCP_ERR_INVALID;
    }
    c->n = n;
    return PPCP_OK;
}

/* ENC 5a against MSG §4, §9.1 and §9.2.
 *
 * Eight message bodies name a `session_id` field: the five of §4, plus
 * `session_offer`, `session_accept` and `session_manifest`.  ENC 5a forbids a
 * body using `session_id` "as a field name for ANY OTHER PURPOSE" — and these
 * eight use it for exactly the envelope's purpose, the Session this message is
 * about.  Writing both would emit the key twice in one map, which ENC 4d makes
 * malformed, so the two cannot both be present and one of them has to be the
 * other.  The envelope's is the one that is, because a receiver routing on
 * `session_id` reads the envelope before it knows the type.
 *
 * The encoder therefore HOISTS the body's value into the envelope, and the
 * body decoders read it back out of the same flat map wherever it sits.  A
 * body and an envelope that disagree is a caller bug, not a wire form.
 * Recorded as finding F-L5-1 for the L17 erratum sweep. */
static const ppcp_id *body_session_id(const ppcp_msg *m)
{
    switch (m->type) {
    case PPCP_MT_SESSION_OPEN:     return &m->body.session_open.session_id;
    case PPCP_MT_SESSION_JOINED:   return &m->body.session_joined.session_id;
    case PPCP_MT_SESSION_RESUME:   return &m->body.session_resume.session_id;
    case PPCP_MT_SESSION_STATE:    return &m->body.session_state.session_id;
    case PPCP_MT_SESSION_CLOSE:    return &m->body.session_close.session_id;
    case PPCP_MT_SESSION_OFFER:    return &m->body.session_offer.session_id;
    case PPCP_MT_SESSION_ACCEPT:   return &m->body.session_accept.session_id;
    case PPCP_MT_SESSION_MANIFEST: return &m->body.session_manifest.session_id;
    default:                       return NULL;
    }
}

ppcp_result ppcp_msg_init(ppcp_msg *m, ppcp_msg_type t, uint64_t msg_id)
{
    const ppcp_msg_info *info = ppcp_msg_for(t);
    ppcp_result          rc;
    if (m == NULL || info == NULL)
        return PPCP_ERR_INVALID;
    memset(m, 0, sizeof(*m));
    rc = ppcp_envelope_init(&m->env, info->type, msg_id);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_id_set_z(&m->type_name, info->type);
    if (rc != PPCP_OK)
        return rc;
    m->type = t;
    return PPCP_OK;
}

ppcp_result ppcp_msg_set_session_id(ppcp_msg *m, const char *session_id)
{
    if (m == NULL || session_id == NULL)
        return PPCP_ERR_INVALID;
    return ppcp_envelope_set_session_id(&m->env, session_id, strlen(session_id));
}

ppcp_result ppcp_msg_set_reply_to(ppcp_msg *m, uint64_t reply_to)
{
    if (m == NULL)
        return PPCP_ERR_INVALID;
    return ppcp_envelope_set_reply_to(&m->env, reply_to);
}

ppcp_result ppcp_msg_encode(uint8_t *out, size_t cap, uint8_t channel, const ppcp_msg *m,
                            size_t *out_written)
{
    msg_wctx       ctx;
    enc_scratch    scratch;
    ppcp_envelope  env;
    const ppcp_id *sid;
    ppcp_result    rc;

    if (out == NULL || m == NULL || out_written == NULL)
        return PPCP_ERR_INVALID;
    memset(&ctx, 0, sizeof(ctx));
    memset(&scratch, 0, sizeof(scratch));
    env = m->env;

    sid = body_session_id(m);
    if (sid != NULL) {
        if (!ppcp_id_is_set(sid))
            return PPCP_ERR_INVALID;
        if (env.has_session_id && !ppcp_id_equal(&env.session_id, sid))
            return PPCP_ERR_INVALID;
        env.has_session_id = true;
        env.session_id     = *sid;
    }

    /* MSG §2 — the channel rule is checked HERE and not left to the caller,
     * because 2a and 2b are the two halves of the reason two channels exist. */
    rc = ppcp_msg_check_channel(m->type, channel);
    if (rc != PPCP_OK)
        return rc;

    switch (m->type) {
    case PPCP_MT_LINK_BIND: case PPCP_MT_HELLO: case PPCP_MT_HELLO_ACCEPT:
    case PPCP_MT_DECLARE: case PPCP_MT_DECLARE_ACK: case PPCP_MT_RELATION_UPDATE:
    case PPCP_MT_CALIBRATION_UPDATE: case PPCP_MT_DISCONTINUITY:
        rc = enc_connection(m, &ctx, &scratch); break;
    case PPCP_MT_SESSION_OPEN: case PPCP_MT_SESSION_JOINED: case PPCP_MT_SESSION_RESUME:
    case PPCP_MT_SESSION_STATE: case PPCP_MT_CONTEXT_CHANGE: case PPCP_MT_SESSION_CLOSE:
        rc = enc_session(m, &ctx, &scratch); break;
    case PPCP_MT_STREAM_OPEN: case PPCP_MT_STREAM_OPEN_ACK: case PPCP_MT_STREAM_CLOSE:
    case PPCP_MT_ARM: case PPCP_MT_DISARM: case PPCP_MT_READINESS:
    case PPCP_MT_INTERRUPTION: case PPCP_MT_HEARTBEAT: case PPCP_MT_HEARTBEAT_ACK:
        rc = enc_stream(m, &ctx, &scratch); break;
    case PPCP_MT_SYNC_PROBE: case PPCP_MT_SYNC_REPLY: case PPCP_MT_SYNC_RESIDUAL:
    case PPCP_MT_CANDIDATE: case PPCP_MT_SHOT: case PPCP_MT_CAPTURE_REQUEST:
    case PPCP_MT_ANNOTATION: case PPCP_MT_SHOT_LINK: case PPCP_MT_SESSION_LINK:
        rc = enc_sync_detect(m, &ctx, &scratch); break;
    case PPCP_MT_CAPTURE_ANNOUNCE: case PPCP_MT_CAPTURE_UPDATE:
    case PPCP_MT_CAPTURE_COMMITTED: case PPCP_MT_PAYLOAD_BEGIN:
    case PPCP_MT_PAYLOAD_CHUNK: case PPCP_MT_PAYLOAD_ACK: case PPCP_MT_PAYLOAD_END:
    case PPCP_MT_PAYLOAD_ABORT: case PPCP_MT_PAYLOAD_RESUME:
        rc = enc_capture(m, &ctx, &scratch); break;
    case PPCP_MT_SESSION_OFFER: case PPCP_MT_SESSION_ACCEPT:
    case PPCP_MT_SESSION_MANIFEST: case PPCP_MT_ERROR:
        rc = enc_offline(m, &ctx, &scratch); break;
    default:
        return PPCP_ERR_INVALID;
    }
    if (rc != PPCP_OK)
        return rc;

    return ppcp_message_encode(out, cap, channel, &env, ctx.n, msg_body_write, &ctx,
                               out_written);
}
