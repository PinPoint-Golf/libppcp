/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * time.h — the injectable clock and the timebase vocabulary of PPCP-CORE §5.1,
 * §5.3–5.5, §6.4 and §6.5.
 *
 * The library owns no clock (plan ground rule 7).  The embedding supplies
 * now(tb) and the tests supply a simulated one, which is what CONF 2a requires:
 * offset, skew and discontinuity are simulated, not waited for, and nothing in
 * CONF §3 that touches time is testable without it.
 *
 * Three invariants are made structural here rather than merely checked:
 *
 *   I1  there is no ppcp_instant without a `tb`, and the clock interface
 *       returns an Instant rather than a number, so a bare timestamp has
 *       nowhere to come from.
 *   I3  an affine TimebaseRelation has one constructor, which takes all four
 *       of offset, skew and both sigmas.  There is no way to build one without
 *       them, so "affine missing a sigma" is unconstructible rather than
 *       forbidden.
 *   I18 there is no composition.  Search this header for a function taking two
 *       relations and returning a third: there is none, and CT-I18's static
 *       half is a test that asserts that by API surface.
 */
#ifndef PPCP_TIME_H
#define PPCP_TIME_H

#include "ppcp/cbor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------- primitives */

/* CORE 5.1 / ENC 4.1a — a point in time.  There is no encoding for a bare
 * timestamp anywhere in PPCP; every one of them is an Instant or an element of
 * a Series, and both carry `tb`. */
typedef struct ppcp_instant {
    ppcp_id tb;
    int64_t ns;
} ppcp_instant;

/* CORE 5.1 — many points in one timebase.  The values are caller-owned: this
 * library allocates nothing, and a per-frame series can run to tens of
 * thousands of entries. */
typedef struct ppcp_series {
    ppcp_id        tb;
    const int64_t *ns;
    size_t         count;
} ppcp_series;

/* CORE 5.1 — half-open [start, end), start <= end. */
typedef struct ppcp_interval {
    ppcp_id tb;
    int64_t start_ns;
    int64_t end_ns;
} ppcp_interval;

/* CORE 5.1 / ENC 4.1e — a value with its dispersion.  Both fields are
 * mandatory together: a point estimate with no dispersion is what silently
 * corrupts fusion (I29, I3). */
typedef struct ppcp_estimate {
    int64_t value_ns;
    double  sigma_ns;
} ppcp_estimate;

/* The only constructors.  Each refuses what the corresponding invariant
 * forbids, so the invalid shape never exists in memory rather than being
 * caught at the encoder. */
PPCP_API ppcp_result ppcp_instant_make(ppcp_instant *out, const char *tb, size_t tb_len,
                                       int64_t ns);
PPCP_API ppcp_result ppcp_instant_make_z(ppcp_instant *out, const char *tb, int64_t ns);
PPCP_API ppcp_result ppcp_instant_validate(const ppcp_instant *in);

PPCP_API ppcp_result ppcp_series_make(ppcp_series *out, const char *tb, size_t tb_len,
                                      const int64_t *ns, size_t count);
PPCP_API ppcp_result ppcp_series_validate(const ppcp_series *in);

PPCP_API ppcp_result ppcp_interval_make(ppcp_interval *out, const char *tb, size_t tb_len,
                                        int64_t start_ns, int64_t end_ns);
PPCP_API ppcp_result ppcp_interval_validate(const ppcp_interval *in);

/* Both keys or neither: there is no ppcp_estimate_make_value(). */
PPCP_API ppcp_result ppcp_estimate_make(ppcp_estimate *out, int64_t value_ns, double sigma_ns);
PPCP_API ppcp_result ppcp_estimate_validate(const ppcp_estimate *in);

/* CORE 5.1: `Duration` is a plain int64 and carries no `tb` (ENC 4.1b).  A
 * duration is not a point in time, and the typedef says so at every call
 * site. */
typedef int64_t ppcp_duration_ns;

/* --------------------------------------------------------------- Timebase */

typedef enum ppcp_timebase_kind {
    PPCP_TB_MONOTONIC = 0,   /* halts across device sleep */
    PPCP_TB_CONTINUOUS,      /* does not */
    PPCP_TB_WALL             /* subject to jumps; label-only (I15) */
} ppcp_timebase_kind;

typedef struct ppcp_timebase {
    ppcp_id            id;
    ppcp_timebase_kind kind;
    bool               epoch_stable;
    ppcp_duration_ns   resolution_ns;
    bool               has_origin;
    ppcp_id            origin;        /* CORE 5.3: opaque, MUST NOT be interpreted */
} ppcp_timebase;

