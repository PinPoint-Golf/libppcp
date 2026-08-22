/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * PPCP-CORE §5.2, §5.6, §5.7 and §5.9 — Peer, Source, CaptureProfile and
 * Calibration.  Work package L4.
 *
 * I19 is the clause this file exists to make unavoidable: every Source
 * declares `timing`, `geometry` and `intrinsics` regardless of which peer owns
 * it, so a host cannot hardcode its own cameras' convention and a third-party
 * host can participate.  The validator therefore checks a *camera* Source's
 * profiles for all three rather than trusting the constructor, because a host
 * implementer who has never met a rolling shutter is exactly who would leave
 * `geometry` out.
 */
#include "ppcp/model.h"
#include "ppcp_codec.h"

#include <string.h>

/* ------------------------------------------------------------- enum maps */

static const ppcp_enum_map convention_map[] = {
    { "mid",                 PPCP_CONV_MID                 },
    { "start",               PPCP_CONV_START               },
    { "end",                 PPCP_CONV_END                 },
    { "nominal_frame_start", PPCP_CONV_NOMINAL_FRAME_START },
    { NULL, 0 }
};

static const ppcp_enum_map provenance_map[] = {
    { "assumed",  PPCP_PROV_ASSUMED  },
    { "vendor",   PPCP_PROV_VENDOR   },
    { "measured", PPCP_PROV_MEASURED },
    { NULL, 0 }
};

static const ppcp_enum_map direction_map[] = {
    { "top_to_bottom", PPCP_ROLL_TOP_TO_BOTTOM },
    { "bottom_to_top", PPCP_ROLL_BOTTOM_TO_TOP },
    { NULL, 0 }
};

static const ppcp_enum_map intrinsics_map[] = {
    { "per_frame", PPCP_INTR_PER_FRAME },
    { "fixed",     PPCP_INTR_FIXED     },
    { "none",      PPCP_INTR_NONE      },
    { NULL, 0 }
};

static const ppcp_enum_map role_map[] = {
    { "host",     PPCP_ROLE_HOST     },
    { "capture",  PPCP_ROLE_CAPTURE  },
    { "observer", PPCP_ROLE_OBSERVER },
    { NULL, 0 }
};

static const ppcp_enum_map calibration_method_map[] = {
    { "factory",           PPCP_CALM_FACTORY           },
    { "per_frame",         PPCP_CALM_PER_FRAME         },
    { "solved",            PPCP_CALM_SOLVED            },
    { "user_measured",     PPCP_CALM_USER_MEASURED     },
    { "estimated_online",  PPCP_CALM_ESTIMATED_ONLINE  },
    { NULL, 0 }
};

static const ppcp_enum_map viewpoint_method_map[] = {
    { "declared",   PPCP_VP_DECLARED   },
    { "classified", PPCP_VP_CLASSIFIED },
    { NULL, 0 }
};

const ppcp_enum_map *ppcp_role_enum_map(void) { return role_map; }

const char *ppcp_role_str(ppcp_role r)
{
    const char *s = ppcp_enum_to_text(role_map, (int)r);
    return s ? s : "observer";
}

/* --------------------------------------------------------------- Timing */

static ppcp_result timing_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_timing *t = (const ppcp_timing *)ctx;
    ppcp_wfield f[4];
    size_t      n  = 0;
    ppcp_result rc = ppcp_timing_validate(t);
    if (rc != PPCP_OK)
        return rc;
    f[n++] = ppcp_wf_enum("convention", convention_map, (int)t->convention);
    if (t->has_offset) {
        /* I22 / 5.7b: present iff `nominal_frame_start`, explicit even when
         * zero, and ALWAYS with its provenance (I31) — a declared zero with no
         * provenance is indistinguishable from an unmeasured one. */
        f[n++] = ppcp_wf_int("frame_start_to_exposure_offset_ns",
                             t->frame_start_to_exposure_offset_ns);
        f[n++] = ppcp_wf_enum("frame_start_to_exposure_offset_provenance", provenance_map,
                              (int)t->offset_provenance);
        if (t->has_offset_sigma)
            f[n++] = ppcp_wf_double("frame_start_to_exposure_offset_sigma_ns",
                                    t->frame_start_to_exposure_offset_sigma_ns);
    }
    return ppcp_rec_write(w, f, n);
}

static ppcp_result timing_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_timing *t = (ppcp_timing *)dst;
    ppcp_rfield  f[4];
    bool         seen_conv = false, seen_prov = false;
    int          conv = 0, prov = 0;
    ppcp_result  rc;
    (void)ctx;

    memset(t, 0, sizeof(*t));
    f[0] = ppcp_rf_enum("convention", convention_map, &conv, &seen_conv);
    f[1] = ppcp_rf("frame_start_to_exposure_offset_ns", PPCP_F_INT,
                   &t->frame_start_to_exposure_offset_ns, &t->has_offset);
    f[2] = ppcp_rf_enum("frame_start_to_exposure_offset_provenance", provenance_map,
                        &prov, &seen_prov);
    f[3] = ppcp_rf("frame_start_to_exposure_offset_sigma_ns", PPCP_F_DOUBLE,
                   &t->frame_start_to_exposure_offset_sigma_ns, &t->has_offset_sigma);
    rc = ppcp_rec_read(r, f, 4);
    if (rc != PPCP_OK)
        return rc;
    if (!seen_conv)
        return PPCP_ERR_MALFORMED;
    t->convention        = (ppcp_convention)conv;
    t->offset_provenance = (ppcp_provenance)prov;
    if (t->has_offset && !seen_prov)
        return PPCP_ERR_MALFORMED;   /* 5.7b / I31 */
    if (ppcp_timing_validate(t) != PPCP_OK)
        return PPCP_ERR_MALFORMED;   /* I22: the iff, checked on the way in */
    return PPCP_OK;
}

/* ------------------------------------------------------------- geometry
 *
 * ENC §4 gives an encoding for every primitive and for `Anchor`, which is a
 * tagged union carried as a map with exactly one key.  It gives none for
 * `geometry`, which is the specification's other tagged union.  This encoder
 * follows `Anchor`: a map with exactly one key, `{"global": true}` or
 * `{"rolling_shutter": { ... }}`.  Recorded as an ambiguity in
 * docs/conformance/claim-libppcp.md. */

static ppcp_result rolling_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_geometry *g = (const ppcp_geometry *)ctx;
    ppcp_wfield f[5];
    size_t      n = 0;
    f[n++] = ppcp_wf_int("readout_ns", g->readout_ns);
    f[n++] = ppcp_wf_enum("readout_provenance", provenance_map, (int)g->readout_provenance);
    f[n++] = ppcp_wf_enum("direction", direction_map, (int)g->direction);
    f[n++] = ppcp_wf_uint("rows", g->rows);
    if (g->has_readout_sigma)
        f[n++] = ppcp_wf_double("readout_sigma_ns", g->readout_sigma_ns);
    return ppcp_rec_write(w, f, n);
}

static ppcp_result geometry_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_geometry *g = (const ppcp_geometry *)ctx;
    ppcp_wfield f[1];
    ppcp_result rc = ppcp_geometry_validate(g);
    if (rc != PPCP_OK)
        return rc;
    if (g->kind == PPCP_GEOM_GLOBAL)
        f[0] = ppcp_wf_bool("global", true);
    else
        f[0] = ppcp_wf_sub("rolling_shutter", rolling_write, g);
    return ppcp_rec_write(w, f, 1);
}

