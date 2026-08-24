/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * PPCP-RV §11.4 — the five bootstrap frames.  The wire half of RT-19 and
 * RT-24, and the standing guard on trap 1.
 */
#include "ppcp/bootstrap.h"
#include "ppcp/cbor.h"
#include "ppcp/frame.h"
#include "test_util.h"

/* §10.4's commitment, so the frame is tied to the vector rather than to an
 * arbitrary 32 bytes. */
static const char CT_HEX[]   = "f32cd8e62f80f76adb4ba21971efbd10eb71aa6715d9e458f5422c1644357a3a";
static const char PK_A_HEX[] = "675dd574ed7789310b3d2e7681f3790b466c773b1521fecf36577958371ea52f";
static const char MAC_I_HEX[] = "b056a374ac4decba04f58bfd746746cd";

/* The deterministic encoding of that bs_offer, byte for byte: the 8-byte
 * header with channel 255, then a3, then "v" first (4.3b, 11.4d). */
static const char OFFER_HEX[] =
    "0000002d"                     /* payload_len = 45, big-endian (ENC §3)   */
    "ff"                           /* channel 255 (11.4a, ENC 2a)             */
    "00" "0000"                    /* flags, reserved — zero in ppcp/1.0 (3b) */
    "a3"                           /* map(3)                                  */
    "6176" "01"                    /* "v": 1   — FIRST, by the 1-char rule    */
    "626374" "5820"                /* "ct": bytes(32)                         */
    "f32cd8e62f80f76adb4ba21971efbd10eb71aa6715d9e458f5422c1644357a3a"
    "627479" "01";                 /* "ty": 1                                 */

static void check_round_trip(const ppcp_bs_frame *in)
{
    uint8_t       wire[PPCP_BS_MAX_FRAME];
    size_t        n = 0, consumed = 0;
    ppcp_bs_frame back;

    CHECK_EQ_I(ppcp_bs_frame_write(in, wire, sizeof(wire), &n), PPCP_OK);
    CHECK(n <= PPCP_BS_MAX_FRAME);
    CHECK_EQ_I(wire[4], 255);                       /* 11.4a, every time */
    CHECK_EQ_I(ppcp_bs_frame_read(wire, n, &back, &consumed), PPCP_OK);
    CHECK_EQ_I(consumed, n);
    CHECK_EQ_I(back.ty, in->ty);
}

/* Corrupts one octet of a valid frame and asserts the result is malformed —
 * never accepted, never a partial read. */
static void check_mutation_is_malformed(const uint8_t *wire, size_t n,
                                        size_t offset, uint8_t value)
{
    uint8_t       copy[PPCP_BS_MAX_FRAME];
    ppcp_bs_frame f;
    size_t        consumed = 0;

    memcpy(copy, wire, n);
    copy[offset] = value;
    CHECK_EQ_I(ppcp_bs_frame_read(copy, n, &f, &consumed), PPCP_ERR_MALFORMED);
}

