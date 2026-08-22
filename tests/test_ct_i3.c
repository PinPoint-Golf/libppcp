/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * CT-I3 — I3, Core, static.
 *
 * "An `affine` relation missing `offset_sigma_ns` or `skew_sigma_ppm` is
 * rejected as malformed on receipt, and cannot be constructed for emission."
 */
#include "ppcp/time.h"
#include "test_util.h"

/* Builds an affine relation map by hand, omitting whichever key the caller
 * names — the shape a peer that thinks a sigma is optional would send. */
static size_t build_affine(uint8_t *out, size_t cap, const char *omit)
{
    ppcp_cbor_writer w;
    ppcp_instant     at;
    size_t           n = 0;
    size_t           count = 9;

    if (omit != NULL)
        count--;
    ppcp_instant_make_z(&at, "tb:a", 100);

    ppcp_cbor_writer_init(&w, out, cap);
    ppcp_cbor_write_map(&w, count);
    ppcp_cbor_write_text_z(&w, "to");     ppcp_cbor_write_text_z(&w, "tb:b");
    ppcp_cbor_write_text_z(&w, "from");   ppcp_cbor_write_text_z(&w, "tb:a");
    ppcp_cbor_write_text_z(&w, "class");  ppcp_cbor_write_text_z(&w, "affine");
    ppcp_cbor_write_text_z(&w, "method"); ppcp_cbor_write_text_z(&w, "measured");
    if (omit == NULL || strcmp(omit, "skew_ppm") != 0) {
        ppcp_cbor_write_text_z(&w, "skew_ppm"); ppcp_cbor_write_double(&w, 12.5);
    }
    if (omit == NULL || strcmp(omit, "offset_ns") != 0) {
        ppcp_cbor_write_text_z(&w, "offset_ns"); ppcp_cbor_write_int(&w, 4200);
    }
    ppcp_cbor_write_text_z(&w, "observed_at"); ppcp_instant_encode(&w, &at);
    if (omit == NULL || strcmp(omit, "skew_sigma_ppm") != 0) {
        ppcp_cbor_write_text_z(&w, "skew_sigma_ppm"); ppcp_cbor_write_double(&w, 0.4);
    }
    if (omit == NULL || strcmp(omit, "offset_sigma_ns") != 0) {
        ppcp_cbor_write_text_z(&w, "offset_sigma_ns"); ppcp_cbor_write_double(&w, 90.0);
    }
    if (ppcp_cbor_writer_finish(&w, &n) != PPCP_OK)
        return 0;
    return n;
}

static void assertion_not_constructible(void)
{
    ppcp_timebase_relation r;
    ppcp_instant           at;

    TEST("CT-I3 — an affine relation without both sigmas is not constructible");
    CHECK_EQ_I(ppcp_instant_make_z(&at, "tb:a", 100), PPCP_OK);
    /* There is one constructor for an affine relation and both sigmas are
     * parameters of it, so the missing-sigma shape has no representation.
     * What CAN be attempted is a nonsensical sigma, and that is refused. */
    CHECK_EQ_I(ppcp_relation_make_affine(&r, "tb:a", "tb:b", 4200, 12.5, -1.0, 0.4,
                                         PPCP_RELM_MEASURED, &at), PPCP_ERR_INVALID);
    CHECK_EQ_I(ppcp_relation_make_affine(&r, "tb:a", "tb:b", 4200, 12.5, 90.0, -0.4,
                                         PPCP_RELM_MEASURED, &at), PPCP_ERR_INVALID);
    CHECK_EQ_I(ppcp_relation_make_affine(&r, "tb:a", "tb:b", 4200, 12.5, 90.0, 0.4,
                                         PPCP_RELM_MEASURED, &at), PPCP_OK);

    TEST("CT-I3 — and a hand-zeroed affine relation does not encode");
    {
        ppcp_cbor_writer w;
        uint8_t          buf[256];
        memset(&r, 0, sizeof(r));
        r.cls = PPCP_REL_AFFINE;
        ppcp_cbor_writer_init(&w, buf, sizeof(buf));
        CHECK(ppcp_relation_encode(&w, &r) != PPCP_OK);
    }
}

