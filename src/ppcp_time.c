/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * PPCP-CORE §5.1, §5.3, §5.4, §5.5 and §6.4.
 */
#include "ppcp/time.h"

#include <string.h>

/* A sigma is a non-negative standard deviation (CORE 5.1).  NaN and infinity
 * are refused here rather than at the far end: a dispersion that is not a
 * number is exactly the "point estimate with no dispersion" I29 exists to
 * prevent, wearing a number's clothes. */
static bool sigma_ok(double s)
{
    if (s != s)                     /* NaN */
        return false;
    if (s < 0.0)
        return false;
    if (s > 1.0e300 && s * 0.5 == s) /* infinity */
        return false;
    return true;
}

static bool finite_ok(double v)
{
    if (v != v)
        return false;
    if ((v > 1.0e300 || v < -1.0e300) && v * 0.5 == v)
        return false;
    return true;
}

/* ------------------------------------------------------------- primitives */

ppcp_result ppcp_instant_make(ppcp_instant *out, const char *tb, size_t tb_len, int64_t ns)
{
    ppcp_result rc;
    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    /* I1: no Instant without a tb.  This is the whole of it — there is no
     * other constructor and the encoder refuses a struct that never went
     * through one. */
    rc = ppcp_id_set(&out->tb, tb, tb_len);
    if (rc != PPCP_OK)
        return rc;
    out->ns = ns;
    return PPCP_OK;
}

ppcp_result ppcp_instant_make_z(ppcp_instant *out, const char *tb, int64_t ns)
{
    return ppcp_instant_make(out, tb, tb ? strlen(tb) : 0, ns);
}

ppcp_result ppcp_instant_validate(const ppcp_instant *in)
{
    if (in == NULL || !ppcp_id_is_set(&in->tb))
        return PPCP_ERR_INVALID;
    return PPCP_OK;
}

ppcp_result ppcp_series_make(ppcp_series *out, const char *tb, size_t tb_len,
                             const int64_t *ns, size_t count)
{
    ppcp_result rc;
    if (out == NULL || (count > 0 && ns == NULL))
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    rc = ppcp_id_set(&out->tb, tb, tb_len);
    if (rc != PPCP_OK)
        return rc;
    if (count > PPCP_CBOR_MAX_ELEMENTS)
        return PPCP_ERR_LIMIT;
    out->ns    = ns;
    out->count = count;
    return PPCP_OK;
}

ppcp_result ppcp_series_validate(const ppcp_series *in)
{
    if (in == NULL || !ppcp_id_is_set(&in->tb))
        return PPCP_ERR_INVALID;
    if (in->count > 0 && in->ns == NULL)
        return PPCP_ERR_INVALID;
    return PPCP_OK;
}

ppcp_result ppcp_interval_make(ppcp_interval *out, const char *tb, size_t tb_len,
                               int64_t start_ns, int64_t end_ns)
{
    ppcp_result rc;
    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    rc = ppcp_id_set(&out->tb, tb, tb_len);
    if (rc != PPCP_OK)
        return rc;
    if (start_ns > end_ns)
        return PPCP_ERR_INVALID;    /* CORE 5.1: half-open [start, end) */
    out->start_ns = start_ns;
    out->end_ns   = end_ns;
    return PPCP_OK;
}

ppcp_result ppcp_interval_validate(const ppcp_interval *in)
{
    if (in == NULL || !ppcp_id_is_set(&in->tb))
        return PPCP_ERR_INVALID;
    if (in->start_ns > in->end_ns)
        return PPCP_ERR_INVALID;
    return PPCP_OK;
}

ppcp_result ppcp_estimate_make(ppcp_estimate *out, int64_t value_ns, double sigma_ns)
{
    if (out == NULL)
        return PPCP_ERR_INVALID;
    /* I29 / ENC 4.1e: both keys or neither.  There is no constructor that
     * takes a value alone, so "an applied estimate travelling without its
     * dispersion" has no representation. */
    if (!sigma_ok(sigma_ns))
        return PPCP_ERR_INVALID;
    out->value_ns = value_ns;
    out->sigma_ns = sigma_ns;
    return PPCP_OK;
}

ppcp_result ppcp_estimate_validate(const ppcp_estimate *in)
{
    if (in == NULL || !sigma_ok(in->sigma_ns))
        return PPCP_ERR_INVALID;
    return PPCP_OK;
}

/* --------------------------------------------------------------- Timebase */

ppcp_result ppcp_timebase_make(ppcp_timebase *out, const char *id, size_t id_len,
                               ppcp_timebase_kind kind, bool epoch_stable,
                               ppcp_duration_ns resolution_ns)
{
    ppcp_result rc;
    if (out == NULL)
        return PPCP_ERR_INVALID;
    if (kind != PPCP_TB_MONOTONIC && kind != PPCP_TB_CONTINUOUS && kind != PPCP_TB_WALL)
        return PPCP_ERR_INVALID;
    if (resolution_ns <= 0)
        return PPCP_ERR_INVALID;    /* a nominal tick of zero is not a tick */
    memset(out, 0, sizeof(*out));
    rc = ppcp_id_set(&out->id, id, id_len);
    if (rc != PPCP_OK)
        return rc;
    out->kind          = kind;
    out->epoch_stable  = epoch_stable;
    out->resolution_ns = resolution_ns;
    return PPCP_OK;
}