PPCP_API ppcp_result ppcp_timebase_make(ppcp_timebase *out, const char *id, size_t id_len,
                                        ppcp_timebase_kind kind, bool epoch_stable,
                                        ppcp_duration_ns resolution_ns);
PPCP_API ppcp_result ppcp_timebase_set_origin(ppcp_timebase *tb, const char *origin, size_t len);
PPCP_API ppcp_result ppcp_timebase_validate(const ppcp_timebase *tb);

/* CORE 5.3b / I15 — no interval is ever computed from a `wall` timebase.  A
 * consumer asks before subtracting; L4 and L8 will refuse the computation. */
PPCP_API bool ppcp_timebase_is_wall(const ppcp_timebase *tb);

/* ------------------------------------------------------- TimebaseRelation */

typedef enum ppcp_relation_class {
    PPCP_REL_AFFINE = 0,
    PPCP_REL_UNRELATED       /* CORE 5.4b: a legal, complete declaration */
} ppcp_relation_class;

typedef enum ppcp_relation_method {
    PPCP_RELM_DECLARED = 0,
    PPCP_RELM_MEASURED,
    PPCP_RELM_ESTIMATED_ONLINE
} ppcp_relation_method;

typedef struct ppcp_timebase_relation {
    ppcp_id              from;
    ppcp_id              to;
    ppcp_relation_class  cls;
    /* affine only; CORE 5.4b requires `unrelated` to carry none of them */
    int64_t              offset_ns;
    double               skew_ppm;
    double               offset_sigma_ns;   /* CORE 5.4a: mandatory */
    double               skew_sigma_ppm;    /* CORE 5.4a: mandatory */
    ppcp_relation_method method;
    ppcp_instant         observed_at;       /* expressed in `from` */
    bool                 has_evidence_stream_id;
    ppcp_id              evidence_stream_id;
} ppcp_timebase_relation;

/* I3 made structural.  There is no other way to obtain an affine relation, and
 * this one cannot be called without both sigmas. */
PPCP_API ppcp_result ppcp_relation_make_affine(ppcp_timebase_relation *out,
                                               const char *from, const char *to,
                                               int64_t offset_ns, double skew_ppm,
                                               double offset_sigma_ns, double skew_sigma_ppm,
                                               ppcp_relation_method method,
                                               const ppcp_instant *observed_at);

PPCP_API ppcp_result ppcp_relation_make_unrelated(ppcp_timebase_relation *out,
                                                  const char *from, const char *to,
                                                  ppcp_relation_method method,
                                                  const ppcp_instant *observed_at);

PPCP_API ppcp_result ppcp_relation_validate(const ppcp_timebase_relation *r);

/* Applies one relation to one instant: value in `to` = value in `from` plus
 * the offset, corrected for skew over the elapsed time since `observed_at`.
 *
 * ⚠ This is not composition and does not become composition by being called
 * twice.  CORE 5.4c and I18 forbid deriving A→C from A→B and B→C, and there is
 * deliberately no function in this library that takes two relations.  A peer
 * needing A→C measures and declares it directly (CORE 5.4.1a, I21). */
PPCP_API ppcp_result ppcp_relation_apply(const ppcp_timebase_relation *r,
                                         const ppcp_instant *t_from,
                                         ppcp_instant *out_to);

/* ---------------------------------------------------- ClockDiscontinuity */

typedef struct ppcp_clock_discontinuity {
    ppcp_id      timebase_id;   /* the clock that stepped */
    ppcp_instant observed_at;   /* CORE 5.5b: in a timebase that did NOT step */
    int64_t      magnitude_ns;  /* signed */
    ppcp_id      cause;         /* open registry (CORE 10.3) */
} ppcp_clock_discontinuity;

PPCP_API ppcp_result ppcp_clock_discontinuity_make(ppcp_clock_discontinuity *out,
                                                   const char *timebase_id,
                                                   const ppcp_instant *observed_at,
                                                   int64_t magnitude_ns,
                                                   const char *cause);
PPCP_API ppcp_result ppcp_clock_discontinuity_validate(const ppcp_clock_discontinuity *d);

/* ------------------------------------------------------------- encode/decode */

