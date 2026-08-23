/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * RT-1, RT-2, RT-3, RT-6 and RT-14 (static half) — every vector of PPCP-RV §10,
 * byte for byte.
 *
 * RV 9c: "It reproduces the test vectors of §10 exactly."
 */
#include "ppcp/rv.h"
#include "test_util.h"

/* RV §10.1 */
static const char SID_HEX[]   = "3f2504e04f8941d39a0c0305e82c3301";
static const char PSK_HEX[]   = "000102030405060708090a0b0c0d0e0f";
static const char PRK_HEX[]   = "d8a961d30def2e84bd930aa64fe8c9583286281ae0f61baa0116a8220bf6bcf9";
static const char K_TLS_HEX[] = "2b0c55242ac1075eef80f548a7b39976b1cc2b88fbb6d609e5f3cd20f36d7fd4";
static const char K_ID_HEX[]  = "fd2d8fcfb1be76f83ca1d551e8d5ab34a2fbe3a76f048acb09c64c1d20646117";
static const char SESSION_ID[] = "3f2504e0-4f89-41d3-9a0c-0305e82c3301";

/* RV §10.2 */
static const char RN_HEX[]       = "a1b2c3d4e5f60718";
static const char RID_HEX[]      = "9b1d2df94b2cfa84";
static const char RN2_HEX[]      = "0f1e2d3c4b5a6978";
static const char TAG_HEX[]      = "b355ada60b4b5aa8";
static const char IDENTITY_HEX[] = "010f1e2d3c4b5a6978b355ada60b4b5aa8";
static const char INSTANCE_NAME[] = "PPCP-9B1D2DF9";

/* RV §10.3 — the minimal code, 75 octets. */
static const char CODE_MIN_HEX[] =
    "a5"
    "61 76 01"
    "62 65 70 81 a2"
    "61 68 6c 31 39 32 2e 31 36 38 2e 31 2e 32 30"
    "61 70 19 1e 6c"
    "62 6d 75 01"
    "63 70 73 6b 50 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f"
    "63 73 69 64 50 3f 25 04 e0 4f 89 41 d3 9a 0c 03 05 e8 2c 33 01";

static const char CODE_MIN_URI[] =
    "ppcp:pWF2AWJlcIGiYWhsMTkyLjE2OC4xLjIwYXAZHmxibXUBY3Bza1AAAQIDBAUGBwgJCgsMDQ4PY3NpZFA_JQTgT4lB05oMAwXoLDMB";

/* RV §10.3 — every optional field, 133 octets. */
static const char CODE_ALL_HEX[] =
    "a8"
    "61 76 01"
    "62 64 6e 65 42 61 79 20 33"
    "62 65 70 81 a2"
    "61 68 6c 31 39 32 2e 31 36 38 2e 31 2e 32 30"
    "61 70 19 1e 6c"
    "62 6d 75 01"
    "63 65 78 70 1a 6a 90 26 c0"
    "63 70 73 6b 50 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f"
    "63 73 69 64 50 3f 25 04 e0 4f 89 41 d3 9a 0c 03 05 e8 2c 33 01"
    "64 77 69 66 69 a3"
    "61 68 f4"
    "61 6b 6c 63 6f 72 72 65 63 74 68 6f 72 73 65"
    "61 73 6d 50 69 6e 50 6f 69 6e 74 2d 42 61 79 33";

static const char CODE_ALL_URI[] =
    "ppcp:qGF2AWJkbmVCYXkgM2JlcIGiYWhsMTkyLjE2OC4xLjIwYXAZHmxibXUBY2V4cBpqkCbAY3Bza1AAAQIDBAUGBwgJCgsMDQ4PY3NpZFA_JQTgT4lB05oMAwXoLDMBZHdpZmmjYWj0YWtsY29ycmVjdGhvcnNlYXNtUGluUG9pbnQtQmF5Mw";

/* ---------------------------------------------------------------- RT-1 */