ppcp_result ppcp_timebase_set_origin(ppcp_timebase *tb, const char *origin, size_t len)
{
    ppcp_result rc;
    if (tb == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_id_set(&tb->origin, origin, len);
    if (rc != PPCP_OK)
        return rc;
    tb->has_origin = true;
    return PPCP_OK;
}

ppcp_result ppcp_timebase_validate(const ppcp_timebase *tb)
{
    if (tb == NULL || !ppcp_id_is_set(&tb->id))
        return PPCP_ERR_INVALID;
    if (tb->resolution_ns <= 0)
        return PPCP_ERR_INVALID;
    if (tb->has_origin && !ppcp_id_is_set(&tb->origin))
        return PPCP_ERR_INVALID;
    return PPCP_OK;
}

bool ppcp_timebase_is_wall(const ppcp_timebase *tb)
{
    return tb != NULL && tb->kind == PPCP_TB_WALL;
}

/* ------------------------------------------------------- TimebaseRelation */

static ppcp_result relation_common(ppcp_timebase_relation *out, const char *from,
                                   const char *to, ppcp_relation_method method,
                                   const ppcp_instant *observed_at)
{
    ppcp_result rc;

    if (out == NULL || observed_at == NULL)
        return PPCP_ERR_INVALID;
    if (method != PPCP_RELM_DECLARED && method != PPCP_RELM_MEASURED &&
        method != PPCP_RELM_ESTIMATED_ONLINE)
        return PPCP_ERR_INVALID;

    memset(out, 0, sizeof(*out));
    rc = ppcp_id_set(&out->from, from, from ? strlen(from) : 0);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_id_set(&out->to, to, to ? strlen(to) : 0);
    if (rc != PPCP_OK)
        return rc;
    /* I4: two Sources on the same clock share a timebase id, and identity is
     * never asserted by relation.  A relation from a timebase to itself is the
     * exact shape that assertion would take, so it is refused. */
    if (ppcp_id_equal(&out->from, &out->to))
        return PPCP_ERR_INVALID;

    rc = ppcp_instant_validate(observed_at);
    if (rc != PPCP_OK)
        return rc;
    /* CORE 5.4: `observed_at` is expressed in `from`. */
    if (!ppcp_id_equal(&observed_at->tb, &out->from))
        return PPCP_ERR_INVALID;

    out->method      = method;
    out->observed_at = *observed_at;
    return PPCP_OK;
}

ppcp_result ppcp_relation_make_affine(ppcp_timebase_relation *out, const char *from,
                                      const char *to, int64_t offset_ns, double skew_ppm,
                                      double offset_sigma_ns, double skew_sigma_ppm,
                                      ppcp_relation_method method,
                                      const ppcp_instant *observed_at)
{
    ppcp_result rc = relation_common(out, from, to, method, observed_at);
    if (rc != PPCP_OK)
        return rc;

    /* CORE 5.4a / I3.  Both sigmas are parameters of this function, so a
     * relation missing either one cannot be built.  That is the difference
     * between an invariant and a validation rule. */
    if (!finite_ok(skew_ppm) || !sigma_ok(offset_sigma_ns) || !sigma_ok(skew_sigma_ppm)) {
        memset(out, 0, sizeof(*out));
        return PPCP_ERR_INVALID;
    }

    out->cls             = PPCP_REL_AFFINE;
    out->offset_ns       = offset_ns;
    out->skew_ppm        = skew_ppm;
    out->offset_sigma_ns = offset_sigma_ns;
    out->skew_sigma_ppm  = skew_sigma_ppm;
    return PPCP_OK;
}

ppcp_result ppcp_relation_make_unrelated(ppcp_timebase_relation *out, const char *from,
                                         const char *to, ppcp_relation_method method,
                                         const ppcp_instant *observed_at)
{
    ppcp_result rc = relation_common(out, from, to, method, observed_at);
    if (rc != PPCP_OK)
        return rc;
    /* CORE 5.4b: `unrelated` carries none of the four affine fields, and is a
     * legal, complete declaration.  An honest Android UNKNOWN device declares
     * this and stays conformant; the alternative is a fabricated offset. */
    out->cls = PPCP_REL_UNRELATED;
    return PPCP_OK;
}

ppcp_result ppcp_relation_validate(const ppcp_timebase_relation *r)
{
    if (r == NULL || !ppcp_id_is_set(&r->from) || !ppcp_id_is_set(&r->to))
        return PPCP_ERR_INVALID;
    if (ppcp_id_equal(&r->from, &r->to))
        return PPCP_ERR_INVALID;                      /* I4 */
    if (ppcp_instant_validate(&r->observed_at) != PPCP_OK)
        return PPCP_ERR_INVALID;
    if (!ppcp_id_equal(&r->observed_at.tb, &r->from))
        return PPCP_ERR_INVALID;

    if (r->cls == PPCP_REL_AFFINE) {
        if (!finite_ok(r->skew_ppm) || !sigma_ok(r->offset_sigma_ns) ||
            !sigma_ok(r->skew_sigma_ppm))
            return PPCP_ERR_MALFORMED;                /* I3 */
        return PPCP_OK;
    }
    if (r->cls == PPCP_REL_UNRELATED) {
        if (r->offset_ns != 0 || r->skew_ppm != 0.0 ||
            r->offset_sigma_ns != 0.0 || r->skew_sigma_ppm != 0.0)
            return PPCP_ERR_MALFORMED;                /* CORE 5.4b */
        return PPCP_OK;
    }
    return PPCP_ERR_MALFORMED;
}

ppcp_result ppcp_relation_apply(const ppcp_timebase_relation *r, const ppcp_instant *t_from,
                                ppcp_instant *out_to)
{
    int64_t elapsed, drift, value;
    double  d;

    if (r == NULL || t_from == NULL || out_to == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_relation_validate(r) != PPCP_OK)
        return PPCP_ERR_INVALID;
    if (r->cls != PPCP_REL_AFFINE)
        return PPCP_ERR_INVALID;   /* `unrelated` means there is no mapping */
    if (!ppcp_id_equal(&t_from->tb, &r->from))
        return PPCP_ERR_INVALID;

    elapsed = t_from->ns - r->observed_at.ns;
    /* skew is parts per million of the elapsed interval since observation.
     * Rounded half away from zero so the mapping is symmetric about the
     * observation point rather than biased toward it. */
    d = (double)elapsed * r->skew_ppm * 1.0e-6;
    drift = (d >= 0.0) ? (int64_t)(d + 0.5) : -(int64_t)(-d + 0.5);
    value = t_from->ns + r->offset_ns + drift;

    return ppcp_instant_make(out_to, r->to.v, r->to.len, value);
}

/* ---------------------------------------------------- ClockDiscontinuity */

ppcp_result ppcp_clock_discontinuity_make(ppcp_clock_discontinuity *out,
                                          const char *timebase_id,
                                          const ppcp_instant *observed_at,
                                          int64_t magnitude_ns, const char *cause)
{
    ppcp_result rc;

    if (out == NULL || observed_at == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    rc = ppcp_id_set(&out->timebase_id, timebase_id, timebase_id ? strlen(timebase_id) : 0);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_instant_validate(observed_at);
    if (rc != PPCP_OK)
        return rc;
    /* CORE 5.5b: `observed_at` MUST NOT be in the timebase that stepped.  A
     * step observed on the stepped clock is not evidence of anything. */
    if (ppcp_id_equal(&observed_at->tb, &out->timebase_id))
        return PPCP_ERR_INVALID;
    rc = ppcp_id_set(&out->cause, cause, cause ? strlen(cause) : 0);
    if (rc != PPCP_OK)
        return rc;

    out->observed_at  = *observed_at;
    out->magnitude_ns = magnitude_ns;
    return PPCP_OK;
}

ppcp_result ppcp_clock_discontinuity_validate(const ppcp_clock_discontinuity *d)
{
    if (d == NULL || !ppcp_id_is_set(&d->timebase_id) || !ppcp_id_is_set(&d->cause))
        return PPCP_ERR_INVALID;
    if (ppcp_instant_validate(&d->observed_at) != PPCP_OK)
        return PPCP_ERR_INVALID;
    if (ppcp_id_equal(&d->observed_at.tb, &d->timebase_id))
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

/* ------------------------------------------------------------ encode/decode */

ppcp_result ppcp_instant_encode(ppcp_cbor_writer *w, const ppcp_instant *in)
{
    ppcp_result rc = ppcp_instant_validate(in);
    if (rc != PPCP_OK)
        return rc;      /* I1: an Instant with no tb is unwriteable */
    /* ENC 4.1: { "ns": int, "tb": tstr } — deterministic order puts "ns" first
     * (both are two characters; 0x6e < 0x74). */
    if (ppcp_cbor_write_map(w, 2) != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, "ns") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_int(w, in->ns) != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, "tb") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text(w, in->tb.v, in->tb.len) != PPCP_OK)
        return ppcp_cbor_writer_status(w);
    return PPCP_OK;
}

ppcp_result ppcp_instant_decode(ppcp_cbor_reader *r, ppcp_instant *out)
{
    ppcp_cbor_item it;
    ppcp_result    rc;
    uint32_t       i, pairs;
    bool           seen_tb = false, seen_ns = false;
    int64_t        ns = 0;
    ppcp_id        tb;

    if (r == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    memset(&tb, 0, sizeof(tb));

    rc = ppcp_cbor_read(r, &it);
    if (rc != PPCP_OK) return rc;
    if (it.type != PPCP_CBOR_MAP) return PPCP_ERR_MALFORMED;
    pairs = it.count;

    for (i = 0; i < pairs; i++) {
        const char *k; size_t klen;
        rc = ppcp_cbor_read_key(r, &k, &klen);
        if (rc != PPCP_OK) return rc;
        if (ppcp_cbor_key_is(k, klen, "tb")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_TEXT) return PPCP_ERR_MALFORMED;
            if (ppcp_id_set(&tb, (const char *)it.bytes, it.len) != PPCP_OK)
                return PPCP_ERR_MALFORMED;
            seen_tb = true;
        } else if (ppcp_cbor_key_is(k, klen, "ns")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_UINT && it.type != PPCP_CBOR_NINT)
                return PPCP_ERR_MALFORMED;
            ns = it.i;
            seen_ns = true;
        } else {
            rc = ppcp_cbor_skip(r);      /* I13 */
            if (rc != PPCP_OK) return rc;
        }
    }
    /* CT-I1's second half: a stream carrying an Instant with a missing or
     * empty `tb` decodes as malformed. */
    if (!seen_tb || !seen_ns)
        return PPCP_ERR_MALFORMED;
    out->tb = tb;
    out->ns = ns;
    return PPCP_OK;
}

