/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * PPCP-ENC §5, and the worked example of §5.1 reproduced byte-for-byte.
 */
#include "ppcp/envelope.h"
#include "ppcp/frame.h"
#include "ppcp/time.h"
#include "test_util.h"

/* The 95 bytes of ENC §5.1, transcribed from the document: an 8-byte frame
 * header and 87 bytes of CBOR. */
static const char ENC_5_1_HEX[] =
    "00 00 00 57"                       /* payload_len = 87 */
    "00"                                /* channel 0 */
    "00 00 00"                          /* flags, reserved */
    "a5"                                /* map(5) */
    "64 74 79 70 65"                    /* "type" */
    "6a 73 79 6e 63 5f 70 72 6f 62 65"  /* "sync_probe" */
    "66 6d 73 67 5f 69 64"              /* "msg_id" */
    "07"                                /* 7 */
    "69 70 72 6f 62 65 5f 73 65 71"     /* "probe_seq" */
    "03"                                /* 3 */
    "6b 74 69 6d 65 62 61 73 65 5f 69 64" /* "timebase_id" */
    "69 74 62 3a 64 65 76 69 63 65"     /* "tb:device" */
    "62 74 31"                          /* "t1" */
    "a2"                                /* map(2) */
    "62 74 62"                          /* "tb" */
    "69 74 62 3a 64 65 76 69 63 65"     /* "tb:device" */
    "62 6e 73"                          /* "ns" */
    "1b 00 00 01 91 2a cd 8e 00";       /* 1723000000000 */

/* The body of the §5.1 sync_probe, written in the document's own key order.
 * ⚠ That order is not RFC 8949 §4.2.1 deterministic — "t1" sorts before
 * "type" — so this body is written through the LITERAL writer.  The same
 * message written deterministically is asserted separately below. */
static ppcp_result probe_body_literal(ppcp_cbor_writer *w, ppcp_envelope_writer *ew,
                                      void *ctx)
{
    (void)ew; (void)ctx;
    if (ppcp_cbor_write_text_z(w, "probe_seq") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_uint(w, 3) != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, "timebase_id") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, "tb:device") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, "t1") != PPCP_OK) return ppcp_cbor_writer_status(w);
    /* The Instant is written inline in the document's order — "tb" then "ns" —
     * which ppcp_instant_encode() would not produce, because it sorts "ns"
     * first.  The same defect, one level down. */
    if (ppcp_cbor_write_map(w, 2) != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, "tb") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, "tb:device") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_text_z(w, "ns") != PPCP_OK) return ppcp_cbor_writer_status(w);
    return ppcp_cbor_write_int(w, 1723000000000LL);
}

/* The same message, deterministically encoded: "t1" first, then "type",
 * "msg_id", "probe_seq", "timebase_id" — and "ns" before "tb" inside t1. */
static ppcp_result probe_body_deterministic(ppcp_cbor_writer *w, ppcp_envelope_writer *ew,
                                            void *ctx)
{
    ppcp_instant t1;
    (void)ctx;

    if (ppcp_instant_make_z(&t1, "tb:device", 1723000000000LL) != PPCP_OK)
        return PPCP_ERR_INVALID;

    if (ppcp_envelope_before(w, ew, "t1", 2) != PPCP_OK) return PPCP_ERR_INVALID;
    if (ppcp_cbor_write_text_z(w, "t1") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_instant_encode(w, &t1) != PPCP_OK) return ppcp_cbor_writer_status(w);

    if (ppcp_envelope_before(w, ew, "probe_seq", 9) != PPCP_OK) return PPCP_ERR_INVALID;
    if (ppcp_cbor_write_text_z(w, "probe_seq") != PPCP_OK) return ppcp_cbor_writer_status(w);
    if (ppcp_cbor_write_uint(w, 3) != PPCP_OK) return ppcp_cbor_writer_status(w);

    if (ppcp_envelope_before(w, ew, "timebase_id", 11) != PPCP_OK) return PPCP_ERR_INVALID;
    if (ppcp_cbor_write_text_z(w, "timebase_id") != PPCP_OK) return ppcp_cbor_writer_status(w);
    return ppcp_cbor_write_text_z(w, "tb:device");
}