static void rt1_derivation_vectors(void)
{
    unsigned char sid[16], psk[16], want[32];
    ppcp_rv_keys  k;

    TEST("RT-1 — RV 10.1 derivation vectors reproduce byte for byte");
    CHECK_EQ_I(ppcp_unhex(SID_HEX, sid, sizeof(sid)), 16);
    CHECK_EQ_I(ppcp_unhex(PSK_HEX, psk, sizeof(psk)), 16);
    CHECK_EQ_I(ppcp_rv_derive(sid, sizeof(sid), psk, sizeof(psk), &k), PPCP_OK);

    ppcp_unhex(PRK_HEX, want, sizeof(want));
    CHECK_BYTES(k.prk, 32, want, 32);
    ppcp_unhex(K_TLS_HEX, want, sizeof(want));
    CHECK_BYTES(k.k_tls, 32, want, 32);
    ppcp_unhex(K_ID_HEX, want, sizeof(want));
    CHECK_BYTES(k.k_id, 32, want, 32);

    TEST("RT-1 — 5.1c: deriving from a persisted PRK gives the same two keys");
    {
        ppcp_rv_keys k2;
        CHECK_EQ_I(ppcp_rv_derive_from_prk(k.prk, &k2), PPCP_OK);
        CHECK_BYTES(k2.k_tls, 32, k.k_tls, 32);
        CHECK_BYTES(k2.k_id, 32, k.k_id, 32);
    }

    TEST("RT-1 — a psk of the wrong width is refused (4.3: 16 or 32 bytes)");
    {
        ppcp_rv_keys k3;
        CHECK_EQ_I(ppcp_rv_derive(sid, 16, psk, 8, &k3), PPCP_ERR_INVALID);
        CHECK_EQ_I(ppcp_rv_derive(sid, 15, psk, 16, &k3), PPCP_ERR_INVALID);
    }
}

static void sha256_known_answers(void)
{
    /* The vectors of §10 exercise HMAC and HKDF but never a bare digest, and
     * ENC 6c will need one for every chunk (L7).  These are the FIPS 180-4
     * examples. */
    unsigned char out[32], want[32];

    TEST("SHA-256 known answers");
    ppcp_sha256_hash("abc", 3, out);
    ppcp_unhex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
               want, sizeof(want));
    CHECK_BYTES(out, 32, want, 32);

    ppcp_sha256_hash("", 0, out);
    ppcp_unhex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
               want, sizeof(want));
    CHECK_BYTES(out, 32, want, 32);

    ppcp_sha256_hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, out);
    ppcp_unhex("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
               want, sizeof(want));
    CHECK_BYTES(out, 32, want, 32);

    /* RFC 4231 test case 1. */
    TEST("HMAC-SHA256 known answer (RFC 4231 case 1)");
    {
        unsigned char key[20];
        memset(key, 0x0b, sizeof(key));
        ppcp_hmac_sha256(key, sizeof(key), (const unsigned char *)"Hi There", 8, out);
        ppcp_unhex("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
                   want, sizeof(want));
        CHECK_BYTES(out, 32, want, 32);
    }
}

/* --------------------------------------------------------------- RT-14 */