ppcp_result ppcp_series_encode(ppcp_cbor_writer *w, const ppcp_series *in)
{
    ppcp_result rc = ppcp_series_validate(in);
    size_t      i;
    if (rc != PPCP_OK)
        return rc;
    /* Deterministic order: "ns" before "tb". */
    if (ppcp_cbor_write_map(w, 2) != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, "ns") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_array(w, in->count) != PPCP_OK) return ppcp_cbor_writer_status(w);
    for (i = 0; i < in->count; i++)
        if (ppcp_cbor_write_int(w, in->ns[i]) != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, "tb") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text(w, in->tb.v, in->tb.len) != PPCP_OK)
        return ppcp_cbor_writer_status(w);
    return PPCP_OK;
}

ppcp_result ppcp_series_decode(ppcp_cbor_reader *r, ppcp_id *out_tb, int64_t *out_ns,
                               size_t cap, size_t *out_count)
{
    ppcp_cbor_item it;
    ppcp_result    rc;
    uint32_t       i, pairs;
    bool           seen_tb = false, seen_ns = false;
    size_t         n = 0;

    if (r == NULL || out_tb == NULL || out_count == NULL)
        return PPCP_ERR_INVALID;
    memset(out_tb, 0, sizeof(*out_tb));

    rc = ppcp_cbor_read(r, &it);
    if (rc != PPCP_OK) return rc;
    if (it.type != PPCP_CBOR_MAP) return PPCP_ERR_MALFORMED;
    pairs = it.count;

    for (i = 0; i < pairs; i++) {
        const char *k; size_t klen;
        rc = ppcp_cbor_read_key(r, &k, &klen);
        if (rc != PPCP_OK) return rc;
        if (ppcp_cbor_key_is(k, klen, "tb")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_TEXT) return PPCP_ERR_MALFORMED;
            if (ppcp_id_set(out_tb, (const char *)it.bytes, it.len) != PPCP_OK)
                return PPCP_ERR_MALFORMED;
            seen_tb = true;
        } else if (ppcp_cbor_key_is(k, klen, "ns")) {
            uint32_t j;
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            /* I2 / CORE 5.8e: `frames.ns` has no scalar form.  A nominal rate
             * is not a substitute for measured timestamps, so an integer here
             * is malformed rather than "constant". */
            if (it.type != PPCP_CBOR_ARRAY) return PPCP_ERR_MALFORMED;
            if (it.count > cap) return PPCP_ERR_NOSPACE;
            for (j = 0; j < it.count; j++) {
                ppcp_cbor_item e;
                rc = ppcp_cbor_read(r, &e);
                if (rc != PPCP_OK) return rc;
                if (e.type != PPCP_CBOR_UINT && e.type != PPCP_CBOR_NINT)
                    return PPCP_ERR_MALFORMED;
                out_ns[j] = e.i;
            }
            n = it.count;
            seen_ns = true;
        } else {
            rc = ppcp_cbor_skip(r);
            if (rc != PPCP_OK) return rc;
        }
    }
    if (!seen_tb || !seen_ns)
        return PPCP_ERR_MALFORMED;
    *out_count = n;
    return PPCP_OK;
}

