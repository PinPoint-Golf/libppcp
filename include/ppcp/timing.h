/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * timing.h — the canonical instant of PPCP-CORE §6.1, the rolling-shutter row
 * instant of §6.2, and the scalar-or-array AchievedFrames accessors of §5.8.
 *
 * CORE §6.1 opens by naming this "the single most likely site of silent
 * non-conformance in the whole protocol", because the conversion spans two
 * entities and two implementers can each apply part of it and both believe
 * themselves compliant.  The error it produces is exposure-dependent, so it
 * moves with the light in the room and looks exactly like clock drift.
 *
 * Two invariants are made structural here:
 *
 *   I22  `frame_start_to_exposure_offset_ns` is present if and only if
 *        `convention == nominal_frame_start`.  There are two constructors:
 *        one takes a convention and refuses nominal_frame_start, the other
 *        takes nominal_frame_start and *requires* the offset and its
 *        provenance.  A defaulted zero is not producible (CT-I22).
 *   I31  the offset and `rolling_shutter.readout_ns` cannot be set without a
 *        provenance, because provenance is a parameter of the constructor.
 *        Plan A12: every timing constant nobody has measured is `assumed`.
 */
#ifndef PPCP_TIMING_H
#define PPCP_TIMING_H

#include "ppcp/time.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CORE 5.7 Timing */
typedef enum ppcp_convention {
    PPCP_CONV_MID = 0,
    PPCP_CONV_START,
    PPCP_CONV_END,
    PPCP_CONV_NOMINAL_FRAME_START   /* what every AVFoundation source declares */
} ppcp_convention;

/* CORE 5.7 Provenance.  `measured` means measured on *this device model*, by
 * the declaring project (5.7f) — not vendor-documented, not inherited. */
typedef enum ppcp_provenance {
    PPCP_PROV_ASSUMED = 0,
    PPCP_PROV_VENDOR,
    PPCP_PROV_MEASURED
} ppcp_provenance;

typedef struct ppcp_timing {
    ppcp_convention convention;
    bool            has_offset;                       /* I22 */
    int64_t         frame_start_to_exposure_offset_ns;
    ppcp_provenance offset_provenance;                /* 5.7b, I31 */
    bool            has_offset_sigma;
    double          frame_start_to_exposure_offset_sigma_ns;
} ppcp_timing;

/* `mid`, `start` or `end`.  Refuses `nominal_frame_start`: that convention
 * needs the offset, so it has its own constructor. */
PPCP_API ppcp_result ppcp_timing_make(ppcp_timing *out, ppcp_convention convention);

/* `nominal_frame_start`.  The offset is a required parameter, explicitly
 * declared even when it is zero (5.7b), and always with its provenance —
 * because a declared zero with no provenance is indistinguishable from an
 * unmeasured one. */
PPCP_API ppcp_result ppcp_timing_make_nominal_frame_start(ppcp_timing *out,
                                                          int64_t offset_ns,
                                                          ppcp_provenance provenance);

/* 5.7: SHOULD be present where provenance is `measured`. */
PPCP_API ppcp_result ppcp_timing_set_offset_sigma(ppcp_timing *t, double sigma_ns);

PPCP_API ppcp_result ppcp_timing_validate(const ppcp_timing *t);

/* CORE 5.7 geometry */
typedef enum ppcp_geometry_kind {
    PPCP_GEOM_GLOBAL = 0,
    PPCP_GEOM_ROLLING_SHUTTER
} ppcp_geometry_kind;

typedef enum ppcp_rolling_direction {
    PPCP_ROLL_TOP_TO_BOTTOM = 0,
    PPCP_ROLL_BOTTOM_TO_TOP
} ppcp_rolling_direction;

typedef struct ppcp_geometry {
    ppcp_geometry_kind     kind;
    /* rolling shutter only */
    int64_t                readout_ns;          /* 6.2a: first-row to last-row exposure start */
    ppcp_provenance        readout_provenance;  /* 5.7e, I31 */
    bool                   has_readout_sigma;
    double                 readout_sigma_ns;
    ppcp_rolling_direction direction;
    uint32_t               rows;                /* 6.2b: rows in the delivered image, R */
} ppcp_geometry;

PPCP_API ppcp_result ppcp_geometry_make_global(ppcp_geometry *out);
PPCP_API ppcp_result ppcp_geometry_make_rolling_shutter(ppcp_geometry *out,
                                                        int64_t readout_ns,
                                                        ppcp_provenance readout_provenance,
                                                        ppcp_rolling_direction direction,
                                                        uint32_t rows);
PPCP_API ppcp_result ppcp_geometry_set_readout_sigma(ppcp_geometry *g, double sigma_ns);
PPCP_API ppcp_result ppcp_geometry_validate(const ppcp_geometry *g);

/* ------------------------------------------------------- canonical instant */

/* CORE §6.1.  `d` is that frame's exposure duration from
 * AchievedFrames.exposure_ns (6.1c) — never from the profile's exposure range,
 * because exposure varies frame to frame under any automatic mode.
 *
 *   mid                   t
 *   start                 t + d/2
 *   end                   t − d/2
 *   nominal_frame_start   t + frame_start_to_exposure_offset_ns + d/2
 *
 * All three inputs are required for the fourth row and none of the three alone
 * is sufficient (6.1b, I17), which is why `t` alone is not enough of a
 * signature to call this function with. */
PPCP_API ppcp_result ppcp_canonical_instant(const ppcp_timing *timing, int64_t t_ns,
                                            ppcp_duration_ns d_ns, int64_t *out_ns);