static void test_enc_5_1_worked_example(void)
{
    unsigned char want[128];
    size_t        want_len = ppcp_unhex(ENC_5_1_HEX, want, sizeof(want));
    unsigned char got[128];
    size_t        got_len = 0;
    ppcp_envelope e;

    TEST("ENC 5.1 worked example");
    CHECK_EQ_I(want_len, 95);

    CHECK_EQ_I(ppcp_envelope_init(&e, "sync_probe", 7), PPCP_OK);
    CHECK_EQ_I(ppcp_message_encode_literal(got, sizeof(got), PPCP_CHANNEL_CONTROL,
                                           &e, 3, probe_body_literal, NULL, &got_len),
               PPCP_OK);
    CHECK_BYTES(got, got_len, want, want_len);

    /* And the header the document annotates. */
    {
        ppcp_frame_header h;
        const uint8_t    *payload = NULL;
        size_t            consumed = 0;
        CHECK_EQ_I(ppcp_frame_read(want, want_len, &h, &payload, &consumed), PPCP_OK);
        CHECK_EQ_I(h.payload_len, 87);
        CHECK_EQ_I(h.channel, PPCP_CHANNEL_CONTROL);
        CHECK_EQ_I(h.flags, 0);
        CHECK_EQ_I(h.reserved, 0);
        CHECK_EQ_I(consumed, 95);
    }
}

static void test_enc_5_1_decodes(void)
{
    unsigned char frame[128];
    size_t        n = ppcp_unhex(ENC_5_1_HEX, frame, sizeof(frame));
    ppcp_frame_header h;
    const uint8_t    *payload = NULL;
    ppcp_envelope     e;
    uint32_t          pairs = 0;

    TEST("ENC 5.1 decodes to the stated fields");
    CHECK_EQ_I(ppcp_frame_read(frame, n, &h, &payload, NULL), PPCP_OK);
    CHECK_EQ_I(ppcp_envelope_decode(payload, h.payload_len,
                                    ppcp_cbor_limits_for_channel(h.channel), &e, &pairs),
               PPCP_OK);
    CHECK(strcmp(e.type, "sync_probe") == 0);
    CHECK_EQ_I(e.msg_id, 7);
    CHECK(!e.has_reply_to);
    CHECK(!e.has_session_id);
    CHECK_EQ_I(pairs, 5);
}

static void test_deterministic_form_of_the_same_message(void)
{
    unsigned char got[128];
    size_t        got_len = 0;
    ppcp_envelope e;
    ppcp_frame_header h;
    const uint8_t    *payload = NULL;
    ppcp_envelope     back;

    /* ⚠ Specification defect, recorded in the claim file: the §5.1 example is
     * a legal encoding (ENC 4e is a SHOULD) but it is not deterministic, so
     * the deterministic form of the same message is a different byte string
     * with the same content. */
    TEST("the deterministic encoding of the same message decodes identically");
    CHECK_EQ_I(ppcp_envelope_init(&e, "sync_probe", 7), PPCP_OK);
    CHECK_EQ_I(ppcp_message_encode(got, sizeof(got), PPCP_CHANNEL_CONTROL, &e, 3,
                                   probe_body_deterministic, NULL, &got_len),
               PPCP_OK);
    /* Same field count, same values, same total length — only the order moved. */
    CHECK_EQ_I(got_len, 95);
    CHECK_EQ_I(ppcp_frame_read(got, got_len, &h, &payload, NULL), PPCP_OK);
    CHECK_EQ_I(h.payload_len, 87);
    CHECK_EQ_I(ppcp_envelope_decode(payload, h.payload_len,
                                    ppcp_cbor_limits_for_channel(h.channel), &back, NULL),
               PPCP_OK);
    CHECK(strcmp(back.type, "sync_probe") == 0);
    CHECK_EQ_I(back.msg_id, 7);
    /* The first key really is "t1", not "type". */
    CHECK_EQ_I(payload[1], 0x62);
}

static ppcp_result body_none(ppcp_cbor_writer *w, ppcp_envelope_writer *ew, void *ctx)
{
    (void)w; (void)ew; (void)ctx;
    return PPCP_OK;
}

