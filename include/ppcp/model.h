/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * model.h — the entity vocabulary of PPCP-CORE §5, as C structs with encode,
 * decode and validate.  Work package L4.
 *
 * §5.1–5.5 (Instant, Series, Interval, Estimate, Timebase, TimebaseRelation,
 * ClockDiscontinuity) landed in L2 and live in time.h; §5.7 Timing, §5.7
 * geometry and §5.8 AchievedFrames landed in L3 and live in timing.h.  This
 * header carries everything else, and the two earlier headers are included so
 * that model.h alone is enough to describe a declaration.
 *
 * THE INVARIANTS MADE UNCONSTRUCTIBLE HERE
 *
 * The rule throughout is CORE §11.1's: an invariant constrains the *shape* of
 * an output.  Where a shape can be made unrepresentable it is, because a
 * validator is a check that can be skipped and a missing constructor is not.
 *
 *   I22    `frame_start_to_exposure_offset_ns` iff `nominal_frame_start` —
 *          two constructors in timing.h, and ppcp_capture_profile_validate
 *          rejects the profile that got past them.
 *   5.6e   `viewpoint.confidence` iff `method: classified` — two setters,
 *          ppcp_source_set_viewpoint_declared and _classified.  A person who
 *          says "down the line" is not expressing a probability.
 *   5.10e  `coincidence_window_ns` and `issue_hold_ns` iff the Session has a
 *          host — ppcp_session_make_hosted takes both, ppcp_session_make_
 *          hostless takes neither, and there is no setter.
 *   I27    a Capture anchors to exactly one of a Shot, a Candidate or its own
 *          Stream — three constructors, one tagged union, zero keys and two
 *          keys both unrepresentable.
 *   I28    `measured` is absent unless a self-test produced it.  There is no
 *          function anywhere in this library that derives a
 *          MeasuredCapability from claimed values or a device table.
 *   I29    `tof_correction` carries both `value_ns` and `sigma_ns` — it is a
 *          ppcp_estimate, whose only constructor takes both (ENC 4.1e).
 *   5.16e/f `confirmed_by` accompanies `confirmed: true`, and a retrospective
 *          basis may only be `confirmed_by: user` — the confirmed constructor
 *          refuses `observer` for those three bases.
 *   5.18j  `stream_id` follows `kind`: a view-specific kind carries it, a
 *          non-view-specific kind does not, and an unrecognised kind is
 *          treated as view-specific if and only if it is present.
 *
 * OPEN REGISTRIES ARE STRINGS.  `Source.kind`, `Stream.kind`,
 * `Candidate.basis`, `Calibration.kind`, `ContextChange.kind`,
 * `ShotLink.basis`, `ClockDiscontinuity.cause` and `Capture.absent_reason` are
 * `ppcp_id`, never enumerations, so an unknown value round-trips and is never
 * fatal (CORE 10.3a, I13).  Everything else in §5 is a closed set and an
 * unknown value there is malformed.
 */
#ifndef PPCP_MODEL_H
#define PPCP_MODEL_H

#include "ppcp/timing.h"
#include "ppcp/hash.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CORE 5.1 `Digest` — { alg: "sha-256", value: bytes }.  One algorithm in
 * ppcp/1.0, so the struct carries the value and the encoder writes the name. */
typedef struct ppcp_digest {
    bool    present;
    uint8_t value[PPCP_SHA256_BYTES];
} ppcp_digest;

PPCP_API ppcp_result ppcp_digest_set(ppcp_digest *d, const uint8_t v[PPCP_SHA256_BYTES]);
PPCP_API bool        ppcp_digest_equal(const ppcp_digest *a, const ppcp_digest *b);
PPCP_API ppcp_result ppcp_digest_encode(ppcp_cbor_writer *w, const ppcp_digest *d);
PPCP_API ppcp_result ppcp_digest_decode(ppcp_cbor_reader *r, ppcp_digest *out);

/* ==================================================== CORE 5.8 — capability */

/* An ordinal protocol vocabulary, not a platform passthrough (CORE 5.8).  A
 * peer maps its platform's states onto it and MAY carry an opaque
 * `vendor_thermal_label` beside it in `heartbeat_ack`. */
typedef enum ppcp_thermal_level {
    PPCP_THERMAL_NOMINAL = 0,
    PPCP_THERMAL_ELEVATED,
    PPCP_THERMAL_SERIOUS,
    PPCP_THERMAL_CRITICAL
} ppcp_thermal_level;

PPCP_API const char *ppcp_thermal_level_str(ppcp_thermal_level l);

/* `{ min, max, median }` — the shape `achieved_exposure_ns`, `achieved_iso`,
 * `AchievedSummary.exposure_ns` and `AchievedSummary.iso` all share. */
typedef struct ppcp_range3 {
    bool    present;
    int64_t min;
    int64_t max;
    int64_t median;
} ppcp_range3;

PPCP_API ppcp_result ppcp_range3_set(ppcp_range3 *r, int64_t min, int64_t max, int64_t median);

/* CORE 5.8 MeasuredCapability — what the peer observed itself sustaining.
 *
 * I28: absence of this on a profile means NOT MEASURED.  There is deliberately
 * no constructor that takes a CaptureProfile, a device-profile table or a
 * previous device model: the only way to fill one in is to have run a
 * self-test and to say which kind it was. */
typedef enum ppcp_measure_method {
    PPCP_MEAS_COLD_SAMPLE = 0,   /* seconds, at onboarding, thermally cold */
    PPCP_MEAS_SUSTAINED          /* taken under sustained thermal load (5.8b) */
} ppcp_measure_method;