/* The inverse: canonical back to the source's own convention.  Uses the same
 * halving expression as the forward direction, so a round trip recovers the
 * original timestamp bit-for-bit for every `d` (CT-S1 assertion 4). */
PPCP_API ppcp_result ppcp_canonical_instant_inverse(const ppcp_timing *timing,
                                                    int64_t canonical_ns,
                                                    ppcp_duration_ns d_ns, int64_t *out_ns);

/* The same conversion carrying the timebase through, so a caller never has a
 * bare number in hand (I1). */
PPCP_API ppcp_result ppcp_canonical_instant_of(const ppcp_timing *timing,
                                               const ppcp_instant *raw,
                                               ppcp_duration_ns d_ns, ppcp_instant *out);

/* CORE 6.2d.  `canonical_first` is the instant of the first row read (6.2c).
 *
 *   top_to_bottom   canonical_first + readout_ns × r / (R − 1)
 *   bottom_to_top   canonical_first + readout_ns × (R − 1 − r) / (R − 1)
 *
 * and where R == 1 the row instant is canonical_first.  On `global` geometry
 * every row shares the frame's instant, so this returns canonical_first there
 * too rather than refusing — a consumer should not need to branch on geometry
 * to ask what time a row was exposed. */
PPCP_API ppcp_result ppcp_row_instant(const ppcp_geometry *geometry,
                                      int64_t canonical_first_ns, uint32_t r,
                                      int64_t *out_ns);

/* ------------------------------------------------- AchievedFrames accessors */

/* ENC 4.1d / CORE 5.8f — a per-frame field is either an array of `frame_count`
 * values, or a single value meaning the value was constant for every frame.
 * A decoder distinguishes them by CBOR major type, not by length: a one-frame
 * Capture still encodes an array of one.
 *
 * The scalar form is the one the shipping product uses, because the
 * application locks exposure — which is why CT-S1 assertion 6 exists. */
typedef enum ppcp_per_frame_form {
    PPCP_PER_FRAME_ABSENT = 0,
    PPCP_PER_FRAME_SCALAR,
    PPCP_PER_FRAME_ARRAY
} ppcp_per_frame_form;

typedef struct ppcp_per_frame_i64 {
    ppcp_per_frame_form form;
    int64_t             scalar;
    const int64_t      *values;   /* caller-owned; length is the Capture's frame count */
    size_t              count;
} ppcp_per_frame_i64;

PPCP_API ppcp_result ppcp_per_frame_i64_scalar(ppcp_per_frame_i64 *out, int64_t v);
PPCP_API ppcp_result ppcp_per_frame_i64_array(ppcp_per_frame_i64 *out,
                                              const int64_t *values, size_t count);
/* 5.8f: a parallel array has exactly `frames.ns` length, and a scalar MUST NOT
 * be used to mean "unknown" or "not sampled". */
PPCP_API ppcp_result ppcp_per_frame_i64_at(const ppcp_per_frame_i64 *pf, size_t frame_count,
                                           size_t index, int64_t *out);

/* CORE 5.8 exposure_provenance.  Carried, not judged: whether `sampled` is good
 * enough is the consumer's policy and the protocol carries the fact (6.1e,
 * I14). */
typedef enum ppcp_exposure_provenance {
    PPCP_EXP_PER_FRAME = 0,   /* the value the pipeline attached to that frame */
    PPCP_EXP_SAMPLED,         /* a device property sampled once per frame */
    PPCP_EXP_LOCKED_CONSTANT  /* one value under a lock; goes with the scalar form */
} ppcp_exposure_provenance;

typedef struct ppcp_achieved_frames {
    ppcp_id                  tb;            /* frames.tb — the stream's timebase */
    const int64_t           *frames_ns;     /* I2/5.8e: no scalar form */
    size_t                   frame_count;
    ppcp_per_frame_i64       exposure_ns;   /* 5.8d: present on any camera Capture with frames */
    bool                     has_exposure_provenance;
    ppcp_exposure_provenance exposure_provenance;
    ppcp_per_frame_i64       iso;
} ppcp_achieved_frames;

PPCP_API ppcp_result ppcp_achieved_frames_make(ppcp_achieved_frames *out,
                                               const char *tb, const int64_t *frames_ns,
                                               size_t frame_count);
PPCP_API ppcp_result ppcp_achieved_frames_set_exposure(ppcp_achieved_frames *af,
                                                       const ppcp_per_frame_i64 *exposure,
                                                       ppcp_exposure_provenance provenance);
PPCP_API ppcp_result ppcp_achieved_frames_validate(const ppcp_achieved_frames *af);

/* The accessor the product path uses: the exposure duration of frame `index`,
 * whichever form the peer sent. */
PPCP_API ppcp_result ppcp_achieved_frames_exposure_at(const ppcp_achieved_frames *af,
                                                      size_t index, int64_t *out_ns);

/* I17 in one call: convention, that frame's exposure, and — for
 * nominal_frame_start — the offset.  This is the function a consumer should
 * reach for, because the ways of getting the conversion wrong all involve
 * assembling the three inputs by hand. */
PPCP_API ppcp_result ppcp_achieved_frames_canonical_at(const ppcp_achieved_frames *af,
                                                       const ppcp_timing *timing,
                                                       size_t index, ppcp_instant *out);

#ifdef __cplusplus
}
#endif
#endif /* PPCP_TIMING_H */
