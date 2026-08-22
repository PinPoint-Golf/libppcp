/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * PPCP-CORE §5.8 — the three capabilities (claimed, measured, achieved) and
 * the Digest of §5.1.  Work package L4.
 *
 * The interesting clause here is ENC 4.1d, the `intrinsics` first-element rule:
 * every other per-frame field distinguishes its scalar and parallel forms by
 * CBOR major type, and `intrinsics` cannot, because its element type is itself
 * an array.  A decoder applying the major-type rule literally reads one
 * constant matrix as a nine-frame series — silently, and with plausible
 * numbers.
 */
#include "ppcp/model.h"
#include "ppcp_codec.h"

#include <string.h>

/* ---------------------------------------------------------------- Digest */

ppcp_result ppcp_digest_set(ppcp_digest *d, const uint8_t v[PPCP_SHA256_BYTES])
{
    if (d == NULL || v == NULL)
        return PPCP_ERR_INVALID;
    memcpy(d->value, v, PPCP_SHA256_BYTES);
    d->present = true;
    return PPCP_OK;
}

bool ppcp_digest_equal(const ppcp_digest *a, const ppcp_digest *b)
{
    if (a == NULL || b == NULL)
        return false;
    if (!a->present || !b->present)
        return false;
    return ppcp_ct_equal(a->value, b->value, PPCP_SHA256_BYTES);
}

ppcp_result ppcp_digest_encode(ppcp_cbor_writer *w, const ppcp_digest *d)
{
    ppcp_wfield f[2];
    if (d == NULL || !d->present)
        return PPCP_ERR_INVALID;
    f[0] = ppcp_wf_text("alg", "sha-256", 7);
    f[1] = ppcp_wf_bytes("value", d->value, PPCP_SHA256_BYTES);
    return ppcp_rec_write(w, f, 2);
}

ppcp_result ppcp_digest_decode(ppcp_cbor_reader *r, ppcp_digest *out)
{
    ppcp_rfield    f[2];
    ppcp_text_ref  alg;
    ppcp_bytes_ref val;
    bool           seen_alg = false, seen_val = false;
    ppcp_result    rc;

    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    memset(&alg, 0, sizeof(alg));
    memset(&val, 0, sizeof(val));

    f[0] = ppcp_rf("alg", PPCP_F_TEXT, &alg, &seen_alg);
    f[1] = ppcp_rf("value", PPCP_F_BYTES, &val, &seen_val);
    rc = ppcp_rec_read(r, f, 2);
    if (rc != PPCP_OK)
        return rc;
    if (!seen_alg || !seen_val)
        return PPCP_ERR_MALFORMED;
    /* One algorithm in ppcp/1.0.  A second would be a MINOR change that adds a
     * value here, and a decoder meeting one it cannot compute must say so
     * rather than accept a digest it will never be able to check. */
    if (!ppcp_cbor_key_is(alg.p, alg.len, "sha-256"))
        return PPCP_ERR_MALFORMED;
    if (val.len != PPCP_SHA256_BYTES)
        return PPCP_ERR_MALFORMED;
    memcpy(out->value, val.p, PPCP_SHA256_BYTES);
    out->present = true;
    return PPCP_OK;
}

/* --------------------------------------------------------- ThermalLevel */

static const ppcp_enum_map thermal_map[] = {
    { "nominal",  PPCP_THERMAL_NOMINAL  },
    { "elevated", PPCP_THERMAL_ELEVATED },
    { "serious",  PPCP_THERMAL_SERIOUS  },
    { "critical", PPCP_THERMAL_CRITICAL },
    { NULL, 0 }
};

const char *ppcp_thermal_level_str(ppcp_thermal_level l)
{
    const char *s = ppcp_enum_to_text(thermal_map, (int)l);
    return s ? s : "nominal";
}

const ppcp_enum_map *ppcp_thermal_enum_map(void) { return thermal_map; }

