/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * ppcp_codec.h — INTERNAL.  Table-driven CBOR record encode/decode.
 *
 * Not part of the port surface (plan A3): nothing here carries PPCP_API and
 * nothing here appears in include/ppcp.  It exists because L4 and L5 between
 * them encode sixty-odd record shapes, and writing each one by hand has two
 * failure modes that a table does not have.
 *
 *   1. Deterministic key order (ENC 4e).  The writer of cbor.h refuses a key
 *      that does not sort strictly after its predecessor, so a hand-written
 *      encoder has to interleave optional fields into RFC 8949 §4.2.1 order at
 *      the source level — where the order is invisible and a later field
 *      addition silently breaks it.  ppcp_rec_write() sorts at run time, so
 *      the order is a property of the function rather than of the author.
 *   2. Unknown keys (I13, ENC 4b).  ppcp_rec_read() skips any key not in its
 *      table, at every depth, by construction rather than by remembering to.
 *
 * Neither reader nor writer allocates: the reader reports text and byte
 * strings as pointers into the caller's buffer, exactly as ppcp_cbor_read does.
 */
#ifndef PPCP_CODEC_H
#define PPCP_CODEC_H

#include "ppcp/cbor.h"
#include "ppcp/frame.h"
#include "ppcp/envelope.h"
#include "ppcp/time.h"

struct ppcp_peer_desc;
struct ppcp_capture;

/* Zero-copy views into the decoder's input buffer. */
typedef struct ppcp_text_ref {
    const char *p;
    size_t      len;
} ppcp_text_ref;

typedef struct ppcp_bytes_ref {
    const uint8_t *p;
    size_t         len;
} ppcp_bytes_ref;

/* A closed enumeration's wire spelling.  Terminated by a NULL name.
 * Open registries are NOT enums: CORE 10.3a makes an unknown value ignored
 * rather than fatal, so they stay strings all the way through (I13). */
typedef struct ppcp_enum_map {
    const char *name;
    int         value;
} ppcp_enum_map;

const char *ppcp_enum_to_text(const ppcp_enum_map *m, int v);

/* The ordinal thermal vocabulary of CORE 5.8, shared by MeasuredCapability,
 * AchievedSummary and `heartbeat_ack`. */
const ppcp_enum_map *ppcp_thermal_enum_map(void);
const ppcp_enum_map *ppcp_role_enum_map(void);
const ppcp_enum_map *ppcp_session_state_enum_map(void);
const ppcp_enum_map *ppcp_session_completeness_enum_map(void);
const ppcp_enum_map *ppcp_capture_completeness_enum_map(void);
const ppcp_enum_map *ppcp_transfer_enum_map(void);
const ppcp_enum_map *ppcp_authority_enum_map(void);

/* CORE 5.10 `epoch` — a wall-clock LABEL, shared by the Session entity and
 * `session_open`. */
ppcp_result ppcp_session_epoch_write(ppcp_cbor_writer *w, const void *ctx);
ppcp_result ppcp_session_epoch_read(ppcp_cbor_reader *r, void *dst, void *ctx);

/* The Peer "head" of MSG 3.3: everything but the three lists, which `declare`
 * carries as top-level keys of its own. */
ppcp_result ppcp_peer_head_encode(ppcp_cbor_writer *w, const struct ppcp_peer_desc *p);
ppcp_result ppcp_peer_head_decode(ppcp_cbor_reader *r, ppcp_arena *a,
                                  struct ppcp_peer_desc *out);
/* CORE 5.2 `product` — informational, and never used to infer behaviour
 * (5.2c, I19).  Shared by the Peer entity, `hello` and `hello_accept`. */
ppcp_result ppcp_product_write(ppcp_cbor_writer *w, const void *ctx);
ppcp_result ppcp_product_read(ppcp_cbor_reader *r, void *dst, void *ctx);

ppcp_result ppcp_peer_timebases_read(ppcp_cbor_reader *r, ppcp_arena *a,
                                     struct ppcp_peer_desc *out);
