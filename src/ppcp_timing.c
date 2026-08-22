/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * PPCP-CORE §6.1, §6.2 and the §5.8 accessors.
 */
#include "ppcp/timing.h"

#include <string.h>

static bool sigma_ok(double s)
{
    if (s != s) return false;
    if (s < 0.0) return false;
    if (s > 1.0e300 && s * 0.5 == s) return false;
    return true;
}

static bool provenance_ok(ppcp_provenance p)
{
    return p == PPCP_PROV_ASSUMED || p == PPCP_PROV_VENDOR || p == PPCP_PROV_MEASURED;
}

/* ---------------------------------------------------------------- Timing */

ppcp_result ppcp_timing_make(ppcp_timing *out, ppcp_convention convention)
{
    if (out == NULL)
        return PPCP_ERR_INVALID;
    if (convention != PPCP_CONV_MID && convention != PPCP_CONV_START &&
        convention != PPCP_CONV_END)
        /* I22's "only if" half: nominal_frame_start reaches the other
         * constructor, which cannot be called without the offset. */
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    out->convention = convention;
    return PPCP_OK;
}

ppcp_result ppcp_timing_make_nominal_frame_start(ppcp_timing *out, int64_t offset_ns,
                                                 ppcp_provenance provenance)
{
    if (out == NULL || !provenance_ok(provenance))
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    out->convention                        = PPCP_CONV_NOMINAL_FRAME_START;
    out->has_offset                        = true;
    out->frame_start_to_exposure_offset_ns = offset_ns;
    /* 5.7b / I31: always with its provenance.  A declared zero is a checkable
     * claim; a declared zero with no provenance is indistinguishable from an
     * unmeasured one, which is the defect MeasuredCapability.method exists to
     * prevent one layer up. */
    out->offset_provenance                 = provenance;
    return PPCP_OK;
}

ppcp_result ppcp_timing_set_offset_sigma(ppcp_timing *t, double sigma_ns)
{
    if (t == NULL || !t->has_offset || !sigma_ok(sigma_ns))
        return PPCP_ERR_INVALID;
    t->has_offset_sigma                        = true;
    t->frame_start_to_exposure_offset_sigma_ns = sigma_ns;
    return PPCP_OK;
}

ppcp_result ppcp_timing_validate(const ppcp_timing *t)
{
    if (t == NULL)
        return PPCP_ERR_INVALID;
    switch (t->convention) {
    case PPCP_CONV_MID:
    case PPCP_CONV_START:
    case PPCP_CONV_END:
        /* I22, "if" half: present only with nominal_frame_start. */
        if (t->has_offset)
            return PPCP_ERR_MALFORMED;
        return PPCP_OK;
    case PPCP_CONV_NOMINAL_FRAME_START:
        /* I22, "only if" half. */
        if (!t->has_offset)
            return PPCP_ERR_MALFORMED;
        if (!provenance_ok(t->offset_provenance))
            return PPCP_ERR_MALFORMED;                      /* 5.7b, I31 */
        if (t->has_offset_sigma && !sigma_ok(t->frame_start_to_exposure_offset_sigma_ns))
            return PPCP_ERR_MALFORMED;
        return PPCP_OK;
    }
    return PPCP_ERR_MALFORMED;
}

/* -------------------------------------------------------------- Geometry */

ppcp_result ppcp_geometry_make_global(ppcp_geometry *out)
{
    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    out->kind = PPCP_GEOM_GLOBAL;
    return PPCP_OK;
}

ppcp_result ppcp_geometry_make_rolling_shutter(ppcp_geometry *out, int64_t readout_ns,
                                               ppcp_provenance readout_provenance,
                                               ppcp_rolling_direction direction,
                                               uint32_t rows)
{
    if (out == NULL || !provenance_ok(readout_provenance))
        return PPCP_ERR_INVALID;
    if (direction != PPCP_ROLL_TOP_TO_BOTTOM && direction != PPCP_ROLL_BOTTOM_TO_TOP)
        return PPCP_ERR_INVALID;
    if (readout_ns < 0 || rows == 0)
        return PPCP_ERR_INVALID;

    memset(out, 0, sizeof(*out));
    out->kind = PPCP_GEOM_ROLLING_SHUTTER;
    /* 6.2a: the interval between the exposure start of the first row read and
     * that of the last row read.  Not the frame period, and not the total
     * sensor readout including blanking. */
    out->readout_ns = readout_ns;
    /* 5.7e / I31: no public platform API exposes readout_ns, so an
     * implementation that has not been through a timecode rig is guessing —
     * and has to say so. */
    out->readout_provenance = readout_provenance;
    out->direction          = direction;
    out->rows               = rows;
    return PPCP_OK;
}

