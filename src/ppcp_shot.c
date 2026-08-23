/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * Detect, Mint and Arbitrate — CORE §5.12, §5.13, §5.16, §8.  Work package
 * L10.  See include/ppcp/shot.h for the contract.
 *
 * Two things shape this file more than anything else.
 *
 *   - Nothing here holds a threshold.  Promotion and exclusion are both
 *     callbacks (I14, CONF §6), and the only numbers this code compares are
 *     the two the Session DECLARED: `coincidence_window_ns` and
 *     `issue_hold_ns`.
 *   - Nothing here rewrites a Shot.  `t0` is written once by
 *     ppcp_shot_make() and there is no path from any function below to it
 *     afterwards (I7); the candidate list is the one thing 5.13d lets another
 *     peer extend, and extension is additive, sorted and idempotent so both
 *     ends converge byte for byte in either delivery order (5.13e).
 */
#include "ppcp/shot.h"
#include "ppcp_codec.h"

#include <string.h>

/* ================================================== CORE §5.12 — Detect */

ppcp_result ppcp_candidate_make_canonical(ppcp_candidate *out, const char *id,
                                          const ppcp_source *src,
                                          const ppcp_capture_profile *profile,
                                          const char *basis, int64_t raw_ns,
                                          ppcp_duration_ns exposure_ns, double confidence,
                                          const ppcp_estimate *tof)
{
    ppcp_instant at;
    int64_t      canonical = raw_ns;
    ppcp_result  rc;

    if (out == NULL || id == NULL || src == NULL || basis == NULL)
        return PPCP_ERR_INVALID;
    if (!ppcp_id_is_set(&src->timebase_id) || !ppcp_id_is_set(&src->peer_id))
        return PPCP_ERR_INVALID;     /* 5.6a / I26 — a Source names its Timebase */

    /* 6.1d — a profile with no `format` is a non-framed source: the canonical
     * instant is `t` and `convention` MUST be `mid`.  A microphone is the case
     * that matters, and it is the one where the whole conversion is a no-op —
     * which is exactly why the omission went unnoticed for acoustic candidates
     * and mattered for `motion` ones (5.12). */
    if (profile != NULL && profile->format.present) {
        rc = ppcp_canonical_instant(&profile->timing, raw_ns, exposure_ns, &canonical);
        if (rc != PPCP_OK)
            return rc;
    } else if (profile != NULL && profile->timing.convention != PPCP_CONV_MID) {
        return PPCP_ERR_INVALID;     /* 6.1d makes this unconstructible */
    }

    rc = ppcp_instant_make(&at, src->timebase_id.v, src->timebase_id.len, canonical);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_candidate_make(out, id, src->peer_id.v, src->id.v, basis, &at, confidence);
    if (rc != PPCP_OK)
        return rc;

    /* 5.12f — the observer corrects, and the correction is visible.  Reported
     * even when it is zero: "no correction was applied" and "the field was
     * omitted" are different statements, and only one of them is checkable. */
    rc = ppcp_candidate_set_canonical_correction(out, canonical - raw_ns);
    if (rc != PPCP_OK)
        return rc;
    if (tof != NULL) {
        /* I29 in the type system: `tof` is an Estimate and there is no way to
         * obtain one carrying a value without a sigma. */
        rc = ppcp_candidate_set_tof_correction(out, tof);
        if (rc != PPCP_OK)
            return rc;
    }
    return PPCP_OK;
}

ppcp_result ppcp_peer_nominate(ppcp_peer *p, const ppcp_candidate *c)
{
    ppcp_msg    m;
    ppcp_id     tb;
    ppcp_result rc;

    if (p == NULL || c == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_candidate_validate(c);
    if (rc != PPCP_OK)
        return rc;

    /* 5.2a — a peer nominates from its own Sources.  A Candidate carrying
     * another peer's id would put a claim in a namespace that is not this
     * peer's to mint in (8.3e's principle, one entity over). */
    if (!ppcp_id_equal(&c->peer_id, ppcp_peer_id(p)))
        return PPCP_ERR_INVALID;

    /* I26 / 5.12a / 7.1a — the Source is one this peer DECLARED, and it names
     * a Timebase this peer declared.  Both halves matter: a Source with no
     * clock strands the Candidate with nothing to convert. */
    memset(&tb, 0, sizeof(tb));
    if (!ppcp_peer_owns_source(p, &c->source_id, &tb))
        return PPCP_ERR_NOT_FOUND;
    if (!ppcp_peer_declares_timebase(p, &tb))
        return PPCP_ERR_INVALID;
    /* 5.12: `at` is the canonical instant IN THE SOURCE'S TIMEBASE.  A
     * Candidate stamped in some other clock has already been converted by
     * somebody, and 8.2a would convert it again. */
    if (!ppcp_id_equal(&c->at.tb, &tb))
        return PPCP_ERR_INVALID;

    rc = ppcp_msg_init(&m, PPCP_MT_CANDIDATE, 1);
    if (rc != PPCP_OK)
        return rc;
    m.body.candidate.candidate = *c;
    return ppcp_peer_send(p, PPCP_CHANNEL_CONTROL, &m);
}

ppcp_result ppcp_peer_shot(ppcp_peer *p, const ppcp_shot *s)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || s == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_shot_validate(s);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_msg_init(&m, PPCP_MT_SHOT, 1);
    if (rc != PPCP_OK)
        return rc;
    m.body.shot.shot = *s;
    return ppcp_peer_send(p, PPCP_CHANNEL_CONTROL, &m);
}

