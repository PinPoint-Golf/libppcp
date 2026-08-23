/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * cbor.h — the deterministic encoder and the limit-enforcing decoder of
 * PPCP-ENC §4 and §8.
 *
 * Written rather than vendored (plan A2).  A general-purpose CBOR library will
 * not enforce the §8 limits before allocating, will not reject an integer key,
 * and will not refuse `null` — and those three refusals are the ones the
 * protocol's extension model rests on.
 *
 * The decoder never allocates and never copies: every text and byte string it
 * reports is a pointer into the caller's own buffer.  That is the strongest
 * possible reading of ENC 3a — a length that breaches §8 is refused when its
 * head is read, and nothing was reserved for it because nothing is ever
 * reserved at all.
 */
#ifndef PPCP_CBOR_H
#define PPCP_CBOR_H

#include "ppcp/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ENC §8 — enforced before allocation.  Sizes are the decoder's, per item; the
 * frame limits themselves live in frame.h because they are per channel. */
#define PPCP_CBOR_MAX_DEPTH        16        /* ENC §8 nesting depth */
#define PPCP_CBOR_MAX_ELEMENTS     (1u << 20)/* ENC §8 array or map elements */
#define PPCP_CBOR_MAX_TEXT         (64u * 1024u)   /* ENC §8 text string */
#define PPCP_CBOR_MAX_THUMBNAIL    (64u * 1024u)   /* ENC §8 thumbnail bytes */
#define PPCP_CBOR_MAX_ANNOTATION   (8u * 1024u)    /* ENC §8 Annotation.body */

/* ENC §8 gives the byte-string limit as "equal to the frame limit", so the
 * decoder is told which frame it is decoding rather than assuming the larger.
 * A control frame that carries an 8 MiB byte string is malformed even though
 * the same bytes on a bulk frame would not be. */
typedef struct ppcp_cbor_limits {
    uint32_t max_bytes;   /* byte-string limit: the frame limit of this channel */
    uint32_t max_text;    /* PPCP_CBOR_MAX_TEXT unless a field narrows it */
    uint32_t max_depth;
    uint32_t max_elements;
} ppcp_cbor_limits;

/* The limits for a frame arriving on `channel` (ENC §8 with §2a). */
PPCP_API ppcp_cbor_limits ppcp_cbor_limits_for_channel(uint8_t channel);

/* ------------------------------------------------------------------ encoder */

/* ENC 4e / RV 4.3a.
 *
 * PPCP_CBOR_ORDER_DETERMINISTIC is the default and rejects a map key that does
 * not sort strictly after its predecessor under RFC 8949 §4.2.1 — bytewise over
 * the *encoded* key.  That makes deterministic encoding a property of the
 * writer rather than a discipline in twelve call sites, and it makes a
 * duplicate key impossible to emit rather than merely forbidden (ENC 4d).
 *
 * PPCP_CBOR_ORDER_LITERAL emits keys in the order written and rejects only
 * exact duplicates.  It exists for two reasons and no others: emitting the
 * PRE-ERRATUM ordering of the ENC §5.1 worked example, which erratum E13
 * re-emitted deterministically but 5.1b keeps legal on receipt (see the note in
 * envelope.h), and re-emitting a foreign peer's map without reordering it.  The
 * RV payload encoder never offers it — RV 4.3a is a MUST. */
typedef enum ppcp_cbor_order {
    PPCP_CBOR_ORDER_DETERMINISTIC = 0,
    PPCP_CBOR_ORDER_LITERAL
} ppcp_cbor_order;

typedef struct ppcp_cbor_level {
    uint32_t remaining;    /* items still owed at this level */
    bool     is_map;
    bool     expect_key;
    size_t   last_key_off; /* offset of the previous key's encoded head */
    size_t   last_key_len; /* its total encoded length */
} ppcp_cbor_level;

typedef struct ppcp_cbor_writer {
    uint8_t        *buf;
    size_t          cap;
    size_t          len;
    ppcp_result     err;      /* sticky: the first failure wins */
    ppcp_cbor_order order;
    uint32_t        depth;
    ppcp_cbor_level level[PPCP_CBOR_MAX_DEPTH];
} ppcp_cbor_writer;

PPCP_API void ppcp_cbor_writer_init(ppcp_cbor_writer *w, uint8_t *buf, size_t cap);
PPCP_API void ppcp_cbor_writer_init_order(ppcp_cbor_writer *w, uint8_t *buf,
                                          size_t cap, ppcp_cbor_order order);

/* Every write returns the writer's sticky error, so a caller may write a whole
 * message and check once. */
