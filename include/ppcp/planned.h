/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * planned.h — the public surface that is DECLARED but NOT YET IMPLEMENTED.
 *
 * Why this header exists.  PinPointStudio and PinPointCapture build against
 * `libppcp` one session behind it (plan §7), so they need the names before the
 * bodies.  Everything here is the port surface both applications will bind to
 * (plan A3), with the work package that will implement it named on every block.
 *
 * ⚠ NOTHING IN THIS HEADER LINKS.  These are declarations only; there is no
 * definition in libppcp.a for any of them.  That is deliberate and it is why
 * they are in a separate header from the implemented surface: an application
 * may include ppcp/ppcp.h, compile against these names, and link successfully
 * so long as it does not CALL one.  The moment it calls one the link fails with
 * an undefined symbol naming the function — which is a better diagnostic than a
 * stub returning PPCP_ERR_UNIMPLEMENTED at runtime, because it happens at build
 * time and it names the missing package.
 *
 * A symbol an application needs that is not here is reported to the
 * orchestrator and added to team L's queue.  It is never implemented
 * application-side (plan §7).
 */
#ifndef PPCP_PLANNED_H
#define PPCP_PLANNED_H

#include "ppcp/timing.h"
#include "ppcp/envelope.h"
#include "ppcp/rv.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 * L4 — type vocabulary and validation (CORE §5 entire)
 * ====================================================================== */

typedef struct ppcp_peer_desc      ppcp_peer_desc;      /* L4 — CORE 5.2 Peer */
typedef struct ppcp_source         ppcp_source;         /* L4 — CORE 5.6 Source */
typedef struct ppcp_capture_profile ppcp_capture_profile;/* L4 — CORE 5.7 */
typedef struct ppcp_session        ppcp_session;        /* L4 — CORE 5.10 */
typedef struct ppcp_stream         ppcp_stream;         /* L4 — CORE 5.11 */
typedef struct ppcp_candidate      ppcp_candidate;      /* L4 — CORE 5.12 */
typedef struct ppcp_shot           ppcp_shot;           /* L4 — CORE 5.13 */
typedef struct ppcp_capture        ppcp_capture;        /* L4 — CORE 5.14 */
typedef struct ppcp_readiness      ppcp_readiness;      /* L4 — CORE 5.15 */
typedef struct ppcp_annotation     ppcp_annotation;     /* L4 — CORE 5.18 */

/* L4 — not yet implemented.  Validate a decoded Peer declaration against the
 * structural rules of CORE §5.2; every invariant that is expressible as a
 * constructor is a constructor instead. */
PPCP_API ppcp_result ppcp_peer_desc_validate(const ppcp_peer_desc *p);
/* L4 — not yet implemented.  CORE 5.6/5.7, including I19: every Source
 * declares timing, geometry and intrinsics whichever peer owns it. */
PPCP_API ppcp_result ppcp_source_validate(const ppcp_source *s);
/* L4 — not yet implemented.  CORE 5.7 with I22, I28 and I31. */
PPCP_API ppcp_result ppcp_capture_profile_validate(const ppcp_capture_profile *p);
/* L4 — not yet implemented.  CORE 5.14 Anchor: exactly one key (I27). */
PPCP_API ppcp_result ppcp_capture_validate(const ppcp_capture *c);
/* L4 — not yet implemented.  CORE 5.10e, and `timebase_ref` immutability (I16). */
PPCP_API ppcp_result ppcp_session_validate(const ppcp_session *s);

/* ======================================================================
 * L5 — message catalogue (MSG §3–§11)
 * ====================================================================== */

/* L5 — not yet implemented.  The message's class, channel and the profile that
 * confers origination, from the table the L16 profile-boundary audit reads. */
typedef enum ppcp_msg_class { PPCP_MSG_REQUEST = 0, PPCP_MSG_RESPONSE, PPCP_MSG_EVENT }
    ppcp_msg_class;

typedef struct ppcp_msg_info {
    const char    *type;
    ppcp_msg_class cls;
    uint8_t        channel;
    const char    *originating_profile;   /* NULL where Core confers it */
} ppcp_msg_info;

/* L5 — not yet implemented.  Looks a message type up in the catalogue; an
 * unknown type is PPCP_ERR_NOT_FOUND, never fatal (I13, I24). */
PPCP_API ppcp_result ppcp_msg_lookup(const char *type, size_t len, ppcp_msg_info *out);
/* L5 — not yet implemented.  MSG §10 error codes and the fatal/non-fatal split. */
PPCP_API bool ppcp_msg_error_is_fatal(const char *code, size_t len);