ppcp_result ppcp_interval_encode(ppcp_cbor_writer *w, const ppcp_interval *in)
{
    ppcp_result rc = ppcp_interval_validate(in);
    if (rc != PPCP_OK)
        return rc;
    /* Deterministic order: "tb"(2) then "end_ns"(6) then "start_ns"(8). */
    if (ppcp_cbor_write_map(w, 3) != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, "tb") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text(w, in->tb.v, in->tb.len) != PPCP_OK)
        return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, "end_ns") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_int(w, in->end_ns) != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, "start_ns") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_int(w, in->start_ns) != PPCP_OK) return ppcp_cbor_writer_status(w);
    return PPCP_OK;
}

ppcp_result ppcp_interval_decode(ppcp_cbor_reader *r, ppcp_interval *out)
{
    ppcp_cbor_item it;
    ppcp_result    rc;
    uint32_t       i, pairs;
    bool           seen_tb = false, seen_s = false, seen_e = false;
    ppcp_interval  v;

    if (r == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    memset(&v, 0, sizeof(v));

    rc = ppcp_cbor_read(r, &it);
    if (rc != PPCP_OK) return rc;
    if (it.type != PPCP_CBOR_MAP) return PPCP_ERR_MALFORMED;
    pairs = it.count;

    for (i = 0; i < pairs; i++) {
        const char *k; size_t klen;
        rc = ppcp_cbor_read_key(r, &k, &klen);
        if (rc != PPCP_OK) return rc;
        if (ppcp_cbor_key_is(k, klen, "tb")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_TEXT) return PPCP_ERR_MALFORMED;
            if (ppcp_id_set(&v.tb, (const char *)it.bytes, it.len) != PPCP_OK)
                return PPCP_ERR_MALFORMED;
            seen_tb = true;
        } else if (ppcp_cbor_key_is(k, klen, "start_ns")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_UINT && it.type != PPCP_CBOR_NINT)
                return PPCP_ERR_MALFORMED;
            v.start_ns = it.i; seen_s = true;
        } else if (ppcp_cbor_key_is(k, klen, "end_ns")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_UINT && it.type != PPCP_CBOR_NINT)
                return PPCP_ERR_MALFORMED;
            v.end_ns = it.i; seen_e = true;
        } else {
            rc = ppcp_cbor_skip(r);
            if (rc != PPCP_OK) return rc;
        }
    }
    if (!seen_tb || !seen_s || !seen_e)
        return PPCP_ERR_MALFORMED;
    if (v.start_ns > v.end_ns)
        return PPCP_ERR_MALFORMED;
    *out = v;
    return PPCP_OK;
}

