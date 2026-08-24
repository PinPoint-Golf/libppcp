/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * RT-18, RT-20a(a), RT-24b and the library half of RT-27 — every row of
 * PPCP-RV §10.4, byte for byte.
 *
 * ⛔ ERRATUM LEVEL, WHICH RT-18 REQUIRES A REPRODUCTION TO RECORD.  These
 * values are taken against `PPCP-RV` revision 9 **as amended by errata
 * E30–E55**, and specifically:
 *
 *   E34 moved FOUR rows — `sas_raw`, the SAS, `K_c` and both MACs — when
 *        11.6c began binding `v || pk_i || pk_a` into both expansions.  A
 *        recomputation yielding `11e66a4c` / SAS `313164` is not wrong about
 *        arithmetic; it is reading revision 9 as first published.
 *   E44 added the two derivation counter-vectors below (RT-24b).
 *   E50 split RT-20 into a/b/c.
 *   E54 added the interposer quadruple, published WITH both shared secrets so
 *        that RT-20a(a) needs no key agreement (11.11c).
 *
 * ⚠ WHAT THIS FILE DOES NOT DEMONSTRATE.  Every row here is arithmetic between
 * parties who are both behaving.  None of it touches the property §11 exists
 * to deliver: RT-20a(a) shows the derivation separates an interposer's two
 * legs FOR THESE KEYS, and what makes that meaningful is 11.5c's ordering,
 * which no amount of arithmetic can observe.  That is RT-20b and RT-20c, both
 * need the relay, and 9g forbids an RV-6 aggregate pass while RT-20c is unrun.
 */
#include "ppcp/rv.h"
#include "test_util.h"

/* ------------------------------------------------------------ §10.4 proper */

/* The two private keys are fixed so the vector reproduces.  They are NOT how a
 * key is chosen — 11.5a requires a fresh CSPRNG draw per attempt, and a peer
 * shipping either would be trivially impersonable by anyone reading the
 * document.  They appear here only as the provenance of the public keys.
 *
 *   sk_i 202122…3f      sk_a 606162…7f
 *
 * The library never sees them: 11.11a's two values are `pk` and `Z`. */
static const char PK_I_HEX[] = "358072d6365880d1aeea329adf9121383851ed21a28e3b75e965d0d2cd166254";
static const char PK_A_HEX[] = "675dd574ed7789310b3d2e7681f3790b466c773b1521fecf36577958371ea52f";
static const char CT_HEX[]   = "f32cd8e62f80f76adb4ba21971efbd10eb71aa6715d9e458f5422c1644357a3a";
static const char Z_HEX[]    = "7c79d7b5f31b9aac367477f5f7c7a68b5c44cac28ed5c902a59ec48c02956a6a";

static const char BK_HEX[]      = "b9f16f38e5a45ec6c0563b4fd3b38b696dfbbf4e3491fe1b7941a62637099349";
static const char SAS_RAW_HEX[] = "c012786c";                 /* 3222435948 big-endian */
#define SAS_EXPECTED 435948u                                  /* 3222435948 mod 10^6 */

static const char K_C_HEX[]   = "887bd19b77e6dd491886afb8cb8df9eeeadb3ead11a05cdf6e9d50b8cc00c90d";
static const char MAC_I_HEX[] = "b056a374ac4decba04f58bfd746746cd";
static const char MAC_A_HEX[] = "e0d3c748f738cf1cf54b08f7a819ff4d";

static const char SID_EXPAND_HEX[] = "1cc4b886e8bd65e063b207ae783bc56b";  /* before the bits */
static const char SID_HEX[]        = "1cc4b886e8bd45e0a3b207ae783bc56b";  /* after them */
static const char SESSION_ID[]     = "1cc4b886-e8bd-45e0-a3b2-07ae783bc56b";

static const char PRK_HEX[]   = "3e351aef1e5fe48411e969526b079830494d2cf13104d661694e897598ccf8c9";
static const char K_TLS_HEX[] = "240b513437501f3ab8602b06b45cd84577f10f126bdc497d3cf797c9559856b0";
static const char K_ID_HEX[]  = "9e8c8b155b89fcc9b70f4043ddaa607a7ff7acec20dc326f5c307661956a0bd9";