PPCP_API ppcp_result ppcp_cbor_write_uint(ppcp_cbor_writer *w, uint64_t v);
PPCP_API ppcp_result ppcp_cbor_write_int(ppcp_cbor_writer *w, int64_t v);
PPCP_API ppcp_result ppcp_cbor_write_bytes(ppcp_cbor_writer *w, const uint8_t *p, size_t n);
PPCP_API ppcp_result ppcp_cbor_write_text(ppcp_cbor_writer *w, const char *p, size_t n);
PPCP_API ppcp_result ppcp_cbor_write_text_z(ppcp_cbor_writer *w, const char *s);
PPCP_API ppcp_result ppcp_cbor_write_bool(ppcp_cbor_writer *w, bool v);
/* ENC §4: floats go on the wire as doubles (0xFB).  There is no half or single
 * writer, because an encoder MUST NOT emit them. */
PPCP_API ppcp_result ppcp_cbor_write_double(ppcp_cbor_writer *w, double v);
PPCP_API ppcp_result ppcp_cbor_write_array(ppcp_cbor_writer *w, size_t count);
PPCP_API ppcp_result ppcp_cbor_write_map(ppcp_cbor_writer *w, size_t count);

/* Fails if any container is still owed items — a truncated map is a bug that
 * should not reach a socket. */
PPCP_API ppcp_result ppcp_cbor_writer_finish(const ppcp_cbor_writer *w, size_t *out_len);
PPCP_API ppcp_result ppcp_cbor_writer_status(const ppcp_cbor_writer *w);

/* RFC 8949 §4.2.1 ordering over two encoded text keys: <0, 0, >0.  Exposed
 * because RV 4.3b is a claim about this function's result. */
PPCP_API int ppcp_cbor_key_cmp(const char *a, size_t alen, const char *b, size_t blen);

/* ------------------------------------------------------------------ decoder */

typedef enum ppcp_cbor_type {
    PPCP_CBOR_UINT = 0,
    PPCP_CBOR_NINT,
    PPCP_CBOR_BYTES,
    PPCP_CBOR_TEXT,
    PPCP_CBOR_ARRAY,
    PPCP_CBOR_MAP,
    PPCP_CBOR_BOOL,
    PPCP_CBOR_DOUBLE
} ppcp_cbor_type;

typedef struct ppcp_cbor_item {
    ppcp_cbor_type type;
    /* Integers are reported in `i` where they fit int64 (ENC §4: "MUST fit in
     * int64"), and a uint beyond INT64_MAX is malformed rather than silently
     * wrapped. */
    int64_t        i;
    double         f;
    bool           b;
    const uint8_t *bytes;  /* BYTES/TEXT: points into the caller's buffer */
    size_t         len;    /* BYTES/TEXT length, in bytes */
    uint32_t       count;  /* ARRAY/MAP element count */
} ppcp_cbor_item;

typedef struct ppcp_cbor_reader {
    const uint8_t   *buf;
    size_t           len;
    size_t           pos;
    uint32_t         depth;
    ppcp_cbor_limits lim;
} ppcp_cbor_reader;

PPCP_API void ppcp_cbor_reader_init(ppcp_cbor_reader *r, const uint8_t *buf,
                                    size_t len, ppcp_cbor_limits lim);

/* Reads the next item's head.  For ARRAY and MAP the elements follow and are
 * read individually; `count` has already been checked against the element
 * limit, which is what ENC 3a's "before allocating" means for a container. */
PPCP_API ppcp_result ppcp_cbor_read(ppcp_cbor_reader *r, ppcp_cbor_item *out);

/* Skips one complete item, nested contents included.  This is I13: a decoder
 * that meets a key it does not know skips its value at any depth and carries
 * on.  Iterative, so a hostile nesting cannot exhaust the C stack — though the
 * depth limit of §8 would have refused it first. */
PPCP_API ppcp_result ppcp_cbor_skip(ppcp_cbor_reader *r);

/* Reads a map key.  Rejects a non-text key as malformed (ENC 4a) — an integer
 * key is the one extension mechanism the protocol deliberately gave up. */
PPCP_API ppcp_result ppcp_cbor_read_key(ppcp_cbor_reader *r, const char **key, size_t *key_len);

PPCP_API bool ppcp_cbor_key_is(const char *key, size_t key_len, const char *lit);

/* A whole-item validating pass: every rule of ENC §4 and §8 at every depth,
 * including duplicate keys (4d), integer keys (4a), `null` (4c), tags and
 * indefinite lengths.  Decoders call it once on a frame payload so the field
 * readers that follow can be simple.
 *
 * Duplicate detection needs the keys of a map at once; the scratch arena is
 * sized by PPCP_CBOR_DUP_SCRATCH and a map with more distinct keys than the
 * arena can hold is PPCP_ERR_LIMIT rather than silently unchecked.  No PPCP
 * message comes within an order of magnitude of it. */
#define PPCP_CBOR_DUP_SCRATCH 256

PPCP_API ppcp_result ppcp_cbor_validate(const uint8_t *buf, size_t len,
                                        ppcp_cbor_limits lim, size_t *consumed);

#ifdef __cplusplus
}
#endif
#endif /* PPCP_CBOR_H */