static ppcp_result rolling_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_geometry *g = (ppcp_geometry *)dst;
    ppcp_rfield    f[5];
    bool           s_ro = false, s_prov = false, s_dir = false, s_rows = false;
    int            prov = 0, dir = 0;
    uint64_t       rows = 0;
    ppcp_result    rc;
    (void)ctx;
    f[0] = ppcp_rf("readout_ns", PPCP_F_INT, &g->readout_ns, &s_ro);
    f[1] = ppcp_rf_enum("readout_provenance", provenance_map, &prov, &s_prov);
    f[2] = ppcp_rf_enum("direction", direction_map, &dir, &s_dir);
    f[3] = ppcp_rf("rows", PPCP_F_UINT, &rows, &s_rows);
    f[4] = ppcp_rf("readout_sigma_ns", PPCP_F_DOUBLE, &g->readout_sigma_ns,
                   &g->has_readout_sigma);
    rc = ppcp_rec_read(r, f, 5);
    if (rc != PPCP_OK)
        return rc;
    /* 5.7e / I31: `readout_ns` never travels without `readout_provenance`.  No
     * public platform API exposes it, so an implementation that has not been
     * through a timecode rig is guessing. */
    if (!s_ro || !s_prov || !s_dir || !s_rows)
        return PPCP_ERR_MALFORMED;
    if (rows > 0xFFFFFFFFu)
        return PPCP_ERR_MALFORMED;
    g->kind               = PPCP_GEOM_ROLLING_SHUTTER;
    g->readout_provenance = (ppcp_provenance)prov;
    g->direction          = (ppcp_rolling_direction)dir;
    g->rows               = (uint32_t)rows;
    return PPCP_OK;
}

static ppcp_result geometry_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_geometry *g = (ppcp_geometry *)dst;
    ppcp_cbor_item it;
    ppcp_result    rc;
    uint32_t       i;
    unsigned       keys = 0;
    (void)ctx;

    memset(g, 0, sizeof(*g));
    rc = ppcp_cbor_read(r, &it);
    if (rc != PPCP_OK) return rc;
    if (it.type != PPCP_CBOR_MAP) return PPCP_ERR_MALFORMED;

    for (i = 0; i < it.count; i++) {
        const char *k; size_t klen;
        rc = ppcp_cbor_read_key(r, &k, &klen);
        if (rc != PPCP_OK) return rc;
        if (ppcp_cbor_key_is(k, klen, "global")) {
            ppcp_cbor_item v;
            rc = ppcp_cbor_read(r, &v);
            if (rc != PPCP_OK) return rc;
            if (v.type != PPCP_CBOR_BOOL || !v.b) return PPCP_ERR_MALFORMED;
            g->kind = PPCP_GEOM_GLOBAL;
            keys++;
        } else if (ppcp_cbor_key_is(k, klen, "rolling_shutter")) {
            rc = rolling_read(r, g, NULL);
            if (rc != PPCP_OK) return rc;
            keys++;
        } else {
            rc = ppcp_cbor_skip(r);
            if (rc != PPCP_OK) return rc;
        }
    }
    if (keys != 1)
        return PPCP_ERR_MALFORMED;
    if (ppcp_geometry_validate(g) != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

/* -------------------------------------------------------- CaptureProfile */

ppcp_result ppcp_capture_profile_make(ppcp_capture_profile *out, const char *id,
                                      const ppcp_timing *timing)
{
    ppcp_result rc;
    if (out == NULL || timing == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_timing_validate(timing);
    if (rc != PPCP_OK)
        return rc;
    memset(out, 0, sizeof(*out));
    rc = ppcp_id_set_z(&out->id, id);
    if (rc != PPCP_OK)
        return rc;
    out->timing = *timing;
    return PPCP_OK;
}

ppcp_result ppcp_capture_profile_set_camera(ppcp_capture_profile *p,
                                            const ppcp_geometry *geometry,
                                            ppcp_intrinsics_mode intrinsics)
{
    ppcp_result rc;
    if (p == NULL || geometry == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_enum_to_text(intrinsics_map, (int)intrinsics) == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_geometry_validate(geometry);
    if (rc != PPCP_OK)
        return rc;
    p->has_geometry   = true;
    p->geometry       = *geometry;
    p->has_intrinsics = true;
    p->intrinsics     = intrinsics;
    return PPCP_OK;
}

ppcp_result ppcp_capture_profile_set_format(ppcp_capture_profile *p, const char *codec,
                                            uint32_t width, uint32_t height,
                                            const char *pixel_format)
{
    ppcp_result rc;
    if (p == NULL || width == 0 || height == 0)
        return PPCP_ERR_INVALID;
    rc = ppcp_id_set_z(&p->format.codec, codec);
    if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&p->format.pixel_format, pixel_format);
    if (rc != PPCP_OK) return rc;
    p->format.width   = width;
    p->format.height  = height;
    p->format.present = true;
    return PPCP_OK;
}

ppcp_result ppcp_capture_profile_set_rate(ppcp_capture_profile *p, int64_t nominal_mhz,
                                          int64_t min_mhz, int64_t max_mhz)
{
    if (p == NULL)
        return PPCP_ERR_INVALID;
    if (nominal_mhz <= 0 || min_mhz <= 0 || max_mhz < min_mhz ||
        nominal_mhz < min_mhz || nominal_mhz > max_mhz)
        return PPCP_ERR_INVALID;
    p->rate.present     = true;
    p->rate.nominal_mhz = nominal_mhz;
    p->rate.min_mhz     = min_mhz;
    p->rate.max_mhz     = max_mhz;
    return PPCP_OK;
}

ppcp_result ppcp_capture_profile_set_optical(ppcp_capture_profile *p, int64_t exposure_min_ns,
                                             int64_t exposure_max_ns, int64_t iso_min,
                                             int64_t iso_max)
{
    if (p == NULL)
        return PPCP_ERR_INVALID;
    if (exposure_min_ns < 0 || exposure_max_ns < exposure_min_ns ||
        iso_min < 0 || iso_max < iso_min)
        return PPCP_ERR_INVALID;
    p->optical.present         = true;
    p->optical.exposure_min_ns = exposure_min_ns;
    p->optical.exposure_max_ns = exposure_max_ns;
    p->optical.iso_min         = iso_min;
    p->optical.iso_max         = iso_max;
    return PPCP_OK;
}

ppcp_result ppcp_capture_profile_set_measured(ppcp_capture_profile *p,
                                              const ppcp_measured_capability *m)
{
    ppcp_result rc;
    if (p == NULL || m == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_measured_capability_validate(m);
    if (rc != PPCP_OK)
        return rc;
    /* 5.7c: results attach per profile — 1080p240 and 1080p120 are separate
     * self-tests with separate results, which is why this is a profile setter
     * and there is nothing like it on the Source. */
    p->has_measured = true;
    p->measured     = *m;
    return PPCP_OK;
}

ppcp_result ppcp_capture_profile_validate(const ppcp_capture_profile *p)
{
    ppcp_result rc;
    if (p == NULL || !ppcp_id_is_set(&p->id))
        return PPCP_ERR_INVALID;
    rc = ppcp_timing_validate(&p->timing);      /* I22 lives here */
    if (rc != PPCP_OK)
        return rc;
    if (p->has_geometry) {
        rc = ppcp_geometry_validate(&p->geometry);   /* I31 lives here */
        if (rc != PPCP_OK)
            return rc;
    }
    if (p->has_intrinsics && ppcp_enum_to_text(intrinsics_map, (int)p->intrinsics) == NULL)
        return PPCP_ERR_INVALID;
    if (p->has_measured) {
        rc = ppcp_measured_capability_validate(&p->measured);
        if (rc != PPCP_OK)
            return rc;
    }
    if (p->rate.present && p->rate.nominal_mhz <= 0)
        return PPCP_ERR_INVALID;
    /* 5.7d / I14: no frame-rate, resolution, quality or confidence THRESHOLD
     * appears here.  `rate` and `format` are declarations of what the mode is,
     * never of what is acceptable — acceptance is the consumer's policy. */
    return PPCP_OK;
}

static ppcp_result format_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_format *f = (const ppcp_format *)ctx;
    ppcp_wfield fl[4];
    fl[0] = ppcp_wf_id("codec", &f->codec);
    fl[1] = ppcp_wf_uint("width", f->width);
    fl[2] = ppcp_wf_uint("height", f->height);
    fl[3] = ppcp_wf_id("pixel_format", &f->pixel_format);
    return ppcp_rec_write(w, fl, 4);
}

static ppcp_result format_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_format *f = (ppcp_format *)dst;
    ppcp_rfield  fl[4];
    uint64_t     wdt = 0, hgt = 0;
    bool         a = false, b = false, c = false, d = false;
    ppcp_result  rc;
    (void)ctx;
    fl[0] = ppcp_rf("codec", PPCP_F_ID, &f->codec, &a);
    fl[1] = ppcp_rf("width", PPCP_F_UINT, &wdt, &b);
    fl[2] = ppcp_rf("height", PPCP_F_UINT, &hgt, &c);
    fl[3] = ppcp_rf("pixel_format", PPCP_F_ID, &f->pixel_format, &d);
    rc = ppcp_rec_read(r, fl, 4);
    if (rc != PPCP_OK) return rc;
    if (!a || !b || !c || !d) return PPCP_ERR_MALFORMED;
    if (wdt == 0 || hgt == 0 || wdt > 0xFFFFFFFFu || hgt > 0xFFFFFFFFu)
        return PPCP_ERR_MALFORMED;
    f->width   = (uint32_t)wdt;
    f->height  = (uint32_t)hgt;
    f->present = true;
    return PPCP_OK;
}

static ppcp_result rate_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_rate *r = (const ppcp_rate *)ctx;
    ppcp_wfield f[3];
    f[0] = ppcp_wf_int("nominal_mhz", r->nominal_mhz);
    f[1] = ppcp_wf_int("min_mhz", r->min_mhz);
    f[2] = ppcp_wf_int("max_mhz", r->max_mhz);
    return ppcp_rec_write(w, f, 3);
}

