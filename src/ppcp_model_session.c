/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * PPCP-CORE §5.10 and §5.11 — Session, ContextChange and Stream.  L4.
 *
 * 5.10e is the clause with the most leverage here.  `coincidence_window_ns`
 * and `issue_hold_ns` are present if and only if the Session has a peer with
 * `role: host`, and their absence is the structural statement that no
 * arbitration occurs — which is the NORMAL case for an entry-level capture
 * device.  Two constructors, no setter: the hostless one cannot be handed
 * them, so a range bundle cannot carry two numbers nothing consults.
 */
#include "ppcp/model.h"
#include "ppcp_codec.h"

#include <string.h>

static const ppcp_enum_map session_state_map[] = {
    { "open",   PPCP_SESSION_OPEN   },
    { "closed", PPCP_SESSION_CLOSED },
    { NULL, 0 }
};

/* Two maps over one enum, because the two axes are different vocabularies:
 * a Session is complete | partial | unknown, a Capture is complete | partial |
 * absent, and `absent` on a Session or `unknown` on a Capture is malformed. */
static const ppcp_enum_map session_completeness_map[] = {
    { "complete", PPCP_COMPLETE },
    { "partial",  PPCP_PARTIAL  },
    { "unknown",  PPCP_UNKNOWN  },
    { NULL, 0 }
};

static const ppcp_enum_map capture_completeness_map[] = {
    { "complete", PPCP_COMPLETE },
    { "partial",  PPCP_PARTIAL  },
    { "absent",   PPCP_ABSENT   },
    { NULL, 0 }
};

static const ppcp_enum_map continuity_map[] = {
    { "continuous",    PPCP_CONTINUOUS    },
    { "shot_windowed", PPCP_SHOT_WINDOWED },
    { NULL, 0 }
};

const ppcp_enum_map *ppcp_session_state_enum_map(void) { return session_state_map; }
const ppcp_enum_map *ppcp_session_completeness_enum_map(void) { return session_completeness_map; }
const ppcp_enum_map *ppcp_capture_completeness_enum_map(void) { return capture_completeness_map; }

/* -------------------------------------------------------- ContextChange */

ppcp_result ppcp_context_change_make(ppcp_context_change *out, const char *id,
                                     const ppcp_instant *at, const char *kind,
                                     const char *value)
{
    ppcp_result rc;
    if (out == NULL || at == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_instant_validate(at) != PPCP_OK)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    rc = ppcp_id_set_z(&out->id, id);      if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->kind, kind);  if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->value, value); if (rc != PPCP_OK) return rc;
    /* 5.10f: `location` and `weather` are LABELS.  Nothing in this library
     * computes from a ContextChange, which is I15's principle applied to
     * context rather than to time. */
    out->at = *at;
    return PPCP_OK;
}

ppcp_result ppcp_context_change_validate(const ppcp_context_change *c)
{
    if (c == NULL || !ppcp_id_is_set(&c->id) || !ppcp_id_is_set(&c->kind) ||
        !ppcp_id_is_set(&c->value))
        return PPCP_ERR_INVALID;
    return ppcp_instant_validate(&c->at);
}

ppcp_result ppcp_context_change_encode(ppcp_cbor_writer *w, const ppcp_context_change *c)
{
    ppcp_wfield f[4];
    ppcp_result rc = ppcp_context_change_validate(c);
    if (rc != PPCP_OK)
        return rc;
    f[0] = ppcp_wf_id("id", &c->id);
    f[1] = ppcp_wf_sub("at", ppcp_sub_write_instant, &c->at);
    f[2] = ppcp_wf_id("kind", &c->kind);
    f[3] = ppcp_wf_id("value", &c->value);
    return ppcp_rec_write(w, f, 4);
}