/* ------------------------------------------------------------- range3 */

ppcp_result ppcp_range3_set(ppcp_range3 *r, int64_t min, int64_t max, int64_t median)
{
    if (r == NULL)
        return PPCP_ERR_INVALID;
    if (min > max || median < min || median > max)
        return PPCP_ERR_INVALID;
    r->present = true;
    r->min     = min;
    r->max     = max;
    r->median  = median;
    return PPCP_OK;
}

static ppcp_result range3_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_range3 *r = (const ppcp_range3 *)ctx;
    ppcp_wfield f[3];
    f[0] = ppcp_wf_int("min", r->min);
    f[1] = ppcp_wf_int("max", r->max);
    f[2] = ppcp_wf_int("median", r->median);
    return ppcp_rec_write(w, f, 3);
}

static ppcp_result range3_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_range3 *out = (ppcp_range3 *)dst;
    ppcp_rfield  f[3];
    bool         a = false, b = false, c = false;
    ppcp_result  rc;
    (void)ctx;
    f[0] = ppcp_rf("min", PPCP_F_INT, &out->min, &a);
    f[1] = ppcp_rf("max", PPCP_F_INT, &out->max, &b);
    f[2] = ppcp_rf("median", PPCP_F_INT, &out->median, &c);
    rc = ppcp_rec_read(r, f, 3);
    if (rc != PPCP_OK)
        return rc;
    if (!a || !b || !c)
        return PPCP_ERR_MALFORMED;
    if (out->min > out->max || out->median < out->min || out->median > out->max)
        return PPCP_ERR_MALFORMED;
    out->present = true;
    return PPCP_OK;
}

/* ------------------------------------------------- MeasuredCapability */

static const ppcp_enum_map measure_method_map[] = {
    { "cold_sample", PPCP_MEAS_COLD_SAMPLE },
    { "sustained",   PPCP_MEAS_SUSTAINED   },
    { NULL, 0 }
};

ppcp_result ppcp_measured_capability_make(ppcp_measured_capability *out,
                                          ppcp_measure_method method,
                                          ppcp_duration_ns duration_ns,
                                          int64_t sustained_rate_mhz,
                                          int64_t dropped_frames,
                                          const ppcp_instant *observed_at)
{
    if (out == NULL || observed_at == NULL)
        return PPCP_ERR_INVALID;
    if (duration_ns <= 0 || sustained_rate_mhz < 0 || dropped_frames < 0)
        return PPCP_ERR_INVALID;
    if (ppcp_instant_validate(observed_at) != PPCP_OK)
        return PPCP_ERR_INVALID;
    if (method != PPCP_MEAS_COLD_SAMPLE && method != PPCP_MEAS_SUSTAINED)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    /* 5.8a / I28: `method` and `duration_ns` are parameters and not setters,
     * so a MeasuredCapability cannot come into existence without saying what
     * kind of measurement it was and how long it ran.  There is no overload
     * that takes a CaptureProfile. */
    out->method             = method;
    out->duration_ns        = duration_ns;
    out->sustained_rate_mhz = sustained_rate_mhz;
    out->dropped_frames     = dropped_frames;
    out->observed_at        = *observed_at;
    return PPCP_OK;
}

ppcp_result ppcp_measured_capability_validate(const ppcp_measured_capability *m)
{
    if (m == NULL)
        return PPCP_ERR_INVALID;
    if (m->duration_ns <= 0)
        return PPCP_ERR_INVALID;
    if (m->sustained_rate_mhz < 0 || m->dropped_frames < 0)
        return PPCP_ERR_INVALID;
    if (ppcp_enum_to_text(measure_method_map, (int)m->method) == NULL)
        return PPCP_ERR_INVALID;
    return ppcp_instant_validate(&m->observed_at);
}

