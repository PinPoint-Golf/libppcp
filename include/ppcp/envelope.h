/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * envelope.h — the message envelope of PPCP-ENC §5.
 *
 * Every frame payload is one CBOR map carrying `type`, `msg_id`, optionally
 * `reply_to` and `session_id`, with the message's own fields flat alongside
 * them (ENC 5a forbids a body reusing those four names for anything else).
 *
 * ⚠ On reproducing the worked example of ENC §5.1.
 *
 * The example's key order is `type`, `msg_id`, `probe_seq`, `timebase_id`,
 * `t1`.  That is NOT RFC 8949 §4.2.1 deterministic order, which would put `t1`
 * first: encoded, "t1" is 62 74 31 and "type" is 64 74 79 70 65, so 0x62 sorts
 * before 0x64.  ENC 4e makes deterministic encoding a SHOULD rather than a
 * MUST, so the example is a legal encoding — but an encoder that honours 4e
 * cannot produce it.  That is why the envelope writer has two modes, and it is
 * reported as a specification defect in docs/conformance/claim-libppcp.md.
 *
 * The deterministic mode is why body fields are written through
 * ppcp_envelope_before(): the reserved keys and the body keys sort into one
 * sequence, and `t1` genuinely does come before `type`.  A body encoder
 * announces each key it is about to write and the envelope flushes whichever
 * reserved keys sort ahead of it.
 */
#ifndef PPCP_ENVELOPE_H
#define PPCP_ENVELOPE_H

#include "ppcp/cbor.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PPCP_TYPE_MAX 64

typedef struct ppcp_envelope {
    char     type[PPCP_TYPE_MAX + 1];   /* ENC §5: message type from MSG §11 */
    uint8_t  type_len;
    uint64_t msg_id;                    /* ENC 5c: per sender, per connection, from 1 */
    bool     has_reply_to;
    uint64_t reply_to;                  /* ENC 5b: every response carries one */
    bool     has_session_id;
    ppcp_id  session_id;                /* absent before the session exists */
} ppcp_envelope;

/* ENC 5c — `msg_id` is per sender.  Two peers may use the same value
 * concurrently and `reply_to` is interpreted against the recipient's own
 * outgoing sequence, so a peer keeps exactly one of these per connection and
 * never inspects the counterpart's. */
typedef struct ppcp_msg_seq {
    uint64_t next;
} ppcp_msg_seq;

PPCP_API void     ppcp_msg_seq_init(ppcp_msg_seq *s);   /* ENC §5: starts at 1 */
PPCP_API uint64_t ppcp_msg_seq_next(ppcp_msg_seq *s);

PPCP_API ppcp_result ppcp_envelope_init(ppcp_envelope *e, const char *type, uint64_t msg_id);
PPCP_API ppcp_result ppcp_envelope_set_reply_to(ppcp_envelope *e, uint64_t reply_to);
PPCP_API ppcp_result ppcp_envelope_set_session_id(ppcp_envelope *e, const char *sid, size_t len);
PPCP_API ppcp_result ppcp_envelope_validate(const ppcp_envelope *e);

/* ENC 5a: a body field may not be named `type`, `msg_id`, `reply_to` or
 * `session_id`.  ppcp_envelope_before() refuses one, so a collision is a
 * failed encode rather than a message that decodes into the wrong fields. */
PPCP_API bool ppcp_envelope_is_reserved_key(const char *key, size_t len);

/* The merge cursor.  Opaque in practice; declared here so it can live on the
 * caller's stack, since nothing in this library allocates. */
typedef struct ppcp_envelope_writer {
    const ppcp_envelope *e;
    unsigned             next;     /* index of the next reserved key to emit */
    bool                 literal;  /* reserved keys were all emitted up front */
} ppcp_envelope_writer;

/* Opens the payload map for `body_fields` further key/value pairs.
 *
 * In PPCP_CBOR_ORDER_LITERAL this writes the reserved keys immediately, in the
 * order ENC §5 tabulates them, which is the order of the §5.1 example.  In
 * PPCP_CBOR_ORDER_DETERMINISTIC it writes only the map head and leaves the
 * reserved keys to ppcp_envelope_before()/_close(). */
PPCP_API ppcp_result ppcp_envelope_open(ppcp_cbor_writer *w, ppcp_envelope_writer *ew,
                                        const ppcp_envelope *e, size_t body_fields);

/* Call immediately before writing each body key.  Emits every reserved key
 * that sorts ahead of it. */
PPCP_API ppcp_result ppcp_envelope_before(ppcp_cbor_writer *w, ppcp_envelope_writer *ew,
                                          const char *key, size_t key_len);

/* Emits whatever reserved keys the body did not overtake. */
PPCP_API ppcp_result ppcp_envelope_close(ppcp_cbor_writer *w, ppcp_envelope_writer *ew);

/* Decodes the reserved keys.  Unknown keys are skipped at every depth (I13);
 * `out_pairs` reports the map's total key/value pair count so a body decoder
 * can walk the same map itself. */
PPCP_API ppcp_result ppcp_envelope_decode(const uint8_t *payload, size_t len,
                                          ppcp_cbor_limits lim,
                                          ppcp_envelope *out, uint32_t *out_pairs);

/* ENC 5d — a receiver that cannot decode a payload answers `error`/`malformed`
 * with `reply_to` where it could recover `msg_id`, and without it otherwise.
 * This recovers `msg_id` from a payload that failed to decode, on a best-effort
 * basis, so a peer can answer specifically rather than generically. */
PPCP_API ppcp_result ppcp_envelope_recover_msg_id(const uint8_t *payload, size_t len,
                                                  ppcp_cbor_limits lim, uint64_t *out_msg_id);

typedef ppcp_result (*ppcp_body_writer)(ppcp_cbor_writer *w, ppcp_envelope_writer *ew,
                                        void *ctx);

/* A whole frame: header, envelope and body.  `body_fields` is the number of
 * key/value pairs `body` will write; writing a different number fails the
 * encode rather than emitting a truncated map. */
PPCP_API ppcp_result ppcp_message_encode(uint8_t *out, size_t cap, uint8_t channel,
                                         const ppcp_envelope *e, size_t body_fields,
                                         ppcp_body_writer body, void *ctx,
                                         size_t *out_written);

/* As above but with PPCP_CBOR_ORDER_LITERAL.  See the warning at the top of
 * this header.  Never for an RV payload — RV 4.3a is a MUST. */
PPCP_API ppcp_result ppcp_message_encode_literal(uint8_t *out, size_t cap, uint8_t channel,
                                                 const ppcp_envelope *e, size_t body_fields,
                                                 ppcp_body_writer body, void *ctx,
                                                 size_t *out_written);

#ifdef __cplusplus
}
#endif
#endif /* PPCP_ENVELOPE_H */