typedef struct ppcp_measured_capability {
    ppcp_measure_method method;          /* 5.8a: mandatory */
    ppcp_duration_ns    duration_ns;     /* 5.8a: mandatory */
    int64_t             sustained_rate_mhz;
    int64_t             dropped_frames;
    ppcp_range3         achieved_exposure_ns;
    ppcp_range3         achieved_iso;
    bool                has_noise_figure;   /* absent means not measured */
    double              noise_figure;
    bool                has_thermal_at_end;
    ppcp_thermal_level  thermal_at_end;
    ppcp_instant        observed_at;
} ppcp_measured_capability;

PPCP_API ppcp_result ppcp_measured_capability_make(ppcp_measured_capability *out,
                                                   ppcp_measure_method method,
                                                   ppcp_duration_ns duration_ns,
                                                   int64_t sustained_rate_mhz,
                                                   int64_t dropped_frames,
                                                   const ppcp_instant *observed_at);
PPCP_API ppcp_result ppcp_measured_capability_validate(const ppcp_measured_capability *m);
PPCP_API ppcp_result ppcp_measured_capability_encode(ppcp_cbor_writer *w,
                                                     const ppcp_measured_capability *m);
PPCP_API ppcp_result ppcp_measured_capability_decode(ppcp_cbor_reader *r,
                                                     ppcp_measured_capability *out);

/* CORE 5.8 AchievedSummary — travels on control, in `capture_announce`. */
typedef struct ppcp_thermal_point {
    ppcp_instant       at;
    ppcp_thermal_level level;
} ppcp_thermal_point;

typedef struct ppcp_achieved_summary {
    bool     has_frame_count;      int64_t frame_count;
    bool     has_dropped_frames;   int64_t dropped_frames;
    bool     has_realised_rate_mhz;int64_t realised_rate_mhz;
    ppcp_range3 exposure_ns;
    ppcp_range3 iso;
    /* A timeline, not a single value (CORE 5.8).  Caller-owned. */
    const ppcp_thermal_point *thermal;
    size_t                    thermal_count;
} ppcp_achieved_summary;

PPCP_API ppcp_result ppcp_achieved_summary_validate(const ppcp_achieved_summary *s);
PPCP_API ppcp_result ppcp_achieved_summary_encode(ppcp_cbor_writer *w,
                                                  const ppcp_achieved_summary *s);
PPCP_API ppcp_result ppcp_achieved_summary_decode(ppcp_cbor_reader *r, ppcp_arena *a,
                                                  ppcp_achieved_summary *out);

/* CORE 5.8 / ENC 4.1d — AchievedFrames on the wire.
 *
 * The struct is timing.h's, because the canonical-instant accessors of L3 are
 * what it is for.  The codec is here because it is where the intrinsics
 * first-element rule lives, and that rule is the one thing about AchievedFrames
 * that is a wire question rather than an arithmetic one. */
PPCP_API ppcp_result ppcp_achieved_frames_encode(ppcp_cbor_writer *w,
                                                 const ppcp_achieved_frames *af);
PPCP_API ppcp_result ppcp_achieved_frames_decode(ppcp_cbor_reader *r, ppcp_arena *a,
                                                 ppcp_achieved_frames *out);

/* ======================================================= CORE 5.7 — profile */

typedef struct ppcp_format {
    bool     present;
    ppcp_id  codec;
    uint32_t width;
    uint32_t height;
    ppcp_id  pixel_format;
} ppcp_format;

typedef struct ppcp_rate {
    bool    present;
    int64_t nominal_mhz;   /* millihertz: 150 fps is 150000 (CORE 5.7) */
    int64_t min_mhz;
    int64_t max_mhz;
} ppcp_rate;

typedef struct ppcp_optical {
    bool    present;
    int64_t exposure_min_ns;
    int64_t exposure_max_ns;
    int64_t iso_min;
    int64_t iso_max;
    bool    has_noise_figure;   /* absent means not measured */
    double  noise_figure;
} ppcp_optical;

typedef enum ppcp_intrinsics_mode {
    PPCP_INTR_PER_FRAME = 0,
    PPCP_INTR_FIXED,
    PPCP_INTR_NONE          /* what a preview profile declares (5.11m) */
} ppcp_intrinsics_mode;

typedef struct ppcp_capture_profile {
    ppcp_id              id;
    ppcp_format          format;
    ppcp_rate            rate;
    ppcp_optical         optical;
    bool                 has_geometry;      /* camera: mandatory (I19) */
    ppcp_geometry        geometry;
    ppcp_timing          timing;            /* always mandatory */
    bool                 has_intrinsics;    /* camera: mandatory (I19) */
    ppcp_intrinsics_mode intrinsics;
    bool                 has_measured;      /* I28: absence means not measured */
    ppcp_measured_capability measured;
} ppcp_capture_profile;

/* Every profile declares `timing`; a camera profile additionally declares
 * `geometry` and `intrinsics` (I19), which is why they are set rather than
 * defaulted. */
PPCP_API ppcp_result ppcp_capture_profile_make(ppcp_capture_profile *out,
                                               const char *id,
                                               const ppcp_timing *timing);
PPCP_API ppcp_result ppcp_capture_profile_set_camera(ppcp_capture_profile *p,
                                                     const ppcp_geometry *geometry,
                                                     ppcp_intrinsics_mode intrinsics);
PPCP_API ppcp_result ppcp_capture_profile_set_format(ppcp_capture_profile *p,
                                                     const char *codec, uint32_t width,
                                                     uint32_t height, const char *pixel_format);
PPCP_API ppcp_result ppcp_capture_profile_set_rate(ppcp_capture_profile *p,
                                                   int64_t nominal_mhz, int64_t min_mhz,
                                                   int64_t max_mhz);
PPCP_API ppcp_result ppcp_capture_profile_set_optical(ppcp_capture_profile *p,
                                                      int64_t exposure_min_ns,
                                                      int64_t exposure_max_ns,
                                                      int64_t iso_min, int64_t iso_max);