ppcp_result ppcp_measured_capability_encode(ppcp_cbor_writer *w,
                                            const ppcp_measured_capability *m)
{
    ppcp_wfield f[9];
    size_t      n  = 0;
    ppcp_result rc = ppcp_measured_capability_validate(m);
    if (rc != PPCP_OK)
        return rc;

    f[n++] = ppcp_wf_enum("method", measure_method_map, (int)m->method);
    f[n++] = ppcp_wf_int("duration_ns", m->duration_ns);
    f[n++] = ppcp_wf_int("sustained_rate_mhz", m->sustained_rate_mhz);
    f[n++] = ppcp_wf_int("dropped_frames", m->dropped_frames);
    f[n++] = ppcp_wf_sub("observed_at", ppcp_sub_write_instant, &m->observed_at);
    if (m->achieved_exposure_ns.present)
        f[n++] = ppcp_wf_sub("achieved_exposure_ns", range3_write, &m->achieved_exposure_ns);
    if (m->achieved_iso.present)
        f[n++] = ppcp_wf_sub("achieved_iso", range3_write, &m->achieved_iso);
    if (m->has_noise_figure)
        f[n++] = ppcp_wf_double("noise_figure", m->noise_figure);
    if (m->has_thermal_at_end)
        f[n++] = ppcp_wf_enum("thermal_at_end", thermal_map, (int)m->thermal_at_end);
    return ppcp_rec_write(w, f, n);
}

ppcp_result ppcp_measured_capability_decode(ppcp_cbor_reader *r, ppcp_measured_capability *out)
{
    ppcp_rfield f[9];
    size_t      n = 0;
    bool        s_method = false, s_dur = false, s_rate = false, s_drop = false, s_obs = false;
    int         method = 0, thermal = 0;
    ppcp_result rc;

    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    f[n++] = ppcp_rf_enum("method", measure_method_map, &method, &s_method);
    f[n++] = ppcp_rf("duration_ns", PPCP_F_INT, &out->duration_ns, &s_dur);
    f[n++] = ppcp_rf("sustained_rate_mhz", PPCP_F_INT, &out->sustained_rate_mhz, &s_rate);
    f[n++] = ppcp_rf("dropped_frames", PPCP_F_INT, &out->dropped_frames, &s_drop);
    f[n++] = ppcp_rf_sub("observed_at", ppcp_sub_read_instant, &out->observed_at, NULL, &s_obs);
    f[n++] = ppcp_rf_sub("achieved_exposure_ns", range3_read, &out->achieved_exposure_ns,
                         NULL, NULL);
    f[n++] = ppcp_rf_sub("achieved_iso", range3_read, &out->achieved_iso, NULL, NULL);
    f[n++] = ppcp_rf("noise_figure", PPCP_F_DOUBLE, &out->noise_figure, &out->has_noise_figure);
    f[n++] = ppcp_rf_enum("thermal_at_end", thermal_map, &thermal, &out->has_thermal_at_end);

    rc = ppcp_rec_read(r, f, n);
    if (rc != PPCP_OK)
        return rc;
    if (!s_method || !s_dur || !s_rate || !s_drop || !s_obs)
        return PPCP_ERR_MALFORMED;   /* 5.8a: method and duration are mandatory */
    out->method         = (ppcp_measure_method)method;
    out->thermal_at_end = (ppcp_thermal_level)thermal;
    if (ppcp_measured_capability_validate(out) != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

/* ---------------------------------------------------- AchievedSummary */

static ppcp_result thermal_point_write(ppcp_cbor_writer *w, const void *elem)
{
    const ppcp_thermal_point *p = (const ppcp_thermal_point *)elem;
    ppcp_wfield f[2];
    f[0] = ppcp_wf_sub("at", ppcp_sub_write_instant, &p->at);
    f[1] = ppcp_wf_enum("level", thermal_map, (int)p->level);
    return ppcp_rec_write(w, f, 2);
}

static ppcp_result thermal_point_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_thermal_point *p = (ppcp_thermal_point *)dst;
    ppcp_rfield f[2];
    bool        s_at = false, s_lvl = false;
    int         level = 0;
    ppcp_result rc;
    (void)ctx;
    f[0] = ppcp_rf_sub("at", ppcp_sub_read_instant, &p->at, NULL, &s_at);
    f[1] = ppcp_rf_enum("level", thermal_map, &level, &s_lvl);
    rc = ppcp_rec_read(r, f, 2);
    if (rc != PPCP_OK)
        return rc;
    if (!s_at || !s_lvl)
        return PPCP_ERR_MALFORMED;
    p->level = (ppcp_thermal_level)level;
    return PPCP_OK;
}

static ppcp_result thermal_list_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_achieved_summary *s = (const ppcp_achieved_summary *)ctx;
    return ppcp_rec_write_array(w, s->thermal, sizeof(ppcp_thermal_point),
                                s->thermal_count, thermal_point_write);
}

