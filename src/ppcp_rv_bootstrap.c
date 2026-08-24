/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * PPCP-RV §11.6 — the RV-6 guided-pairing derivation, end to end, from the
 * shared secret to the PRK that §5.1 takes over.
 *
 * ⛔ THERE IS NO X25519 IN THIS FILE AND THERE NEVER WILL BE (11.11, CA1).
 * Exactly two values in §11 need key agreement — a peer's own public key and
 * `Z` — and both arrive here as parameters, exactly as `psk`, `sid`, `rn` and
 * `rn2` already do.  11.11c is what that buys: the whole chain is a pure
 * function of `Z`, `v`, `pk_i` and `pk_a`, so §10.4 reproduces in a component
 * that cannot do curve arithmetic at all, which is where RT-18 belongs (B7).
 *
 * ⛔ ONE CALL, NOT SIX.  `sas_raw`, `K_c`, both MACs, `sid`, `PRK`, `K_tls`
 * and `K_id` all descend from `Z` and the transcript with no branching and no
 * I/O between them.  Six entry points would be six chances to bind the
 * transcript into the wrong one, which is precisely errata E40 and E43 — the
 * two most serious findings in three review passes.  One transcript
 * construction is one thing to get wrong.
 *
 * ⛔ THE TRANSCRIPT IS BOUND INTO `sas_raw` AND `K_c` AND INTO NOTHING ELSE.
 * Not `ct`, not `BK`, not `sid`, not `PRK`, not either MAC label (11.6c1).
 * Binding it into `sid` or `PRK` "for consistency" produces MATCHING digits,
 * MATCHING MACs and a DIVERGENT `PRK`: a comparison the operator affirms, a
 * confirmation that succeeds, and then a TLS handshake failing with
 * PSK_IDENTITY_NOT_FOUND, which looks exactly like RV 3.5d's platform limit
 * and will be diagnosed as one.  §10.4's counter-vector for it is `sid`
 * 18dd04b1…, and tests/test_rv_bootstrap.c asserts that wrong value against
 * this file's right one.
 *
 * ⛔ AND `pk_i || pk_a` IS NOT REDUNDANT WITH `Z` (11.6c2).  X25519 is not
 * contributory: clamping makes every scalar a multiple of 8, so a counterpart
 * can present a DIFFERENT public key yielding a bit-identical, non-zero `Z`
 * (§10.4's R-11 witness, pk_a' 87abc1e8…).  `BK`, `sid` and `PRK` are all
 * identical across that substitution.  The explicit key binding in
 * `sas_raw`'s info is the ONLY thing separating the two peers, and dropping
 * it is undetectable from outside.
 */
#include "ppcp/rv.h"
#include "ppcp_wipe.h"

#include <string.h>

/* 11.5b, 11.6c and 11.6d — ASCII bytes of the quoted label, no terminator. */
static const char LABEL_COMMIT[]  = "ppcp1 bs-commit";
static const char SALT_BOOTSTRAP[] = "ppcp1 bootstrap";
static const char INFO_SAS[]      = "ppcp1 sas";
static const char INFO_CONFIRM[]  = "ppcp1 bs-confirm";
static const char INFO_SID[]      = "ppcp1 bootstrap-sid";
/* 11.5f — two labels, one per direction, so neither MAC can be reflected back
 * at its own sender by a relay that has nothing else to send. */
static const char LABEL_MAC_I[]   = "ppcp1 bs-confirm-i";
static const char LABEL_MAC_A[]   = "ppcp1 bs-confirm-a";

/* 11.6c — v as ONE octet, each pk 32 raw octets, INITIATOR FIRST. */
#define BS_TRANSCRIPT_BYTES (1u + PPCP_RV_BS_KEY_BYTES + PPCP_RV_BS_KEY_BYTES)

/* The longest info is INFO_CONFIRM (16) followed by the transcript (65). */
#define BS_INFO_MAX (sizeof(INFO_CONFIRM) - 1u + BS_TRANSCRIPT_BYTES)


void ppcp_rv_bs_commit(const uint8_t pk_i[PPCP_RV_BS_KEY_BYTES],
                       uint8_t ct[PPCP_RV_BS_CT_BYTES])
{
    ppcp_sha256 s;

    if (pk_i == NULL || ct == NULL)
        return;

    /* 11.5b — ct = SHA-256("ppcp1 bs-commit" || pk_i), and the initiator does
     * NOT send pk_i in the same frame.  No transcript here: 11.6c1's boundary
     * starts below `BK`, and the commitment is above it. */
    ppcp_sha256_init(&s);
    ppcp_sha256_update(&s, LABEL_COMMIT, sizeof(LABEL_COMMIT) - 1u);
    ppcp_sha256_update(&s, pk_i, PPCP_RV_BS_KEY_BYTES);
    ppcp_sha256_final(&s, ct);
}

bool ppcp_rv_ct_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
    /* The same comparison RV 5.3d already needed, named again here because
     * 11.5d and 11.5f are separate MUSTs and a reader of either should not
     * have to go looking for it in common.h. */
    return ppcp_ct_equal(a, b, len);
}