static ppcp_result rate_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_rate  *v = (ppcp_rate *)dst;
    ppcp_rfield f[3];
    bool        a = false, b = false, c = false;
    ppcp_result rc;
    (void)ctx;
    f[0] = ppcp_rf("nominal_mhz", PPCP_F_INT, &v->nominal_mhz, &a);
    f[1] = ppcp_rf("min_mhz", PPCP_F_INT, &v->min_mhz, &b);
    f[2] = ppcp_rf("max_mhz", PPCP_F_INT, &v->max_mhz, &c);
    rc = ppcp_rec_read(r, f, 3);
    if (rc != PPCP_OK) return rc;
    if (!a || !b || !c) return PPCP_ERR_MALFORMED;
    v->present = true;
    return PPCP_OK;
}

static ppcp_result optical_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_optical *o = (const ppcp_optical *)ctx;
    ppcp_wfield f[5];
    size_t      n = 0;
    f[n++] = ppcp_wf_int("exposure_min_ns", o->exposure_min_ns);
    f[n++] = ppcp_wf_int("exposure_max_ns", o->exposure_max_ns);
    f[n++] = ppcp_wf_int("iso_min", o->iso_min);
    f[n++] = ppcp_wf_int("iso_max", o->iso_max);
    if (o->has_noise_figure)        /* absent means not measured (CORE 5.7) */
        f[n++] = ppcp_wf_double("noise_figure", o->noise_figure);
    return ppcp_rec_write(w, f, n);
}

static ppcp_result optical_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_optical *o = (ppcp_optical *)dst;
    ppcp_rfield   f[5];
    bool          a = false, b = false, c = false, d = false;
    ppcp_result   rc;
    (void)ctx;
    f[0] = ppcp_rf("exposure_min_ns", PPCP_F_INT, &o->exposure_min_ns, &a);
    f[1] = ppcp_rf("exposure_max_ns", PPCP_F_INT, &o->exposure_max_ns, &b);
    f[2] = ppcp_rf("iso_min", PPCP_F_INT, &o->iso_min, &c);
    f[3] = ppcp_rf("iso_max", PPCP_F_INT, &o->iso_max, &d);
    f[4] = ppcp_rf("noise_figure", PPCP_F_DOUBLE, &o->noise_figure, &o->has_noise_figure);
    rc = ppcp_rec_read(r, f, 5);
    if (rc != PPCP_OK) return rc;
    if (!a || !b || !c || !d) return PPCP_ERR_MALFORMED;
    o->present = true;
    return PPCP_OK;
}

static ppcp_result measured_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    (void)ctx;
    return ppcp_measured_capability_decode(r, (ppcp_measured_capability *)dst);
}

static ppcp_result measured_write(ppcp_cbor_writer *w, const void *ctx)
{
    return ppcp_measured_capability_encode(w, (const ppcp_measured_capability *)ctx);
}

ppcp_result ppcp_capture_profile_encode(ppcp_cbor_writer *w, const ppcp_capture_profile *p)
{
    ppcp_wfield f[8];
    size_t      n  = 0;
    ppcp_result rc = ppcp_capture_profile_validate(p);
    if (rc != PPCP_OK)
        return rc;
    f[n++] = ppcp_wf_id("id", &p->id);
    f[n++] = ppcp_wf_sub("timing", timing_write, &p->timing);
    if (p->format.present)
        f[n++] = ppcp_wf_sub("format", format_write, &p->format);
    if (p->rate.present)
        f[n++] = ppcp_wf_sub("rate", rate_write, &p->rate);
    if (p->optical.present)
        f[n++] = ppcp_wf_sub("optical", optical_write, &p->optical);
    if (p->has_geometry)
        f[n++] = ppcp_wf_sub("geometry", geometry_write, &p->geometry);
    if (p->has_intrinsics)
        f[n++] = ppcp_wf_enum("intrinsics", intrinsics_map, (int)p->intrinsics);
    /* I28: `measured` is written if and only if a self-test produced one.
     * There is no branch here that synthesises it from the fields above. */
    if (p->has_measured)
        f[n++] = ppcp_wf_sub("measured", measured_write, &p->measured);
    return ppcp_rec_write(w, f, n);
}

