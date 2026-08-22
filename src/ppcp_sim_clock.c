/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * The simulated clock CONF 2a requires.
 *
 * "Offset, skew and discontinuity are simulated, not waited for.  Nothing in
 * CONF §3 that touches time is testable without it."  It advances only when
 * told to, so a test for a 20 ppm drift over ten minutes runs in microseconds
 * and is deterministic on every machine.
 *
 * It lives in the library rather than in tests/ because it is pure arithmetic —
 * it cannot break the sans-I/O rule — and because both application teams need
 * the same one, which ground rule 1 says belongs here rather than copied twice.
 */
#include "ppcp/time.h"

#include <string.h>

ppcp_result ppcp_sim_clock_init(ppcp_sim_clock *c, const char *tb, int64_t base_ns)
{
    ppcp_result rc;
    if (c == NULL)
        return PPCP_ERR_INVALID;
    memset(c, 0, sizeof(*c));
    rc = ppcp_id_set(&c->tb, tb, tb ? strlen(tb) : 0);
    if (rc != PPCP_OK)
        return rc;
    c->base_ns = base_ns;
    return PPCP_OK;
}

void ppcp_sim_clock_set_offset(ppcp_sim_clock *c, int64_t offset_ns)
{
    if (c != NULL)
        c->offset_ns = offset_ns;
}

void ppcp_sim_clock_set_skew_ppm(ppcp_sim_clock *c, double skew_ppm)
{
    if (c != NULL)
        c->skew_ppm = skew_ppm;
}

void ppcp_sim_clock_advance(ppcp_sim_clock *c, ppcp_duration_ns d_ns)
{
    if (c != NULL)
        c->elapsed_ns += d_ns;
}

ppcp_result ppcp_sim_clock_inject_discontinuity(ppcp_sim_clock *c, int64_t magnitude_ns,
                                                const ppcp_instant *observed_at,
                                                const char *cause,
                                                ppcp_clock_discontinuity *out)
{
    if (c == NULL)
        return PPCP_ERR_INVALID;
    c->step_ns += magnitude_ns;

    /* CORE 6.4b — a discontinuity is reported as an *observation*, not as a
     * property of the clock.  So the simulated step comes with the record a
     * conformant peer would emit for it (5.5a), including the reference
     * instant in a timebase that did not step (5.5b). */
    if (out == NULL)
        return PPCP_OK;
    return ppcp_clock_discontinuity_make(out, c->tb.v, observed_at, magnitude_ns,
                                         cause != NULL ? cause : "unknown");
}

static ppcp_result sim_now(void *ctx, const char *timebase_id, int64_t *out_ns)
{
    const ppcp_sim_clock *c = (const ppcp_sim_clock *)ctx;
    double  drift;
    int64_t d;

    if (c == NULL || out_ns == NULL || timebase_id == NULL)
        return PPCP_ERR_INVALID;
    /* The embedding's clock is asked which timebase it is being read in, and a
     * simulated one that answered for any id would hide exactly the mistake
     * I1 exists to catch. */
    if (!ppcp_cbor_key_is(timebase_id, strlen(timebase_id), c->tb.v))
        return PPCP_ERR_NOT_FOUND;

    drift = (double)c->elapsed_ns * c->skew_ppm * 1.0e-6;
    d = (drift >= 0.0) ? (int64_t)(drift + 0.5) : -(int64_t)(-drift + 0.5);

    *out_ns = c->base_ns + c->elapsed_ns + d + c->offset_ns + c->step_ns;
    return PPCP_OK;
}

ppcp_clock ppcp_sim_clock_interface(ppcp_sim_clock *c)
{
    ppcp_clock k;
    k.now = sim_now;
    k.ctx = c;
    return k;
}