ppcp_result ppcp_context_change_decode(ppcp_cbor_reader *r, ppcp_context_change *out)
{
    ppcp_rfield f[4];
    bool        a = false, b = false, c = false, d = false;
    ppcp_result rc;
    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    f[0] = ppcp_rf("id", PPCP_F_ID, &out->id, &a);
    f[1] = ppcp_rf_sub("at", ppcp_sub_read_instant, &out->at, NULL, &b);
    f[2] = ppcp_rf("kind", PPCP_F_ID, &out->kind, &c);
    f[3] = ppcp_rf("value", PPCP_F_ID, &out->value, &d);
    rc = ppcp_rec_read(r, f, 4);
    if (rc != PPCP_OK) return rc;
    if (!a || !b || !c || !d) return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

/* ---------------------------------------------------------------- Stream */

ppcp_result ppcp_stream_make(ppcp_stream *out, const char *id, const char *session_id,
                             const char *source_id, const char *kind, const char *profile_id,
                             const char *timebase_id, ppcp_continuity continuity,
                             const ppcp_instant *opened_at)
{
    ppcp_result rc;
    if (out == NULL || opened_at == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_enum_to_text(continuity_map, (int)continuity) == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_instant_validate(opened_at) != PPCP_OK)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    rc = ppcp_id_set_z(&out->id, id);                   if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->session_id, session_id);   if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->source_id, source_id);     if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->kind, kind);               if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->profile_id, profile_id);   if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->timebase_id, timebase_id); if (rc != PPCP_OK) return rc;
    /* I5 / 5.11a: source, profile, timebase and calibration are fixed for THE
     * STREAM'S lifetime.  They are constructor parameters and there is no
     * setter for any of them; a change closes the Stream and opens another
     * within the same Session. */
    out->continuity = continuity;
    out->opened_at  = *opened_at;
    return PPCP_OK;
}

ppcp_result ppcp_stream_set_calibration_id(ppcp_stream *s, const char *calibration_id)
{
    ppcp_result rc;
    if (s == NULL)
        return PPCP_ERR_INVALID;
    /* Settable once, before the Stream is opened; changing it afterwards is
     * I5's forbidden case, and the engine of L6 refuses it. */
    if (s->has_calibration_id)
        return PPCP_ERR_INVALID;
    rc = ppcp_id_set_z(&s->calibration_id, calibration_id);
    if (rc != PPCP_OK)
        return rc;
    s->has_calibration_id = true;
    return PPCP_OK;
}

ppcp_result ppcp_stream_close(ppcp_stream *s, const ppcp_instant *closed_at)
{
    if (s == NULL || closed_at == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_instant_validate(closed_at) != PPCP_OK)
        return PPCP_ERR_INVALID;
    if (!ppcp_id_equal(&closed_at->tb, &s->timebase_id))
        return PPCP_ERR_INVALID;
    if (closed_at->ns < s->opened_at.ns)
        return PPCP_ERR_INVALID;
    s->has_closed_at = true;
    s->closed_at     = *closed_at;
    return PPCP_OK;
}

bool ppcp_stream_is_preview(const ppcp_stream *s)
{
    if (s == NULL)
        return false;
    return ppcp_cbor_key_is(s->kind.v, s->kind.len, PPCP_STREAM_KIND_PREVIEW);
}