ppcp_result ppcp_peer_shot_link(ppcp_peer *p, const ppcp_shot_link *l)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || l == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_shot_link_validate(l);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_msg_init(&m, PPCP_MT_SHOT_LINK, 1);
    if (rc != PPCP_OK)
        return rc;
    m.body.shot_link.link = *l;
    return ppcp_peer_send(p, PPCP_CHANNEL_CONTROL, &m);
}

/* ================================================ CORE §5.13 — extension */

/* Bytewise, so the ordering is total and identical at both ends. */
static int id_cmp(const ppcp_id *a, const ppcp_id *b)
{
    size_t n = (a->len < b->len) ? a->len : b->len;
    int    r = (n == 0) ? 0 : memcmp(a->v, b->v, n);
    if (r != 0)
        return r;
    if (a->len == b->len)
        return 0;
    return (a->len < b->len) ? -1 : 1;
}

ppcp_result ppcp_shot_attach_candidate_id(ppcp_shot *s, const ppcp_id *candidate_id)
{
    size_t i, at;

    if (s == NULL || candidate_id == NULL || !ppcp_id_is_set(candidate_id))
        return PPCP_ERR_INVALID;
    for (i = 0; i < s->candidate_count; i++)
        if (ppcp_id_equal(&s->candidates[i], candidate_id))
            return PPCP_OK;          /* additive and idempotent (5.13e) */
    if (s->candidate_count == PPCP_SHOT_MAX_CANDIDATES)
        return PPCP_ERR_LIMIT;

    /* Kept sorted, so two peers applying two extensions in either order end
     * with the same LIST and not merely the same set — which is what makes
     * "converges" checkable by byte comparison (CT-I35). */
    at = s->candidate_count;
    for (i = 0; i < s->candidate_count; i++) {
        if (id_cmp(candidate_id, &s->candidates[i]) < 0) {
            at = i;
            break;
        }
    }
    for (i = s->candidate_count; i > at; i--)
        s->candidates[i] = s->candidates[i - 1];
    s->candidates[at] = *candidate_id;
    s->candidate_count++;
    return PPCP_OK;
}

ppcp_result ppcp_shot_attach_candidate(ppcp_shot *s, const ppcp_candidate *c)
{
    if (c == NULL)
        return PPCP_ERR_INVALID;
    /* ⚠ Takes a Candidate and reaches only for its `id`.  There is
     * deliberately no parameter here through which `t0` could move (I7,
     * 5.13b): a late Candidate attaches, it does not re-decide the event. */
    return ppcp_shot_attach_candidate_id(s, &c->id);
}

ppcp_result ppcp_shot_adopt_extension(ppcp_shot *dst, const ppcp_shot *incoming)
{
    size_t      i;
    ppcp_result rc;

    if (dst == NULL || incoming == NULL)
        return PPCP_ERR_INVALID;
    /* 5.13d / 7.2c / I7 — `id`, `t0`, `authority` and `issued_by` are the
     * issuer's and are never changed by another peer.  Refusing here is what
     * stops this function being a merge, which I9 forbids: two Shots that
     * disagree about any of the four are two Shots, and reconciliation makes a
     * `shot_link` out of them rather than one Shot. */
    if (!ppcp_id_equal(&dst->id, &incoming->id))
        return PPCP_ERR_INVALID;
    if (!ppcp_id_equal(&dst->t0.tb, &incoming->t0.tb) || dst->t0.ns != incoming->t0.ns)
        return PPCP_ERR_INVALID;
    if (dst->authority != incoming->authority)
        return PPCP_ERR_INVALID;
    if (!ppcp_id_equal(&dst->issued_by, &incoming->issued_by))
        return PPCP_ERR_INVALID;

    for (i = 0; i < incoming->candidate_count; i++) {
        rc = ppcp_shot_attach_candidate_id(dst, &incoming->candidates[i]);
        if (rc != PPCP_OK)
            return rc;
    }
    for (i = 0; i < incoming->capture_count; i++) {
        size_t j;
        bool   have = false;
        for (j = 0; j < dst->capture_count; j++)
            if (ppcp_id_equal(&dst->captures[j], &incoming->captures[i]))
                have = true;
        if (have)
            continue;
        if (dst->capture_count == PPCP_SHOT_MAX_CAPTURES)
            return PPCP_ERR_LIMIT;
        dst->captures[dst->capture_count++] = incoming->captures[i];
    }
    return PPCP_OK;
}

/* ====================================================== CORE §8.2i — Mint */

