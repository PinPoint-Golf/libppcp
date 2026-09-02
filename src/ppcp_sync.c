/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * Clock synchronisation and the relation set — CORE §5.4, §5.4.1, §6.3.
 * Work package L9.  See include/ppcp/sync.h for the contract.
 *
 * There is no math.h here (tests/purity.cmake forbids it), so the one
 * transcendental this file needs — a square root, for two sigmas — is written
 * out.  Everything else is integer arithmetic or a least-squares fit that is
 * four sums and a division.
 */
#include "ppcp_sync.h"

#include <string.h>
#include <limits.h>

/* --------------------------------------------------------------- sqrt
 *
 * Newton's method from a halved-exponent seed obtained by bisection on the
 * value itself.  Twenty iterations is far more than double precision needs and
 * costs nothing at the rate this is called (once per published relation). */
static double sync_sqrt(double v)
{
    double x;
    int    i;

    if (!(v > 0.0))
        return 0.0;
    /* Seed: scale into [1, 4) by repeated halving/doubling of the square, so
     * the first Newton step is already close for any magnitude. */
    x = v;
    if (x > 4.0) {
        double d = 1.0;
        while (x > 4.0) { x *= 0.25; d *= 2.0; }
        x = d * 2.0;
    } else if (x < 0.25) {
        double d = 1.0;
        while (x < 0.25) { x *= 4.0; d *= 0.5; }
        x = d * 0.5;
    } else {
        x = 1.0;
    }
    for (i = 0; i < 24; i++) {
        if (x <= 0.0)
            return 0.0;
        x = 0.5 * (x + v / x);
    }
    return x;
}

static double sync_abs(double v) { return (v < 0.0) ? -v : v; }

static int64_t sync_round(double v)
{
    if (v >= 0.0)
        return (int64_t)(v + 0.5);
    return -(int64_t)(-v + 0.5);
}

/* ======================================================== the estimator */

size_t ppcp_sync_estimator_sizeof(void) { return sizeof(struct ppcp_sync_estimator); }

ppcp_result ppcp_sync_estimator_new(void *storage, size_t storage_len,
                                    const char *local_tb, const char *remote_tb,
                                    ppcp_sync_estimator **out)
{
    ppcp_sync_estimator *e;
    ppcp_result          rc;

    if (storage == NULL || out == NULL || local_tb == NULL)
        return PPCP_ERR_INVALID;
    if (storage_len < sizeof(*e))
        return PPCP_ERR_NOSPACE;

    e = (ppcp_sync_estimator *)storage;
    memset(e, 0, sizeof(*e));
    rc = ppcp_id_set_z(&e->local_tb, local_tb);
    if (rc != PPCP_OK)
        return rc;
    if (remote_tb != NULL) {
        rc = ppcp_id_set_z(&e->remote_tb, remote_tb);
        if (rc != PPCP_OK)
            return rc;
        /* I4 — a relation whose two ends are one timebase is not a relation.
         * Refusing it here means the estimator cannot produce one either. */
        if (ppcp_id_equal(&e->local_tb, &e->remote_tb))
            return PPCP_ERR_INVALID;
        e->has_remote = true;
    }
    e->min_rtt_ns = INT64_MAX;
    *out = e;
    return PPCP_OK;
}