/* ------------------------------------------------- §10.4 counter-vectors (E44) */

/* What a WRONG implementation produces, so the reproduction is self-checking
 * rather than merely matching.  Both of these give CORRECT digits and CORRECT
 * MACs — the operator affirms a comparison that genuinely succeeded — and
 * surface one connection later as PSK_IDENTITY_NOT_FOUND, which looks exactly
 * like RV 3.5d's platform limitation and will be diagnosed as one. */
static const char CAUSE1_PRK_HEX[] = "9b77924572627d0e6d1c51fc679a3596ccd1c4a7dff7943da2ef856ef64dc1ba";
static const char CAUSE6_SID_HEX[] = "18dd04b1da8342a6b4248fb1bd2d0626";

/* The R-11 witness (11.6c2).  `pk_a'` yields a bit-identical, non-zero `Z`
 * from a DIFFERENT public key, so BK, sid and PRK are identical across the
 * substitution and only `sas_raw`'s explicit pk_i||pk_a separates the peers. */
static const char PK_A_PRIME_HEX[] = "87abc1e84c4c5572d2b1e63c69f5617a215518cf6261eb5a0e7db49ddad34208";
#define SAS_UNDER_SUBSTITUTION 485158u

/* ------------------------------------------- the interposer quadruple (E54) */

static const char PK_M1_HEX[] = "605a725d2a4adfeeb1a29e17edd621c1b7593ee8cdbc44ac6c4ab6e2f805d23c";
static const char PK_M2_HEX[] = "dc2cca31e8e43bbd91dff7e475cca3347eb478107d5bd765aba4ae4a30c35d44";
static const char Z1_HEX[]    = "0be5bbbe4a543727f7868cfa105122ca94bc39c1521a3b48dd149d9b726bc312";
static const char Z2_HEX[]    = "caec775fe4759a50d53cfaaac27866b18d8acce940c34a8c844ab9b062aba02e";
static const char SAS1_RAW_HEX[] = "aa480ea7";
static const char SAS2_RAW_HEX[] = "e668025b";
#define SAS_LEG1 849063u
#define SAS_LEG2 576027u

#define HEX32(name, lit) uint8_t name[32]; (void)ppcp_unhex(lit, name, sizeof(name))

