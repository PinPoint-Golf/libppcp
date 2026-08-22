/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * CT-S1 — the canonical instant conversion.  Invariants I17 and I22.
 * Also CT-I17, which CONF §3 defines as "see CT-S1".
 *
 * CONF §4.1's six assertions, in order, plus the four worked examples of
 * CORE §6.1.1 that assertion 1 names.
 *
 * "Assertion 2 is the whole test.  The other four are why it is worth writing
 * carefully."
 */
#include "ppcp/timing.h"
#include "test_util.h"

/* ---------------------------------------------------------- assertion 1 */

static void worked_examples(void)
{
    ppcp_timing t;
    int64_t     out = 0;

    /* A — AVFoundation-style device.  convention: nominal_frame_start,
     * offset 120000, t = 1000000000, d = 2000000. */
    TEST("CORE 6.1.1 example A");
    CHECK_EQ_I(ppcp_timing_make_nominal_frame_start(&t, 120000, PPCP_PROV_ASSUMED),
               PPCP_OK);
    CHECK_EQ_I(ppcp_canonical_instant(&t, 1000000000LL, 2000000LL, &out), PPCP_OK);
    CHECK_EQ_I(out, 1001120000LL);

    /* B — FLIR-style host camera.  convention: start, t = 1000000000,
     * d = 500000. */
    TEST("CORE 6.1.1 example B");
    CHECK_EQ_I(ppcp_timing_make(&t, PPCP_CONV_START), PPCP_OK);
    CHECK_EQ_I(ppcp_canonical_instant(&t, 1000000000LL, 500000LL, &out), PPCP_OK);
    CHECK_EQ_I(out, 1000250000LL);

    /* C — end convention. */
    TEST("CORE 6.1.1 example C");
    CHECK_EQ_I(ppcp_timing_make(&t, PPCP_CONV_END), PPCP_OK);
    CHECK_EQ_I(ppcp_canonical_instant(&t, 1000000000LL, 500000LL, &out), PPCP_OK);
    CHECK_EQ_I(out, 999750000LL);

    /* D — the cost of skipping the conversion.  Both timestamps are already in
     * a common timebase and both nominally 1 s; raw they differ by 0, canonical
     * by 870000 ns — 13% of a frame at 150 fps, and it MOVES with exposure,
     * which is what makes it indistinguishable from clock drift. */
    TEST("CORE 6.1.1 example D — 870000 ns of systematic error");
    {
        int64_t     a = 0, b = 0;
        ppcp_timing ta, tb;
        CHECK_EQ_I(ppcp_timing_make_nominal_frame_start(&ta, 120000, PPCP_PROV_ASSUMED),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_timing_make(&tb, PPCP_CONV_START), PPCP_OK);
        CHECK_EQ_I(ppcp_canonical_instant(&ta, 1000000000LL, 2000000LL, &a), PPCP_OK);
        CHECK_EQ_I(ppcp_canonical_instant(&tb, 1000000000LL, 500000LL, &b), PPCP_OK);
        CHECK_EQ_I(a - b, 870000LL);
    }

    /* 6.1d — a non-framed source: the canonical instant is t and the
     * convention MUST be mid. */
    TEST("CORE 6.1d — mid is the identity");
    CHECK_EQ_I(ppcp_timing_make(&t, PPCP_CONV_MID), PPCP_OK);
    CHECK_EQ_I(ppcp_canonical_instant(&t, 1723000000000LL, 500000LL, &out), PPCP_OK);
    CHECK_EQ_I(out, 1723000000000LL);
}

/* ---------------------------------------------------------- assertion 2 */

