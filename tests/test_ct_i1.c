/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * CT-I1 — I1, Core, static.
 *
 * "No timestamp is encodable without a `tb`.  Attempt to emit an `Instant` with
 * a missing or empty `tb` and assert the encoder refuses.  Decode a stream
 * containing one and assert `malformed`."
 */
#include "ppcp/time.h"
#include "test_util.h"

static void assertion_encoder_refuses(void)
{
    ppcp_instant     t;
    ppcp_cbor_writer w;
    uint8_t          buf[64];

    TEST("CT-I1 — an Instant with an empty or missing tb cannot be constructed");
    CHECK_EQ_I(ppcp_instant_make(&t, "", 0, 1000), PPCP_ERR_INVALID);
    CHECK_EQ_I(ppcp_instant_make(&t, NULL, 0, 1000), PPCP_ERR_INVALID);
    CHECK_EQ_I(ppcp_instant_make_z(&t, "tb:device", 1000), PPCP_OK);

    TEST("CT-I1 — and one hand-zeroed cannot be encoded either");
    memset(&t, 0, sizeof(t));
    ppcp_cbor_writer_init(&w, buf, sizeof(buf));
    CHECK_EQ_I(ppcp_instant_encode(&w, &t), PPCP_ERR_INVALID);

    TEST("CT-I1 — a tb over 64 bytes is not an Id (CORE 5.1)");
    {
        char long_id[80];
        memset(long_id, 'x', sizeof(long_id));
        CHECK_EQ_I(ppcp_instant_make(&t, long_id, 65, 1), PPCP_ERR_INVALID);
        CHECK_EQ_I(ppcp_instant_make(&t, long_id, 64, 1), PPCP_OK);
    }
}

static void assertion_decoder_rejects(void)
{
    /* { "ns": 1000 } — an Instant with no tb. */
    static const uint8_t no_tb[] = { 0xa1, 0x62, 'n', 's', 0x19, 0x03, 0xe8 };
    /* { "ns": 1000, "tb": "" } — present but empty, which is not an Id. */
    static const uint8_t empty_tb[] = {
        0xa2, 0x62, 'n', 's', 0x19, 0x03, 0xe8, 0x62, 't', 'b', 0x60
    };
    /* A bare integer where an Instant belongs: ENC 4.1a says there is no
     * encoding for a bare timestamp anywhere in the protocol. */
    static const uint8_t bare[] = { 0x19, 0x03, 0xe8 };
    ppcp_cbor_reader r;
    ppcp_instant     t;

    TEST("CT-I1 — a stream carrying an Instant with no tb is malformed");
    ppcp_cbor_reader_init(&r, no_tb, sizeof(no_tb), ppcp_cbor_limits_for_channel(0));
    CHECK_EQ_I(ppcp_instant_decode(&r, &t), PPCP_ERR_MALFORMED);

    TEST("CT-I1 — an empty tb is malformed");
    ppcp_cbor_reader_init(&r, empty_tb, sizeof(empty_tb), ppcp_cbor_limits_for_channel(0));
    CHECK_EQ_I(ppcp_instant_decode(&r, &t), PPCP_ERR_MALFORMED);

    TEST("CT-I1 — a bare timestamp is not an Instant");
    ppcp_cbor_reader_init(&r, bare, sizeof(bare), ppcp_cbor_limits_for_channel(0));
    CHECK_EQ_I(ppcp_instant_decode(&r, &t), PPCP_ERR_MALFORMED);
}

static void assertion_series_and_interval_carry_tb(void)
{
    ppcp_series   s;
    ppcp_interval iv;
    static const int64_t ns[3] = { 1, 2, 3 };

    TEST("CT-I1 — a Series still carries tb, so I1 holds for many points too");
    CHECK_EQ_I(ppcp_series_make(&s, "", 0, ns, 3), PPCP_ERR_INVALID);
    CHECK_EQ_I(ppcp_series_make(&s, "tb:a", 4, ns, 3), PPCP_OK);

    TEST("CT-I1 — and an Interval");
    CHECK_EQ_I(ppcp_interval_make(&iv, "", 0, 0, 1), PPCP_ERR_INVALID);
    CHECK_EQ_I(ppcp_interval_make(&iv, "tb:a", 4, 0, 1), PPCP_OK);
    /* CORE 5.1: half-open [start, end), start <= end. */
    CHECK_EQ_I(ppcp_interval_make(&iv, "tb:a", 4, 2, 1), PPCP_ERR_INVALID);
}

static void assertion_round_trip(void)
{
    ppcp_instant     t, back;
    ppcp_cbor_writer w;
    ppcp_cbor_reader r;
    uint8_t          buf[64];
    size_t           n = 0;

    TEST("CT-I1 — a well-formed Instant round-trips");
    CHECK_EQ_I(ppcp_instant_make_z(&t, "tb:device", -1723000000000LL), PPCP_OK);
    ppcp_cbor_writer_init(&w, buf, sizeof(buf));
    CHECK_EQ_I(ppcp_instant_encode(&w, &t), PPCP_OK);
    CHECK_EQ_I(ppcp_cbor_writer_finish(&w, &n), PPCP_OK);
    ppcp_cbor_reader_init(&r, buf, n, ppcp_cbor_limits_for_channel(0));
    CHECK_EQ_I(ppcp_instant_decode(&r, &back), PPCP_OK);
    CHECK_EQ_I(back.ns, -1723000000000LL);
    CHECK(strcmp(back.tb.v, "tb:device") == 0);
}

int main(void)
{
    assertion_encoder_refuses();
    assertion_decoder_rejects();
    assertion_series_and_interval_carry_tb();
    assertion_round_trip();
    TEST_MAIN_END();
}