typedef struct summary_read_ctx {
    ppcp_arena            *arena;
    ppcp_achieved_summary *out;
} summary_read_ctx;

static ppcp_result thermal_list_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    summary_read_ctx *c = (summary_read_ctx *)ctx;
    void   *base = NULL;
    size_t  count = 0;
    ppcp_result rc;
    (void)dst;
    rc = ppcp_rec_read_array_arena(r, c->arena, sizeof(ppcp_thermal_point),
                                   sizeof(int64_t), &base, &count,
                                   thermal_point_read, NULL);
    if (rc != PPCP_OK)
        return rc;
    c->out->thermal       = (const ppcp_thermal_point *)base;
    c->out->thermal_count = count;
    return PPCP_OK;
}

ppcp_result ppcp_achieved_summary_validate(const ppcp_achieved_summary *s)
{
    size_t i;
    if (s == NULL)
        return PPCP_ERR_INVALID;
    if (s->has_frame_count && s->frame_count < 0)
        return PPCP_ERR_INVALID;
    if (s->has_dropped_frames && s->dropped_frames < 0)
        return PPCP_ERR_INVALID;
    if (s->has_realised_rate_mhz && s->realised_rate_mhz < 0)
        return PPCP_ERR_INVALID;
    if (s->thermal_count > 0 && s->thermal == NULL)
        return PPCP_ERR_INVALID;
    for (i = 0; i < s->thermal_count; i++) {
        if (ppcp_instant_validate(&s->thermal[i].at) != PPCP_OK)
            return PPCP_ERR_INVALID;
    }
    return PPCP_OK;
}

ppcp_result ppcp_achieved_summary_encode(ppcp_cbor_writer *w, const ppcp_achieved_summary *s)
{
    ppcp_wfield f[6];
    size_t      n  = 0;
    ppcp_result rc = ppcp_achieved_summary_validate(s);
    if (rc != PPCP_OK)
        return rc;
    if (s->has_frame_count)
        f[n++] = ppcp_wf_int("frame_count", s->frame_count);
    if (s->has_dropped_frames)
        f[n++] = ppcp_wf_int("dropped_frames", s->dropped_frames);
    if (s->has_realised_rate_mhz)
        f[n++] = ppcp_wf_int("realised_rate_mhz", s->realised_rate_mhz);
    if (s->exposure_ns.present)
        f[n++] = ppcp_wf_sub("exposure_ns", range3_write, &s->exposure_ns);
    if (s->iso.present)
        f[n++] = ppcp_wf_sub("iso", range3_write, &s->iso);
    if (s->thermal_count > 0)
        f[n++] = ppcp_wf_sub("thermal", thermal_list_write, s);
    return ppcp_rec_write(w, f, n);
}

