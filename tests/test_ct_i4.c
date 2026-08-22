/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * CT-I4 — I4, Core, static.
 *
 * "Two Sources declared on one clock share one `timebase_id`.  Assert no
 * `TimebaseRelation` with `from == to` is emitted, and that identity is never
 * asserted by relation."
 *
 * The Source type itself lands in L4; what is testable now is the half the
 * invariant actually turns on — that a self-relation, which is the only shape
 * "identity asserted by relation" could take, cannot be built or decoded.
 */
#include "ppcp/time.h"
#include "test_util.h"

static void assertion_no_self_relation(void)
{
    ppcp_timebase_relation r;
    ppcp_instant           at;

    TEST("CT-I4 — a relation from a timebase to itself is not constructible");
    CHECK_EQ_I(ppcp_instant_make_z(&at, "tb:hosttime", 0), PPCP_OK);
    CHECK_EQ_I(ppcp_relation_make_affine(&r, "tb:hosttime", "tb:hosttime", 0, 0.0,
                                         0.0, 0.0, PPCP_RELM_DECLARED, &at),
               PPCP_ERR_INVALID);
    CHECK_EQ_I(ppcp_relation_make_unrelated(&r, "tb:hosttime", "tb:hosttime",
                                            PPCP_RELM_DECLARED, &at), PPCP_ERR_INVALID);

    TEST("CT-I4 — a zero-offset relation between two ids is a claim, not identity");
    /* This is legal and is not what I4 forbids: two clocks that happen to
     * agree are still two clocks.  What is forbidden is asserting they are ONE
     * clock by relating an id to itself. */
    CHECK_EQ_I(ppcp_relation_make_affine(&r, "tb:cam", "tb:mic", 0, 0.0, 0.0, 0.0,
                                         PPCP_RELM_MEASURED, &at), PPCP_ERR_INVALID);
    /* (`observed_at` is in tb:hosttime, which is neither, so that one failed on
     * CORE 5.4's "expressed in `from`" rule.  With the right one it passes.) */
    {
        ppcp_instant at2;
        CHECK_EQ_I(ppcp_instant_make_z(&at2, "tb:cam", 0), PPCP_OK);
        CHECK_EQ_I(ppcp_relation_make_affine(&r, "tb:cam", "tb:mic", 0, 0.0, 0.0, 0.0,
                                             PPCP_RELM_MEASURED, &at2), PPCP_OK);
    }
}

static void assertion_self_relation_rejected_on_receipt(void)
{
    uint8_t                buf[256];
    size_t                 n = 0;
    ppcp_cbor_writer       w;
    ppcp_cbor_reader       rd;
    ppcp_timebase_relation back;
    ppcp_instant           at;

    TEST("CT-I4 — a self-relation on the wire is malformed");
    CHECK_EQ_I(ppcp_instant_make_z(&at, "tb:x", 0), PPCP_OK);
    ppcp_cbor_writer_init(&w, buf, sizeof(buf));
    ppcp_cbor_write_map(&w, 6);
    ppcp_cbor_write_text_z(&w, "to");     ppcp_cbor_write_text_z(&w, "tb:x");
    ppcp_cbor_write_text_z(&w, "from");   ppcp_cbor_write_text_z(&w, "tb:x");
    ppcp_cbor_write_text_z(&w, "class");  ppcp_cbor_write_text_z(&w, "unrelated");
    ppcp_cbor_write_text_z(&w, "method"); ppcp_cbor_write_text_z(&w, "declared");
    ppcp_cbor_write_text_z(&w, "observed_at"); ppcp_instant_encode(&w, &at);
    ppcp_cbor_write_text_z(&w, "evidence_stream_id"); ppcp_cbor_write_text_z(&w, "st:1");
    CHECK_EQ_I(ppcp_cbor_writer_finish(&w, &n), PPCP_OK);

    ppcp_cbor_reader_init(&rd, buf, n, ppcp_cbor_limits_for_channel(0));
    CHECK_EQ_I(ppcp_relation_decode(&rd, &back), PPCP_ERR_MALFORMED);
}

static void assertion_shared_id_needs_no_relation(void)
{
    ppcp_timebase cam, mic;

    /* CORE 5.3: "Two Sources on the same clock reference the SAME Timebase.id
     * (I4)."  On iOS both reference tb:hosttime and no relation exists because
     * none is needed.  The mechanism is that there is no third option: declare
     * one id, or declare two and supply a relation. */
    TEST("CT-I4 — identity is a shared id, and it is just an equality");
    CHECK_EQ_I(ppcp_timebase_make(&cam, "tb:hosttime", 11, PPCP_TB_CONTINUOUS,
                                  true, 41), PPCP_OK);
    CHECK_EQ_I(ppcp_timebase_make(&mic, "tb:hosttime", 11, PPCP_TB_CONTINUOUS,
                                  true, 41), PPCP_OK);
    CHECK(ppcp_id_equal(&cam.id, &mic.id));

    TEST("CT-I4 — two ids are two clocks, and a relation is then structurally required");
    CHECK_EQ_I(ppcp_timebase_make(&mic, "tb:audio", 8, PPCP_TB_MONOTONIC, false, 21),
               PPCP_OK);
    CHECK(!ppcp_id_equal(&cam.id, &mic.id));
}

int main(void)
{
    assertion_no_self_relation();
    assertion_self_relation_rejected_on_receipt();
    assertion_shared_id_needs_no_relation();
    TEST_MAIN_END();
}