static void rt14_identities(void)
{
    unsigned char k_id[32], rn[8], rn2[8], want[32];
    unsigned char rid[8], identity[17];
    char          name[PPCP_RV_INSTANCE_NAME_MAX];

    ppcp_unhex(K_ID_HEX, k_id, sizeof(k_id));
    ppcp_unhex(RN_HEX, rn, sizeof(rn));
    ppcp_unhex(RN2_HEX, rn2, sizeof(rn2));

    TEST("RT-8 / RV 10.2 — rid reproduces byte for byte");
    CHECK_EQ_I(ppcp_rv_rid(k_id, rn, rid), PPCP_OK);
    ppcp_unhex(RID_HEX, want, sizeof(want));
    CHECK_BYTES(rid, 8, want, 8);

    TEST("RV 3.2a — the instance name carries the first four bytes of rid");
    CHECK_EQ_I(ppcp_rv_instance_name(rid, name), PPCP_OK);
    CHECK(strcmp(name, INSTANCE_NAME) == 0);

    TEST("RT-14 — the PSK identity of RV 10.2 reproduces byte for byte");
    CHECK_EQ_I(ppcp_rv_psk_identity(k_id, rn2, identity), PPCP_OK);
    ppcp_unhex(IDENTITY_HEX, want, sizeof(want));
    CHECK_BYTES(identity, 17, want, 17);
    CHECK_EQ_I(identity[0], 0x01);

    TEST("RT-14 — the tag half matches the vector on its own");
    {
        unsigned char parsed_rn2[8], parsed_tag[8];
        CHECK_EQ_I(ppcp_rv_psk_identity_parse(identity, 17, parsed_rn2, parsed_tag),
                   PPCP_OK);
        CHECK_BYTES(parsed_rn2, 8, rn2, 8);
        ppcp_unhex(TAG_HEX, want, sizeof(want));
        CHECK_BYTES(parsed_tag, 8, want, 8);
    }

    TEST("RT-14 — the identity differs across connections (5.3a: fresh rn2)");
    {
        unsigned char rn2b[8];
        unsigned char identity_b[17];
        memcpy(rn2b, rn2, 8);
        rn2b[7] ^= 0x01u;
        CHECK_EQ_I(ppcp_rv_psk_identity(k_id, rn2b, identity_b), PPCP_OK);
        CHECK(memcmp(identity, identity_b, 17) != 0);
        /* And the difference is not only the nonce: the tag moves too. */
        CHECK(memcmp(identity + 9, identity_b + 9, 8) != 0);
    }

    TEST("RT-14 — the identity contains no sid (5.3e)");
    {
        unsigned char sid[16];
        unsigned      i;
        ppcp_unhex(SID_HEX, sid, sizeof(sid));
        /* Nothing stable across connections may appear.  The strongest cheap
         * check is that no run of four sid bytes occurs anywhere in the 17. */
        for (i = 0; i + 4u <= 16u; i++) {
            unsigned j;
            for (j = 0; j + 4u <= 17u; j++)
                CHECK(memcmp(sid + i, identity + j, 4) != 0);
        }
    }

    TEST("RT-14 — a malformed identity is refused, not transcoded (5.3f)");
    {
        unsigned char rn2b[8], tagb[8];
        unsigned char short_id[16];
        memcpy(short_id, identity, 16);
        CHECK_EQ_I(ppcp_rv_psk_identity_parse(short_id, 16, rn2b, tagb),
                   PPCP_ERR_MALFORMED);
        {
            unsigned char wrong_version[17];
            memcpy(wrong_version, identity, 17);
            wrong_version[0] = 0x02;
            CHECK_EQ_I(ppcp_rv_psk_identity_parse(wrong_version, 17, rn2b, tagb),
                       PPCP_ERR_MALFORMED);
        }
    }
}

/* ------------------------------------------------------- the resolver */

static void rt8_resolver(void)
{
    unsigned char k_id[32], other[32], rn[8], rn2[8], rid[8], identity[17];
    ppcp_rv_pairing held[3];
    size_t          idx = 99;
    int             tag_a = 1, tag_b = 2, tag_c = 3;

    ppcp_unhex(K_ID_HEX, k_id, sizeof(k_id));
    memcpy(other, k_id, 32);
    other[0] ^= 0xffu;
    ppcp_unhex(RN_HEX, rn, sizeof(rn));
    ppcp_unhex(RN2_HEX, rn2, sizeof(rn2));
    CHECK_EQ_I(ppcp_rv_rid(k_id, rn, rid), PPCP_OK);
    CHECK_EQ_I(ppcp_rv_psk_identity(k_id, rn2, identity), PPCP_OK);

    held[0].k_id = other;  held[0].user = &tag_a;
    held[1].k_id = k_id;   held[1].user = &tag_b;
    held[2].k_id = other;  held[2].user = &tag_c;

    TEST("RT-8 / RV 3.4b — a discovered rid resolves under the right K_id only");
    CHECK_EQ_I(ppcp_rv_resolve_rid(held, 3, rn, rid, &idx), PPCP_OK);
    CHECK_EQ_I(idx, 1);
    CHECK(held[idx].user == &tag_b);

    TEST("RV 3.4c — an unresolvable rid is PPCP_ERR_NOT_FOUND, so no connection");
    {
        ppcp_rv_pairing none[2];
        none[0].k_id = other; none[0].user = NULL;
        none[1].k_id = other; none[1].user = NULL;
        CHECK_EQ_I(ppcp_rv_resolve_rid(none, 2, rn, rid, &idx), PPCP_ERR_NOT_FOUND);
    }

    TEST("RV 5.3b — an offered PSK identity resolves the same way");
    CHECK_EQ_I(ppcp_rv_resolve_psk_identity(held, 3, identity, 17, &idx), PPCP_OK);
    CHECK_EQ_I(idx, 1);

    TEST("RV 5.3c — an unresolvable identity fails exactly as a wrong key does");
    {
        ppcp_rv_pairing none[1];
        unsigned char   forged[17];
        none[0].k_id = other; none[0].user = NULL;
        CHECK_EQ_I(ppcp_rv_resolve_psk_identity(none, 1, identity, 17, &idx),
                   PPCP_ERR_NOT_FOUND);
        /* A forged tag under a known nonce: the same result, the same code. */
        memcpy(forged, identity, 17);
        forged[16] ^= 0x01u;
        CHECK_EQ_I(ppcp_rv_resolve_psk_identity(held, 3, forged, 17, &idx),
                   PPCP_ERR_NOT_FOUND);
    }

    TEST("RT-8 — rid changes when rn rotates (3.4a)");
    {
        unsigned char rn_b[8], rid_b[8];
        memcpy(rn_b, rn, 8);
        rn_b[0] ^= 0x01u;
        CHECK_EQ_I(ppcp_rv_rid(k_id, rn_b, rid_b), PPCP_OK);
        CHECK(memcmp(rid, rid_b, 8) != 0);
    }
}