ppcp_result ppcp_achieved_summary_decode(ppcp_cbor_reader *r, ppcp_arena *a,
                                         ppcp_achieved_summary *out)
{
    ppcp_rfield      f[6];
    size_t           n = 0;
    summary_read_ctx ctx;
    ppcp_result      rc;

    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    ctx.arena = a;
    ctx.out   = out;

    f[n++] = ppcp_rf("frame_count", PPCP_F_INT, &out->frame_count, &out->has_frame_count);
    f[n++] = ppcp_rf("dropped_frames", PPCP_F_INT, &out->dropped_frames,
                     &out->has_dropped_frames);
    f[n++] = ppcp_rf("realised_rate_mhz", PPCP_F_INT, &out->realised_rate_mhz,
                     &out->has_realised_rate_mhz);
    f[n++] = ppcp_rf_sub("exposure_ns", range3_read, &out->exposure_ns, NULL, NULL);
    f[n++] = ppcp_rf_sub("iso", range3_read, &out->iso, NULL, NULL);
    f[n++] = ppcp_rf_sub("thermal", thermal_list_read, NULL, &ctx, NULL);

    rc = ppcp_rec_read(r, f, n);
    if (rc != PPCP_OK)
        return rc;
    if (ppcp_achieved_summary_validate(out) != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

/* ------------------------------------------------------- per-frame Matrix3 */

ppcp_result ppcp_per_frame_m3_scalar(ppcp_per_frame_m3 *out, const ppcp_matrix3 *v)
{
    if (out == NULL || v == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    out->form   = PPCP_PER_FRAME_SCALAR;
    out->scalar = *v;
    return PPCP_OK;
}

ppcp_result ppcp_per_frame_m3_array(ppcp_per_frame_m3 *out, const ppcp_matrix3 *values,
                                    size_t count)
{
    if (out == NULL || values == NULL)
        return PPCP_ERR_INVALID;
    /* ENC 4.1d: an empty `intrinsics` array MUST NOT be emitted — it has no
     * first element to branch on, and a Capture with no frames carries no
     * AchievedFrames at all (5.8d). */
    if (count == 0)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    out->form   = PPCP_PER_FRAME_ARRAY;
    out->values = values;
    out->count  = count;
    return PPCP_OK;
}

ppcp_result ppcp_per_frame_m3_at(const ppcp_per_frame_m3 *pf, size_t frame_count,
                                 size_t index, ppcp_matrix3 *out)
{
    if (pf == NULL || out == NULL || index >= frame_count)
        return PPCP_ERR_INVALID;
    switch (pf->form) {
    case PPCP_PER_FRAME_SCALAR:
        *out = pf->scalar;
        return PPCP_OK;
    case PPCP_PER_FRAME_ARRAY:
        /* 5.8f: a parallel array has exactly frames.ns length. */
        if (pf->count != frame_count || pf->values == NULL)
            return PPCP_ERR_INVALID;
        *out = pf->values[index];
        return PPCP_OK;
    case PPCP_PER_FRAME_ABSENT:
    default:
        return PPCP_ERR_NOT_FOUND;
    }
}

/* ------------------------------------------------- AchievedFrames encode */

static ppcp_result write_matrix3(ppcp_cbor_writer *w, const ppcp_matrix3 *m)
{
    size_t i;
    if (ppcp_cbor_write_array(w, 9) != PPCP_OK)
        return ppcp_cbor_writer_status(w);
    for (i = 0; i < 9; i++) {
        if (ppcp_cbor_write_double(w, m->m[i]) != PPCP_OK)
            return ppcp_cbor_writer_status(w);
    }
    return PPCP_OK;
}

static ppcp_result pf_i64_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_per_frame_i64 *pf = (const ppcp_per_frame_i64 *)ctx;
    size_t i;
    if (pf->form == PPCP_PER_FRAME_SCALAR)
        return ppcp_cbor_write_int(w, pf->scalar);
    if (pf->form != PPCP_PER_FRAME_ARRAY || pf->values == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_cbor_write_array(w, pf->count) != PPCP_OK)
        return ppcp_cbor_writer_status(w);
    for (i = 0; i < pf->count; i++) {
        if (ppcp_cbor_write_int(w, pf->values[i]) != PPCP_OK)
            return ppcp_cbor_writer_status(w);
    }
    return PPCP_OK;
}

static ppcp_result pf_m3_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_per_frame_m3 *pf = (const ppcp_per_frame_m3 *)ctx;
    size_t i;
    if (pf->form == PPCP_PER_FRAME_SCALAR)
        return write_matrix3(w, &pf->scalar);          /* a bare [f64 x 9] */
    if (pf->form != PPCP_PER_FRAME_ARRAY || pf->values == NULL || pf->count == 0)
        return PPCP_ERR_INVALID;                       /* ENC 4.1d: never empty */
    if (ppcp_cbor_write_array(w, pf->count) != PPCP_OK)
        return ppcp_cbor_writer_status(w);
    for (i = 0; i < pf->count; i++) {
        ppcp_result rc = write_matrix3(w, &pf->values[i]);
        if (rc != PPCP_OK)
            return rc;
    }
    return PPCP_OK;
}

static const ppcp_enum_map exposure_prov_map[] = {
    { "per_frame",       PPCP_EXP_PER_FRAME       },
    { "sampled",         PPCP_EXP_SAMPLED         },
    { "locked_constant", PPCP_EXP_LOCKED_CONSTANT },
    { NULL, 0 }
};

static ppcp_result frames_series_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_achieved_frames *af = (const ppcp_achieved_frames *)ctx;
    ppcp_series s;
    memset(&s, 0, sizeof(s));
    s.tb    = af->tb;
    s.ns    = af->frames_ns;
    s.count = af->frame_count;
    return ppcp_series_encode(w, &s);
}

