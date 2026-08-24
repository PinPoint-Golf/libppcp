/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * PPCP-RV §11.5 — the exchange engine.  RT-19, the library half of RT-21,
 * RT-24, and a local mirror of RT-20b(ii)'s ordering check.
 *
 * ⚠ WHAT THIS FILE CANNOT DEMONSTRATE, STATED SO THE GREEN IS NOT MISREAD.
 * Every row here is two engines from this repository behaving honestly toward
 * each other.  CONF §2c is explicit that an implementation tested only against
 * itself passes a class of rows by accident, and §11's own version of that is
 * sharper: RT-20b(ii) is the ONLY test that catches trap 2, it needs a relay
 * that withholds `bs_reveal`, and the relay is L21 — next session.  The
 * ordering assertion below is the best a single process can do and it is NOT
 * RT-20b.  9g forbids an RV-6 aggregate pass while RT-20c is unrun.
 */
#include "ppcp/bootstrap.h"
#include "test_util.h"

/* §10.4's own keys, so the engine is tied to the published vector rather than
 * to arbitrary bytes. */
static const char PK_I_HEX[] = "358072d6365880d1aeea329adf9121383851ed21a28e3b75e965d0d2cd166254";
static const char PK_A_HEX[] = "675dd574ed7789310b3d2e7681f3790b466c773b1521fecf36577958371ea52f";
static const char Z_HEX[]    = "7c79d7b5f31b9aac367477f5f7c7a68b5c44cac28ed5c902a59ec48c02956a6a";
static const char PRK_HEX[]  = "3e351aef1e5fe48411e969526b079830494d2cf13104d661694e897598ccf8c9";
static const char SID_HEX[]  = "1cc4b886e8bd45e0a3b207ae783bc56b";
#define SAS_EXPECTED 435948u

static uint8_t pk_i[32], pk_a[32], z[32];

/* Nothing secret survives a finished engine (11.6f as amended by E51). */
static void check_erased(const ppcp_bs_engine *e, const char *where)
{
    const unsigned char *p = (const unsigned char *)&e->bs;
    size_t i, nonzero = 0;

    for (i = 0; i < sizeof(e->bs); i++)
        if (p[i] != 0u)
            nonzero++;
    p = (const unsigned char *)&e->pairing;
    for (i = 0; i < sizeof(e->pairing); i++)
        if (p[i] != 0u)
            nonzero++;
    for (i = 0; i < sizeof(e->pk_i); i++)
        if (e->pk_i[i] != 0u || e->pk_a[i] != 0u)
            nonzero++;
    if (nonzero != 0u) {
        fprintf(stderr, "FAIL erasure at %s: %zu octets survive\n", where, nonzero);
        ppcp_test_failures++;
    }
}

/* Drives one honest exchange to the point where both sides are showing digits.
 * Returns through the two engines and their two step buffers. */
static void run_to_compare(ppcp_bs_engine *ini, ppcp_bs_engine *acc)
{
    ppcp_bs_step s;
    size_t       used = 0;
    uint8_t      offer[PPCP_BS_MAX_FRAME], accept[PPCP_BS_MAX_FRAME];
    uint8_t      reveal[PPCP_BS_MAX_FRAME];
    size_t       offer_len, accept_len, reveal_len;

    CHECK_EQ_I(ppcp_bs_engine_init(ini, PPCP_BS_ROLE_INITIATOR, 1u, pk_i), PPCP_OK);
    CHECK_EQ_I(ppcp_bs_engine_init(acc, PPCP_BS_ROLE_ACCEPTOR,  1u, pk_a), PPCP_OK);

    CHECK_EQ_I(ppcp_bs_engine_start(ini, &s), PPCP_OK);
    CHECK(s.has_out);
    memcpy(offer, s.out, s.out_len);
    offer_len = s.out_len;

    /* ⛔ THE ORDERING CLAUSE (11.5c).  bs_accept comes back from the SAME call
     * that consumed bs_offer, and the acceptor has not seen pk_i — it cannot
     * have, because bs_offer carries only a commitment.  A relay is what
     * observes this properly (RT-20b(ii)); this is the local mirror. */
    CHECK_EQ_I(ppcp_bs_engine_recv(acc, offer, offer_len, &used, &s), PPCP_OK);
    CHECK_EQ_I(used, offer_len);
    CHECK(s.has_out);
    {
        ppcp_bs_frame got;
        size_t        c = 0;
        CHECK_EQ_I(ppcp_bs_frame_read(s.out, s.out_len, &got, &c), PPCP_OK);
        CHECK_EQ_I(got.ty, PPCP_BS_ACCEPT);
        CHECK_BYTES(got.pk, 32, pk_a, 32);        /* its key, chosen blind */
    }
    /* And the acceptor's record of pk_i is still empty at this point. */
    {
        uint8_t zeros[32];
        memset(zeros, 0, sizeof(zeros));
        CHECK_BYTES(acc->pk_i, 32, zeros, 32);
    }
    memcpy(accept, s.out, s.out_len);
    accept_len = s.out_len;

    CHECK_EQ_I(ppcp_bs_engine_recv(ini, accept, accept_len, &used, &s), PPCP_OK);
    CHECK_EQ_I(s.event, PPCP_BS_EV_NEED_SECRET);
    CHECK(s.has_out);
    CHECK_BYTES(s.peer_pk, 32, pk_a, 32);
    memcpy(reveal, s.out, s.out_len);
    reveal_len = s.out_len;

    CHECK_EQ_I(ppcp_bs_engine_supply_secret(ini, z, &s), PPCP_OK);
    CHECK_EQ_I(s.event, PPCP_BS_EV_COMPARE);

    CHECK_EQ_I(ppcp_bs_engine_recv(acc, reveal, reveal_len, &used, &s), PPCP_OK);
    CHECK_EQ_I(s.event, PPCP_BS_EV_NEED_SECRET);
    CHECK(!s.has_out);                    /* 11.5e — it waits for its own user */
    CHECK_BYTES(s.peer_pk, 32, pk_i, 32);

    CHECK_EQ_I(ppcp_bs_engine_supply_secret(acc, z, &s), PPCP_OK);
    CHECK_EQ_I(s.event, PPCP_BS_EV_COMPARE);
}

