/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * hash.h — SHA-256, HMAC-SHA256 and HKDF-SHA256, implemented in the library.
 *
 * No OpenSSL, no libsodium, no platform crypto: plan A1 says libppcp has no
 * dependencies, and REQ-LIC-2/3/5 are why.  These three primitives are what
 * PPCP needs — SHA-256 for the payload digests of ENC 6c, and HKDF over HMAC
 * for the key derivation of RV §5.1 — and all three are short enough to read.
 *
 * ⚠ These are the correctness half of RV's security model, not the whole of
 * it.  RT-12 — entropy quality, protected storage, erasure — is a *review*
 * method precisely because nothing here can demonstrate it: the library never
 * generates a random byte (RV 7.2a is the embedding's obligation) and never
 * stores one.  Every random input below is a parameter.
 */
#ifndef PPCP_HASH_H
#define PPCP_HASH_H

#include "ppcp/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PPCP_SHA256_BYTES 32
#define PPCP_SHA256_BLOCK 64

typedef struct ppcp_sha256 {
    uint32_t h[8];
    uint64_t bits;
    uint8_t  buf[PPCP_SHA256_BLOCK];
    size_t   buflen;
} ppcp_sha256;

PPCP_API void ppcp_sha256_init(ppcp_sha256 *s);
PPCP_API void ppcp_sha256_update(ppcp_sha256 *s, const void *data, size_t len);
PPCP_API void ppcp_sha256_final(ppcp_sha256 *s, uint8_t out[PPCP_SHA256_BYTES]);
PPCP_API void ppcp_sha256_hash(const void *data, size_t len, uint8_t out[PPCP_SHA256_BYTES]);

PPCP_API void ppcp_hmac_sha256(const uint8_t *key, size_t key_len,
                               const uint8_t *msg, size_t msg_len,
                               uint8_t out[PPCP_SHA256_BYTES]);

/* RFC 5869.  `salt` may be empty, in which case a block of zeros is used, as
 * the RFC specifies — though RV §5.1 always supplies `sid`. */
PPCP_API ppcp_result ppcp_hkdf_extract(const uint8_t *salt, size_t salt_len,
                                       const uint8_t *ikm, size_t ikm_len,
                                       uint8_t prk[PPCP_SHA256_BYTES]);

/* `info` is the ASCII bytes of the label with no terminator (RV §5.1). */
PPCP_API ppcp_result ppcp_hkdf_expand(const uint8_t prk[PPCP_SHA256_BYTES],
                                      const uint8_t *info, size_t info_len,
                                      uint8_t *out, size_t out_len);

#ifdef __cplusplus
}
#endif
#endif /* PPCP_HASH_H */