ppcp_result ppcp_achieved_frames_encode(ppcp_cbor_writer *w, const ppcp_achieved_frames *af)
{
    ppcp_wfield f[5];
    size_t      n  = 0;
    ppcp_result rc = ppcp_achieved_frames_validate(af);
    if (rc != PPCP_OK)
        return rc;

    f[n++] = ppcp_wf_sub("frames", frames_series_write, af);
    if (af->exposure_ns.form != PPCP_PER_FRAME_ABSENT) {
        f[n++] = ppcp_wf_sub("exposure_ns", pf_i64_write, &af->exposure_ns);
        /* 5.8 / I31: `exposure_provenance` travels WITH `exposure_ns`, always. */
        if (!af->has_exposure_provenance)
            return PPCP_ERR_INVALID;
        f[n++] = ppcp_wf_enum("exposure_provenance", exposure_prov_map,
                              (int)af->exposure_provenance);
    }
    if (af->iso.form != PPCP_PER_FRAME_ABSENT)
        f[n++] = ppcp_wf_sub("iso", pf_i64_write, &af->iso);
    if (af->intrinsics.form != PPCP_PER_FRAME_ABSENT)
        f[n++] = ppcp_wf_sub("intrinsics", pf_m3_write, &af->intrinsics);
    return ppcp_rec_write(w, f, n);
}

/* ------------------------------------------------- AchievedFrames decode */

typedef struct af_read_ctx {
    ppcp_arena           *arena;
    ppcp_achieved_frames *out;
} af_read_ctx;

static ppcp_result read_number(ppcp_cbor_reader *r, double *out)
{
    ppcp_cbor_item it;
    ppcp_result    rc = ppcp_cbor_read(r, &it);
    if (rc != PPCP_OK)
        return rc;
    if (it.type == PPCP_CBOR_DOUBLE) { *out = it.f; return PPCP_OK; }
    if (it.type == PPCP_CBOR_UINT || it.type == PPCP_CBOR_NINT) {
        *out = (double)it.i;
        return PPCP_OK;
    }
    return PPCP_ERR_MALFORMED;
}