ppcp_result ppcp_estimate_encode(ppcp_cbor_writer *w, const ppcp_estimate *in)
{
    ppcp_result rc = ppcp_estimate_validate(in);
    if (rc != PPCP_OK)
        return rc;
    /* ENC 4.1e: both keys.  Deterministic order: "sigma_ns"(8) then
     * "value_ns"(8) — equal length, so bytewise: 's' < 'v'. */
    if (ppcp_cbor_write_map(w, 2) != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, "sigma_ns") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_double(w, in->sigma_ns) != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, "value_ns") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_int(w, in->value_ns) != PPCP_OK) return ppcp_cbor_writer_status(w);
    return PPCP_OK;
}

ppcp_result ppcp_estimate_decode(ppcp_cbor_reader *r, ppcp_estimate *out)
{
    ppcp_cbor_item it;
    ppcp_result    rc;
    uint32_t       i, pairs;
    bool           seen_v = false, seen_s = false;
    ppcp_estimate  e;

    if (r == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    memset(&e, 0, sizeof(e));

    rc = ppcp_cbor_read(r, &it);
    if (rc != PPCP_OK) return rc;
    if (it.type != PPCP_CBOR_MAP) return PPCP_ERR_MALFORMED;
    pairs = it.count;

    for (i = 0; i < pairs; i++) {
        const char *k; size_t klen;
        rc = ppcp_cbor_read_key(r, &k, &klen);
        if (rc != PPCP_OK) return rc;
        if (ppcp_cbor_key_is(k, klen, "value_ns")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_UINT && it.type != PPCP_CBOR_NINT)
                return PPCP_ERR_MALFORMED;
            e.value_ns = it.i; seen_v = true;
        } else if (ppcp_cbor_key_is(k, klen, "sigma_ns")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            /* ENC §4 requires a decoder to accept half and single floats, and
             * an integer sigma is a plausible encoder slip that is not
             * permitted: Sigma is a float type. */
            if (it.type != PPCP_CBOR_DOUBLE) return PPCP_ERR_MALFORMED;
            e.sigma_ns = it.f; seen_s = true;
        } else {
            rc = ppcp_cbor_skip(r);
            if (rc != PPCP_OK) return rc;
        }
    }
    /* I29 / ENC 4.1e: one key without the other is malformed on receipt. */
    if (seen_v != seen_s)
        return PPCP_ERR_MALFORMED;
    if (!seen_v)
        return PPCP_ERR_MALFORMED;
    if (!sigma_ok(e.sigma_ns))
        return PPCP_ERR_MALFORMED;
    *out = e;
    return PPCP_OK;
}

/* --------------------------------------------------- Timebase encode/decode */

static const char *tb_kind_str(ppcp_timebase_kind k)
{
    switch (k) {
    case PPCP_TB_MONOTONIC:  return "monotonic";
    case PPCP_TB_CONTINUOUS: return "continuous";
    case PPCP_TB_WALL:       return "wall";
    }
    return "monotonic";
}

ppcp_result ppcp_timebase_encode(ppcp_cbor_writer *w, const ppcp_timebase *in)
{
    size_t      n;
    ppcp_result rc = ppcp_timebase_validate(in);
    if (rc != PPCP_OK)
        return rc;

    n = 4 + (in->has_origin ? 1u : 0u);
    /* Deterministic order: id(2), kind(4), origin(6), resolution_ns(13),
     * epoch_stable(12) — sorted by encoded length then bytes:
     * "id", "kind", "origin", "epoch_stable", "resolution_ns". */
    if (ppcp_cbor_write_map(w, n) != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, "id") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text(w, in->id.v, in->id.len) != PPCP_OK)
        return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, "kind") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, tb_kind_str(in->kind)) != PPCP_OK)
        return ppcp_cbor_writer_status(w);
    if (in->has_origin) {
        if (ppcp_cbor_write_text_z(w, "origin") != PPCP_OK) return ppcp_cbor_writer_status(w);
        if (ppcp_cbor_write_text(w, in->origin.v, in->origin.len) != PPCP_OK)
            return ppcp_cbor_writer_status(w);
    }
    if (ppcp_cbor_write_text_z(w, "epoch_stable") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_bool(w, in->epoch_stable) != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, "resolution_ns") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_int(w, in->resolution_ns) != PPCP_OK) return ppcp_cbor_writer_status(w);
    return PPCP_OK;
}