ppcp_result ppcp_capture_profile_decode(ppcp_cbor_reader *r, ppcp_capture_profile *out)
{
    ppcp_rfield f[8];
    size_t      n = 0;
    bool        s_id = false, s_timing = false;
    int         intr = 0;
    ppcp_result rc;

    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    f[n++] = ppcp_rf("id", PPCP_F_ID, &out->id, &s_id);
    f[n++] = ppcp_rf_sub("timing", timing_read, &out->timing, NULL, &s_timing);
    f[n++] = ppcp_rf_sub("format", format_read, &out->format, NULL, NULL);
    f[n++] = ppcp_rf_sub("rate", rate_read, &out->rate, NULL, NULL);
    f[n++] = ppcp_rf_sub("optical", optical_read, &out->optical, NULL, NULL);
    f[n++] = ppcp_rf_sub("geometry", geometry_read, &out->geometry, NULL, &out->has_geometry);
    f[n++] = ppcp_rf_enum("intrinsics", intrinsics_map, &intr, &out->has_intrinsics);
    f[n++] = ppcp_rf_sub("measured", measured_read, &out->measured, NULL, &out->has_measured);

    rc = ppcp_rec_read(r, f, n);
    if (rc != PPCP_OK)
        return rc;
    if (!s_id || !s_timing)
        return PPCP_ERR_MALFORMED;
    out->intrinsics = (ppcp_intrinsics_mode)intr;
    if (ppcp_capture_profile_validate(out) != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

/* ----------------------------------------------------------- Calibration */

ppcp_result ppcp_calibration_make(ppcp_calibration *out, const char *id, const char *source_id,
                                  const char *kind, const uint8_t *parameters,
                                  size_t parameters_len, const uint8_t *uncertainty,
                                  size_t uncertainty_len, ppcp_calibration_method method,
                                  const ppcp_instant *observed_at)
{
    ppcp_result rc;
    if (out == NULL || observed_at == NULL)
        return PPCP_ERR_INVALID;
    /* 5.9: `uncertainty` is MANDATORY.  It is a parameter and not a setter for
     * the same reason a TimebaseRelation's sigmas are (5.4a): a calibration
     * with no dispersion is a point estimate wearing a measurement's clothes. */
    if (parameters == NULL || parameters_len == 0 ||
        uncertainty == NULL || uncertainty_len == 0)
        return PPCP_ERR_INVALID;
    if (ppcp_enum_to_text(calibration_method_map, (int)method) == NULL)
        return PPCP_ERR_INVALID;
    if (ppcp_instant_validate(observed_at) != PPCP_OK)
        return PPCP_ERR_INVALID;

    memset(out, 0, sizeof(*out));
    rc = ppcp_id_set_z(&out->id, id);              if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->source_id, source_id); if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->kind, kind);           if (rc != PPCP_OK) return rc;
    out->parameters      = parameters;
    out->parameters_len  = parameters_len;
    out->uncertainty     = uncertainty;
    out->uncertainty_len = uncertainty_len;
    out->method          = method;
    out->observed_at     = *observed_at;
    return PPCP_OK;
}

ppcp_result ppcp_calibration_validate(const ppcp_calibration *c)
{
    if (c == NULL)
        return PPCP_ERR_INVALID;
    if (!ppcp_id_is_set(&c->id) || !ppcp_id_is_set(&c->source_id) || !ppcp_id_is_set(&c->kind))
        return PPCP_ERR_INVALID;
    if (c->parameters == NULL || c->parameters_len == 0)
        return PPCP_ERR_INVALID;
    if (c->uncertainty == NULL || c->uncertainty_len == 0)
        return PPCP_ERR_INVALID;
    if (ppcp_enum_to_text(calibration_method_map, (int)c->method) == NULL)
        return PPCP_ERR_INVALID;
    return ppcp_instant_validate(&c->observed_at);
}

/* The kind-specific maps are carried as already-encoded CBOR, so a kind this
 * library does not know round-trips byte-identically. */
static ppcp_result raw_cbor_write(ppcp_cbor_writer *w, const uint8_t *p, size_t n)
{
    ppcp_cbor_reader rd;
    ppcp_cbor_item   it;
    ppcp_result      rc;
    uint32_t         i;
    ppcp_cbor_limits lim = ppcp_cbor_limits_for_channel(PPCP_CHANNEL_BULK);

    /* Re-emitted through the writer rather than memcpy'd, so a foreign map
     * cannot smuggle a tag, an indefinite length or a duplicate key past
     * ENC 4d on its way back out. */
    ppcp_cbor_reader_init(&rd, p, n, lim);
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
        case PPCP_CBOR_UINT: case PPCP_CBOR_NINT:
            rc = ppcp_cbor_write_int(w, v.i); break;
        case PPCP_CBOR_DOUBLE: rc = ppcp_cbor_write_double(w, v.f); break;
        case PPCP_CBOR_BOOL:   rc = ppcp_cbor_write_bool(w, v.b); break;
        case PPCP_CBOR_TEXT:   rc = ppcp_cbor_write_text(w, (const char *)v.bytes, v.len); break;
        case PPCP_CBOR_BYTES:  rc = ppcp_cbor_write_bytes(w, v.bytes, v.len); break;
        default:
            /* Nested containers in a kind-specific map are legal CBOR but this
             * library has no shape for them; refusing is honest. */
            return PPCP_ERR_INVALID;
        }
        if (rc != PPCP_OK) return rc;
    }
    return ppcp_cbor_writer_status(w);
}

typedef struct raw_ref { const uint8_t *p; size_t n; } raw_ref;

static ppcp_result raw_sub_write(ppcp_cbor_writer *w, const void *ctx)
{
    const raw_ref *r = (const raw_ref *)ctx;
    return raw_cbor_write(w, r->p, r->n);
}

static ppcp_result raw_sub_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    raw_ref       *out = (raw_ref *)dst;
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

ppcp_result ppcp_calibration_encode(ppcp_cbor_writer *w, const ppcp_calibration *c)
{
    ppcp_wfield f[6];
    raw_ref     par, unc;
    ppcp_result rc = ppcp_calibration_validate(c);
    if (rc != PPCP_OK)
        return rc;
    par.p = c->parameters;  par.n = c->parameters_len;
    unc.p = c->uncertainty; unc.n = c->uncertainty_len;
    f[0] = ppcp_wf_id("id", &c->id);
    f[1] = ppcp_wf_id("source_id", &c->source_id);
    f[2] = ppcp_wf_id("kind", &c->kind);
    f[3] = ppcp_wf_sub("parameters", raw_sub_write, &par);
    f[4] = ppcp_wf_sub("uncertainty", raw_sub_write, &unc);
    f[5] = ppcp_wf_enum("method", calibration_method_map, (int)c->method);
    /* observed_at is a sixth mandatory field; written through the sub writer. */
    {
        ppcp_wfield g[7];
        size_t i;
        for (i = 0; i < 6; i++) g[i] = f[i];
        g[6] = ppcp_wf_sub("observed_at", ppcp_sub_write_instant, &c->observed_at);
        return ppcp_rec_write(w, g, 7);
    }
}

ppcp_result ppcp_calibration_decode(ppcp_cbor_reader *r, ppcp_calibration *out)
{
    ppcp_rfield f[7];
    raw_ref     par, unc;
    bool        s_id = false, s_src = false, s_kind = false, s_par = false;
    bool        s_unc = false, s_method = false, s_obs = false;
    int         method = 0;
    ppcp_result rc;

    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    memset(&par, 0, sizeof(par));
    memset(&unc, 0, sizeof(unc));

    f[0] = ppcp_rf("id", PPCP_F_ID, &out->id, &s_id);
    f[1] = ppcp_rf("source_id", PPCP_F_ID, &out->source_id, &s_src);
    f[2] = ppcp_rf("kind", PPCP_F_ID, &out->kind, &s_kind);
    f[3] = ppcp_rf_sub("parameters", raw_sub_read, &par, NULL, &s_par);
    f[4] = ppcp_rf_sub("uncertainty", raw_sub_read, &unc, NULL, &s_unc);
    f[5] = ppcp_rf_enum("method", calibration_method_map, &method, &s_method);
    f[6] = ppcp_rf_sub("observed_at", ppcp_sub_read_instant, &out->observed_at, NULL, &s_obs);

    rc = ppcp_rec_read(r, f, 7);
    if (rc != PPCP_OK)
        return rc;
    if (!s_id || !s_src || !s_kind || !s_par || !s_unc || !s_method || !s_obs)
        return PPCP_ERR_MALFORMED;
    out->parameters      = par.p;
    out->parameters_len  = par.n;
    out->uncertainty     = unc.p;
    out->uncertainty_len = unc.n;
    out->method          = (ppcp_calibration_method)method;
    if (ppcp_calibration_validate(out) != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

/* ---------------------------------------------------------------- Source */

ppcp_result ppcp_source_make(ppcp_source *out, const char *id, const char *peer_id,
                             const char *kind, const char *timebase_id, bool physical,
                             const ppcp_capture_profile *profiles, size_t profile_count)
{
    ppcp_result rc;
    if (out == NULL || profiles == NULL || profile_count == 0)
        return PPCP_ERR_INVALID;   /* CORE 5.6: `profiles` is 1..n */
    memset(out, 0, sizeof(*out));
    rc = ppcp_id_set_z(&out->id, id);                  if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->peer_id, peer_id);        if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->kind, kind);              if (rc != PPCP_OK) return rc;
    /* 5.6a: every Source declares `timebase_id`, and I4 makes two Sources on
     * one clock share the id rather than assert identity by relation. */
    rc = ppcp_id_set_z(&out->timebase_id, timebase_id); if (rc != PPCP_OK) return rc;
    out->physical      = physical;
    out->profiles      = profiles;
    out->profile_count = profile_count;
    return PPCP_OK;
}

