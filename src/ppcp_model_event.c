/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * PPCP-CORE §5.12–§5.18 — Candidate, Shot, Capture, Readiness, ShotLink,
 * SessionLink and Annotation.  Work package L4.
 */
#include "ppcp/model.h"
#include "ppcp_codec.h"

#include <string.h>

static const ppcp_enum_map authority_map[] = {
    { "host",   PPCP_AUTHORITY_HOST   },
    { "device", PPCP_AUTHORITY_DEVICE },
    { NULL, 0 }
};

static const ppcp_enum_map transfer_map[] = {
    { "pending",   PPCP_TRANSFER_PENDING   },
    { "in_flight", PPCP_TRANSFER_IN_FLIGHT },
    { "present",   PPCP_TRANSFER_PRESENT   },
    { "confirmed", PPCP_TRANSFER_CONFIRMED },
    { "failed",    PPCP_TRANSFER_FAILED    },
    { NULL, 0 }
};

static const ppcp_enum_map confirmed_by_map[] = {
    { "observer", PPCP_CONFIRMED_BY_OBSERVER },
    { "user",     PPCP_CONFIRMED_BY_USER     },
    { NULL, 0 }
};

static const ppcp_enum_map annot_provenance_map[] = {
    { "user",            PPCP_ANNOT_USER            },
    { "device_advisory", PPCP_ANNOT_DEVICE_ADVISORY },
    { NULL, 0 }
};

static const ppcp_enum_map relation_class_map[] = {
    { "affine",    PPCP_REL_AFFINE    },
    { "unrelated", PPCP_REL_UNRELATED },
    { NULL, 0 }
};

const ppcp_enum_map *ppcp_transfer_enum_map(void)  { return transfer_map; }
const ppcp_enum_map *ppcp_authority_enum_map(void) { return authority_map; }

/* ------------------------------------------------------------- Candidate */

ppcp_result ppcp_candidate_make(ppcp_candidate *out, const char *id, const char *peer_id,
                                const char *source_id, const char *basis,
                                const ppcp_instant *at, double confidence)
{
    ppcp_result rc;
    if (out == NULL || at == NULL)
        return PPCP_ERR_INVALID;
    if (!(confidence >= 0.0) || !(confidence <= 1.0))
        return PPCP_ERR_INVALID;
    if (ppcp_instant_validate(at) != PPCP_OK)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    rc = ppcp_id_set_z(&out->id, id);                if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->peer_id, peer_id);      if (rc != PPCP_OK) return rc;
    /* 5.12a / I26: `source_id` is MANDATORY and names a Source with a declared
     * Timebase.  An optional one would strand a Candidate with no calibration
     * to apply — and for an acoustic candidate, calibration is where the
     * surveyed geometry lives (8.1). */
    rc = ppcp_id_set_z(&out->source_id, source_id);  if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->basis, basis);          if (rc != PPCP_OK) return rc;
    /* 5.12e / I33: `at` is the CANONICAL instant, converted by the nominator
     * before emission.  This constructor takes it already converted because
     * there is no route from a Candidate to the exposure of the frame it came
     * from, so a consumer could not do it and must not try. */
    out->at         = *at;
    out->confidence = confidence;
    return PPCP_OK;
}

ppcp_result ppcp_candidate_set_tof_correction(ppcp_candidate *c, const ppcp_estimate *e)
{
    ppcp_result rc;
    if (c == NULL || e == NULL)
        return PPCP_ERR_INVALID;
    /* I29 / 5.12d: both `value_ns` and `sigma_ns`, and the parameter type is
     * the guarantee — ppcp_estimate_make is the only way to obtain one and it
     * takes both.  Time of flight is a CONVERGING estimate, and its sigma is
     * the only way a consumer knows where in that convergence a shot sits. */
    rc = ppcp_estimate_validate(e);
    if (rc != PPCP_OK)
        return rc;
    c->has_tof_correction = true;
    c->tof_correction     = *e;
    return PPCP_OK;
}

ppcp_result ppcp_candidate_set_canonical_correction(ppcp_candidate *c, int64_t ns)
{
    if (c == NULL)
        return PPCP_ERR_INVALID;
    /* 5.12f: deliberately a bare integer while `tof_correction` beside it is an
     * Estimate.  The canonical correction is arithmetic over declared values,
     * and its trustworthiness is `frame_start_to_exposure_offset_provenance`,
     * one hop away through `source_id` (I31).  A per-candidate sigma here would
     * duplicate that and invite a peer to invent one. */
    c->has_canonical_correction = true;
    c->canonical_correction_ns  = ns;
    return PPCP_OK;
}

ppcp_result ppcp_candidate_set_classifier(ppcp_candidate *c, const uint8_t *map, size_t len)
{
    if (c == NULL || map == NULL || len == 0)
        return PPCP_ERR_INVALID;
    c->classifier     = map;
    c->classifier_len = len;
    return PPCP_OK;
}