ppcp_result ppcp_timebase_decode(ppcp_cbor_reader *r, ppcp_timebase *out)
{
    ppcp_cbor_item it;
    ppcp_result    rc;
    uint32_t       i, pairs;
    bool           seen_id = false, seen_kind = false, seen_es = false, seen_res = false;
    ppcp_timebase  tb;

    if (r == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    memset(&tb, 0, sizeof(tb));

    rc = ppcp_cbor_read(r, &it);
    if (rc != PPCP_OK) return rc;
    if (it.type != PPCP_CBOR_MAP) return PPCP_ERR_MALFORMED;
    pairs = it.count;

    for (i = 0; i < pairs; i++) {
        const char *k; size_t klen;
        rc = ppcp_cbor_read_key(r, &k, &klen);
        if (rc != PPCP_OK) return rc;
        if (ppcp_cbor_key_is(k, klen, "id")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_TEXT) return PPCP_ERR_MALFORMED;
            if (ppcp_id_set(&tb.id, (const char *)it.bytes, it.len) != PPCP_OK)
                return PPCP_ERR_MALFORMED;
            seen_id = true;
        } else if (ppcp_cbor_key_is(k, klen, "kind")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_TEXT) return PPCP_ERR_MALFORMED;
            if (ppcp_cbor_key_is((const char *)it.bytes, it.len, "monotonic"))
                tb.kind = PPCP_TB_MONOTONIC;
            else if (ppcp_cbor_key_is((const char *)it.bytes, it.len, "continuous"))
                tb.kind = PPCP_TB_CONTINUOUS;
            else if (ppcp_cbor_key_is((const char *)it.bytes, it.len, "wall"))
                tb.kind = PPCP_TB_WALL;
            else
                return PPCP_ERR_MALFORMED;   /* CORE 5.3: kind is a closed set */
            seen_kind = true;
        } else if (ppcp_cbor_key_is(k, klen, "epoch_stable")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_BOOL) return PPCP_ERR_MALFORMED;
            tb.epoch_stable = it.b; seen_es = true;
        } else if (ppcp_cbor_key_is(k, klen, "resolution_ns")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_UINT && it.type != PPCP_CBOR_NINT)
                return PPCP_ERR_MALFORMED;
            tb.resolution_ns = it.i; seen_res = true;
        } else if (ppcp_cbor_key_is(k, klen, "origin")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_TEXT) return PPCP_ERR_MALFORMED;
            if (ppcp_id_set(&tb.origin, (const char *)it.bytes, it.len) != PPCP_OK)
                return PPCP_ERR_MALFORMED;
            tb.has_origin = true;
        } else {
            rc = ppcp_cbor_skip(r);
            if (rc != PPCP_OK) return rc;
        }
    }
    if (!seen_id || !seen_kind || !seen_es || !seen_res)
        return PPCP_ERR_MALFORMED;
    if (tb.resolution_ns <= 0)
        return PPCP_ERR_MALFORMED;
    *out = tb;
    return PPCP_OK;
}

/* --------------------------------------------------- Relation encode/decode */

static const char *rel_method_str(ppcp_relation_method m)
{
    switch (m) {
    case PPCP_RELM_DECLARED:          return "declared";
    case PPCP_RELM_MEASURED:          return "measured";
    case PPCP_RELM_ESTIMATED_ONLINE:  return "estimated_online";
    }
    return "declared";
}

ppcp_result ppcp_relation_encode(ppcp_cbor_writer *w, const ppcp_timebase_relation *in)
{
    size_t      n;
    bool        affine;
    ppcp_result rc = ppcp_relation_validate(in);
    if (rc != PPCP_OK)
        return rc;

    affine = (in->cls == PPCP_REL_AFFINE);
    /* to(2) from(4) class(5) method(6) offset_ns(9) skew_ppm(8)
     * observed_at(11) skew_sigma_ppm(14) offset_sigma_ns(15)
     * evidence_stream_id(18) — written in RFC 8949 §4.2.1 order below. */
    n = 5u + (affine ? 4u : 0u) + (in->has_evidence_stream_id ? 1u : 0u);

    if (ppcp_cbor_write_map(w, n) != PPCP_OK) return ppcp_cbor_writer_status(w);

    if (ppcp_cbor_write_text_z(w, "to") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text(w, in->to.v, in->to.len) != PPCP_OK)
        return ppcp_cbor_writer_status(w);

    if (ppcp_cbor_write_text_z(w, "from") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text(w, in->from.v, in->from.len) != PPCP_OK)
        return ppcp_cbor_writer_status(w);

    if (ppcp_cbor_write_text_z(w, "class") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, affine ? "affine" : "unrelated") != PPCP_OK)
        return ppcp_cbor_writer_status(w);

    if (ppcp_cbor_write_text_z(w, "method") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, rel_method_str(in->method)) != PPCP_OK)
        return ppcp_cbor_writer_status(w);

    if (affine) {
        if (ppcp_cbor_write_text_z(w, "skew_ppm") != PPCP_OK) return ppcp_cbor_writer_status(w);
        if (ppcp_cbor_write_double(w, in->skew_ppm) != PPCP_OK) return ppcp_cbor_writer_status(w);
        if (ppcp_cbor_write_text_z(w, "offset_ns") != PPCP_OK) return ppcp_cbor_writer_status(w);
        if (ppcp_cbor_write_int(w, in->offset_ns) != PPCP_OK) return ppcp_cbor_writer_status(w);
    }

    if (ppcp_cbor_write_text_z(w, "observed_at") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_instant_encode(w, &in->observed_at) != PPCP_OK) return ppcp_cbor_writer_status(w);

    if (affine) {
        if (ppcp_cbor_write_text_z(w, "skew_sigma_ppm") != PPCP_OK)
            return ppcp_cbor_writer_status(w);
        if (ppcp_cbor_write_double(w, in->skew_sigma_ppm) != PPCP_OK)
            return ppcp_cbor_writer_status(w);
        if (ppcp_cbor_write_text_z(w, "offset_sigma_ns") != PPCP_OK)
            return ppcp_cbor_writer_status(w);
        if (ppcp_cbor_write_double(w, in->offset_sigma_ns) != PPCP_OK)
            return ppcp_cbor_writer_status(w);
    }

    if (in->has_evidence_stream_id) {
        if (ppcp_cbor_write_text_z(w, "evidence_stream_id") != PPCP_OK)
            return ppcp_cbor_writer_status(w);
        if (ppcp_cbor_write_text(w, in->evidence_stream_id.v, in->evidence_stream_id.len) != PPCP_OK)
            return ppcp_cbor_writer_status(w);
    }
    return PPCP_OK;
}

