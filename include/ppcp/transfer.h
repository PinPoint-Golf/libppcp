/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * transfer.h — Captures, bulk transfer and the eviction rule.  Work package L7.
 *
 * CORE §5.11.1–2, §5.14; MSG §8; ENC §6.
 *
 * THREE THINGS LIVE HERE.
 *
 * 1. The **payload codec** — chunking, offsets, and the SHA-256 of ENC 6c: one
 *    digest per chunk over `data` alone, and one over the concatenation of
 *    every chunk in index order.  A sender and a receiver, both of which the
 *    embedding drives with bytes it already holds.
 *
 * 2. The **transfer table** — the OWNER'S view of where each Capture's payload
 *    has got to (5.14f).  It exists chiefly to make one thing impossible:
 *    there is no function anywhere that sets `confirmed`.  8.4b says only the
 *    receiver can say it, so the only route is ppcp_transfer_on_committed(),
 *    which needs a `capture_committed` to call.
 *
 * 3. The **eviction predicate** — 5.14g's four exits, and 5.14g1's refusal of
 *    a fifth.  Every exit is a case where THE PROTOCOL says no receiver will
 *    ever confirm the payload.  A peer's own retention policy is not such a
 *    case and cannot be made into one here.
 *
 * And one thing deliberately does not live here: the window length of a
 * continuous Capture, which 5.11e says is the producing peer's alone and
 * appears nowhere in the specification (I14).
 */
#ifndef PPCP_TRANSFER_H
#define PPCP_TRANSFER_H

#include "ppcp/message.h"
#include "ppcp/hash.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ====================================================== ENC §6 — the payload */

/* ENC 6c / 6e — SHA-256 over the PAYLOAD BYTES, never over the CBOR encoding
 * of the enclosing message, so a re-encode does not change a Capture's
 * identity. */
PPCP_API ppcp_result ppcp_payload_digest(const uint8_t *data, size_t len,
                                         ppcp_digest *out);

/* The number of chunks a payload of `bytes` splits into.  Zero bytes is zero
 * chunks: a Capture with no payload does not transfer one. */
PPCP_API ppcp_result ppcp_payload_chunk_count(uint64_t bytes, uint32_t chunk_bytes,
                                              uint32_t *out_count);

/* One chunk, by index.  ENC 6b: `offset` is `index x chunk_bytes` for EVERY
 * chunk, and `data` is exactly `chunk_bytes` long except for the last. */
PPCP_API ppcp_result ppcp_payload_chunk_at(const uint8_t *data, size_t len,
                                           uint32_t chunk_bytes, uint32_t index,
                                           const uint8_t **out_data, size_t *out_len,
                                           uint64_t *out_offset, ppcp_digest *out_digest);

/* The receiving half.  It streams: a receiver never holds the whole payload to
 * hash it, which is the point of chunking in the first place. */
typedef struct ppcp_payload_receiver {
    ppcp_id     capture_id;
    uint64_t    bytes;          /* announced total */
    uint64_t    received;
    uint32_t    chunk_bytes;
    uint32_t    next_index;     /* 8.3a: ascending, one bulk channel */
    bool        has_acked;
    uint32_t    acked_index;    /* 8.3d: resumption is from the one AFTER this */
    ppcp_digest expect;         /* 8.1e: known by payload_begin */
    ppcp_sha256 running;
    bool        done;
} ppcp_payload_receiver;

PPCP_API ppcp_result ppcp_payload_receiver_begin(ppcp_payload_receiver *r,
                                                 const ppcp_body_payload_begin *b);
/* ENC 6d — the chunk digest is verified ON ARRIVAL.  A mismatch is
 * PPCP_ERR_MALFORMED and the caller answers `payload_abort` / `malformed`;
 * the transfer may then be retried from `payload_resume`. */
PPCP_API ppcp_result ppcp_payload_receiver_chunk(ppcp_payload_receiver *r,
                                                 const ppcp_body_payload_chunk *c);
/* ENC 6d — and the whole-payload digest at `payload_end`.  8.3b: it MUST equal
 * the one `payload_begin` announced, so this checks both. */
PPCP_API ppcp_result ppcp_payload_receiver_end(ppcp_payload_receiver *r,
                                               const ppcp_body_payload_end *e);
/* 8.3d — the index a `payload_resume` should name: the one AFTER the last
 * acknowledged, or 0 if none was acknowledged. */
PPCP_API uint32_t ppcp_payload_receiver_resume_index(const ppcp_payload_receiver *r);

/* =============================================== CORE 5.14 — the owner's view */

#define PPCP_TRANSFER_MAX 128

/* Why the entry is not just a `ppcp_capture`: three of 5.14g's four exits are
 * facts about the CONVERSATION, not about the Capture.  `already_present` came
 * back from a receiver; `shed` was permitted by a clause; `confirmed` was
 * asserted by somebody else.  A predicate over the entity alone can answer
 * only two of the four, which is exactly how I38 was got wrong the first
 * time. */
typedef struct ppcp_transfer_entry {
    ppcp_id             capture_id;
    ppcp_id             stream_id;
    ppcp_anchor         anchor;
    ppcp_completeness   completeness;
    ppcp_transfer_state transfer;
    ppcp_digest         digest;
    bool                has_bytes;
    uint64_t            bytes;
    bool                has_acked_index;
    uint32_t            acked_index;
    /* 5.14g exit 3 — the receiver answered `payload_abort`/`already_present`,
     * which for this purpose is equivalent to a commit. */
    bool                already_present;
    /* 5.14g exit 4 — the PROTOCOL permitted the owner to shed it: 5.11j for a
     * preview segment, 5.12.1b for candidate evidence. */
    bool                shed_permitted;
    /* 5.11j: a preview Capture is live-only.  Recorded so 8.1i can be refused
     * on the way in rather than noticed on the way out. */
    bool                preview;
} ppcp_transfer_entry;