/* ---------------------------------------------------------------- RT-2 */

static void fill_minimal(ppcp_rv_payload *p, unsigned char *psk, unsigned char *sid)
{
    ppcp_rv_payload_init(p);
    CHECK_EQ_I(ppcp_rv_payload_add_endpoint(p, "192.168.1.20", 12, 7788), PPCP_OK);
    CHECK_EQ_I(ppcp_rv_payload_set_max_uses(p, 1), PPCP_OK);
    CHECK_EQ_I(ppcp_rv_payload_set_secret(p, psk, 16, sid), PPCP_OK);
}

static void rt2_minimal_code(void)
{
    unsigned char   want[256], got[256], psk[16], sid[16];
    char            uri[PPCP_RV_MAX_URI];
    size_t          want_len, got_len = 0, uri_len = 0;
    ppcp_rv_payload p;

    ppcp_unhex(PSK_HEX, psk, sizeof(psk));
    ppcp_unhex(SID_HEX, sid, sizeof(sid));
    want_len = ppcp_unhex(CODE_MIN_HEX, want, sizeof(want));

    TEST("RT-2 — RV 10.3 minimal code is 75 octets and encodes byte for byte");
    CHECK_EQ_I(want_len, 75);
    fill_minimal(&p, psk, sid);
    CHECK_EQ_I(ppcp_rv_payload_encode(&p, got, sizeof(got), &got_len), PPCP_OK);
    CHECK_BYTES(got, got_len, want, want_len);

    TEST("RT-2 — and the URI is 105 characters");
    CHECK_EQ_I(ppcp_rv_uri_encode(&p, uri, sizeof(uri), &uri_len), PPCP_OK);
    CHECK_EQ_I(uri_len, 105);
    CHECK_EQ_I(uri_len, strlen(CODE_MIN_URI));
    CHECK_BYTES(uri, uri_len, CODE_MIN_URI, strlen(CODE_MIN_URI));

    TEST("RT-2 — and it decodes back to the same fields");
    {
        ppcp_rv_payload back;
        CHECK_EQ_I(ppcp_rv_payload_decode(want, want_len, &back), PPCP_OK);
        CHECK_EQ_I(back.v, 1);
        CHECK_EQ_I(back.ep_count, 1);
        CHECK_EQ_I(back.ep[0].p, 7788);
        CHECK_EQ_I(back.ep[0].h_len, 12);
        CHECK(memcmp(back.ep[0].h, "192.168.1.20", 12) == 0);
        CHECK(back.has_mu);
        CHECK_EQ_I(back.mu, 1);
        CHECK_EQ_I(back.psk_len, 16);
        CHECK_BYTES(back.psk, 16, psk, 16);
        CHECK_BYTES(back.sid, 16, sid, 16);
        CHECK(!back.has_dn);
        CHECK(!back.has_exp);
        CHECK(!back.has_wifi);
    }

    TEST("RT-2 — the URI decodes too");
    {
        unsigned char   scratch[256];
        ppcp_rv_payload back;
        CHECK_EQ_I(ppcp_rv_uri_decode(CODE_MIN_URI, strlen(CODE_MIN_URI),
                                      scratch, sizeof(scratch), &back), PPCP_OK);
        CHECK_EQ_I(back.ep[0].p, 7788);
        CHECK_BYTES(back.sid, 16, sid, 16);
    }
}