ppcp_result ppcp_relation_decode(ppcp_cbor_reader *r, ppcp_timebase_relation *out)
{
    ppcp_cbor_item         it;
    ppcp_result            rc;
    uint32_t               i, pairs;
    ppcp_timebase_relation v;
    bool seen_from = false, seen_to = false, seen_class = false, seen_method = false;
    bool seen_obs = false, seen_off = false, seen_skew = false;
    bool seen_offsig = false, seen_skewsig = false;

    if (r == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    memset(&v, 0, sizeof(v));

    rc = ppcp_cbor_read(r, &it);
    if (rc != PPCP_OK) return rc;
    if (it.type != PPCP_CBOR_MAP) return PPCP_ERR_MALFORMED;
    pairs = it.count;

    for (i = 0; i < pairs; i++) {
        const char *k; size_t klen;
        rc = ppcp_cbor_read_key(r, &k, &klen);
        if (rc != PPCP_OK) return rc;

        if (ppcp_cbor_key_is(k, klen, "from") || ppcp_cbor_key_is(k, klen, "to") ||
            ppcp_cbor_key_is(k, klen, "evidence_stream_id")) {
            ppcp_id *dst = ppcp_cbor_key_is(k, klen, "from") ? &v.from
                         : ppcp_cbor_key_is(k, klen, "to")   ? &v.to
                                                             : &v.evidence_stream_id;
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_TEXT) return PPCP_ERR_MALFORMED;
            if (ppcp_id_set(dst, (const char *)it.bytes, it.len) != PPCP_OK)
                return PPCP_ERR_MALFORMED;
            if (dst == &v.from) seen_from = true;
            else if (dst == &v.to) seen_to = true;
            else v.has_evidence_stream_id = true;
        } else if (ppcp_cbor_key_is(k, klen, "class")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_TEXT) return PPCP_ERR_MALFORMED;
            if (ppcp_cbor_key_is((const char *)it.bytes, it.len, "affine"))
                v.cls = PPCP_REL_AFFINE;
            else if (ppcp_cbor_key_is((const char *)it.bytes, it.len, "unrelated"))
                v.cls = PPCP_REL_UNRELATED;
            else
                return PPCP_ERR_MALFORMED;   /* I3: affine or unrelated, nothing else */
            seen_class = true;
        } else if (ppcp_cbor_key_is(k, klen, "method")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_TEXT) return PPCP_ERR_MALFORMED;
            if (ppcp_cbor_key_is((const char *)it.bytes, it.len, "declared"))
                v.method = PPCP_RELM_DECLARED;
            else if (ppcp_cbor_key_is((const char *)it.bytes, it.len, "measured"))
                v.method = PPCP_RELM_MEASURED;
            else if (ppcp_cbor_key_is((const char *)it.bytes, it.len, "estimated_online"))
                v.method = PPCP_RELM_ESTIMATED_ONLINE;
            else
                return PPCP_ERR_MALFORMED;
            seen_method = true;
        } else if (ppcp_cbor_key_is(k, klen, "observed_at")) {
            rc = ppcp_instant_decode(r, &v.observed_at);
            if (rc != PPCP_OK) return rc;
            seen_obs = true;
        } else if (ppcp_cbor_key_is(k, klen, "offset_ns")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_UINT && it.type != PPCP_CBOR_NINT)
                return PPCP_ERR_MALFORMED;
            v.offset_ns = it.i; seen_off = true;
        } else if (ppcp_cbor_key_is(k, klen, "skew_ppm")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_DOUBLE) return PPCP_ERR_MALFORMED;
            v.skew_ppm = it.f; seen_skew = true;
        } else if (ppcp_cbor_key_is(k, klen, "offset_sigma_ns")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_DOUBLE) return PPCP_ERR_MALFORMED;
            v.offset_sigma_ns = it.f; seen_offsig = true;
        } else if (ppcp_cbor_key_is(k, klen, "skew_sigma_ppm")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_DOUBLE) return PPCP_ERR_MALFORMED;
            v.skew_sigma_ppm = it.f; seen_skewsig = true;
        } else {
            rc = ppcp_cbor_skip(r);
            if (rc != PPCP_OK) return rc;
        }
    }

    if (!seen_from || !seen_to || !seen_class || !seen_method || !seen_obs)
        return PPCP_ERR_MALFORMED;

    if (v.cls == PPCP_REL_AFFINE) {
        /* CORE 5.4a / I3 — this is the receiving half of the invariant.  A
         * relation missing either sigma is malformed and MUST be rejected;
         * accepting it and treating the sigma as zero is the failure mode. */
        if (!seen_off || !seen_skew || !seen_offsig || !seen_skewsig)
            return PPCP_ERR_MALFORMED;
    } else {
        /* CORE 5.4b: `unrelated` carries none of them. */
        if (seen_off || seen_skew || seen_offsig || seen_skewsig)
            return PPCP_ERR_MALFORMED;
    }

    rc = ppcp_relation_validate(&v);
    if (rc != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    *out = v;
    return PPCP_OK;
}

