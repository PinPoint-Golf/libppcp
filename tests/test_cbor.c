/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * PPCP-ENC §4 and §8 — the encoder's determinism and the decoder's refusals.
 */
#include "ppcp/cbor.h"
#include "ppcp/frame.h"
#include "test_util.h"

static ppcp_cbor_limits ctl(void) { return ppcp_cbor_limits_for_channel(0); }

static void test_shortest_form_heads(void)
{
    unsigned char buf[64];
    ppcp_cbor_writer w;
    size_t n = 0;
    static const unsigned char want[] = {
        0x85,
        0x17,                                     /* 23 */
        0x18, 0x18,                               /* 24 */
        0x19, 0x01, 0x00,                         /* 256 */
        0x1a, 0x00, 0x01, 0x00, 0x00,             /* 65536 */
        0x3b, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00  /* -4294967297 */
    };

    TEST("RFC 8949 4.2.1 — shortest-form heads");
    ppcp_cbor_writer_init(&w, buf, sizeof(buf));
    CHECK_EQ_I(ppcp_cbor_write_array(&w, 5), PPCP_OK);
    CHECK_EQ_I(ppcp_cbor_write_uint(&w, 23), PPCP_OK);
    CHECK_EQ_I(ppcp_cbor_write_uint(&w, 24), PPCP_OK);
    CHECK_EQ_I(ppcp_cbor_write_uint(&w, 256), PPCP_OK);
    CHECK_EQ_I(ppcp_cbor_write_uint(&w, 65536), PPCP_OK);
    /* -1 - n with n = 2^32, which is the first value needing the 8-byte head. */
    CHECK_EQ_I(ppcp_cbor_write_int(&w, -4294967297LL), PPCP_OK);
    CHECK_EQ_I(ppcp_cbor_writer_finish(&w, &n), PPCP_OK);
    CHECK_BYTES(buf, n, want, sizeof(want));
}

static void test_deterministic_key_order_enforced(void)
{
    unsigned char buf[64];
    ppcp_cbor_writer w;

    TEST("ENC 4e — the writer refuses an out-of-order map key");
    ppcp_cbor_writer_init(&w, buf, sizeof(buf));
    CHECK_EQ_I(ppcp_cbor_write_map(&w, 2), PPCP_OK);
    CHECK_EQ_I(ppcp_cbor_write_text_z(&w, "type"), PPCP_OK);
    CHECK_EQ_I(ppcp_cbor_write_uint(&w, 1), PPCP_OK);
    /* "t1" encodes 62 74 31 and sorts before "type" (64 ...), so writing it
     * second is not deterministic encoding. */
    CHECK(ppcp_cbor_write_text_z(&w, "t1") != PPCP_OK);

    TEST("ENC 4d — the writer refuses a duplicate key in either mode");
    ppcp_cbor_writer_init_order(&w, buf, sizeof(buf), PPCP_CBOR_ORDER_LITERAL);
    CHECK_EQ_I(ppcp_cbor_write_map(&w, 2), PPCP_OK);
    CHECK_EQ_I(ppcp_cbor_write_text_z(&w, "a"), PPCP_OK);
    CHECK_EQ_I(ppcp_cbor_write_uint(&w, 1), PPCP_OK);
    CHECK(ppcp_cbor_write_text_z(&w, "a") != PPCP_OK);

    TEST("the literal writer keeps the order it is given");
    ppcp_cbor_writer_init_order(&w, buf, sizeof(buf), PPCP_CBOR_ORDER_LITERAL);
    CHECK_EQ_I(ppcp_cbor_write_map(&w, 2), PPCP_OK);
    CHECK_EQ_I(ppcp_cbor_write_text_z(&w, "type"), PPCP_OK);
    CHECK_EQ_I(ppcp_cbor_write_uint(&w, 1), PPCP_OK);
    CHECK_EQ_I(ppcp_cbor_write_text_z(&w, "t1"), PPCP_OK);
    CHECK_EQ_I(ppcp_cbor_write_uint(&w, 2), PPCP_OK);
}

static void test_key_cmp(void)
{
    TEST("RFC 8949 4.2.1 key order — RV 4.3b rests on this");
    /* One-character `v` sorts before every two-character key: that is what
     * makes RV 4.2a true by construction. */
    CHECK(ppcp_cbor_key_cmp("v", 1, "dn", 2) < 0);
    CHECK(ppcp_cbor_key_cmp("v", 1, "ep", 2) < 0);
    CHECK(ppcp_cbor_key_cmp("v", 1, "wifi", 4) < 0);
    /* And the Draft 1 defect: `n` would have sorted before `v`. */
    CHECK(ppcp_cbor_key_cmp("n", 1, "v", 1) < 0);
    /* Equal length compares bytewise. */
    CHECK(ppcp_cbor_key_cmp("ep", 2, "mu", 2) < 0);
    CHECK(ppcp_cbor_key_cmp("sid", 3, "psk", 3) > 0);
    CHECK(ppcp_cbor_key_cmp("ns", 2, "tb", 2) < 0);
    CHECK_EQ_I(ppcp_cbor_key_cmp("abc", 3, "abc", 3), 0);
}