static void assertion_2_offset_is_applied(void)
{
    ppcp_timing zero, present;
    int64_t     a = 0, b = 0;

    /* "Setting frame_start_to_exposure_offset_ns to 0 and to 120000 produces
     * outputs differing by exactly 120000 ns.  An implementation that ignores
     * the field passes every other test in this suite." */
    TEST("CT-S1 assertion 2 — offset 0 and 120000 differ by exactly 120000");
    CHECK_EQ_I(ppcp_timing_make_nominal_frame_start(&zero, 0, PPCP_PROV_ASSUMED),
               PPCP_OK);
    CHECK_EQ_I(ppcp_timing_make_nominal_frame_start(&present, 120000, PPCP_PROV_ASSUMED),
               PPCP_OK);
    CHECK_EQ_I(ppcp_canonical_instant(&zero, 1000000000LL, 2000000LL, &a), PPCP_OK);
    CHECK_EQ_I(ppcp_canonical_instant(&present, 1000000000LL, 2000000LL, &b), PPCP_OK);
    CHECK_EQ_I(b - a, 120000LL);

    /* The exposure is varied to prove the difference is the offset and not an
     * artefact of one exposure value. */
    CHECK_EQ_I(ppcp_canonical_instant(&zero, 7, 999999LL, &a), PPCP_OK);
    CHECK_EQ_I(ppcp_canonical_instant(&present, 7, 999999LL, &b), PPCP_OK);
    CHECK_EQ_I(b - a, 120000LL);

    /* I22, both directions.  A declared zero is accepted and a defaulted zero
     * is not producible: the offset is a parameter of the only constructor
     * that yields this convention. */
    TEST("CT-S1 / CT-I22 — the offset exists iff the convention is nominal_frame_start");
    {
        ppcp_timing t;
        CHECK_EQ_I(ppcp_timing_make(&t, PPCP_CONV_NOMINAL_FRAME_START), PPCP_ERR_INVALID);
        CHECK_EQ_I(ppcp_timing_make(&t, PPCP_CONV_START), PPCP_OK);
        CHECK(!t.has_offset);
        /* A hand-built profile that sets an offset on `start` is malformed. */
        t.has_offset = true;
        t.frame_start_to_exposure_offset_ns = 120000;
        CHECK_EQ_I(ppcp_timing_validate(&t), PPCP_ERR_MALFORMED);
        /* And one claiming nominal_frame_start with no offset is malformed. */
        memset(&t, 0, sizeof(t));
        t.convention = PPCP_CONV_NOMINAL_FRAME_START;
        CHECK_EQ_I(ppcp_timing_validate(&t), PPCP_ERR_MALFORMED);
    }

    /* I31 / plan A12 — the offset never travels without its provenance, and
     * "assumed" is what an implementation that has not been through a timecode
     * rig must declare. */
    TEST("CT-S1 / CT-I31 — the offset carries provenance by construction");
    CHECK_EQ_I(zero.offset_provenance, PPCP_PROV_ASSUMED);
    {
        ppcp_timing t;
        CHECK_EQ_I(ppcp_timing_make_nominal_frame_start(&t, 0, (ppcp_provenance)99),
                   PPCP_ERR_INVALID);
    }
}

/* ---------------------------------------------------------- assertion 3 */

static void assertion_3_per_frame_exposure(void)
{
    ppcp_timing t;
    int64_t     a = 0, b = 0;

    /* "Doubling every exposure duration changes the converted instants; an
     * implementation using the profile's exposure *range* rather than the
     * per-frame value fails here." (6.1c) */
    TEST("CT-S1 assertion 3 — doubling the exposure changes the result");
    CHECK_EQ_I(ppcp_timing_make_nominal_frame_start(&t, 120000, PPCP_PROV_ASSUMED),
               PPCP_OK);
    CHECK_EQ_I(ppcp_canonical_instant(&t, 1000000000LL, 2000000LL, &a), PPCP_OK);
    CHECK_EQ_I(ppcp_canonical_instant(&t, 1000000000LL, 4000000LL, &b), PPCP_OK);
    CHECK(a != b);
    CHECK_EQ_I(b - a, 1000000LL);      /* exactly half the added exposure */

    CHECK_EQ_I(ppcp_timing_make(&t, PPCP_CONV_START), PPCP_OK);
    CHECK_EQ_I(ppcp_canonical_instant(&t, 0, 500000LL, &a), PPCP_OK);
    CHECK_EQ_I(ppcp_canonical_instant(&t, 0, 1000000LL, &b), PPCP_OK);
    CHECK_EQ_I(b - a, 250000LL);
}

/* ---------------------------------------------------------- assertion 4 */