int main(void)
{
    ppcp_bs_frame f, g;
    uint8_t       wire[PPCP_BS_MAX_FRAME], want[PPCP_BS_MAX_FRAME];
    size_t        n = 0, want_len, consumed = 0;

    /* ------------------------------------------------------------ trap 1
     *
     * ⛔ THE STANDING GUARD.  ppcp_channel_validate() rejects 255 and MUST go
     * on rejecting it — that rejection IS 11.4a's fail-closed property, and
     * relaxing it to let a bootstrap frame out would delete the safety
     * argument while implementing the clause that relies on it.  If a future
     * change relaxes the validator, THIS is what fails, and it fails saying
     * why. */
    TEST("trap 1 / 11.4a — the PPCP channel rule still refuses 255, and must");
    CHECK_EQ_I(ppcp_channel_validate(PPCP_CHANNEL_RESERVED), PPCP_ERR_MALFORMED);
    CHECK_EQ_I(ppcp_frame_check_length(PPCP_CHANNEL_RESERVED, 8u), PPCP_ERR_MALFORMED);
    {
        /* The PPCP frame writer cannot emit a bootstrap frame, by design.  The
         * bootstrap path below is separate (CA6) and does not consult it. */
        uint8_t body[4] = { 0, 1, 2, 3 };
        size_t  written = 0;
        CHECK_EQ_I(ppcp_frame_write(wire, sizeof(wire), PPCP_CHANNEL_RESERVED,
                                    body, sizeof(body), &written),
                   PPCP_ERR_MALFORMED);
    }

    /* ------------------------------------------------- deterministic bytes */

    TEST("11.4a / 4.3a — bs_offer encodes deterministically, v first, channel 255");
    memset(&f, 0, sizeof(f));
    f.ty = PPCP_BS_OFFER;
    f.v  = PPCP_BS_VERSION;
    (void)ppcp_unhex(CT_HEX, f.ct, sizeof(f.ct));
    CHECK_EQ_I(ppcp_bs_frame_write(&f, wire, sizeof(wire), &n), PPCP_OK);
    want_len = ppcp_unhex(OFFER_HEX, want, sizeof(want));
    CHECK_BYTES(wire, n, want, want_len);

    TEST("11.4d — `v` really is the first key, and a frame with it later is refused");
    {
        /* The same three pairs, `ty` first: legal CBOR, correct values, and
         * malformed — 11.4d is a MUST about the frame, so that a peer meeting
         * a version it does not implement decodes far enough to say so. */
        static const char TY_FIRST_HEX[] =
            "0000002d" "ff" "00" "0000"
            "a3"
            "627479" "01"
            "6176" "01"
            "626374" "5820"
            "f32cd8e62f80f76adb4ba21971efbd10eb71aa6715d9e458f5422c1644357a3a";
        uint8_t bad[PPCP_BS_MAX_FRAME];
        size_t  bad_len = ppcp_unhex(TY_FIRST_HEX, bad, sizeof(bad));
        CHECK_EQ_I(ppcp_bs_frame_read(bad, bad_len, &g, &consumed), PPCP_ERR_MALFORMED);
    }

    /* ------------------------------------------------------- round trips */

    TEST("11.4b — all five frame types round-trip");
    check_round_trip(&f);                                   /* bs_offer */

    memset(&g, 0, sizeof(g));
    g.ty = PPCP_BS_ACCEPT;
    g.v  = PPCP_BS_VERSION;
    (void)ppcp_unhex(PK_A_HEX, g.pk, sizeof(g.pk));
    check_round_trip(&g);

    g.ty = PPCP_BS_REVEAL;
    check_round_trip(&g);

    memset(&g, 0, sizeof(g));
    g.ty = PPCP_BS_CONFIRM;
    (void)ppcp_unhex(MAC_I_HEX, g.mac, sizeof(g.mac));
    check_round_trip(&g);

    TEST("11.4g — bs_abort carries `rc` and nothing else, for all seven codes");
    {
        int code;
        for (code = PPCP_BS_RC_UNSUPPORTED_VERSION; code <= PPCP_BS_RC_MALFORMED; code++) {
            ppcp_bs_frame a;
            memset(&a, 0, sizeof(a));
            a.ty = PPCP_BS_ABORT;
            a.rc = (ppcp_bs_reason)code;
            CHECK_EQ_I(ppcp_bs_frame_write(&a, wire, sizeof(wire), &n), PPCP_OK);
            CHECK_EQ_I(ppcp_bs_frame_read(wire, n, &g, &consumed), PPCP_OK);
            CHECK_EQ_I(g.rc, code);
            /* Two keys only: `rc` and `ty`.  There is nowhere to put a
             * message, a diagnostic string or a peer name. */
            CHECK_EQ_I(n, PPCP_FRAME_HEADER_BYTES + 9u);
        }
    }

    /* ⛔ 11.4f — a user's refusal and a failed MAC are the SAME code on the
     * wire and are indistinguishable to the counterpart.
     *
     * ⚠ WHAT CAN HONESTLY BE ASSERTED HERE, AND WHAT CANNOT.  Writing `rc: 4`
     * twice and observing the bytes match proves nothing — the two causes are
     * the same input to this function.  The clause is about the ENGINE
     * choosing the same code for two different events, and that is asserted in
     * test_bs_engine.c, which drives a refusal and a MAC failure and compares
     * the frames they emit.  What this row asserts is the enabling half: an
     * abort frame has nowhere to carry a distinguishing detail (11.4g), so
     * whatever the engine chooses, nothing else leaks alongside it. */
    TEST("11.4g — an abort has nowhere to carry a detail that could separate the two");
    {
        ppcp_bs_frame a;
        size_t        ln = 0, consumed_a = 0;
        uint8_t       out_a[PPCP_BS_MAX_FRAME];

        memset(&a, 0, sizeof(a));
        a.ty = PPCP_BS_ABORT;
        a.rc = PPCP_BS_RC_REJECTED;
        CHECK_EQ_I(ppcp_bs_frame_write(&a, out_a, sizeof(out_a), &ln), PPCP_OK);
        /* Exactly `rc` and `ty`, and the decoder refuses anything more. */
        CHECK_EQ_I(ln, PPCP_FRAME_HEADER_BYTES + 9u);
        CHECK_EQ_I(out_a[PPCP_FRAME_HEADER_BYTES], 0xa2);      /* map(2), not map(3) */
        CHECK_EQ_I(ppcp_bs_frame_read(out_a, ln, &g, &consumed_a), PPCP_OK);
        CHECK_EQ_I(g.rc, PPCP_BS_RC_REJECTED);
    }

    /* ------------------------------------------------------ what is refused */

    memset(&f, 0, sizeof(f));
    f.ty = PPCP_BS_OFFER;
    f.v  = PPCP_BS_VERSION;
    (void)ppcp_unhex(CT_HEX, f.ct, sizeof(f.ct));
    CHECK_EQ_I(ppcp_bs_frame_write(&f, wire, sizeof(wire), &n), PPCP_OK);

    TEST("11.4a — a frame on any channel but 255 is refused on this connection");
    check_mutation_is_malformed(wire, n, 4, 0);       /* the PPCP control channel */
    check_mutation_is_malformed(wire, n, 4, 1);       /* bulk */

    TEST("11.4c — an unknown `ty` is malformed");
    check_mutation_is_malformed(wire, n, n - 1u, 6);  /* ty: 6 */
    check_mutation_is_malformed(wire, n, n - 1u, 0);  /* ty: 0 */

    TEST("11.4h1 — a `v` outside 1..255 is malformed");
    check_mutation_is_malformed(wire, n, 11, 0);      /* v: 0 */

    TEST("11.4c1 / E46 — an unrecognised map key is MALFORMED, not skipped");
    {
        /* The same bs_offer with one extra, harmless-looking key.  Every other
         * decoder in the protocol set would skip it; this one must not. */
        static const char EXTRA_KEY_HEX[] =
            "00000032" "ff" "00" "0000"
            "a4"
            "6176" "01"
            "626374" "5820"
            "f32cd8e62f80f76adb4ba21971efbd10eb71aa6715d9e458f5422c1644357a3a"
            "627479" "01"
            "6378797a" "01";           /* "xyz": 1 — unrecognised */
        uint8_t bad[PPCP_BS_MAX_FRAME];
        size_t  bad_len = ppcp_unhex(EXTRA_KEY_HEX, bad, sizeof(bad));
        CHECK_EQ_I(ppcp_bs_frame_read(bad, bad_len, &g, &consumed), PPCP_ERR_MALFORMED);
    }

    TEST("11.4c — a key belonging to another frame type is malformed");
    {
        /* bs_reveal carrying `v`: every field well-formed, the set wrong. */
        static const char REVEAL_WITH_V_HEX[] =
            "0000002d" "ff" "00" "0000"
            "a3"
            "6176" "01"
            "62706b" "5820"
            "675dd574ed7789310b3d2e7681f3790b466c773b1521fecf36577958371ea52f"
            "627479" "03";
        uint8_t bad[PPCP_BS_MAX_FRAME];
        size_t  bad_len = ppcp_unhex(REVEAL_WITH_V_HEX, bad, sizeof(bad));
        CHECK_EQ_I(ppcp_bs_frame_read(bad, bad_len, &g, &consumed), PPCP_ERR_MALFORMED);
    }

    TEST("11.4c — a missing field is malformed");
    {
        static const char OFFER_NO_CT_HEX[] =
            "00000008" "ff" "00" "0000"
            "a2"
            "6176" "01"
            "627479" "01";
        uint8_t bad[PPCP_BS_MAX_FRAME];
        size_t  bad_len = ppcp_unhex(OFFER_NO_CT_HEX, bad, sizeof(bad));
        CHECK_EQ_I(ppcp_bs_frame_read(bad, bad_len, &g, &consumed), PPCP_ERR_MALFORMED);
    }

    TEST("11.4c — a field of the wrong length is malformed");
    {
        /* `ct` of 31 octets: the right type, one byte short. */
        static const char SHORT_CT_HEX[] =
            "0000002c" "ff" "00" "0000"
            "a3"
            "6176" "01"
            "626374" "581f"
            "f32cd8e62f80f76adb4ba21971efbd10eb71aa6715d9e458f5422c1644357a"
            "627479" "01";
        uint8_t bad[PPCP_BS_MAX_FRAME];
        size_t  bad_len = ppcp_unhex(SHORT_CT_HEX, bad, sizeof(bad));
        CHECK_EQ_I(ppcp_bs_frame_read(bad, bad_len, &g, &consumed), PPCP_ERR_MALFORMED);
    }

    TEST("11.4c — a field of the wrong type is malformed");
    {
        /* `v` as a text string rather than an unsigned integer. */
        static const char TEXT_V_HEX[] =
            "0000002e" "ff" "00" "0000"
            "a3"
            "6176" "6131"
            "626374" "5820"
            "f32cd8e62f80f76adb4ba21971efbd10eb71aa6715d9e458f5422c1644357a3a"
            "627479" "01";
        uint8_t bad[PPCP_BS_MAX_FRAME];
        size_t  bad_len = ppcp_unhex(TEXT_V_HEX, bad, sizeof(bad));
        CHECK_EQ_I(ppcp_bs_frame_read(bad, bad_len, &g, &consumed), PPCP_ERR_MALFORMED);
    }

    TEST("ENC 3a — an oversized payload_len is refused when the head is read");
    {
        uint8_t big[PPCP_BS_MAX_FRAME];
        memcpy(big, wire, n);
        big[3] = 0xffu;                 /* payload_len well past the vocabulary */
        CHECK_EQ_I(ppcp_bs_frame_read(big, n, &g, &consumed), PPCP_ERR_MALFORMED);
    }

    TEST("ENC 4d — a DUPLICATE map key is malformed, exercised rather than assumed");
    {
        /* ⛔ Confirmed by running it, not by reading the contract (machine
         * review, C2).  §11's own repeat check is `ppcp_bs_engine`'s `seen`
         * mask, and that catches a repeated FRAME, not a repeated KEY inside
         * one — so the whole of ENC 4d rests on ppcp_cbor_validate here, and
         * until now nobody had put a duplicate key in front of it.
         *
         * Two spellings of one field is two meanings, and this is a place two
         * implementations could silently disagree: a decoder taking the FIRST
         * `v` and one taking the LAST read the same octets as different
         * frames.  §11.4c1 already makes the vocabulary closed; 4d is what
         * makes a key unambiguous within it.
         *
         * A bs_offer with `v` twice — map(4), otherwise byte-identical. */
        static const char DUP_HEX[] =
            "00000030"                 /* payload_len = 48                     */
            "ff" "00" "0000"           /* channel 255, flags, reserved         */
            "a4"                       /* map(4)                               */
            "6176" "01"                /* "v": 1                               */
            "6176" "01"                /* ⛔ "v": 1 AGAIN                      */
            "626374" "5820"
            "f32cd8e62f80f76adb4ba21971efbd10eb71aa6715d9e458f5422c1644357a3a"
            "627479" "01";             /* "ty": 1                              */
        uint8_t dup[PPCP_BS_MAX_FRAME];
        size_t  dup_len = ppcp_unhex(DUP_HEX, dup, sizeof(dup));

        CHECK_EQ_I(ppcp_bs_frame_read(dup, dup_len, &g, &consumed),
                   PPCP_ERR_MALFORMED);
    }

    TEST("ENC 3c — a short buffer is TRUNCATED, and the caller decides");
    {
        size_t k;
        for (k = 0; k < n; k++)
            CHECK_EQ_I(ppcp_bs_frame_read(wire, k, &g, &consumed), PPCP_ERR_TRUNCATED);
    }

    TEST("ENC 3b — a later minor's flags and reserved bits do not fail the frame");
    {
        uint8_t bits[PPCP_BS_MAX_FRAME];
        memcpy(bits, wire, n);
        bits[5] = 0x01u;
        bits[6] = 0x00u;
        bits[7] = 0x02u;
        CHECK_EQ_I(ppcp_bs_frame_read(bits, n, &g, &consumed), PPCP_OK);
    }

    TEST("11.4e — a `v` this peer does not implement DECODES, and is the engine's business");
    {
        /* v: 2 is a well-formed frame.  It must not be confused with a
         * malformed one: 11.4e requires the peer to abort with
         * unsupported_version and tell its USER the counterpart needs a newer
         * application, which is a different outcome and a different message. */
        uint8_t v2[PPCP_BS_MAX_FRAME];
        memcpy(v2, wire, n);
        v2[11] = 0x02u;
        CHECK_EQ_I(ppcp_bs_frame_read(v2, n, &g, &consumed), PPCP_OK);
        CHECK_EQ_I(g.v, 2);
    }

    TEST("11.4a — the frames never grow: the largest is bs_accept, well inside the cap");
    {
        ppcp_bs_frame a;
        memset(&a, 0, sizeof(a));
        a.ty = PPCP_BS_ACCEPT;
        a.v  = PPCP_BS_VERSION;
        (void)ppcp_unhex(PK_A_HEX, a.pk, sizeof(a.pk));
        CHECK_EQ_I(ppcp_bs_frame_write(&a, wire, sizeof(wire), &n), PPCP_OK);
        CHECK_EQ_I(n, PPCP_FRAME_HEADER_BYTES + 45u);
        CHECK(n <= PPCP_BS_MAX_FRAME);
        /* And a buffer one byte short is NOSPACE, not a truncated write. */
        CHECK_EQ_I(ppcp_bs_frame_write(&a, wire, n - 1u, &n), PPCP_ERR_NOSPACE);
    }

    TEST_MAIN_END();
}