ppcp_result ppcp_source_set_calibration(ppcp_source *s, const ppcp_calibration *c)
{
    ppcp_result rc;
    if (s == NULL || c == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_calibration_validate(c);
    if (rc != PPCP_OK)
        return rc;
    if (!ppcp_id_equal(&c->source_id, &s->id))
        return PPCP_ERR_INVALID;
    s->has_calibration = true;
    s->calibration     = *c;
    return PPCP_OK;
}

ppcp_result ppcp_source_set_optics(ppcp_source *s, const char *optics)
{
    ppcp_result rc;
    if (s == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_id_set_z(&s->optics, optics);
    if (rc != PPCP_OK)
        return rc;
    s->has_optics = true;
    return PPCP_OK;
}

ppcp_result ppcp_source_set_label(ppcp_source *s, const char *label)
{
    ppcp_result rc;
    if (s == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_id_set_z(&s->label, label);
    if (rc != PPCP_OK)
        return rc;
    s->has_label = true;
    return PPCP_OK;
}

ppcp_result ppcp_source_set_viewpoint_declared(ppcp_source *s, const char *label)
{
    ppcp_result rc;
    if (s == NULL)
        return PPCP_ERR_INVALID;
    memset(&s->viewpoint, 0, sizeof(s->viewpoint));
    rc = ppcp_id_set_z(&s->viewpoint.label, label);
    if (rc != PPCP_OK)
        return rc;
    /* 5.6e: a person who states "down the line" is not expressing a
     * probability, so there is no confidence parameter on this constructor and
     * no setter that would add one. */
    s->viewpoint.present = true;
    s->viewpoint.method  = PPCP_VP_DECLARED;
    return PPCP_OK;
}

ppcp_result ppcp_source_set_viewpoint_classified(ppcp_source *s, const char *label,
                                                 double confidence)
{
    ppcp_result rc;
    if (s == NULL)
        return PPCP_ERR_INVALID;
    if (!(confidence >= 0.0) || !(confidence <= 1.0))
        return PPCP_ERR_INVALID;
    memset(&s->viewpoint, 0, sizeof(s->viewpoint));
    rc = ppcp_id_set_z(&s->viewpoint.label, label);
    if (rc != PPCP_OK)
        return rc;
    s->viewpoint.present        = true;
    s->viewpoint.method         = PPCP_VP_CLASSIFIED;
    s->viewpoint.has_confidence = true;
    s->viewpoint.confidence     = confidence;
    return PPCP_OK;
}

bool ppcp_source_kind_is_camera(const ppcp_source *s)
{
    if (s == NULL)
        return false;
    return ppcp_cbor_key_is(s->kind.v, s->kind.len, "camera");
}

ppcp_result ppcp_source_validate(const ppcp_source *s)
{
    size_t i;
    if (s == NULL)
        return PPCP_ERR_INVALID;
    if (!ppcp_id_is_set(&s->id) || !ppcp_id_is_set(&s->peer_id) ||
        !ppcp_id_is_set(&s->kind) || !ppcp_id_is_set(&s->timebase_id))
        return PPCP_ERR_INVALID;
    if (s->profiles == NULL || s->profile_count == 0)
        return PPCP_ERR_INVALID;
    for (i = 0; i < s->profile_count; i++) {
        ppcp_result rc = ppcp_capture_profile_validate(&s->profiles[i]);
        if (rc != PPCP_OK)
            return rc;
        /* I19 / 5.6a: a camera Source's every profile declares `timing`,
         * `geometry` AND `intrinsics`, whichever peer owns it.  `timing` is
         * unconditional and already checked; these two are the ones a host
         * implementer omits. */
        if (ppcp_source_kind_is_camera(s)) {
            if (!s->profiles[i].has_geometry || !s->profiles[i].has_intrinsics)
                return PPCP_ERR_INVALID;
        }
    }
    if (s->viewpoint.present) {
        if (!ppcp_id_is_set(&s->viewpoint.label))
            return PPCP_ERR_INVALID;
        /* 5.6e — the iff, on the way in as well as the way out. */
        if (s->viewpoint.method == PPCP_VP_CLASSIFIED) {
            if (!s->viewpoint.has_confidence)
                return PPCP_ERR_INVALID;
            if (!(s->viewpoint.confidence >= 0.0) || !(s->viewpoint.confidence <= 1.0))
                return PPCP_ERR_INVALID;
        } else if (s->viewpoint.has_confidence) {
            return PPCP_ERR_INVALID;
        }
    }
    if (s->has_calibration)
        return ppcp_calibration_validate(&s->calibration);
    return PPCP_OK;
}

static ppcp_result profile_elem_write(ppcp_cbor_writer *w, const void *elem)
{
    return ppcp_capture_profile_encode(w, (const ppcp_capture_profile *)elem);
}

static ppcp_result profiles_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_source *s = (const ppcp_source *)ctx;
    return ppcp_rec_write_array(w, s->profiles, sizeof(ppcp_capture_profile),
                                s->profile_count, profile_elem_write);
}

static ppcp_result viewpoint_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_viewpoint *v = (const ppcp_viewpoint *)ctx;
    ppcp_wfield f[3];
    size_t      n = 0;
    f[n++] = ppcp_wf_id("label", &v->label);
    f[n++] = ppcp_wf_enum("method", viewpoint_method_map, (int)v->method);
    if (v->has_confidence)
        f[n++] = ppcp_wf_double("confidence", v->confidence);
    return ppcp_rec_write(w, f, n);
}

static ppcp_result viewpoint_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_viewpoint *v = (ppcp_viewpoint *)dst;
    ppcp_rfield     f[3];
    bool            s_label = false, s_method = false;
    int             method = 0;
    ppcp_result     rc;
    (void)ctx;
    f[0] = ppcp_rf("label", PPCP_F_ID, &v->label, &s_label);
    f[1] = ppcp_rf_enum("method", viewpoint_method_map, &method, &s_method);
    f[2] = ppcp_rf("confidence", PPCP_F_DOUBLE, &v->confidence, &v->has_confidence);
    rc = ppcp_rec_read(r, f, 3);
    if (rc != PPCP_OK) return rc;
    if (!s_label || !s_method) return PPCP_ERR_MALFORMED;
    v->method  = (ppcp_viewpoint_method)method;
    v->present = true;
    /* 5.6e both ways: classified without a confidence, and declared with one,
     * are each malformed. */
    if (v->method == PPCP_VP_CLASSIFIED) {
        if (!v->has_confidence) return PPCP_ERR_MALFORMED;
    } else if (v->has_confidence) {
        return PPCP_ERR_MALFORMED;
    }
    return PPCP_OK;
}