/* 5.7f / I28: the caller has run a self-test on THIS device model.  There is
 * no other route to a `measured`. */
PPCP_API ppcp_result ppcp_capture_profile_set_measured(ppcp_capture_profile *p,
                                                       const ppcp_measured_capability *m);
PPCP_API ppcp_result ppcp_capture_profile_validate(const ppcp_capture_profile *p);
PPCP_API ppcp_result ppcp_capture_profile_encode(ppcp_cbor_writer *w,
                                                 const ppcp_capture_profile *p);
PPCP_API ppcp_result ppcp_capture_profile_decode(ppcp_cbor_reader *r,
                                                 ppcp_capture_profile *out);

/* ==================================================== CORE 5.9 — Calibration */

typedef enum ppcp_calibration_method {
    PPCP_CALM_FACTORY = 0,
    PPCP_CALM_PER_FRAME,
    PPCP_CALM_SOLVED,
    PPCP_CALM_USER_MEASURED,
    PPCP_CALM_ESTIMATED_ONLINE
} ppcp_calibration_method;

/* `parameters` and `uncertainty` are kind-specific maps.  The protocol does not
 * define their contents, so they are carried as the encoded CBOR map — which
 * round-trips byte-identically for a kind this library does not know, and
 * keeps `uncertainty` mandatory (5.9) without inventing a schema for it. */
typedef struct ppcp_calibration {
    ppcp_id                 id;
    ppcp_id                 source_id;
    ppcp_id                 kind;            /* open registry */
    const uint8_t          *parameters;      /* one encoded CBOR map */
    size_t                  parameters_len;
    const uint8_t          *uncertainty;     /* mandatory (5.9) */
    size_t                  uncertainty_len;
    ppcp_calibration_method method;
    ppcp_instant            observed_at;
} ppcp_calibration;

PPCP_API ppcp_result ppcp_calibration_make(ppcp_calibration *out, const char *id,
                                           const char *source_id, const char *kind,
                                           const uint8_t *parameters, size_t parameters_len,
                                           const uint8_t *uncertainty, size_t uncertainty_len,
                                           ppcp_calibration_method method,
                                           const ppcp_instant *observed_at);
PPCP_API ppcp_result ppcp_calibration_validate(const ppcp_calibration *c);
PPCP_API ppcp_result ppcp_calibration_encode(ppcp_cbor_writer *w, const ppcp_calibration *c);
PPCP_API ppcp_result ppcp_calibration_decode(ppcp_cbor_reader *r, ppcp_calibration *out);

/* ======================================================== CORE 5.6 — Source */

typedef enum ppcp_viewpoint_method {
    PPCP_VP_DECLARED = 0,   /* a user or installer stated it */
    PPCP_VP_CLASSIFIED      /* the peer worked it out — and only then, a confidence */
} ppcp_viewpoint_method;

typedef struct ppcp_viewpoint {
    bool                  present;
    ppcp_id               label;      /* open registry: dtl, face_on, rear, … */
    ppcp_viewpoint_method method;
    bool                  has_confidence;   /* 5.6e: iff method == classified */
    double                confidence;
} ppcp_viewpoint;

typedef struct ppcp_source {
    ppcp_id  id;
    ppcp_id  peer_id;
    ppcp_id  kind;          /* open registry (10.3) */
    ppcp_id  timebase_id;   /* 5.6a: mandatory */
    bool     physical;      /* 5.6b: false for a virtual multi-lens device */
    const ppcp_capture_profile *profiles;   /* 1..n; caller-owned */
    size_t   profile_count;
    bool     has_calibration;
    ppcp_calibration calibration;
    bool     has_optics;    /* 5.6d: which physical lens this Source is */
    ppcp_id  optics;
    ppcp_viewpoint viewpoint;
    bool     has_label;
    ppcp_id  label;         /* informational */
} ppcp_source;

PPCP_API ppcp_result ppcp_source_make(ppcp_source *out, const char *id, const char *peer_id,
                                      const char *kind, const char *timebase_id,
                                      bool physical,
                                      const ppcp_capture_profile *profiles,
                                      size_t profile_count);
PPCP_API ppcp_result ppcp_source_set_calibration(ppcp_source *s, const ppcp_calibration *c);
PPCP_API ppcp_result ppcp_source_set_optics(ppcp_source *s, const char *optics);
PPCP_API ppcp_result ppcp_source_set_label(ppcp_source *s, const char *label);
/* 5.6e — two setters, because `confidence` is present if and only if the
 * viewpoint was classified. */
PPCP_API ppcp_result ppcp_source_set_viewpoint_declared(ppcp_source *s, const char *label);
PPCP_API ppcp_result ppcp_source_set_viewpoint_classified(ppcp_source *s, const char *label,
                                                          double confidence);
PPCP_API bool        ppcp_source_kind_is_camera(const ppcp_source *s);
PPCP_API ppcp_result ppcp_source_validate(const ppcp_source *s);
PPCP_API ppcp_result ppcp_source_encode(ppcp_cbor_writer *w, const ppcp_source *s);
PPCP_API ppcp_result ppcp_source_decode(ppcp_cbor_reader *r, ppcp_arena *a, ppcp_source *out);

/* ========================================================== CORE 5.2 — Peer */

typedef enum ppcp_role {
    PPCP_ROLE_HOST = 0,
    PPCP_ROLE_CAPTURE,
    PPCP_ROLE_OBSERVER      /* receives, contributes no Streams */
} ppcp_role;

PPCP_API const char *ppcp_role_str(ppcp_role r);

typedef struct ppcp_product {
    bool    present;
    ppcp_id vendor;
    ppcp_id model;
    ppcp_id version;
} ppcp_product;

/* Named `ppcp_peer_desc` and not `ppcp_peer` because the engine of L6 owns
 * that name: this is the *declaration*, the thing that crosses the wire. */