/* ======================================================================
 * L6 — the peer engine (CORE §2.2.2, §7, §10; MSG §3–5)
 * ====================================================================== */

typedef struct ppcp_peer ppcp_peer;   /* L6 — the sans-I/O state machine */

typedef enum ppcp_role { PPCP_ROLE_HOST = 0, PPCP_ROLE_CAPTURE, PPCP_ROLE_OBSERVER }
    ppcp_role;

/* L6 — not yet implemented.  Everything the engine wants from the embedding,
 * supplied as callbacks so that no threshold and no I/O lives in the library.
 *
 * ⚠ `ingest_policy` is a callback and not a number.  I14 forbids a frame-rate,
 * resolution, quality or confidence threshold anywhere in the model, and
 * PinPointStudio's 120 fps floor lives in PinPointStudio (plan H2, CT-I14). */
typedef struct ppcp_peer_config {
    ppcp_role   role;
    ppcp_clock  clock;
    const char *peer_id;
    const char *const *profiles;   /* declared profiles; MUST include "core" */
    size_t      profile_count;

    /* L6 — the embedding's acceptance decision for a counterpart's declaration. */
    bool (*ingest_policy)(void *ctx, const ppcp_peer_desc *counterpart);
    /* L9 — thermal, storage and battery for `heartbeat`. */
    ppcp_result (*health)(void *ctx, ppcp_readiness *out);
    void       *ctx;
} ppcp_peer_config;

/* L6 — not yet implemented.  The engine is constructed into caller-owned
 * storage; ppcp_peer_sizeof() is how much.  Nothing in this library
 * allocates. */
PPCP_API size_t      ppcp_peer_sizeof(void);
PPCP_API ppcp_result ppcp_peer_new(void *storage, size_t storage_len,
                                   const ppcp_peer_config *cfg, ppcp_peer **out);
PPCP_API void        ppcp_peer_free(ppcp_peer *p);

/* L6 — not yet implemented.  Bytes in, on the channel they arrived on; ENC 2c
 * makes the header-matches-stream check this function's job. */
PPCP_API ppcp_result ppcp_peer_feed(ppcp_peer *p, uint8_t channel,
                                    const uint8_t *bytes, size_t len);
/* L6 — not yet implemented.  Bytes out, per channel.  The embedding writes
 * them to whatever it has; the library never holds a socket. */
PPCP_API ppcp_result ppcp_peer_drain(ppcp_peer *p, uint8_t channel,
                                     uint8_t *out, size_t cap, size_t *out_len);

typedef enum ppcp_event_kind {
    PPCP_EVENT_NONE = 0,
    PPCP_EVENT_HELLO,            /* L6 */
    PPCP_EVENT_DECLARE,          /* L6 */
    PPCP_EVENT_SESSION_STATE,    /* L6 */
    PPCP_EVENT_STREAM_OPEN,      /* L6 */
    PPCP_EVENT_STREAM_CLOSE,     /* L6 */
    PPCP_EVENT_READINESS,        /* L6 */
    PPCP_EVENT_CANDIDATE,        /* L10 */
    PPCP_EVENT_SHOT,             /* L10 */
    PPCP_EVENT_CAPTURE,          /* L7 */
    PPCP_EVENT_PAYLOAD,          /* L7 */
    PPCP_EVENT_RELATION_UPDATE,  /* L9 */
    PPCP_EVENT_ANNOTATION,       /* L11 */
    PPCP_EVENT_ERROR             /* L5 */
} ppcp_event_kind;

typedef struct ppcp_event {
    ppcp_event_kind kind;
    const void     *data;      /* the entity for this kind; borrowed, not owned */
    ppcp_result     status;
} ppcp_event;

/* L6 — not yet implemented.  Drains one event; PPCP_ERR_NOT_FOUND when the
 * queue is empty. */
PPCP_API ppcp_result ppcp_peer_next_event(ppcp_peer *p, ppcp_event *out);

/* L6 — not yet implemented.  MSG §3: `declare` is a complete snapshot by
 * generation, never a delta. */
PPCP_API ppcp_result ppcp_peer_declare(ppcp_peer *p, const ppcp_peer_desc *self);
/* L6 — not yet implemented.  CORE §7.2 session lifecycle. */
PPCP_API ppcp_result ppcp_peer_session_open(ppcp_peer *p, const ppcp_session *s);
PPCP_API ppcp_result ppcp_peer_session_close(ppcp_peer *p);
/* L6 — not yet implemented.  CORE §7.3; either peer may open or close a Stream. */
PPCP_API ppcp_result ppcp_peer_stream_open(ppcp_peer *p, const ppcp_stream *s);
PPCP_API ppcp_result ppcp_peer_stream_close(ppcp_peer *p, const char *stream_id);
/* L6 — not yet implemented.  CORE §7.3 arm/disarm and readiness.  A hostless
 * peer records neither (CORE 7.3b). */
