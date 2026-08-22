/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * PPCP-ENC §2, §3 and §7.  All multi-byte header fields are big-endian, which
 * is also CBOR's own byte order, so there is exactly one byte order in the
 * whole format (ENC §9).
 */
#include "ppcp/frame.h"

#include <string.h>

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

bool ppcp_channel_is_bulk(uint8_t channel)
{
    /* ENC 2a: channel 0 is control, 1 and above are bulk. */
    return channel != PPCP_CHANNEL_CONTROL && channel != PPCP_CHANNEL_RESERVED;
}

ppcp_result ppcp_channel_validate(uint8_t channel)
{
    return (channel == PPCP_CHANNEL_RESERVED) ? PPCP_ERR_MALFORMED : PPCP_OK;
}

uint32_t ppcp_channel_frame_limit(uint8_t channel)
{
    return ppcp_channel_is_bulk(channel) ? PPCP_LIMIT_BULK_FRAME
                                         : PPCP_LIMIT_CONTROL_FRAME;
}

ppcp_result ppcp_frame_check_length(uint8_t channel, uint32_t payload_len)
{
    if (ppcp_channel_validate(channel) != PPCP_OK)
        return PPCP_ERR_MALFORMED;
    if (payload_len > ppcp_channel_frame_limit(channel))
        return PPCP_ERR_FATAL_LIMIT;   /* ENC 8a */
    return PPCP_OK;
}

ppcp_result ppcp_frame_check_stream(uint8_t header_channel, uint8_t stream_channel)
{
    return (header_channel == stream_channel) ? PPCP_OK : PPCP_ERR_MALFORMED;
}

ppcp_result ppcp_frame_header_write(uint8_t out[PPCP_FRAME_HEADER_BYTES],
                                    const ppcp_frame_header *h)
{
    ppcp_result rc;

    if (out == NULL || h == NULL)
        return PPCP_ERR_INVALID;
    if (h->flags != 0 || h->reserved != 0)
        return PPCP_ERR_INVALID;       /* ENC 3b */
    rc = ppcp_frame_check_length(h->channel, h->payload_len);
    if (rc != PPCP_OK)
        return rc;

    wr32(out, h->payload_len);
    out[4] = h->channel;
    out[5] = 0;
    wr16(out + 6, 0);
    return PPCP_OK;
}

ppcp_result ppcp_frame_header_parse(const uint8_t in[PPCP_FRAME_HEADER_BYTES],
                                    ppcp_frame_header *out)
{
    if (in == NULL || out == NULL)
        return PPCP_ERR_INVALID;

    out->payload_len = rd32(in);
    out->channel     = in[4];
    /* ENC 3b: reported, not refused.  A later MINOR may use these bits and a
     * ppcp/1.0 receiver must not fail the frame for carrying them. */
    out->flags       = in[5];
    out->reserved    = rd16(in + 6);

    return ppcp_frame_check_length(out->channel, out->payload_len);
}

ppcp_result ppcp_frame_read(const uint8_t *buf, size_t len, ppcp_frame_header *out_header,
                            const uint8_t **out_payload, size_t *out_consumed)
{
    ppcp_frame_header h;
    ppcp_result       rc;

    if (buf == NULL || out_header == NULL)
        return PPCP_ERR_INVALID;
    if (len < PPCP_FRAME_HEADER_BYTES)
        return PPCP_ERR_TRUNCATED;

    /* The length is checked here, before the payload is looked for.  That is
     * ENC 3a: a frame beyond the channel's limit is rejected without anything
     * being reserved for it. */
    rc = ppcp_frame_header_parse(buf, &h);
    if (rc != PPCP_OK)
        return rc;

    if (len - PPCP_FRAME_HEADER_BYTES < h.payload_len)
        return PPCP_ERR_TRUNCATED;     /* ENC 3c — the caller decides what that means */

    *out_header = h;
    if (out_payload != NULL)
        *out_payload = buf + PPCP_FRAME_HEADER_BYTES;
    if (out_consumed != NULL)
        *out_consumed = PPCP_FRAME_HEADER_BYTES + h.payload_len;
    return PPCP_OK;
}

ppcp_result ppcp_frame_write(uint8_t *out, size_t cap, uint8_t channel,
                             const uint8_t *payload, size_t payload_len,
                             size_t *out_written)
{
    ppcp_frame_header h;
    ppcp_result       rc;

    if (out == NULL || (payload == NULL && payload_len > 0))
        return PPCP_ERR_INVALID;
    if (payload_len > 0xffffffffu)
        return PPCP_ERR_FATAL_LIMIT;

    h.payload_len = (uint32_t)payload_len;
    h.channel     = channel;
    h.flags       = 0;
    h.reserved    = 0;

    rc = ppcp_frame_check_length(channel, h.payload_len);
    if (rc != PPCP_OK)
        return rc;
    if (cap < PPCP_FRAME_HEADER_BYTES + payload_len)
        return PPCP_ERR_NOSPACE;

    rc = ppcp_frame_header_write(out, &h);
    if (rc != PPCP_OK)
        return rc;
    memcpy(out + PPCP_FRAME_HEADER_BYTES, payload, payload_len);
    if (out_written != NULL)
        *out_written = PPCP_FRAME_HEADER_BYTES + payload_len;
    return PPCP_OK;
}

/* ------------------------------------------------------------ ENC §7 bundle */

ppcp_result ppcp_bundle_header_write(uint8_t out[PPCP_BUNDLE_HEADER_BYTES],
                                     const ppcp_bundle_header *h)
{
    if (out == NULL || h == NULL)
        return PPCP_ERR_INVALID;
    if (h->major != PPCP_BUNDLE_MAJOR)
        return PPCP_ERR_INVALID;
    if (h->reserved != 0)
        return PPCP_ERR_INVALID;

    memcpy(out, PPCP_BUNDLE_MAGIC, PPCP_BUNDLE_MAGIC_BYTES);
    wr16(out + 8, h->major);
    wr16(out + 10, h->minor);
    wr32(out + 12, 0);
    return PPCP_OK;
}

ppcp_result ppcp_bundle_header_parse(const uint8_t in[PPCP_BUNDLE_HEADER_BYTES],
                                     ppcp_bundle_header *out)
{
    if (in == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    if (memcmp(in, PPCP_BUNDLE_MAGIC, PPCP_BUNDLE_MAGIC_BYTES) != 0)
        return PPCP_ERR_MALFORMED;

    out->major    = rd16(in + 8);
    out->minor    = rd16(in + 10);
    out->reserved = rd32(in + 12);

    if (out->major != PPCP_BUNDLE_MAJOR)
        return PPCP_ERR_MALFORMED;
    /* ENC 7f: a higher minor is accepted.  The frames it carries that this
     * reader does not understand are skipped, not refused (I13). */
    return PPCP_OK;
}