typedef struct ppcp_peer_desc {
    ppcp_id       id;
    ppcp_role     role;          /* 5.2a: per session, fixed for its lifetime */
    ppcp_id       protocol_version;
    const ppcp_id *extensions;   /* reverse-DNS strings (10.2a) */
    size_t        extension_count;
    const ppcp_id *profiles;     /* 1..n; MUST include "core" */
    size_t        profile_count;
    const ppcp_timebase *timebases;   /* 1..n */
    size_t        timebase_count;
    const ppcp_timebase_relation *relations;
    size_t        relation_count;
    const ppcp_source *sources;  /* 0..n — a Peer owning none participates fully */
    size_t        source_count;
    ppcp_product  product;       /* 5.2c: never used to infer behaviour (I19) */
} ppcp_peer_desc;

PPCP_API ppcp_result ppcp_peer_desc_make(ppcp_peer_desc *out, const char *id, ppcp_role role,
                                         const char *protocol_version,
                                         const ppcp_id *profiles, size_t profile_count,
                                         const ppcp_timebase *timebases, size_t timebase_count);
PPCP_API ppcp_result ppcp_peer_desc_set_sources(ppcp_peer_desc *p, const ppcp_source *sources,
                                                size_t count);
PPCP_API ppcp_result ppcp_peer_desc_set_relations(ppcp_peer_desc *p,
                                                  const ppcp_timebase_relation *rel, size_t count);
PPCP_API ppcp_result ppcp_peer_desc_set_extensions(ppcp_peer_desc *p, const ppcp_id *ext,
                                                   size_t count);
PPCP_API ppcp_result ppcp_peer_desc_set_product(ppcp_peer_desc *p, const char *vendor,
                                                const char *model, const char *version);
PPCP_API bool        ppcp_peer_desc_has_profile(const ppcp_peer_desc *p, const char *profile);
PPCP_API ppcp_result ppcp_peer_desc_validate(const ppcp_peer_desc *p);
PPCP_API ppcp_result ppcp_peer_desc_encode(ppcp_cbor_writer *w, const ppcp_peer_desc *p);
PPCP_API ppcp_result ppcp_peer_desc_decode(ppcp_cbor_reader *r, ppcp_arena *a,
                                           ppcp_peer_desc *out);

/* The eight profiles of CORE §2.2.  Held as strings on the wire; these are the
 * spellings, so a caller never invents one. */
#define PPCP_PROFILE_CORE      "core"
#define PPCP_PROFILE_CAPTURE   "capture"
#define PPCP_PROFILE_DETECT    "detect"
#define PPCP_PROFILE_MINT      "mint"
#define PPCP_PROFILE_ARBITRATE "arbitrate"
#define PPCP_PROFILE_LIVE      "live"
#define PPCP_PROFILE_MARKUP    "markup"
#define PPCP_PROFILE_OFFLINE   "offline"

/* ======================================================= CORE 5.10 — Session */

typedef struct ppcp_context_change {
    ppcp_id      id;
    ppcp_instant at;
    ppcp_id      kind;    /* open registry: club, shot_type, handedness, … */
    ppcp_id      value;
} ppcp_context_change;

PPCP_API ppcp_result ppcp_context_change_make(ppcp_context_change *out, const char *id,
                                              const ppcp_instant *at, const char *kind,
                                              const char *value);
PPCP_API ppcp_result ppcp_context_change_validate(const ppcp_context_change *c);
PPCP_API ppcp_result ppcp_context_change_encode(ppcp_cbor_writer *w,
                                                const ppcp_context_change *c);
PPCP_API ppcp_result ppcp_context_change_decode(ppcp_cbor_reader *r,
                                                ppcp_context_change *out);

typedef enum ppcp_session_state {
    PPCP_SESSION_OPEN = 0,
    PPCP_SESSION_CLOSED
} ppcp_session_state;

typedef enum ppcp_completeness {
    PPCP_COMPLETE = 0,
    PPCP_PARTIAL,
    PPCP_ABSENT,      /* Capture only */
    PPCP_UNKNOWN      /* Session only */
} ppcp_completeness;

typedef struct ppcp_epoch {
    bool         present;
    int64_t      wall_utc_ns;   /* a LABEL (I15); never used to compute an interval */
    ppcp_instant at;
} ppcp_epoch;

struct ppcp_stream;
struct ppcp_shot;

typedef struct ppcp_session {
    ppcp_id            id;
    const ppcp_peer_desc *peers;
    size_t             peer_count;
    ppcp_id            timebase_ref;      /* IMMUTABLE once set (I16) */
    ppcp_epoch         epoch;
    /* 5.10e: present if and only if a peer with role host is in the roster.
     * Their absence is the structural statement that no arbitration occurs. */
    bool               has_arbitration;
    ppcp_duration_ns   coincidence_window_ns;
    ppcp_duration_ns   issue_hold_ns;
    bool               has_heartbeat_interval;
    uint32_t           heartbeat_interval_ms;
    const struct ppcp_stream *streams;
    size_t             stream_count;
    const struct ppcp_shot *shots;
    size_t             shot_count;
    const ppcp_context_change *contexts;
    size_t             context_count;
    ppcp_session_state state;
    ppcp_completeness  completeness;      /* asserted, never inferred (I10) */
} ppcp_session;

#define PPCP_DEFAULT_COINCIDENCE_WINDOW_NS 50000000   /* CORE 5.10, 50 ms */
#define PPCP_DEFAULT_ISSUE_HOLD_NS         200000000  /* CORE 5.10, 200 ms */
#define PPCP_DEFAULT_HEARTBEAT_MS          1000       /* CORE 7.4a */

/* 5.10e made structural: the hostless constructor cannot be given the two
 * arbitration parameters, and there is no setter for them. */
