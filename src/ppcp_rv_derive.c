/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * PPCP-RV §5.1 key derivation, §3.4 resolvable identifiers, §5.3 PSK identity
 * and the resolver over held pairings.
 *
 * Every random value is a parameter.  Nothing here calls a random number
 * generator, opens a socket or writes to storage — RV 7.2a puts the CSPRNG in
 * the embedding, and RT-12 is a review method because no test on the wire can
 * tell a good secret from a predictable one.
 */
#include "ppcp/rv.h"

#include <string.h>

/* RV §5.1 — ASCII bytes, no terminator. */
static const char INFO_TLS[]  = "ppcp1 tls-psk";
static const char INFO_ID[]   = "ppcp1 rendezvous-id";
/* RV §3.4 and §5.3 — the HMAC labels, likewise unterminated. */
static const char LABEL_RID[] = "ppcp1 rid";
static const char LABEL_PSK[] = "ppcp1 psk-id";

ppcp_result ppcp_rv_derive_from_prk(const uint8_t prk[PPCP_RV_KEY_BYTES], ppcp_rv_keys *out)
{
    ppcp_result rc;

    if (prk == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    memcpy(out->prk, prk, PPCP_RV_KEY_BYTES);

    /* 5.1a and 5.1b: each key has exactly one use.  Deriving them separately is
     * what lets an identifier be broadcast in the clear on a multicast network
     * without that publication revealing anything about the handshake key. */
    rc = ppcp_hkdf_expand(out->prk, (const uint8_t *)INFO_TLS, sizeof(INFO_TLS) - 1u,
                          out->k_tls, PPCP_RV_KEY_BYTES);
    if (rc != PPCP_OK)
        return rc;
    return ppcp_hkdf_expand(out->prk, (const uint8_t *)INFO_ID, sizeof(INFO_ID) - 1u,
                            out->k_id, PPCP_RV_KEY_BYTES);
}

ppcp_result ppcp_rv_derive(const uint8_t *sid, size_t sid_len, const uint8_t *psk,
                           size_t psk_len, ppcp_rv_keys *out)
{
    uint8_t     prk[PPCP_SHA256_BYTES];
    ppcp_result rc;

    if (sid == NULL || psk == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    if (sid_len != PPCP_RV_SID_BYTES)
        return PPCP_ERR_INVALID;            /* 4.3: sid is 16 raw bytes */
    if (psk_len != 16u && psk_len != 32u)
        return PPCP_ERR_INVALID;            /* 4.3: psk is 16 or 32 bytes */

    /* PRK = HKDF-Extract(salt = sid, IKM = psk).  The salt is the session
     * identifier, so key material from one code can never be reused by another
     * even if a publisher were to repeat a secret (7.3d forbids it anyway). */
    rc = ppcp_hkdf_extract(sid, sid_len, psk, psk_len, prk);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_rv_derive_from_prk(prk, out);
    memset(prk, 0, sizeof(prk));
    return rc;
}

/* The shared shape of 3.4 and 5.3: HMAC-SHA256(K_id, label || nonce), first
 * eight bytes.  One construction, keyed the same way, so a reader of either
 * clause is reading the same code. */
static ppcp_result tag8(const uint8_t k_id[PPCP_RV_KEY_BYTES], const char *label,
                        size_t label_len, const uint8_t nonce[PPCP_RV_RN_BYTES],
                        uint8_t out[PPCP_RV_RID_BYTES])
{
    uint8_t msg[32];
    uint8_t mac[PPCP_SHA256_BYTES];

    if (k_id == NULL || nonce == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    if (label_len + PPCP_RV_RN_BYTES > sizeof(msg))
        return PPCP_ERR_INVALID;

    memcpy(msg, label, label_len);
    memcpy(msg + label_len, nonce, PPCP_RV_RN_BYTES);
    ppcp_hmac_sha256(k_id, PPCP_RV_KEY_BYTES, msg, label_len + PPCP_RV_RN_BYTES, mac);
    memcpy(out, mac, PPCP_RV_RID_BYTES);
    memset(mac, 0, sizeof(mac));
    return PPCP_OK;
}

ppcp_result ppcp_rv_rid(const uint8_t k_id[PPCP_RV_KEY_BYTES],
                        const uint8_t rn[PPCP_RV_RN_BYTES],
                        uint8_t rid[PPCP_RV_RID_BYTES])
{
    return tag8(k_id, LABEL_RID, sizeof(LABEL_RID) - 1u, rn, rid);
}

ppcp_result ppcp_rv_instance_name(const uint8_t rid[PPCP_RV_RID_BYTES],
                                  char out[PPCP_RV_INSTANCE_NAME_MAX])
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned i;

    if (rid == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    /* 3.2a: "PPCP-" then the first four bytes of rid in uppercase hex.  3.2b
     * is why it is computed rather than taken from a platform default: those
     * default to the device name, which is frequently a person's name, and
     * publishing that on a driving range's network is a privacy failure no
     * amount of transport encryption repairs. */
    out[0] = 'P'; out[1] = 'P'; out[2] = 'C'; out[3] = 'P'; out[4] = '-';
    for (i = 0; i < 4u; i++) {
        out[5 + i * 2u]      = hex[(rid[i] >> 4) & 0xfu];
        out[5 + i * 2u + 1u] = hex[rid[i] & 0xfu];
    }
    out[13] = '\0';
    return PPCP_OK;
}

ppcp_result ppcp_rv_psk_identity(const uint8_t k_id[PPCP_RV_KEY_BYTES],
                                 const uint8_t rn2[PPCP_RV_RN_BYTES],
                                 uint8_t identity[PPCP_RV_PSK_IDENTITY_BYTES])
{
    ppcp_result rc;

    if (identity == NULL)
        return PPCP_ERR_INVALID;
    /* 5.3a: 0x01 || rn2 || tag.  The leading byte is a format version, kept
     * from A11 so a future third form has a discriminator.
     *
     * 5.3e: no `sid`, no `Peer.id`, nothing stable across connections.  The
     * identity is sent in the clear in the ClientHello, so anything stable in
     * it is a tracking beacon — which is exactly what Draft 1 shipped before
     * this construction replaced it. */
    identity[0] = 0x01u;
    memcpy(identity + 1, rn2, PPCP_RV_RN_BYTES);
    rc = tag8(k_id, LABEL_PSK, sizeof(LABEL_PSK) - 1u, rn2, identity + 1 + PPCP_RV_RN_BYTES);
    if (rc != PPCP_OK)
        memset(identity, 0, PPCP_RV_PSK_IDENTITY_BYTES);
    return rc;
}

ppcp_result ppcp_rv_psk_identity_parse(const uint8_t *identity, size_t len,
                                       uint8_t rn2[PPCP_RV_RN_BYTES],
                                       uint8_t tag[PPCP_RV_RID_BYTES])
{
    if (identity == NULL || rn2 == NULL || tag == NULL)
        return PPCP_ERR_INVALID;
    if (len != PPCP_RV_PSK_IDENTITY_BYTES)
        return PPCP_ERR_MALFORMED;
    if (identity[0] != 0x01u)
        return PPCP_ERR_MALFORMED;
    memcpy(rn2, identity + 1, PPCP_RV_RN_BYTES);
    memcpy(tag, identity + 1 + PPCP_RV_RN_BYTES, PPCP_RV_RID_BYTES);
    return PPCP_OK;
}

/* Both resolvers walk every held pairing without an early exit and compare in
 * constant time.  5.3c requires an unresolvable identity and a wrong key to
 * fail with the same alert; 5.3d asks for them to be indistinguishable in
 * timing too, and a loop that returned on the first match would leak the
 * position of the matching pairing. */
static ppcp_result resolve(const ppcp_rv_pairing *pairings, size_t count,
                           const char *label, size_t label_len,
                           const uint8_t nonce[PPCP_RV_RN_BYTES],
                           const uint8_t want[PPCP_RV_RID_BYTES], size_t *out_index)
{
    size_t i;
    size_t found = 0;
    bool   any   = false;

    if (pairings == NULL || nonce == NULL || want == NULL || out_index == NULL)
        return PPCP_ERR_INVALID;

    for (i = 0; i < count; i++) {
        uint8_t computed[PPCP_RV_RID_BYTES];
        if (pairings[i].k_id == NULL)
            continue;
        if (tag8(pairings[i].k_id, label, label_len, nonce, computed) != PPCP_OK)
            continue;
        if (ppcp_ct_equal(computed, want, PPCP_RV_RID_BYTES) && !any) {
            found = i;
            any   = true;
        }
        memset(computed, 0, sizeof(computed));
    }
    if (!any)
        return PPCP_ERR_NOT_FOUND;
    *out_index = found;
    return PPCP_OK;
}

ppcp_result ppcp_rv_resolve_rid(const ppcp_rv_pairing *pairings, size_t count,
                                const uint8_t rn[PPCP_RV_RN_BYTES],
                                const uint8_t rid[PPCP_RV_RID_BYTES], size_t *out_index)
{
    return resolve(pairings, count, LABEL_RID, sizeof(LABEL_RID) - 1u, rn, rid, out_index);
}

ppcp_result ppcp_rv_resolve_psk_identity(const ppcp_rv_pairing *pairings, size_t count,
                                         const uint8_t *identity, size_t len,
                                         size_t *out_index)
{
    uint8_t     rn2[PPCP_RV_RN_BYTES];
    uint8_t     tag[PPCP_RV_RID_BYTES];
    ppcp_result rc = ppcp_rv_psk_identity_parse(identity, len, rn2, tag);

    if (rc != PPCP_OK)
        return rc;
    return resolve(pairings, count, LABEL_PSK, sizeof(LABEL_PSK) - 1u, rn2, tag, out_index);
}