static void assertion_4_round_trip(void)
{
    static const int64_t exposures[] = {
        0, 1, 2, 3, 999, 1000, 500000, 2000000, 8333333, 16666667
    };
    static const int64_t stamps[] = {
        0, 1, -1, 1000000000LL, -1723000000000LL, 1723000000000LL
    };
    ppcp_timing conventions[4];
    unsigned    c, i, j;

    TEST("CT-S1 assertion 4 — canonical and back recovers the original bit-for-bit");
    CHECK_EQ_I(ppcp_timing_make(&conventions[0], PPCP_CONV_MID), PPCP_OK);
    CHECK_EQ_I(ppcp_timing_make(&conventions[1], PPCP_CONV_START), PPCP_OK);
    CHECK_EQ_I(ppcp_timing_make(&conventions[2], PPCP_CONV_END), PPCP_OK);
    CHECK_EQ_I(ppcp_timing_make_nominal_frame_start(&conventions[3], -37500,
                                                    PPCP_PROV_MEASURED), PPCP_OK);

    for (c = 0; c < 4; c++) {
        for (i = 0; i < sizeof(stamps) / sizeof(stamps[0]); i++) {
            for (j = 0; j < sizeof(exposures) / sizeof(exposures[0]); j++) {
                int64_t canonical = 0, back = 0;
                CHECK_EQ_I(ppcp_canonical_instant(&conventions[c], stamps[i],
                                                  exposures[j], &canonical), PPCP_OK);
                CHECK_EQ_I(ppcp_canonical_instant_inverse(&conventions[c], canonical,
                                                          exposures[j], &back), PPCP_OK);
                CHECK_EQ_I(back, stamps[i]);
            }
        }
    }

    TEST("CT-S1 — an exposure is a duration, so a negative one is a caller bug");
    {
        int64_t out = 0;
        CHECK_EQ_I(ppcp_canonical_instant(&conventions[1], 0, -1, &out), PPCP_ERR_INVALID);
    }
}

/* ---------------------------------------------------------- assertion 5 */

