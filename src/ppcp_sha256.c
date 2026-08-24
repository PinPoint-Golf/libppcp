/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * SHA-256 (FIPS 180-4), HMAC-SHA256 (RFC 2104) and HKDF-SHA256 (RFC 5869).
 *
 * Written out rather than pulled in: the library has no dependencies (plan A1),
 * and RV §10's vectors are the test that this is right.
 */
#include "ppcp/hash.h"
#include "ppcp_wipe.h"

#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static uint32_t ror(uint32_t x, unsigned n) { return (x >> n) | (x << (32u - n)); }

static void sha256_block(ppcp_sha256 *s, const uint8_t *p)
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    unsigned i;

    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
               ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
    for (i = 16; i < 64; i++) {
        uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3];
    e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];

    for (i = 0; i < 64; i++) {
        uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
    s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

void ppcp_sha256_init(ppcp_sha256 *s)
{
    if (s == NULL) return;
    s->h[0] = 0x6a09e667u; s->h[1] = 0xbb67ae85u;
    s->h[2] = 0x3c6ef372u; s->h[3] = 0xa54ff53au;
    s->h[4] = 0x510e527fu; s->h[5] = 0x9b05688cu;
    s->h[6] = 0x1f83d9abu; s->h[7] = 0x5be0cd19u;
    s->bits   = 0;
    s->buflen = 0;
}

void ppcp_sha256_update(ppcp_sha256 *s, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;

    if (s == NULL || (len > 0 && p == NULL))
        return;
    s->bits += (uint64_t)len * 8u;

    if (s->buflen > 0) {
        size_t need = PPCP_SHA256_BLOCK - s->buflen;
        size_t take = (len < need) ? len : need;
        memcpy(s->buf + s->buflen, p, take);
        s->buflen += take;
        p   += take;
        len -= take;
        if (s->buflen == PPCP_SHA256_BLOCK) {
            sha256_block(s, s->buf);
            s->buflen = 0;
        }
    }
    while (len >= PPCP_SHA256_BLOCK) {
        sha256_block(s, p);
        p   += PPCP_SHA256_BLOCK;
        len -= PPCP_SHA256_BLOCK;
    }
    if (len > 0) {
        memcpy(s->buf + s->buflen, p, len);
        s->buflen += len;
    }
}

void ppcp_sha256_final(ppcp_sha256 *s, uint8_t out[PPCP_SHA256_BYTES])
{
    uint8_t  pad[PPCP_SHA256_BLOCK * 2];
    size_t   padlen;
    uint64_t bits;
    unsigned i;

    if (s == NULL || out == NULL) return;
    bits = s->bits;

    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80u;
    padlen = (s->buflen < 56u) ? (56u - s->buflen) : (120u - s->buflen);
    ppcp_sha256_update(s, pad, padlen);
    s->bits = bits;   /* the padding is not message length */

    for (i = 0; i < 8; i++)
        pad[i] = (uint8_t)(bits >> (56u - 8u * i));
    ppcp_sha256_update(s, pad, 8);

    for (i = 0; i < 8; i++) {
        out[i * 4]     = (uint8_t)(s->h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(s->h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(s->h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)s->h[i];
    }
}

void ppcp_sha256_hash(const void *data, size_t len, uint8_t out[PPCP_SHA256_BYTES])
{
    ppcp_sha256 s;
    ppcp_sha256_init(&s);
    ppcp_sha256_update(&s, data, len);
    ppcp_sha256_final(&s, out);
}

void ppcp_hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *msg,
                      size_t msg_len, uint8_t out[PPCP_SHA256_BYTES])
{
    uint8_t     k[PPCP_SHA256_BLOCK];
    uint8_t     pad[PPCP_SHA256_BLOCK];
    uint8_t     inner[PPCP_SHA256_BYTES];
    ppcp_sha256 s;
    unsigned    i;

    if (out == NULL) return;

    memset(k, 0, sizeof(k));
    if (key_len > PPCP_SHA256_BLOCK)
        ppcp_sha256_hash(key, key_len, k);
    else if (key_len > 0)
        memcpy(k, key, key_len);

    for (i = 0; i < PPCP_SHA256_BLOCK; i++)
        pad[i] = (uint8_t)(k[i] ^ 0x36u);
    ppcp_sha256_init(&s);
    ppcp_sha256_update(&s, pad, sizeof(pad));
    ppcp_sha256_update(&s, msg, msg_len);
    ppcp_sha256_final(&s, inner);

    for (i = 0; i < PPCP_SHA256_BLOCK; i++)
        pad[i] = (uint8_t)(k[i] ^ 0x5cu);
    ppcp_sha256_init(&s);
    ppcp_sha256_update(&s, pad, sizeof(pad));
    ppcp_sha256_update(&s, inner, sizeof(inner));
    ppcp_sha256_final(&s, out);

    /* ⛔ EVERY LOCAL HERE IS KEY-BEARING, AND THREE OF THE FOUR USED TO BE
     * CLEARED WITH A PLAIN memset (machine review, F1).  The comment on `k`
     * was right and applied to its neighbours too:
     *
     *   k      the key schedule itself;
     *   pad    holds `k[i] ^ 0x5c` on the way out — XOR is its own inverse, so
     *          this recovers the key exactly.  For 11.5f's confirmation MACs
     *          that key is `K_c`, and for hkdf_expand it is the `PRK`;
     *   inner  the inner digest, a value the outer HMAC is keyed over;
     *   s      ⚠ the one nobody had noticed.  The context is a 64-octet buffer
     *          and it is NOT re-initialised on the way out, so after
     *          hkdf_extract("ppcp1 bootstrap", Z) its tail still holds bytes
     *          of `Z`, and after any keyed expansion it holds `key ^ 0x5c`.
     *
     * All four now go through the one helper (7.2e, 11.6f/E51, 11.11h). */
    ppcp_wipe(k, sizeof(k));
    ppcp_wipe(pad, sizeof(pad));
    ppcp_wipe(inner, sizeof(inner));
    ppcp_wipe(&s, sizeof(s));
}

ppcp_result ppcp_hkdf_extract(const uint8_t *salt, size_t salt_len, const uint8_t *ikm,
                              size_t ikm_len, uint8_t prk[PPCP_SHA256_BYTES])
{
    uint8_t zeros[PPCP_SHA256_BYTES];

    if (prk == NULL || (ikm_len > 0 && ikm == NULL))
        return PPCP_ERR_INVALID;
    if (salt == NULL || salt_len == 0) {
        memset(zeros, 0, sizeof(zeros));
        salt     = zeros;
        salt_len = sizeof(zeros);
    }
    /* RFC 5869 §2.2: PRK = HMAC-Hash(salt, IKM).  RV §5.1 passes `sid` as the
     * salt and `psk` as the IKM, which is what binds the derived keys to the
     * session the code was minted for. */
    ppcp_hmac_sha256(salt, salt_len, ikm, ikm_len, prk);
    return PPCP_OK;
}

ppcp_result ppcp_hkdf_expand(const uint8_t prk[PPCP_SHA256_BYTES], const uint8_t *info,
                             size_t info_len, uint8_t *out, size_t out_len)
{
    uint8_t  t[PPCP_SHA256_BYTES];
    uint8_t  block[PPCP_SHA256_BYTES + 512 + 1];
    size_t   done = 0;
    uint8_t  counter = 1;
    size_t   tlen = 0;

    if (prk == NULL || out == NULL || (info_len > 0 && info == NULL))
        return PPCP_ERR_INVALID;
    if (out_len == 0 || out_len > 255u * PPCP_SHA256_BYTES)
        return PPCP_ERR_INVALID;
    if (info_len > 512u)
        return PPCP_ERR_INVALID;   /* every RV info string is under twenty bytes */

    while (done < out_len) {
        size_t n = 0, take;
        if (tlen > 0) {
            memcpy(block, t, tlen);
            n = tlen;
        }
        if (info_len > 0) {
            memcpy(block + n, info, info_len);
            n += info_len;
        }
        block[n++] = counter;

        ppcp_hmac_sha256(prk, PPCP_SHA256_BYTES, block, n, t);
        tlen = PPCP_SHA256_BYTES;

        take = out_len - done;
        if (take > PPCP_SHA256_BYTES)
            take = PPCP_SHA256_BYTES;
        memcpy(out + done, t, take);
        done += take;
        counter++;
    }
    /* `t` is a block of the output key stream and `block` is the PRK-keyed
     * input that produced it; neither outlives the call (7.2e). */
    ppcp_wipe(t, sizeof(t));
    ppcp_wipe(block, sizeof(block));
    return PPCP_OK;
}