ppcp_result ppcp_stream_validate(const ppcp_stream *s)
{
    ppcp_result rc;
    if (s == NULL)
        return PPCP_ERR_INVALID;
    if (!ppcp_id_is_set(&s->id) || !ppcp_id_is_set(&s->session_id) ||
        !ppcp_id_is_set(&s->source_id) || !ppcp_id_is_set(&s->kind) ||
        !ppcp_id_is_set(&s->profile_id) || !ppcp_id_is_set(&s->timebase_id))
        return PPCP_ERR_INVALID;
    if (ppcp_enum_to_text(continuity_map, (int)s->continuity) == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_instant_validate(&s->opened_at);
    if (rc != PPCP_OK)
        return rc;
    /* Every instant on a Stream is in the Stream's own timebase (I1 with
     * 5.11: `timebase_id` is restated on the Stream for exactly this locality). */
    if (!ppcp_id_equal(&s->opened_at.tb, &s->timebase_id))
        return PPCP_ERR_INVALID;
    if (s->has_closed_at) {
        rc = ppcp_instant_validate(&s->closed_at);
        if (rc != PPCP_OK)
            return rc;
        if (!ppcp_id_equal(&s->closed_at.tb, &s->timebase_id))
            return PPCP_ERR_INVALID;
        if (s->closed_at.ns < s->opened_at.ns)
            return PPCP_ERR_INVALID;
    }
    /* 5.11: a `preview` Stream is ALWAYS `continuous` (the table of §5.11), and
     * 5.11j turns on that being true. */
    if (ppcp_stream_is_preview(s) && s->continuity != PPCP_CONTINUOUS)
        return PPCP_ERR_INVALID;
    return PPCP_OK;
}

ppcp_result ppcp_stream_encode(ppcp_cbor_writer *w, const ppcp_stream *s)
{
    ppcp_wfield f[10];
    size_t      n  = 0;
    ppcp_result rc = ppcp_stream_validate(s);
    if (rc != PPCP_OK)
        return rc;
    f[n++] = ppcp_wf_id("id", &s->id);
    f[n++] = ppcp_wf_id("session_id", &s->session_id);
    f[n++] = ppcp_wf_id("source_id", &s->source_id);
    f[n++] = ppcp_wf_id("kind", &s->kind);
    f[n++] = ppcp_wf_id("profile_id", &s->profile_id);
    f[n++] = ppcp_wf_id("timebase_id", &s->timebase_id);
    f[n++] = ppcp_wf_enum("continuity", continuity_map, (int)s->continuity);
    f[n++] = ppcp_wf_sub("opened_at", ppcp_sub_write_instant, &s->opened_at);
    if (s->has_calibration_id)
        f[n++] = ppcp_wf_id("calibration_id", &s->calibration_id);
    if (s->has_closed_at)
        f[n++] = ppcp_wf_sub("closed_at", ppcp_sub_write_instant, &s->closed_at);
    return ppcp_rec_write(w, f, n);
}

ppcp_result ppcp_stream_decode(ppcp_cbor_reader *r, ppcp_stream *out)
{
    ppcp_rfield f[10];
    size_t      n = 0;
    bool        s_id = false, s_sid = false, s_src = false, s_kind = false;
    bool        s_prof = false, s_tb = false, s_cont = false, s_open = false;
    int         cont = 0;
    ppcp_result rc;

    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    f[n++] = ppcp_rf("id", PPCP_F_ID, &out->id, &s_id);
    f[n++] = ppcp_rf("session_id", PPCP_F_ID, &out->session_id, &s_sid);
    f[n++] = ppcp_rf("source_id", PPCP_F_ID, &out->source_id, &s_src);
    f[n++] = ppcp_rf("kind", PPCP_F_ID, &out->kind, &s_kind);
    f[n++] = ppcp_rf("profile_id", PPCP_F_ID, &out->profile_id, &s_prof);
    f[n++] = ppcp_rf("timebase_id", PPCP_F_ID, &out->timebase_id, &s_tb);
    f[n++] = ppcp_rf_enum("continuity", continuity_map, &cont, &s_cont);
    f[n++] = ppcp_rf_sub("opened_at", ppcp_sub_read_instant, &out->opened_at, NULL, &s_open);
    f[n++] = ppcp_rf("calibration_id", PPCP_F_ID, &out->calibration_id,
                     &out->has_calibration_id);
    f[n++] = ppcp_rf_sub("closed_at", ppcp_sub_read_instant, &out->closed_at, NULL,
                         &out->has_closed_at);
    rc = ppcp_rec_read(r, f, n);
    if (rc != PPCP_OK)
        return rc;
    if (!s_id || !s_sid || !s_src || !s_kind || !s_prof || !s_tb || !s_cont || !s_open)
        return PPCP_ERR_MALFORMED;
    out->continuity = (ppcp_continuity)cont;
    if (ppcp_stream_validate(out) != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

/* --------------------------------------------------------------- Session */

static ppcp_result session_make_common(ppcp_session *out, const char *id,
                                       const char *timebase_ref,
                                       const ppcp_instant *opened_at)
{
    ppcp_result rc;
    if (out == NULL || opened_at == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_instant_validate(opened_at);
    if (rc != PPCP_OK) return rc;
    memset(out, 0, sizeof(*out));
    rc = ppcp_id_set_z(&out->id, id);
    if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->timebase_ref, timebase_ref);
    if (rc != PPCP_OK) return rc;
    /* 5.10h: `opened_at` is IN `timebase_ref`.  Checked here rather than
     * trusted, because an instant in some other clock is the fabricated start
     * time the erratum exists to rule out. */
    if (!ppcp_id_equal(&opened_at->tb, &out->timebase_ref))
        return PPCP_ERR_INVALID;
    out->opened_at              = *opened_at;
    out->state                  = PPCP_SESSION_OPEN;
    out->completeness           = PPCP_UNKNOWN;
    out->has_heartbeat_interval = false;
    return PPCP_OK;
}

ppcp_result ppcp_session_make_hostless(ppcp_session *out, const char *id,
                                       const char *timebase_ref,
                                       const ppcp_instant *opened_at)
{
    /* 5.10e: no host, therefore no arbitration parameters — and no way to add
     * them afterwards.  Offline `timebase_ref` is the capturing peer's own
     * (5.10a); the offline case is the same structure with a different value,
     * not a special mode. */
    return session_make_common(out, id, timebase_ref, opened_at);
}

ppcp_result ppcp_session_make_hosted(ppcp_session *out, const char *id,
                                     const char *timebase_ref,
                                     const ppcp_instant *opened_at,
                                     ppcp_duration_ns coincidence_window_ns,
                                     ppcp_duration_ns issue_hold_ns)
{
    ppcp_result rc;
    if (coincidence_window_ns <= 0 || issue_hold_ns <= 0)
        return PPCP_ERR_INVALID;
    rc = session_make_common(out, id, timebase_ref, opened_at);
    if (rc != PPCP_OK)
        return rc;
    /* 4.1c / 8.2: a TOLERANCE and a DEADLINE are different quantities and are
     * declared separately.  A host may reasonably run a 40 ms tolerance inside
     * a 200 ms hold. */
    out->has_arbitration        = true;
    out->coincidence_window_ns  = coincidence_window_ns;
    out->issue_hold_ns          = issue_hold_ns;
    return PPCP_OK;
}

ppcp_result ppcp_session_set_epoch(ppcp_session *s, int64_t wall_utc_ns, const ppcp_instant *at)
{
    if (s == NULL || at == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_instant_validate(at) != PPCP_OK)
        return PPCP_ERR_INVALID;
    /* I15: a wall-clock LABEL.  Nothing in this library subtracts it from
     * anything, and 5.3b forbids a wall timebase in an interval computation. */
    s->epoch.present     = true;
    s->epoch.wall_utc_ns = wall_utc_ns;
    s->epoch.at          = *at;
    return PPCP_OK;
}

ppcp_result ppcp_session_set_heartbeat_interval(ppcp_session *s, uint32_t ms)
{
    if (s == NULL || ms == 0)
        return PPCP_ERR_INVALID;
    s->has_heartbeat_interval = true;
    s->heartbeat_interval_ms  = ms;
    return PPCP_OK;
}

ppcp_result ppcp_session_set_peers(ppcp_session *s, const ppcp_peer_desc *peers, size_t count)
{
    size_t i, hosts = 0;
    if (s == NULL || peers == NULL || count == 0)
        return PPCP_ERR_INVALID;
    for (i = 0; i < count; i++) {
        if (peers[i].role == PPCP_ROLE_HOST)
            hosts++;
    }
    /* I20 / 5.2b: at most one peer with role host.  Not exactly one — sessions
     * exist with no host at all, and that is the normal case. */
    if (hosts > 1)
        return PPCP_ERR_INVALID;
    /* 5.10e: the roster and the arbitration parameters have to agree, and this
     * is the one place both are in hand. */
    if ((hosts == 1) != s->has_arbitration)
        return PPCP_ERR_INVALID;
    s->peers      = peers;
    s->peer_count = count;
    return PPCP_OK;
}

ppcp_result ppcp_session_set_streams(ppcp_session *s, const ppcp_stream *st, size_t count)
{
    if (s == NULL || (count > 0 && st == NULL))
        return PPCP_ERR_INVALID;
    s->streams      = st;
    s->stream_count = count;
    return PPCP_OK;
}

ppcp_result ppcp_session_set_shots(ppcp_session *s, const ppcp_shot *sh, size_t count)
{
    if (s == NULL || (count > 0 && sh == NULL))
        return PPCP_ERR_INVALID;
    s->shots      = sh;
    s->shot_count = count;
    return PPCP_OK;
}

ppcp_result ppcp_session_set_contexts(ppcp_session *s, const ppcp_context_change *c,
                                      size_t count)
{
    if (s == NULL || (count > 0 && c == NULL))
        return PPCP_ERR_INVALID;
    s->contexts      = c;
    s->context_count = count;
    return PPCP_OK;
}

ppcp_result ppcp_session_set_completeness(ppcp_session *s, ppcp_completeness c)
{
    if (s == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_enum_to_text(session_completeness_map, (int)c) == NULL)
        return PPCP_ERR_INVALID;    /* `absent` is a Capture's word, not a Session's */
    /* 5.10d / I10: asserted by the peer that owns the data.  Nothing infers it
     * from what happened to arrive. */
    s->completeness = c;
    return PPCP_OK;
}

bool ppcp_session_timebase_ref_matches(const ppcp_session *s, const char *tb)
{
    ppcp_id other;
    if (s == NULL || tb == NULL)
        return false;
    if (ppcp_id_set_z(&other, tb) != PPCP_OK)
        return false;
    return ppcp_id_equal(&s->timebase_ref, &other);
}

ppcp_result ppcp_session_validate(const ppcp_session *s)
{
    size_t i, hosts = 0;
    if (s == NULL || !ppcp_id_is_set(&s->id) || !ppcp_id_is_set(&s->timebase_ref))
        return PPCP_ERR_INVALID;
    if (ppcp_enum_to_text(session_state_map, (int)s->state) == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_enum_to_text(session_completeness_map, (int)s->completeness) == NULL)
        return PPCP_ERR_INVALID;
    for (i = 0; i < s->peer_count; i++) {
        ppcp_result rc = ppcp_peer_desc_validate(&s->peers[i]);
        if (rc != PPCP_OK)
            return rc;
        if (s->peers[i].role == PPCP_ROLE_HOST)
            hosts++;
    }
    if (hosts > 1)
        return PPCP_ERR_INVALID;                       /* I20 */
    if (s->peer_count > 0 && (hosts == 1) != s->has_arbitration)
        return PPCP_ERR_INVALID;                       /* 5.10e, the iff */
    if (s->has_arbitration &&
        (s->coincidence_window_ns <= 0 || s->issue_hold_ns <= 0))
        return PPCP_ERR_INVALID;
    for (i = 0; i < s->stream_count; i++) {
        ppcp_result rc = ppcp_stream_validate(&s->streams[i]);
        if (rc != PPCP_OK)
            return rc;
        if (!ppcp_id_equal(&s->streams[i].session_id, &s->id))
            return PPCP_ERR_INVALID;
    }
    for (i = 0; i < s->context_count; i++) {
        ppcp_result rc = ppcp_context_change_validate(&s->contexts[i]);
        if (rc != PPCP_OK)
            return rc;
    }
    if (s->epoch.present) {
        ppcp_result rc = ppcp_instant_validate(&s->epoch.at);
        if (rc != PPCP_OK)
            return rc;
    }
    /* 5.10h — cardinality 1, and expressed in `timebase_ref`. */
    {
        ppcp_result rc = ppcp_instant_validate(&s->opened_at);
        if (rc != PPCP_OK)
            return rc;
        if (!ppcp_id_equal(&s->opened_at.tb, &s->timebase_ref))
            return PPCP_ERR_INVALID;
    }
    /* I12: any subset of Streams is valid, including none.  There is
     * deliberately no minimum here. */
    return PPCP_OK;
}

static ppcp_result epoch_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_epoch *e = (const ppcp_epoch *)ctx;
    ppcp_wfield f[2];
    f[0] = ppcp_wf_int("wall_utc_ns", e->wall_utc_ns);
    f[1] = ppcp_wf_sub("at", ppcp_sub_write_instant, &e->at);
    return ppcp_rec_write(w, f, 2);
}

ppcp_result ppcp_session_epoch_write(ppcp_cbor_writer *w, const void *ctx);
ppcp_result ppcp_session_epoch_write(ppcp_cbor_writer *w, const void *ctx)
{
    return epoch_write(w, ctx);
}

static ppcp_result epoch_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_epoch *e = (ppcp_epoch *)dst;
    ppcp_rfield f[2];
    bool        a = false, b = false;
    ppcp_result rc;
    (void)ctx;
    f[0] = ppcp_rf("wall_utc_ns", PPCP_F_INT, &e->wall_utc_ns, &a);
    f[1] = ppcp_rf_sub("at", ppcp_sub_read_instant, &e->at, NULL, &b);
    rc = ppcp_rec_read(r, f, 2);
    if (rc != PPCP_OK) return rc;
    if (!a || !b) return PPCP_ERR_MALFORMED;
    e->present = true;
    return PPCP_OK;
}

ppcp_result ppcp_session_epoch_read(ppcp_cbor_reader *r, void *dst, void *ctx);
ppcp_result ppcp_session_epoch_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    return epoch_read(r, dst, ctx);
}