static void test_encoder_refuses_null_and_tags_by_omission(void)
{
    /* There is no writer for `null`, for a tag or for an indefinite length —
     * ENC 4c and 4d are enforced by the encoder having no way to say them.
     * The decoder's refusal is what is tested here. */
    unsigned char null_item[] = { 0xf6 };
    unsigned char undef_item[] = { 0xf7 };
    unsigned char tagged[]    = { 0xc0, 0x01 };
    unsigned char indef_arr[] = { 0x9f, 0x01, 0xff };
    unsigned char indef_txt[] = { 0x7f, 0x61, 'a', 0xff };
    unsigned char int_key[]   = { 0xa1, 0x01, 0x02 };
    unsigned char dup_key[]   = { 0xa2, 0x61, 'a', 0x01, 0x61, 'a', 0x02 };

    TEST("ENC 4c/4d — null, undefined, tags and indefinite lengths are malformed");
    CHECK_EQ_I(ppcp_cbor_validate(null_item, sizeof(null_item), ctl(), NULL),
               PPCP_ERR_MALFORMED);
    CHECK_EQ_I(ppcp_cbor_validate(undef_item, sizeof(undef_item), ctl(), NULL),
               PPCP_ERR_MALFORMED);
    CHECK_EQ_I(ppcp_cbor_validate(tagged, sizeof(tagged), ctl(), NULL), PPCP_ERR_MALFORMED);
    CHECK_EQ_I(ppcp_cbor_validate(indef_arr, sizeof(indef_arr), ctl(), NULL),
               PPCP_ERR_MALFORMED);
    CHECK_EQ_I(ppcp_cbor_validate(indef_txt, sizeof(indef_txt), ctl(), NULL),
               PPCP_ERR_MALFORMED);

    TEST("ENC 4a — an integer map key is malformed");
    CHECK_EQ_I(ppcp_cbor_validate(int_key, sizeof(int_key), ctl(), NULL),
               PPCP_ERR_MALFORMED);

    TEST("ENC 4d — a duplicate map key is malformed");
    CHECK_EQ_I(ppcp_cbor_validate(dup_key, sizeof(dup_key), ctl(), NULL),
               PPCP_ERR_MALFORMED);
}

static void test_duplicate_keys_at_depth(void)
{
    /* The duplicate is inside a nested map, and the outer map has a key of the
     * same name — which is legal.  A scanner that shared one key set across
     * levels would either miss this or reject the legal case. */
    unsigned char nested_dup[] = {
        0xa1, 0x61, 'a',
        0xa2, 0x61, 'b', 0x01, 0x61, 'b', 0x02
    };
    unsigned char shadowing_ok[] = {
        0xa2,
        0x61, 'a', 0x01,
        0x61, 'b', 0xa1, 0x61, 'a', 0x02
    };

    TEST("ENC 4d — duplicate detection is per map, at every depth");
    CHECK_EQ_I(ppcp_cbor_validate(nested_dup, sizeof(nested_dup), ctl(), NULL),
               PPCP_ERR_MALFORMED);
    CHECK_EQ_I(ppcp_cbor_validate(shadowing_ok, sizeof(shadowing_ok), ctl(), NULL),
               PPCP_OK);
}