PPCP_API ppcp_result ppcp_session_make_hostless(ppcp_session *out, const char *id,
                                                const char *timebase_ref);
PPCP_API ppcp_result ppcp_session_make_hosted(ppcp_session *out, const char *id,
                                              const char *timebase_ref,
                                              ppcp_duration_ns coincidence_window_ns,
                                              ppcp_duration_ns issue_hold_ns);
PPCP_API ppcp_result ppcp_session_set_epoch(ppcp_session *s, int64_t wall_utc_ns,
                                            const ppcp_instant *at);
PPCP_API ppcp_result ppcp_session_set_heartbeat_interval(ppcp_session *s, uint32_t ms);
PPCP_API ppcp_result ppcp_session_set_peers(ppcp_session *s, const ppcp_peer_desc *peers,
                                            size_t count);
PPCP_API ppcp_result ppcp_session_set_completeness(ppcp_session *s, ppcp_completeness c);
/* I12 — a Session is valid with ANY subset of Streams, including none and
 * including video-only.  These setters therefore accept zero, and
 * ppcp_session_validate has no minimum. */
PPCP_API ppcp_result ppcp_session_set_streams(ppcp_session *s, const struct ppcp_stream *st,
                                              size_t count);
PPCP_API ppcp_result ppcp_session_set_shots(ppcp_session *s, const struct ppcp_shot *sh,
                                            size_t count);
PPCP_API ppcp_result ppcp_session_set_contexts(ppcp_session *s, const ppcp_context_change *c,
                                               size_t count);
/* I16 — there is no ppcp_session_set_timebase_ref().  A host that re-solves a
 * clock mapping on import declares a NEW TimebaseRelation from `timebase_ref`
 * (5.10b, 8.5d); this is the function that answers "may I?" with "no". */
PPCP_API bool        ppcp_session_timebase_ref_matches(const ppcp_session *s, const char *tb);
PPCP_API ppcp_result ppcp_session_validate(const ppcp_session *s);
PPCP_API ppcp_result ppcp_session_encode(ppcp_cbor_writer *w, const ppcp_session *s);
PPCP_API ppcp_result ppcp_session_decode(ppcp_cbor_reader *r, ppcp_arena *a,
                                         ppcp_session *out);

/* ======================================================== CORE 5.11 — Stream */

typedef enum ppcp_continuity {
    PPCP_CONTINUOUS = 0,
    PPCP_SHOT_WINDOWED
} ppcp_continuity;

typedef struct ppcp_stream {
    ppcp_id         id;
    ppcp_id         session_id;
    ppcp_id         source_id;
    ppcp_id         kind;          /* open registry: video, preview, audio, … */
    ppcp_id         profile_id;
    bool            has_calibration_id;
    ppcp_id         calibration_id;
    ppcp_id         timebase_id;
    ppcp_continuity continuity;
    ppcp_instant    opened_at;
    bool            has_closed_at;
    ppcp_instant    closed_at;
} ppcp_stream;

#define PPCP_STREAM_KIND_VIDEO    "video"
#define PPCP_STREAM_KIND_PREVIEW  "preview"
#define PPCP_STREAM_KIND_AUDIO    "audio"
#define PPCP_STREAM_KIND_IMU      "imu"
#define PPCP_STREAM_KIND_WRIST    "wrist"
#define PPCP_STREAM_KIND_EVENT    "event"
#define PPCP_STREAM_KIND_METADATA "metadata"

PPCP_API ppcp_result ppcp_stream_make(ppcp_stream *out, const char *id, const char *session_id,
                                      const char *source_id, const char *kind,
                                      const char *profile_id, const char *timebase_id,
                                      ppcp_continuity continuity,
                                      const ppcp_instant *opened_at);
PPCP_API ppcp_result ppcp_stream_set_calibration_id(ppcp_stream *s, const char *calibration_id);
PPCP_API ppcp_result ppcp_stream_close(ppcp_stream *s, const ppcp_instant *closed_at);
PPCP_API bool        ppcp_stream_is_preview(const ppcp_stream *s);
PPCP_API ppcp_result ppcp_stream_validate(const ppcp_stream *s);
PPCP_API ppcp_result ppcp_stream_encode(ppcp_cbor_writer *w, const ppcp_stream *s);
PPCP_API ppcp_result ppcp_stream_decode(ppcp_cbor_reader *r, ppcp_stream *out);

/* ===================================================== CORE 5.12 — Candidate */

typedef struct ppcp_candidate {
    ppcp_id       id;
    ppcp_id       peer_id;
    ppcp_id       source_id;    /* 5.12a / I26: mandatory, and on a declared Timebase */
    ppcp_id       basis;        /* open registry: acoustic, motion, external, … */
    ppcp_instant  at;           /* 5.12e / I33: the CANONICAL instant, already converted */
    bool          has_tof_correction;
    ppcp_estimate tof_correction;         /* I29: both value and sigma, by construction */
    bool          has_canonical_correction;
    int64_t       canonical_correction_ns;
    double        confidence;             /* mandatory */
    const uint8_t *classifier;            /* basis-specific map, carried opaque */
    size_t         classifier_len;
    bool          has_evidence_capture_id;
    ppcp_id       evidence_capture_id;
} ppcp_candidate;

PPCP_API ppcp_result ppcp_candidate_make(ppcp_candidate *out, const char *id,
                                         const char *peer_id, const char *source_id,
                                         const char *basis, const ppcp_instant *at,
                                         double confidence);
/* I29 in the type system: the parameter is an Estimate, and ppcp_estimate_make
 * is the only way to obtain one. */
PPCP_API ppcp_result ppcp_candidate_set_tof_correction(ppcp_candidate *c,
                                                       const ppcp_estimate *e);