static void test_msg_seq_and_reply_to(void)
{
    ppcp_msg_seq  s;
    ppcp_envelope e;
    unsigned char buf[128];
    size_t        n = 0;
    ppcp_frame_header h;
    const uint8_t    *payload = NULL;
    ppcp_envelope     back;

    TEST("ENC 5c — msg_id is per sender, from 1; a response carries reply_to");
    ppcp_msg_seq_init(&s);
    CHECK_EQ_I(ppcp_msg_seq_next(&s), 1);
    CHECK_EQ_I(ppcp_msg_seq_next(&s), 2);
    CHECK_EQ_I(ppcp_msg_seq_next(&s), 3);

    CHECK_EQ_I(ppcp_envelope_init(&e, "sync_reply", 1), PPCP_OK);
    CHECK_EQ_I(ppcp_envelope_set_reply_to(&e, 7), PPCP_OK);
    CHECK_EQ_I(ppcp_envelope_set_session_id(&e, "s-1", 3), PPCP_OK);
    CHECK_EQ_I(ppcp_message_encode(buf, sizeof(buf), PPCP_CHANNEL_CONTROL, &e, 0,
                                   body_none, NULL, &n), PPCP_OK);
    CHECK_EQ_I(ppcp_frame_read(buf, n, &h, &payload, NULL), PPCP_OK);
    CHECK_EQ_I(ppcp_envelope_decode(payload, h.payload_len,
                                    ppcp_cbor_limits_for_channel(0), &back, NULL), PPCP_OK);
    CHECK(back.has_reply_to);
    CHECK_EQ_I(back.reply_to, 7);
    CHECK(back.has_session_id);
    CHECK(strcmp(back.session_id.v, "s-1") == 0);

    /* ENC §5: msg_id starts at 1, so zero is not a message id. */
    CHECK_EQ_I(ppcp_envelope_init(&e, "sync_reply", 0), PPCP_ERR_INVALID);
}

static ppcp_result body_reserved_collision(ppcp_cbor_writer *w, ppcp_envelope_writer *ew,
                                           void *ctx)
{
    (void)w; (void)ctx;
    /* ENC 5a — a body field may not be named `session_id`. */
    return ppcp_envelope_before(w, ew, "session_id", 10);
}

static void test_reserved_key_collision(void)
{
    ppcp_envelope e;
    unsigned char buf[128];
    size_t        n = 0;

    TEST("ENC 5a — a body may not reuse a reserved key name");
    CHECK(ppcp_envelope_is_reserved_key("type", 4));
    CHECK(ppcp_envelope_is_reserved_key("msg_id", 6));
    CHECK(ppcp_envelope_is_reserved_key("reply_to", 8));
    CHECK(ppcp_envelope_is_reserved_key("session_id", 10));
    CHECK(!ppcp_envelope_is_reserved_key("probe_seq", 9));

    CHECK_EQ_I(ppcp_envelope_init(&e, "hello", 1), PPCP_OK);
    CHECK(ppcp_message_encode(buf, sizeof(buf), PPCP_CHANNEL_CONTROL, &e, 1,
                              body_reserved_collision, NULL, &n) != PPCP_OK);
}

static void test_recover_msg_id(void)
{
    /* ENC 5d — a receiver that cannot decode a payload answers with `reply_to`
     * where it could recover `msg_id`.  Here the payload carries a duplicate
     * key further on, so ppcp_envelope_decode() refuses it and the recovery
     * path still finds the id. */
    unsigned char bad[] = {
        0xa3,
        0x64, 't', 'y', 'p', 'e', 0x63, 'a', 'b', 'c',
        0x66, 'm', 's', 'g', '_', 'i', 'd', 0x0c,
        0x66, 'm', 's', 'g', '_', 'i', 'd', 0x0d
    };
    ppcp_envelope e;
    uint64_t      id = 0;

    TEST("ENC 5d — msg_id recovered from an undecodable payload");
    CHECK_EQ_I(ppcp_envelope_decode(bad, sizeof(bad), ppcp_cbor_limits_for_channel(0),
                                    &e, NULL), PPCP_ERR_MALFORMED);
    CHECK_EQ_I(ppcp_envelope_recover_msg_id(bad, sizeof(bad),
                                            ppcp_cbor_limits_for_channel(0), &id), PPCP_OK);
    CHECK_EQ_I(id, 12);
}

int main(void)
{
    test_enc_5_1_worked_example();
    test_enc_5_1_decodes();
    test_deterministic_form_of_the_same_message();
    test_msg_seq_and_reply_to();
    test_reserved_key_collision();
    test_recover_msg_id();
    TEST_MAIN_END();
}