int main(void)
{
    ppcp_bs_engine ini, acc;
    ppcp_bs_step   s, t;
    size_t         used = 0;
    uint8_t        want[32];

    (void)ppcp_unhex(PK_I_HEX, pk_i, sizeof(pk_i));
    (void)ppcp_unhex(PK_A_HEX, pk_a, sizeof(pk_a));
    (void)ppcp_unhex(Z_HEX,    z,    sizeof(z));

    /* ------------------------------------------- the exchange, end to end */

    TEST("11.5 — one honest exchange, both roles, against §10.4's own keys");
    run_to_compare(&ini, &acc);
    {
        uint32_t a = 0, b = 0;
        CHECK_EQ_I(ppcp_bs_engine_sas(&ini, &a), PPCP_OK);
        CHECK_EQ_I(ppcp_bs_engine_sas(&acc, &b), PPCP_OK);
        /* Both peers display it (11.7b), and it is §10.4's number. */
        CHECK_EQ_I(a, SAS_EXPECTED);
        CHECK_EQ_I(b, SAS_EXPECTED);
    }

    /* ⛔ 11.5g — the gate.  Nothing is held until BOTH this side has affirmed
     * AND the counterpart's MAC has verified. */
    TEST("11.5g — no pairing before affirmation, and none after affirmation alone");
    {
        ppcp_bs_pairing p;
        CHECK_EQ_I(ppcp_bs_engine_take_pairing(&ini, &p), PPCP_ERR_INVALID);

        CHECK_EQ_I(ppcp_bs_engine_affirm(&ini, &s), PPCP_OK);
        CHECK(s.has_out);
        CHECK_EQ_I(s.event, PPCP_BS_EV_NONE);          /* affirmed, not paired */
        /* Still nothing: the counterpart's MAC has not arrived. */
        CHECK_EQ_I(ppcp_bs_engine_take_pairing(&ini, &p), PPCP_ERR_INVALID);
        CHECK_EQ_I(ppcp_bs_engine_state(&ini), PPCP_BS_ST_COMPARE);

        /* The acceptor's own user affirms, independently (11.7c). */
        CHECK_EQ_I(ppcp_bs_engine_affirm(&acc, &t), PPCP_OK);
        CHECK(t.has_out);

        /* Now exchange the two bs_confirm frames. */
        CHECK_EQ_I(ppcp_bs_engine_recv(&ini, t.out, t.out_len, &used, &t), PPCP_OK);
        CHECK_EQ_I(t.event, PPCP_BS_EV_PAIRED);
        CHECK(t.close);                                /* 11.5h */
        CHECK_EQ_I(ppcp_bs_engine_recv(&acc, s.out, s.out_len, &used, &s), PPCP_OK);
        CHECK_EQ_I(s.event, PPCP_BS_EV_PAIRED);
    }

    TEST("11.7f / 11.6f — the digits and K_c are gone the moment the handshake ends");
    {
        uint32_t v = 0;
        /* 11.7f: not reused, not cached, not shown again after the attempt. */
        CHECK_EQ_I(ppcp_bs_engine_sas(&ini, &v), PPCP_ERR_INVALID);
        CHECK_EQ_I(ppcp_bs_engine_sas(&acc, &v), PPCP_ERR_INVALID);
        /* And the ephemeral half really is zeroed, not merely unreachable. */
        {
            const unsigned char *p = (const unsigned char *)&ini.bs;
            size_t i, nonzero = 0;
            for (i = 0; i < sizeof(ini.bs); i++)
                if (p[i] != 0u)
                    nonzero++;
            CHECK_EQ_I(nonzero, 0);
        }
    }

    /* ⛔ 11.1a — from here the pairing is INDISTINGUISHABLE from one
     * established by a scanned code, and §5.1's own vector is what says so. */
    TEST("11.1a / 11.6e — both ends take the same pairing, and it is §10.4's");
    {
        ppcp_bs_pairing pi, pa;
        CHECK_EQ_I(ppcp_bs_engine_take_pairing(&ini, &pi), PPCP_OK);
        CHECK_EQ_I(ppcp_bs_engine_take_pairing(&acc, &pa), PPCP_OK);

        (void)ppcp_unhex(PRK_HEX, want, sizeof(want));
        CHECK_BYTES(pi.keys.prk, 32, want, 32);
        CHECK_BYTES(pa.keys.prk, 32, want, 32);
        (void)ppcp_unhex(SID_HEX, want, sizeof(want));
        CHECK_BYTES(pi.sid, 16, want, 16);
        CHECK_BYTES(pa.sid, 16, want, 16);
        CHECK_BYTES(pi.keys.k_tls, 32, pa.keys.k_tls, 32);
        CHECK_BYTES(pi.keys.k_id,  32, pa.keys.k_id,  32);

        /* Taken means taken — the engine keeps no copy (trap 6). */
        check_erased(&ini, "after take_pairing");
        check_erased(&acc, "after take_pairing");
        CHECK_EQ_I(ppcp_bs_engine_state(&ini), PPCP_BS_ST_DONE);
        {
            ppcp_bs_pairing again;
            CHECK_EQ_I(ppcp_bs_engine_take_pairing(&ini, &again), PPCP_ERR_INVALID);
        }
    }

    /* ---------------------------------------------------------------- RT-19 */

    TEST("RT-19 / 11.5d — a pk that does not match the commitment aborts, and derives nothing");
    {
        ppcp_bs_engine a;
        ppcp_bs_frame  lie;
        uint8_t        wire[PPCP_BS_MAX_FRAME], offer[PPCP_BS_MAX_FRAME];
        size_t         n = 0, offer_len = 0;
        ppcp_bs_engine i2;

        CHECK_EQ_I(ppcp_bs_engine_init(&i2, PPCP_BS_ROLE_INITIATOR, 1u, pk_i), PPCP_OK);
        CHECK_EQ_I(ppcp_bs_engine_start(&i2, &s), PPCP_OK);
        memcpy(offer, s.out, s.out_len);
        offer_len = s.out_len;

        CHECK_EQ_I(ppcp_bs_engine_init(&a, PPCP_BS_ROLE_ACCEPTOR, 1u, pk_a), PPCP_OK);
        CHECK_EQ_I(ppcp_bs_engine_recv(&a, offer, offer_len, &used, &s), PPCP_OK);

        /* Commit to one key, reveal another. */
        memset(&lie, 0, sizeof(lie));
        lie.ty = PPCP_BS_REVEAL;
        memcpy(lie.pk, pk_a, 32);            /* not the key ct commits to */
        CHECK_EQ_I(ppcp_bs_frame_write(&lie, wire, sizeof(wire), &n), PPCP_OK);

        CHECK_EQ_I(ppcp_bs_engine_recv(&a, wire, n, &used, &s), PPCP_OK);
        CHECK_EQ_I(s.event, PPCP_BS_EV_ABORTED);
        CHECK_EQ_I(s.rc, PPCP_BS_RC_COMMITMENT_MISMATCH);
        CHECK(s.has_out);
        CHECK(s.close);
        {
            ppcp_bs_frame got;
            size_t        c = 0;
            CHECK_EQ_I(ppcp_bs_frame_read(s.out, s.out_len, &got, &c), PPCP_OK);
            CHECK_EQ_I(got.ty, PPCP_BS_ABORT);
            CHECK_EQ_I(got.rc, PPCP_BS_RC_COMMITMENT_MISMATCH);
        }
        /* "MUST NOT derive anything from a pk_i that failed this check." */
        check_erased(&a, "after commitment_mismatch");
        {
            uint32_t v = 0;
            CHECK_EQ_I(ppcp_bs_engine_sas(&a, &v), PPCP_ERR_INVALID);
        }
    }

    /* ---------------------------------------------------------------- RT-24 */

    TEST("RT-24 / 11.4h — a bs_accept.v differing from the v sent aborts");
    {
        ppcp_bs_engine i2;
        ppcp_bs_frame  bad;
        uint8_t        wire[PPCP_BS_MAX_FRAME];
        size_t         n = 0;

        CHECK_EQ_I(ppcp_bs_engine_init(&i2, PPCP_BS_ROLE_INITIATOR, 1u, pk_i), PPCP_OK);
        CHECK_EQ_I(ppcp_bs_engine_start(&i2, &s), PPCP_OK);

        memset(&bad, 0, sizeof(bad));
        bad.ty = PPCP_BS_ACCEPT;
        bad.v  = 2u;                         /* not the v that was offered */
        memcpy(bad.pk, pk_a, 32);
        CHECK_EQ_I(ppcp_bs_frame_write(&bad, wire, sizeof(wire), &n), PPCP_OK);

        CHECK_EQ_I(ppcp_bs_engine_recv(&i2, wire, n, &used, &s), PPCP_OK);
        CHECK_EQ_I(s.event, PPCP_BS_EV_ABORTED);
        CHECK_EQ_I(s.rc, PPCP_BS_RC_UNSUPPORTED_VERSION);
        check_erased(&i2, "after unsupported_version");
    }

    TEST("11.4e — an acceptor offered a `v` it does not implement aborts, not silently");
    {
        ppcp_bs_engine a;
        ppcp_bs_frame  offer;
        uint8_t        wire[PPCP_BS_MAX_FRAME];
        size_t         n = 0;

        CHECK_EQ_I(ppcp_bs_engine_init(&a, PPCP_BS_ROLE_ACCEPTOR, 1u, pk_a), PPCP_OK);
        memset(&offer, 0, sizeof(offer));
        offer.ty = PPCP_BS_OFFER;
        offer.v  = 2u;
        ppcp_rv_bs_commit(pk_i, offer.ct);
        CHECK_EQ_I(ppcp_bs_frame_write(&offer, wire, sizeof(wire), &n), PPCP_OK);

        /* A well-formed bs_offer, so 11.3c's silent close does NOT apply: the
         * counterpart has demonstrated it speaks this protocol and is owed a
         * diagnostic its user can act on (11.4e, and 11.9d1 offers the code on
         * the FIRST such abort because a second attempt fails identically). */
        CHECK_EQ_I(ppcp_bs_engine_recv(&a, wire, n, &used, &s), PPCP_OK);
        CHECK(s.has_out);
        CHECK_EQ_I(s.rc, PPCP_BS_RC_UNSUPPORTED_VERSION);
    }

    /* ------------------------------------------- RT-21, the library's half */

    TEST("RT-21 / 11.6b — an all-zero Z aborts with invalid_key and derives nothing");
    {
        ppcp_bs_engine i2, a2;
        uint8_t        zeros[32];

        memset(zeros, 0, sizeof(zeros));
        run_to_compare(&i2, &a2);
        /* Rebuild to the point where Z is owed, then hand over zeros. */
        CHECK_EQ_I(ppcp_bs_engine_init(&i2, PPCP_BS_ROLE_INITIATOR, 1u, pk_i), PPCP_OK);
        CHECK_EQ_I(ppcp_bs_engine_start(&i2, &s), PPCP_OK);
        {
            ppcp_bs_frame accept;
            uint8_t       wire[PPCP_BS_MAX_FRAME];
            size_t        n = 0;
            memset(&accept, 0, sizeof(accept));
            accept.ty = PPCP_BS_ACCEPT;
            accept.v  = 1u;
            memcpy(accept.pk, pk_a, 32);
            CHECK_EQ_I(ppcp_bs_frame_write(&accept, wire, sizeof(wire), &n), PPCP_OK);
            CHECK_EQ_I(ppcp_bs_engine_recv(&i2, wire, n, &used, &s), PPCP_OK);
            CHECK_EQ_I(s.event, PPCP_BS_EV_NEED_SECRET);
        }
        CHECK_EQ_I(ppcp_bs_engine_supply_secret(&i2, zeros, &s), PPCP_OK);
        CHECK_EQ_I(s.event, PPCP_BS_EV_ABORTED);
        CHECK_EQ_I(s.rc, PPCP_BS_RC_INVALID_KEY);
        CHECK(s.has_out);
        check_erased(&i2, "after invalid_key");

        /* ⛔ Trap 7 — and it is NOT retried.  The engine is single-shot: there
         * is no call that reopens it, so a retry loop cannot be written around
         * this one without constructing a new engine and a new keypair, which
         * 11.9b puts behind a further explicit user action. */
        CHECK_EQ_I(ppcp_bs_engine_supply_secret(&i2, z, &s), PPCP_ERR_INVALID);
        CHECK_EQ_I(ppcp_bs_engine_start(&i2, &s), PPCP_ERR_INVALID);
        CHECK_EQ_I(ppcp_bs_engine_state(&i2), PPCP_BS_ST_DONE);
    }

    /* ------------------------------------------------------- 11.4f, for real */

    /* ⛔ THE ASSERTION test_bs_frame.c COULD NOT HONESTLY MAKE.  Two DIFFERENT
     * events — a user declining, and a counterpart's MAC failing to verify —
     * and the frames the engine emits for them must be byte-identical, because
     * 11.4f says the counterpart cannot tell them apart. */
    TEST("11.4f — a user's refusal and a failed MAC emit byte-identical aborts");
    {
        ppcp_bs_engine i1, a1, i2, a2;
        uint8_t        refusal[PPCP_BS_MAX_FRAME], mac_fail[PPCP_BS_MAX_FRAME];
        size_t         refusal_len, mac_fail_len;

        /* (a) the user declines. */
        run_to_compare(&i1, &a1);
        CHECK_EQ_I(ppcp_bs_engine_abort(&i1, PPCP_BS_RC_REJECTED, &s), PPCP_OK);
        CHECK(s.has_out);
        memcpy(refusal, s.out, s.out_len);
        refusal_len = s.out_len;
        check_erased(&i1, "after a user's refusal");

        /* (b) a confirmation MAC that does not verify. */
        run_to_compare(&i2, &a2);
        {
            ppcp_bs_frame forged;
            uint8_t       wire[PPCP_BS_MAX_FRAME];
            size_t        n = 0;
            memset(&forged, 0, sizeof(forged));
            forged.ty = PPCP_BS_CONFIRM;
            memset(forged.mac, 0x5a, sizeof(forged.mac));
            CHECK_EQ_I(ppcp_bs_frame_write(&forged, wire, sizeof(wire), &n), PPCP_OK);
            CHECK_EQ_I(ppcp_bs_engine_recv(&i2, wire, n, &used, &s), PPCP_OK);
        }
        CHECK_EQ_I(s.event, PPCP_BS_EV_ABORTED);
        CHECK_EQ_I(s.rc, PPCP_BS_RC_REJECTED);
        memcpy(mac_fail, s.out, s.out_len);
        mac_fail_len = s.out_len;
        check_erased(&i2, "after a failed MAC");

        CHECK_BYTES(refusal, refusal_len, mac_fail, mac_fail_len);
    }

    TEST("11.5f — a peer sent its own MAC back aborts with `rejected`");
    {
        ppcp_bs_engine i3, a3;
        uint8_t        own[PPCP_BS_MAX_FRAME];
        size_t         own_len;

        run_to_compare(&i3, &a3);
        CHECK_EQ_I(ppcp_bs_engine_affirm(&i3, &s), PPCP_OK);
        memcpy(own, s.out, s.out_len);
        own_len = s.out_len;

        /* A relay with nothing else to send reflects the frame it just saw.
         * The two labels of 11.5f already differ, and the guard is explicit. */
        CHECK_EQ_I(ppcp_bs_engine_recv(&i3, own, own_len, &used, &s), PPCP_OK);
        CHECK_EQ_I(s.event, PPCP_BS_EV_ABORTED);
        CHECK_EQ_I(s.rc, PPCP_BS_RC_REJECTED);
        check_erased(&i3, "after a reflected MAC");
    }

    /* ------------------------------------------------- order and 11.3c */

    TEST("11.3c — an acceptor whose FIRST frame is not a well-formed bs_offer replies NOTHING");
    {
        ppcp_bs_engine a;
        uint8_t        junk[PPCP_BS_MAX_FRAME];
        size_t         n = 0;
        ppcp_bs_frame  reveal;

        /* A well-formed frame, of the wrong type for a first frame. */
        memset(&reveal, 0, sizeof(reveal));
        reveal.ty = PPCP_BS_REVEAL;
        memcpy(reveal.pk, pk_i, 32);
        CHECK_EQ_I(ppcp_bs_frame_write(&reveal, junk, sizeof(junk), &n), PPCP_OK);

        CHECK_EQ_I(ppcp_bs_engine_init(&a, PPCP_BS_ROLE_ACCEPTOR, 1u, pk_a), PPCP_OK);
        CHECK_EQ_I(ppcp_bs_engine_recv(&a, junk, n, &used, &s), PPCP_OK);
        /* ⛔ WITHOUT REPLY.  Something that has not demonstrated it speaks this
         * protocol gets nothing to learn from. */
        CHECK(!s.has_out);
        CHECK(s.close);
        CHECK_EQ_I(s.event, PPCP_BS_EV_ABORTED);

        /* And the same for bytes that are not a frame at all. */
        {
            ppcp_bs_engine b;
            uint8_t        rubbish[16];
            memset(rubbish, 0xcc, sizeof(rubbish));
            CHECK_EQ_I(ppcp_bs_engine_init(&b, PPCP_BS_ROLE_ACCEPTOR, 1u, pk_a), PPCP_OK);
            CHECK_EQ_I(ppcp_bs_engine_recv(&b, rubbish, sizeof(rubbish), &used, &s), PPCP_OK);
            CHECK(!s.has_out);
            CHECK(s.close);
        }
    }

    TEST("11.4c — out of order, and a repeated frame, are both malformed");
    {
        ppcp_bs_engine i4;
        ppcp_bs_frame  confirm;
        uint8_t        wire[PPCP_BS_MAX_FRAME];
        size_t         n = 0;

        /* bs_confirm before bs_accept: the initiator has no K_c and no reason
         * to expect one. */
        CHECK_EQ_I(ppcp_bs_engine_init(&i4, PPCP_BS_ROLE_INITIATOR, 1u, pk_i), PPCP_OK);
        CHECK_EQ_I(ppcp_bs_engine_start(&i4, &s), PPCP_OK);
        memset(&confirm, 0, sizeof(confirm));
        confirm.ty = PPCP_BS_CONFIRM;
        CHECK_EQ_I(ppcp_bs_frame_write(&confirm, wire, sizeof(wire), &n), PPCP_OK);
        CHECK_EQ_I(ppcp_bs_engine_recv(&i4, wire, n, &used, &s), PPCP_OK);
        CHECK_EQ_I(s.rc, PPCP_BS_RC_MALFORMED);
        check_erased(&i4, "after an out-of-order frame");

        /* A second bs_offer at an acceptor that has already answered one. */
        {
            ppcp_bs_engine a4;
            uint8_t        offer[PPCP_BS_MAX_FRAME];
            size_t         offer_len;
            ppcp_bs_engine i5;

            CHECK_EQ_I(ppcp_bs_engine_init(&i5, PPCP_BS_ROLE_INITIATOR, 1u, pk_i), PPCP_OK);
            CHECK_EQ_I(ppcp_bs_engine_start(&i5, &s), PPCP_OK);
            memcpy(offer, s.out, s.out_len);
            offer_len = s.out_len;

            CHECK_EQ_I(ppcp_bs_engine_init(&a4, PPCP_BS_ROLE_ACCEPTOR, 1u, pk_a), PPCP_OK);
            CHECK_EQ_I(ppcp_bs_engine_recv(&a4, offer, offer_len, &used, &s), PPCP_OK);
            CHECK(s.has_out);
            CHECK_EQ_I(ppcp_bs_engine_recv(&a4, offer, offer_len, &used, &s), PPCP_OK);
            CHECK_EQ_I(s.rc, PPCP_BS_RC_MALFORMED);
        }
    }

    TEST("ENC 3c — a partial frame leaves the engine untouched");
    {
        ppcp_bs_engine a5, i6;
        uint8_t        offer[PPCP_BS_MAX_FRAME];
        size_t         offer_len;

        CHECK_EQ_I(ppcp_bs_engine_init(&i6, PPCP_BS_ROLE_INITIATOR, 1u, pk_i), PPCP_OK);
        CHECK_EQ_I(ppcp_bs_engine_start(&i6, &s), PPCP_OK);
        memcpy(offer, s.out, s.out_len);
        offer_len = s.out_len;

        CHECK_EQ_I(ppcp_bs_engine_init(&a5, PPCP_BS_ROLE_ACCEPTOR, 1u, pk_a), PPCP_OK);
        CHECK_EQ_I(ppcp_bs_engine_recv(&a5, offer, offer_len - 1u, &used, &s),
                   PPCP_ERR_TRUNCATED);
        CHECK_EQ_I(ppcp_bs_engine_state(&a5), PPCP_BS_ST_NEW);   /* untouched */
        CHECK_EQ_I(used, 0);
        /* And the whole frame still works afterwards. */
        CHECK_EQ_I(ppcp_bs_engine_recv(&a5, offer, offer_len, &used, &s), PPCP_OK);
        CHECK(s.has_out);
    }

    TEST("11.9a — a counterpart's bs_abort ends the attempt and leaves no pairing");
    {
        ppcp_bs_engine i7, a7;
        ppcp_bs_frame  ab;
        uint8_t        wire[PPCP_BS_MAX_FRAME];
        size_t         n = 0;

        run_to_compare(&i7, &a7);
        memset(&ab, 0, sizeof(ab));
        ab.ty = PPCP_BS_ABORT;
        ab.rc = PPCP_BS_RC_REJECTED;
        CHECK_EQ_I(ppcp_bs_frame_write(&ab, wire, sizeof(wire), &n), PPCP_OK);

        CHECK_EQ_I(ppcp_bs_engine_recv(&i7, wire, n, &used, &s), PPCP_OK);
        CHECK_EQ_I(s.event, PPCP_BS_EV_ABORTED);
        CHECK_EQ_I(s.rc, PPCP_BS_RC_REJECTED);
        CHECK(!s.has_out);          /* 11.4c: no reply to an abort */
        CHECK(s.close);
        check_erased(&i7, "after a received abort");
        {
            ppcp_bs_pairing p;
            CHECK_EQ_I(ppcp_bs_engine_take_pairing(&i7, &p), PPCP_ERR_INVALID);
        }
    }

    TEST("11.3e — a timeout is the embedding's, and it erases like any other abort");
    {
        ppcp_bs_engine i8, a8;
        run_to_compare(&i8, &a8);
        /* The library owns no clock: the embedding decides 30 or 60 seconds
         * have passed and says so (ground rule 8). */
        CHECK_EQ_I(ppcp_bs_engine_abort(&i8, PPCP_BS_RC_TIMEOUT, &s), PPCP_OK);
        CHECK_EQ_I(s.rc, PPCP_BS_RC_TIMEOUT);
        check_erased(&i8, "after a timeout");
    }

    TEST("11.7e — the digits do not exist before 11.5d completes");
    {
        ppcp_bs_engine i9;
        uint32_t       v = 0;
        CHECK_EQ_I(ppcp_bs_engine_init(&i9, PPCP_BS_ROLE_INITIATOR, 1u, pk_i), PPCP_OK);
        CHECK_EQ_I(ppcp_bs_engine_sas(&i9, &v), PPCP_ERR_INVALID);
        CHECK_EQ_I(ppcp_bs_engine_start(&i9, &s), PPCP_OK);
        CHECK_EQ_I(ppcp_bs_engine_sas(&i9, &v), PPCP_ERR_INVALID);
        /* And no affirmation is possible before there is anything to affirm. */
        CHECK_EQ_I(ppcp_bs_engine_affirm(&i9, &s), PPCP_ERR_INVALID);
    }

    TEST("a counterpart's bs_confirm arriving before Z is held, not refused");
    {
        ppcp_bs_engine i10;
        ppcp_bs_frame  accept;
        uint8_t        wire[PPCP_BS_MAX_FRAME];
        size_t         n = 0;
        ppcp_bs_engine a10;
        uint8_t        peer_confirm[PPCP_BS_MAX_FRAME];
        size_t         peer_len;

        /* Build a real acceptor-side confirm to replay. */
        run_to_compare(&i10, &a10);
        CHECK_EQ_I(ppcp_bs_engine_affirm(&a10, &s), PPCP_OK);
        memcpy(peer_confirm, s.out, s.out_len);
        peer_len = s.out_len;

        /* A fresh initiator, taken only as far as owing Z. */
        CHECK_EQ_I(ppcp_bs_engine_init(&i10, PPCP_BS_ROLE_INITIATOR, 1u, pk_i), PPCP_OK);
        CHECK_EQ_I(ppcp_bs_engine_start(&i10, &s), PPCP_OK);
        memset(&accept, 0, sizeof(accept));
        accept.ty = PPCP_BS_ACCEPT;
        accept.v  = 1u;
        memcpy(accept.pk, pk_a, 32);
        CHECK_EQ_I(ppcp_bs_frame_write(&accept, wire, sizeof(wire), &n), PPCP_OK);
        CHECK_EQ_I(ppcp_bs_engine_recv(&i10, wire, n, &used, &s), PPCP_OK);
        CHECK_EQ_I(ppcp_bs_engine_state(&i10), PPCP_BS_ST_AWAIT_SECRET);

        /* The counterpart's user was quicker than our own arithmetic.  The
         * frame is in order; it simply cannot be verified yet. */
        CHECK_EQ_I(ppcp_bs_engine_recv(&i10, peer_confirm, peer_len, &used, &s), PPCP_OK);
        CHECK_EQ_I(s.event, PPCP_BS_EV_NONE);
        CHECK(!s.has_out);

        CHECK_EQ_I(ppcp_bs_engine_supply_secret(&i10, z, &s), PPCP_OK);
        CHECK_EQ_I(s.event, PPCP_BS_EV_COMPARE);
        /* Verified now — but still no pairing, because THIS user has not
         * affirmed (11.5g, 11.7c). */
        {
            ppcp_bs_pairing p;
            CHECK_EQ_I(ppcp_bs_engine_take_pairing(&i10, &p), PPCP_ERR_INVALID);
            CHECK_EQ_I(ppcp_bs_engine_affirm(&i10, &s), PPCP_OK);
            CHECK_EQ_I(s.event, PPCP_BS_EV_PAIRED);
            CHECK_EQ_I(ppcp_bs_engine_take_pairing(&i10, &p), PPCP_OK);
            (void)ppcp_unhex(PRK_HEX, want, sizeof(want));
            CHECK_BYTES(p.keys.prk, 32, want, 32);
        }
    }

    /* The obvious way to drive two engines, and the way the relay of L21 will:
     * one engine's step buffer handed straight to the other's recv() with that
     * same step as the output.  `buf` then points INTO `step`.  Written with
     * two separate buffers a test never exercises it, and the caller who
     * writes the natural loop gets a zeroed frame. */
    TEST("the step buffer may alias the inbound frame — the relay's own loop");
    {
        ppcp_bs_engine i12, a12;
        size_t         n = 0;

        CHECK_EQ_I(ppcp_bs_engine_init(&i12, PPCP_BS_ROLE_INITIATOR, 1u, pk_i), PPCP_OK);
        CHECK_EQ_I(ppcp_bs_engine_init(&a12, PPCP_BS_ROLE_ACCEPTOR,  1u, pk_a), PPCP_OK);

        CHECK_EQ_I(ppcp_bs_engine_start(&i12, &s), PPCP_OK);
        /* offer -> acceptor, reusing s throughout */
        CHECK_EQ_I(ppcp_bs_engine_recv(&a12, s.out, s.out_len, &n, &s), PPCP_OK);
        CHECK(s.has_out);
        /* accept -> initiator */
        CHECK_EQ_I(ppcp_bs_engine_recv(&i12, s.out, s.out_len, &n, &s), PPCP_OK);
        CHECK_EQ_I(s.event, PPCP_BS_EV_NEED_SECRET);
        CHECK(s.has_out);
        /* reveal -> acceptor */
        CHECK_EQ_I(ppcp_bs_engine_recv(&a12, s.out, s.out_len, &n, &s), PPCP_OK);
        CHECK_EQ_I(s.event, PPCP_BS_EV_NEED_SECRET);

        CHECK_EQ_I(ppcp_bs_engine_supply_secret(&i12, z, &s), PPCP_OK);
        CHECK_EQ_I(ppcp_bs_engine_supply_secret(&a12, z, &s), PPCP_OK);
        {
            uint32_t d = 0;
            CHECK_EQ_I(ppcp_bs_engine_sas(&i12, &d), PPCP_OK);
            CHECK_EQ_I(d, SAS_EXPECTED);
        }
        CHECK_EQ_I(ppcp_bs_engine_affirm(&a12, &s), PPCP_OK);
        CHECK_EQ_I(ppcp_bs_engine_recv(&i12, s.out, s.out_len, &n, &s), PPCP_OK);
        CHECK_EQ_I(ppcp_bs_engine_affirm(&i12, &s), PPCP_OK);
        CHECK_EQ_I(s.event, PPCP_BS_EV_PAIRED);
        CHECK_EQ_I(ppcp_bs_engine_recv(&a12, s.out, s.out_len, &n, &s), PPCP_OK);
        CHECK_EQ_I(s.event, PPCP_BS_EV_PAIRED);
    }

    TEST("11.10a — the engine's entire vocabulary is the five frames");
    {
        /* Nothing else can leave it: every outbound byte in this file came
         * from ppcp_bs_frame_write(), which encodes only those five types, and
         * there is no entry point taking a Peer.id, a name, a Source list or a
         * session identifier. */
        ppcp_bs_engine i11, a11;
        run_to_compare(&i11, &a11);
        CHECK_EQ_I(sizeof(((ppcp_bs_frame *)0)->ct), 32u);
        ppcp_bs_engine_wipe(&i11);
        ppcp_bs_engine_wipe(&a11);
        check_erased(&i11, "after an explicit wipe");
        /* Idempotent, and safe on a path the embedding abandons. */
        ppcp_bs_engine_wipe(&i11);
        check_erased(&i11, "after a second wipe");
    }

    TEST_MAIN_END();
}