static ppcp_result peer_elem_write(ppcp_cbor_writer *w, const void *elem)
{
    return ppcp_peer_desc_encode(w, (const ppcp_peer_desc *)elem);
}

static ppcp_result stream_elem_write(ppcp_cbor_writer *w, const void *elem)
{
    return ppcp_stream_encode(w, (const ppcp_stream *)elem);
}

static ppcp_result context_elem_write(ppcp_cbor_writer *w, const void *elem)
{
    return ppcp_context_change_encode(w, (const ppcp_context_change *)elem);
}

static ppcp_result shot_elem_write(ppcp_cbor_writer *w, const void *elem)
{
    return ppcp_shot_encode(w, (const ppcp_shot *)elem);
}

static ppcp_result peers_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_session *s = (const ppcp_session *)ctx;
    return ppcp_rec_write_array(w, s->peers, sizeof(ppcp_peer_desc), s->peer_count,
                                peer_elem_write);
}

static ppcp_result streams_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_session *s = (const ppcp_session *)ctx;
    return ppcp_rec_write_array(w, s->streams, sizeof(ppcp_stream), s->stream_count,
                                stream_elem_write);
}

static ppcp_result contexts_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_session *s = (const ppcp_session *)ctx;
    return ppcp_rec_write_array(w, s->contexts, sizeof(ppcp_context_change),
                                s->context_count, context_elem_write);
}

