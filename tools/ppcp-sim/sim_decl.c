/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * sim_decl.c — a `Peer` declaration (CORE §5.2) built from a JSON file.
 *
 * Every field the CONF 2c list names is here and is data: `timing.convention`,
 * `geometry`, `provenance` with a non-zero offset, `unrelated` relations, the
 * profile set, and as many timebases as the file cares to declare.  Nothing in
 * this file decides anything — it reads what the file says and hands it to the
 * library's constructors, which are the things that refuse an invalid shape.
 */
#include "sim.h"
#include "sim_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SIM_JSON_NODES 4096

static void copy_id(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap)
        n = cap - 1u;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

int64_t sim_now_ns(void)
{
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    clock_gettime(CLOCK_REALTIME, &ts);
#endif
    return (int64_t)ts.tv_sec * 1000000000 + (int64_t)ts.tv_nsec;
}

ppcp_result sim_clock_now(void *ctx, const char *timebase_id, int64_t *out_ns)
{
    sim_clock *c = (sim_clock *)ctx;
    size_t     i;
    int64_t    elapsed = sim_now_ns() - c->origin_ns;

    for (i = 0; i < c->count; i++) {
        if (strcmp(c->tb[i].id, timebase_id) == 0) {
            double drift = (double)elapsed * c->tb[i].skew_ppm * 1.0e-6;
            int64_t d = (drift >= 0.0) ? (int64_t)(drift + 0.5) : -(int64_t)(-drift + 0.5);
            *out_ns = c->tb[i].offset_ns + elapsed + d;
            return PPCP_OK;
        }
    }
    return PPCP_ERR_NOT_FOUND;
}

/* ------------------------------------------------------------- enum tables */

static bool role_of(const char *s, ppcp_role *out)
{
    if (strcmp(s, "host") == 0)     { *out = PPCP_ROLE_HOST;     return true; }
    if (strcmp(s, "capture") == 0)  { *out = PPCP_ROLE_CAPTURE;  return true; }
    if (strcmp(s, "observer") == 0) { *out = PPCP_ROLE_OBSERVER; return true; }
    return false;
}

static bool tb_kind_of(const char *s, ppcp_timebase_kind *out)
{
    if (strcmp(s, "monotonic") == 0)  { *out = PPCP_TB_MONOTONIC;  return true; }
    if (strcmp(s, "continuous") == 0) { *out = PPCP_TB_CONTINUOUS; return true; }
    if (strcmp(s, "wall") == 0)       { *out = PPCP_TB_WALL;       return true; }
    return false;
}

static bool convention_of(const char *s, ppcp_convention *out)
{
    if (strcmp(s, "mid") == 0)   { *out = PPCP_CONV_MID;   return true; }
    if (strcmp(s, "start") == 0) { *out = PPCP_CONV_START; return true; }
    if (strcmp(s, "end") == 0)   { *out = PPCP_CONV_END;   return true; }
    if (strcmp(s, "nominal_frame_start") == 0) {
        *out = PPCP_CONV_NOMINAL_FRAME_START;
        return true;
    }
    return false;
}

static bool provenance_of(const char *s, ppcp_provenance *out)
{
    if (strcmp(s, "assumed") == 0)  { *out = PPCP_PROV_ASSUMED;  return true; }
    if (strcmp(s, "vendor") == 0)   { *out = PPCP_PROV_VENDOR;   return true; }
    if (strcmp(s, "measured") == 0) { *out = PPCP_PROV_MEASURED; return true; }
    return false;
}

static bool intrinsics_of(const char *s, ppcp_intrinsics_mode *out)
{
    if (strcmp(s, "per_frame") == 0) { *out = PPCP_INTR_PER_FRAME; return true; }
    if (strcmp(s, "fixed") == 0)     { *out = PPCP_INTR_FIXED;     return true; }
    if (strcmp(s, "none") == 0)      { *out = PPCP_INTR_NONE;      return true; }
    return false;
}