static void test_float_acceptance(void)
{
    /* ENC §4: half and single precision MUST NOT be emitted; a decoder MUST
     * accept them. */
    unsigned char half[]   = { 0xf9, 0x3c, 0x00 };              /* 1.0 */
    unsigned char single[] = { 0xfa, 0x3f, 0x80, 0x00, 0x00 };  /* 1.0 */
    unsigned char dbl[]    = { 0xfb, 0x3f, 0xf0, 0, 0, 0, 0, 0, 0 };
    unsigned char half_neg[] = { 0xf9, 0xc0, 0x00 };            /* -2.0 */
    unsigned char half_sub[] = { 0xf9, 0x00, 0x01 };            /* 2^-24 */
    ppcp_cbor_reader r;
    ppcp_cbor_item   it;

    TEST("ENC §4 — half and single floats are accepted and widened");
    ppcp_cbor_reader_init(&r, half, sizeof(half), ctl());
    CHECK_EQ_I(ppcp_cbor_read(&r, &it), PPCP_OK);
    CHECK_EQ_I(it.type, PPCP_CBOR_DOUBLE);
    CHECK(it.f == 1.0);

    ppcp_cbor_reader_init(&r, single, sizeof(single), ctl());
    CHECK_EQ_I(ppcp_cbor_read(&r, &it), PPCP_OK);
    CHECK(it.f == 1.0);

    ppcp_cbor_reader_init(&r, dbl, sizeof(dbl), ctl());
    CHECK_EQ_I(ppcp_cbor_read(&r, &it), PPCP_OK);
    CHECK(it.f == 1.0);

    ppcp_cbor_reader_init(&r, half_neg, sizeof(half_neg), ctl());
    CHECK_EQ_I(ppcp_cbor_read(&r, &it), PPCP_OK);
    CHECK(it.f == -2.0);

    ppcp_cbor_reader_init(&r, half_sub, sizeof(half_sub), ctl());
    CHECK_EQ_I(ppcp_cbor_read(&r, &it), PPCP_OK);
    CHECK(it.f > 5.9e-8 && it.f < 6.0e-8);

    TEST("ENC §4 — the encoder emits doubles only");
    {
        unsigned char buf[16];
        ppcp_cbor_writer w;
        size_t n = 0;
        ppcp_cbor_writer_init(&w, buf, sizeof(buf));
        CHECK_EQ_I(ppcp_cbor_write_double(&w, 1.0), PPCP_OK);
        CHECK_EQ_I(ppcp_cbor_writer_finish(&w, &n), PPCP_OK);
        CHECK_EQ_I(n, 9);
        CHECK_EQ_I(buf[0], 0xfb);
    }
}

static void test_limits_before_allocation(void)
{
    /* ENC §8 and 3a.  Each of these is refused when its HEAD is read: the
     * decoder never allocates and never copies, so the length is checked
     * before a byte of the body is touched. */
    unsigned char big_text[8];
    unsigned char big_bytes[8];
    unsigned char huge_array[6];
    ppcp_cbor_reader r;
    ppcp_cbor_item   it;

    TEST("ENC §8 — a text string over 64 KiB is refused on its head");
    big_text[0] = 0x7a;                       /* text, 4-byte length */
    big_text[1] = 0x00; big_text[2] = 0x01; big_text[3] = 0x00; big_text[4] = 0x01;
    ppcp_cbor_reader_init(&r, big_text, 5, ctl());
    CHECK_EQ_I(ppcp_cbor_read(&r, &it), PPCP_ERR_LIMIT);

    TEST("ENC §8 — a byte string over the channel's frame limit is refused");
    big_bytes[0] = 0x5a;                      /* bytes, 4-byte length */
    big_bytes[1] = 0x00; big_bytes[2] = 0x20; big_bytes[3] = 0x00; big_bytes[4] = 0x00;
    ppcp_cbor_reader_init(&r, big_bytes, 5, ctl());
    CHECK_EQ_I(ppcp_cbor_read(&r, &it), PPCP_ERR_LIMIT);   /* 2 MiB on control */
    /* The same head on a bulk channel is inside its 8 MiB limit, and fails as
     * truncated rather than as a limit — the length is legal, the bytes are
     * simply not here. */
    ppcp_cbor_reader_init(&r, big_bytes, 5, ppcp_cbor_limits_for_channel(1));
    CHECK_EQ_I(ppcp_cbor_read(&r, &it), PPCP_ERR_TRUNCATED);

    TEST("ENC §8 — an element count over 2^20 is refused");
    huge_array[0] = 0x9a;
    huge_array[1] = 0x00; huge_array[2] = 0x20; huge_array[3] = 0x00; huge_array[4] = 0x00;
    ppcp_cbor_reader_init(&r, huge_array, 5, ctl());
    CHECK_EQ_I(ppcp_cbor_read(&r, &it), PPCP_ERR_LIMIT);

    TEST("an element count larger than the bytes remaining is truncated, not accepted");
    {
        unsigned char lying[] = { 0x98, 0x40, 0x01 };   /* array(64), one item present */
        ppcp_cbor_reader_init(&r, lying, sizeof(lying), ctl());
        CHECK_EQ_I(ppcp_cbor_read(&r, &it), PPCP_ERR_TRUNCATED);
    }
}

static void test_nesting_depth(void)
{
    unsigned char deep[32];
    unsigned      i;
    ppcp_result   rc;

    TEST("ENC §8 — nesting deeper than 16 is refused");
    for (i = 0; i < 20; i++)
        deep[i] = 0x81;          /* array(1) */
    deep[20] = 0x01;
    rc = ppcp_cbor_validate(deep, 21, ctl(), NULL);
    CHECK_EQ_I(rc, PPCP_ERR_LIMIT);

    /* Sixteen is inside the limit. */
    for (i = 0; i < 16; i++)
        deep[i] = 0x81;
    deep[16] = 0x01;
    CHECK_EQ_I(ppcp_cbor_validate(deep, 17, ctl(), NULL), PPCP_OK);
}

