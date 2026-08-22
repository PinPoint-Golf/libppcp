/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * CONF 2a — the injectable clock.  Offset, skew and discontinuity are
 * simulated, not waited for.
 */
#include "ppcp/time.h"
#include "test_util.h"

static void test_clock_returns_an_instant(void)
{
    ppcp_sim_clock c;
    ppcp_clock     k;
    ppcp_instant   t;

    TEST("I1 — the clock interface hands back an Instant, never a number");
    CHECK_EQ_I(ppcp_sim_clock_init(&c, "tb:device", 1000000000LL), PPCP_OK);
    k = ppcp_sim_clock_interface(&c);
    CHECK_EQ_I(ppcp_clock_read(&k, "tb:device", &t), PPCP_OK);
    CHECK(strcmp(t.tb.v, "tb:device") == 0);
    CHECK_EQ_I(t.ns, 1000000000LL);

    TEST("a clock asked for a timebase it does not keep says so");
    CHECK_EQ_I(ppcp_clock_read(&k, "tb:host", &t), PPCP_ERR_NOT_FOUND);
}

static void test_offset_and_skew(void)
{
    ppcp_sim_clock c;
    ppcp_clock     k;
    ppcp_instant   t;

    TEST("injected offset");
    CHECK_EQ_I(ppcp_sim_clock_init(&c, "tb:a", 0), PPCP_OK);
    k = ppcp_sim_clock_interface(&c);
    ppcp_sim_clock_set_offset(&c, 5000);
    CHECK_EQ_I(ppcp_clock_read(&k, "tb:a", &t), PPCP_OK);
    CHECK_EQ_I(t.ns, 5000);

    /* CORE §6.3: 20 ppm is the measured cross-device figure, about 1.2 ms per
     * minute — a full 150 fps frame every 5.5 minutes.  Sixty seconds of
     * simulated time, in no time at all. */
    TEST("injected skew — 20 ppm over one minute is 1.2 ms");
    CHECK_EQ_I(ppcp_sim_clock_init(&c, "tb:a", 0), PPCP_OK);
    k = ppcp_sim_clock_interface(&c);
    ppcp_sim_clock_set_skew_ppm(&c, 20.0);
    ppcp_sim_clock_advance(&c, 60LL * 1000000000LL);
    CHECK_EQ_I(ppcp_clock_read(&k, "tb:a", &t), PPCP_OK);
    CHECK_EQ_I(t.ns - 60LL * 1000000000LL, 1200000LL);
}

static void test_injected_discontinuity(void)
{
    ppcp_sim_clock           c;
    ppcp_clock               k;
    ppcp_instant             t, reference;
    ppcp_clock_discontinuity d;

    TEST("CORE 6.4 — an injected step, with the record a peer would report");
    CHECK_EQ_I(ppcp_sim_clock_init(&c, "tb:wall", 1000000000LL), PPCP_OK);
    k = ppcp_sim_clock_interface(&c);
    /* 5.5b: observed in a reference timebase that did NOT step. */
    CHECK_EQ_I(ppcp_instant_make_z(&reference, "tb:mono", 42), PPCP_OK);
    CHECK_EQ_I(ppcp_sim_clock_inject_discontinuity(&c, -250000000LL, &reference,
                                                   "ntp_correction", &d), PPCP_OK);
    CHECK_EQ_I(ppcp_clock_read(&k, "tb:wall", &t), PPCP_OK);
    CHECK_EQ_I(t.ns, 750000000LL);
    CHECK_EQ_I(d.magnitude_ns, -250000000LL);
    CHECK(strcmp(d.timebase_id.v, "tb:wall") == 0);
    CHECK(strcmp(d.cause.v, "ntp_correction") == 0);
    CHECK(strcmp(d.observed_at.tb.v, "tb:mono") == 0);

    TEST("CORE 5.5b — observed_at may not be in the clock that stepped");
    {
        ppcp_instant bad;
        ppcp_clock_discontinuity dd;
        CHECK_EQ_I(ppcp_instant_make_z(&bad, "tb:wall", 42), PPCP_OK);
        CHECK_EQ_I(ppcp_clock_discontinuity_make(&dd, "tb:wall", &bad, 1, "sleep"),
                   PPCP_ERR_INVALID);
    }
}

static void test_discontinuity_round_trip(void)
{
    ppcp_clock_discontinuity d, back;
    ppcp_instant             at;
    ppcp_cbor_writer         w;
    ppcp_cbor_reader         r;
    uint8_t                  buf[128];
    size_t                   n = 0;

    TEST("ClockDiscontinuity round-trips, with an open-registry cause");
    CHECK_EQ_I(ppcp_instant_make_z(&at, "tb:mono", 12345), PPCP_OK);
    /* CORE 10.3b — a vendor value is reverse-DNS namespaced and passes. */
    CHECK_EQ_I(ppcp_clock_discontinuity_make(&d, "tb:wall", &at, -7,
                                             "com.example.leap"), PPCP_OK);
    ppcp_cbor_writer_init(&w, buf, sizeof(buf));
    CHECK_EQ_I(ppcp_clock_discontinuity_encode(&w, &d), PPCP_OK);
    CHECK_EQ_I(ppcp_cbor_writer_finish(&w, &n), PPCP_OK);

    ppcp_cbor_reader_init(&r, buf, n, ppcp_cbor_limits_for_channel(0));
    CHECK_EQ_I(ppcp_clock_discontinuity_decode(&r, &back), PPCP_OK);
    CHECK_EQ_I(back.magnitude_ns, -7);
    CHECK(strcmp(back.cause.v, "com.example.leap") == 0);
}

static void test_wall_is_label_only(void)
{
    ppcp_timebase tb;

    TEST("CORE 6.5a / I15 — a wall timebase is identifiable as one");
    CHECK_EQ_I(ppcp_timebase_make(&tb, "tb:wall", 7, PPCP_TB_WALL, false, 1000), PPCP_OK);
    CHECK(ppcp_timebase_is_wall(&tb));
    CHECK_EQ_I(ppcp_timebase_make(&tb, "tb:mono", 7, PPCP_TB_MONOTONIC, true, 1), PPCP_OK);
    CHECK(!ppcp_timebase_is_wall(&tb));
}

int main(void)
{
    test_clock_returns_an_instant();
    test_offset_and_skew();
    test_injected_discontinuity();
    test_discontinuity_round_trip();
    test_wall_is_label_only();
    TEST_MAIN_END();
}