static bool method_of(const char *s, ppcp_relation_method *out)
{
    if (strcmp(s, "declared") == 0)          { *out = PPCP_RELM_DECLARED;  return true; }
    if (strcmp(s, "measured") == 0)          { *out = PPCP_RELM_MEASURED;  return true; }
    if (strcmp(s, "estimated_online") == 0)  { *out = PPCP_RELM_ESTIMATED_ONLINE; return true; }
    return false;
}

/* ------------------------------------------------------------------ pieces */

static bool load_timing(const sj_doc *doc, const sj_node *n, ppcp_timing *out,
                        char *err, size_t err_len)
{
    ppcp_convention conv;
    ppcp_provenance prov = PPCP_PROV_ASSUMED;
    const sj_node  *sigma;

    if (!convention_of(sj_str_or(sj_get(doc, n, "convention"), "?"), &conv)) {
        snprintf(err, err_len, "timing.convention is missing or unknown");
        return false;
    }
    if (conv == PPCP_CONV_NOMINAL_FRAME_START) {
        const sj_node *off = sj_get(doc, n, "frame_start_to_exposure_offset_ns");
        if (off == NULL) {
            snprintf(err, err_len,
                     "nominal_frame_start needs frame_start_to_exposure_offset_ns (CORE 5.7b)");
            return false;
        }
        if (!provenance_of(sj_str_or(sj_get(doc, n, "offset_provenance"), "?"), &prov)) {
            snprintf(err, err_len,
                     "nominal_frame_start needs offset_provenance (I31): "
                     "assumed, vendor or measured");
            return false;
        }
        if (ppcp_timing_make_nominal_frame_start(out, sj_int_or(off, 0), prov) != PPCP_OK) {
            snprintf(err, err_len, "the library refused that timing");
            return false;
        }
    } else if (ppcp_timing_make(out, conv) != PPCP_OK) {
        snprintf(err, err_len, "the library refused that timing convention");
        return false;
    }
    sigma = sj_get(doc, n, "frame_start_to_exposure_offset_sigma_ns");
    if (sigma != NULL)
        (void)ppcp_timing_set_offset_sigma(out, sj_real_or(sigma, 0.0));
    return true;
}

static bool load_geometry(const sj_doc *doc, const sj_node *n, ppcp_geometry *out,
                          char *err, size_t err_len)
{
    const char *kind = sj_str_or(sj_get(doc, n, "kind"), "global");
    const sj_node *sigma;

    if (strcmp(kind, "global") == 0) {
        if (ppcp_geometry_make_global(out) != PPCP_OK) {
            snprintf(err, err_len, "the library refused a global geometry");
            return false;
        }
        return true;
    }
    if (strcmp(kind, "rolling_shutter") == 0) {
        ppcp_provenance prov;
        ppcp_rolling_direction dir = PPCP_ROLL_TOP_TO_BOTTOM;
        const char *ds = sj_str_or(sj_get(doc, n, "direction"), "top_to_bottom");
        if (strcmp(ds, "bottom_to_top") == 0)
            dir = PPCP_ROLL_BOTTOM_TO_TOP;
        else if (strcmp(ds, "top_to_bottom") != 0) {
            snprintf(err, err_len, "geometry.direction must be top_to_bottom or bottom_to_top");
            return false;
        }
        if (!provenance_of(sj_str_or(sj_get(doc, n, "readout_provenance"), "?"), &prov)) {
            snprintf(err, err_len, "rolling_shutter needs readout_provenance (I31)");
            return false;
        }
        if (ppcp_geometry_make_rolling_shutter(out, sj_int_or(sj_get(doc, n, "readout_ns"), 0),
                                               prov, dir,
                                               (uint32_t)sj_int_or(sj_get(doc, n, "rows"), 0))
            != PPCP_OK) {
            snprintf(err, err_len, "the library refused that rolling-shutter geometry");
            return false;
        }
        sigma = sj_get(doc, n, "readout_sigma_ns");
        if (sigma != NULL)
            (void)ppcp_geometry_set_readout_sigma(out, sj_real_or(sigma, 0.0));
        return true;
    }
    snprintf(err, err_len, "geometry.kind must be global or rolling_shutter");
    return false;
}