static void rt2_all_fields_code(void)
{
    unsigned char   want[256], got[256], psk[16], sid[16];
    char            uri[PPCP_RV_MAX_URI];
    size_t          want_len, got_len = 0, uri_len = 0;
    ppcp_rv_payload p;

    ppcp_unhex(PSK_HEX, psk, sizeof(psk));
    ppcp_unhex(SID_HEX, sid, sizeof(sid));
    want_len = ppcp_unhex(CODE_ALL_HEX, want, sizeof(want));

    TEST("RT-2 — the 133-octet all-fields code encodes byte for byte");
    CHECK_EQ_I(want_len, 133);
    fill_minimal(&p, psk, sid);
    CHECK_EQ_I(ppcp_rv_payload_set_display_name(&p, "Bay 3", 5), PPCP_OK);
    CHECK_EQ_I(ppcp_rv_payload_set_expiry(&p, 1787832000ULL), PPCP_OK);
    {
        ppcp_rv_wifi wifi;
        memset(&wifi, 0, sizeof(wifi));
        wifi.s = "PinPoint-Bay3"; wifi.s_len = 13;
        wifi.has_k = true; wifi.k = "correcthorse"; wifi.k_len = 12;
        wifi.has_h = true; wifi.h = false;
        CHECK_EQ_I(ppcp_rv_payload_set_wifi(&p, &wifi), PPCP_OK);
    }

    TEST("RV 4.3 / 4.4d — a display name over 64 bytes is refused, not truncated");
    {
        char long_dn[80];
        ppcp_rv_payload q;
        memset(long_dn, 'x', sizeof(long_dn));
        fill_minimal(&q, psk, sid);
        CHECK_EQ_I(ppcp_rv_payload_set_display_name(&q, long_dn, 65), PPCP_ERR_INVALID);
        CHECK_EQ_I(ppcp_rv_payload_set_display_name(&q, long_dn, 64), PPCP_OK);
    }
    TEST("RT-2 — the 133-octet all-fields code encodes byte for byte");
    CHECK_EQ_I(ppcp_rv_payload_encode(&p, got, sizeof(got), &got_len), PPCP_OK);
    CHECK_BYTES(got, got_len, want, want_len);

    /* "The first four octets are a8 61 76 01 — map(8), "v", 1.  With `n` in
     * place of `dn` they would have been a8 61 6e …, and a parser reading the
     * first key to find the version would have read a display name." */
    TEST("RT-2 — `v` is still the first key with every optional field present (4.3b)");
    CHECK_EQ_I(got[0], 0xa8);
    CHECK_EQ_I(got[1], 0x61);
    CHECK_EQ_I(got[2], 0x76);
    CHECK_EQ_I(got[3], 0x01);

    TEST("RT-2 — and the URI is 183 characters");
    CHECK_EQ_I(ppcp_rv_uri_encode(&p, uri, sizeof(uri), &uri_len), PPCP_OK);
    CHECK_EQ_I(uri_len, 183);
    CHECK_BYTES(uri, uri_len, CODE_ALL_URI, strlen(CODE_ALL_URI));

    TEST("RT-2 — the all-fields code decodes to every field");
    {
        ppcp_rv_payload back;
        CHECK_EQ_I(ppcp_rv_payload_decode(want, want_len, &back), PPCP_OK);
        CHECK(back.has_dn);
        CHECK_EQ_I(back.dn_len, 5);
        CHECK(memcmp(back.dn, "Bay 3", 5) == 0);
        CHECK(back.has_exp);
        CHECK_EQ_I(back.exp, 1787832000ULL);
        CHECK(back.has_wifi);
        CHECK_EQ_I(back.wifi.s_len, 13);
        CHECK(memcmp(back.wifi.s, "PinPoint-Bay3", 13) == 0);
        CHECK(back.wifi.has_k);
        CHECK(memcmp(back.wifi.k, "correcthorse", 12) == 0);
        CHECK(back.wifi.has_h);
        CHECK(back.wifi.h == false);
    }
}

