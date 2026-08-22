/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * frame.h — the frame header and channel rules of PPCP-ENC §2 and §3.
 *
 * Eight bytes in front of every payload, on a socket and in a bundle alike.
 * The header is redundant on a transport that already delimits messages and is
 * mandatory anyway (ENC §3.1): a file has no channels, so without the channel
 * byte a control frame and a bulk chunk are indistinguishable and the "live
 * bytes are bundle bytes" claim collapses into a second format.
 */
#ifndef PPCP_FRAME_H
#define PPCP_FRAME_H

#include "ppcp/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PPCP_FRAME_HEADER_BYTES 8u

/* ENC 2a */
#define PPCP_CHANNEL_CONTROL   0u
#define PPCP_CHANNEL_BULK      1u
#define PPCP_CHANNEL_PREVIEW   2u   /* plan A6 — the optional third connection */
#define PPCP_CHANNEL_RESERVED  255u

/* ENC §8.  Two values, not one, because 8a and 8b are deliberately different:
 * a length beyond the channel limit means the stream has desynchronised and is
 * fatal, while every other breach is answered with `error` so that one bad
 * message does not end a session that is otherwise capturing. */
#define PPCP_LIMIT_CONTROL_FRAME (1u * 1024u * 1024u)
#define PPCP_LIMIT_BULK_FRAME    (8u * 1024u * 1024u)
#define PPCP_LIMIT_CHUNK_BYTES   (4u * 1024u * 1024u)   /* ENC 6f */
#define PPCP_DEFAULT_CHUNK_BYTES (256u * 1024u)         /* ENC 6f SHOULD */

typedef struct ppcp_frame_header {
    uint32_t payload_len;
    uint8_t  channel;
    uint8_t  flags;      /* ENC 3b: MUST be 0 in ppcp/1.0 */
    uint16_t reserved;   /* ENC 3b: MUST be 0 in ppcp/1.0 */
} ppcp_frame_header;

PPCP_API bool ppcp_channel_is_bulk(uint8_t channel);

/* ENC 2a: 255 is reserved and is not a usable channel. */
PPCP_API ppcp_result ppcp_channel_validate(uint8_t channel);

/* ENC §8: 1 MiB on control, 8 MiB on bulk. */
PPCP_API uint32_t ppcp_channel_frame_limit(uint8_t channel);

/* ENC 3a + 8a.  Returns PPCP_ERR_FATAL_LIMIT — never PPCP_ERR_LIMIT — because
 * a peer cannot skip a frame whose length it cannot trust. */
PPCP_API ppcp_result ppcp_frame_check_length(uint8_t channel, uint32_t payload_len);

/* ENC 2c: where a transport carries one channel per stream, the channel byte
 * still has to match the stream the frame arrived on.  A mismatch is
 * `error` / `malformed`.  Plan A6 makes this checkable, which is one of the
 * reasons two TCP connections were chosen over a multiplexer. */
PPCP_API ppcp_result ppcp_frame_check_stream(uint8_t header_channel, uint8_t stream_channel);

/* Refuses a non-zero `flags` or `reserved`: an encoder MUST emit zero. */
PPCP_API ppcp_result ppcp_frame_header_write(uint8_t out[PPCP_FRAME_HEADER_BYTES],
                                             const ppcp_frame_header *h);

/* Accepts a non-zero `flags` or `reserved` and reports them (ENC 3b: a
 * receiver ignores unknown bits rather than failing, so a later MINOR may use
 * them).  Checks the length against the channel limit, which is the whole
 * point of reading the header before the body. */
PPCP_API ppcp_result ppcp_frame_header_parse(const uint8_t in[PPCP_FRAME_HEADER_BYTES],
                                             ppcp_frame_header *out);

/* Reads one frame from a byte stream.
 *
 * Returns PPCP_ERR_TRUNCATED when the buffer holds less than a whole frame.
 * ENC 3c makes that the reader's decision rather than this function's: at the
 * end of a bundle it means `completeness: partial` (ENC 7d), and on a live
 * transport it is a fatal `malformed`. */
PPCP_API ppcp_result ppcp_frame_read(const uint8_t *buf, size_t len,
                                     ppcp_frame_header *out_header,
                                     const uint8_t **out_payload,
                                     size_t *out_consumed);

PPCP_API ppcp_result ppcp_frame_write(uint8_t *out, size_t cap, uint8_t channel,
                                      const uint8_t *payload, size_t payload_len,
                                      size_t *out_written);

/* ------------------------------------------------------- ENC §7 bundle header */

#define PPCP_BUNDLE_MAGIC        "PPCPBNDL"
#define PPCP_BUNDLE_MAGIC_BYTES  8u
#define PPCP_BUNDLE_HEADER_BYTES 16u
#define PPCP_BUNDLE_MAJOR        1u
#define PPCP_BUNDLE_MINOR        0u

typedef struct ppcp_bundle_header {
    uint16_t major;
    uint16_t minor;
    uint32_t reserved;
} ppcp_bundle_header;

PPCP_API ppcp_result ppcp_bundle_header_write(uint8_t out[PPCP_BUNDLE_HEADER_BYTES],
                                              const ppcp_bundle_header *h);

/* ENC 7f: a reader accepts a bundle whose `minor` exceeds its own and ignores
 * the frames it does not understand (I13).  A differing `major` is refused —
 * that is what MAJOR means. */
PPCP_API ppcp_result ppcp_bundle_header_parse(const uint8_t in[PPCP_BUNDLE_HEADER_BYTES],
                                              ppcp_bundle_header *out);

#ifdef __cplusplus
}
#endif
#endif /* PPCP_FRAME_H */