ppcp_result ppcp_candidate_set_evidence(ppcp_candidate *c, const char *capture_id)
{
    ppcp_result rc;
    if (c == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_id_set_z(&c->evidence_capture_id, capture_id);
    if (rc != PPCP_OK)
        return rc;
    c->has_evidence_capture_id = true;
    return PPCP_OK;
}

ppcp_result ppcp_candidate_validate(const ppcp_candidate *c)
{
    if (c == NULL)
        return PPCP_ERR_INVALID;
    if (!ppcp_id_is_set(&c->id) || !ppcp_id_is_set(&c->peer_id) ||
        !ppcp_id_is_set(&c->source_id) || !ppcp_id_is_set(&c->basis))
        return PPCP_ERR_INVALID;
    if (!(c->confidence >= 0.0) || !(c->confidence <= 1.0))
        return PPCP_ERR_INVALID;
    if (c->has_tof_correction) {
        ppcp_result rc = ppcp_estimate_validate(&c->tof_correction);
        if (rc != PPCP_OK)
            return rc;
    }
    return ppcp_instant_validate(&c->at);
}

/* The classifier map is basis-specific and opaque (5.12b): it is interpreted
 * only in the context of `basis`, so this library carries the bytes and never
 * looks inside. */
typedef struct raw_map_ref { const uint8_t *p; size_t n; } raw_map_ref;

static ppcp_result raw_map_write(ppcp_cbor_writer *w, const void *ctx);
static ppcp_result raw_map_read(ppcp_cbor_reader *r, void *dst, void *ctx);

ppcp_result ppcp_candidate_encode(ppcp_cbor_writer *w, const ppcp_candidate *c)
{
    ppcp_wfield f[9];
    raw_map_ref cls;
    size_t      n  = 0;
    ppcp_result rc = ppcp_candidate_validate(c);
    if (rc != PPCP_OK)
        return rc;
    cls.p = c->classifier;
    cls.n = c->classifier_len;
    f[n++] = ppcp_wf_id("id", &c->id);
    f[n++] = ppcp_wf_id("peer_id", &c->peer_id);
    f[n++] = ppcp_wf_id("source_id", &c->source_id);
    f[n++] = ppcp_wf_id("basis", &c->basis);
    f[n++] = ppcp_wf_sub("at", ppcp_sub_write_instant, &c->at);
    f[n++] = ppcp_wf_double("confidence", c->confidence);
    if (c->has_tof_correction)
        f[n++] = ppcp_wf_sub("tof_correction", ppcp_sub_write_estimate, &c->tof_correction);
    if (c->has_canonical_correction)
        f[n++] = ppcp_wf_int("canonical_correction_ns", c->canonical_correction_ns);
    if (c->classifier != NULL)
        f[n++] = ppcp_wf_sub("classifier", raw_map_write, &cls);
    {
        ppcp_wfield g[10];
        size_t i;
        for (i = 0; i < n; i++) g[i] = f[i];
        if (c->has_evidence_capture_id)
            g[n++] = ppcp_wf_id("evidence_capture_id", &c->evidence_capture_id);
        return ppcp_rec_write(w, g, n);
    }
}

ppcp_result ppcp_candidate_decode(ppcp_cbor_reader *r, ppcp_candidate *out)
{
    ppcp_rfield f[10];
    raw_map_ref cls;
    size_t      n = 0;
    bool        s_id = false, s_peer = false, s_src = false, s_basis = false;
    bool        s_at = false, s_conf = false, s_cls = false;
    ppcp_result rc;

    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    memset(&cls, 0, sizeof(cls));

    f[n++] = ppcp_rf("id", PPCP_F_ID, &out->id, &s_id);
    f[n++] = ppcp_rf("peer_id", PPCP_F_ID, &out->peer_id, &s_peer);
    f[n++] = ppcp_rf("source_id", PPCP_F_ID, &out->source_id, &s_src);
    f[n++] = ppcp_rf("basis", PPCP_F_ID, &out->basis, &s_basis);
    f[n++] = ppcp_rf_sub("at", ppcp_sub_read_instant, &out->at, NULL, &s_at);
    f[n++] = ppcp_rf("confidence", PPCP_F_DOUBLE, &out->confidence, &s_conf);
    f[n++] = ppcp_rf_sub("tof_correction", ppcp_sub_read_estimate, &out->tof_correction,
                         NULL, &out->has_tof_correction);
    f[n++] = ppcp_rf("canonical_correction_ns", PPCP_F_INT, &out->canonical_correction_ns,
                     &out->has_canonical_correction);
    f[n++] = ppcp_rf_sub("classifier", raw_map_read, &cls, NULL, &s_cls);
    f[n++] = ppcp_rf("evidence_capture_id", PPCP_F_ID, &out->evidence_capture_id,
                     &out->has_evidence_capture_id);

    rc = ppcp_rec_read(r, f, n);
    if (rc != PPCP_OK)
        return rc;
    /* An unknown `basis` is NOT fatal (I13, 10.3a): it decoded into a string
     * and this validator does not consult a list of known values. */
    if (!s_id || !s_peer || !s_src || !s_basis || !s_at || !s_conf)
        return PPCP_ERR_MALFORMED;
    if (s_cls) {
        out->classifier     = cls.p;
        out->classifier_len = cls.n;
    }
    if (ppcp_candidate_validate(out) != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

/* ------------------------------------------------------------------ Shot */

ppcp_result ppcp_shot_make(ppcp_shot *out, const char *id, const char *session_id,
                           const ppcp_instant *t0, ppcp_authority authority,
                           const char *issued_by, const char *first_candidate_id)
{
    ppcp_result rc;
    if (out == NULL || t0 == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_enum_to_text(authority_map, (int)authority) == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_instant_validate(t0) != PPCP_OK)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    rc = ppcp_id_set_z(&out->id, id);                  if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->session_id, session_id);  if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->issued_by, issued_by);    if (rc != PPCP_OK) return rc;
    /* 5.13a / I6: every Shot references at least one Candidate somewhere in
     * the Session, so the first one is a constructor parameter rather than
     * something to remember to add.  A Shot MAY have zero from any given peer. */
    rc = ppcp_id_set_z(&out->candidates[0], first_candidate_id);
    if (rc != PPCP_OK) return rc;
    out->candidate_count = 1;
    /* I7: `t0` is set here and there is no setter.  A late Candidate attaches;
     * it does not move t0, because revision would invalidate captures already
     * extracted against it. */
    out->t0        = *t0;
    out->authority = authority;
    return PPCP_OK;
}

ppcp_result ppcp_shot_add_capture(ppcp_shot *s, const char *capture_id)
{
    ppcp_result rc;
    size_t      i;
    ppcp_id     id;
    if (s == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_id_set_z(&id, capture_id);
    if (rc != PPCP_OK)
        return rc;
    for (i = 0; i < s->capture_count; i++) {
        if (ppcp_id_equal(&s->captures[i], &id))
            return PPCP_OK;      /* idempotent, like candidate attachment */
    }
    if (s->capture_count >= PPCP_SHOT_MAX_CAPTURES)
        return PPCP_ERR_LIMIT;
    s->captures[s->capture_count++] = id;
    return PPCP_OK;
}

ppcp_result ppcp_shot_validate(const ppcp_shot *s)
{
    if (s == NULL)
        return PPCP_ERR_INVALID;
    if (!ppcp_id_is_set(&s->id) || !ppcp_id_is_set(&s->session_id) ||
        !ppcp_id_is_set(&s->issued_by))
        return PPCP_ERR_INVALID;
    if (ppcp_enum_to_text(authority_map, (int)s->authority) == NULL)
        return PPCP_ERR_INVALID;
    if (s->candidate_count == 0)
        return PPCP_ERR_INVALID;    /* 5.13a / I6: `candidates` is 1..n */
    if (s->candidate_count > PPCP_SHOT_MAX_CANDIDATES ||
        s->capture_count > PPCP_SHOT_MAX_CAPTURES)
        return PPCP_ERR_INVALID;
    return ppcp_instant_validate(&s->t0);
}

typedef struct idarr_ref { const ppcp_id *v; size_t n; } idarr_ref;

static ppcp_result idarr_write(ppcp_cbor_writer *w, const void *ctx)
{
    const idarr_ref *a = (const idarr_ref *)ctx;
    return ppcp_rec_write_array(w, a->v, sizeof(ppcp_id), a->n, ppcp_elem_write_id);
}

typedef struct idarr_dst { ppcp_id *v; size_t cap; size_t *count; } idarr_dst;

static ppcp_result idarr_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    idarr_dst *d = (idarr_dst *)dst;
    (void)ctx;
    return ppcp_rec_read_array(r, d->v, sizeof(ppcp_id), d->cap, d->count,
                               ppcp_sub_read_id_elem, NULL);
}

ppcp_result ppcp_shot_encode(ppcp_cbor_writer *w, const ppcp_shot *s)
{
    ppcp_wfield f[7];
    idarr_ref   cands, caps;
    size_t      n  = 0;
    ppcp_result rc = ppcp_shot_validate(s);
    if (rc != PPCP_OK)
        return rc;
    cands.v = s->candidates; cands.n = s->candidate_count;
    caps.v  = s->captures;   caps.n  = s->capture_count;
    f[n++] = ppcp_wf_id("id", &s->id);
    f[n++] = ppcp_wf_id("session_id", &s->session_id);
    f[n++] = ppcp_wf_sub("t0", ppcp_sub_write_instant, &s->t0);
    f[n++] = ppcp_wf_enum("authority", authority_map, (int)s->authority);
    f[n++] = ppcp_wf_id("issued_by", &s->issued_by);
    /* 5.13a: ALL of them, always — winners and losers.  Arbitration is a
     * conclusion; candidates are the evidence (I8). */
    f[n++] = ppcp_wf_sub("candidates", idarr_write, &cands);
    f[n++] = ppcp_wf_sub("captures", idarr_write, &caps);
    return ppcp_rec_write(w, f, n);
}

ppcp_result ppcp_shot_decode(ppcp_cbor_reader *r, ppcp_shot *out)
{
    ppcp_rfield f[7];
    idarr_dst   cands, caps;
    size_t      n = 0;
    bool        s_id = false, s_sid = false, s_t0 = false, s_auth = false;
    bool        s_by = false, s_cands = false;
    int         auth = 0;
    ppcp_result rc;

    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    cands.v = out->candidates; cands.cap = PPCP_SHOT_MAX_CANDIDATES;
    cands.count = &out->candidate_count;
    caps.v  = out->captures;   caps.cap  = PPCP_SHOT_MAX_CAPTURES;
    caps.count  = &out->capture_count;

    f[n++] = ppcp_rf("id", PPCP_F_ID, &out->id, &s_id);
    f[n++] = ppcp_rf("session_id", PPCP_F_ID, &out->session_id, &s_sid);
    f[n++] = ppcp_rf_sub("t0", ppcp_sub_read_instant, &out->t0, NULL, &s_t0);
    f[n++] = ppcp_rf_enum("authority", authority_map, &auth, &s_auth);
    f[n++] = ppcp_rf("issued_by", PPCP_F_ID, &out->issued_by, &s_by);
    f[n++] = ppcp_rf_sub("candidates", idarr_read, &cands, NULL, &s_cands);
    f[n++] = ppcp_rf_sub("captures", idarr_read, &caps, NULL, NULL);

    rc = ppcp_rec_read(r, f, n);
    if (rc != PPCP_OK)
        return rc;
    if (!s_id || !s_sid || !s_t0 || !s_auth || !s_by || !s_cands)
        return PPCP_ERR_MALFORMED;
    out->authority = (ppcp_authority)auth;
    if (ppcp_shot_validate(out) != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

/* ---------------------------------------------------------------- Anchor */

ppcp_result ppcp_anchor_shot(ppcp_anchor *out, const char *shot_id)
{
    ppcp_result rc;
    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    rc = ppcp_id_set_z(&out->id, shot_id);
    if (rc != PPCP_OK)
        return rc;
    out->kind = PPCP_ANCHOR_SHOT;
    return PPCP_OK;
}

ppcp_result ppcp_anchor_candidate(ppcp_anchor *out, const char *candidate_id)
{
    ppcp_result rc;
    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    rc = ppcp_id_set_z(&out->id, candidate_id);
    if (rc != PPCP_OK)
        return rc;
    out->kind = PPCP_ANCHOR_CANDIDATE;
    return PPCP_OK;
}

ppcp_result ppcp_anchor_stream(ppcp_anchor *out)
{
    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    out->kind = PPCP_ANCHOR_STREAM;
    return PPCP_OK;
}

ppcp_result ppcp_anchor_encode(ppcp_cbor_writer *w, const ppcp_anchor *a)
{
    ppcp_wfield f[1];
    if (a == NULL)
        return PPCP_ERR_INVALID;
    /* ENC 4.1: a map with EXACTLY one key.  One field, always. */
    switch (a->kind) {
    case PPCP_ANCHOR_SHOT:
        if (!ppcp_id_is_set(&a->id)) return PPCP_ERR_INVALID;
        f[0] = ppcp_wf_id("shot_id", &a->id);
        break;
    case PPCP_ANCHOR_CANDIDATE:
        if (!ppcp_id_is_set(&a->id)) return PPCP_ERR_INVALID;
        f[0] = ppcp_wf_id("candidate_id", &a->id);
        break;
    case PPCP_ANCHOR_STREAM:
        f[0] = ppcp_wf_bool("stream", true);
        break;
    default:
        return PPCP_ERR_INVALID;
    }
    return ppcp_rec_write(w, f, 1);
}

ppcp_result ppcp_anchor_decode(ppcp_cbor_reader *r, ppcp_anchor *out)
{
    ppcp_cbor_item it;
    ppcp_result    rc;
    uint32_t       i;
    unsigned       keys = 0;

    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    rc = ppcp_cbor_read(r, &it);
    if (rc != PPCP_OK) return rc;
    if (it.type != PPCP_CBOR_MAP) return PPCP_ERR_MALFORMED;

    for (i = 0; i < it.count; i++) {
        const char *k; size_t klen; ppcp_cbor_item v;
        rc = ppcp_cbor_read_key(r, &k, &klen);
        if (rc != PPCP_OK) return rc;
        if (ppcp_cbor_key_is(k, klen, "shot_id") ||
            ppcp_cbor_key_is(k, klen, "candidate_id")) {
            rc = ppcp_cbor_read(r, &v);
            if (rc != PPCP_OK) return rc;
            if (v.type != PPCP_CBOR_TEXT) return PPCP_ERR_MALFORMED;
            if (ppcp_id_set(&out->id, (const char *)v.bytes, v.len) != PPCP_OK)
                return PPCP_ERR_MALFORMED;
            out->kind = ppcp_cbor_key_is(k, klen, "shot_id") ? PPCP_ANCHOR_SHOT
                                                             : PPCP_ANCHOR_CANDIDATE;
            keys++;
        } else if (ppcp_cbor_key_is(k, klen, "stream")) {
            rc = ppcp_cbor_read(r, &v);
            if (rc != PPCP_OK) return rc;
            if (v.type != PPCP_CBOR_BOOL || !v.b) return PPCP_ERR_MALFORMED;
            out->kind = PPCP_ANCHOR_STREAM;
            keys++;
        } else {
            /* An unknown key is still skipped (I13); it is the count of ANCHOR
             * keys that must be one, not the count of keys in the map. */
            rc = ppcp_cbor_skip(r);
            if (rc != PPCP_OK) return rc;
        }
    }
    /* I27 made structural on the way in: zero keys and more than one are both
     * malformed (ENC 4.1). */
    if (keys != 1)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

/* --------------------------------------------------------------- Capture */

static ppcp_result capture_make_common(ppcp_capture *out, const char *id,
                                       const char *stream_id, ppcp_completeness completeness)
{
    ppcp_result rc;
    if (out == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_enum_to_text(ppcp_capture_completeness_enum_map(), (int)completeness) == NULL)
        return PPCP_ERR_INVALID;   /* `unknown` is a Session's word, not a Capture's */
    rc = ppcp_id_set_z(&out->id, id);
    if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->stream_id, stream_id);
    if (rc != PPCP_OK) return rc;
    out->completeness = completeness;
    out->transfer     = PPCP_TRANSFER_PENDING;   /* the owner's view (5.14f) */
    return PPCP_OK;
}

ppcp_result ppcp_capture_make_shot(ppcp_capture *out, const char *id, const char *shot_id,
                                   const char *stream_id, ppcp_completeness completeness)
{
    ppcp_result rc;
    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    rc = ppcp_anchor_shot(&out->anchor, shot_id);
    if (rc != PPCP_OK)
        return rc;
    return capture_make_common(out, id, stream_id, completeness);
}

ppcp_result ppcp_capture_make_candidate(ppcp_capture *out, const char *id,
                                        const char *candidate_id, const char *stream_id,
                                        ppcp_completeness completeness)
{
    ppcp_result rc;
    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    rc = ppcp_anchor_candidate(&out->anchor, candidate_id);
    if (rc != PPCP_OK)
        return rc;
    return capture_make_common(out, id, stream_id, completeness);
}

ppcp_result ppcp_capture_make_segment(ppcp_capture *out, const char *id, const char *stream_id,
                                      ppcp_completeness completeness,
                                      const ppcp_interval *interval)
{
    ppcp_result rc;
    if (out == NULL || interval == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_interval_validate(interval) != PPCP_OK)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    rc = ppcp_anchor_stream(&out->anchor);
    if (rc != PPCP_OK)
        return rc;
    rc = capture_make_common(out, id, stream_id, completeness);
    if (rc != PPCP_OK)
        return rc;
    /* 5.14d: on a stream-anchored Capture `interval` is mandatory INCLUDING
     * when `completeness: absent` — a segment with no interval says nothing
     * about what it covers, and an absent segment WITH one is how a peer states
     * that a named span was not recorded.  So it is a parameter here. */
    out->has_interval = true;
    out->interval     = *interval;
    return PPCP_OK;
}

ppcp_result ppcp_capture_set_interval(ppcp_capture *c, const ppcp_interval *iv)
{
    if (c == NULL || iv == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_interval_validate(iv) != PPCP_OK)
        return PPCP_ERR_INVALID;
    c->has_interval = true;
    c->interval     = *iv;
    return PPCP_OK;
}

ppcp_result ppcp_capture_set_absent_reason(ppcp_capture *c, const char *reason)
{
    ppcp_result rc;
    if (c == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_id_set_z(&c->absent_reason, reason);
    if (rc != PPCP_OK)
        return rc;
    c->has_absent_reason = true;
    return PPCP_OK;
}

ppcp_result ppcp_capture_add_gap(ppcp_capture *c, const ppcp_interval *gap)
{
    if (c == NULL || gap == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_interval_validate(gap) != PPCP_OK)
        return PPCP_ERR_INVALID;
    if (c->gap_count >= PPCP_CAPTURE_MAX_GAPS)
        return PPCP_ERR_LIMIT;
    c->gaps[c->gap_count++] = *gap;
    return PPCP_OK;
}

ppcp_result ppcp_capture_set_summary(ppcp_capture *c, const ppcp_achieved_summary *s)
{
    ppcp_result rc;
    if (c == NULL || s == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_achieved_summary_validate(s);
    if (rc != PPCP_OK)
        return rc;
    c->has_achieved_summary = true;
    c->achieved_summary     = *s;
    return PPCP_OK;
}

ppcp_result ppcp_capture_set_digest(ppcp_capture *c, const ppcp_digest *d, uint64_t bytes)
{
    if (c == NULL || d == NULL || !d->present)
        return PPCP_ERR_INVALID;
    c->digest    = *d;
    c->has_bytes = true;
    c->bytes     = bytes;
    return PPCP_OK;
}

ppcp_result ppcp_capture_set_transfer(ppcp_capture *c, ppcp_transfer_state t)
{
    if (c == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_enum_to_text(transfer_map, (int)t) == NULL)
        return PPCP_ERR_INVALID;
    /* 8.4b / 5.14f: an owner MUST NOT set `confirmed` on its own authority.
     * Only the receiver can say it, which is the whole reason
     * `capture_committed` exists — so `confirmed` is reachable only through
     * ppcp_transfer_on_committed(), which needs one to call. */
    if (t == PPCP_TRANSFER_CONFIRMED)
        return PPCP_ERR_INVALID;
    c->transfer = t;
    return PPCP_OK;
}

/* Internal: the one path to `confirmed`, used by transfer.c on receipt of a
 * `capture_committed` from the receiver. */
ppcp_result ppcp_capture_mark_confirmed(ppcp_capture *c)
{
    if (c == NULL)
        return PPCP_ERR_INVALID;
    c->transfer = PPCP_TRANSFER_CONFIRMED;
    return PPCP_OK;
}

ppcp_result ppcp_capture_validate(const ppcp_capture *c)
{
    size_t i;
    if (c == NULL || !ppcp_id_is_set(&c->id) || !ppcp_id_is_set(&c->stream_id))
        return PPCP_ERR_INVALID;
    if (ppcp_enum_to_text(ppcp_capture_completeness_enum_map(), (int)c->completeness) == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_enum_to_text(transfer_map, (int)c->transfer) == NULL)
        return PPCP_ERR_INVALID;
    if (c->anchor.kind != PPCP_ANCHOR_STREAM && !ppcp_id_is_set(&c->anchor.id))
        return PPCP_ERR_INVALID;

    if (c->anchor.kind == PPCP_ANCHOR_STREAM) {
        /* 5.14d: always mandatory on a segment, absent or not. */
        if (!c->has_interval)
            return PPCP_ERR_INVALID;
    }
    /* 5.14d1 (erratum E12): an `absent` Capture MAY carry `interval` whatever
     * its anchor, and SHOULD where the peer knows the span it could not
     * supply.  8.4b's answer to an orphan capture request is SHOT-anchored and
     * `absent`, and the pre-erratum rule forbade the one field that says which
     * span left the buffer — so a device could report the cause and not the
     * extent. */
    /* 5.14 / I10: `absent_reason` is mandatory when absent, and meaningless
     * otherwise.  Absence is ASSERTED, never inferred from a missing payload. */
    if (c->completeness == PPCP_ABSENT) {
        if (!c->has_absent_reason)
            return PPCP_ERR_INVALID;
    } else if (c->has_absent_reason) {
        return PPCP_ERR_INVALID;
    }
    if (c->gap_count > PPCP_CAPTURE_MAX_GAPS)
        return PPCP_ERR_INVALID;
    for (i = 0; i < c->gap_count; i++) {
        ppcp_result rc = ppcp_interval_validate(&c->gaps[i]);
        if (rc != PPCP_OK)
            return rc;
        if (c->has_interval) {
            if (!ppcp_id_equal(&c->gaps[i].tb, &c->interval.tb))
                return PPCP_ERR_INVALID;
            /* I11: a gap is loss INSIDE a segment that otherwise exists, so it
             * lies within the Capture's own interval. */
            if (c->gaps[i].start_ns < c->interval.start_ns ||
                c->gaps[i].end_ns > c->interval.end_ns)
                return PPCP_ERR_INVALID;
        }
    }
    if (c->has_achieved_summary) {
        ppcp_result rc = ppcp_achieved_summary_validate(&c->achieved_summary);
        if (rc != PPCP_OK)
            return rc;
    }
    return PPCP_OK;
}

ppcp_result ppcp_capture_validate_in_stream(const ppcp_capture *c, const ppcp_stream *s)
{
    ppcp_result rc = ppcp_capture_validate(c);
    if (rc != PPCP_OK)
        return rc;
    if (s == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_stream_validate(s);
    if (rc != PPCP_OK)
        return rc;
    if (!ppcp_id_equal(&c->stream_id, &s->id))
        return PPCP_ERR_INVALID;
    /* 5.14d: `{stream: true}` is permitted only on a Stream whose `continuity`
     * is `continuous`.  This is CT-I27's second assertion. */
    if (c->anchor.kind == PPCP_ANCHOR_STREAM && s->continuity != PPCP_CONTINUOUS)
        return PPCP_ERR_INVALID;
    /* I11: gaps are meaningful only on `continuous` streams. */
    if (c->gap_count > 0 && s->continuity != PPCP_CONTINUOUS)
        return PPCP_ERR_INVALID;
    if (c->has_interval && !ppcp_id_equal(&c->interval.tb, &s->timebase_id))
        return PPCP_ERR_INVALID;
    /* 5.11j / MSG 8.1i: a preview Capture is LIVE-ONLY.  A consumer never sees
     * `transfer: pending` on one — what could not be delivered promptly is
     * discarded and announced absent with `not_retained`. */
    if (ppcp_stream_is_preview(s) && c->transfer == PPCP_TRANSFER_PENDING &&
        c->completeness != PPCP_ABSENT)
        return PPCP_ERR_INVALID;
    return PPCP_OK;
}

static ppcp_result gaps_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_capture *c = (const ppcp_capture *)ctx;
    return ppcp_rec_write_array(w, c->gaps, sizeof(ppcp_interval), c->gap_count,
                                ppcp_elem_write_interval);
}

static ppcp_result summary_write(ppcp_cbor_writer *w, const void *ctx)
{
    return ppcp_achieved_summary_encode(w, (const ppcp_achieved_summary *)ctx);
}

static ppcp_result anchor_write(ppcp_cbor_writer *w, const void *ctx)
{
    return ppcp_anchor_encode(w, (const ppcp_anchor *)ctx);
}

static ppcp_result digest_write(ppcp_cbor_writer *w, const void *ctx)
{
    return ppcp_digest_encode(w, (const ppcp_digest *)ctx);
}

ppcp_result ppcp_capture_encode(ppcp_cbor_writer *w, const ppcp_capture *c)
{
    ppcp_wfield f[11];
    size_t      n  = 0;
    ppcp_result rc = ppcp_capture_validate(c);
    if (rc != PPCP_OK)
        return rc;
    f[n++] = ppcp_wf_id("id", &c->id);
    f[n++] = ppcp_wf_sub("anchor", anchor_write, &c->anchor);
    f[n++] = ppcp_wf_id("stream_id", &c->stream_id);
    f[n++] = ppcp_wf_enum("completeness", ppcp_capture_completeness_enum_map(),
                          (int)c->completeness);
    f[n++] = ppcp_wf_enum("transfer", transfer_map, (int)c->transfer);
    if (c->has_interval)
        f[n++] = ppcp_wf_sub("interval", ppcp_sub_write_interval, &c->interval);
    if (c->has_absent_reason)
        f[n++] = ppcp_wf_id("absent_reason", &c->absent_reason);
    if (c->gap_count > 0)
        f[n++] = ppcp_wf_sub("gaps", gaps_write, c);
    if (c->has_achieved_summary)
        f[n++] = ppcp_wf_sub("achieved_summary", summary_write, &c->achieved_summary);
    if (c->digest.present)
        f[n++] = ppcp_wf_sub("digest", digest_write, &c->digest);
    if (c->has_bytes)
        f[n++] = ppcp_wf_uint("bytes", c->bytes);
    /* I30 / 8.1b: there is deliberately no `achieved_frames` key here.  The
     * per-frame series travel with the payload they describe, which is also the
     * only context in which they are interpretable. */
    return ppcp_rec_write(w, f, n);
}

typedef struct gaps_dst { ppcp_capture *c; } gaps_dst;

static ppcp_result gaps_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_capture *c = (ppcp_capture *)dst;
    (void)ctx;
    return ppcp_rec_read_array(r, c->gaps, sizeof(ppcp_interval), PPCP_CAPTURE_MAX_GAPS,
                               &c->gap_count, ppcp_sub_read_interval, NULL);
}

static ppcp_result anchor_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    (void)ctx;
    return ppcp_anchor_decode(r, (ppcp_anchor *)dst);
}

static ppcp_result digest_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    (void)ctx;
    return ppcp_digest_decode(r, (ppcp_digest *)dst);
}

/* A Capture's AchievedSummary needs no arena in practice — the thermal
 * timeline is the only list and a bounded one — but the decoder takes one so
 * the shape is the same everywhere.  A NULL arena means "no thermal timeline". */
static ppcp_result summary_read_noarena(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    return ppcp_achieved_summary_decode(r, (ppcp_arena *)ctx, (ppcp_achieved_summary *)dst);
}

ppcp_result ppcp_capture_decode_arena(ppcp_cbor_reader *r, ppcp_arena *a, ppcp_capture *out)
{
    ppcp_rfield f[11];
    size_t      n = 0;
    bool        s_id = false, s_anchor = false, s_stream = false;
    bool        s_comp = false, s_xfer = false;
    int         comp = 0, xfer = 0;
    ppcp_result rc;

    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    f[n++] = ppcp_rf("id", PPCP_F_ID, &out->id, &s_id);
    f[n++] = ppcp_rf_sub("anchor", anchor_read, &out->anchor, NULL, &s_anchor);
    f[n++] = ppcp_rf("stream_id", PPCP_F_ID, &out->stream_id, &s_stream);
    f[n++] = ppcp_rf_enum("completeness", ppcp_capture_completeness_enum_map(), &comp, &s_comp);
    f[n++] = ppcp_rf_enum("transfer", transfer_map, &xfer, &s_xfer);
    f[n++] = ppcp_rf_sub("interval", ppcp_sub_read_interval, &out->interval, NULL,
                         &out->has_interval);
    f[n++] = ppcp_rf("absent_reason", PPCP_F_ID, &out->absent_reason, &out->has_absent_reason);
    f[n++] = ppcp_rf_sub("gaps", gaps_read, out, NULL, NULL);
    f[n++] = ppcp_rf_sub("achieved_summary", summary_read_noarena, &out->achieved_summary,
                         a, &out->has_achieved_summary);
    f[n++] = ppcp_rf_sub("digest", digest_read, &out->digest, NULL, NULL);
    f[n++] = ppcp_rf("bytes", PPCP_F_UINT, &out->bytes, &out->has_bytes);

    rc = ppcp_rec_read(r, f, n);
    if (rc != PPCP_OK)
        return rc;
    if (!s_id || !s_anchor || !s_stream || !s_comp || !s_xfer)
        return PPCP_ERR_MALFORMED;
    out->completeness = (ppcp_completeness)comp;
    out->transfer     = (ppcp_transfer_state)xfer;
    if (ppcp_capture_validate(out) != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

ppcp_result ppcp_capture_decode(ppcp_cbor_reader *r, ppcp_capture *out)
{
    return ppcp_capture_decode_arena(r, NULL, out);
}

/* -------------------------------------------------------------- Readiness */

ppcp_result ppcp_readiness_settled(ppcp_readiness *out)
{
    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    out->settled = true;
    return PPCP_OK;
}

ppcp_result ppcp_readiness_not_settled(ppcp_readiness *out, uint32_t ready_ms)
{
    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    /* CORE 5.15: `estimated_ready_ms` is mandatory when `settled == false`, so
     * it is this constructor's parameter.  5.15a: no device state-machine name
     * crosses the wire, and there is no constructor here that takes one. */
    out->settled                = false;
    out->has_estimated_ready_ms = true;
    out->estimated_ready_ms     = ready_ms;
    return PPCP_OK;
}

ppcp_result ppcp_readiness_set_blocked(ppcp_readiness *r, const char *reason)
{
    ppcp_result rc;
    if (r == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_id_set_z(&r->blocked_reason, reason);
    if (rc != PPCP_OK)
        return rc;
    r->has_blocked_reason = true;
    return PPCP_OK;
}

ppcp_result ppcp_readiness_validate(const ppcp_readiness *r)
{
    if (r == NULL)
        return PPCP_ERR_INVALID;
    if (!r->settled && !r->has_estimated_ready_ms)
        return PPCP_ERR_INVALID;
    if (r->settled && r->has_estimated_ready_ms)
        return PPCP_ERR_INVALID;
    return PPCP_OK;
}

ppcp_result ppcp_readiness_encode(ppcp_cbor_writer *w, const ppcp_readiness *r)
{
    ppcp_wfield f[3];
    size_t      n  = 0;
    ppcp_result rc = ppcp_readiness_validate(r);
    if (rc != PPCP_OK)
        return rc;
    f[n++] = ppcp_wf_bool("settled", r->settled);
    if (r->has_estimated_ready_ms)
        f[n++] = ppcp_wf_uint("estimated_ready_ms", r->estimated_ready_ms);
    if (r->has_blocked_reason)
        f[n++] = ppcp_wf_id("blocked_reason", &r->blocked_reason);
    return ppcp_rec_write(w, f, n);
}

ppcp_result ppcp_readiness_decode(ppcp_cbor_reader *r, ppcp_readiness *out)
{
    ppcp_rfield f[3];
    bool        s_settled = false;
    uint64_t    ms = 0;
    ppcp_result rc;

    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    f[0] = ppcp_rf("settled", PPCP_F_BOOL, &out->settled, &s_settled);
    f[1] = ppcp_rf("estimated_ready_ms", PPCP_F_UINT, &ms, &out->has_estimated_ready_ms);
    f[2] = ppcp_rf("blocked_reason", PPCP_F_ID, &out->blocked_reason, &out->has_blocked_reason);
    rc = ppcp_rec_read(r, f, 3);
    if (rc != PPCP_OK) return rc;
    if (!s_settled) return PPCP_ERR_MALFORMED;
    if (out->has_estimated_ready_ms) {
        if (ms > 0xFFFFFFFFu) return PPCP_ERR_MALFORMED;
        out->estimated_ready_ms = (uint32_t)ms;
    }
    if (ppcp_readiness_validate(out) != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

/* ------------------------------------------------------------ DeviceStatus */

ppcp_result ppcp_device_status_available(ppcp_device_status *out, const char *source_id,
                                         const ppcp_instant *since)
{
    ppcp_result rc;
    if (out == NULL || since == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_instant_validate(since);
    if (rc != PPCP_OK)
        return rc;
    memset(out, 0, sizeof(*out));
    rc = ppcp_id_set_z(&out->source_id, source_id);
    if (rc != PPCP_OK)
        return rc;
    /* MSG 5.5b: `reason` MUST NOT be present when `available: true`, and this
     * constructor gives no way to attach one.  The unavailable case takes its
     * reason as a parameter for the same reason. */
    out->available = true;
    out->since     = *since;
    return PPCP_OK;
}

ppcp_result ppcp_device_status_unavailable(ppcp_device_status *out, const char *source_id,
                                           const char *reason, const ppcp_instant *since)
{
    ppcp_result rc;
    if (out == NULL || since == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_instant_validate(since);
    if (rc != PPCP_OK)
        return rc;
    memset(out, 0, sizeof(*out));
    rc = ppcp_id_set_z(&out->source_id, source_id);
    if (rc != PPCP_OK)
        return rc;
    /* 5.20a / 5.15a: `reason` says WHY the Source cannot be used, never what
     * it is presently doing.  It is an open registry (5.20d notes `no_source`
     * is deliberately not in it), so the spelling is not checked here. */
    rc = ppcp_id_set_z(&out->reason, reason);
    if (rc != PPCP_OK)
        return rc;
    out->available  = false;
    out->has_reason = true;
    out->since      = *since;
    return PPCP_OK;
}

ppcp_result ppcp_device_status_validate(const ppcp_device_status *d)
{
    if (d == NULL)
        return PPCP_ERR_INVALID;
    if (!ppcp_id_is_set(&d->source_id))
        return PPCP_ERR_INVALID;
    /* MSG 5.5b, both ways: a reason without unavailability contradicts the
     * clause, and unavailability without a reason drops the only field that
     * makes the report actionable. */
    if (d->available && d->has_reason)
        return PPCP_ERR_INVALID;
    if (!d->available && !d->has_reason)
        return PPCP_ERR_INVALID;
    if (d->has_reason && !ppcp_id_is_set(&d->reason))
        return PPCP_ERR_INVALID;
    return ppcp_instant_validate(&d->since);
}

size_t ppcp_device_status_wfields(ppcp_wfield *f, const ppcp_device_status *d)
{
    size_t n = 0;
    f[n++] = ppcp_wf_id("source_id", &d->source_id);
    f[n++] = ppcp_wf_bool("available", d->available);
    if (d->has_reason)
        f[n++] = ppcp_wf_id("reason", &d->reason);
    f[n++] = ppcp_wf_sub("since", ppcp_sub_write_instant, &d->since);
    return n;
}

size_t ppcp_device_status_rfields(ppcp_rfield *f, ppcp_device_status *d,
                                  ppcp_device_status_seen *s)
{
    size_t n = 0;
    memset(d, 0, sizeof(*d));
    memset(s, 0, sizeof(*s));
    f[n++] = ppcp_rf("source_id", PPCP_F_ID, &d->source_id, &s->source_id);
    f[n++] = ppcp_rf("available", PPCP_F_BOOL, &d->available, &s->available);
    f[n++] = ppcp_rf("reason", PPCP_F_ID, &d->reason, &d->has_reason);
    f[n++] = ppcp_rf_sub("since", ppcp_sub_read_instant, &d->since, NULL, &s->since);
    return n;
}

ppcp_result ppcp_device_status_finish(ppcp_device_status *d, const ppcp_device_status_seen *s)
{
    if (!s->source_id || !s->available || !s->since)
        return PPCP_ERR_MALFORMED;
    if (ppcp_device_status_validate(d) != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

ppcp_result ppcp_device_status_encode(ppcp_cbor_writer *w, const ppcp_device_status *d)
{
    ppcp_wfield f[4];
    size_t      n;
    ppcp_result rc = ppcp_device_status_validate(d);
    if (rc != PPCP_OK)
        return rc;
    n = ppcp_device_status_wfields(f, d);
    return ppcp_rec_write(w, f, n);
}

ppcp_result ppcp_device_status_decode(ppcp_cbor_reader *r, ppcp_device_status *out)
{
    ppcp_rfield             f[4];
    ppcp_device_status_seen seen;
    size_t                  n;
    ppcp_result             rc;

    if (out == NULL)
        return PPCP_ERR_INVALID;
    n  = ppcp_device_status_rfields(f, out, &seen);
    rc = ppcp_rec_read(r, f, n);
    if (rc != PPCP_OK)
        return rc;
    return ppcp_device_status_finish(out, &seen);
}

/* ------------------------------------------------------------ BufferMargin */

ppcp_result ppcp_buffer_margin_make(ppcp_buffer_margin *out, const char *stream_id,
                                    const ppcp_instant *retained_from,
                                    uint64_t discarded_since_open)
{
    ppcp_result rc;
    if (out == NULL || retained_from == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_instant_validate(retained_from);
    if (rc != PPCP_OK)
        return rc;
    memset(out, 0, sizeof(*out));
    rc = ppcp_id_set_z(&out->stream_id, stream_id);
    if (rc != PPCP_OK)
        return rc;
    out->retained_from        = *retained_from;
    out->discarded_since_open = discarded_since_open;
    return PPCP_OK;
}

ppcp_result ppcp_buffer_margin_set_retention_target(ppcp_buffer_margin *b,
                                                    ppcp_duration_ns target_ns)
{
    if (b == NULL || target_ns <= 0)
        return PPCP_ERR_INVALID;
    b->has_retention_target = true;
    b->retention_target_ns  = target_ns;
    return PPCP_OK;
}

ppcp_result ppcp_buffer_margin_set_last_discard(ppcp_buffer_margin *b,
                                                const ppcp_instant *since,
                                                ppcp_duration_ns duration_ns)
{
    ppcp_result rc;
    if (b == NULL || since == NULL || duration_ns < 0)
        return PPCP_ERR_INVALID;
    rc = ppcp_instant_validate(since);
    if (rc != PPCP_OK)
        return rc;
    /* 5.21: `last_discard` is ONE statement — a span, sized and timestamped —
     * so there is no setter for half of it. */
    b->has_last_discard          = true;
    b->last_discard_since        = *since;
    b->last_discard_duration_ns  = duration_ns;
    return PPCP_OK;
}

ppcp_result ppcp_buffer_margin_validate(const ppcp_buffer_margin *b)
{
    ppcp_result rc;
    if (b == NULL)
        return PPCP_ERR_INVALID;
    if (!ppcp_id_is_set(&b->stream_id))
        return PPCP_ERR_INVALID;
    rc = ppcp_instant_validate(&b->retained_from);
    if (rc != PPCP_OK)
        return rc;
    if (b->has_retention_target && b->retention_target_ns <= 0)
        return PPCP_ERR_INVALID;
    if (b->has_last_discard) {
        if (b->last_discard_duration_ns < 0)
            return PPCP_ERR_INVALID;
        rc = ppcp_instant_validate(&b->last_discard_since);
        if (rc != PPCP_OK)
            return rc;
    }
    return PPCP_OK;
}

static ppcp_result last_discard_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_buffer_margin *b = (const ppcp_buffer_margin *)ctx;
    ppcp_wfield f[2];
    f[0] = ppcp_wf_sub("since", ppcp_sub_write_instant, &b->last_discard_since);
    f[1] = ppcp_wf_int("duration", b->last_discard_duration_ns);
    return ppcp_rec_write(w, f, 2);
}

static ppcp_result last_discard_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_buffer_margin *b = (ppcp_buffer_margin *)dst;
    ppcp_rfield f[2];
    bool        s_since = false, s_dur = false;
    ppcp_result rc;
    (void)ctx;
    f[0] = ppcp_rf_sub("since", ppcp_sub_read_instant, &b->last_discard_since, NULL, &s_since);
    f[1] = ppcp_rf("duration", PPCP_F_INT, &b->last_discard_duration_ns, &s_dur);
    rc = ppcp_rec_read(r, f, 2);
    if (rc != PPCP_OK)
        return rc;
    if (!s_since || !s_dur)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

size_t ppcp_buffer_margin_wfields(ppcp_wfield *f, const ppcp_buffer_margin *b)
{
    size_t n = 0;
    f[n++] = ppcp_wf_id("stream_id", &b->stream_id);
    f[n++] = ppcp_wf_sub("retained_from", ppcp_sub_write_instant, &b->retained_from);
    if (b->has_retention_target)
        f[n++] = ppcp_wf_int("retention_target", b->retention_target_ns);
    f[n++] = ppcp_wf_uint("discarded_since_open", b->discarded_since_open);
    if (b->has_last_discard)
        f[n++] = ppcp_wf_sub("last_discard", last_discard_write, b);
    return n;
}

size_t ppcp_buffer_margin_rfields(ppcp_rfield *f, ppcp_buffer_margin *b,
                                  ppcp_buffer_margin_seen *s)
{
    size_t n = 0;
    memset(b, 0, sizeof(*b));
    memset(s, 0, sizeof(*s));
    f[n++] = ppcp_rf("stream_id", PPCP_F_ID, &b->stream_id, &s->stream_id);
    f[n++] = ppcp_rf_sub("retained_from", ppcp_sub_read_instant, &b->retained_from, NULL,
                         &s->retained_from);
    f[n++] = ppcp_rf("retention_target", PPCP_F_INT, &b->retention_target_ns,
                     &b->has_retention_target);
    f[n++] = ppcp_rf("discarded_since_open", PPCP_F_UINT, &b->discarded_since_open,
                     &s->discarded_since_open);
    f[n++] = ppcp_rf_sub("last_discard", last_discard_read, b, NULL, &b->has_last_discard);
    return n;
}

ppcp_result ppcp_buffer_margin_finish(ppcp_buffer_margin *b, const ppcp_buffer_margin_seen *s)
{
    if (!s->stream_id || !s->retained_from || !s->discarded_since_open)
        return PPCP_ERR_MALFORMED;
    if (ppcp_buffer_margin_validate(b) != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

ppcp_result ppcp_buffer_margin_encode(ppcp_cbor_writer *w, const ppcp_buffer_margin *b)
{
    ppcp_wfield f[5];
    size_t      n;
    ppcp_result rc = ppcp_buffer_margin_validate(b);
    if (rc != PPCP_OK)
        return rc;
    n = ppcp_buffer_margin_wfields(f, b);
    return ppcp_rec_write(w, f, n);
}

ppcp_result ppcp_buffer_margin_decode(ppcp_cbor_reader *r, ppcp_buffer_margin *out)
{
    ppcp_rfield             f[5];
    ppcp_buffer_margin_seen seen;
    size_t                  n;
    ppcp_result             rc;

    if (out == NULL)
        return PPCP_ERR_INVALID;
    n  = ppcp_buffer_margin_rfields(f, out, &seen);
    rc = ppcp_rec_read(r, f, n);
    if (rc != PPCP_OK)
        return rc;
    return ppcp_buffer_margin_finish(out, &seen);
}

/* --------------------------------------------------------------- ShotLink */

bool ppcp_shot_link_basis_is_retrospective(const char *basis, size_t len)
{
    if (basis == NULL)
        return false;
    /* 5.16b/f — the three retrospective bases.  `arrival_pairing` is NOT one
     * (5.16c): it records an association a peer made live, at capture time,
     * and there is no later moment at which the evidence would be better. */
    return ppcp_cbor_key_is(basis, len, PPCP_LINK_INTERVAL_ALIGNMENT) ||
           ppcp_cbor_key_is(basis, len, PPCP_LINK_ACOUSTIC_CORRELATION) ||
           ppcp_cbor_key_is(basis, len, PPCP_LINK_SEQUENCE_ALIGNMENT);
}

ppcp_result ppcp_shot_link_make(ppcp_shot_link *out, const char *id, const char *local_shot_id,
                                const char *foreign_shot_id, const char *basis,
                                double confidence)
{
    ppcp_result rc;
    if (out == NULL)
        return PPCP_ERR_INVALID;
    if (!(confidence >= 0.0) || !(confidence <= 1.0))
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    rc = ppcp_id_set_z(&out->id, id);                            if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->local_shot_id, local_shot_id);      if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->foreign_shot_id, foreign_shot_id);  if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->basis, basis);                      if (rc != PPCP_OK) return rc;
    out->confidence = confidence;
    out->confirmed  = false;
    return PPCP_OK;
}

ppcp_result ppcp_shot_link_confirm(ppcp_shot_link *l, ppcp_confirmed_by by)
{
    if (l == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_enum_to_text(confirmed_by_map, (int)by) == NULL)
        return PPCP_ERR_INVALID;
    /* 5.16f: a retrospective basis may only be `confirmed_by: user`.  The
     * confirmation requirement is about the cost of being wrong, not the
     * difficulty of being right (5.16b). */
    if (by == PPCP_CONFIRMED_BY_OBSERVER &&
        ppcp_shot_link_basis_is_retrospective(l->basis.v, l->basis.len))
        return PPCP_ERR_INVALID;
    /* 5.16e: `confirmed` and `confirmed_by` are set together, so a bare
     * boolean cannot come to carry two epistemic states again. */
    l->confirmed        = true;
    l->has_confirmed_by = true;
    l->confirmed_by     = by;
    return PPCP_OK;
}

ppcp_result ppcp_shot_link_set_foreign_session(ppcp_shot_link *l, const char *sid)
{
    ppcp_result rc;
    if (l == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_id_set_z(&l->foreign_session_id, sid);
    if (rc != PPCP_OK) return rc;
    l->has_foreign_session_id = true;
    return PPCP_OK;
}

ppcp_result ppcp_shot_link_set_foreign_system(ppcp_shot_link *l, const char *sys)
{
    ppcp_result rc;
    if (l == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_id_set_z(&l->foreign_system, sys);
    if (rc != PPCP_OK) return rc;
    l->has_foreign_system = true;
    return PPCP_OK;
}

ppcp_result ppcp_shot_link_validate(const ppcp_shot_link *l)
{
    if (l == NULL)
        return PPCP_ERR_INVALID;
    if (!ppcp_id_is_set(&l->id) || !ppcp_id_is_set(&l->local_shot_id) ||
        !ppcp_id_is_set(&l->foreign_shot_id) || !ppcp_id_is_set(&l->basis))
        return PPCP_ERR_INVALID;
    if (!(l->confidence >= 0.0) || !(l->confidence <= 1.0))
        return PPCP_ERR_INVALID;
    if (l->confirmed != l->has_confirmed_by)
        return PPCP_ERR_INVALID;                       /* 5.16e, the iff */
    if (l->confirmed) {
        if (ppcp_enum_to_text(confirmed_by_map, (int)l->confirmed_by) == NULL)
            return PPCP_ERR_INVALID;
        if (l->confirmed_by == PPCP_CONFIRMED_BY_OBSERVER &&
            ppcp_shot_link_basis_is_retrospective(l->basis.v, l->basis.len))
            return PPCP_ERR_INVALID;                   /* 5.16f */
    }
    return PPCP_OK;
}

ppcp_result ppcp_shot_link_encode(ppcp_cbor_writer *w, const ppcp_shot_link *l)
{
    ppcp_wfield f[9];
    size_t      n  = 0;
    ppcp_result rc = ppcp_shot_link_validate(l);
    if (rc != PPCP_OK)
        return rc;
    f[n++] = ppcp_wf_id("id", &l->id);
    f[n++] = ppcp_wf_id("local_shot_id", &l->local_shot_id);
    f[n++] = ppcp_wf_id("foreign_shot_id", &l->foreign_shot_id);
    f[n++] = ppcp_wf_id("basis", &l->basis);
    f[n++] = ppcp_wf_double("confidence", l->confidence);
    f[n++] = ppcp_wf_bool("confirmed", l->confirmed);
    if (l->has_confirmed_by)
        f[n++] = ppcp_wf_enum("confirmed_by", confirmed_by_map, (int)l->confirmed_by);
    if (l->has_foreign_session_id)
        f[n++] = ppcp_wf_id("foreign_session_id", &l->foreign_session_id);
    if (l->has_foreign_system)
        f[n++] = ppcp_wf_id("foreign_system", &l->foreign_system);
    return ppcp_rec_write(w, f, n);
}

ppcp_result ppcp_shot_link_decode(ppcp_cbor_reader *r, ppcp_shot_link *out)
{
    ppcp_rfield f[9];
    size_t      n = 0;
    bool        s_id = false, s_local = false, s_foreign = false, s_basis = false;
    bool        s_conf = false, s_confirmed = false;
    int         by = 0;
    ppcp_result rc;

    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    f[n++] = ppcp_rf("id", PPCP_F_ID, &out->id, &s_id);
    f[n++] = ppcp_rf("local_shot_id", PPCP_F_ID, &out->local_shot_id, &s_local);
    f[n++] = ppcp_rf("foreign_shot_id", PPCP_F_ID, &out->foreign_shot_id, &s_foreign);
    f[n++] = ppcp_rf("basis", PPCP_F_ID, &out->basis, &s_basis);
    f[n++] = ppcp_rf("confidence", PPCP_F_DOUBLE, &out->confidence, &s_conf);
    f[n++] = ppcp_rf("confirmed", PPCP_F_BOOL, &out->confirmed, &s_confirmed);
    f[n++] = ppcp_rf_enum("confirmed_by", confirmed_by_map, &by, &out->has_confirmed_by);
    f[n++] = ppcp_rf("foreign_session_id", PPCP_F_ID, &out->foreign_session_id,
                     &out->has_foreign_session_id);
    f[n++] = ppcp_rf("foreign_system", PPCP_F_ID, &out->foreign_system,
                     &out->has_foreign_system);
    rc = ppcp_rec_read(r, f, n);
    if (rc != PPCP_OK) return rc;
    if (!s_id || !s_local || !s_foreign || !s_basis || !s_conf || !s_confirmed)
        return PPCP_ERR_MALFORMED;
    out->confirmed_by = (ppcp_confirmed_by)by;
    if (ppcp_shot_link_validate(out) != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

/* ------------------------------------------------------------ SessionLink */

static ppcp_result session_link_common(ppcp_session_link *out, const char *id,
                                       const char *from_session, const char *to_session,
                                       const char *from_tb, const char *to_tb,
                                       const char *basis, const char *derived_by)
{
    ppcp_result rc;
    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    rc = ppcp_id_set_z(&out->id, id);                          if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->from_session_id, from_session);   if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->to_session_id, to_session);       if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->from_timebase_id, from_tb);       if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->to_timebase_id, to_tb);           if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->basis, basis);                    if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->derived_by, derived_by);          if (rc != PPCP_OK) return rc;
    return PPCP_OK;
}

ppcp_result ppcp_session_link_make_affine(ppcp_session_link *out, const char *id,
                                          const char *from_session, const char *to_session,
                                          const char *from_tb, const char *to_tb,
                                          int64_t offset_ns, double skew_ppm,
                                          double offset_sigma_ns, double skew_sigma_ppm,
                                          const char *basis, const char *derived_by)
{
    ppcp_result rc = session_link_common(out, id, from_session, to_session, from_tb, to_tb,
                                         basis, derived_by);
    if (rc != PPCP_OK)
        return rc;
    /* 5.17b: both sigmas, for the same reason a TimebaseRelation carries them
     * (5.4a) — and like that constructor, they are parameters rather than
     * setters, so the missing-sigma shape has no representation (I3's method
     * applied to I25's type). */
    if (!(offset_sigma_ns >= 0.0) || !(skew_sigma_ppm >= 0.0))
        return PPCP_ERR_INVALID;
    out->cls             = PPCP_REL_AFFINE;
    out->offset_ns       = offset_ns;
    out->skew_ppm        = skew_ppm;
    out->offset_sigma_ns = offset_sigma_ns;
    out->skew_sigma_ppm  = skew_sigma_ppm;
    return PPCP_OK;
}

ppcp_result ppcp_session_link_make_unrelated(ppcp_session_link *out, const char *id,
                                             const char *from_session, const char *to_session,
                                             const char *from_tb, const char *to_tb,
                                             const char *basis, const char *derived_by)
{
    ppcp_result rc = session_link_common(out, id, from_session, to_session, from_tb, to_tb,
                                         basis, derived_by);
    if (rc != PPCP_OK)
        return rc;
    out->cls = PPCP_REL_UNRELATED;
    return PPCP_OK;
}

ppcp_result ppcp_session_link_validate(const ppcp_session_link *l)
{
    if (l == NULL)
        return PPCP_ERR_INVALID;
    if (!ppcp_id_is_set(&l->id) || !ppcp_id_is_set(&l->from_session_id) ||
        !ppcp_id_is_set(&l->to_session_id) || !ppcp_id_is_set(&l->from_timebase_id) ||
        !ppcp_id_is_set(&l->to_timebase_id) || !ppcp_id_is_set(&l->basis) ||
        !ppcp_id_is_set(&l->derived_by))
        return PPCP_ERR_INVALID;
    if (l->cls == PPCP_REL_AFFINE) {
        if (!(l->offset_sigma_ns >= 0.0) || !(l->skew_sigma_ppm >= 0.0))
            return PPCP_ERR_INVALID;
    } else if (l->cls == PPCP_REL_UNRELATED) {
        if (l->offset_ns != 0 || l->skew_ppm != 0.0 ||
            l->offset_sigma_ns != 0.0 || l->skew_sigma_ppm != 0.0)
            return PPCP_ERR_INVALID;
    } else {
        return PPCP_ERR_INVALID;
    }
    return PPCP_OK;
}

ppcp_result ppcp_session_link_encode(ppcp_cbor_writer *w, const ppcp_session_link *l)
{
    ppcp_wfield f[12];
    size_t      n  = 0;
    ppcp_result rc = ppcp_session_link_validate(l);
    if (rc != PPCP_OK)
        return rc;
    f[n++] = ppcp_wf_id("id", &l->id);
    f[n++] = ppcp_wf_id("from_session_id", &l->from_session_id);
    f[n++] = ppcp_wf_id("to_session_id", &l->to_session_id);
    f[n++] = ppcp_wf_id("from_timebase_id", &l->from_timebase_id);
    f[n++] = ppcp_wf_id("to_timebase_id", &l->to_timebase_id);
    f[n++] = ppcp_wf_enum("class", relation_class_map, (int)l->cls);
    f[n++] = ppcp_wf_id("basis", &l->basis);
    f[n++] = ppcp_wf_id("derived_by", &l->derived_by);
    f[n++] = ppcp_wf_bool("confirmed", l->confirmed);
    if (l->cls == PPCP_REL_AFFINE) {
        f[n++] = ppcp_wf_int("offset_ns", l->offset_ns);
        f[n++] = ppcp_wf_double("skew_ppm", l->skew_ppm);
        {
            ppcp_wfield g[14];
            size_t i;
            for (i = 0; i < n; i++) g[i] = f[i];
            g[n++] = ppcp_wf_double("offset_sigma_ns", l->offset_sigma_ns);
            g[n++] = ppcp_wf_double("skew_sigma_ppm", l->skew_sigma_ppm);
            return ppcp_rec_write(w, g, n);
        }
    }
    return ppcp_rec_write(w, f, n);
}

ppcp_result ppcp_session_link_decode(ppcp_cbor_reader *r, ppcp_session_link *out)
{
    ppcp_rfield f[13];
    size_t      n = 0;
    bool        s_id = false, s_fs = false, s_ts = false, s_ftb = false, s_ttb = false;
    bool        s_cls = false, s_basis = false, s_by = false, s_confirmed = false;
    bool        s_off = false, s_skew = false, s_osig = false, s_ssig = false;
    int         cls = 0;
    ppcp_result rc;

    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    f[n++] = ppcp_rf("id", PPCP_F_ID, &out->id, &s_id);
    f[n++] = ppcp_rf("from_session_id", PPCP_F_ID, &out->from_session_id, &s_fs);
    f[n++] = ppcp_rf("to_session_id", PPCP_F_ID, &out->to_session_id, &s_ts);
    f[n++] = ppcp_rf("from_timebase_id", PPCP_F_ID, &out->from_timebase_id, &s_ftb);
    f[n++] = ppcp_rf("to_timebase_id", PPCP_F_ID, &out->to_timebase_id, &s_ttb);
    f[n++] = ppcp_rf_enum("class", relation_class_map, &cls, &s_cls);
    f[n++] = ppcp_rf("basis", PPCP_F_ID, &out->basis, &s_basis);
    f[n++] = ppcp_rf("derived_by", PPCP_F_ID, &out->derived_by, &s_by);
    f[n++] = ppcp_rf("confirmed", PPCP_F_BOOL, &out->confirmed, &s_confirmed);
    f[n++] = ppcp_rf("offset_ns", PPCP_F_INT, &out->offset_ns, &s_off);
    f[n++] = ppcp_rf("skew_ppm", PPCP_F_DOUBLE, &out->skew_ppm, &s_skew);
    f[n++] = ppcp_rf("offset_sigma_ns", PPCP_F_DOUBLE, &out->offset_sigma_ns, &s_osig);
    f[n++] = ppcp_rf("skew_sigma_ppm", PPCP_F_DOUBLE, &out->skew_sigma_ppm, &s_ssig);

    rc = ppcp_rec_read(r, f, n);
    if (rc != PPCP_OK) return rc;
    if (!s_id || !s_fs || !s_ts || !s_ftb || !s_ttb || !s_cls || !s_basis || !s_by ||
        !s_confirmed)
        return PPCP_ERR_MALFORMED;
    out->cls = (ppcp_relation_class)cls;
    if (out->cls == PPCP_REL_AFFINE) {
        /* 5.17b, the same rejection 5.4a makes for a TimebaseRelation. */
        if (!s_off || !s_skew || !s_osig || !s_ssig)
            return PPCP_ERR_MALFORMED;
    } else if (s_off || s_skew || s_osig || s_ssig) {
        return PPCP_ERR_MALFORMED;
    }
    if (ppcp_session_link_validate(out) != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

/* ------------------------------------------------------------- Annotation */

ppcp_kind_view ppcp_annotation_kind_view(const char *kind, size_t len)
{
    if (kind == NULL)
        return PPCP_KIND_UNREGISTERED;
    /* 5.18j: the registry marks each value view-specific or not.  `line` and
     * `plane` are; `text` and `nav_anchor` are not. */
    if (ppcp_cbor_key_is(kind, len, "line") || ppcp_cbor_key_is(kind, len, "plane"))
        return PPCP_KIND_VIEW_SPECIFIC;
    if (ppcp_cbor_key_is(kind, len, "text") || ppcp_cbor_key_is(kind, len, "nav_anchor"))
        return PPCP_KIND_NOT_VIEW_SPECIFIC;
    return PPCP_KIND_UNREGISTERED;
}

ppcp_result ppcp_annotation_make(ppcp_annotation *out, const char *id, const char *session_id,
                                 const char *shot_id, const ppcp_instant *at,
                                 const char *author_peer_id,
                                 ppcp_annotation_provenance provenance, const char *kind,
                                 const char *format, const uint8_t *body, size_t body_len,
                                 const ppcp_instant *created_at, uint64_t revision)
{
    ppcp_result rc;
    if (out == NULL || at == NULL || created_at == NULL || body == NULL)
        return PPCP_ERR_INVALID;
    /* 5.18f / ENC §8: at most 8 KiB.  Anything approaching the cap is a
     * different feature, and `annotation` travels on the CONTROL channel. */
    if (body_len == 0 || body_len > PPCP_ANNOTATION_BODY_MAX)
        return PPCP_ERR_INVALID;
    if (ppcp_enum_to_text(annot_provenance_map, (int)provenance) == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_instant_validate(at) != PPCP_OK ||
        ppcp_instant_validate(created_at) != PPCP_OK)
        return PPCP_ERR_INVALID;

    memset(out, 0, sizeof(*out));
    rc = ppcp_id_set_z(&out->id, id);                        if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->session_id, session_id);        if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->shot_id, shot_id);              if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->author_peer_id, author_peer_id);if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->kind, kind);                    if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->format, format);                if (rc != PPCP_OK) return rc;
    out->at         = *at;
    out->created_at = *created_at;
    out->provenance = provenance;
    out->body       = body;         /* 5.18a: opaque, stored and returned unchanged */
    out->body_len   = body_len;
    out->revision   = revision;
    return PPCP_OK;
}

ppcp_result ppcp_annotation_set_stream_id(ppcp_annotation *a, const char *stream_id)
{
    ppcp_result rc;
    if (a == NULL)
        return PPCP_ERR_INVALID;
    /* 5.18j: presence follows `kind`.  Attaching a stream to a kind the
     * registry marks not-view-specific is refused here rather than at the
     * encoder, because 5.18h has nothing to bind to otherwise. */
    if (ppcp_annotation_kind_view(a->kind.v, a->kind.len) == PPCP_KIND_NOT_VIEW_SPECIFIC)
        return PPCP_ERR_INVALID;
    rc = ppcp_id_set_z(&a->stream_id, stream_id);
    if (rc != PPCP_OK)
        return rc;
    a->has_stream_id = true;
    return PPCP_OK;
}

ppcp_result ppcp_annotation_set_deleted(ppcp_annotation *a, bool deleted)
{
    if (a == NULL)
        return PPCP_ERR_INVALID;
    a->has_deleted = true;
    a->deleted     = deleted;
    return PPCP_OK;
}

ppcp_result ppcp_annotation_validate(const ppcp_annotation *a)
{
    ppcp_kind_view kv;
    if (a == NULL)
        return PPCP_ERR_INVALID;
    if (!ppcp_id_is_set(&a->id) || !ppcp_id_is_set(&a->session_id) ||
        !ppcp_id_is_set(&a->shot_id) || !ppcp_id_is_set(&a->author_peer_id) ||
        !ppcp_id_is_set(&a->kind) || !ppcp_id_is_set(&a->format))
        return PPCP_ERR_INVALID;
    if (a->body == NULL || a->body_len == 0 || a->body_len > PPCP_ANNOTATION_BODY_MAX)
        return PPCP_ERR_INVALID;
    if (ppcp_enum_to_text(annot_provenance_map, (int)a->provenance) == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_instant_validate(&a->at) != PPCP_OK ||
        ppcp_instant_validate(&a->created_at) != PPCP_OK)
        return PPCP_ERR_INVALID;
    /* 5.18j — the presence rule, statically checkable because it is derived
     * from `kind`, which is on the wire.  An unregistered kind is treated as
     * view-specific if and only if `stream_id` is present, which is the
     * conservative default and is therefore always legal. */
    kv = ppcp_annotation_kind_view(a->kind.v, a->kind.len);
    if (kv == PPCP_KIND_VIEW_SPECIFIC && !a->has_stream_id)
        return PPCP_ERR_INVALID;
    if (kv == PPCP_KIND_NOT_VIEW_SPECIFIC && a->has_stream_id)
        return PPCP_ERR_INVALID;
    return PPCP_OK;
}

ppcp_result ppcp_annotation_encode(ppcp_cbor_writer *w, const ppcp_annotation *a)
{
    ppcp_wfield f[12];
    size_t      n  = 0;
    ppcp_result rc = ppcp_annotation_validate(a);
    if (rc != PPCP_OK)
        return rc;
    f[n++] = ppcp_wf_id("id", &a->id);
    f[n++] = ppcp_wf_id("session_id", &a->session_id);
    f[n++] = ppcp_wf_id("shot_id", &a->shot_id);
    f[n++] = ppcp_wf_sub("at", ppcp_sub_write_instant, &a->at);
    f[n++] = ppcp_wf_id("author_peer_id", &a->author_peer_id);
    f[n++] = ppcp_wf_enum("provenance", annot_provenance_map, (int)a->provenance);
    f[n++] = ppcp_wf_id("kind", &a->kind);
    f[n++] = ppcp_wf_id("format", &a->format);
    f[n++] = ppcp_wf_bytes("body", a->body, a->body_len);
    f[n++] = ppcp_wf_sub("created_at", ppcp_sub_write_instant, &a->created_at);
    f[n++] = ppcp_wf_uint("revision", a->revision);
    if (a->has_stream_id)
        f[n++] = ppcp_wf_id("stream_id", &a->stream_id);
    {
        ppcp_wfield g[13];
        size_t i;
        for (i = 0; i < n; i++) g[i] = f[i];
        if (a->has_deleted)
            g[n++] = ppcp_wf_bool("deleted", a->deleted);
        return ppcp_rec_write(w, g, n);
    }
}

ppcp_result ppcp_annotation_decode(ppcp_cbor_reader *r, ppcp_annotation *out)
{
    ppcp_rfield    f[13];
    ppcp_bytes_ref body;
    size_t         n = 0;
    bool           s_id = false, s_sid = false, s_shot = false, s_at = false;
    bool           s_author = false, s_prov = false, s_kind = false, s_fmt = false;
    bool           s_body = false, s_created = false, s_rev = false;
    int            prov = 0;
    ppcp_result    rc;

    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    memset(&body, 0, sizeof(body));

    f[n++] = ppcp_rf("id", PPCP_F_ID, &out->id, &s_id);
    f[n++] = ppcp_rf("session_id", PPCP_F_ID, &out->session_id, &s_sid);
    f[n++] = ppcp_rf("shot_id", PPCP_F_ID, &out->shot_id, &s_shot);
    f[n++] = ppcp_rf_sub("at", ppcp_sub_read_instant, &out->at, NULL, &s_at);
    f[n++] = ppcp_rf("author_peer_id", PPCP_F_ID, &out->author_peer_id, &s_author);
    f[n++] = ppcp_rf_enum("provenance", annot_provenance_map, &prov, &s_prov);
    f[n++] = ppcp_rf("kind", PPCP_F_ID, &out->kind, &s_kind);
    f[n++] = ppcp_rf("format", PPCP_F_ID, &out->format, &s_fmt);
    f[n++] = ppcp_rf("body", PPCP_F_BYTES, &body, &s_body);
    f[n++] = ppcp_rf_sub("created_at", ppcp_sub_read_instant, &out->created_at, NULL,
                         &s_created);
    f[n++] = ppcp_rf("revision", PPCP_F_UINT, &out->revision, &s_rev);
    f[n++] = ppcp_rf("stream_id", PPCP_F_ID, &out->stream_id, &out->has_stream_id);
    f[n++] = ppcp_rf("deleted", PPCP_F_BOOL, &out->deleted, &out->has_deleted);

    rc = ppcp_rec_read(r, f, n);
    if (rc != PPCP_OK)
        return rc;
    if (!s_id || !s_sid || !s_shot || !s_at || !s_author || !s_prov || !s_kind ||
        !s_fmt || !s_body || !s_created || !s_rev)
        return PPCP_ERR_MALFORMED;
    if (body.len > PPCP_ANNOTATION_BODY_MAX)
        return PPCP_ERR_LIMIT;
    /* 5.18a: the body points into the caller's buffer and is never copied,
     * rewritten or reinterpreted — which is what makes the round trip
     * byte-identical for a `format` this peer does not understand. */
    out->body       = body.p;
    out->body_len   = body.len;
    out->provenance = (ppcp_annotation_provenance)prov;
    if (ppcp_annotation_validate(out) != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

/* ------------------------------------------------------- opaque CBOR maps */

static ppcp_result raw_map_write(ppcp_cbor_writer *w, const void *ctx)
{
    const raw_map_ref *m = (const raw_map_ref *)ctx;
    ppcp_cbor_reader   rd;
    ppcp_cbor_item     it;
    ppcp_result        rc;
    uint32_t           i;

    ppcp_cbor_reader_init(&rd, m->p, m->n, ppcp_cbor_limits_for_channel(PPCP_CHANNEL_CONTROL));
    rc = ppcp_cbor_read(&rd, &it);
    if (rc != PPCP_OK) return rc;
    if (it.type != PPCP_CBOR_MAP) return PPCP_ERR_INVALID;
    if (ppcp_cbor_write_map(w, it.count) != PPCP_OK) return ppcp_cbor_writer_status(w);
    for (i = 0; i < it.count; i++) {
        const char *k; size_t klen; ppcp_cbor_item v;
        rc = ppcp_cbor_read_key(&rd, &k, &klen);
        if (rc != PPCP_OK) return rc;
        if (ppcp_cbor_write_text(w, k, klen) != PPCP_OK) return ppcp_cbor_writer_status(w);
        rc = ppcp_cbor_read(&rd, &v);
        if (rc != PPCP_OK) return rc;
        switch (v.type) {
        case PPCP_CBOR_UINT: case PPCP_CBOR_NINT: rc = ppcp_cbor_write_int(w, v.i); break;
        case PPCP_CBOR_DOUBLE: rc = ppcp_cbor_write_double(w, v.f); break;
        case PPCP_CBOR_BOOL:   rc = ppcp_cbor_write_bool(w, v.b); break;
        case PPCP_CBOR_TEXT:   rc = ppcp_cbor_write_text(w, (const char *)v.bytes, v.len); break;
        case PPCP_CBOR_BYTES:  rc = ppcp_cbor_write_bytes(w, v.bytes, v.len); break;
        default: return PPCP_ERR_INVALID;
        }
        if (rc != PPCP_OK) return rc;
    }
    return ppcp_cbor_writer_status(w);
}

static ppcp_result raw_map_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    raw_map_ref   *out = (raw_map_ref *)dst;
    const uint8_t *start;
    size_t         before;
    ppcp_cbor_item it;
    ppcp_result    rc;
    uint32_t       i;
    (void)ctx;

    before = r->pos;
    start  = r->buf + before;
    rc = ppcp_cbor_read(r, &it);
    if (rc != PPCP_OK) return rc;
    if (it.type != PPCP_CBOR_MAP) return PPCP_ERR_MALFORMED;
    for (i = 0; i < it.count; i++) {
        const char *k; size_t klen;
        rc = ppcp_cbor_read_key(r, &k, &klen);
        if (rc != PPCP_OK) return rc;
        rc = ppcp_cbor_skip(r);
        if (rc != PPCP_OK) return rc;
    }
    out->p = start;
    out->n = r->pos - before;
    return PPCP_OK;
}