static void rt2_session_id(void)
{
    unsigned char sid[16], back[16];
    char          text[PPCP_RV_SESSION_ID_CHARS];

    /* 4.3e — the canonical lowercase text form, and no other.  Two
     * implementations choosing differently would duplicate every Capture in a
     * re-imported session (CORE 8.5c). */
    TEST("RT-2 — sid becomes Session.id as canonical lowercase UUID text");
    ppcp_unhex(SID_HEX, sid, sizeof(sid));
    CHECK_EQ_I(ppcp_rv_sid_to_session_id(sid, text), PPCP_OK);
    CHECK(strcmp(text, SESSION_ID) == 0);

    TEST("RT-2 — and back");
    CHECK_EQ_I(ppcp_rv_session_id_to_sid(SESSION_ID, 36, back), PPCP_OK);
    CHECK_BYTES(back, 16, sid, 16);

    TEST("RT-2 — a non-canonical form is refused");
    CHECK_EQ_I(ppcp_rv_session_id_to_sid("3f2504e04f8941d39a0c0305e82c3301", 32, back),
               PPCP_ERR_MALFORMED);
    CHECK_EQ_I(ppcp_rv_session_id_to_sid("3f2504e0-4f89-41d3-9a0c-0305e82c330", 35, back),
               PPCP_ERR_MALFORMED);
    /* Uppercase input parses — it is still a UUID — but the canonical output
     * is lowercase, which is what the wire carries. */
    CHECK_EQ_I(ppcp_rv_session_id_to_sid("3F2504E0-4F89-41D3-9A0C-0305E82C3301", 36, back),
               PPCP_OK);
    CHECK_BYTES(back, 16, sid, 16);
}

/* ---------------------------------------------------------------- RT-3 */

static void rt3_unknown_version(void)
{
    unsigned char   buf[256];
    ppcp_rv_payload p;
    size_t          n;

    /* A payload identical to the minimal code but with v = 2. */
    static const char V2_HEX[] =
        "a5"
        "61 76 02"
        "62 65 70 81 a2"
        "61 68 6c 31 39 32 2e 31 36 38 2e 31 2e 32 30"
        "61 70 19 1e 6c"
        "62 6d 75 01"
        "63 70 73 6b 50 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f"
        "63 73 69 64 50 3f 25 04 e0 4f 89 41 d3 9a 0c 03 05 e8 2c 33 01";

    TEST("RT-3 — an unknown v produces a VERSION report, not a generic failure");
    n = ppcp_unhex(V2_HEX, buf, sizeof(buf));
    CHECK_EQ_I(ppcp_rv_payload_decode(buf, n, &p), PPCP_ERR_VERSION_NEWER);
    /* 4.2b: the result code is distinct, so the application can say "this code
     * requires a newer version of the application" rather than "could not
     * pair" — which tells a user nothing they can act on. */
    CHECK(strcmp(ppcp_result_str(PPCP_ERR_VERSION_NEWER),
                 "a newer version of the application is required") == 0);
    /* 4.2d: nothing else was acted on. */
    CHECK_EQ_I(p.v, 2);
    CHECK_EQ_I(p.ep_count, 0);
    CHECK_EQ_I(p.psk_len, 0);

    TEST("RT-3 — a payload whose first key is not `v` is malformed (4.2a)");
    {
        static const char NO_V_FIRST[] =
            "a2"
            "62 6d 75 01"
            "63 70 73 6b 50 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f";
        n = ppcp_unhex(NO_V_FIRST, buf, sizeof(buf));
        CHECK_EQ_I(ppcp_rv_payload_decode(buf, n, &p), PPCP_ERR_MALFORMED);
    }

    TEST("RV 4.4b — an undecodable payload is an invalid code, and no connection");
    {
        static const char GARBAGE[] = "a1 01 02";   /* an integer map key */
        n = ppcp_unhex(GARBAGE, buf, sizeof(buf));
        CHECK_EQ_I(ppcp_rv_payload_decode(buf, n, &p), PPCP_ERR_MALFORMED);
    }

    TEST("RV 4.2c — an unknown key at any nesting level is ignored");
    {
        /* The minimal code with an extra top-level key `zz` and an extra key
         * inside the endpoint map. */
        static const char EXTRA_HEX[] =
            "a6"
            "61 76 01"
            "62 65 70 81 a3"
            "61 68 6c 31 39 32 2e 31 36 38 2e 31 2e 32 30"
            "61 70 19 1e 6c"
            "62 7a 7a 01"
            "62 6d 75 01"
            "63 70 73 6b 50 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f"
            "63 73 69 64 50 3f 25 04 e0 4f 89 41 d3 9a 0c 03 05 e8 2c 33 01"
            "62 7a 7a 63 61 62 63";
        n = ppcp_unhex(EXTRA_HEX, buf, sizeof(buf));
        CHECK_EQ_I(ppcp_rv_payload_decode(buf, n, &p), PPCP_OK);
        CHECK_EQ_I(p.ep_count, 1);
        CHECK_EQ_I(p.ep[0].p, 7788);
    }
}