ppcp_result ppcp_geometry_set_readout_sigma(ppcp_geometry *g, double sigma_ns)
{
    if (g == NULL || g->kind != PPCP_GEOM_ROLLING_SHUTTER || !sigma_ok(sigma_ns))
        return PPCP_ERR_INVALID;
    g->has_readout_sigma = true;
    g->readout_sigma_ns  = sigma_ns;
    return PPCP_OK;
}

ppcp_result ppcp_geometry_validate(const ppcp_geometry *g)
{
    if (g == NULL)
        return PPCP_ERR_INVALID;
    if (g->kind == PPCP_GEOM_GLOBAL) {
        if (g->readout_ns != 0 || g->rows != 0)
            return PPCP_ERR_MALFORMED;
        return PPCP_OK;
    }
    if (g->kind != PPCP_GEOM_ROLLING_SHUTTER)
        return PPCP_ERR_MALFORMED;
    if (g->rows == 0 || g->readout_ns < 0)
        return PPCP_ERR_MALFORMED;
    if (!provenance_ok(g->readout_provenance))
        return PPCP_ERR_MALFORMED;
    if (g->has_readout_sigma && !sigma_ok(g->readout_sigma_ns))
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

/* ------------------------------------------------------- canonical instant */

ppcp_result ppcp_canonical_instant(const ppcp_timing *timing, int64_t t_ns,
                                   ppcp_duration_ns d_ns, int64_t *out_ns)
{
    ppcp_result rc;
    int64_t     half;

    if (out_ns == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_timing_validate(timing);
    if (rc != PPCP_OK)
        return rc;
    /* An exposure duration is a duration: it is not signed, and a negative one
     * is a caller that has passed a timestamp where a length belongs. */
    if (d_ns < 0)
        return PPCP_ERR_INVALID;

    /* Truncating division, used identically here and in the inverse, so the
     * round trip of CT-S1 assertion 4 is bit-exact for odd `d` as well as
     * even.  Every worked example of §6.1.1 has an even `d`, so the choice is
     * not observable there — it is observable in the round trip, which is why
     * the inverse mirrors the expression rather than recomputing it. */
    half = d_ns / 2;

    switch (timing->convention) {
    case PPCP_CONV_MID:
        *out_ns = t_ns;
        return PPCP_OK;
    case PPCP_CONV_START:
        *out_ns = t_ns + half;
        return PPCP_OK;
    case PPCP_CONV_END:
        *out_ns = t_ns - half;
        return PPCP_OK;
    case PPCP_CONV_NOMINAL_FRAME_START:
        /* 6.1b / I17: all three of convention, offset and the per-frame `d`.
         * An implementation that ignores the offset passes every other test in
         * the suite (CT-S1 assertion 2). */
        *out_ns = t_ns + timing->frame_start_to_exposure_offset_ns + half;
        return PPCP_OK;
    }
    return PPCP_ERR_INVALID;
}

ppcp_result ppcp_canonical_instant_inverse(const ppcp_timing *timing, int64_t canonical_ns,
                                           ppcp_duration_ns d_ns, int64_t *out_ns)
{
    ppcp_result rc;
    int64_t     half;

    if (out_ns == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_timing_validate(timing);
    if (rc != PPCP_OK)
        return rc;
    if (d_ns < 0)
        return PPCP_ERR_INVALID;

    half = d_ns / 2;

    switch (timing->convention) {
    case PPCP_CONV_MID:
        *out_ns = canonical_ns;
        return PPCP_OK;
    case PPCP_CONV_START:
        *out_ns = canonical_ns - half;
        return PPCP_OK;
    case PPCP_CONV_END:
        *out_ns = canonical_ns + half;
        return PPCP_OK;
    case PPCP_CONV_NOMINAL_FRAME_START:
        *out_ns = canonical_ns - timing->frame_start_to_exposure_offset_ns - half;
        return PPCP_OK;
    }
    return PPCP_ERR_INVALID;
}

ppcp_result ppcp_canonical_instant_of(const ppcp_timing *timing, const ppcp_instant *raw,
                                      ppcp_duration_ns d_ns, ppcp_instant *out)
{
    int64_t     ns = 0;
    ppcp_result rc;

    rc = ppcp_instant_validate(raw);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_canonical_instant(timing, raw->ns, d_ns, &ns);
    if (rc != PPCP_OK)
        return rc;
    /* The conversion does not change the timebase: it corrects where in the
     * exposure the timestamp points, not which clock it is on. */
    return ppcp_instant_make(out, raw->tb.v, raw->tb.len, ns);
}

ppcp_result ppcp_row_instant(const ppcp_geometry *geometry, int64_t canonical_first_ns,
                             uint32_t r, int64_t *out_ns)
{
    ppcp_result rc;
    uint32_t    R, k;
    int64_t     num, span;

    if (out_ns == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_geometry_validate(geometry);
    if (rc != PPCP_OK)
        return rc;

    if (geometry->kind == PPCP_GEOM_GLOBAL) {
        /* Every row of a globally-shuttered frame is exposed together. */
        *out_ns = canonical_first_ns;
        return PPCP_OK;
    }

    R = geometry->rows;
    if (r >= R)
        return PPCP_ERR_INVALID;
    if (R == 1) {
        /* 6.2d's tail: where R == 1 the row instant is canonical_first.  Stated
         * separately because the general formula divides by R − 1. */
        *out_ns = canonical_first_ns;
        return PPCP_OK;
    }

    /* 6.2d.  `r` is 0-based with r = 0 at the top of the *delivered* image, and
     * `canonical_first` is the instant of the first row *read* (6.2c) — which
     * under bottom_to_top is the bottom of the image, hence the reversal. */
    k = (geometry->direction == PPCP_ROLL_TOP_TO_BOTTOM) ? r : (R - 1u - r);

    span = geometry->readout_ns;
    /* readout_ns is a frame-scale quantity (tens of milliseconds at most) and
     * rows a sensor dimension, so the product cannot approach int64 — but the
     * guard is cheap and the alternative is a silent wrap. */
    if (span != 0 && (int64_t)k > INT64_MAX / span)
        return PPCP_ERR_INVALID;
    num = span * (int64_t)k;

    /* ⚠ 6.2d gives the formula over the reals and does not say how to round.
     * Round half away from zero, which is exact at both ends (r = 0 gives 0 and
     * r = R − 1 gives readout_ns under either direction) and never off by more
     * than half a nanosecond in between.  Recorded as a specification
     * ambiguity; see docs/conformance/claim-libppcp.md. */
    *out_ns = canonical_first_ns + (num + (int64_t)(R - 1u) / 2) / (int64_t)(R - 1u);
    return PPCP_OK;
}

/* -------------------------------------------------------- AchievedFrames */

ppcp_result ppcp_per_frame_i64_scalar(ppcp_per_frame_i64 *out, int64_t v)
{
    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    out->form   = PPCP_PER_FRAME_SCALAR;
    out->scalar = v;
    return PPCP_OK;
}

ppcp_result ppcp_per_frame_i64_array(ppcp_per_frame_i64 *out, const int64_t *values,
                                     size_t count)
{
    if (out == NULL || (count > 0 && values == NULL))
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    out->form   = PPCP_PER_FRAME_ARRAY;
    out->values = values;
    out->count  = count;
    return PPCP_OK;
}

ppcp_result ppcp_per_frame_i64_at(const ppcp_per_frame_i64 *pf, size_t frame_count,
                                  size_t index, int64_t *out)
{
    if (pf == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    if (index >= frame_count)
        return PPCP_ERR_INVALID;

    switch (pf->form) {
    case PPCP_PER_FRAME_ABSENT:
        return PPCP_ERR_NOT_FOUND;
    case PPCP_PER_FRAME_SCALAR:
        /* 5.8f: a scalar means the value was constant for every frame in the
         * Capture, and MUST NOT mean "unknown" or "not sampled". */
        *out = pf->scalar;
        return PPCP_OK;
    case PPCP_PER_FRAME_ARRAY:
        /* ENC 4.1c / 5.8f: a parallel array has exactly frames.ns length.  A
         * short array is malformed rather than something to index carefully. */
        if (pf->count != frame_count)
            return PPCP_ERR_MALFORMED;
        *out = pf->values[index];
        return PPCP_OK;
    }
    return PPCP_ERR_INVALID;
}

ppcp_result ppcp_achieved_frames_make(ppcp_achieved_frames *out, const char *tb,
                                      const int64_t *frames_ns, size_t frame_count)
{
    ppcp_result rc;

    if (out == NULL || (frame_count > 0 && frames_ns == NULL))
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    /* I1 and I2 together: the series carries its timebase, and it is a series
     * — there is no scalar form for frames.ns, because a nominal rate is not a
     * substitute for measured timestamps (5.8e). */
    rc = ppcp_id_set(&out->tb, tb, tb ? strlen(tb) : 0);
    if (rc != PPCP_OK)
        return rc;
    out->frames_ns   = frames_ns;
    out->frame_count = frame_count;
    return PPCP_OK;
}

ppcp_result ppcp_achieved_frames_set_exposure(ppcp_achieved_frames *af,
                                              const ppcp_per_frame_i64 *exposure,
                                              ppcp_exposure_provenance provenance)
{
    if (af == NULL || exposure == NULL)
        return PPCP_ERR_INVALID;
    if (exposure->form == PPCP_PER_FRAME_ABSENT)
        return PPCP_ERR_INVALID;
    if (exposure->form == PPCP_PER_FRAME_ARRAY && exposure->count != af->frame_count)
        return PPCP_ERR_INVALID;                 /* ENC 4.1c */
    if (provenance != PPCP_EXP_PER_FRAME && provenance != PPCP_EXP_SAMPLED &&
        provenance != PPCP_EXP_LOCKED_CONSTANT)
        return PPCP_ERR_INVALID;
    /* 5.8: `exposure_provenance` is mandatory with `exposure_ns`.  It is a
     * parameter here for that reason — the pair cannot be split. */
    af->exposure_ns             = *exposure;
    af->exposure_provenance     = provenance;
    af->has_exposure_provenance = true;
    return PPCP_OK;
}

ppcp_result ppcp_achieved_frames_validate(const ppcp_achieved_frames *af)
{
    if (af == NULL || !ppcp_id_is_set(&af->tb))
        return PPCP_ERR_INVALID;
    if (af->frame_count > 0 && af->frames_ns == NULL)
        return PPCP_ERR_INVALID;
    if (af->exposure_ns.form == PPCP_PER_FRAME_ARRAY &&
        af->exposure_ns.count != af->frame_count)
        return PPCP_ERR_MALFORMED;
    if (af->iso.form == PPCP_PER_FRAME_ARRAY && af->iso.count != af->frame_count)
        return PPCP_ERR_MALFORMED;
    if (af->exposure_ns.form != PPCP_PER_FRAME_ABSENT && !af->has_exposure_provenance)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

ppcp_result ppcp_achieved_frames_exposure_at(const ppcp_achieved_frames *af, size_t index,
                                             int64_t *out_ns)
{
    if (af == NULL)
        return PPCP_ERR_INVALID;
    return ppcp_per_frame_i64_at(&af->exposure_ns, af->frame_count, index, out_ns);
}

ppcp_result ppcp_achieved_frames_canonical_at(const ppcp_achieved_frames *af,
                                              const ppcp_timing *timing, size_t index,
                                              ppcp_instant *out)
{
    int64_t      d = 0;
    ppcp_instant raw;
    ppcp_result  rc;

    rc = ppcp_achieved_frames_validate(af);
    if (rc != PPCP_OK)
        return rc;
    if (index >= af->frame_count)
        return PPCP_ERR_INVALID;

    /* 5.8d / I17: without exposure the conversion is impossible, so it fails
     * rather than falling back to the profile's exposure range (6.1c). */
    rc = ppcp_achieved_frames_exposure_at(af, index, &d);
    if (rc != PPCP_OK)
        return rc;

    rc = ppcp_instant_make(&raw, af->tb.v, af->tb.len, af->frames_ns[index]);
    if (rc != PPCP_OK)
        return rc;
    return ppcp_canonical_instant_of(timing, &raw, d, out);
}