/* ------------------------------------------ ClockDiscontinuity encode/decode */

ppcp_result ppcp_clock_discontinuity_encode(ppcp_cbor_writer *w,
                                            const ppcp_clock_discontinuity *in)
{
    ppcp_result rc = ppcp_clock_discontinuity_validate(in);
    if (rc != PPCP_OK)
        return rc;
    /* cause(5) magnitude_ns(12) observed_at(11) timebase_id(11):
     * "cause", "observed_at", "timebase_id", "magnitude_ns". */
    if (ppcp_cbor_write_map(w, 4) != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, "cause") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text(w, in->cause.v, in->cause.len) != PPCP_OK)
        return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, "observed_at") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_instant_encode(w, &in->observed_at) != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, "timebase_id") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text(w, in->timebase_id.v, in->timebase_id.len) != PPCP_OK)
        return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, "magnitude_ns") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_int(w, in->magnitude_ns) != PPCP_OK) return ppcp_cbor_writer_status(w);
    return PPCP_OK;
}

ppcp_result ppcp_clock_discontinuity_decode(ppcp_cbor_reader *r,
                                            ppcp_clock_discontinuity *out)
{
    ppcp_cbor_item           it;
    ppcp_result              rc;
    uint32_t                 i, pairs;
    ppcp_clock_discontinuity d;
    bool seen_tb = false, seen_at = false, seen_mag = false, seen_cause = false;

    if (r == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    memset(&d, 0, sizeof(d));

    rc = ppcp_cbor_read(r, &it);
    if (rc != PPCP_OK) return rc;
    if (it.type != PPCP_CBOR_MAP) return PPCP_ERR_MALFORMED;
    pairs = it.count;

    for (i = 0; i < pairs; i++) {
        const char *k; size_t klen;
        rc = ppcp_cbor_read_key(r, &k, &klen);
        if (rc != PPCP_OK) return rc;
        if (ppcp_cbor_key_is(k, klen, "timebase_id")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_TEXT) return PPCP_ERR_MALFORMED;
            if (ppcp_id_set(&d.timebase_id, (const char *)it.bytes, it.len) != PPCP_OK)
                return PPCP_ERR_MALFORMED;
            seen_tb = true;
        } else if (ppcp_cbor_key_is(k, klen, "cause")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_TEXT) return PPCP_ERR_MALFORMED;
            /* CORE 10.3: `cause` is an open registry.  An unknown value is
             * carried through, never rejected (I13, 10.3a). */
            if (ppcp_id_set(&d.cause, (const char *)it.bytes, it.len) != PPCP_OK)
                return PPCP_ERR_MALFORMED;
            seen_cause = true;
        } else if (ppcp_cbor_key_is(k, klen, "observed_at")) {
            rc = ppcp_instant_decode(r, &d.observed_at);
            if (rc != PPCP_OK) return rc;
            seen_at = true;
        } else if (ppcp_cbor_key_is(k, klen, "magnitude_ns")) {
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_UINT && it.type != PPCP_CBOR_NINT)
                return PPCP_ERR_MALFORMED;
            d.magnitude_ns = it.i; seen_mag = true;
        } else {
            rc = ppcp_cbor_skip(r);
            if (rc != PPCP_OK) return rc;
        }
    }
    if (!seen_tb || !seen_at || !seen_mag || !seen_cause)
        return PPCP_ERR_MALFORMED;
    if (ppcp_clock_discontinuity_validate(&d) != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    *out = d;
    return PPCP_OK;
}

/* ------------------------------------------------------------------- clock */

ppcp_result ppcp_clock_read(const ppcp_clock *c, const char *timebase_id, ppcp_instant *out)
{
    int64_t     ns = 0;
    ppcp_result rc;

    if (c == NULL || c->now == NULL || timebase_id == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    rc = c->now(c->ctx, timebase_id, &ns);
    if (rc != PPCP_OK)
        return rc;
    /* I1 again: the only thing this function can hand back is an Instant. */
    return ppcp_instant_make_z(out, timebase_id, ns);
}