static bool load_capture_profile(const sj_doc *doc, const sj_node *n,
                                 ppcp_capture_profile *out, char *err, size_t err_len)
{
    ppcp_timing    timing;
    const sj_node *g, *f, *r, *o;

    if (!load_timing(doc, sj_get(doc, n, "timing"), &timing, err, err_len))
        return false;
    if (ppcp_capture_profile_make(out, sj_str_or(sj_get(doc, n, "id"), "cp:0"), &timing)
        != PPCP_OK) {
        snprintf(err, err_len, "the library refused that CaptureProfile");
        return false;
    }
    g = sj_get(doc, n, "geometry");
    if (g != NULL) {
        ppcp_geometry        geom;
        ppcp_intrinsics_mode intr;
        if (!load_geometry(doc, g, &geom, err, err_len))
            return false;
        if (!intrinsics_of(sj_str_or(sj_get(doc, n, "intrinsics"), "per_frame"), &intr)) {
            snprintf(err, err_len, "intrinsics must be per_frame, fixed or none");
            return false;
        }
        if (ppcp_capture_profile_set_camera(out, &geom, intr) != PPCP_OK) {
            snprintf(err, err_len, "the library refused that camera profile");
            return false;
        }
    }
    f = sj_get(doc, n, "format");
    if (f != NULL)
        (void)ppcp_capture_profile_set_format(out,
                sj_str_or(sj_get(doc, f, "codec"), "h264"),
                (uint32_t)sj_int_or(sj_get(doc, f, "width"), 1920),
                (uint32_t)sj_int_or(sj_get(doc, f, "height"), 1080),
                sj_str_or(sj_get(doc, f, "pixel_format"), "nv12"));
    r = sj_get(doc, n, "rate");
    if (r != NULL)
        (void)ppcp_capture_profile_set_rate(out,
                sj_int_or(sj_get(doc, r, "nominal_mhz"), 0),
                sj_int_or(sj_get(doc, r, "min_mhz"), 0),
                sj_int_or(sj_get(doc, r, "max_mhz"), 0));
    o = sj_get(doc, n, "optical");
    if (o != NULL)
        (void)ppcp_capture_profile_set_optical(out,
                sj_int_or(sj_get(doc, o, "exposure_min_ns"), 0),
                sj_int_or(sj_get(doc, o, "exposure_max_ns"), 0),
                sj_int_or(sj_get(doc, o, "iso_min"), 0),
                sj_int_or(sj_get(doc, o, "iso_max"), 0));
    if (ppcp_capture_profile_validate(out) != PPCP_OK) {
        snprintf(err, err_len, "CaptureProfile `%s` is not valid",
                 sj_str_or(sj_get(doc, n, "id"), "?"));
        return false;
    }
    return true;
}