static ppcp_result frames_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    af_read_ctx *c = (af_read_ctx *)ctx;
    ppcp_cbor_item it;
    ppcp_result    rc;
    uint32_t       i, pairs;
    bool           seen_tb = false, seen_ns = false;
    int64_t       *ns = NULL;
    size_t         count = 0;
    (void)dst;

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
            if (ppcp_id_set(&c->out->tb, (const char *)it.bytes, it.len) != PPCP_OK)
                return PPCP_ERR_MALFORMED;
            seen_tb = true;
        } else if (ppcp_cbor_key_is(k, klen, "ns")) {
            uint32_t j;
            rc = ppcp_cbor_read(r, &it);
            if (rc != PPCP_OK) return rc;
            if (it.type != PPCP_CBOR_ARRAY) return PPCP_ERR_MALFORMED;
            count = it.count;
            if (count > 0) {
                ns = (int64_t *)ppcp_arena_take(c->arena, count, sizeof(int64_t),
                                                sizeof(int64_t));
                if (ns == NULL) return PPCP_ERR_LIMIT;
            }
            for (j = 0; j < count; j++) {
                rc = ppcp_cbor_read(r, &it);
                if (rc != PPCP_OK) return rc;
                if (it.type != PPCP_CBOR_UINT && it.type != PPCP_CBOR_NINT)
                    return PPCP_ERR_MALFORMED;
                ns[j] = it.i;
            }
            seen_ns = true;
        } else {
            rc = ppcp_cbor_skip(r);
            if (rc != PPCP_OK) return rc;
        }
    }
    /* I1 / 5.8e: `frames` carries `tb`, and `ns` has no scalar form — a nominal
     * rate is not a substitute for measured timestamps (I2). */
    if (!seen_tb || !seen_ns)
        return PPCP_ERR_MALFORMED;
    c->out->frames_ns   = ns;
    c->out->frame_count = count;
    return PPCP_OK;
}

static ppcp_result pf_i64_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    af_read_ctx        *c  = (af_read_ctx *)ctx;
    ppcp_per_frame_i64 *pf = (ppcp_per_frame_i64 *)dst;
    ppcp_cbor_item      it;
    ppcp_result         rc;
    uint32_t            i;
    int64_t            *v;

    rc = ppcp_cbor_read(r, &it);
    if (rc != PPCP_OK)
        return rc;
    /* ENC 4.1d: the two forms are distinguished by CBOR major type, not by
     * length — a one-frame Capture still encodes an array of one. */
    if (it.type == PPCP_CBOR_UINT || it.type == PPCP_CBOR_NINT) {
        pf->form   = PPCP_PER_FRAME_SCALAR;
        pf->scalar = it.i;
        return PPCP_OK;
    }
    if (it.type != PPCP_CBOR_ARRAY)
        return PPCP_ERR_MALFORMED;
    v = NULL;
    if (it.count > 0) {
        v = (int64_t *)ppcp_arena_take(c->arena, it.count, sizeof(int64_t), sizeof(int64_t));
        if (v == NULL)
            return PPCP_ERR_LIMIT;
    }
    for (i = 0; i < it.count; i++) {
        ppcp_cbor_item e;
        rc = ppcp_cbor_read(r, &e);
        if (rc != PPCP_OK) return rc;
        if (e.type != PPCP_CBOR_UINT && e.type != PPCP_CBOR_NINT)
            return PPCP_ERR_MALFORMED;
        v[i] = e.i;
    }
    pf->form   = PPCP_PER_FRAME_ARRAY;
    pf->values = v;
    pf->count  = it.count;
    return PPCP_OK;
}