typedef struct mint_pending {
    bool           in_use;
    ppcp_candidate c;
    bool           has_ref;      /* expressible in Session.timebase_ref (8.2i1) */
    int64_t        at_ref_ns;
    bool           answered;     /* a `shot` referenced it */
    bool           minted;
    bool           declined;     /* the promotion policy said no (I32) */
    /* F-D5-1 — the Shot this Candidate became, kept so the embedding can read
     * back what a pump minted instead of decoding its own queued frames.  The
     * arbiter has always had ppcp_arbiter_shot_at(); this is its twin. */
    ppcp_shot      shot;
    size_t         mint_order;   /* 0-based, in mint order */
} mint_pending;

struct ppcp_mint {
    ppcp_peer            *p;
    ppcp_id_fn            id_fn;
    void                 *id_ctx;
    ppcp_promotion_policy promote;
    void                 *promote_ctx;
    mint_pending          pend[PPCP_MINT_MAX_PENDING];
    size_t                minted;
};

size_t ppcp_mint_sizeof(void) { return sizeof(struct ppcp_mint); }

ppcp_result ppcp_mint_new(void *storage, size_t storage_len, ppcp_peer *p,
                          ppcp_id_fn id_fn, void *id_ctx, ppcp_mint **out)
{
    ppcp_mint *m;

    if (storage == NULL || p == NULL || id_fn == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    if (storage_len < sizeof(*m))
        return PPCP_ERR_NOSPACE;
    /* 8.3d — "a peer that issues Shots implements the Mint profile."  A peer
     * that minted without declaring it fails CONF §1d, so the object it would
     * have minted with does not exist. */
    if (!ppcp_peer_declares(p, PPCP_PROFILE_MINT))
        return PPCP_ERR_INVALID;

    m = (ppcp_mint *)storage;
    memset(m, 0, sizeof(*m));
    m->p      = p;
    m->id_fn  = id_fn;
    m->id_ctx = id_ctx;
    *out = m;
    return PPCP_OK;
}

ppcp_result ppcp_mint_set_promotion_policy(ppcp_mint *m, ppcp_promotion_policy fn, void *ctx)
{
    if (m == NULL)
        return PPCP_ERR_INVALID;
    m->promote     = fn;
    m->promote_ctx = ctx;
    return PPCP_OK;
}

/* Converts a Candidate's instant into `Session.timebase_ref`, or says it
 * cannot.  8.2i1: there is no fallback and no zero offset — a peer that
 * substituted one would be doing exactly what 5.4b refuses. */
static bool mint_to_ref(const ppcp_mint *m, const ppcp_candidate *c, int64_t *out_ns)
{
    const ppcp_id *ref = ppcp_peer_timebase_ref(m->p);
    ppcp_instant   at_ref;

    if (ref == NULL)
        return false;
    if (ppcp_relations_convert(ppcp_peer_relations(m->p), &c->at, ref, &at_ref) != PPCP_OK)
        return false;
    *out_ns = at_ref.ns;
    return true;
}

ppcp_result ppcp_mint_observe_own(ppcp_mint *m, const ppcp_candidate *c)
{
    size_t i, slot = PPCP_MINT_MAX_PENDING;

    if (m == NULL || c == NULL)
        return PPCP_ERR_INVALID;
    for (i = 0; i < PPCP_MINT_MAX_PENDING; i++) {
        if (m->pend[i].in_use && ppcp_id_equal(&m->pend[i].c.id, &c->id))
            return PPCP_OK;                      /* already held */
        if (!m->pend[i].in_use && slot == PPCP_MINT_MAX_PENDING)
            slot = i;
    }
    if (slot == PPCP_MINT_MAX_PENDING)
        return PPCP_ERR_LIMIT;

    memset(&m->pend[slot], 0, sizeof(m->pend[slot]));
    m->pend[slot].in_use  = true;
    m->pend[slot].c       = *c;
    m->pend[slot].has_ref = mint_to_ref(m, c, &m->pend[slot].at_ref_ns);
    return PPCP_OK;
}

ppcp_result ppcp_mint_observe_shot(ppcp_mint *m, const ppcp_shot *s)
{
    size_t i, j;

    if (m == NULL || s == NULL)
        return PPCP_ERR_INVALID;
    for (i = 0; i < PPCP_MINT_MAX_PENDING; i++) {
        if (!m->pend[i].in_use)
            continue;
        for (j = 0; j < s->candidate_count; j++) {
            if (ppcp_id_equal(&m->pend[i].c.id, &s->candidates[j])) {
                /* 8.2i — the deadline runs only "with no `shot` referencing
                 * it".  One does now. */
                m->pend[i].answered = true;
                break;
            }
        }
    }
    return PPCP_OK;
}

ppcp_result ppcp_mint_pump(ppcp_mint *m, int64_t now_ref_ns, size_t *out_minted)
{
    const ppcp_body_session_open *sp;
    const ppcp_id *ref, *sess;
    size_t   i, made = 0;
    int64_t  hold_ns = 0, margin_ns = 0;
    bool     zero_host;

    if (m == NULL)
        return PPCP_ERR_INVALID;
    if (out_minted != NULL)
        *out_minted = 0;

    ref  = ppcp_peer_timebase_ref(m->p);
    sess = ppcp_peer_session_id(m->p);
    if (ref == NULL || sess == NULL)
        return PPCP_ERR_NOT_FOUND;     /* no Session: nothing to mint into */

    zero_host = ppcp_peer_zero_host(m->p);
    sp        = ppcp_peer_session_params(m->p);
    if (!zero_host && sp != NULL && sp->has_arbitration) {
        hold_ns = sp->issue_hold_ns;
        /* 8.2i — "plus a margin of at least one `heartbeat_interval_ms` to
         * cover the link". */
        margin_ns = (int64_t)(sp->has_heartbeat_interval ? sp->heartbeat_interval_ms
                                                         : PPCP_DEFAULT_HEARTBEAT_MS) *
                    1000000;
    }

    for (i = 0; i < PPCP_MINT_MAX_PENDING; i++) {
        mint_pending *e = &m->pend[i];
        ppcp_id       shot_id;
        ppcp_instant  t0;
        ppcp_shot     sh;

        if (!e->in_use || e->minted || e->answered || e->declined)
            continue;
        /* A relation may have arrived since the nomination.  8.2i1 is a
         * standing condition, not a one-off verdict. */
        if (!e->has_ref)
            e->has_ref = mint_to_ref(m, &e->c, &e->at_ref_ns);
        if (!e->has_ref)
            continue;   /* 8.2i1 — retained with no Shot, which is legal (I8) */

        /* 8.3a — with no arbitrating host there is no deadline and no window.
         * With one, 8.2i's is `issue_hold_ns` plus a heartbeat interval. */
        if (!zero_host && now_ref_ns < e->at_ref_ns + hold_ns + margin_ns)
            continue;

        /* I32 — "only for a Candidate its own promotion policy would have
         * promoted in a hostless session".  Host silence does not promote a
         * Candidate the peer did not believe, and a peer with no policy at all
         * has believed nothing. */
        if (m->promote == NULL || !m->promote(m->promote_ctx, &e->c)) {
            e->declined = true;
            continue;
        }

        if (m->id_fn(m->id_ctx, &shot_id) != PPCP_OK || !ppcp_id_is_set(&shot_id))
            continue;
        if (ppcp_instant_make(&t0, ref->v, ref->len, e->at_ref_ns) != PPCP_OK)
            continue;
        /* 8.3a / I23 — exactly one Candidate, `authority: device`.  There is
         * no loop here and no window: the shape is the invariant. */
        if (ppcp_shot_make(&sh, shot_id.v, sess->v, &t0, PPCP_AUTHORITY_DEVICE,
                           ppcp_peer_id(m->p)->v, e->c.id.v) != PPCP_OK)
            continue;
        /* 8.2j — sent immediately on minting, so the counterpart learns of it
         * without waiting for a payload. */
        if (ppcp_peer_shot(m->p, &sh) != PPCP_OK)
            continue;
        e->minted     = true;
        e->shot       = sh;
        e->mint_order = m->minted;
        m->minted++;
        made++;
    }
    if (out_minted != NULL)
        *out_minted = made;
    return PPCP_OK;
}

size_t ppcp_mint_pending_count(const ppcp_mint *m)
{
    size_t i, n = 0;
    if (m == NULL)
        return 0;
    for (i = 0; i < PPCP_MINT_MAX_PENDING; i++)
        if (m->pend[i].in_use && !m->pend[i].minted && !m->pend[i].answered &&
            !m->pend[i].declined)
            n++;
    return n;
}

size_t ppcp_mint_minted_count(const ppcp_mint *m)
{
    return (m == NULL) ? 0 : m->minted;
}

const ppcp_shot *ppcp_mint_shot_at(const ppcp_mint *m, size_t index)
{
    size_t i;
    if (m == NULL || index >= m->minted)
        return NULL;
    for (i = 0; i < PPCP_MINT_MAX_PENDING; i++)
        if (m->pend[i].in_use && m->pend[i].minted && m->pend[i].mint_order == index)
            return &m->pend[i].shot;
    return NULL;
}

const ppcp_shot *ppcp_mint_shot_for(const ppcp_mint *m, const ppcp_id *candidate_id)
{
    size_t i;
    if (m == NULL || candidate_id == NULL)
        return NULL;
    for (i = 0; i < PPCP_MINT_MAX_PENDING; i++)
        if (m->pend[i].in_use && m->pend[i].minted &&
            ppcp_id_equal(&m->pend[i].c.id, candidate_id))
            return &m->pend[i].shot;
    return NULL;
}

size_t ppcp_mint_retained_count(const ppcp_mint *m)
{
    size_t i, n = 0;
    if (m == NULL)
        return 0;
    for (i = 0; i < PPCP_MINT_MAX_PENDING; i++)
        if (m->pend[i].in_use && !m->pend[i].minted && !m->pend[i].answered)
            n++;
    return n;
}

/* =================================================== CORE §8.2 — Arbitrate */

typedef struct arb_cand {
    ppcp_candidate c;
    int64_t        at_ref_ns;
    double         sigma_ns;
    bool           excluded;    /* 8.2d — retained, but does not set `t0` */
} arb_cand;

typedef struct arb_group {
    bool      in_use;
    bool      issued;
    bool      foreign;      /* 8.2k — the Shot is the device's, not ours */
    bool      late;         /* 8.2h — issued past the mint deadline */
    arb_cand  cands[PPCP_SHOT_MAX_CANDIDATES];
    size_t    count;
    bool      has_earliest;
    int64_t   earliest_ns;  /* the earliest CONTRIBUTING Candidate (8.2g) */
    ppcp_shot shot;
} arb_group;

struct ppcp_arbiter {
    ppcp_peer              *p;
    ppcp_id_fn              id_fn;
    void                   *id_ctx;
    ppcp_arbitration_policy policy;
    void                   *policy_ctx;
    arb_group               g[PPCP_ARBITER_MAX_GROUPS];
    ppcp_candidate          retained[PPCP_ARBITER_MAX_RETAINED];
    size_t                  retained_count;
    size_t                  issued;
    size_t                  late;
};

size_t ppcp_arbiter_sizeof(void) { return sizeof(struct ppcp_arbiter); }

ppcp_result ppcp_arbiter_new(void *storage, size_t storage_len, ppcp_peer *p,
                             ppcp_id_fn id_fn, void *id_ctx, ppcp_arbiter **out)
{
    ppcp_arbiter *a;

    if (storage == NULL || p == NULL || id_fn == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    if (storage_len < sizeof(*a))
        return PPCP_ERR_NOSPACE;
    /* I20 — "arbitration is available only to a peer with `role: host`".  Both
     * halves are checked: the role, which is 5.2a and fixed for the Session,
     * and the profile, which is what confers `shot` and `capture_request`. */
    if (ppcp_peer_get_role(p) != PPCP_ROLE_HOST)
        return PPCP_ERR_INVALID;
    if (!ppcp_peer_declares(p, PPCP_PROFILE_ARBITRATE))
        return PPCP_ERR_INVALID;

    a = (ppcp_arbiter *)storage;
    memset(a, 0, sizeof(*a));
    a->p      = p;
    a->id_fn  = id_fn;
    a->id_ctx = id_ctx;
    *out = a;
    return PPCP_OK;
}

ppcp_result ppcp_arbiter_set_policy(ppcp_arbiter *a, ppcp_arbitration_policy fn, void *ctx)
{
    if (a == NULL)
        return PPCP_ERR_INVALID;
    a->policy     = fn;
    a->policy_ctx = ctx;
    return PPCP_OK;
}

static void arb_retain(ppcp_arbiter *a, const ppcp_candidate *c)
{
    size_t i;
    for (i = 0; i < a->retained_count; i++)
        if (ppcp_id_equal(&a->retained[i].id, &c->id))
            return;
    if (a->retained_count == PPCP_ARBITER_MAX_RETAINED)
        return;   /* the oldest evidence is not dropped for the newest */
    a->retained[a->retained_count++] = *c;
}

/* The instant a group is anchored on, for 8.2b's comparison: the earliest
 * contributing Candidate before issue, and the issued `t0` afterwards.  Single
 * anchor rather than single linkage, because linkage lets a chain of
 * near-misses drag a group arbitrarily wide and 8.2c's window is a tolerance,
 * not a chaining rule. */
static bool arb_anchor(const arb_group *g, int64_t *out)
{
    if (g->issued) {
        *out = g->shot.t0.ns;
        return true;
    }
    if (!g->has_earliest)
        return false;
    *out = g->earliest_ns;
    return true;
}

static int64_t arb_window(const ppcp_arbiter *a)
{
    const ppcp_body_session_open *sp = ppcp_peer_session_params(a->p);
    if (sp != NULL && sp->has_arbitration)
        return sp->coincidence_window_ns;
    return PPCP_DEFAULT_COINCIDENCE_WINDOW_NS;   /* 8.2c — the stated default */
}

static int64_t arb_hold(const ppcp_arbiter *a)
{
    const ppcp_body_session_open *sp = ppcp_peer_session_params(a->p);
    if (sp != NULL && sp->has_arbitration)
        return sp->issue_hold_ns;
    return PPCP_DEFAULT_ISSUE_HOLD_NS;
}

static int64_t arb_margin(const ppcp_arbiter *a)
{
    const ppcp_body_session_open *sp = ppcp_peer_session_params(a->p);
    uint32_t ms = (sp != NULL && sp->has_heartbeat_interval) ? sp->heartbeat_interval_ms
                                                             : PPCP_DEFAULT_HEARTBEAT_MS;
    return (int64_t)ms * 1000000;
}

static ppcp_result arb_add(ppcp_arbiter *a, arb_group *g, const arb_cand *ac)
{
    size_t i;

    for (i = 0; i < g->count; i++)
        if (ppcp_id_equal(&g->cands[i].c.id, &ac->c.id))
            return PPCP_OK;
    if (g->count == PPCP_SHOT_MAX_CANDIDATES)
        return PPCP_ERR_LIMIT;
    g->cands[g->count++] = *ac;

    /* 8.2g — the hold is opened by the EARLIEST Candidate contributing to the
     * Shot.  An excluded one does not open it, because it is not contributing
     * to anything. */
    if (!ac->excluded && (!g->has_earliest || ac->at_ref_ns < g->earliest_ns)) {
        g->has_earliest = true;
        g->earliest_ns  = ac->at_ref_ns;
    }
    /* 8.2e — the Shot is already issued, so this attaches.  `t0` is not
     * revised, and this function has no way to revise it. */
    if (g->issued) {
        ppcp_result rc = ppcp_shot_attach_candidate_id(&g->shot, &ac->c.id);
        if (rc != PPCP_OK)
            return rc;
        /* 7.2g — re-sent with the extended list and every other field
         * unchanged, whether the Shot is this host's or the device's. */
        return ppcp_peer_shot(a->p, &g->shot);
    }
    (void)a;
    return PPCP_OK;
}

ppcp_result ppcp_arbiter_observe(ppcp_arbiter *a, const ppcp_candidate *c,
                                 bool *out_excluded)
{
    const ppcp_id *ref;
    ppcp_instant   at_ref;
    arb_cand       ac;
    const ppcp_timebase_relation *rel;
    int64_t        window;
    size_t         i, free_slot = PPCP_ARBITER_MAX_GROUPS;
    ppcp_result    rc;

    if (a == NULL || c == NULL)
        return PPCP_ERR_INVALID;
    if (out_excluded != NULL)
        *out_excluded = false;
    ref = ppcp_peer_timebase_ref(a->p);
    if (ref == NULL)
        return PPCP_ERR_NOT_FOUND;

    memset(&ac, 0, sizeof(ac));
    ac.c = *c;

    /* 8.2a — converted into `timebase_ref` with the CURRENT relation set,
     * before anything is compared.  The canonical-instant conversion has
     * already been applied by the nominator (5.12e); applying it again here
     * would double the correction (I33), which is why there is no call to
     * ppcp_canonical_instant() anywhere in this file below Detect. */
    rc = ppcp_relations_convert(ppcp_peer_relations(a->p), &c->at, ref, &at_ref);
    if (rc != PPCP_OK) {
        /* 8.2d — missing, or `unrelated`.  Excluded and RETAINED: there is not
         * even an instant to group it by, and inventing one is what 5.4b and
         * 8.1e both forbid.  The pairing "host ↔ peer declaring `unrelated`
         * timebases" puts every candidate from that peer right here. */
        arb_retain(a, c);
        if (out_excluded != NULL)
            *out_excluded = true;
        return PPCP_OK;
    }
    ac.at_ref_ns = at_ref.ns;
    (void)ppcp_relations_sigma_ns(ppcp_peer_relations(a->p), &c->at, ref, &ac.sigma_ns);
    rel = ppcp_relations_find(ppcp_peer_relations(a->p), &c->at.tb, ref);

    /* 8.2d's third case — "too uncertain under host policy".  The policy is
     * the embedding's and the number it judges is handed to it; the library
     * holds no threshold (I14). */
    if (a->policy != NULL && !a->policy(a->policy_ctx, c, rel, ac.sigma_ns))
        ac.excluded = true;
    if (out_excluded != NULL)
        *out_excluded = ac.excluded;

    window = arb_window(a);
    for (i = 0; i < PPCP_ARBITER_MAX_GROUPS; i++) {
        int64_t anchor;
        if (!a->g[i].in_use) {
            if (free_slot == PPCP_ARBITER_MAX_GROUPS)
                free_slot = i;
            continue;
        }
        if (!arb_anchor(&a->g[i], &anchor))
            continue;
        /* 8.2b — the same Shot if the converted instants fall within
         * `coincidence_window_ns`. */
        if (at_ref.ns - anchor <= window && anchor - at_ref.ns <= window)
            return arb_add(a, &a->g[i], &ac);
    }

    /* An excluded Candidate never CREATES a Shot: 8.2d takes it out of
     * arbitration, and arbitration is what issues.  It is retained, and a Shot
     * issued near it later will pick it up. */
    if (ac.excluded) {
        arb_retain(a, c);
        return PPCP_OK;
    }
    if (free_slot == PPCP_ARBITER_MAX_GROUPS) {
        arb_retain(a, c);
        return PPCP_ERR_LIMIT;
    }
    memset(&a->g[free_slot], 0, sizeof(a->g[free_slot]));
    a->g[free_slot].in_use = true;
    return arb_add(a, &a->g[free_slot], &ac);
}

static ppcp_result arb_issue(ppcp_arbiter *a, arb_group *g)
{
    const ppcp_id *ref  = ppcp_peer_timebase_ref(a->p);
    const ppcp_id *sess = ppcp_peer_session_id(a->p);
    size_t         i, best = PPCP_SHOT_MAX_CANDIDATES;
    ppcp_id        shot_id;
    ppcp_instant   t0;
    ppcp_result    rc;

    if (ref == NULL || sess == NULL)
        return PPCP_ERR_NOT_FOUND;

    /* WHICH contributing Candidate sets `t0`.  The specification does not say,
     * and it is a choice a host has to make: 8.2h's rationale is a fast IMU
     * nomination followed by a sample-accurate acoustic one resolving to the
     * acoustic instant, which is the LEAST UNCERTAIN, not the earliest and not
     * the most confident.  So: the smallest combined timing uncertainty — the
     * relation's sigma at that instant, widened by `tof_correction`'s where
     * there is one — and the earliest instant breaks a tie, so the choice is
     * deterministic and independent of arrival order.
     *
     * ⚠ `confidence` is NOT consulted.  It is the nominator's belief that the
     * event happened, not a statement about WHEN, and using it here would be
     * the library holding a quality threshold (I14). */
    for (i = 0; i < g->count; i++) {
        double si, sb;
        if (g->cands[i].excluded)
            continue;
        if (best == PPCP_SHOT_MAX_CANDIDATES) {
            best = i;
            continue;
        }
        si = g->cands[i].sigma_ns;
        sb = g->cands[best].sigma_ns;
        if (g->cands[i].c.has_tof_correction)
            si += g->cands[i].c.tof_correction.sigma_ns;
        if (g->cands[best].c.has_tof_correction)
            sb += g->cands[best].c.tof_correction.sigma_ns;
        if (si < sb || (si == sb && g->cands[i].at_ref_ns < g->cands[best].at_ref_ns))
            best = i;
    }
    if (best == PPCP_SHOT_MAX_CANDIDATES)
        return PPCP_ERR_NOT_FOUND;   /* every Candidate excluded: no Shot (8.2d) */

    rc = a->id_fn(a->id_ctx, &shot_id);
    if (rc != PPCP_OK || !ppcp_id_is_set(&shot_id))
        return PPCP_ERR_INVALID;
    rc = ppcp_instant_make(&t0, ref->v, ref->len, g->cands[best].at_ref_ns);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_shot_make(&g->shot, shot_id.v, sess->v, &t0, PPCP_AUTHORITY_HOST,
                        ppcp_peer_id(a->p)->v, g->cands[best].c.id.v);
    if (rc != PPCP_OK)
        return rc;
    /* 8.2f — every contributing AND excluded Candidate.  Exclusion is a
     * conclusion; the Candidate remains evidence (I8), and a consumer may
     * re-derive t0 later with a better clock estimate. */
    for (i = 0; i < g->count; i++) {
        rc = ppcp_shot_attach_candidate_id(&g->shot, &g->cands[i].c.id);
        if (rc != PPCP_OK)
            return rc;
    }
    g->issued = true;
    a->issued++;
    return ppcp_peer_shot(a->p, &g->shot);
}

ppcp_result ppcp_arbiter_pump(ppcp_arbiter *a, int64_t now_ref_ns, size_t *out_issued)
{
    size_t  i, n = 0;
    int64_t hold, margin;

    if (a == NULL)
        return PPCP_ERR_INVALID;
    if (out_issued != NULL)
        *out_issued = 0;
    hold   = arb_hold(a);
    margin = arb_margin(a);

    for (i = 0; i < PPCP_ARBITER_MAX_GROUPS; i++) {
        arb_group *g = &a->g[i];
        if (!g->in_use || g->issued || !g->has_earliest)
            continue;
        /* 8.2h — no earlier than `issue_hold_ns` after the earliest
         * contributing Candidate. */
        if (now_ref_ns < g->earliest_ns + hold)
            continue;
        /* 8.2h — and no later than the mint deadline.  Past it, the nominating
         * peer is entitled to mint and two Shots for one event become
         * possible with no defect on either side; the Shot is still issued,
         * because not issuing is worse, and the lateness is counted so a host
         * can find out it is running slow. */
        if (now_ref_ns > g->earliest_ns + hold + margin) {
            g->late = true;
            a->late++;
        }
        if (arb_issue(a, g) == PPCP_OK)
            n++;
    }
    if (out_issued != NULL)
        *out_issued = n;
    return PPCP_OK;
}

ppcp_result ppcp_arbiter_observe_shot(ppcp_arbiter *a, const ppcp_shot *s)
{
    size_t i, j, k;

    if (a == NULL || s == NULL)
        return PPCP_ERR_INVALID;

    for (i = 0; i < PPCP_ARBITER_MAX_GROUPS; i++) {
        arb_group *g = &a->g[i];
        bool       shares = false;
        if (!g->in_use)
            continue;

        /* 5.13d — an extension to a Shot this host issued is ADOPTED. */
        if (g->issued && ppcp_id_equal(&g->shot.id, &s->id)) {
            return ppcp_shot_adopt_extension(&g->shot, s);
        }

        for (j = 0; j < g->count && !shares; j++)
            for (k = 0; k < s->candidate_count; k++)
                if (ppcp_id_equal(&g->cands[j].c.id, &s->candidates[k])) {
                    shares = true;
                    break;
                }
        if (!shares)
            continue;

        if (s->authority != PPCP_AUTHORITY_DEVICE)
            continue;

        if (!g->issued) {
            /* 8.2k / I35 — the device's Shot is the one that exists.  It may
             * already anchor an extracted Capture, so the host attaches to it
             * rather than issuing a competing Shot, and `t0` stays the
             * device's. */
            g->shot    = *s;
            g->issued  = true;
            g->foreign = true;
            for (j = 0; j < g->count; j++) {
                ppcp_result rc = ppcp_shot_attach_candidate_id(&g->shot, &g->cands[j].c.id);
                if (rc != PPCP_OK)
                    return rc;
            }
            if (g->shot.candidate_count != s->candidate_count)
                return ppcp_peer_shot(a->p, &g->shot);   /* 7.2f — the extension */
            return PPCP_OK;
        }

        /* 8.2l — both issued, because the two messages crossed.  Neither is
         * withdrawn (I7, I9) and there is deliberately no `withdraw` message
         * to reach for; the host links them and a consumer MUST NOT count two
         * events.  `confirmed_by: observer` is exact by construction here —
         * the two Shots demonstrably reference one Candidate. */
        {
            ppcp_shot_link l;
            ppcp_id        link_id;
            ppcp_result    rc = a->id_fn(a->id_ctx, &link_id);
            if (rc != PPCP_OK)
                return rc;
            rc = ppcp_shot_link_make(&l, link_id.v, g->shot.id.v, s->id.v,
                                     PPCP_LINK_SHARED_CANDIDATE, 1.0);
            if (rc != PPCP_OK)
                return rc;
            rc = ppcp_shot_link_confirm(&l, PPCP_CONFIRMED_BY_OBSERVER);
            if (rc != PPCP_OK)
                return rc;
            return ppcp_peer_shot_link(a->p, &l);
        }
    }
    return PPCP_OK;
}

size_t ppcp_arbiter_group_count(const ppcp_arbiter *a)
{
    size_t i, n = 0;
    if (a == NULL)
        return 0;
    for (i = 0; i < PPCP_ARBITER_MAX_GROUPS; i++)
        if (a->g[i].in_use)
            n++;
    return n;
}

size_t ppcp_arbiter_issued_count(const ppcp_arbiter *a) { return (a == NULL) ? 0 : a->issued; }
size_t ppcp_arbiter_late_count(const ppcp_arbiter *a)   { return (a == NULL) ? 0 : a->late; }

size_t ppcp_arbiter_retained_count(const ppcp_arbiter *a)
{
    return (a == NULL) ? 0 : a->retained_count;
}

const ppcp_shot *ppcp_arbiter_shot_at(const ppcp_arbiter *a, size_t group)
{
    if (a == NULL || group >= PPCP_ARBITER_MAX_GROUPS)
        return NULL;
    if (!a->g[group].in_use || !a->g[group].issued)
        return NULL;
    return &a->g[group].shot;
}

/* ============================================ CORE §8.4 — orphan capture requests */

ppcp_result ppcp_peer_capture_request(ppcp_peer *p, const char *shot_id,
                                      const ppcp_instant *t0, const ppcp_id *stream_ids,
                                      size_t count, ppcp_duration_ns pre_ns,
                                      ppcp_duration_ns post_ns)
{
    ppcp_msg    m;
    ppcp_result rc;
    size_t      i;

    if (p == NULL || shot_id == NULL || t0 == NULL)
        return PPCP_ERR_INVALID;
    if (count > PPCP_MAX_STREAM_IDS)
        return PPCP_ERR_LIMIT;
    if (pre_ns < 0 || post_ns < 0)
        return PPCP_ERR_INVALID;
    rc = ppcp_instant_validate(t0);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_msg_init(&m, PPCP_MT_CAPTURE_REQUEST, 1);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_id_set_z(&m.body.capture_request.shot_id, shot_id);
    if (rc != PPCP_OK)
        return rc;
    m.body.capture_request.t0 = *t0;
    for (i = 0; i < count; i++)
        m.body.capture_request.stream_ids[i] = stream_ids[i];
    m.body.capture_request.stream_id_count = count;
    m.body.capture_request.pre_ns  = pre_ns;
    m.body.capture_request.post_ns = post_ns;
    return ppcp_peer_send(p, PPCP_CHANNEL_CONTROL, &m);
}

ppcp_result ppcp_peer_capture_absent(ppcp_peer *p, const char *capture_id,
                                     const char *shot_id, const char *stream_id,
                                     const char *absent_reason, uint64_t in_reply_to)
{
    ppcp_capture c;
    ppcp_msg     m;
    ppcp_result  rc;

    if (p == NULL || capture_id == NULL || shot_id == NULL || stream_id == NULL ||
        absent_reason == NULL)
        return PPCP_ERR_INVALID;

    /* 8.4b / 7.3b — a Capture, not an `error`.  `completeness: absent` with a
     * reason IS the answer: absence is asserted by the owner, never inferred
     * by the receiver from a payload that did not arrive (I10). */
    rc = ppcp_capture_make_shot(&c, capture_id, shot_id, stream_id, PPCP_ABSENT);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_capture_set_absent_reason(&c, absent_reason);
    if (rc != PPCP_OK)
        return rc;

    rc = ppcp_msg_init(&m, PPCP_MT_CAPTURE_ANNOUNCE, 1);
    if (rc != PPCP_OK)
        return rc;
    m.body.capture_announce.capture = c;
    rc = ppcp_msg_set_reply_to(&m, in_reply_to);
    if (rc != PPCP_OK)
        return rc;
    return ppcp_peer_send(p, PPCP_CHANNEL_CONTROL, &m);
}