void ppcp_rv_bootstrap_wipe(ppcp_rv_bootstrap *out)
{
    if (out == NULL)
        return;
    /* Everything, both halves.  11.6f as amended by E51: on a handshake that
     * FAILED the erasure includes `PRK`, `K_tls`, `K_id` and `sid` where they
     * were computed — a peer holds all of them from the moment it has `Z`,
     * which is up to the 60 seconds 11.3e allows before either user has
     * affirmed and the pairing exists at all (11.5g).  Computing is not
     * holding, and this is what closes the gap between them. */
    ppcp_wipe(out, sizeof(*out));
}

ppcp_result ppcp_rv_bootstrap_derive(const uint8_t z[PPCP_RV_BS_KEY_BYTES],
                                     uint8_t v,
                                     const uint8_t pk_i[PPCP_RV_BS_KEY_BYTES],
                                     const uint8_t pk_a[PPCP_RV_BS_KEY_BYTES],
                                     ppcp_rv_bootstrap *out)
{
    uint8_t       transcript[BS_TRANSCRIPT_BYTES];
    uint8_t       info[BS_INFO_MAX];
    uint8_t       mac[PPCP_SHA256_BYTES];
    uint8_t       zero_acc = 0u;
    ppcp_rv_keys  keys;
    ppcp_result   rc;
    size_t        i;

    if (z == NULL || pk_i == NULL || pk_a == NULL || out == NULL)
        return PPCP_ERR_INVALID;

    ppcp_wipe(out, sizeof(*out));

    /* 11.4h1 — `v` is 1..255.  The type carries the upper bound; zero is the
     * only unrepresentable-by-type failure left, and it is a CALLER'S BUG.
     * ⛔ It is PPCP_ERR_MALFORMED and not PPCP_ERR_RV_INVALID_KEY: mapping a
     * programming error onto 11.6b's attack signal would have an operator
     * told an attack is under way because a caller passed a zero (R-18). */
    if (v == 0u)
        return PPCP_ERR_MALFORMED;

    /* 11.6b as amended by E36 — an all-zero `Z` is `invalid_key`, derives
     * NOTHING, is never a transport error and is never retried.  Accumulated
     * without an early exit, because `z` is secret.
     *
     * ⚠ This branch fires for almost nobody, and that is the point of E36:
     * OpenSSL fails EVP_PKEY_derive for each of the five standard small-order
     * u-coordinates and CryptoKit throws underlyingCoreCryptoError(-7), so
     * NEITHER application's library ever reaches here with zeros.  The library
     * can only see the zero; the caller can only see the failure; 11.11f
     * makes them the same event and puts the other half on the caller. */
    for (i = 0; i < PPCP_RV_BS_KEY_BYTES; i++)
        zero_acc = (uint8_t)(zero_acc | z[i]);
    if (zero_acc == 0u)
        return PPCP_ERR_RV_INVALID_KEY;

    /* 11.6c — transcript = v || pk_i || pk_a.  INITIATOR FIRST: the order is
     * bound into the derivation, and transposing it is one of the six
     * divergence causes §10.4 lists. */
    transcript[0] = v;
    memcpy(transcript + 1, pk_i, PPCP_RV_BS_KEY_BYTES);
    memcpy(transcript + 1 + PPCP_RV_BS_KEY_BYTES, pk_a, PPCP_RV_BS_KEY_BYTES);

    /* BK = HKDF-Extract(salt = "ppcp1 bootstrap", IKM = Z).  No transcript:
     * 11.6c1 puts the binding in the two expansions below, not in the
     * extraction. */
    rc = ppcp_hkdf_extract((const uint8_t *)SALT_BOOTSTRAP, sizeof(SALT_BOOTSTRAP) - 1u,
                           z, PPCP_RV_BS_KEY_BYTES, out->bk);
    if (rc != PPCP_OK)
        goto fail;

    /* sas_raw = HKDF-Expand(BK, "ppcp1 sas" || transcript, 4).  ⛔ The
     * transcript is here BECAUSE `Z` alone would say neither whose keys
     * produced it (11.6c2) nor under which version (11.4i). */
    memcpy(info, INFO_SAS, sizeof(INFO_SAS) - 1u);
    memcpy(info + sizeof(INFO_SAS) - 1u, transcript, BS_TRANSCRIPT_BYTES);
    rc = ppcp_hkdf_expand(out->bk, info, sizeof(INFO_SAS) - 1u + BS_TRANSCRIPT_BYTES,
                          out->sas_raw, sizeof(out->sas_raw));
    if (rc != PPCP_OK)
        goto fail;

    /* 11.7a — BIG-ENDIAN uint32, modulo 1 000 000, rendered as exactly six
     * decimal digits with leading zeros ("%06u"; `000042` is a valid string).
     * ⚠ Read little-endian this vector yields 808448, which is six perfectly
     * plausible digits that nothing but §10.4 distinguishes from the right
     * answer — PinPointStudio's fifth divergence cause.  Reducing here is
     * also what makes that cause unreachable for a caller: the field is a
     * uint32_t already reduced and no caller ever handles sas_raw as a
     * number. */
    out->sas = (((uint32_t)out->sas_raw[0] << 24) |
                ((uint32_t)out->sas_raw[1] << 16) |
                ((uint32_t)out->sas_raw[2] << 8)  |
                 (uint32_t)out->sas_raw[3]) % 1000000u;

    /* K_c = HKDF-Expand(BK, "ppcp1 bs-confirm" || transcript, 32).  Same
     * binding, one construction rather than two (11.6c). */
    memcpy(info, INFO_CONFIRM, sizeof(INFO_CONFIRM) - 1u);
    memcpy(info + sizeof(INFO_CONFIRM) - 1u, transcript, BS_TRANSCRIPT_BYTES);
    rc = ppcp_hkdf_expand(out->bk, info, sizeof(INFO_CONFIRM) - 1u + BS_TRANSCRIPT_BYTES,
                          out->k_c, PPCP_RV_KEY_BYTES);
    if (rc != PPCP_OK)
        goto fail;

    /* 11.5f — the MACs are keyed by K_c and carry the LABEL ONLY.  ⛔ No
     * transcript: it is already bound, one level up, into the key itself
     * (11.6c1 names "either MAC label" among what must not carry it). */
    ppcp_hmac_sha256(out->k_c, PPCP_RV_KEY_BYTES,
                     (const uint8_t *)LABEL_MAC_I, sizeof(LABEL_MAC_I) - 1u, mac);
    memcpy(out->mac_i, mac, PPCP_RV_BS_MAC_BYTES);
    ppcp_hmac_sha256(out->k_c, PPCP_RV_KEY_BYTES,
                     (const uint8_t *)LABEL_MAC_A, sizeof(LABEL_MAC_A) - 1u, mac);
    memcpy(out->mac_a, mac, PPCP_RV_BS_MAC_BYTES);

    /* 11.6d — sid = HKDF-Expand(BK, "ppcp1 bootstrap-sid", 16).  ⛔ NO
     * TRANSCRIPT (11.6c1), and the counter-vector for getting that wrong is
     * §10.4's sid 18dd04b1…, which produces correct digits and correct MACs
     * and is caught nowhere but the PRK. */
    rc = ppcp_hkdf_expand(out->bk, (const uint8_t *)INFO_SID, sizeof(INFO_SID) - 1u,
                          out->sid, PPCP_RV_SID_BYTES);
    if (rc != PPCP_OK)
        goto fail;

    /* ⛔ THE VERSION AND VARIANT BITS ARE SET BEFORE `sid` IS USED FOR
     * ANYTHING, INCLUDING AS THE SALT BELOW (11.6d, 4.3e).  Salting the PRK
     * with the raw expansion is §10.4's cause 1 — the OLDEST of the six and
     * the one the two-row expand/sid presentation exists to catch.  It too
     * gives correct digits and correct MACs, and its counter-vector is
     * PRK 9b779245…. */
    out->sid[6] = (uint8_t)((out->sid[6] & 0x0fu) | 0x40u);
    out->sid[8] = (uint8_t)((out->sid[8] & 0x3fu) | 0x80u);

    /* 11.6e — from here nothing is new.  §5.1 is taken VERBATIM, over a `PRK`
     * that came from an exchange instead of from a code: it takes an input
     * keying material and a salt, and this path supplies `Z` and a derived
     * `sid` where the code path supplies `psk` and a printed one.  Calling
     * §5.1's own function rather than repeating its three lines is the
     * strongest available statement that this section does not amend it —
     * and giving §5.1 a second shape is what A17 argues against one layer up. */
    rc = ppcp_rv_derive(out->sid, PPCP_RV_SID_BYTES, z, PPCP_RV_BS_KEY_BYTES, &keys);
    if (rc != PPCP_OK) {
        ppcp_wipe(&keys, sizeof(keys));
        goto fail;
    }
    memcpy(out->prk,   keys.prk,   PPCP_RV_KEY_BYTES);
    memcpy(out->k_tls, keys.k_tls, PPCP_RV_KEY_BYTES);
    memcpy(out->k_id,  keys.k_id,  PPCP_RV_KEY_BYTES);
    ppcp_wipe(&keys, sizeof(keys));

    ppcp_wipe(transcript, sizeof(transcript));
    ppcp_wipe(info, sizeof(info));
    ppcp_wipe(mac, sizeof(mac));
    return PPCP_OK;

fail:
    /* Nothing half-derived survives a failure, and that includes the rows
     * already written into `out` (11.6f / E51). */
    ppcp_rv_bootstrap_wipe(out);
    ppcp_wipe(transcript, sizeof(transcript));
    ppcp_wipe(info, sizeof(info));
    ppcp_wipe(mac, sizeof(mac));
    return rc;
}