static void assertion_5_rolling_shutter(void)
{
    ppcp_geometry g;
    int64_t       out = 0;
    const int64_t first = 1000000000LL;
    const int64_t readout = 10000000LL;     /* 10 ms */
    const uint32_t R = 1081;                /* R - 1 = 1080, so the maths is exact */

    /* "A rolling-shutter profile's row-r instants match CORE 6.2d under BOTH
     * top_to_bottom and bottom_to_top, including the R == 1 case." */
    TEST("CT-S1 assertion 5 — top_to_bottom");
    CHECK_EQ_I(ppcp_geometry_make_rolling_shutter(&g, readout, PPCP_PROV_ASSUMED,
                                                  PPCP_ROLL_TOP_TO_BOTTOM, R), PPCP_OK);
    CHECK_EQ_I(ppcp_row_instant(&g, first, 0, &out), PPCP_OK);
    CHECK_EQ_I(out, first);                                   /* first row read */
    CHECK_EQ_I(ppcp_row_instant(&g, first, R - 1, &out), PPCP_OK);
    CHECK_EQ_I(out, first + readout);                         /* last row read */
    CHECK_EQ_I(ppcp_row_instant(&g, first, 540, &out), PPCP_OK);
    CHECK_EQ_I(out, first + readout / 2);                     /* halfway */

    TEST("CT-S1 assertion 5 — bottom_to_top reverses it");
    CHECK_EQ_I(ppcp_geometry_make_rolling_shutter(&g, readout, PPCP_PROV_ASSUMED,
                                                  PPCP_ROLL_BOTTOM_TO_TOP, R), PPCP_OK);
    /* Row 0 is the TOP of the delivered image, which under bottom_to_top is
     * the LAST row read — so it carries the full readout, not zero.  A
     * top_to_bottom implementation reused for both directions fails here. */
    CHECK_EQ_I(ppcp_row_instant(&g, first, 0, &out), PPCP_OK);
    CHECK_EQ_I(out, first + readout);
    CHECK_EQ_I(ppcp_row_instant(&g, first, R - 1, &out), PPCP_OK);
    CHECK_EQ_I(out, first);
    CHECK_EQ_I(ppcp_row_instant(&g, first, 540, &out), PPCP_OK);
    CHECK_EQ_I(out, first + readout / 2);

    TEST("CT-S1 assertion 5 — R == 1, where the general formula divides by zero");
    CHECK_EQ_I(ppcp_geometry_make_rolling_shutter(&g, readout, PPCP_PROV_ASSUMED,
                                                  PPCP_ROLL_TOP_TO_BOTTOM, 1), PPCP_OK);
    CHECK_EQ_I(ppcp_row_instant(&g, first, 0, &out), PPCP_OK);
    CHECK_EQ_I(out, first);
    CHECK_EQ_I(ppcp_geometry_make_rolling_shutter(&g, readout, PPCP_PROV_ASSUMED,
                                                  PPCP_ROLL_BOTTOM_TO_TOP, 1), PPCP_OK);
    CHECK_EQ_I(ppcp_row_instant(&g, first, 0, &out), PPCP_OK);
    CHECK_EQ_I(out, first);
    /* A row that does not exist is not a row. */
    CHECK_EQ_I(ppcp_row_instant(&g, first, 1, &out), PPCP_ERR_INVALID);

    TEST("CT-S1 assertion 5 — global geometry: every row shares the frame's instant");
    CHECK_EQ_I(ppcp_geometry_make_global(&g), PPCP_OK);
    CHECK_EQ_I(ppcp_row_instant(&g, first, 0, &out), PPCP_OK);
    CHECK_EQ_I(out, first);
    CHECK_EQ_I(ppcp_row_instant(&g, first, 4096, &out), PPCP_OK);
    CHECK_EQ_I(out, first);

    /* 5.7e / I31 — readout_ns carries provenance on the same terms as the
     * exposure offset, because no public platform API exposes it. */
    TEST("CT-S1 / CT-I31 — readout_ns cannot be declared without provenance");
    CHECK_EQ_I(ppcp_geometry_make_rolling_shutter(&g, readout, (ppcp_provenance)7,
                                                  PPCP_ROLL_TOP_TO_BOTTOM, R),
               PPCP_ERR_INVALID);
    CHECK_EQ_I(ppcp_geometry_make_rolling_shutter(&g, readout, PPCP_PROV_ASSUMED,
                                                  PPCP_ROLL_TOP_TO_BOTTOM, 0),
               PPCP_ERR_INVALID);
}

/* ---------------------------------------------------------- assertion 6 */