static ppcp_result shots_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_session *s = (const ppcp_session *)ctx;
    return ppcp_rec_write_array(w, s->shots, sizeof(ppcp_shot), s->shot_count,
                                shot_elem_write);
}

ppcp_result ppcp_session_encode(ppcp_cbor_writer *w, const ppcp_session *s)
{
    ppcp_wfield f[13];
    size_t      n  = 0;
    ppcp_result rc = ppcp_session_validate(s);
    if (rc != PPCP_OK)
        return rc;
    f[n++] = ppcp_wf_id("id", &s->id);
    f[n++] = ppcp_wf_id("timebase_ref", &s->timebase_ref);
    f[n++] = ppcp_wf_sub("opened_at", ppcp_sub_write_instant, &s->opened_at);
    f[n++] = ppcp_wf_enum("state", session_state_map, (int)s->state);
    f[n++] = ppcp_wf_enum("completeness", session_completeness_map, (int)s->completeness);
    f[n++] = ppcp_wf_sub("peers", peers_write, s);
    f[n++] = ppcp_wf_sub("streams", streams_write, s);
    f[n++] = ppcp_wf_sub("shots", shots_write, s);
    f[n++] = ppcp_wf_sub("contexts", contexts_write, s);
    if (s->epoch.present)
        f[n++] = ppcp_wf_sub("epoch", epoch_write, &s->epoch);
    if (s->has_arbitration) {
        f[n++] = ppcp_wf_int("coincidence_window_ns", s->coincidence_window_ns);
        f[n++] = ppcp_wf_int("issue_hold_ns", s->issue_hold_ns);
    }
    if (s->has_heartbeat_interval)
        f[n++] = ppcp_wf_uint("heartbeat_interval_ms", s->heartbeat_interval_ms);
    return ppcp_rec_write(w, f, n);
}