int main(void)
{
    uint8_t pk_i[32], pk_a[32], z[32], want[32], ct[32];
    ppcp_rv_bootstrap bs;
    ppcp_result rc;

    (void)ppcp_unhex(PK_I_HEX, pk_i, sizeof(pk_i));
    (void)ppcp_unhex(PK_A_HEX, pk_a, sizeof(pk_a));
    (void)ppcp_unhex(Z_HEX,    z,    sizeof(z));

    /* ------------------------------------------------------------- RT-18 */

    TEST("RT-18 / 11.5b — the commitment, and no key agreement to compute it");
    ppcp_rv_bs_commit(pk_i, ct);
    (void)ppcp_unhex(CT_HEX, want, sizeof(want));
    CHECK_BYTES(ct, sizeof(ct), want, 32);

    TEST("RT-18 — the whole chain of 11.6c..11.6e from the published Z");
    rc = ppcp_rv_bootstrap_derive(z, 1u, pk_i, pk_a, &bs);
    CHECK_EQ_I(rc, PPCP_OK);

    (void)ppcp_unhex(BK_HEX, want, sizeof(want));
    CHECK_BYTES(bs.bk, sizeof(bs.bk), want, 32);

    (void)ppcp_unhex(SAS_RAW_HEX, want, sizeof(want));
    CHECK_BYTES(bs.sas_raw, sizeof(bs.sas_raw), want, 4);

    /* 11.7a — BIG-ENDIAN, modulo 10^6, six digits with leading zeros.  Read
     * little-endian this row gives 808448, which is six perfectly plausible
     * digits that nothing but this vector distinguishes from the answer. */
    TEST("RT-18 / 11.7a — the six displayed digits");
    CHECK_EQ_I(bs.sas, SAS_EXPECTED);
    {
        char digits[8];
        int  n = snprintf(digits, sizeof(digits), "%06u", bs.sas);
        CHECK_EQ_I(n, 6);
        CHECK(strcmp(digits, "435948") == 0);
    }

    (void)ppcp_unhex(K_C_HEX, want, sizeof(want));
    CHECK_BYTES(bs.k_c, sizeof(bs.k_c), want, 32);

    TEST("RT-18 / 11.5f — both confirmation MACs, two labels, one per direction");
    (void)ppcp_unhex(MAC_I_HEX, want, sizeof(want));
    CHECK_BYTES(bs.mac_i, sizeof(bs.mac_i), want, 16);
    (void)ppcp_unhex(MAC_A_HEX, want, sizeof(want));
    CHECK_BYTES(bs.mac_a, sizeof(bs.mac_a), want, 16);
    /* The labels differ so neither can be reflected back at its own sender. */
    CHECK(!ppcp_rv_ct_equal(bs.mac_i, bs.mac_a, PPCP_RV_BS_MAC_BYTES));

    TEST("RT-18 / 11.6d — sid, with the version and variant bits set");
    (void)ppcp_unhex(SID_HEX, want, sizeof(want));
    CHECK_BYTES(bs.sid, sizeof(bs.sid), want, 16);
    CHECK_EQ_I(bs.sid[6] & 0xf0u, 0x40u);          /* 4.3e — a version 4 UUID */
    CHECK_EQ_I(bs.sid[8] & 0xc0u, 0x80u);
    {
        char text[PPCP_RV_SESSION_ID_CHARS];
        CHECK_EQ_I(ppcp_rv_sid_to_session_id(bs.sid, text), PPCP_OK);
        CHECK(strcmp(text, SESSION_ID) == 0);
    }

    /* ⛔ THE ROW THAT MATTERS.  Two implementations agreeing on all six digits
     * and disagreeing here show the operator a successful comparison and then
     * fail the TLS handshake with no diagnostic. */
    TEST("RT-18 / 11.6e — PRK, K_tls and K_id, where §11 rejoins §5.1");
    (void)ppcp_unhex(PRK_HEX, want, sizeof(want));
    CHECK_BYTES(bs.prk, sizeof(bs.prk), want, 32);
    (void)ppcp_unhex(K_TLS_HEX, want, sizeof(want));
    CHECK_BYTES(bs.k_tls, sizeof(bs.k_tls), want, 32);
    (void)ppcp_unhex(K_ID_HEX, want, sizeof(want));
    CHECK_BYTES(bs.k_id, sizeof(bs.k_id), want, 32);

    /* ---------------------------------------- RT-24a, asserted where it can be
     *
     * RT-24a is a REVIEW row and E44 explains why: as a static row it asserts
     * nothing RT-18 does not, because a bound transcript changes the PRK and
     * RT-18's PRK row already fails.  What CAN be asserted mechanically is the
     * OTHER side of 11.6c1 — that `PRK` is a function of `Z` and `sid` alone,
     * which is exactly what §5.1 computes from the same two inputs.  If this
     * file ever bound a transcript into 11.6e, these two would diverge. */
    TEST("RT-24a (mechanical half) / 11.6c1 — PRK is §5.1's, over Z and sid, unamended");
    {
        ppcp_rv_keys k51;
        CHECK_EQ_I(ppcp_rv_derive(bs.sid, PPCP_RV_SID_BYTES, z, 32, &k51), PPCP_OK);
        CHECK_BYTES(k51.prk,   sizeof(k51.prk),   bs.prk,   32);
        CHECK_BYTES(k51.k_tls, sizeof(k51.k_tls), bs.k_tls, 32);
        CHECK_BYTES(k51.k_id,  sizeof(k51.k_id),  bs.k_id,  32);
    }

    /* --------------------------------------------------- trap 6 / 11.6f, E51 */

    TEST("11.6f as amended by E51 — wipe clears BOTH halves, not only the ephemeral one");
    ppcp_rv_bootstrap_wipe(&bs);
    {
        const unsigned char *p = (const unsigned char *)&bs;
        size_t i, nonzero = 0;
        for (i = 0; i < sizeof(bs); i++)
            if (p[i] != 0u)
                nonzero++;
        /* prk, k_tls, k_id and sid are gone too.  A peer holds all of them
         * from the moment it has Z — up to the 60 seconds 11.3e allows —
         * before the pairing exists at all (11.5g). */
        CHECK_EQ_I(nonzero, 0);
    }

    /* -------------------------------------------------------------- RT-24b */

    TEST("RT-24b / cause 1 — sid salted BEFORE its version and variant bits are set");
    {
        uint8_t expand[16], wrong_prk[32];
        ppcp_rv_keys k;

        CHECK_EQ_I(ppcp_rv_bootstrap_derive(z, 1u, pk_i, pk_a, &bs), PPCP_OK);
        (void)ppcp_unhex(SID_EXPAND_HEX, expand, sizeof(expand));

        /* The wrong thing, done deliberately: extract with the RAW expansion. */
        CHECK_EQ_I(ppcp_rv_derive(expand, sizeof(expand), z, 32, &k), PPCP_OK);
        (void)ppcp_unhex(CAUSE1_PRK_HEX, wrong_prk, sizeof(wrong_prk));
        CHECK_BYTES(k.prk, sizeof(k.prk), wrong_prk, 32);

        /* ⛔ And this is what makes it the silent one: the digits and both
         * MACs are CORRECT.  Only the PRK differs, one connection later. */
        CHECK_EQ_I(bs.sas, SAS_EXPECTED);
        CHECK(!ppcp_rv_ct_equal(k.prk, bs.prk, 32));
    }

    TEST("RT-24b / cause 6 — the transcript bound into sid as well, against 11.6c1");
    {
        uint8_t transcript[1 + 32 + 32];
        uint8_t info[19 + sizeof(transcript)];
        uint8_t wrong_sid[16], want_sid[16];
        static const char INFO_SID[] = "ppcp1 bootstrap-sid";

        transcript[0] = 1u;
        memcpy(transcript + 1, pk_i, 32);
        memcpy(transcript + 33, pk_a, 32);
        memcpy(info, INFO_SID, sizeof(INFO_SID) - 1u);
        memcpy(info + sizeof(INFO_SID) - 1u, transcript, sizeof(transcript));

        CHECK_EQ_I(ppcp_hkdf_expand(bs.bk, info, sizeof(info), wrong_sid, 16), PPCP_OK);
        wrong_sid[6] = (uint8_t)((wrong_sid[6] & 0x0fu) | 0x40u);
        wrong_sid[8] = (uint8_t)((wrong_sid[8] & 0x3fu) | 0x80u);

        (void)ppcp_unhex(CAUSE6_SID_HEX, want_sid, sizeof(want_sid));
        CHECK_BYTES(wrong_sid, sizeof(wrong_sid), want_sid, 16);

        /* "differs from the true sid in its FIRST octet, so one printed line
         * settles it" — and the digits and MACs are correct here too. */
        CHECK(wrong_sid[0] != bs.sid[0]);
        CHECK_EQ_I(bs.sas, SAS_EXPECTED);
    }

    /* ------------------------------------- RT-24c, the half that needs no curve
     *
     * The witness itself — that X25519(sk_i, pk_a') equals Z bit for bit — needs
     * an actual key agreement, which per 11.11 this component may not be able
     * to make; E50 split it out for that reason and it belongs on whichever
     * side holds the curve.  What runs HERE is the consequence: fed the SAME
     * Z with the substituted public key, the digits differ.  That is 11.6c2's
     * whole point — BK, sid and PRK are identical under the substitution, and
     * sas_raw's explicit pk_i||pk_a is the only thing separating the peers. */
    TEST("RT-24c (derivation half) / 11.6c2 — same Z, substituted pk_a, different digits");
    {
        uint8_t pk_a_prime[32];
        ppcp_rv_bootstrap sub;

        (void)ppcp_unhex(PK_A_PRIME_HEX, pk_a_prime, sizeof(pk_a_prime));
        CHECK_EQ_I(ppcp_rv_bootstrap_derive(z, 1u, pk_i, pk_a_prime, &sub), PPCP_OK);

        CHECK_EQ_I(sub.sas, SAS_UNDER_SUBSTITUTION);
        CHECK(sub.sas != bs.sas);
        /* Identical below the transcript, exactly as 11.6c2 says. */
        CHECK_BYTES(sub.bk,  sizeof(sub.bk),  bs.bk,  32);
        CHECK_BYTES(sub.sid, sizeof(sub.sid), bs.sid, 16);
        CHECK_BYTES(sub.prk, sizeof(sub.prk), bs.prk, 32);
        ppcp_rv_bootstrap_wipe(&sub);
    }

    ppcp_rv_bootstrap_wipe(&bs);

    /* ------------------------------------------------------------ RT-20a(a)
     *
     * The required, deterministic part.  §10.4 publishes BOTH legs' shared
     * secrets, so this row needs no key agreement, no socket, no peer and no
     * counterpart — 11.11c's promise reaching the one test that touches the
     * security property.
     *
     * ⚠ It shows the DERIVATION separates two legs for THESE keys.  What makes
     * that meaningful is that the interposer had to choose both of its keys
     * BLIND, and that is 11.5c's ordering, which is RT-20b's and needs the
     * relay. */
    TEST("RT-20a(a) — the interposer quadruple: two legs, two different numbers");
    {
        uint8_t pk_m1[32], pk_m2[32], z1[32], z2[32], raw[4];
        ppcp_rv_bootstrap leg1, leg2;

        (void)ppcp_unhex(PK_M1_HEX, pk_m1, sizeof(pk_m1));
        (void)ppcp_unhex(PK_M2_HEX, pk_m2, sizeof(pk_m2));
        (void)ppcp_unhex(Z1_HEX,    z1,    sizeof(z1));
        (void)ppcp_unhex(Z2_HEX,    z2,    sizeof(z2));

        /* leg 1 — the initiator believes it is talking to the interposer, so
         * the transcript is pk_i || pk_M1. */
        CHECK_EQ_I(ppcp_rv_bootstrap_derive(z1, 1u, pk_i, pk_m1, &leg1), PPCP_OK);
        (void)ppcp_unhex(SAS1_RAW_HEX, raw, sizeof(raw));
        CHECK_BYTES(leg1.sas_raw, sizeof(leg1.sas_raw), raw, 4);
        CHECK_EQ_I(leg1.sas, SAS_LEG1);

        /* leg 2 — the acceptor believes the interposer is the initiator, so
         * the transcript is pk_M2 || pk_a.  INITIATOR FIRST, still. */
        CHECK_EQ_I(ppcp_rv_bootstrap_derive(z2, 1u, pk_m2, pk_a, &leg2), PPCP_OK);
        (void)ppcp_unhex(SAS2_RAW_HEX, raw, sizeof(raw));
        CHECK_BYTES(leg2.sas_raw, sizeof(leg2.sas_raw), raw, 4);
        CHECK_EQ_I(leg2.sas, SAS_LEG2);

        /* 849063 != 576027 — the operator sees two different numbers. */
        CHECK(leg1.sas != leg2.sas);

        ppcp_rv_bootstrap_wipe(&leg1);
        ppcp_rv_bootstrap_wipe(&leg2);
    }

    /* -------------------------------------------- RT-27, the library's half
     *
     * "A reported agreement failure and an all-zero Z BOTH produce invalid_key
     * and neither is reported as a transport error" (11.11f, 11.6b).  The
     * library can only see the zero; the caller can only see the failure.
     * And the two failures are DISTINGUISHABLE from each other, which is R-18:
     * a `v` out of range is a caller's bug, not an attack. */
    TEST("RT-27 (library half) / 11.6b — an all-zero Z is invalid_key and derives nothing");
    {
        uint8_t zeros[32];
        memset(zeros, 0, sizeof(zeros));
        memset(&bs, 0xaa, sizeof(bs));
        CHECK_EQ_I(ppcp_rv_bootstrap_derive(zeros, 1u, pk_i, pk_a, &bs),
                   PPCP_ERR_RV_INVALID_KEY);
        {
            const unsigned char *p = (const unsigned char *)&bs;
            size_t i, nonzero = 0;
            for (i = 0; i < sizeof(bs); i++)
                if (p[i] != 0u)
                    nonzero++;
            CHECK_EQ_I(nonzero, 0);        /* "derives nothing" */
        }
    }

    TEST("R-18 / 11.4h1 — a `v` outside 1..255 is MALFORMED, not an attack signal");
    {
        memset(&bs, 0xaa, sizeof(bs));
        CHECK_EQ_I(ppcp_rv_bootstrap_derive(z, 0u, pk_i, pk_a, &bs), PPCP_ERR_MALFORMED);
        /* ⛔ Not PPCP_ERR_RV_INVALID_KEY.  One code for both would report a
         * caller's programming error to an operator as an attack. */
        CHECK(ppcp_rv_bootstrap_derive(z, 0u, pk_i, pk_a, &bs) != PPCP_ERR_RV_INVALID_KEY);
    }

    TEST("11.4i — a different `v` is a different derivation, digits and MACs both");
    {
        ppcp_rv_bootstrap v1, v2;
        CHECK_EQ_I(ppcp_rv_bootstrap_derive(z, 1u, pk_i, pk_a, &v1), PPCP_OK);
        CHECK_EQ_I(ppcp_rv_bootstrap_derive(z, 2u, pk_i, pk_a, &v2), PPCP_OK);
        /* The rewrite surviving 11.4h's echo check changes the digits, so the
         * operator sees it before anyone confirms, AND fails the MACs, so the
         * peers see it if the operator does not. */
        CHECK(v1.sas != v2.sas);
        CHECK(!ppcp_rv_ct_equal(v1.mac_i, v2.mac_i, PPCP_RV_BS_MAC_BYTES));
        CHECK(!ppcp_rv_ct_equal(v1.mac_a, v2.mac_a, PPCP_RV_BS_MAC_BYTES));
        /* And 11.6c1's boundary holds across it: sid and PRK are functions of
         * Z alone and do NOT move with the version. */
        CHECK_BYTES(v2.sid, sizeof(v2.sid), v1.sid, 16);
        CHECK_BYTES(v2.prk, sizeof(v2.prk), v1.prk, 32);
        ppcp_rv_bootstrap_wipe(&v1);
        ppcp_rv_bootstrap_wipe(&v2);
    }

    TEST("11.6c — the transcript is initiator-first, and transposing it shows");
    {
        ppcp_rv_bootstrap fwd, rev;
        CHECK_EQ_I(ppcp_rv_bootstrap_derive(z, 1u, pk_i, pk_a, &fwd), PPCP_OK);
        CHECK_EQ_I(ppcp_rv_bootstrap_derive(z, 1u, pk_a, pk_i, &rev), PPCP_OK);
        CHECK(fwd.sas != rev.sas);   /* §10.4's cause 3 */
        ppcp_rv_bootstrap_wipe(&fwd);
        ppcp_rv_bootstrap_wipe(&rev);
    }

    TEST("11.5d — the commitment check is available in constant time");
    {
        uint8_t other[32];
        ppcp_rv_bs_commit(pk_i, ct);
        ppcp_rv_bs_commit(pk_a, other);
        CHECK(ppcp_rv_ct_equal(ct, other, 32) == false);
        CHECK(ppcp_rv_ct_equal(ct, ct, 32) == true);
    }

    printf("RT-18 / RT-20a(a) / RT-24b reproduced against PPCP-RV revision 9 "
           "as amended by errata E30-E55 (E34 for sas_raw/SAS/K_c/mac_i/mac_a, "
           "E44 for the counter-vectors, E54 for the interposer quadruple)\n");

    TEST_MAIN_END();
}