PPCP_API ppcp_result ppcp_peer_arm(ppcp_peer *p);
PPCP_API ppcp_result ppcp_peer_disarm(ppcp_peer *p);
PPCP_API ppcp_result ppcp_peer_readiness(ppcp_peer *p, const ppcp_readiness *r);

/* ======================================================================
 * L7 — captures and bulk transfer (CORE §5.11.1–2, §5.14; MSG §8; ENC §6)
 * ====================================================================== */

/* L7 — not yet implemented.  Announce a Capture on control; AchievedFrames
 * travels with the payload and never here (I30). */
PPCP_API ppcp_result ppcp_peer_capture_announce(ppcp_peer *p, const ppcp_capture *c);
/* L7 — not yet implemented.  ENC §6: begin, chunks in ascending index, end,
 * with SHA-256 per chunk and over the whole payload. */
PPCP_API ppcp_result ppcp_peer_payload_begin(ppcp_peer *p, const char *capture_id,
                                             const ppcp_achieved_frames *frames,
                                             size_t total_bytes, size_t chunk_bytes);
PPCP_API ppcp_result ppcp_peer_payload_chunk(ppcp_peer *p, const char *capture_id,
                                             uint32_t index, const uint8_t *data,
                                             size_t len);
PPCP_API ppcp_result ppcp_peer_payload_end(ppcp_peer *p, const char *capture_id);
/* L7 — not yet implemented.  MSG §8: resume from the last acked index. */
PPCP_API ppcp_result ppcp_peer_payload_resume(ppcp_peer *p, const char *capture_id,
                                              uint32_t from_index);
/* L7 — not yet implemented.  CORE 5.14g's four exits, and 5.14g1's refusal of
 * a fifth: a peer's own retention policy does not make shot-anchored payload
 * evictable, and `confirmed` is asserted by the receiver and by nobody else
 * (I38). */
PPCP_API bool ppcp_capture_is_evictable(const ppcp_capture *c);

/* ======================================================================
 * L8 — bundle reader and writer (CORE §9; ENC §7; MSG §9.1–9.2)
 * ====================================================================== */

typedef struct ppcp_bundle_writer ppcp_bundle_writer;   /* L8 */
typedef struct ppcp_bundle_reader ppcp_bundle_reader;   /* L8 */

/* L8 — not yet implemented.  Appends frames in the order they would have been
 * sent, with `session_manifest` before any payload frame (ENC 7c).  The
 * embedding owns the file; the writer produces bytes. */
PPCP_API size_t      ppcp_bundle_writer_sizeof(void);
PPCP_API ppcp_result ppcp_bundle_writer_new(void *storage, size_t storage_len,
                                            ppcp_bundle_writer **out);
PPCP_API ppcp_result ppcp_bundle_writer_append(ppcp_bundle_writer *w, uint8_t channel,
                                               const uint8_t *payload, size_t len,
                                               uint8_t *out, size_t cap, size_t *out_len);
PPCP_API ppcp_result ppcp_bundle_writer_finish(ppcp_bundle_writer *w);

/* L8 — not yet implemented.  Streams a bundle through the same feed path a
 * socket uses; there is no importer (plan A10).  A truncated final frame means
 * `completeness: partial` unless the bundle asserted otherwise, and a partial
 * Session is never upgraded on the strength of what happened to be present
 * (ENC 7d, I10). */
PPCP_API size_t      ppcp_bundle_reader_sizeof(void);
PPCP_API ppcp_result ppcp_bundle_reader_new(void *storage, size_t storage_len,
                                            ppcp_peer *sink, ppcp_bundle_reader **out);
PPCP_API ppcp_result ppcp_bundle_reader_feed(ppcp_bundle_reader *r,
                                             const uint8_t *bytes, size_t len);
PPCP_API ppcp_result ppcp_bundle_reader_finish(ppcp_bundle_reader *r, bool *out_partial);

/* ======================================================================
 * L9 — clock synchronisation and liveness (CORE §5.4.1, §6.3, §7.4, §8.3g)
 * ====================================================================== */

typedef struct ppcp_sync_estimator ppcp_sync_estimator;   /* L9 */