typedef struct sess_read_ctx {
    ppcp_arena   *arena;
    ppcp_session *out;
} sess_read_ctx;

static ppcp_result peer_elem_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    return ppcp_peer_desc_decode(r, (ppcp_arena *)ctx, (ppcp_peer_desc *)dst);
}

static ppcp_result stream_elem_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    (void)ctx;
    return ppcp_stream_decode(r, (ppcp_stream *)dst);
}

static ppcp_result context_elem_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    (void)ctx;
    return ppcp_context_change_decode(r, (ppcp_context_change *)dst);
}

static ppcp_result shot_elem_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    (void)ctx;
    return ppcp_shot_decode(r, (ppcp_shot *)dst);
}

static ppcp_result peers_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    sess_read_ctx *c = (sess_read_ctx *)ctx;
    void *base = NULL; size_t count = 0;
    ppcp_result rc;
    (void)dst;
    rc = ppcp_rec_read_array_arena(r, c->arena, sizeof(ppcp_peer_desc), sizeof(void *),
                                   &base, &count, peer_elem_read, c->arena);
    if (rc != PPCP_OK) return rc;
    c->out->peers      = (const ppcp_peer_desc *)base;
    c->out->peer_count = count;
    return PPCP_OK;
}

static ppcp_result streams_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    sess_read_ctx *c = (sess_read_ctx *)ctx;
    void *base = NULL; size_t count = 0;
    ppcp_result rc;
    (void)dst;
    rc = ppcp_rec_read_array_arena(r, c->arena, sizeof(ppcp_stream), sizeof(void *),
                                   &base, &count, stream_elem_read, NULL);
    if (rc != PPCP_OK) return rc;
    c->out->streams      = (const ppcp_stream *)base;
    c->out->stream_count = count;
    return PPCP_OK;
}