ppcp_result ppcp_sync_estimator_set_remote_tb(ppcp_sync_estimator *e, const char *tb,
                                              size_t len)
{
    ppcp_id id;
    ppcp_result rc;

    if (e == NULL || tb == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_id_set(&id, tb, len);
    if (rc != PPCP_OK)
        return rc;
    if (ppcp_id_equal(&e->local_tb, &id))
        return PPCP_ERR_INVALID;                       /* I4 */
    if (e->has_remote) {
        if (ppcp_id_equal(&e->remote_tb, &id))
            return PPCP_OK;
        /* 6.1b: `t2` and `t3` are in one responder timebase and that timebase
         * is one the responder declared.  A responder that changed it has not
         * refined this estimate, it has ended it. */
        if (e->count != 0)
            return PPCP_ERR_INVALID;
    }
    e->remote_tb  = id;
    e->has_remote = true;
    return PPCP_OK;
}

void ppcp_sync_estimator_restart(ppcp_sync_estimator *e)
{
    if (e == NULL)
        return;
    /* The window goes; the published estimate stays.  6.3c says a thermal
     * event makes the fit stale, and 6.3e says the published value is never
     * stepped — discarding both would do exactly the thing 6.3e forbids the
     * moment the next burst lands. */
    e->count      = 0;
    e->next       = 0;
    e->min_rtt_ns = INT64_MAX;
}

size_t ppcp_sync_estimator_count(const ppcp_sync_estimator *e)
{
    return (e == NULL) ? 0 : (size_t)e->observed;
}

bool ppcp_sync_estimator_has_estimate(const ppcp_sync_estimator *e)
{
    return (e != NULL) && e->has_estimate && e->has_remote;
}

int64_t ppcp_sync_estimator_min_rtt_ns(const ppcp_sync_estimator *e)
{
    if (e == NULL || e->count == 0)
        return 0;
    return e->min_rtt_ns;
}

const ppcp_id *ppcp_sync_estimator_local_tb(const ppcp_sync_estimator *e)
{
    return (e == NULL) ? NULL : &e->local_tb;
}

const ppcp_id *ppcp_sync_estimator_remote_tb(const ppcp_sync_estimator *e)
{
    if (e == NULL || !e->has_remote)
        return NULL;
    return &e->remote_tb;
}

/* 6.3f1 — the RTT gate, relative to the lowest RTT seen: the greater of one
 * minimum above it and PPCP_SYNC_ADMIT_MARGIN_NS.  `order` is the window
 * sorted by ascending RTT (or NULL to count without one). */
static size_t sync_admitted(const ppcp_sync_estimator *e, const size_t *order)
{
    size_t  i, n = 0;
    int64_t margin, gate;
    if (e->count == 0 || e->min_rtt_ns == INT64_MAX)
        return 0;
    margin = (e->min_rtt_ns > PPCP_SYNC_ADMIT_MARGIN_NS) ? e->min_rtt_ns
                                                          : PPCP_SYNC_ADMIT_MARGIN_NS;
    gate = e->min_rtt_ns + margin;
    for (i = 0; i < e->count; i++) {
        int64_t rtt = e->s[order ? order[i] : i].rtt_ns;
        if (rtt <= gate)
            n++;
        else if (order != NULL)
            break;   /* sorted: nothing after this is inside either */
    }
    return n;
}

size_t ppcp_sync_estimator_admitted(const ppcp_sync_estimator *e)
{
    return (e == NULL) ? 0 : sync_admitted(e, NULL);
}

/* The fit.  Least squares of offset against local time over the retained
 * exchanges inside the RTT gate — 6.3a's "offset AND rate", 6.3f's
 * minimum-RTT filtering. */
static void sync_refit(ppcp_sync_estimator *e)
{
    /* Indices into e->s, ordered by ascending RTT.  A selection sort over at
     * most 32 entries: the window is small and the code is readable, which
     * matters more here than the constant factor. */
    size_t  order[PPCP_SYNC_WINDOW];
    size_t  i, j, n, keep;
    int64_t x0;
    double  sx = 0.0, sy = 0.0, sxy = 0.0, sxx = 0.0;
    double  denom, a, b, var = 0.0, span, raw_off, predicted;
    int64_t x_hi = 0, x_lo = 0, obs_ns;

    if (e->count < 2)
        return;

    for (i = 0; i < e->count; i++)
        order[i] = i;
    for (i = 0; i + 1 < e->count; i++) {
        size_t best = i;
        for (j = i + 1; j < e->count; j++)
            if (e->s[order[j]].rtt_ns < e->s[order[best]].rtt_ns)
                best = j;
        if (best != i) {
            size_t t = order[i]; order[i] = order[best]; order[best] = t;
        }
    }

    keep = sync_admitted(e, order);
    if (keep < PPCP_SYNC_ADMIT_MIN)
        keep = PPCP_SYNC_ADMIT_MIN;
    if (keep < 2)
        keep = 2;
    if (keep > e->count)
        keep = e->count;
    n = keep;

    x0 = e->s[order[0]].t_local_ns;
    for (i = 0; i < n; i++) {
        double x = (double)(e->s[order[i]].t_local_ns - x0);
        double y = (double)e->s[order[i]].offset_ns;
        sx += x; sy += y; sxy += x * y; sxx += x * x;
    }
    denom = (double)n * sxx - sx * sx;
    if (denom == 0.0)
        return;   /* every retained exchange at one instant: no rate yet (6.3a) */

    b = ((double)n * sxy - sx * sy) / denom;
    a = (sy - b * sx) / (double)n;

    for (i = 0; i < n; i++) {
        double  x  = (double)(e->s[order[i]].t_local_ns - x0);
        double  y  = (double)e->s[order[i]].offset_ns;
        double  r  = y - (a + b * x);
        int64_t dx = e->s[order[i]].t_local_ns - x0;
        var += r * r;
        if (i == 0 || dx > x_hi) x_hi = dx;
        if (i == 0 || dx < x_lo) x_lo = dx;
    }
    var = (n > 2) ? var / (double)(n - 2) : 0.0;

    /* The most recent exchange in the WINDOW, not merely in the retained set:
     * `observed_at` is when this relation was observed, and the caller's
     * conversions extrapolate from it. */
    obs_ns = e->s[0].t_local_ns;
    for (i = 1; i < e->count; i++)
        if (e->s[i].t_local_ns > obs_ns)
            obs_ns = e->s[i].t_local_ns;

    raw_off = a + b * (double)(obs_ns - x0);

    /* 6.3e — filtered, never stepped.  The previous estimate is first carried
     * forward to `obs_ns` at its own rate, so the filter measures a genuine
     * disagreement rather than the drift it already predicted. */
    if (!e->has_estimate) {
        e->offset_ns = raw_off;
        e->skew_ppm  = b * 1.0e6;
    } else {
        predicted = e->offset_ns +
                    e->skew_ppm * 1.0e-6 * (double)(obs_ns - e->observed_at_ns);
        e->offset_ns = predicted + (raw_off - predicted) / (double)PPCP_SYNC_FILTER_DIV;
        e->skew_ppm  = e->skew_ppm + (b * 1.0e6 - e->skew_ppm) / (double)PPCP_SYNC_FILTER_DIV;
    }
    e->observed_at_ns = obs_ns;
    e->has_estimate   = true;

    /* offset sigma: the fit's residual dispersion, and the half-RTT bound that
     * a two-way exchange cannot see past.  Delay asymmetry is a systematic
     * error, so it is combined in quadrature rather than ignored — a peer
     * publishing only the residual would claim a precision the method does not
     * have. */
    {
        double resid = sync_sqrt(var);
        double asym  = (e->min_rtt_ns > 0) ? (double)e->min_rtt_ns * 0.5 : 0.0;
        e->offset_sigma_ns = sync_sqrt(resid * resid + asym * asym);
    }

    /* skew sigma: the standard error of the fitted slope, floored by what the
     * offset uncertainty alone implies over the span.  Two exchanges fit a
     * line exactly and their residual is zero, which is not the same as
     * knowing the rate. */
    {
        double se_slope = (n > 2) ? sync_sqrt(var * (double)n / denom) * 1.0e6 : 0.0;
        double se_floor;
        span = (double)(x_hi - x_lo);
        se_floor = (span > 0.0) ? (e->offset_sigma_ns * 1.41421356237 / span) * 1.0e6 : 0.0;
        e->skew_sigma_ppm = (se_slope > se_floor) ? se_slope : se_floor;
    }
}

ppcp_result ppcp_sync_estimator_observe(ppcp_sync_estimator *e, int64_t t1, int64_t t2,
                                        int64_t t3, int64_t t4)
{
    int64_t rtt, offset, mid;

    if (e == NULL)
        return PPCP_ERR_INVALID;

    /* rtt = (t4 − t1) − (t3 − t2): the round trip with the responder's
     * residence time removed.  6.1c permits t3 == t2, which declares the
     * residence time is included in the measurement instead. */
    rtt    = (t4 - t1) - (t3 - t2);
    offset = ((t2 - t1) + (t3 - t4)) / 2;
    mid    = t1 + (t4 - t1) / 2;
    if (rtt < 0)
        return PPCP_ERR_MALFORMED;

    {
        size_t slot;
        if (e->count < PPCP_SYNC_WINDOW) {
            slot = e->next;
            e->next = (e->next + 1u) % PPCP_SYNC_WINDOW;
            e->count++;
        } else {
            /* 6.3f1 / E69 — the victim is the oldest exchange past the age
             * bound where one exists, else the highest-RTT one.  Never the
             * arrival order: a noisy burst would otherwise sit in the fit for
             * as long as it took the maintenance cadence to walk past it. */
            size_t  i, victim = 0;
            bool    aged = false;
            int64_t oldest = INT64_MAX, worst = -1;
            for (i = 0; i < e->count; i++) {
                if (mid - e->s[i].t_local_ns > PPCP_SYNC_MAX_AGE_NS &&
                    e->s[i].t_local_ns < oldest) {
                    oldest = e->s[i].t_local_ns;
                    victim = i;
                    aged   = true;
                }
            }
            if (!aged) {
                for (i = 0; i < e->count; i++) {
                    if (e->s[i].rtt_ns > worst) {
                        worst  = e->s[i].rtt_ns;
                        victim = i;
                    }
                }
            }
            slot = victim;
        }
        e->s[slot].t_local_ns = mid;
        e->s[slot].offset_ns  = offset;
        e->s[slot].rtt_ns     = rtt;
    }
    e->observed++;
    if (rtt < e->min_rtt_ns)
        e->min_rtt_ns = rtt;

    sync_refit(e);
    return PPCP_OK;
}

ppcp_result ppcp_sync_estimator_relation(const ppcp_sync_estimator *e,
                                         ppcp_timebase_relation *out)
{
    ppcp_instant observed_at;
    ppcp_result  rc;

    if (e == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    /* 6.3a — offset AND rate.  Until the exchange has produced both there is
     * no relation to publish, and a one-shot handshake never will. */
    if (!e->has_estimate || !e->has_remote)
        return PPCP_ERR_NOT_FOUND;

    rc = ppcp_instant_make(&observed_at, e->local_tb.v, e->local_tb.len, e->observed_at_ns);
    if (rc != PPCP_OK)
        return rc;
    return ppcp_relation_make_affine(out, e->local_tb.v, e->remote_tb.v,
                                     sync_round(e->offset_ns), e->skew_ppm,
                                     e->offset_sigma_ns, e->skew_sigma_ppm,
                                     PPCP_RELM_ESTIMATED_ONLINE, &observed_at);
}

/* ==================================================== the relation set */

void ppcp_relations_init(ppcp_relation_set *rs)
{
    if (rs != NULL)
        memset(rs, 0, sizeof(*rs));
}

size_t ppcp_relations_count(const ppcp_relation_set *rs)
{
    return (rs == NULL) ? 0 : rs->count;
}

const ppcp_timebase_relation *ppcp_relations_find(const ppcp_relation_set *rs,
                                                  const ppcp_id *from, const ppcp_id *to)
{
    size_t i;
    if (rs == NULL || from == NULL || to == NULL)
        return NULL;
    for (i = 0; i < rs->count; i++)
        if (ppcp_id_equal(&rs->r[i].from, from) && ppcp_id_equal(&rs->r[i].to, to))
            return &rs->r[i];
    return NULL;
}

ppcp_result ppcp_relations_put(ppcp_relation_set *rs, const ppcp_timebase_relation *r)
{
    size_t i;

    if (rs == NULL || r == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_relation_validate(r) != PPCP_OK)
        return PPCP_ERR_INVALID;
    for (i = 0; i < rs->count; i++) {
        if (ppcp_id_equal(&rs->r[i].from, &r->from) && ppcp_id_equal(&rs->r[i].to, &r->to)) {
            rs->r[i] = *r;
            return PPCP_OK;
        }
    }
    if (rs->count == PPCP_RELATION_SET_MAX)
        return PPCP_ERR_LIMIT;
    rs->r[rs->count++] = *r;
    return PPCP_OK;
}

/* The sigma of one relation at one instant: the offset's own sigma, and the
 * rate uncertainty accumulated over every nanosecond since it was observed. */
static double relation_sigma_at(const ppcp_timebase_relation *r, int64_t at_ns)
{
    double elapsed     = (double)(at_ns - r->observed_at.ns);
    double drift_sigma = sync_abs(elapsed) * r->skew_sigma_ppm * 1.0e-6;
    return sync_sqrt(r->offset_sigma_ns * r->offset_sigma_ns + drift_sigma * drift_sigma);
}

/* 5.4d (erratum E67) — an affine relation read the other way.  Where
 *   to = from + offset + skew·(from − observed_at),
 * the inverse is exact:
 *   from = to − offset + skew'·(to − observed_at')
 * with observed_at' = observed_at + offset (the same instant, in `to`) and
 * skew' = −skew / (1 + skew).  Both sigmas carry over unchanged: they are the
 * uncertainty of ONE measurement, and reading it from the other end does not
 * make it a different measurement.  ⚠ This is NOT composition (5.4c, I18):
 * nothing is chained through a third timebase and nothing is invented. */
bool ppcp_relation_invert(const ppcp_timebase_relation *r, ppcp_timebase_relation *out)
{
    ppcp_instant obs;
    double       s, s_inv;
    if (r == NULL || out == NULL || ppcp_relation_validate(r) != PPCP_OK ||
        r->cls != PPCP_REL_AFFINE)
        return false;
    s     = r->skew_ppm * 1.0e-6;
    s_inv = -s / (1.0 + s);
    if (ppcp_instant_make(&obs, r->to.v, r->to.len, r->observed_at.ns + r->offset_ns) != PPCP_OK)
        return false;
    return ppcp_relation_make_affine(out, r->to.v, r->from.v, -r->offset_ns, s_inv * 1.0e6,
                                     r->offset_sigma_ns, r->skew_sigma_ppm, r->method,
                                     &obs) == PPCP_OK;
}

/* 5.4d — which relation answers a conversion `in` → `to_tb`: the one declared
 * in that direction, or the inverse of the one declared the other way, whichever
 * carries the smaller sigma at `in`.  Two peers measuring the same pair of
 * clocks each publish their own estimate; before this, a conversion was bound
 * to whichever peer happened to have declared that DIRECTION, and a host with
 * a 1 ms estimate of the relation waited a minute on the phone's 30 ms one.
 * Returns NULL with *rc set where nothing affine exists. */
static const ppcp_timebase_relation *relations_resolve(const ppcp_relation_set *rs,
                                                       const ppcp_instant *in,
                                                       const ppcp_id *to_tb,
                                                       ppcp_timebase_relation *inv_out,
                                                       ppcp_result *rc)
{
    const ppcp_timebase_relation *direct  = ppcp_relations_find(rs, &in->tb, to_tb);
    const ppcp_timebase_relation *reverse = ppcp_relations_find(rs, to_tb, &in->tb);
    bool direct_ok, inverse_ok;

    /* `unrelated` is complete, and means no mapping (5.4b) — declared in EITHER
     * direction it wins over anything measured the other way.  A peer that says
     * its clock cannot be related is not overruled by a counterpart's fit
     * (IOP-5, 8.2i1: the Candidate is retained ungrouped, not converted). */
    if ((direct != NULL && direct->cls != PPCP_REL_AFFINE) ||
        (reverse != NULL && reverse->cls != PPCP_REL_AFFINE)) {
        *rc = PPCP_ERR_INVALID;
        return NULL;
    }
    direct_ok  = (direct != NULL);
    inverse_ok = (reverse != NULL && ppcp_relation_invert(reverse, inv_out));

    if (direct_ok && inverse_ok) {
        *rc = PPCP_OK;
        return (relation_sigma_at(inv_out, in->ns) < relation_sigma_at(direct, in->ns))
               ? inv_out : direct;
    }
    if (direct_ok)  { *rc = PPCP_OK; return direct; }
    if (inverse_ok) { *rc = PPCP_OK; return inv_out; }
    *rc = PPCP_ERR_NOT_FOUND;        /* not composed, not assumed zero (5.4c, 8.2i1) */
    return NULL;
}

ppcp_result ppcp_relations_convert(const ppcp_relation_set *rs, const ppcp_instant *in,
                                   const ppcp_id *to_tb, ppcp_instant *out)
{
    const ppcp_timebase_relation *r;
    ppcp_timebase_relation        inv;
    ppcp_result                   rc;

    if (rs == NULL || in == NULL || to_tb == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_instant_validate(in) != PPCP_OK)
        return PPCP_ERR_INVALID;

    /* I4 — one clock is one timebase, and identity is never asserted as a
     * relation.  So it is answered here, without one. */
    if (ppcp_id_equal(&in->tb, to_tb)) {
        *out = *in;
        return PPCP_OK;
    }

    r = relations_resolve(rs, in, to_tb, &inv, &rc);
    if (r == NULL)
        return rc;
    return ppcp_relation_apply(r, in, out);
}

ppcp_result ppcp_relations_sigma_ns(const ppcp_relation_set *rs, const ppcp_instant *in,
                                    const ppcp_id *to_tb, double *out_sigma_ns)
{
    const ppcp_timebase_relation *r;
    ppcp_timebase_relation        inv;
    ppcp_result                   rc;

    if (rs == NULL || in == NULL || to_tb == NULL || out_sigma_ns == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_id_equal(&in->tb, to_tb)) {
        *out_sigma_ns = 0.0;
        return PPCP_OK;
    }
    r = relations_resolve(rs, in, to_tb, &inv, &rc);
    if (r == NULL)
        return rc;

    /* The offset was measured at `observed_at`; every nanosecond since then is
     * a nanosecond over which the rate uncertainty has been accumulating.  A
     * consumer handed only `offset_sigma_ns` would trust a two-minute-old
     * relation exactly as much as a fresh one. */
    *out_sigma_ns = relation_sigma_at(r, in->ns);
    return PPCP_OK;
}