static ppcp_result calibration_sub_write(ppcp_cbor_writer *w, const void *ctx)
{
    return ppcp_calibration_encode(w, (const ppcp_calibration *)ctx);
}

static ppcp_result calibration_sub_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    (void)ctx;
    return ppcp_calibration_decode(r, (ppcp_calibration *)dst);
}

ppcp_result ppcp_source_encode(ppcp_cbor_writer *w, const ppcp_source *s)
{
    ppcp_wfield f[10];
    size_t      n  = 0;
    ppcp_result rc = ppcp_source_validate(s);
    if (rc != PPCP_OK)
        return rc;
    f[n++] = ppcp_wf_id("id", &s->id);
    f[n++] = ppcp_wf_id("peer_id", &s->peer_id);
    f[n++] = ppcp_wf_id("kind", &s->kind);
    f[n++] = ppcp_wf_id("timebase_id", &s->timebase_id);
    f[n++] = ppcp_wf_bool("physical", s->physical);
    f[n++] = ppcp_wf_sub("profiles", profiles_write, s);
    if (s->has_calibration)
        f[n++] = ppcp_wf_sub("calibration", calibration_sub_write, &s->calibration);
    if (s->has_optics)
        f[n++] = ppcp_wf_id("optics", &s->optics);
    if (s->viewpoint.present)
        f[n++] = ppcp_wf_sub("viewpoint", viewpoint_write, &s->viewpoint);
    if (s->has_label)
        f[n++] = ppcp_wf_id("label", &s->label);
    return ppcp_rec_write(w, f, n);
}

static ppcp_result profile_elem_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    (void)ctx;
    return ppcp_capture_profile_decode(r, (ppcp_capture_profile *)dst);
}

typedef struct src_read_ctx {
    ppcp_arena  *arena;
    ppcp_source *out;
} src_read_ctx;

static ppcp_result profiles_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    src_read_ctx *c = (src_read_ctx *)ctx;
    void   *base = NULL;
    size_t  count = 0;
    ppcp_result rc;
    (void)dst;
    rc = ppcp_rec_read_array_arena(r, c->arena, sizeof(ppcp_capture_profile),
                                   sizeof(void *), &base, &count, profile_elem_read, NULL);
    if (rc != PPCP_OK)
        return rc;
    c->out->profiles      = (const ppcp_capture_profile *)base;
    c->out->profile_count = count;
    return PPCP_OK;
}

ppcp_result ppcp_source_decode(ppcp_cbor_reader *r, ppcp_arena *a, ppcp_source *out)
{
    ppcp_rfield  f[10];
    size_t       n = 0;
    src_read_ctx ctx;
    bool         s_id = false, s_peer = false, s_kind = false, s_tb = false;
    bool         s_phys = false, s_prof = false;
    ppcp_result  rc;

    if (out == NULL)
        return PPCP_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    ctx.arena = a;
    ctx.out   = out;

    f[n++] = ppcp_rf("id", PPCP_F_ID, &out->id, &s_id);
    f[n++] = ppcp_rf("peer_id", PPCP_F_ID, &out->peer_id, &s_peer);
    f[n++] = ppcp_rf("kind", PPCP_F_ID, &out->kind, &s_kind);
    f[n++] = ppcp_rf("timebase_id", PPCP_F_ID, &out->timebase_id, &s_tb);
    f[n++] = ppcp_rf("physical", PPCP_F_BOOL, &out->physical, &s_phys);
    f[n++] = ppcp_rf_sub("profiles", profiles_read, NULL, &ctx, &s_prof);
    f[n++] = ppcp_rf_sub("calibration", calibration_sub_read, &out->calibration, NULL,
                         &out->has_calibration);
    f[n++] = ppcp_rf("optics", PPCP_F_ID, &out->optics, &out->has_optics);
    f[n++] = ppcp_rf_sub("viewpoint", viewpoint_read, &out->viewpoint, NULL, NULL);
    f[n++] = ppcp_rf("label", PPCP_F_ID, &out->label, &out->has_label);

    rc = ppcp_rec_read(r, f, n);
    if (rc != PPCP_OK)
        return rc;
    if (!s_id || !s_peer || !s_kind || !s_tb || !s_phys || !s_prof)
        return PPCP_ERR_MALFORMED;
    if (ppcp_source_validate(out) != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}

/* ------------------------------------------------------------------ Peer */

ppcp_result ppcp_peer_desc_make(ppcp_peer_desc *out, const char *id, ppcp_role role,
                                const char *protocol_version, const ppcp_id *profiles,
                                size_t profile_count, const ppcp_timebase *timebases,
                                size_t timebase_count)
{
    ppcp_result rc;
    size_t      i;
    bool        has_core = false;

    if (out == NULL || profiles == NULL || profile_count == 0)
        return PPCP_ERR_INVALID;
    if (timebases == NULL || timebase_count == 0)
        return PPCP_ERR_INVALID;   /* CORE 5.2: `timebases` is 1..n */
    if (ppcp_enum_to_text(role_map, (int)role) == NULL)
        return PPCP_ERR_INVALID;
    for (i = 0; i < profile_count; i++) {
        if (ppcp_cbor_key_is(profiles[i].v, profiles[i].len, PPCP_PROFILE_CORE))
            has_core = true;
    }
    if (!has_core)
        return PPCP_ERR_INVALID;   /* CORE 5.2: `profiles` MUST include core */

    memset(out, 0, sizeof(*out));
    rc = ppcp_id_set_z(&out->id, id);
    if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&out->protocol_version, protocol_version);
    if (rc != PPCP_OK) return rc;
    out->role           = role;
    out->profiles       = profiles;
    out->profile_count  = profile_count;
    out->timebases      = timebases;
    out->timebase_count = timebase_count;
    return PPCP_OK;
}

ppcp_result ppcp_peer_desc_set_sources(ppcp_peer_desc *p, const ppcp_source *sources,
                                       size_t count)
{
    if (p == NULL || (count > 0 && sources == NULL))
        return PPCP_ERR_INVALID;
    /* 3.3d: a host owning no Sources sets an EMPTY list — it does not skip the
     * field.  So zero is legal and is not the same as never having called. */
    p->sources      = sources;
    p->source_count = count;
    return PPCP_OK;
}

ppcp_result ppcp_peer_desc_set_relations(ppcp_peer_desc *p, const ppcp_timebase_relation *rel,
                                         size_t count)
{
    if (p == NULL || (count > 0 && rel == NULL))
        return PPCP_ERR_INVALID;
    p->relations      = rel;
    p->relation_count = count;
    return PPCP_OK;
}

ppcp_result ppcp_peer_desc_set_extensions(ppcp_peer_desc *p, const ppcp_id *ext, size_t count)
{
    if (p == NULL || (count > 0 && ext == NULL))
        return PPCP_ERR_INVALID;
    p->extensions      = ext;
    p->extension_count = count;
    return PPCP_OK;
}

ppcp_result ppcp_peer_desc_set_product(ppcp_peer_desc *p, const char *vendor,
                                       const char *model, const char *version)
{
    ppcp_result rc;
    if (p == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_id_set_z(&p->product.vendor, vendor);   if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&p->product.model, model);     if (rc != PPCP_OK) return rc;
    rc = ppcp_id_set_z(&p->product.version, version); if (rc != PPCP_OK) return rc;
    /* 5.2c: informational only.  Nothing in this library reads these three
     * fields again — everything the protocol requires is declared (I19). */
    p->product.present = true;
    return PPCP_OK;
}

