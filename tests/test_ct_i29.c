/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * CT-I29 — I29, Detect, static.  The Estimate half.
 *
 * "A `Candidate.tof_correction` carrying `value_ns` without `sigma_ns`, or the
 * reverse, is rejected as malformed and is not constructible for emission."
 *
 * `Candidate` itself lands in L4/L10.  What is testable now is the type the
 * invariant is made of: ENC 4.1e says an Estimate carries both keys, and that
 * an encoder cannot emit a value without its sigma — "which is I29 and I3 made
 * structural in the same way `Instant` makes I1 structural".
 */
#include "ppcp/time.h"
#include "test_util.h"

static void assertion_both_or_neither(void)
{
    ppcp_estimate e;

    TEST("CT-I29 — there is no constructor that takes a value alone");
    CHECK_EQ_I(ppcp_estimate_make(&e, 250000, 40.0), PPCP_OK);
    CHECK_EQ_I(e.value_ns, 250000);

    TEST("CT-I29 — a sigma must be a non-negative real (CORE 5.1)");
    CHECK_EQ_I(ppcp_estimate_make(&e, 250000, -1.0), PPCP_ERR_INVALID);
    {
        /* NaN, built without math.h so this test carries no header the library
         * would not.  A dispersion that is not a number is the "point estimate
         * with no dispersion" wearing a number's clothes. */
        double inf = 1e308 * 10.0;
        double nan = inf - inf;
        CHECK_EQ_I(ppcp_estimate_make(&e, 250000, nan), PPCP_ERR_INVALID);
        CHECK_EQ_I(ppcp_estimate_make(&e, 250000, inf), PPCP_ERR_INVALID);
    }
    CHECK_EQ_I(ppcp_estimate_make(&e, 250000, 0.0), PPCP_OK);   /* zero is a sigma */
}

static void assertion_value_without_sigma_is_malformed(void)
{
    /* { "value_ns": 250000 } */
    static const uint8_t value_only[] = {
        0xa1, 0x68, 'v','a','l','u','e','_','n','s', 0x1a, 0x00, 0x03, 0xd0, 0x90
    };
    /* { "sigma_ns": 40.0 } */
    static const uint8_t sigma_only[] = {
        0xa1, 0x68, 's','i','g','m','a','_','n','s',
        0xfb, 0x40, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    ppcp_cbor_reader r;
    ppcp_estimate    e;

    TEST("CT-I29 — value_ns without sigma_ns is malformed on receipt");
    ppcp_cbor_reader_init(&r, value_only, sizeof(value_only),
                          ppcp_cbor_limits_for_channel(0));
    CHECK_EQ_I(ppcp_estimate_decode(&r, &e), PPCP_ERR_MALFORMED);

    TEST("CT-I29 — and the reverse");
    ppcp_cbor_reader_init(&r, sigma_only, sizeof(sigma_only),
                          ppcp_cbor_limits_for_channel(0));
    CHECK_EQ_I(ppcp_estimate_decode(&r, &e), PPCP_ERR_MALFORMED);
}

static void assertion_round_trip(void)
{
    ppcp_estimate    e, back;
    ppcp_cbor_writer w;
    ppcp_cbor_reader r;
    uint8_t          buf[64];
    size_t           n = 0;

    TEST("CT-I29 — a complete Estimate round-trips, both keys present");
    CHECK_EQ_I(ppcp_estimate_make(&e, -1250, 12.5), PPCP_OK);
    ppcp_cbor_writer_init(&w, buf, sizeof(buf));
    CHECK_EQ_I(ppcp_estimate_encode(&w, &e), PPCP_OK);
    CHECK_EQ_I(ppcp_cbor_writer_finish(&w, &n), PPCP_OK);
    /* map(2), and the encoder wrote both keys or it wrote nothing. */
    CHECK_EQ_I(buf[0], 0xa2);
    ppcp_cbor_reader_init(&r, buf, n, ppcp_cbor_limits_for_channel(0));
    CHECK_EQ_I(ppcp_estimate_decode(&r, &back), PPCP_OK);
    CHECK_EQ_I(back.value_ns, -1250);
    CHECK(back.sigma_ns == 12.5);

    TEST("CT-I29 — a half-precision sigma is accepted (ENC §4)");
    {
        static const uint8_t half_sigma[] = {
            0xa2,
            0x68, 's','i','g','m','a','_','n','s', 0xf9, 0x4a, 0x40,   /* 12.5 */
            0x68, 'v','a','l','u','e','_','n','s', 0x18, 0x2a
        };
        ppcp_cbor_reader_init(&r, half_sigma, sizeof(half_sigma),
                              ppcp_cbor_limits_for_channel(0));
        CHECK_EQ_I(ppcp_estimate_decode(&r, &back), PPCP_OK);
        CHECK(back.sigma_ns == 12.5);
        CHECK_EQ_I(back.value_ns, 42);
    }
}

int main(void)
{
    assertion_both_or_neither();
    assertion_value_without_sigma_is_malformed();
    assertion_round_trip();
    TEST_MAIN_END();
}