static void assertion_rejected_on_receipt(void)
{
    uint8_t                buf[256];
    size_t                 n;
    ppcp_cbor_reader       rd;
    ppcp_timebase_relation r;

    TEST("CT-I3 — a complete affine relation decodes");
    n = build_affine(buf, sizeof(buf), NULL);
    CHECK(n > 0);
    ppcp_cbor_reader_init(&rd, buf, n, ppcp_cbor_limits_for_channel(0));
    CHECK_EQ_I(ppcp_relation_decode(&rd, &r), PPCP_OK);
    CHECK_EQ_I(r.offset_ns, 4200);

    TEST("CT-I3 — missing offset_sigma_ns is malformed on receipt");
    n = build_affine(buf, sizeof(buf), "offset_sigma_ns");
    CHECK(n > 0);
    ppcp_cbor_reader_init(&rd, buf, n, ppcp_cbor_limits_for_channel(0));
    CHECK_EQ_I(ppcp_relation_decode(&rd, &r), PPCP_ERR_MALFORMED);

    TEST("CT-I3 — missing skew_sigma_ppm is malformed on receipt");
    n = build_affine(buf, sizeof(buf), "skew_sigma_ppm");
    CHECK(n > 0);
    ppcp_cbor_reader_init(&rd, buf, n, ppcp_cbor_limits_for_channel(0));
    CHECK_EQ_I(ppcp_relation_decode(&rd, &r), PPCP_ERR_MALFORMED);

    TEST("CT-I3 — and missing offset or skew themselves (CORE 5.4a names all four)");
    n = build_affine(buf, sizeof(buf), "offset_ns");
    ppcp_cbor_reader_init(&rd, buf, n, ppcp_cbor_limits_for_channel(0));
    CHECK_EQ_I(ppcp_relation_decode(&rd, &r), PPCP_ERR_MALFORMED);
    n = build_affine(buf, sizeof(buf), "skew_ppm");
    ppcp_cbor_reader_init(&rd, buf, n, ppcp_cbor_limits_for_channel(0));
    CHECK_EQ_I(ppcp_relation_decode(&rd, &r), PPCP_ERR_MALFORMED);
}

static void assertion_unrelated_is_complete(void)
{
    ppcp_timebase_relation r, back;
    ppcp_instant           at;
    ppcp_cbor_writer       w;
    ppcp_cbor_reader       rd;
    uint8_t                buf[256];
    size_t                 n = 0;

    /* CORE 5.4b — `unrelated` is a legal, complete declaration.  An honest
     * Android UNKNOWN device declares it and stays conformant; the alternative
     * is a fabricated offset, which is the failure this contract prevents. */
    TEST("CT-I3 — `unrelated` is a complete declaration and carries no affine field");
    CHECK_EQ_I(ppcp_instant_make_z(&at, "tb:cam", 0), PPCP_OK);
    CHECK_EQ_I(ppcp_relation_make_unrelated(&r, "tb:cam", "tb:mic",
                                            PPCP_RELM_DECLARED, &at), PPCP_OK);
    ppcp_cbor_writer_init(&w, buf, sizeof(buf));
    CHECK_EQ_I(ppcp_relation_encode(&w, &r), PPCP_OK);
    CHECK_EQ_I(ppcp_cbor_writer_finish(&w, &n), PPCP_OK);
    ppcp_cbor_reader_init(&rd, buf, n, ppcp_cbor_limits_for_channel(0));
    CHECK_EQ_I(ppcp_relation_decode(&rd, &back), PPCP_OK);
    CHECK_EQ_I(back.cls, PPCP_REL_UNRELATED);

    TEST("CT-I3 — `unrelated` carrying an offset is malformed (CORE 5.4b)");
    {
        ppcp_cbor_writer w2;
        size_t           n2 = 0;
        ppcp_cbor_writer_init(&w2, buf, sizeof(buf));
        ppcp_cbor_write_map(&w2, 6);
        ppcp_cbor_write_text_z(&w2, "to");     ppcp_cbor_write_text_z(&w2, "tb:mic");
        ppcp_cbor_write_text_z(&w2, "from");   ppcp_cbor_write_text_z(&w2, "tb:cam");
        ppcp_cbor_write_text_z(&w2, "class");  ppcp_cbor_write_text_z(&w2, "unrelated");
        ppcp_cbor_write_text_z(&w2, "method"); ppcp_cbor_write_text_z(&w2, "declared");
        ppcp_cbor_write_text_z(&w2, "offset_ns"); ppcp_cbor_write_int(&w2, 17);
        ppcp_cbor_write_text_z(&w2, "observed_at"); ppcp_instant_encode(&w2, &at);
        CHECK_EQ_I(ppcp_cbor_writer_finish(&w2, &n2), PPCP_OK);
        ppcp_cbor_reader_init(&rd, buf, n2, ppcp_cbor_limits_for_channel(0));
        CHECK_EQ_I(ppcp_relation_decode(&rd, &back), PPCP_ERR_MALFORMED);
    }

    TEST("CT-I3 — `unrelated` has no mapping, so applying it fails");
    {
        ppcp_instant from, to;
        CHECK_EQ_I(ppcp_instant_make_z(&from, "tb:cam", 5), PPCP_OK);
        CHECK_EQ_I(ppcp_relation_apply(&r, &from, &to), PPCP_ERR_INVALID);
    }
}

int main(void)
{
    assertion_not_constructible();
    assertion_rejected_on_receipt();
    assertion_unrelated_is_complete();
    TEST_MAIN_END();
}