static ppcp_result contexts_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    sess_read_ctx *c = (sess_read_ctx *)ctx;
    void *base = NULL; size_t count = 0;
    ppcp_result rc;
    (void)dst;
    rc = ppcp_rec_read_array_arena(r, c->arena, sizeof(ppcp_context_change), sizeof(void *),
                                   &base, &count, context_elem_read, NULL);
    if (rc != PPCP_OK) return rc;
    c->out->contexts      = (const ppcp_context_change *)base;
    c->out->context_count = count;
    return PPCP_OK;
}

static ppcp_result shots_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    sess_read_ctx *c = (sess_read_ctx *)ctx;
    void *base = NULL; size_t count = 0;
    ppcp_result rc;
    (void)dst;
    rc = ppcp_rec_read_array_arena(r, c->arena, sizeof(ppcp_shot), sizeof(void *),
                                   &base, &count, shot_elem_read, NULL);
    if (rc != PPCP_OK) return rc;
    c->out->shots      = (const ppcp_shot *)base;
    c->out->shot_count = count;
    return PPCP_OK;
}

ppcp_result ppcp_session_decode(ppcp_cbor_reader *r, ppcp_arena *a, ppcp_session *out)
{
    ppcp_rfield   f[13];
    size_t        n = 0;
    sess_read_ctx ctx;
    bool          s_id = false, s_tb = false, s_state = false, s_comp = false;
    bool          s_open = false;
    bool          s_cw = false, s_ih = false;
    int           state = 0, comp = 0;
    uint64_t      hb = 0;
    ppcp_result   rc;

    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    ctx.arena = a;
    ctx.out   = out;

    f[n++] = ppcp_rf("id", PPCP_F_ID, &out->id, &s_id);
    f[n++] = ppcp_rf("timebase_ref", PPCP_F_ID, &out->timebase_ref, &s_tb);
    f[n++] = ppcp_rf_sub("opened_at", ppcp_sub_read_instant, &out->opened_at, NULL, &s_open);
    f[n++] = ppcp_rf_enum("state", session_state_map, &state, &s_state);
    f[n++] = ppcp_rf_enum("completeness", session_completeness_map, &comp, &s_comp);
    f[n++] = ppcp_rf_sub("peers", peers_read, NULL, &ctx, NULL);
    f[n++] = ppcp_rf_sub("streams", streams_read, NULL, &ctx, NULL);
    f[n++] = ppcp_rf_sub("shots", shots_read, NULL, &ctx, NULL);
    f[n++] = ppcp_rf_sub("contexts", contexts_read, NULL, &ctx, NULL);
    f[n++] = ppcp_rf_sub("epoch", epoch_read, &out->epoch, NULL, NULL);
    f[n++] = ppcp_rf("coincidence_window_ns", PPCP_F_INT, &out->coincidence_window_ns, &s_cw);
    f[n++] = ppcp_rf("issue_hold_ns", PPCP_F_INT, &out->issue_hold_ns, &s_ih);
    f[n++] = ppcp_rf("heartbeat_interval_ms", PPCP_F_UINT, &hb, &out->has_heartbeat_interval);

    rc = ppcp_rec_read(r, f, n);
    if (rc != PPCP_OK)
        return rc;
    if (!s_id || !s_tb || !s_state || !s_comp || !s_open)
        return PPCP_ERR_MALFORMED;   /* 5.10h: cardinality 1 */
    /* 5.10e both ways: one parameter without the other is malformed whatever
     * the roster says, because they are two halves of one statement. */
    if (s_cw != s_ih)
        return PPCP_ERR_MALFORMED;
    out->has_arbitration = s_cw;
    out->state           = (ppcp_session_state)state;
    out->completeness    = (ppcp_completeness)comp;
    if (out->has_heartbeat_interval) {
        if (hb == 0 || hb > 0xFFFFFFFFu)
            return PPCP_ERR_MALFORMED;
        out->heartbeat_interval_ms = (uint32_t)hb;
    }
    if (ppcp_session_validate(out) != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}