static void test_int64_bound(void)
{
    /* ENC §4: integers MUST fit in int64.  A uint beyond INT64_MAX would wrap
     * on the way in and be a different number on the way out. */
    unsigned char too_big[] = { 0x1b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    ppcp_cbor_reader r;
    ppcp_cbor_item   it;
    ppcp_cbor_writer w;
    unsigned char    buf[16];

    TEST("ENC §4 — an integer outside int64 is malformed");
    ppcp_cbor_reader_init(&r, too_big, sizeof(too_big), ctl());
    CHECK_EQ_I(ppcp_cbor_read(&r, &it), PPCP_ERR_MALFORMED);

    ppcp_cbor_writer_init(&w, buf, sizeof(buf));
    CHECK(ppcp_cbor_write_uint(&w, 0xffffffffffffffffULL) != PPCP_OK);

    TEST("INT64_MIN round-trips");
    ppcp_cbor_writer_init(&w, buf, sizeof(buf));
    CHECK_EQ_I(ppcp_cbor_write_int(&w, INT64_MIN), PPCP_OK);
    {
        size_t n = 0;
        CHECK_EQ_I(ppcp_cbor_writer_finish(&w, &n), PPCP_OK);
        ppcp_cbor_reader_init(&r, buf, n, ctl());
        CHECK_EQ_I(ppcp_cbor_read(&r, &it), PPCP_OK);
        CHECK(it.i == INT64_MIN);
    }
}

static void test_skip(void)
{
    /* I13 — the mechanism by which a MINOR version adds a field. */
    unsigned char msg[] = {
        0xa3,
        0x61, 'a', 0x01,
        0x61, 'x', 0x82, 0xa1, 0x61, 'q', 0x18, 0x2a, 0x63, 'h', 'e', 'y',
        0x61, 'z', 0x01
    };
    ppcp_cbor_reader r;
    ppcp_cbor_item   it;
    const char      *k; size_t klen;
    int              seen_z = 0;
    uint32_t         i;

    TEST("I13 — an unknown key's value is skipped whatever shape it has");
    CHECK_EQ_I(ppcp_cbor_validate(msg, sizeof(msg), ctl(), NULL), PPCP_OK);
    ppcp_cbor_reader_init(&r, msg, sizeof(msg), ctl());
    CHECK_EQ_I(ppcp_cbor_read(&r, &it), PPCP_OK);
    for (i = 0; i < it.count; i++) {
        ppcp_cbor_item v;
        CHECK_EQ_I(ppcp_cbor_read_key(&r, &k, &klen), PPCP_OK);
        if (ppcp_cbor_key_is(k, klen, "z")) {
            CHECK_EQ_I(ppcp_cbor_read(&r, &v), PPCP_OK);
            CHECK_EQ_I(v.i, 1);
            seen_z = 1;
        } else if (ppcp_cbor_key_is(k, klen, "a")) {
            CHECK_EQ_I(ppcp_cbor_read(&r, &v), PPCP_OK);
        } else {
            CHECK_EQ_I(ppcp_cbor_skip(&r), PPCP_OK);
        }
    }
    CHECK(seen_z);
}

static void test_writer_completeness(void)
{
    unsigned char buf[32];
    ppcp_cbor_writer w;
    size_t n = 0;

    TEST("a map still owed items does not finish");
    ppcp_cbor_writer_init(&w, buf, sizeof(buf));
    CHECK_EQ_I(ppcp_cbor_write_map(&w, 2), PPCP_OK);
    CHECK_EQ_I(ppcp_cbor_write_text_z(&w, "a"), PPCP_OK);
    CHECK_EQ_I(ppcp_cbor_write_uint(&w, 1), PPCP_OK);
    CHECK(ppcp_cbor_writer_finish(&w, &n) != PPCP_OK);

    TEST("a full buffer is PPCP_ERR_NOSPACE, not a silent truncation");
    ppcp_cbor_writer_init(&w, buf, 3);
    CHECK_EQ_I(ppcp_cbor_write_map(&w, 1), PPCP_OK);
    CHECK_EQ_I(ppcp_cbor_write_text_z(&w, "a"), PPCP_OK);
    CHECK_EQ_I(ppcp_cbor_write_uint(&w, 1000000), PPCP_ERR_NOSPACE);
}

int main(void)
{
    test_shortest_form_heads();
    test_deterministic_key_order_enforced();
    test_key_cmp();
    test_encoder_refuses_null_and_tags_by_omission();
    test_duplicate_keys_at_depth();
    test_float_acceptance();
    test_limits_before_allocation();
    test_nesting_depth();
    test_int64_bound();
    test_skip();
    test_writer_completeness();
    TEST_MAIN_END();
}