static ppcp_result pf_m3_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    af_read_ctx       *c  = (af_read_ctx *)ctx;
    ppcp_per_frame_m3 *pf = (ppcp_per_frame_m3 *)dst;
    ppcp_cbor_item     head, first;
    ppcp_result        rc;
    size_t             i, j;
    ppcp_matrix3      *v;

    rc = ppcp_cbor_read(r, &head);
    if (rc != PPCP_OK)
        return rc;
    if (head.type != PPCP_CBOR_ARRAY)
        return PPCP_ERR_MALFORMED;
    /* ENC 4.1d: an EMPTY intrinsics array is malformed on receipt.  It has no
     * first element to branch on, and this is the branch. */
    if (head.count == 0)
        return PPCP_ERR_MALFORMED;

    rc = ppcp_cbor_read(r, &first);
    if (rc != PPCP_OK)
        return rc;

    if (first.type == PPCP_CBOR_DOUBLE || first.type == PPCP_CBOR_UINT ||
        first.type == PPCP_CBOR_NINT) {
        /* A number first: one Matrix3, constant across the Capture. */
        if (head.count != 9)
            return PPCP_ERR_MALFORMED;
        pf->form = PPCP_PER_FRAME_SCALAR;
        pf->scalar.m[0] = (first.type == PPCP_CBOR_DOUBLE) ? first.f : (double)first.i;
        for (i = 1; i < 9; i++) {
            rc = read_number(r, &pf->scalar.m[i]);
            if (rc != PPCP_OK)
                return rc;
        }
        return PPCP_OK;
    }
    if (first.type != PPCP_CBOR_ARRAY)
        return PPCP_ERR_MALFORMED;

    /* An array first: one Matrix3 per frame.  The head we already read is
     * element zero's own array head. */
    v = (ppcp_matrix3 *)ppcp_arena_take(c->arena, head.count, sizeof(ppcp_matrix3),
                                        sizeof(double));
    if (v == NULL)
        return PPCP_ERR_LIMIT;
    if (first.count != 9)
        return PPCP_ERR_MALFORMED;
    for (j = 0; j < 9; j++) {
        rc = read_number(r, &v[0].m[j]);
        if (rc != PPCP_OK)
            return rc;
    }
    for (i = 1; i < head.count; i++) {
        ppcp_cbor_item e;
        rc = ppcp_cbor_read(r, &e);
        if (rc != PPCP_OK) return rc;
        if (e.type != PPCP_CBOR_ARRAY || e.count != 9)
            return PPCP_ERR_MALFORMED;
        for (j = 0; j < 9; j++) {
            rc = read_number(r, &v[i].m[j]);
            if (rc != PPCP_OK)
                return rc;
        }
    }
    pf->form   = PPCP_PER_FRAME_ARRAY;
    pf->values = v;
    pf->count  = head.count;
    return PPCP_OK;
}

ppcp_result ppcp_achieved_frames_decode(ppcp_cbor_reader *r, ppcp_arena *a,
                                        ppcp_achieved_frames *out)
{
    ppcp_rfield f[5];
    size_t      n = 0;
    af_read_ctx ctx;
    bool        seen_frames = false, seen_exposure = false;
    int         prov = 0;
    ppcp_result rc;

    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    ctx.arena = a;
    ctx.out   = out;

    f[n++] = ppcp_rf_sub("frames", frames_read, NULL, &ctx, &seen_frames);
    f[n++] = ppcp_rf_sub("exposure_ns", pf_i64_read, &out->exposure_ns, &ctx, &seen_exposure);
    f[n++] = ppcp_rf_enum("exposure_provenance", exposure_prov_map, &prov,
                          &out->has_exposure_provenance);
    f[n++] = ppcp_rf_sub("iso", pf_i64_read, &out->iso, &ctx, NULL);
    f[n++] = ppcp_rf_sub("intrinsics", pf_m3_read, &out->intrinsics, &ctx, NULL);

    rc = ppcp_rec_read(r, f, n);
    if (rc != PPCP_OK)
        return rc;
    if (!seen_frames)
        return PPCP_ERR_MALFORMED;
    out->exposure_provenance = (ppcp_exposure_provenance)prov;
    if (seen_exposure && !out->has_exposure_provenance)
        return PPCP_ERR_MALFORMED;   /* I31: the value never travels alone */
    if (ppcp_achieved_frames_validate(out) != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}