ppcp_result ppcp_peer_relations_read(ppcp_cbor_reader *r, ppcp_arena *a,
                                     struct ppcp_peer_desc *out);
ppcp_result ppcp_peer_sources_read(ppcp_cbor_reader *r, ppcp_arena *a,
                                   struct ppcp_peer_desc *out);

/* A Capture whose AchievedSummary carries a thermal timeline needs somewhere
 * to put it; ppcp_capture_decode passes NULL and rejects one. */
ppcp_result ppcp_capture_decode_arena(ppcp_cbor_reader *r, ppcp_arena *a,
                                      struct ppcp_capture *out);
/* The one path to `transfer: confirmed` (5.14f, 8.4b): receipt of a
 * `capture_committed` from the receiver, and nothing else. */
ppcp_result ppcp_capture_mark_confirmed(struct ppcp_capture *c);

ppcp_result ppcp_peer_timebases_write(ppcp_cbor_writer *w, const void *ctx);
ppcp_result ppcp_peer_relations_write(ppcp_cbor_writer *w, const void *ctx);
ppcp_result ppcp_peer_sources_write(ppcp_cbor_writer *w, const void *ctx);
ppcp_result ppcp_enum_from_text(const ppcp_enum_map *m, const char *s, size_t len, int *out);

typedef enum ppcp_f_kind {
    PPCP_F_ID = 0,   /* ppcp_id */
    PPCP_F_TEXT,     /* ppcp_text_ref — a string too long or too free for an Id */
    PPCP_F_INT,      /* int64_t */
    PPCP_F_UINT,     /* uint64_t */
    PPCP_F_BOOL,     /* bool */
    PPCP_F_DOUBLE,   /* double */
    PPCP_F_BYTES,    /* ppcp_bytes_ref */
    PPCP_F_ENUM,     /* int, spelled through a ppcp_enum_map */
    PPCP_F_SUB       /* a nested item, written or read by a callback */
} ppcp_f_kind;

/* ------------------------------------------------------------------ writer */

typedef ppcp_result (*ppcp_sub_write)(ppcp_cbor_writer *w, const void *ctx);

typedef struct ppcp_wfield {
    const char    *key;
    ppcp_f_kind    kind;
    const char    *text;
    size_t         text_len;
    const uint8_t *bytes;
    size_t         bytes_len;
    int64_t        i;
    uint64_t       u;
    double         f;
    bool           b;
    ppcp_sub_write sub;
    const void    *ctx;
} ppcp_wfield;

ppcp_wfield ppcp_wf_id(const char *key, const ppcp_id *id);
ppcp_wfield ppcp_wf_text(const char *key, const char *s, size_t len);
ppcp_wfield ppcp_wf_int(const char *key, int64_t v);
ppcp_wfield ppcp_wf_uint(const char *key, uint64_t v);
ppcp_wfield ppcp_wf_bool(const char *key, bool v);
ppcp_wfield ppcp_wf_double(const char *key, double v);
ppcp_wfield ppcp_wf_bytes(const char *key, const uint8_t *p, size_t n);
ppcp_wfield ppcp_wf_enum(const char *key, const ppcp_enum_map *m, int v);
ppcp_wfield ppcp_wf_sub(const char *key, ppcp_sub_write fn, const void *ctx);

/* Writes a CBOR map of `n` pairs with the keys in RFC 8949 §4.2.1 order.
 * A duplicate key is PPCP_ERR_INVALID rather than a map the far end will
 * reject (ENC 4d). */
ppcp_result ppcp_rec_write(ppcp_cbor_writer *w, ppcp_wfield *f, size_t n);

/* The same, for a message BODY inside an envelope.  ENC §5 puts the reserved
 * keys and the body keys in one map, so under deterministic ordering they sort
 * into one sequence — `t1` really does come before `type`.  The envelope
 * writer is told each key before it is written and flushes whichever reserved
 * keys sort ahead of it; this sorts the body's own keys first so the merge is
 * a single pass. */
ppcp_result ppcp_rec_write_body(ppcp_cbor_writer *w, ppcp_envelope_writer *ew,
                                ppcp_wfield *f, size_t n);