/* ---------------------------------------------------------------- RT-6 */

static void rt6_expiry(void)
{
    unsigned char   psk[16], sid[16];
    ppcp_rv_payload p;
    ppcp_rv_expiry  out;

    ppcp_unhex(PSK_HEX, psk, sizeof(psk));
    ppcp_unhex(SID_HEX, sid, sizeof(sid));
    fill_minimal(&p, psk, sid);

    TEST("RV 7.3c — no `exp` is not an expiry decision");
    CHECK_EQ_I(ppcp_rv_check_expiry(&p, 1787832000ULL, PPCP_RV_CLOCK_TRUSTED, &out),
               PPCP_OK);
    CHECK_EQ_I(out, PPCP_RV_EXPIRY_OK);

    p.has_exp = true;
    p.exp     = 1787832000ULL;

    TEST("RT-6 / 4.4a — a trusted clock past `exp` reports EXPIRED, not a failure");
    CHECK_EQ_I(ppcp_rv_check_expiry(&p, 1787832001ULL, PPCP_RV_CLOCK_TRUSTED, &out),
               PPCP_OK);
    CHECK_EQ_I(out, PPCP_RV_EXPIRY_EXPIRED);
    CHECK_EQ_I(ppcp_rv_check_expiry(&p, 1787831999ULL, PPCP_RV_CLOCK_TRUSTED, &out),
               PPCP_OK);
    CHECK_EQ_I(out, PPCP_RV_EXPIRY_OK);

    /* 4.4a1 — a device with a wrong clock at a range has no network to correct
     * it, and the publisher enforces `exp` itself (7.3e).  Refusing would leave
     * the user with no path at all. */
    TEST("4.4a1 — an untrusted clock attempts anyway and reports POSSIBLY expired");
    CHECK_EQ_I(ppcp_rv_check_expiry(&p, 1787832001ULL, PPCP_RV_CLOCK_UNTRUSTED, &out),
               PPCP_OK);
    CHECK_EQ_I(out, PPCP_RV_EXPIRY_POSSIBLY_EXPIRED);

    TEST("RV 7.4f — a PRK from a mu > 1 code is never persisted");
    CHECK(ppcp_rv_may_persist(&p));
    p.mu = 3;
    CHECK(!ppcp_rv_may_persist(&p));
}

static void base64url_edges(void)
{
    unsigned char in[5] = { 0xff, 0xee, 0xdd, 0xcc, 0xbb };
    unsigned char back[8];
    char          out[16];
    size_t        n = 0, m = 0;
    unsigned      len;

    TEST("RV 4.1a — base64url, unpadded, round-trips at every length");
    for (len = 0; len <= 5u; len++) {
        CHECK_EQ_I(ppcp_base64url_encode(in, len, out, sizeof(out), &n), PPCP_OK);
        CHECK_EQ_I(ppcp_base64url_decode(out, n, back, sizeof(back), &m), PPCP_OK);
        CHECK_EQ_I(m, len);
        CHECK(memcmp(back, in, len) == 0);
    }

    TEST("RV 4.1a — padding is not accepted");
    CHECK_EQ_I(ppcp_base64url_decode("aGk=", 4, back, sizeof(back), &m),
               PPCP_ERR_MALFORMED);
    TEST("and neither is standard base64's alphabet");
    CHECK_EQ_I(ppcp_base64url_decode("a+/b", 4, back, sizeof(back), &m),
               PPCP_ERR_MALFORMED);
}