typedef struct ppcp_transfer_table {
    ppcp_transfer_entry entries[PPCP_TRANSFER_MAX];
    size_t              count;
} ppcp_transfer_table;

PPCP_API void ppcp_transfer_table_init(ppcp_transfer_table *t);
PPCP_API size_t ppcp_transfer_table_count(const ppcp_transfer_table *t);
PPCP_API const ppcp_transfer_entry *ppcp_transfer_find(const ppcp_transfer_table *t,
                                                       const ppcp_id *capture_id);

/* Records an announced Capture.  `is_preview` comes from the Stream's `kind`,
 * which the caller knows and a Capture does not carry.
 *
 * MSG 8.1i / CORE 5.11j: a preview Capture announced `transfer: pending` is
 * refused here, because preview is live-only — what could not be delivered
 * promptly is discarded and announced `absent` with `not_retained`, and a
 * queue told nothing else fills with the cheapest data in the session. */
PPCP_API ppcp_result ppcp_transfer_observe_announce(ppcp_transfer_table *t,
                                                    const ppcp_capture *c,
                                                    bool is_preview);

/* The owner's own transitions.  `confirmed` is NOT reachable from here:
 * ppcp_transfer_set() refuses it (8.4b). */
PPCP_API ppcp_result ppcp_transfer_set(ppcp_transfer_table *t, const ppcp_id *capture_id,
                                       ppcp_transfer_state state);
PPCP_API ppcp_result ppcp_transfer_on_acked(ppcp_transfer_table *t,
                                            const ppcp_id *capture_id, uint32_t index);

/* 8.4a / 5.14f — the ONLY route to `confirmed`, and it needs a message from
 * the receiver to travel.  The digest is checked as content: a
 * `capture_committed` naming a payload this owner does not recognise confirms
 * nothing. */
PPCP_API ppcp_result ppcp_transfer_on_committed(ppcp_transfer_table *t,
                                                const ppcp_body_capture_committed *m);
/* 8.3c / 5.14g exit 3. */
PPCP_API ppcp_result ppcp_transfer_on_already_present(ppcp_transfer_table *t,
                                                      const ppcp_id *capture_id);
/* 5.14g exit 4.  Refused for a shot-anchored Capture: 5.14g1 says shot payload
 * is never sheddable by policy, and this is the function a policy would have
 * called. */
PPCP_API ppcp_result ppcp_transfer_mark_shed(ppcp_transfer_table *t,
                                             const ppcp_id *capture_id);

/* I38 / 5.14g — the four exits, and nothing else.
 *
 * ⚠ ppcp_capture_is_evictable() answers over the ENTITY and therefore knows
 * only exits 1 and 2.  It is here because a consumer holding a decoded Capture
 * and nothing else still has a question to ask; it is NOT the predicate an
 * owner uses before deleting a file.  That one is
 * ppcp_transfer_is_evictable(), which can see all four. */
PPCP_API bool ppcp_capture_is_evictable(const ppcp_capture *c);
PPCP_API bool ppcp_transfer_is_evictable(const ppcp_transfer_table *t,
                                         const ppcp_id *capture_id);

/* ======================================= I36 — stream-anchored coverage
 *
 * "The announced segments and their gaps account for the whole interval from
 * `opened_at` onward, with no overlap.  Time accounted for by neither is a
 * DEFECT, not a dropout" (5.11c).
 *
 * Accounting is over Captures that have been ANNOUNCED, not over payload that
 * has arrived (5.11d): `completeness` and `transfer` are independent axes, and
 * a consumer that waited for bytes before accounting would report a defect
 * every time a link was slow. */
#define PPCP_COVERAGE_MAX 128

typedef struct ppcp_coverage {
    ppcp_id       stream_id;
    ppcp_id       tb;
    int64_t       opened_ns;
    bool          has_closed;
    int64_t       closed_ns;
    ppcp_interval segments[PPCP_COVERAGE_MAX];
    size_t        segment_count;
} ppcp_coverage;

PPCP_API ppcp_result ppcp_coverage_init(ppcp_coverage *cov, const ppcp_stream *s);
PPCP_API ppcp_result ppcp_coverage_close(ppcp_coverage *cov, int64_t closed_ns);
/* Adds one stream-anchored Capture.  5.14e: a segment that OVERLAPS another on
 * the same Stream is refused — two accounts of one interval are two answers to
 * one question.  An `absent` segment carrying its interval and reason is added
 * like any other and SATISFIES coverage; that is CT-I36 case (b). */
PPCP_API ppcp_result ppcp_coverage_add(ppcp_coverage *cov, const ppcp_capture *c);

/* Checks coverage up to `up_to_ns`.
 *
 * 5.11c1 draws the line that matters:
 *   - a hole BETWEEN announced segments is a defect in EITHER kind of Session;
 *   - a hole AFTER the last announced segment is the incompleteness a
 *     `partial` or `unknown` Session already declares, and a defect only in a
 *     Session asserted `complete`.
 *
 * Returns PPCP_OK when accounted, PPCP_ERR_MALFORMED with `out_hole` set to
 * the first unaccounted span otherwise. */
PPCP_API ppcp_result ppcp_coverage_check(const ppcp_coverage *cov, int64_t up_to_ns,
                                         ppcp_completeness session_completeness,
                                         ppcp_interval *out_hole);

#ifdef __cplusplus
}
#endif
#endif /* PPCP_TRANSFER_H */
