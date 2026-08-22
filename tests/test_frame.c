/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * PPCP-ENC §2, §3, §7 and §8's fatal/non-fatal split.
 */
#include "ppcp/frame.h"
#include "test_util.h"

static void test_header_round_trip(void)
{
    uint8_t           buf[PPCP_FRAME_HEADER_BYTES];
    ppcp_frame_header h, back;

    TEST("ENC §3 — eight big-endian bytes");
    h.payload_len = 87; h.channel = 0; h.flags = 0; h.reserved = 0;
    CHECK_EQ_I(ppcp_frame_header_write(buf, &h), PPCP_OK);
    CHECK_EQ_I(buf[0], 0x00); CHECK_EQ_I(buf[1], 0x00);
    CHECK_EQ_I(buf[2], 0x00); CHECK_EQ_I(buf[3], 0x57);
    CHECK_EQ_I(buf[4], 0x00); CHECK_EQ_I(buf[5], 0x00);
    CHECK_EQ_I(buf[6], 0x00); CHECK_EQ_I(buf[7], 0x00);
    CHECK_EQ_I(ppcp_frame_header_parse(buf, &back), PPCP_OK);
    CHECK_EQ_I(back.payload_len, 87);
    CHECK_EQ_I(back.channel, 0);
}

static void test_flags_and_reserved(void)
{
    uint8_t           raw[PPCP_FRAME_HEADER_BYTES] = { 0, 0, 0, 4, 1, 0x0f, 0xab, 0xcd };
    ppcp_frame_header h;

    TEST("ENC 3b — a receiver ignores unknown flag bits rather than failing");
    CHECK_EQ_I(ppcp_frame_header_parse(raw, &h), PPCP_OK);
    CHECK_EQ_I(h.flags, 0x0f);
    CHECK_EQ_I(h.reserved, 0xabcd);

    TEST("ENC 3b — but an encoder emits zero");
    h.payload_len = 4; h.channel = 1; h.flags = 1; h.reserved = 0;
    CHECK_EQ_I(ppcp_frame_header_write(raw, &h), PPCP_ERR_INVALID);
    h.flags = 0; h.reserved = 1;
    CHECK_EQ_I(ppcp_frame_header_write(raw, &h), PPCP_ERR_INVALID);
}

static void test_channels(void)
{
    TEST("ENC 2a — channel 0 is control, 1 and above are bulk, 255 is reserved");
    CHECK(!ppcp_channel_is_bulk(PPCP_CHANNEL_CONTROL));
    CHECK(ppcp_channel_is_bulk(1));
    CHECK(ppcp_channel_is_bulk(2));
    CHECK_EQ_I(ppcp_channel_validate(255), PPCP_ERR_MALFORMED);
    CHECK_EQ_I(ppcp_channel_validate(0), PPCP_OK);
    CHECK_EQ_I(ppcp_channel_frame_limit(0), PPCP_LIMIT_CONTROL_FRAME);
    CHECK_EQ_I(ppcp_channel_frame_limit(1), PPCP_LIMIT_BULK_FRAME);

    TEST("ENC 2c — the channel byte must match the stream it arrived on");
    CHECK_EQ_I(ppcp_frame_check_stream(0, 0), PPCP_OK);
    CHECK_EQ_I(ppcp_frame_check_stream(1, 0), PPCP_ERR_MALFORMED);
}

static void test_limits_are_fatal(void)
{
    uint8_t           raw[PPCP_FRAME_HEADER_BYTES];
    ppcp_frame_header h;

    /* ENC 8a — deliberately different from every other breach: a peer cannot
     * skip a frame whose length it cannot trust, so this is unrecoverable. */
    TEST("ENC 8a — a length past the channel limit is FATAL, not merely a limit");
    raw[0] = 0x00; raw[1] = 0x20; raw[2] = 0x00; raw[3] = 0x00;   /* 2 MiB */
    raw[4] = 0; raw[5] = 0; raw[6] = 0; raw[7] = 0;
    CHECK_EQ_I(ppcp_frame_header_parse(raw, &h), PPCP_ERR_FATAL_LIMIT);

    /* The same length on bulk is inside its 8 MiB limit. */
    raw[4] = 1;
    CHECK_EQ_I(ppcp_frame_header_parse(raw, &h), PPCP_OK);

    raw[0] = 0x00; raw[1] = 0x90; raw[2] = 0x00; raw[3] = 0x00;   /* 9 MiB */
    CHECK_EQ_I(ppcp_frame_header_parse(raw, &h), PPCP_ERR_FATAL_LIMIT);
}

static void test_truncation(void)
{
    uint8_t buf[16];
    ppcp_frame_header h;
    const uint8_t    *payload = NULL;
    size_t            consumed = 0, written = 0;
    const uint8_t     body[3] = { 0xa0, 0x01, 0x02 };

    TEST("ENC 3c — a short frame is TRUNCATED; what that means is the reader's call");
    CHECK_EQ_I(ppcp_frame_write(buf, sizeof(buf), 0, body, sizeof(body), &written), PPCP_OK);
    CHECK_EQ_I(written, 11);
    CHECK_EQ_I(ppcp_frame_read(buf, 4, &h, &payload, &consumed), PPCP_ERR_TRUNCATED);
    CHECK_EQ_I(ppcp_frame_read(buf, 10, &h, &payload, &consumed), PPCP_ERR_TRUNCATED);
    CHECK_EQ_I(ppcp_frame_read(buf, 11, &h, &payload, &consumed), PPCP_OK);
    CHECK_EQ_I(consumed, 11);
    CHECK_EQ_I(payload[0], 0xa0);
}

static void test_bundle_header(void)
{
    uint8_t            buf[PPCP_BUNDLE_HEADER_BYTES];
    ppcp_bundle_header h, back;

    TEST("ENC §7 — the bundle magic and version");
    h.major = 1; h.minor = 0; h.reserved = 0;
    CHECK_EQ_I(ppcp_bundle_header_write(buf, &h), PPCP_OK);
    CHECK(memcmp(buf, "PPCPBNDL", 8) == 0);
    CHECK_EQ_I(buf[8], 0); CHECK_EQ_I(buf[9], 1);
    CHECK_EQ_I(buf[10], 0); CHECK_EQ_I(buf[11], 0);
    CHECK_EQ_I(ppcp_bundle_header_parse(buf, &back), PPCP_OK);
    CHECK_EQ_I(back.major, 1);
    CHECK_EQ_I(back.minor, 0);

    TEST("ENC 7f — a higher minor is accepted; a different major is not");
    buf[11] = 7;
    CHECK_EQ_I(ppcp_bundle_header_parse(buf, &back), PPCP_OK);
    CHECK_EQ_I(back.minor, 7);
    buf[9] = 2;
    CHECK_EQ_I(ppcp_bundle_header_parse(buf, &back), PPCP_ERR_MALFORMED);

    TEST("a bundle without the magic is not a bundle");
    buf[0] = 'X';
    CHECK_EQ_I(ppcp_bundle_header_parse(buf, &back), PPCP_ERR_MALFORMED);
}

int main(void)
{
    test_header_round_trip();
    test_flags_and_reserved();
    test_channels();
    test_limits_are_fatal();
    test_truncation();
    test_bundle_header();
    TEST_MAIN_END();
}