bool ppcp_peer_desc_has_profile(const ppcp_peer_desc *p, const char *profile)
{
    size_t i;
    if (p == NULL || profile == NULL)
        return false;
    for (i = 0; i < p->profile_count; i++) {
        if (ppcp_cbor_key_is(p->profiles[i].v, p->profiles[i].len, profile))
            return true;
    }
    return false;
}

ppcp_result ppcp_peer_desc_validate(const ppcp_peer_desc *p)
{
    size_t i, j;
    if (p == NULL || !ppcp_id_is_set(&p->id) || !ppcp_id_is_set(&p->protocol_version))
        return PPCP_ERR_INVALID;
    if (ppcp_enum_to_text(role_map, (int)p->role) == NULL)
        return PPCP_ERR_INVALID;
    if (p->profiles == NULL || p->profile_count == 0)
        return PPCP_ERR_INVALID;
    if (!ppcp_peer_desc_has_profile(p, PPCP_PROFILE_CORE))
        return PPCP_ERR_INVALID;
    if (p->timebases == NULL || p->timebase_count == 0)
        return PPCP_ERR_INVALID;
    for (i = 0; i < p->timebase_count; i++) {
        ppcp_result rc = ppcp_timebase_validate(&p->timebases[i]);
        if (rc != PPCP_OK)
            return rc;
    }
    for (i = 0; i < p->relation_count; i++) {
        ppcp_result rc = ppcp_relation_validate(&p->relations[i]);
        if (rc != PPCP_OK)
            return rc;
    }
    for (i = 0; i < p->source_count; i++) {
        ppcp_result rc = ppcp_source_validate(&p->sources[i]);
        if (rc != PPCP_OK)
            return rc;
        if (!ppcp_id_equal(&p->sources[i].peer_id, &p->id))
            return PPCP_ERR_INVALID;
        /* MSG 3.3b: every `timebase_id` a Source references appears in
         * `timebases`.  5.3a says the same thing one layer up, and this is the
         * check that makes an Android UNKNOWN device's missing relation a
         * detectable error rather than a silent assumption of zero. */
        for (j = 0; j < p->timebase_count; j++) {
            if (ppcp_id_equal(&p->sources[i].timebase_id, &p->timebases[j].id))
                break;
        }
        if (j == p->timebase_count)
            return PPCP_ERR_INVALID;
    }
    return PPCP_OK;
}

static ppcp_result id_list_write(ppcp_cbor_writer *w, const ppcp_id *v, size_t n)
{
    return ppcp_rec_write_array(w, v, sizeof(ppcp_id), n, ppcp_elem_write_id);
}

typedef struct idlist_ref { const ppcp_id *v; size_t n; } idlist_ref;

static ppcp_result idlist_sub_write(ppcp_cbor_writer *w, const void *ctx)
{
    const idlist_ref *r = (const idlist_ref *)ctx;
    return id_list_write(w, r->v, r->n);
}

ppcp_result ppcp_product_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_product *p = (const ppcp_product *)ctx;
    ppcp_wfield f[3];
    f[0] = ppcp_wf_id("vendor", &p->vendor);
    f[1] = ppcp_wf_id("model", &p->model);
    f[2] = ppcp_wf_id("version", &p->version);
    return ppcp_rec_write(w, f, 3);
}

ppcp_result ppcp_product_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_product *p = (ppcp_product *)dst;
    ppcp_rfield   f[3];
    ppcp_result   rc;
    (void)ctx;
    f[0] = ppcp_rf("vendor", PPCP_F_ID, &p->vendor, NULL);
    f[1] = ppcp_rf("model", PPCP_F_ID, &p->model, NULL);
    f[2] = ppcp_rf("version", PPCP_F_ID, &p->version, NULL);
    rc = ppcp_rec_read(r, f, 3);
    if (rc != PPCP_OK) return rc;
    p->present = true;
    return PPCP_OK;
}

static ppcp_result protocol_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_peer_desc *p = (const ppcp_peer_desc *)ctx;
    idlist_ref  ext;
    ppcp_wfield f[2];
    ext.v = p->extensions;
    ext.n = p->extension_count;
    f[0] = ppcp_wf_id("version", &p->protocol_version);
    f[1] = ppcp_wf_sub("extensions", idlist_sub_write, &ext);
    return ppcp_rec_write(w, f, 2);
}

typedef struct peer_read_ctx {
    ppcp_arena     *arena;
    ppcp_peer_desc *out;
} peer_read_ctx;

static ppcp_result protocol_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    peer_read_ctx *c = (peer_read_ctx *)ctx;
    ppcp_cbor_item it;
    ppcp_result    rc;
    uint32_t       i;
    (void)dst;

    rc = ppcp_cbor_read(r, &it);
    if (rc != PPCP_OK) return rc;
    if (it.type != PPCP_CBOR_MAP) return PPCP_ERR_MALFORMED;
    for (i = 0; i < it.count; i++) {
        const char *k; size_t klen;
        rc = ppcp_cbor_read_key(r, &k, &klen);
        if (rc != PPCP_OK) return rc;
        if (ppcp_cbor_key_is(k, klen, "version")) {
            ppcp_cbor_item v;
            rc = ppcp_cbor_read(r, &v);
            if (rc != PPCP_OK) return rc;
            if (v.type != PPCP_CBOR_TEXT) return PPCP_ERR_MALFORMED;
            if (ppcp_id_set(&c->out->protocol_version, (const char *)v.bytes, v.len) != PPCP_OK)
                return PPCP_ERR_MALFORMED;
        } else if (ppcp_cbor_key_is(k, klen, "extensions")) {
            void *base = NULL; size_t count = 0;
            rc = ppcp_rec_read_array_arena(r, c->arena, sizeof(ppcp_id), sizeof(void *),
                                           &base, &count, ppcp_sub_read_id_elem, NULL);
            if (rc != PPCP_OK) return rc;
            c->out->extensions      = (const ppcp_id *)base;
            c->out->extension_count = count;
        } else {
            rc = ppcp_cbor_skip(r);
            if (rc != PPCP_OK) return rc;
        }
    }
    return PPCP_OK;
}

static ppcp_result peer_profiles_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    peer_read_ctx *c = (peer_read_ctx *)ctx;
    void *base = NULL; size_t count = 0;
    ppcp_result rc;
    (void)dst;
    rc = ppcp_rec_read_array_arena(r, c->arena, sizeof(ppcp_id), sizeof(void *),
                                   &base, &count, ppcp_sub_read_id_elem, NULL);
    if (rc != PPCP_OK) return rc;
    c->out->profiles      = (const ppcp_id *)base;
    c->out->profile_count = count;
    return PPCP_OK;
}

static ppcp_result peer_timebases_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    peer_read_ctx *c = (peer_read_ctx *)ctx;
    void *base = NULL; size_t count = 0;
    ppcp_result rc;
    (void)dst;
    rc = ppcp_rec_read_array_arena(r, c->arena, sizeof(ppcp_timebase), sizeof(void *),
                                   &base, &count, ppcp_sub_read_timebase, NULL);
    if (rc != PPCP_OK) return rc;
    c->out->timebases      = (const ppcp_timebase *)base;
    c->out->timebase_count = count;
    return PPCP_OK;
}

static ppcp_result peer_relations_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    peer_read_ctx *c = (peer_read_ctx *)ctx;
    void *base = NULL; size_t count = 0;
    ppcp_result rc;
    (void)dst;
    rc = ppcp_rec_read_array_arena(r, c->arena, sizeof(ppcp_timebase_relation), sizeof(void *),
                                   &base, &count, ppcp_sub_read_relation, NULL);
    if (rc != PPCP_OK) return rc;
    c->out->relations      = (const ppcp_timebase_relation *)base;
    c->out->relation_count = count;
    return PPCP_OK;
}