/* L9 — not yet implemented.  One estimator per declared Timebase: I21 and
 * CORE 6.3b run the exchange once per timebase and declare each relation
 * directly.  There is no composition here either. */
PPCP_API size_t      ppcp_sync_estimator_sizeof(void);
PPCP_API ppcp_result ppcp_sync_estimator_new(void *storage, size_t storage_len,
                                             const char *local_tb, const char *remote_tb,
                                             ppcp_sync_estimator **out);
/* L9 — not yet implemented.  The four timestamps of CORE §6.3: t1 send, t2
 * receive, t3 send, t4 receive. */
PPCP_API ppcp_result ppcp_sync_estimator_observe(ppcp_sync_estimator *e,
                                                 int64_t t1, int64_t t2,
                                                 int64_t t3, int64_t t4);
/* L9 — not yet implemented.  Publishes the current estimate as a relation with
 * both sigmas.  Filtered, never stepped (6.3e): a stepped offset mid-session
 * produces a discontinuity in fused output that is very hard to diagnose. */
PPCP_API ppcp_result ppcp_sync_estimator_relation(const ppcp_sync_estimator *e,
                                                  ppcp_timebase_relation *out);

/* ======================================================================
 * L10 — Detect, Mint, Arbitrate (CORE §5.12, §5.13, §5.16, §8)
 * ====================================================================== */

/* L10 — not yet implemented.  Promotion is a callback, not a threshold: I14
 * again, and I32's negative half depends on the peer having a policy of its
 * own to decline with. */
typedef bool (*ppcp_promotion_policy)(void *ctx, const ppcp_candidate *c);
/* L10 — not yet implemented.  Exclude-and-retain on a missing, unrelated or
 * over-uncertain relation; the excluded Candidate stays in `Shot.candidates`
 * (I8). */
typedef bool (*ppcp_arbitration_policy)(void *ctx, const ppcp_candidate *c,
                                        const ppcp_timebase_relation *rel);

/* L10 — not yet implemented.  Emits a Candidate.  `at` is the canonical
 * instant, converted here by the nominator from its raw instant, profile and
 * exposure (I33) — a consumer never applies the conversion a second time. */
PPCP_API ppcp_result ppcp_peer_nominate(ppcp_peer *p, const ppcp_candidate *c);
/* L10 — not yet implemented.  Mint installs the promotion policy; the 8.2i
 * deadline is issue_hold_ns plus one heartbeat interval, and 8.2i1 refuses to
 * mint at all with no affine relation to `timebase_ref`. */
PPCP_API ppcp_result ppcp_peer_set_promotion_policy(ppcp_peer *p,
                                                    ppcp_promotion_policy fn, void *ctx);
/* L10 — not yet implemented.  Arbitrate is available only to the peer with
 * role host (I20). */
PPCP_API ppcp_result ppcp_peer_set_arbitration_policy(ppcp_peer *p,
                                                      ppcp_arbitration_policy fn, void *ctx);
/* L10 — not yet implemented.  Attaches a late Candidate to an issued Shot
 * without moving `t0` (I7), additively and order-independently (5.13d–e). */
PPCP_API ppcp_result ppcp_shot_attach_candidate(ppcp_shot *s, const ppcp_candidate *c);

/* ⚠ There is deliberately NO ppcp_shot_merge() and no ppcp_session_merge().
 * I9: reconciliation creates links; no entity is rewritten or merged.  CT-I9
 * asserts that by API surface rather than by behaviour, which is why the
 * absence is documented here rather than left to be noticed. */

/* ======================================================================
 * L11 — Markup (CORE §5.18; MSG §9.0)
 * ====================================================================== */

/* L11 — not yet implemented.  `body` is opaque, at most 8 KiB, and round-trips
 * byte-identical.  Supersession is by `id`, then `revision`, then bytewise
 * `author_peer_id` — a total order, so both delivery orders converge. */
PPCP_API ppcp_result ppcp_annotation_encode(ppcp_cbor_writer *w, const ppcp_annotation *a);
PPCP_API ppcp_result ppcp_annotation_decode(ppcp_cbor_reader *r, ppcp_annotation *out);
PPCP_API int         ppcp_annotation_supersedes(const ppcp_annotation *a,
                                                const ppcp_annotation *b);

/* ⚠ There is deliberately no path from an Annotation to a Shot, a Candidate, a
 * calibration or any computed quantity.  I37 is asserted by API surface. */

#ifdef __cplusplus
}
#endif
#endif /* PPCP_PLANNED_H */