/* ------------------------------------------------------------------ reader */

typedef ppcp_result (*ppcp_sub_read)(ppcp_cbor_reader *r, void *dst, void *ctx);

typedef struct ppcp_rfield {
    const char   *key;
    ppcp_f_kind   kind;
    void         *dst;
    bool         *seen;    /* optional; set true when the key was present */
    ppcp_sub_read sub;     /* PPCP_F_SUB */
    void         *ctx;     /* PPCP_F_ENUM: the map.  PPCP_F_SUB: the callback's */
} ppcp_rfield;

ppcp_rfield ppcp_rf(const char *key, ppcp_f_kind kind, void *dst, bool *seen);
ppcp_rfield ppcp_rf_enum(const char *key, const ppcp_enum_map *m, int *dst, bool *seen);
ppcp_rfield ppcp_rf_sub(const char *key, ppcp_sub_read fn, void *dst, void *ctx, bool *seen);

/* Reads one CBOR map, filling the fields it recognises and skipping every
 * other key at every depth (I13, ENC 4b). */
ppcp_result ppcp_rec_read(ppcp_cbor_reader *r, ppcp_rfield *f, size_t n);

/* ------------------------------------------------------------------- lists */

/* Reads a CBOR array of `n` items into caller storage of `cap` elements,
 * calling `fn` per element.  An array longer than `cap` is PPCP_ERR_LIMIT: the
 * decoder never allocates, so the caller's arena is the limit (ENC 3a). */
ppcp_result ppcp_rec_read_array(ppcp_cbor_reader *r, void *base, size_t elem_size,
                                size_t cap, size_t *out_count,
                                ppcp_sub_read fn, void *ctx);

/* The same, into a bump region: the element count is not known until the array
 * head is read, so the caller supplies storage rather than a capacity. */
ppcp_result ppcp_rec_read_array_arena(ppcp_cbor_reader *r, ppcp_arena *a,
                                      size_t elem_size, size_t align,
                                      void **out_base, size_t *out_count,
                                      ppcp_sub_read fn, void *ctx);

/* Writes a CBOR array of `count` items, calling `fn` per element with a
 * pointer to the element. */
typedef ppcp_result (*ppcp_elem_write)(ppcp_cbor_writer *w, const void *elem);
ppcp_result ppcp_rec_write_array(ppcp_cbor_writer *w, const void *base, size_t elem_size,
                                 size_t count, ppcp_elem_write fn);

/* --------------------------------------------------- small shared decoders */

/* Sub-readers with the ppcp_sub_read signature, for use in field tables. */
ppcp_result ppcp_sub_read_instant(ppcp_cbor_reader *r, void *dst, void *ctx);
ppcp_result ppcp_sub_read_interval(ppcp_cbor_reader *r, void *dst, void *ctx);
ppcp_result ppcp_sub_read_estimate(ppcp_cbor_reader *r, void *dst, void *ctx);
ppcp_result ppcp_sub_read_relation(ppcp_cbor_reader *r, void *dst, void *ctx);
ppcp_result ppcp_sub_read_timebase(ppcp_cbor_reader *r, void *dst, void *ctx);

ppcp_result ppcp_sub_write_instant(ppcp_cbor_writer *w, const void *ctx);
ppcp_result ppcp_sub_write_interval(ppcp_cbor_writer *w, const void *ctx);
ppcp_result ppcp_sub_write_estimate(ppcp_cbor_writer *w, const void *ctx);

ppcp_result ppcp_elem_write_relation(ppcp_cbor_writer *w, const void *elem);
ppcp_result ppcp_elem_write_timebase(ppcp_cbor_writer *w, const void *elem);
ppcp_result ppcp_elem_write_interval(ppcp_cbor_writer *w, const void *elem);
ppcp_result ppcp_elem_write_id(ppcp_cbor_writer *w, const void *elem);
ppcp_result ppcp_sub_read_id_elem(ppcp_cbor_reader *r, void *dst, void *ctx);

#endif /* PPCP_CODEC_H */
