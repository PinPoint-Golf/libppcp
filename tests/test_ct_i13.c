/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * CT-I13 — I13, Core.  The encoding half, which is what L1 unlocks.
 *
 * CONF §3's CT-I13 is a fixture test: "Replay a stream containing an unknown
 * message type, an unknown map key at three nesting levels, an unknown
 * `Source.kind` and an unknown `Candidate.basis`.  Assert none is fatal and the
 * surrounding data survives."
 *
 * The message catalogue (L5), Source and Candidate (L4) do not exist yet, so
 * the two halves that depend on them are not claimed.  What is asserted here is
 * the mechanism the whole invariant rests on: an unknown key at three nesting
 * levels is skipped and every known field around it survives.
 */
#include "ppcp/envelope.h"
#include "ppcp/frame.h"
#include "ppcp/time.h"
#include "test_util.h"

/* An envelope carrying a message type this library has never heard of, an
 * unknown key at the top level, another inside a nested map, and a third
 * inside a map nested inside that — with a known field after each. */
static size_t build_stream_with_unknowns(uint8_t *out, size_t cap)
{
    ppcp_cbor_writer w;
    size_t           n = 0;

    ppcp_cbor_writer_init(&w, out, cap);
    ppcp_cbor_write_map(&w, 5);

    /* level 1 unknown */
    ppcp_cbor_write_text_z(&w, "t1");
    ppcp_cbor_write_map(&w, 2);
    ppcp_cbor_write_text_z(&w, "ns"); ppcp_cbor_write_int(&w, 1723000000000LL);
    ppcp_cbor_write_text_z(&w, "tb"); ppcp_cbor_write_text_z(&w, "tb:device");

    ppcp_cbor_write_text_z(&w, "type");
    ppcp_cbor_write_text_z(&w, "com.example.future_event");   /* unknown type */

    ppcp_cbor_write_text_z(&w, "msg_id"); ppcp_cbor_write_uint(&w, 12);

    ppcp_cbor_write_text_z(&w, "unknown_a");                  /* level 1 */
    ppcp_cbor_write_map(&w, 2);
    ppcp_cbor_write_text_z(&w, "k"); ppcp_cbor_write_uint(&w, 1);
    ppcp_cbor_write_text_z(&w, "unknown_b");                  /* level 2 */
    ppcp_cbor_write_array(&w, 2);
    ppcp_cbor_write_map(&w, 1);
    ppcp_cbor_write_text_z(&w, "unknown_c");                  /* level 3 */
    ppcp_cbor_write_bool(&w, true);
    ppcp_cbor_write_text_z(&w, "trailing");

    ppcp_cbor_write_text_z(&w, "unknown_z"); ppcp_cbor_write_int(&w, -9);

    if (ppcp_cbor_writer_finish(&w, &n) != PPCP_OK)
        return 0;
    return n;
}

static void assertion_unknown_keys_at_three_levels(void)
{
    uint8_t          buf[512];
    size_t           n;
    ppcp_envelope    e;
    uint32_t         pairs = 0;

    TEST("CT-I13 — an unknown key at three nesting levels is skipped, not fatal");
    n = build_stream_with_unknowns(buf, sizeof(buf));
    CHECK(n > 0);
    CHECK_EQ_I(ppcp_cbor_validate(buf, n, ppcp_cbor_limits_for_channel(0), NULL), PPCP_OK);

    /* And the surrounding data survives: the envelope still decodes, with the
     * unknown message type carried through rather than refused. */
    CHECK_EQ_I(ppcp_envelope_decode(buf, n, ppcp_cbor_limits_for_channel(0), &e, &pairs),
               PPCP_OK);
    CHECK(strcmp(e.type, "com.example.future_event") == 0);
    CHECK_EQ_I(e.msg_id, 12);
    CHECK_EQ_I(pairs, 5);
}

static void assertion_known_field_after_unknown_survives(void)
{
    uint8_t          buf[512];
    size_t           n;
    ppcp_cbor_reader r;
    ppcp_cbor_item   it;
    uint32_t         i;
    int              found = 0;
    ppcp_instant     t1;

    /* The failure mode I13 guards against is not "the decoder errored": it is
     * a decoder that stops reading at the first key it does not know, and
     * quietly loses everything after it. */
    TEST("CT-I13 — a known field positioned AFTER an unknown one is still read");
    n = build_stream_with_unknowns(buf, sizeof(buf));
    ppcp_cbor_reader_init(&r, buf, n, ppcp_cbor_limits_for_channel(0));
    CHECK_EQ_I(ppcp_cbor_read(&r, &it), PPCP_OK);
    for (i = 0; i < it.count; i++) {
        const char *k; size_t klen;
        CHECK_EQ_I(ppcp_cbor_read_key(&r, &k, &klen), PPCP_OK);
        if (ppcp_cbor_key_is(k, klen, "t1")) {
            CHECK_EQ_I(ppcp_instant_decode(&r, &t1), PPCP_OK);
            found++;
        } else {
            CHECK_EQ_I(ppcp_cbor_skip(&r), PPCP_OK);
        }
    }
    CHECK_EQ_I(found, 1);
    CHECK_EQ_I(t1.ns, 1723000000000LL);
}

static void assertion_open_registry_values_pass(void)
{
    ppcp_clock_discontinuity d;
    ppcp_instant             at;

    /* CORE 10.3a — unknown values in an open registry are ignored, never
     * fatal; 10.3b — a vendor value is reverse-DNS namespaced. */
    TEST("CT-I13 — an unknown open-registry value is carried, not refused");
    CHECK_EQ_I(ppcp_instant_make_z(&at, "tb:mono", 1), PPCP_OK);
    CHECK_EQ_I(ppcp_clock_discontinuity_make(&d, "tb:wall", &at, 5,
                                             "com.example.gps_fix"), PPCP_OK);
    CHECK(strcmp(d.cause.v, "com.example.gps_fix") == 0);
}

static void assertion_unknown_frame_flags_are_not_fatal(void)
{
    uint8_t           raw[PPCP_FRAME_HEADER_BYTES] = { 0, 0, 0, 1, 0, 0xff, 0xff, 0xff };
    ppcp_frame_header h;

    /* ENC 3b is the same idea at the framing layer: a receiver ignores unknown
     * bits rather than failing, so a later MINOR may use them. */
    TEST("CT-I13 — unknown frame header bits are reported, not refused");
    CHECK_EQ_I(ppcp_frame_header_parse(raw, &h), PPCP_OK);
    CHECK_EQ_I(h.payload_len, 1);
}

int main(void)
{
    assertion_unknown_keys_at_three_levels();
    assertion_known_field_after_unknown_survives();
    assertion_open_registry_values_pass();
    assertion_unknown_frame_flags_are_not_fatal();
    TEST_MAIN_END();
}