PPCP_API ppcp_result ppcp_candidate_set_canonical_correction(ppcp_candidate *c, int64_t ns);
PPCP_API ppcp_result ppcp_candidate_set_classifier(ppcp_candidate *c, const uint8_t *map,
                                                   size_t len);
PPCP_API ppcp_result ppcp_candidate_set_evidence(ppcp_candidate *c, const char *capture_id);
PPCP_API ppcp_result ppcp_candidate_validate(const ppcp_candidate *c);
PPCP_API ppcp_result ppcp_candidate_encode(ppcp_cbor_writer *w, const ppcp_candidate *c);
PPCP_API ppcp_result ppcp_candidate_decode(ppcp_cbor_reader *r, ppcp_candidate *out);

/* ========================================================== CORE 5.13 — Shot */

typedef enum ppcp_authority {
    PPCP_AUTHORITY_HOST = 0,
    PPCP_AUTHORITY_DEVICE
} ppcp_authority;

/* `candidates` and `captures` are held in fixed arrays rather than by pointer,
 * because 5.13d makes the candidate list the one part of a Shot another peer
 * may extend — additively and in place, converging in either delivery order. */
#define PPCP_SHOT_MAX_CANDIDATES 32
#define PPCP_SHOT_MAX_CAPTURES   32

typedef struct ppcp_shot {
    ppcp_id        id;
    ppcp_id        session_id;
    ppcp_instant   t0;           /* in Session.timebase_ref (5.13c); never revised (I7) */
    ppcp_authority authority;
    ppcp_id        issued_by;
    ppcp_id        candidates[PPCP_SHOT_MAX_CANDIDATES];   /* 1..n, winners and losers */
    size_t         candidate_count;
    ppcp_id        captures[PPCP_SHOT_MAX_CAPTURES];
    size_t         capture_count;
} ppcp_shot;

PPCP_API ppcp_result ppcp_shot_make(ppcp_shot *out, const char *id, const char *session_id,
                                    const ppcp_instant *t0, ppcp_authority authority,
                                    const char *issued_by, const char *first_candidate_id);
PPCP_API ppcp_result ppcp_shot_add_capture(ppcp_shot *s, const char *capture_id);
PPCP_API ppcp_result ppcp_shot_validate(const ppcp_shot *s);
PPCP_API ppcp_result ppcp_shot_encode(ppcp_cbor_writer *w, const ppcp_shot *s);
PPCP_API ppcp_result ppcp_shot_decode(ppcp_cbor_reader *r, ppcp_shot *out);

/* ======================================================= CORE 5.14 — Capture */

/* I27 / ENC 4.1d — exactly one key.  Zero and two are not representable, which
 * is the invariant made structural rather than validated. */
typedef enum ppcp_anchor_kind {
    PPCP_ANCHOR_SHOT = 0,
    PPCP_ANCHOR_CANDIDATE,
    PPCP_ANCHOR_STREAM      /* a segment of the Capture's own continuous Stream */
} ppcp_anchor_kind;

typedef struct ppcp_anchor {
    ppcp_anchor_kind kind;
    ppcp_id          id;    /* unused for PPCP_ANCHOR_STREAM */
} ppcp_anchor;

PPCP_API ppcp_result ppcp_anchor_shot(ppcp_anchor *out, const char *shot_id);
PPCP_API ppcp_result ppcp_anchor_candidate(ppcp_anchor *out, const char *candidate_id);
PPCP_API ppcp_result ppcp_anchor_stream(ppcp_anchor *out);
PPCP_API ppcp_result ppcp_anchor_encode(ppcp_cbor_writer *w, const ppcp_anchor *a);
PPCP_API ppcp_result ppcp_anchor_decode(ppcp_cbor_reader *r, ppcp_anchor *out);

typedef enum ppcp_transfer_state {
    PPCP_TRANSFER_PENDING = 0,   /* held locally, unsent */
    PPCP_TRANSFER_IN_FLIGHT,
    PPCP_TRANSFER_PRESENT,
    PPCP_TRANSFER_CONFIRMED,     /* only the receiver can say this (5.14f, 8.4b) */
    PPCP_TRANSFER_FAILED
} ppcp_transfer_state;

#define PPCP_ABSENT_OUTSIDE_BUFFER "outside_buffer"
#define PPCP_ABSENT_NOT_RETAINED   "not_retained"
#define PPCP_ABSENT_STORAGE_FULL   "storage_full"
#define PPCP_ABSENT_NOT_ARMED      "not_armed"
#define PPCP_ABSENT_THERMAL_LIMIT  "thermal_limit"
#define PPCP_ABSENT_LINK_LOST      "link_lost"

#define PPCP_CAPTURE_MAX_GAPS 32

typedef struct ppcp_capture {
    ppcp_id             id;
    ppcp_anchor         anchor;
    ppcp_id             stream_id;
    bool                has_interval;
    ppcp_interval       interval;
    ppcp_completeness   completeness;      /* complete | partial | absent (I10) */
    bool                has_absent_reason;
    ppcp_id             absent_reason;     /* open registry; mandatory when absent */
    ppcp_interval       gaps[PPCP_CAPTURE_MAX_GAPS];  /* I11: loss, only on continuous */
    size_t              gap_count;
    bool                has_achieved_summary;
    ppcp_achieved_summary achieved_summary;
    ppcp_transfer_state transfer;
    ppcp_digest         digest;
    bool                has_bytes;
    uint64_t            bytes;
} ppcp_capture;

/* Three constructors, one per anchor.  A stream-anchored Capture takes its
 * interval as a parameter because 5.14d makes it mandatory there even when
 * `completeness: absent` — for a segment the interval IS the claim. */
PPCP_API ppcp_result ppcp_capture_make_shot(ppcp_capture *out, const char *id,
                                            const char *shot_id, const char *stream_id,
                                            ppcp_completeness completeness);