static ppcp_result source_elem_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    ppcp_arena *a = (ppcp_arena *)ctx;
    return ppcp_source_decode(r, a, (ppcp_source *)dst);
}

static ppcp_result peer_sources_read(ppcp_cbor_reader *r, void *dst, void *ctx)
{
    peer_read_ctx *c = (peer_read_ctx *)ctx;
    void *base = NULL; size_t count = 0;
    ppcp_result rc;
    (void)dst;
    rc = ppcp_rec_read_array_arena(r, c->arena, sizeof(ppcp_source), sizeof(void *),
                                   &base, &count, source_elem_read, c->arena);
    if (rc != PPCP_OK) return rc;
    c->out->sources      = (const ppcp_source *)base;
    c->out->source_count = count;
    return PPCP_OK;
}

static ppcp_result source_elem_write(ppcp_cbor_writer *w, const void *elem)
{
    return ppcp_source_encode(w, (const ppcp_source *)elem);
}

/* The three list writers `declare` also uses, declared in ppcp_codec.h. */
ppcp_result ppcp_peer_timebases_read(ppcp_cbor_reader *r, ppcp_arena *a, ppcp_peer_desc *out)
{
    peer_read_ctx ctx;
    ctx.arena = a;
    ctx.out   = out;
    return peer_timebases_read(r, NULL, &ctx);
}

ppcp_result ppcp_peer_relations_read(ppcp_cbor_reader *r, ppcp_arena *a, ppcp_peer_desc *out)
{
    peer_read_ctx ctx;
    ctx.arena = a;
    ctx.out   = out;
    return peer_relations_read(r, NULL, &ctx);
}

ppcp_result ppcp_peer_sources_read(ppcp_cbor_reader *r, ppcp_arena *a, ppcp_peer_desc *out)
{
    peer_read_ctx ctx;
    ctx.arena = a;
    ctx.out   = out;
    return peer_sources_read(r, NULL, &ctx);
}

ppcp_result ppcp_peer_timebases_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_peer_desc *p = (const ppcp_peer_desc *)ctx;
    return ppcp_rec_write_array(w, p->timebases, sizeof(ppcp_timebase),
                                p->timebase_count, ppcp_elem_write_timebase);
}

ppcp_result ppcp_peer_relations_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_peer_desc *p = (const ppcp_peer_desc *)ctx;
    return ppcp_rec_write_array(w, p->relations, sizeof(ppcp_timebase_relation),
                                p->relation_count, ppcp_elem_write_relation);
}

ppcp_result ppcp_peer_sources_write(ppcp_cbor_writer *w, const void *ctx)
{
    const ppcp_peer_desc *p = (const ppcp_peer_desc *)ctx;
    return ppcp_rec_write_array(w, p->sources, sizeof(ppcp_source),
                                p->source_count, source_elem_write);
}

static void peer_common_fields(ppcp_wfield *f, const ppcp_peer_desc *p, size_t *n)
{
    f[(*n)++] = ppcp_wf_id("id", &p->id);
    f[(*n)++] = ppcp_wf_enum("role", role_map, (int)p->role);
    f[(*n)++] = ppcp_wf_sub("protocol", protocol_write, p);
}

ppcp_result ppcp_peer_head_encode(ppcp_cbor_writer *w, const ppcp_peer_desc *p)
{
    ppcp_wfield f[5];
    idlist_ref  prof;
    size_t      n = 0;
    ppcp_result rc = ppcp_peer_desc_validate(p);
    if (rc != PPCP_OK)
        return rc;
    prof.v = p->profiles;
    prof.n = p->profile_count;
    peer_common_fields(f, p, &n);
    f[n++] = ppcp_wf_sub("profiles", idlist_sub_write, &prof);
    if (p->product.present)
        f[n++] = ppcp_wf_sub("product", ppcp_product_write, &p->product);
    return ppcp_rec_write(w, f, n);
}

ppcp_result ppcp_peer_desc_encode(ppcp_cbor_writer *w, const ppcp_peer_desc *p)
{
    ppcp_wfield f[8];
    idlist_ref  prof;
    size_t      n = 0;
    ppcp_result rc = ppcp_peer_desc_validate(p);
    if (rc != PPCP_OK)
        return rc;
    prof.v = p->profiles;
    prof.n = p->profile_count;
    peer_common_fields(f, p, &n);
    f[n++] = ppcp_wf_sub("profiles", idlist_sub_write, &prof);
    f[n++] = ppcp_wf_sub("timebases", ppcp_peer_timebases_write, p);
    if (p->relation_count > 0)
        f[n++] = ppcp_wf_sub("relations", ppcp_peer_relations_write, p);
    f[n++] = ppcp_wf_sub("sources", ppcp_peer_sources_write, p);
    if (p->product.present)
        f[n++] = ppcp_wf_sub("product", ppcp_product_write, &p->product);
    return ppcp_rec_write(w, f, n);
}

static ppcp_result peer_decode_common(ppcp_cbor_reader *r, ppcp_arena *a, ppcp_peer_desc *out,
                                      bool with_lists)
{
    ppcp_rfield   f[8];
    size_t        n = 0;
    peer_read_ctx ctx;
    bool          s_id = false, s_role = false, s_proto = false, s_prof = false;
    int           role = 0;
    ppcp_result   rc;

    memset(out, 0, sizeof(*out));
    ctx.arena = a;
    ctx.out   = out;

    f[n++] = ppcp_rf("id", PPCP_F_ID, &out->id, &s_id);
    f[n++] = ppcp_rf_enum("role", role_map, &role, &s_role);
    f[n++] = ppcp_rf_sub("protocol", protocol_read, NULL, &ctx, &s_proto);
    f[n++] = ppcp_rf_sub("profiles", peer_profiles_read, NULL, &ctx, &s_prof);
    f[n++] = ppcp_rf_sub("product", ppcp_product_read, &out->product, NULL, NULL);
    if (with_lists) {
        f[n++] = ppcp_rf_sub("timebases", peer_timebases_read, NULL, &ctx, NULL);
        f[n++] = ppcp_rf_sub("relations", peer_relations_read, NULL, &ctx, NULL);
        f[n++] = ppcp_rf_sub("sources", peer_sources_read, NULL, &ctx, NULL);
    }
    rc = ppcp_rec_read(r, f, n);
    if (rc != PPCP_OK)
        return rc;
    if (!s_id || !s_role || !s_proto || !s_prof)
        return PPCP_ERR_MALFORMED;
    out->role = (ppcp_role)role;
    return PPCP_OK;
}

ppcp_result ppcp_peer_head_decode(ppcp_cbor_reader *r, ppcp_arena *a, ppcp_peer_desc *out)
{
    if (out == NULL)
        return PPCP_ERR_INVALID;
    /* The head has no timebases, so ppcp_peer_desc_validate would reject it;
     * `declare` validates the assembled Peer once its lists are read. */
    return peer_decode_common(r, a, out, false);
}

ppcp_result ppcp_peer_desc_decode(ppcp_cbor_reader *r, ppcp_arena *a, ppcp_peer_desc *out)
{
    ppcp_result rc;
    if (out == NULL)
        return PPCP_ERR_INVALID;
    rc = peer_decode_common(r, a, out, true);
    if (rc != PPCP_OK)
        return rc;
    if (ppcp_peer_desc_validate(out) != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    return PPCP_OK;
}
