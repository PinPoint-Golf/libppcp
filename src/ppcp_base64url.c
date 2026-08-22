/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * base64url without padding — RV 4.1a.
 *
 * Unpadded because the pairing code is scanned from a screen and every
 * character costs scanning distance (RV §4.5), and because `=` is not in the
 * URL-safe alphabet the scheme's users expect.
 */
#include "ppcp/rv.h"

static const char enc_tab[65] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static int dec_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

ppcp_result ppcp_base64url_encode(const uint8_t *in, size_t in_len, char *out,
                                  size_t cap, size_t *out_len)
{
    size_t need = ((in_len + 2u) / 3u) * 4u;
    size_t i = 0, o = 0;

    if (out == NULL || (in_len > 0 && in == NULL))
        return PPCP_ERR_INVALID;
    if (in_len % 3u == 1u) need -= 2u;
    else if (in_len % 3u == 2u) need -= 1u;
    if (cap < need)
        return PPCP_ERR_NOSPACE;

    while (i + 3u <= in_len) {
        uint32_t t = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[o++] = enc_tab[(t >> 18) & 63u];
        out[o++] = enc_tab[(t >> 12) & 63u];
        out[o++] = enc_tab[(t >> 6) & 63u];
        out[o++] = enc_tab[t & 63u];
        i += 3u;
    }
    if (in_len - i == 1u) {
        uint32_t t = (uint32_t)in[i] << 16;
        out[o++] = enc_tab[(t >> 18) & 63u];
        out[o++] = enc_tab[(t >> 12) & 63u];
    } else if (in_len - i == 2u) {
        uint32_t t = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
        out[o++] = enc_tab[(t >> 18) & 63u];
        out[o++] = enc_tab[(t >> 12) & 63u];
        out[o++] = enc_tab[(t >> 6) & 63u];
    }
    if (out_len != NULL)
        *out_len = o;
    return PPCP_OK;
}

ppcp_result ppcp_base64url_decode(const char *in, size_t in_len, uint8_t *out,
                                  size_t cap, size_t *out_len)
{
    size_t   i = 0, o = 0;
    uint32_t acc = 0;
    unsigned nbits = 0;

    if (out == NULL || (in_len > 0 && in == NULL))
        return PPCP_ERR_INVALID;
    /* A padded input is refused rather than tolerated: 4.1a says no padding,
     * and quietly accepting `=` would let two implementations disagree about
     * whether a code round-trips byte-identically. */
    for (i = 0; i < in_len; i++) {
        int v = dec_val(in[i]);
        if (v < 0)
            return PPCP_ERR_MALFORMED;
        acc = (acc << 6) | (uint32_t)v;
        nbits += 6u;
        if (nbits >= 8u) {
            nbits -= 8u;
            if (o >= cap)
                return PPCP_ERR_NOSPACE;
            out[o++] = (uint8_t)((acc >> nbits) & 0xffu);
        }
    }
    /* Leftover bits must be zero, or the encoding was not canonical. */
    if (nbits >= 6u)
        return PPCP_ERR_MALFORMED;
    if (nbits > 0 && (acc & ((1u << nbits) - 1u)) != 0)
        return PPCP_ERR_MALFORMED;
    if (out_len != NULL)
        *out_len = o;
    return PPCP_OK;
}