PPCP_API ppcp_result ppcp_capture_make_candidate(ppcp_capture *out, const char *id,
                                                 const char *candidate_id,
                                                 const char *stream_id,
                                                 ppcp_completeness completeness);
PPCP_API ppcp_result ppcp_capture_make_segment(ppcp_capture *out, const char *id,
                                               const char *stream_id,
                                               ppcp_completeness completeness,
                                               const ppcp_interval *interval);
PPCP_API ppcp_result ppcp_capture_set_interval(ppcp_capture *c, const ppcp_interval *iv);
PPCP_API ppcp_result ppcp_capture_set_absent_reason(ppcp_capture *c, const char *reason);
/* I11: a gap means data was LOST inside a segment that otherwise exists.
 * Deliberate non-retention is an `absent` segment (5.11c3), never a gap. */
PPCP_API ppcp_result ppcp_capture_add_gap(ppcp_capture *c, const ppcp_interval *gap);
PPCP_API ppcp_result ppcp_capture_set_summary(ppcp_capture *c, const ppcp_achieved_summary *s);
PPCP_API ppcp_result ppcp_capture_set_digest(ppcp_capture *c, const ppcp_digest *d,
                                             uint64_t bytes);
/* 5.14f / 8.4b — `confirmed` is not in reach of this setter.  An owner sets it
 * only through ppcp_transfer_on_committed() in transfer.h, which needs a
 * `capture_committed` from the receiver to call. */
PPCP_API ppcp_result ppcp_capture_set_transfer(ppcp_capture *c, ppcp_transfer_state t);
PPCP_API ppcp_result ppcp_capture_validate(const ppcp_capture *c);
/* CT-I27's second half and I11's: `{stream: true}` is refused on a
 * `shot_windowed` Stream, and gaps are meaningful only on a `continuous` one. */
PPCP_API ppcp_result ppcp_capture_validate_in_stream(const ppcp_capture *c,
                                                     const ppcp_stream *s);
PPCP_API ppcp_result ppcp_capture_encode(ppcp_cbor_writer *w, const ppcp_capture *c);
PPCP_API ppcp_result ppcp_capture_decode(ppcp_cbor_reader *r, ppcp_capture *out);

/* ===================================================== CORE 5.15 — Readiness */

typedef struct ppcp_readiness {
    bool     settled;
    bool     has_estimated_ready_ms;   /* mandatory when !settled */
    uint32_t estimated_ready_ms;
    bool     has_blocked_reason;
    ppcp_id  blocked_reason;           /* storage_full, thermal_limit, … */
} ppcp_readiness;

/* 5.15a — no device state-machine name crosses the wire.  There is no
 * `ppcp_readiness_make(const char *state)`, and these two are the whole API. */
PPCP_API ppcp_result ppcp_readiness_settled(ppcp_readiness *out);
PPCP_API ppcp_result ppcp_readiness_not_settled(ppcp_readiness *out, uint32_t ready_ms);
PPCP_API ppcp_result ppcp_readiness_set_blocked(ppcp_readiness *r, const char *reason);
PPCP_API ppcp_result ppcp_readiness_validate(const ppcp_readiness *r);
PPCP_API ppcp_result ppcp_readiness_encode(ppcp_cbor_writer *w, const ppcp_readiness *r);
PPCP_API ppcp_result ppcp_readiness_decode(ppcp_cbor_reader *r, ppcp_readiness *out);

/* ====================================================== CORE 5.16 — ShotLink */

typedef enum ppcp_confirmed_by {
    PPCP_CONFIRMED_BY_OBSERVER = 0,  /* a live assertion by the peer that observed it */
    PPCP_CONFIRMED_BY_USER           /* a human decision */
} ppcp_confirmed_by;

#define PPCP_LINK_ARRIVAL_PAIRING     "arrival_pairing"
#define PPCP_LINK_SHARED_CANDIDATE    "shared_candidate"
#define PPCP_LINK_INTERVAL_ALIGNMENT  "interval_alignment"
#define PPCP_LINK_ACOUSTIC_CORRELATION "acoustic_correlation"
#define PPCP_LINK_SEQUENCE_ALIGNMENT  "sequence_alignment"
#define PPCP_LINK_MANUAL              "manual"

typedef struct ppcp_shot_link {
    ppcp_id           id;
    ppcp_id           local_shot_id;
    ppcp_id           foreign_shot_id;
    bool              has_foreign_session_id;
    ppcp_id           foreign_session_id;
    ppcp_id           basis;            /* open registry */
    bool              has_foreign_system;
    ppcp_id           foreign_system;   /* reverse-DNS */
    double            confidence;
    bool              confirmed;
    bool              has_confirmed_by; /* 5.16e: present iff confirmed */
    ppcp_confirmed_by confirmed_by;
} ppcp_shot_link;

/* 5.16b/f — `interval_alignment`, `acoustic_correlation` and
 * `sequence_alignment` are retrospective.  This is the predicate 5.16f turns
 * on and it is exported because a consumer needs it too. */
PPCP_API bool ppcp_shot_link_basis_is_retrospective(const char *basis, size_t len);

PPCP_API ppcp_result ppcp_shot_link_make(ppcp_shot_link *out, const char *id,
                                         const char *local_shot_id,
                                         const char *foreign_shot_id,
                                         const char *basis, double confidence);
/* Refuses `confirmed_by: observer` on a retrospective basis (5.16f), and there
 * is no way to set `confirmed` without saying which kind it was (5.16e). */