PPCP_API ppcp_result ppcp_instant_encode(ppcp_cbor_writer *w, const ppcp_instant *in);
PPCP_API ppcp_result ppcp_instant_decode(ppcp_cbor_reader *r, ppcp_instant *out);
PPCP_API ppcp_result ppcp_series_encode(ppcp_cbor_writer *w, const ppcp_series *in);
PPCP_API ppcp_result ppcp_series_decode(ppcp_cbor_reader *r, ppcp_id *out_tb,
                                        int64_t *out_ns, size_t cap, size_t *out_count);
PPCP_API ppcp_result ppcp_interval_encode(ppcp_cbor_writer *w, const ppcp_interval *in);
PPCP_API ppcp_result ppcp_interval_decode(ppcp_cbor_reader *r, ppcp_interval *out);
PPCP_API ppcp_result ppcp_estimate_encode(ppcp_cbor_writer *w, const ppcp_estimate *in);
PPCP_API ppcp_result ppcp_estimate_decode(ppcp_cbor_reader *r, ppcp_estimate *out);
PPCP_API ppcp_result ppcp_timebase_encode(ppcp_cbor_writer *w, const ppcp_timebase *in);
PPCP_API ppcp_result ppcp_timebase_decode(ppcp_cbor_reader *r, ppcp_timebase *out);
PPCP_API ppcp_result ppcp_relation_encode(ppcp_cbor_writer *w, const ppcp_timebase_relation *in);
PPCP_API ppcp_result ppcp_relation_decode(ppcp_cbor_reader *r, ppcp_timebase_relation *out);
PPCP_API ppcp_result ppcp_clock_discontinuity_encode(ppcp_cbor_writer *w,
                                                     const ppcp_clock_discontinuity *in);
PPCP_API ppcp_result ppcp_clock_discontinuity_decode(ppcp_cbor_reader *r,
                                                     ppcp_clock_discontinuity *out);

/* ------------------------------------------------------------------- clock */

/* The embedding's clock.  It is handed a timebase id and returns the value of
 * that clock; the library never asks "what time is it" without saying which
 * clock it means, which is I1 one layer above the wire. */
typedef ppcp_result (*ppcp_clock_now_fn)(void *ctx, const char *timebase_id,
                                         int64_t *out_ns);

typedef struct ppcp_clock {
    ppcp_clock_now_fn now;
    void             *ctx;
} ppcp_clock;

/* Returns an Instant, never a number.  A caller that wanted a bare timestamp
 * would have to throw the `tb` away deliberately. */
PPCP_API ppcp_result ppcp_clock_read(const ppcp_clock *c, const char *timebase_id,
                                     ppcp_instant *out);

/* -------------------------------------------------- simulated clock (CONF 2a)
 *
 * Test infrastructure, in the library rather than in tests/, for two reasons:
 * it is pure arithmetic and so cannot break the sans-I/O rule, and both
 * application teams need the same one to test their own timing paths without
 * copying it between repositories (plan ground rule 1).
 *
 * Time advances only when ppcp_sim_clock_advance() is called.  Offset, skew and
 * discontinuity are injected, never waited for.
 */
typedef struct ppcp_sim_clock {
    ppcp_id  tb;
    int64_t  base_ns;      /* the value at zero elapsed */
    int64_t  elapsed_ns;   /* driven by ppcp_sim_clock_advance */
    int64_t  offset_ns;    /* a constant injected offset */
    double   skew_ppm;     /* rate error, parts per million */
    int64_t  step_ns;      /* accumulated injected discontinuity */
} ppcp_sim_clock;

PPCP_API ppcp_result ppcp_sim_clock_init(ppcp_sim_clock *c, const char *tb, int64_t base_ns);
PPCP_API void ppcp_sim_clock_set_offset(ppcp_sim_clock *c, int64_t offset_ns);
PPCP_API void ppcp_sim_clock_set_skew_ppm(ppcp_sim_clock *c, double skew_ppm);
PPCP_API void ppcp_sim_clock_advance(ppcp_sim_clock *c, ppcp_duration_ns d_ns);

/* CORE §6.4 — a step in the clock, injected.  Returns the ClockDiscontinuity a
 * conformant peer would report for it (5.5a), which needs a reference instant
 * in a timebase that did not step (5.5b) and so is supplied by the caller. */
PPCP_API ppcp_result ppcp_sim_clock_inject_discontinuity(ppcp_sim_clock *c,
                                                         int64_t magnitude_ns,
                                                         const ppcp_instant *observed_at,
                                                         const char *cause,
                                                         ppcp_clock_discontinuity *out);

PPCP_API ppcp_clock ppcp_sim_clock_interface(ppcp_sim_clock *c);

#ifdef __cplusplus
}
#endif
#endif /* PPCP_TIME_H */