/* A deterministic stand-in for a CSPRNG, so the rejection loop of 5.3a1 is
 * exercised rather than merely reached.  The first draw is chosen to produce a
 * tag containing a zero byte; the counter walks on until one is clean. */
typedef struct fake_rng { unsigned counter; unsigned draws; } fake_rng;

static bool fake_random(void *ctx, uint8_t *out, size_t len)
{
    fake_rng *r = (fake_rng *)ctx;
    size_t    i;
    r->draws++;
    for (i = 0; i < len; i++)
        out[i] = (uint8_t)(r->counter * 37u + (unsigned)i * 11u + 1u);
    r->counter++;
    return true;
}

static bool refusing_random(void *ctx, uint8_t *out, size_t len)
{
    (void)ctx; (void)out; (void)len;
    return false;
}

static void e21_no_zero_octet(void)
{
    unsigned char k_id[32];
    unsigned char rn2[8], identity[17];
    fake_rng      rng;
    unsigned      trial;

    ppcp_unhex(K_ID_HEX, k_id, sizeof(k_id));

    TEST("5.3a1 (E21) — a drawn identity never carries a 0x00 octet");
    for (trial = 0; trial < 500u; trial++) {
        rng.counter = trial;
        rng.draws   = 0;
        CHECK_EQ_I(ppcp_rv_psk_identity_draw(k_id, fake_random, &rng, rn2, identity),
                   PPCP_OK);
        CHECK(ppcp_rv_psk_identity_usable(identity));
        CHECK(rng.draws >= 1u);
        /* The draw is still a valid 5.3a identity: same 17 octets, same tag,
         * so 5.3b resolves it with no change at the server. */
        {
            unsigned char again[17];
            CHECK_EQ_I(ppcp_rv_psk_identity(k_id, rn2, again), PPCP_OK);
            CHECK_BYTES(again, 17, identity, 17);
        }
    }

    TEST("5.3a1 — the rejection loop actually rejects, and usable() sees a zero");
    {
        unsigned char zeroed[17];
        unsigned      rejected = 0, i;
        memcpy(zeroed, identity, 17);
        zeroed[9] = 0x00u;
        CHECK(!ppcp_rv_psk_identity_usable(zeroed));
        /* Over 500 raw draws, roughly 6% carry a zero somewhere; assert the
         * population the erratum is about is not empty. */
        for (i = 0; i < 500u; i++) {
            unsigned char raw_rn2[8], raw_id[17];
            fake_rng      r;
            r.counter = i; r.draws = 0;
            (void)fake_random(&r, raw_rn2, sizeof(raw_rn2));
            CHECK_EQ_I(ppcp_rv_psk_identity(k_id, raw_rn2, raw_id), PPCP_OK);
            if (!ppcp_rv_psk_identity_usable(raw_id))
                rejected++;
        }
        CHECK(rejected > 0u);
    }

    TEST("5.3a1 — a CSPRNG that cannot answer aborts the draw, never falls back");
    CHECK_EQ_I(ppcp_rv_psk_identity_draw(k_id, refusing_random, NULL, rn2, identity),
               PPCP_ERR_INVALID);

    TEST("RV 10.2's vector is itself free of 0x00, so E21 leaves it conformant");
    {
        unsigned char want[17];
        ppcp_unhex(IDENTITY_HEX, want, sizeof(want));
        CHECK(ppcp_rv_psk_identity_usable(want));
    }
}

int main(void)
{
    sha256_known_answers();
    rt1_derivation_vectors();
    rt14_identities();
    e21_no_zero_octet();
    rt8_resolver();
    rt2_minimal_code();
    rt2_all_fields_code();
    rt2_session_id();
    rt3_unknown_version();
    rt6_expiry();
    base64url_edges();
    TEST_MAIN_END();
}