PPCP_API ppcp_result ppcp_shot_link_confirm(ppcp_shot_link *l, ppcp_confirmed_by by);
PPCP_API ppcp_result ppcp_shot_link_set_foreign_session(ppcp_shot_link *l, const char *sid);
PPCP_API ppcp_result ppcp_shot_link_set_foreign_system(ppcp_shot_link *l, const char *sys);
PPCP_API ppcp_result ppcp_shot_link_validate(const ppcp_shot_link *l);
PPCP_API ppcp_result ppcp_shot_link_encode(ppcp_cbor_writer *w, const ppcp_shot_link *l);
PPCP_API ppcp_result ppcp_shot_link_decode(ppcp_cbor_reader *r, ppcp_shot_link *out);

/* =================================================== CORE 5.17 — SessionLink */

typedef struct ppcp_session_link {
    ppcp_id             id;
    ppcp_id             from_session_id;
    ppcp_id             to_session_id;
    ppcp_id             from_timebase_id;
    ppcp_id             to_timebase_id;
    ppcp_relation_class cls;
    int64_t             offset_ns;
    double              skew_ppm;
    double              offset_sigma_ns;   /* 5.17b: mandatory when affine */
    double              skew_sigma_ppm;
    ppcp_id             basis;
    ppcp_id             derived_by;
    bool                confirmed;
} ppcp_session_link;

PPCP_API ppcp_result ppcp_session_link_make_affine(ppcp_session_link *out, const char *id,
                                                   const char *from_session, const char *to_session,
                                                   const char *from_tb, const char *to_tb,
                                                   int64_t offset_ns, double skew_ppm,
                                                   double offset_sigma_ns, double skew_sigma_ppm,
                                                   const char *basis, const char *derived_by);
PPCP_API ppcp_result ppcp_session_link_make_unrelated(ppcp_session_link *out, const char *id,
                                                      const char *from_session,
                                                      const char *to_session,
                                                      const char *from_tb, const char *to_tb,
                                                      const char *basis, const char *derived_by);
PPCP_API ppcp_result ppcp_session_link_validate(const ppcp_session_link *l);
PPCP_API ppcp_result ppcp_session_link_encode(ppcp_cbor_writer *w, const ppcp_session_link *l);
PPCP_API ppcp_result ppcp_session_link_decode(ppcp_cbor_reader *r, ppcp_session_link *out);

/* ⚠ I25: a SessionLink mutates neither Session, and I18 forbids composing one
 * with a TimebaseRelation.  There is no function here that takes a
 * ppcp_session_link and a ppcp_session, and none that takes a
 * ppcp_session_link and a ppcp_timebase_relation.  CT-I25's static half is an
 * assertion about that absence (tests/api_surface.cmake). */

/* ==================================================== CORE 5.18 — Annotation */

#define PPCP_ANNOTATION_BODY_MAX 8192   /* 5.18f — and ENC §8 agrees */

typedef enum ppcp_annotation_provenance {
    PPCP_ANNOT_USER = 0,
    PPCP_ANNOT_DEVICE_ADVISORY
} ppcp_annotation_provenance;

typedef struct ppcp_annotation {
    ppcp_id        id;
    ppcp_id        session_id;
    ppcp_id        shot_id;
    bool           has_stream_id;    /* 5.18j: presence follows `kind` */
    ppcp_id        stream_id;
    ppcp_instant   at;
    ppcp_id        author_peer_id;
    ppcp_annotation_provenance provenance;
    ppcp_id        kind;             /* open registry: line, plane, text, nav_anchor */
    ppcp_id        format;
    const uint8_t *body;             /* opaque; round-trips byte-identical (5.18a) */
    size_t         body_len;
    ppcp_instant   created_at;
    uint64_t       revision;
    bool           has_deleted;
    bool           deleted;
} ppcp_annotation;

/* 5.18j — `line` and `plane` are view-specific; `text` and `nav_anchor` are
 * not.  An unrecognised kind is treated as view-specific if and only if
 * `stream_id` is present, which is the conservative default: it then renders
 * only on the Stream named, or not at all. */
typedef enum ppcp_kind_view {
    PPCP_KIND_VIEW_SPECIFIC = 0,
    PPCP_KIND_NOT_VIEW_SPECIFIC,
    PPCP_KIND_UNREGISTERED
} ppcp_kind_view;

PPCP_API ppcp_kind_view ppcp_annotation_kind_view(const char *kind, size_t len);

PPCP_API ppcp_result ppcp_annotation_make(ppcp_annotation *out, const char *id,
                                          const char *session_id, const char *shot_id,
                                          const ppcp_instant *at, const char *author_peer_id,
                                          ppcp_annotation_provenance provenance,
                                          const char *kind, const char *format,
                                          const uint8_t *body, size_t body_len,
                                          const ppcp_instant *created_at, uint64_t revision);
PPCP_API ppcp_result ppcp_annotation_set_stream_id(ppcp_annotation *a, const char *stream_id);
PPCP_API ppcp_result ppcp_annotation_set_deleted(ppcp_annotation *a, bool deleted);
PPCP_API ppcp_result ppcp_annotation_validate(const ppcp_annotation *a);
/* The codec is here rather than in L11 because `annotation` is one of the
 * forty-five messages of MSG §11 and I24 makes every peer parse all of them,
 * whether or not it declares Markup.  The supersession comparison — which is
 * behaviour rather than vocabulary — stays with L11. */
PPCP_API ppcp_result ppcp_annotation_encode(ppcp_cbor_writer *w, const ppcp_annotation *a);
PPCP_API ppcp_result ppcp_annotation_decode(ppcp_cbor_reader *r, ppcp_annotation *out);

/* ==================================================================== I9/I25 */

/* ⚠ There is deliberately NO merge or rewrite operation anywhere in this
 * header.  I9: reconciliation creates links; no entity is rewritten or merged.
 * CT-I9 asserts the absence by API surface (tests/api_surface.cmake), which is
 * the method CONF §3 names for it. */

#ifdef __cplusplus
}
#endif
#endif /* PPCP_MODEL_H */