static void assertion_6_scalar_equals_constant_array(void)
{
    static const int64_t frames[5] = {
        1000000000LL, 1006666667LL, 1013333334LL, 1020000001LL, 1026666668LL
    };
    static int64_t constant_array[5];
    ppcp_achieved_frames scalar_form, array_form;
    ppcp_per_frame_i64   pf;
    ppcp_timing          t;
    unsigned             i;

    /* "The scalar form and an equivalent constant array produce identical
     * canonical instants.  The shipping application locks exposure, so the
     * scalar path is the one the product uses; a conversion test that
     * exercises only the varying-exposure path does not test what ships." */
    TEST("CT-S1 assertion 6 — scalar and constant array agree exactly");
    for (i = 0; i < 5; i++)
        constant_array[i] = 4000000LL;

    CHECK_EQ_I(ppcp_timing_make_nominal_frame_start(&t, 120000, PPCP_PROV_ASSUMED),
               PPCP_OK);

    CHECK_EQ_I(ppcp_achieved_frames_make(&scalar_form, "tb:device", frames, 5), PPCP_OK);
    CHECK_EQ_I(ppcp_per_frame_i64_scalar(&pf, 4000000LL), PPCP_OK);
    /* 5.8: locked_constant goes with the scalar form, and only under a lock. */
    CHECK_EQ_I(ppcp_achieved_frames_set_exposure(&scalar_form, &pf,
                                                 PPCP_EXP_LOCKED_CONSTANT), PPCP_OK);

    CHECK_EQ_I(ppcp_achieved_frames_make(&array_form, "tb:device", frames, 5), PPCP_OK);
    CHECK_EQ_I(ppcp_per_frame_i64_array(&pf, constant_array, 5), PPCP_OK);
    CHECK_EQ_I(ppcp_achieved_frames_set_exposure(&array_form, &pf,
                                                 PPCP_EXP_PER_FRAME), PPCP_OK);

    for (i = 0; i < 5; i++) {
        ppcp_instant a, b;
        CHECK_EQ_I(ppcp_achieved_frames_canonical_at(&scalar_form, &t, i, &a), PPCP_OK);
        CHECK_EQ_I(ppcp_achieved_frames_canonical_at(&array_form, &t, i, &b), PPCP_OK);
        CHECK_EQ_I(a.ns, b.ns);
        CHECK(strcmp(a.tb.v, b.tb.v) == 0);
        CHECK_EQ_I(a.ns, frames[i] + 120000LL + 2000000LL);
    }

    TEST("CT-S1 — a varying exposure array gives a different answer per frame");
    {
        static int64_t varying[5] = { 1000000, 2000000, 3000000, 4000000, 5000000 };
        ppcp_achieved_frames v;
        CHECK_EQ_I(ppcp_achieved_frames_make(&v, "tb:device", frames, 5), PPCP_OK);
        CHECK_EQ_I(ppcp_per_frame_i64_array(&pf, varying, 5), PPCP_OK);
        CHECK_EQ_I(ppcp_achieved_frames_set_exposure(&v, &pf, PPCP_EXP_PER_FRAME),
                   PPCP_OK);
        for (i = 0; i < 5; i++) {
            ppcp_instant a;
            CHECK_EQ_I(ppcp_achieved_frames_canonical_at(&v, &t, i, &a), PPCP_OK);
            CHECK_EQ_I(a.ns, frames[i] + 120000LL + varying[i] / 2);
        }
    }

    TEST("CT-S1 — a short parallel array is malformed (ENC 4.1c, CORE 5.8f)");
    {
        ppcp_achieved_frames bad;
        CHECK_EQ_I(ppcp_achieved_frames_make(&bad, "tb:device", frames, 5), PPCP_OK);
        CHECK_EQ_I(ppcp_per_frame_i64_array(&pf, constant_array, 3), PPCP_OK);
        CHECK_EQ_I(ppcp_achieved_frames_set_exposure(&bad, &pf, PPCP_EXP_PER_FRAME),
                   PPCP_ERR_INVALID);
    }

    TEST("CT-S1 / I17 — without exposure the conversion fails rather than guessing");
    {
        ppcp_achieved_frames none;
        ppcp_instant         a;
        CHECK_EQ_I(ppcp_achieved_frames_make(&none, "tb:device", frames, 5), PPCP_OK);
        CHECK_EQ_I(ppcp_achieved_frames_canonical_at(&none, &t, 0, &a),
                   PPCP_ERR_NOT_FOUND);
    }
}

static void the_instant_keeps_its_timebase(void)
{
    ppcp_timing  t;
    ppcp_instant raw, out;

    /* The conversion corrects where in the exposure the timestamp points; it
     * does not move it to another clock.  I1 holds through it. */
    TEST("CT-S1 — the canonical instant is still an Instant, on the same timebase");
    CHECK_EQ_I(ppcp_timing_make_nominal_frame_start(&t, 120000, PPCP_PROV_ASSUMED),
               PPCP_OK);
    CHECK_EQ_I(ppcp_instant_make_z(&raw, "tb:device", 1000000000LL), PPCP_OK);
    CHECK_EQ_I(ppcp_canonical_instant_of(&t, &raw, 2000000LL, &out), PPCP_OK);
    CHECK_EQ_I(out.ns, 1001120000LL);
    CHECK(strcmp(out.tb.v, "tb:device") == 0);
}

int main(void)
{
    worked_examples();
    assertion_2_offset_is_applied();
    assertion_3_per_frame_exposure();
    assertion_4_round_trip();
    assertion_5_rolling_shutter();
    assertion_6_scalar_equals_constant_array();
    the_instant_keeps_its_timebase();
    TEST_MAIN_END();
}