static bool load_source(const sj_doc *doc, const sj_node *n, sim_decl *d, size_t index,
                        char *err, size_t err_len)
{
    const sj_node *profs = sj_get(doc, n, "profiles");
    int            count = sj_len(doc, profs);
    int            i;
    const sj_node *vp;

    if (count <= 0 || count > (int)SIM_MAX_CP) {
        snprintf(err, err_len, "a Source declares 1..%d CaptureProfiles (5.6)", SIM_MAX_CP);
        return false;
    }
    for (i = 0; i < count; i++) {
        if (!load_capture_profile(doc, sj_at(doc, profs, i), &d->cp[index][i], err, err_len))
            return false;
    }
    d->cp_count[index] = (size_t)count;
    if (ppcp_source_make(&d->src[index],
                         sj_str_or(sj_get(doc, n, "id"), "src:0"),
                         d->peer_id,
                         sj_str_or(sj_get(doc, n, "kind"), "camera"),
                         sj_str_or(sj_get(doc, n, "timebase"), d->tb[0].id.v),
                         sj_bool_or(sj_get(doc, n, "physical"), true),
                         d->cp[index], (size_t)count) != PPCP_OK) {
        snprintf(err, err_len, "the library refused Source `%s`",
                 sj_str_or(sj_get(doc, n, "id"), "?"));
        return false;
    }
    if (sj_get(doc, n, "optics") != NULL)
        (void)ppcp_source_set_optics(&d->src[index],
                                     sj_str_or(sj_get(doc, n, "optics"), ""));
    if (sj_get(doc, n, "label") != NULL)
        (void)ppcp_source_set_label(&d->src[index], sj_str_or(sj_get(doc, n, "label"), ""));
    vp = sj_get(doc, n, "viewpoint");
    if (vp != NULL) {
        const char *m = sj_str_or(sj_get(doc, vp, "method"), "declared");
        const char *l = sj_str_or(sj_get(doc, vp, "label"), "dtl");
        if (strcmp(m, "classified") == 0)
            (void)ppcp_source_set_viewpoint_classified(&d->src[index], l,
                    sj_real_or(sj_get(doc, vp, "confidence"), 0.5));
        else
            (void)ppcp_source_set_viewpoint_declared(&d->src[index], l);
    }
    if (ppcp_source_validate(&d->src[index]) != PPCP_OK) {
        snprintf(err, err_len, "Source `%s` is not valid",
                 sj_str_or(sj_get(doc, n, "id"), "?"));
        return false;
    }
    return true;
}

static bool load_relation(const sj_doc *doc, const sj_node *n, ppcp_timebase_relation *out,
                          char *err, size_t err_len)
{
    const char          *cls = sj_str_or(sj_get(doc, n, "class"), "affine");
    const char          *from = sj_str_or(sj_get(doc, n, "from"), "");
    const char          *to   = sj_str_or(sj_get(doc, n, "to"), "");
    ppcp_relation_method meth;
    ppcp_instant         at;
    const sj_node       *obs = sj_get(doc, n, "observed_at");

    if (!method_of(sj_str_or(sj_get(doc, n, "method"), "measured"), &meth)) {
        snprintf(err, err_len, "relation.method must be declared, measured or estimated_online");
        return false;
    }
    if (ppcp_instant_make_z(&at, sj_str_or(sj_get(doc, obs, "tb"), from),
                            sj_int_or(sj_get(doc, obs, "ns"), 0)) != PPCP_OK) {
        snprintf(err, err_len, "relation.observed_at is not a valid Instant");
        return false;
    }
    if (strcmp(cls, "unrelated") == 0) {
        /* CORE 5.4b — a legal, complete declaration, and the one the
         * interoperability pairing of CONF §5 needs a peer to be able to make. */
        if (ppcp_relation_make_unrelated(out, from, to, meth, &at) != PPCP_OK) {
            snprintf(err, err_len, "the library refused that `unrelated` relation");
            return false;
        }
        return true;
    }
    /* I3 in the type system: both sigmas or no relation. */
    if (ppcp_relation_make_affine(out, from, to,
                                  sj_int_or(sj_get(doc, n, "offset_ns"), 0),
                                  sj_real_or(sj_get(doc, n, "skew_ppm"), 0.0),
                                  sj_real_or(sj_get(doc, n, "offset_sigma_ns"), -1.0),
                                  sj_real_or(sj_get(doc, n, "skew_sigma_ppm"), -1.0),
                                  meth, &at) != PPCP_OK) {
        snprintf(err, err_len,
                 "affine relation %s -> %s refused: both offset_sigma_ns and "
                 "skew_sigma_ppm are mandatory (CORE 5.4a, I3)", from, to);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------- entry */

bool sim_decl_load(sim_decl *d, const char *path, char *err, size_t err_len)
{
    static sj_node nodes[SIM_JSON_NODES];
    sj_doc         doc;
    char          *text;
    const sj_node *root, *n, *arr;
    int            i, count;
    ppcp_role      role;

    memset(d, 0, sizeof(*d));
    text = sj_read_file(path, err, err_len);
    if (text == NULL)
        return false;
    if (!sj_parse(&doc, text, nodes, SIM_JSON_NODES)) {
        snprintf(err, err_len, "%s:%d: %s", path, doc.err_line, doc.err);
        free(text);
        return false;
    }
    root = sj_root(&doc);
    if (root == NULL || root->type != SJ_OBJ) {
        snprintf(err, err_len, "%s: the top-level value must be an object", path);
        free(text);
        return false;
    }

    copy_id(d->peer_id, sizeof(d->peer_id), sj_str_or(sj_get(&doc, root, "peer_id"), "sim:peer"));
    if (!role_of(sj_str_or(sj_get(&doc, root, "role"), "capture"), &role)) {
        snprintf(err, err_len, "role must be host, capture or observer");
        goto fail;
    }
    d->role = role;

    /* profiles */
    arr   = sj_get(&doc, root, "profiles");
    count = sj_len(&doc, arr);
    if (count <= 0 || count > (int)SIM_MAX_PROF) {
        snprintf(err, err_len, "profiles must name 1..%d profiles, and must include core",
                 SIM_MAX_PROF);
        goto fail;
    }
    for (i = 0; i < count; i++) {
        const char *s = sj_str_or(sj_at(&doc, arr, i), "");
        copy_id(d->profile_text[i], sizeof(d->profile_text[i]), s);
        d->profile_ptr[i] = d->profile_text[i];
        if (ppcp_id_set_z(&d->profiles[i], d->profile_text[i]) != PPCP_OK) {
            snprintf(err, err_len, "profile `%s` is not a valid Id", s);
            goto fail;
        }
    }
    d->profile_count = (size_t)count;

    /* timebases, and the simulated clock behind them */
    arr   = sj_get(&doc, root, "timebases");
    count = sj_len(&doc, arr);
    if (count <= 0 || count > (int)SIM_MAX_TB) {
        snprintf(err, err_len, "timebases must declare 1..%d entries (CORE 5.2)", SIM_MAX_TB);
        goto fail;
    }
    d->clock.origin_ns = sim_now_ns();
    for (i = 0; i < count; i++) {
        const sj_node     *t = sj_at(&doc, arr, i);
        ppcp_timebase_kind kind;
        const char        *id = sj_str_or(sj_get(&doc, t, "id"), "");
        if (!tb_kind_of(sj_str_or(sj_get(&doc, t, "kind"), "monotonic"), &kind)) {
            snprintf(err, err_len, "timebase.kind must be monotonic, continuous or wall");
            goto fail;
        }
        if (ppcp_timebase_make(&d->tb[i], id, strlen(id), kind,
                               sj_bool_or(sj_get(&doc, t, "epoch_stable"), true),
                               sj_int_or(sj_get(&doc, t, "resolution_ns"), 1000)) != PPCP_OK) {
            snprintf(err, err_len, "the library refused timebase `%s`", id);
            goto fail;
        }
        if (sj_get(&doc, t, "origin") != NULL) {
            const char *o = sj_str_or(sj_get(&doc, t, "origin"), "");
            (void)ppcp_timebase_set_origin(&d->tb[i], o, strlen(o));
        }
        copy_id(d->clock.tb[i].id, sizeof(d->clock.tb[i].id), id);
        d->clock.tb[i].offset_ns = sj_int_or(sj_get(&doc, t, "offset_ns"), 0);
        d->clock.tb[i].skew_ppm  = sj_real_or(sj_get(&doc, t, "skew_ppm"), 0.0);
    }
    d->tb_count    = (size_t)count;
    d->clock.count = (size_t)count;

    /* relations */
    arr   = sj_get(&doc, root, "relations");
    count = sj_len(&doc, arr);
    if (count > (int)SIM_MAX_REL) {
        snprintf(err, err_len, "at most %d relations", SIM_MAX_REL);
        goto fail;
    }
    for (i = 0; i < count; i++) {
        if (!load_relation(&doc, sj_at(&doc, arr, i), &d->rel[i], err, err_len))
            goto fail;
    }
    d->rel_count = (size_t)(count > 0 ? count : 0);

    /* sources */
    arr   = sj_get(&doc, root, "sources");
    count = sj_len(&doc, arr);
    if (count > (int)SIM_MAX_SRC) {
        snprintf(err, err_len, "at most %d Sources", SIM_MAX_SRC);
        goto fail;
    }
    for (i = 0; i < count; i++) {
        if (!load_source(&doc, sj_at(&doc, arr, i), d, (size_t)i, err, err_len))
            goto fail;
    }
    d->src_count = (size_t)(count > 0 ? count : 0);

    /* the declaration itself.  MSG 3.3d: a peer owning no Source declares an
     * empty list, and that is a complete declaration. */
    if (ppcp_peer_desc_make(&d->desc, d->peer_id, d->role,
                            sj_str_or(sj_get(&doc, root, "protocol_version"),
                                      ppcp_wire_version()),
                            d->profiles, d->profile_count,
                            d->tb, d->tb_count) != PPCP_OK) {
        snprintf(err, err_len, "the library refused the Peer declaration");
        goto fail;
    }
    if (d->src_count > 0 &&
        ppcp_peer_desc_set_sources(&d->desc, d->src, d->src_count) != PPCP_OK) {
        snprintf(err, err_len, "the library refused the Source list");
        goto fail;
    }
    if (d->rel_count > 0 &&
        ppcp_peer_desc_set_relations(&d->desc, d->rel, d->rel_count) != PPCP_OK) {
        snprintf(err, err_len, "the library refused the relation list");
        goto fail;
    }
    n = sj_get(&doc, root, "product");
    if (n != NULL) {
        /* 5.2c — informational, and I19 forbids inferring anything from it.
         * It is here so a run's log says which synthetic peer it was. */
        (void)ppcp_peer_desc_set_product(&d->desc,
                sj_str_or(sj_get(&doc, n, "vendor"), "ppcp-sim"),
                sj_str_or(sj_get(&doc, n, "model"), "synthetic"),
                sj_str_or(sj_get(&doc, n, "version"), "1"));
    }
    if (ppcp_peer_desc_validate(&d->desc) != PPCP_OK) {
        snprintf(err, err_len, "the declaration is not valid (MSG 3.3b, I19)");
        goto fail;
    }

    n = sj_get(&doc, root, "sync_timebase");
    if (n != NULL) {
        copy_id(d->sync_tb, sizeof(d->sync_tb), sj_str_or(n, ""));
        d->has_sync_tb = true;
    } else {
        copy_id(d->sync_tb, sizeof(d->sync_tb), d->tb[0].id.v);
        d->has_sync_tb = true;
    }

    n = sj_get(&doc, root, "session");
    copy_id(d->session_id, sizeof(d->session_id),
            sj_str_or(sj_get(&doc, n, "id"), "ses:sim"));
    copy_id(d->timebase_ref, sizeof(d->timebase_ref),
            sj_str_or(sj_get(&doc, n, "timebase_ref"), d->tb[0].id.v));
    d->coincidence_window_ns = sj_int_or(sj_get(&doc, n, "coincidence_window_ns"),
                                         PPCP_DEFAULT_COINCIDENCE_WINDOW_NS);
    d->issue_hold_ns         = sj_int_or(sj_get(&doc, n, "issue_hold_ns"),
                                         PPCP_DEFAULT_ISSUE_HOLD_NS);
    d->heartbeat_ms          = (uint32_t)sj_int_or(sj_get(&doc, n, "heartbeat_interval_ms"),
                                                   PPCP_DEFAULT_HEARTBEAT_MS);

    free(text);
    return true;

fail:
    free(text);
    return false;
}
