/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * peer.h — the sans-I/O peer engine.  Work package L6.
 *
 * CORE §2.2.2, §7 and §10; MSG §3–§5; ENC §2.1.
 *
 * WHAT THIS IS.  One state machine that serves both ends (CORE A.3): the same
 * object is a host, a capture peer or an observer according to its declared
 * role and profiles.  It owns no socket, no thread, no timer and no clock.
 * The embedding pushes bytes in with ppcp_peer_feed() and pulls bytes out with
 * ppcp_peer_drain(), per channel, and reads what happened with
 * ppcp_peer_next_event().
 *
 * ⚠ WHY ppcp_peer_feed() REPORTS WHAT IT CONSUMED.
 *
 * A frame is length-prefixed, and the limits of ENC §8 are 1 MiB on control
 * and 8 MiB on bulk.  An engine that buffered a whole frame internally would
 * carry nine megabytes of storage per link for the benefit of a caller that
 * already has the bytes in a socket buffer.  So the engine buffers NOTHING:
 * it consumes whole frames from the caller's buffer and reports how many bytes
 * it took.  A trailing partial frame is left for the caller to re-present with
 * more bytes after it.  That is also exactly what a bundle reader wants
 * (work package L8), which is what makes "a file is a transport" true rather
 * than merely claimed.
 *
 * WHAT IT REFUSES TO DO.
 *
 *   C1  It parses every message of MSG §11 whatever its profiles.  There is no
 *       profile parameter on the decoder and there is none here either.
 *   C2  It refuses to ORIGINATE a message no declared profile confers.  Every
 *       entry point below runs ppcp_msg_profiles_confer() first and answers
 *       PPCP_ERR_INVALID, so the wire never carries the violation.
 *   C3  A request whose behaviour it does not implement is answered
 *       `error` / `profile_not_supported`, and the transport stays open.
 *   I14 There is no threshold in here.  Whether a counterpart's declaration is
 *       acceptable is `ingest_policy`, a callback the embedding supplies.
 *   I20 At most one host.  A `hello` carrying `role: host` to a peer that is
 *       itself host is answered `error` / `role_conflict`.
 */
#ifndef PPCP_PEER_H
#define PPCP_PEER_H

#include "ppcp/message.h"
#include "ppcp/frame.h"
#include "ppcp/transfer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ====================================================== ENC §2.1 link binding
 *
 * A listener receiving several streams needs two facts the transport does not
 * supply: which streams belong to one peer, and which of them is channel 0.
 * Erratum E1 puts both in an explicit first frame rather than in an implicit
 * rule, because the two implementations that invented implicit rules invented
 * different ones (plan §9, 22 August 2026).
 *
 * The binder is the listener half and is deliberately NOT part of ppcp_peer: a
 * listener meets streams before it knows which peer they belong to, which is
 * the whole problem 2.1 exists to solve.  The dialler half is
 * ppcp_peer_set_link_id() and ppcp_peer_open_channel() below.
 */

#define PPCP_MAX_LINKS 8

typedef struct ppcp_link {
    bool    in_use;
    uint8_t link_id[PPCP_LINK_ID_BYTES];
    /* Bit n set means channel n is bound on this link.  2.1c: a second
     * `link_bind` for a channel this link already holds is refused. */
    uint32_t channels;
} ppcp_link;

typedef struct ppcp_link_binder {
    ppcp_link links[PPCP_MAX_LINKS];
} ppcp_link_binder;

PPCP_API void ppcp_link_binder_init(ppcp_link_binder *b);

/* Offers the FIRST frame of a newly accepted stream, which arrived on
 * `stream_channel` as far as the transport is concerned.
 *
 * ENC 2.1c, the three refusals, each PPCP_ERR_MALFORMED and each meaning the
 * listener closes the stream:
 *   - the first frame is not `link_bind`;
 *   - the `link_bind`'s `channel` disagrees with the frame header's;
 *   - the `link_id` names a link that already holds that channel.
 * A `link_bind` naming an unknown `link_id` opens a new link.
 *
 * PPCP_ERR_TRUNCATED means the frame is not yet whole; call again with more.
 * PPCP_ERR_LIMIT means every link slot is in use.
 *
 * `out_link` receives the link index, which is how the embedding then routes
 * that stream's later bytes to the ppcp_peer it associates with the link. */
PPCP_API ppcp_result ppcp_link_binder_offer(ppcp_link_binder *b, uint8_t stream_channel,
                                            const uint8_t *bytes, size_t len,
                                            size_t *out_consumed, size_t *out_link);

/* 2.1c: a link that has not bound channel 0 within the LISTENER'S OWN timeout
 * is discarded with every stream it holds.  The timeout is the embedding's
 * policy and so is not in here; this is the predicate and this is the
 * discard. */
PPCP_API bool        ppcp_link_binder_is_ready(const ppcp_link_binder *b, size_t link);
PPCP_API ppcp_result ppcp_link_binder_discard(ppcp_link_binder *b, size_t link);
PPCP_API size_t      ppcp_link_binder_count(const ppcp_link_binder *b);
PPCP_API bool        ppcp_link_binder_has_channel(const ppcp_link_binder *b, size_t link,
                                                  uint8_t channel);
PPCP_API const uint8_t *ppcp_link_binder_id(const ppcp_link_binder *b, size_t link);

/* ============================================================== the engine */

typedef struct ppcp_peer ppcp_peer;

typedef enum ppcp_peer_state {
    PPCP_PEER_INIT = 0,     /* nothing sent */
    PPCP_PEER_HELLO_SENT,   /* dialler has sent `hello`, awaiting `hello_accept` */
    PPCP_PEER_CONNECTED,    /* a wire version is agreed */
    PPCP_PEER_DECLARED,     /* this peer has sent `declare` */
    PPCP_PEER_JOINED,       /* a Session is open */
    PPCP_PEER_CLOSED        /* a fatal error was sent or received (MSG 10b) */
} ppcp_peer_state;

/* The embedding's acceptance decision for a counterpart's declaration
 * (MSG 3.4, CORE 7.2b, I14).
 *
 * ⚠ It returns a verdict, not a number, and the library never supplies one.
 * PinPointStudio's 120 fps floor lives in PinPointStudio (plan H2, CT-I14).
 * On rejection the callback writes a machine-readable `reason`, which 3.4a
 * requires and which does NOT close the connection. */
typedef bool (*ppcp_ingest_policy_fn)(void *ctx, const ppcp_peer_desc *counterpart,
                                      ppcp_id *out_reason);

typedef struct ppcp_peer_config {
    ppcp_role   role;              /* 5.2a: fixed for the Session's lifetime */
    const char *peer_id;
    const char *const *profiles;   /* declared profiles; MUST include "core" */
    size_t      profile_count;

    /* MSG 3.1b: most-preferred first, at least one.  NULL defaults to the one
     * version this library speaks. */
    const char *const *versions;
    size_t      version_count;
    /* CORE 10.1e: the responder's support window — the oldest wire version it
     * accepts.  NULL means the last entry of `versions`. */
    const char *min_version;

    /* ENC 2.1a: only a DIALLER mints a `link_id` and sends `link_bind`.  A
     * listener that sent one would be answering a binding with a binding. */
    bool        listener;

    ppcp_ingest_policy_fn ingest_policy;
    void       *ctx;

    /* L9 uses these; L6 stores them and calls neither. */
    ppcp_clock  clock;
    ppcp_result (*health)(void *ctx, ppcp_readiness *out);
} ppcp_peer_config;

/* Constructed into caller-owned storage — nothing in this library allocates.
 * ppcp_peer_sizeof() is how much, and it is large (a few hundred kilobytes)
 * because the outbound queues and the counterpart's declaration live inside
 * it.  It is about 460 KB on a 64-bit target, and two things dominate: the
 * three 64 KiB outbound queues, and the four-deep event ring, each slot of
 * which holds a whole `ppcp_msg` (about 48 KB, most of it the 256-entry
 * `session_manifest` arm of the union).  A caller that wants it smaller
 * drains more often; a caller that wants it on the stack should not. */
PPCP_API size_t      ppcp_peer_sizeof(void);
PPCP_API ppcp_result ppcp_peer_new(void *storage, size_t storage_len,
                                   const ppcp_peer_config *cfg, ppcp_peer **out);
/* Nothing was allocated, so nothing is freed: this zeroes the engine so a
 * use-after-close is a refusal rather than a stale state machine. */
PPCP_API void        ppcp_peer_free(ppcp_peer *p);

/* -------------------------------------------------------------- transport */

/* Bytes in, on the channel they arrived on.
 *
 * Consumes whole frames only and reports how many bytes it took; the caller
 * keeps the tail.  ENC 2c is checked here — the channel byte in every header
 * matches the stream it arrived on — and a mismatch is PPCP_ERR_MALFORMED.
 *
 * A malformed frame is answered with `error`/`malformed` and does NOT close
 * the transport (ENC 5d); a `payload_len` past the channel's limit is fatal
 * (ENC 8a) and returns PPCP_ERR_FATAL_LIMIT with the engine closed. */
PPCP_API ppcp_result ppcp_peer_feed(ppcp_peer *p, uint8_t channel,
                                    const uint8_t *bytes, size_t len,
                                    size_t *out_consumed);

/* Bytes out, per channel.  Copies at most `cap` bytes of whole frames; a frame
 * larger than `cap` is PPCP_ERR_NOSPACE and stays queued. */
PPCP_API ppcp_result ppcp_peer_drain(ppcp_peer *p, uint8_t channel,
                                     uint8_t *out, size_t cap, size_t *out_len);
PPCP_API size_t      ppcp_peer_pending(const ppcp_peer *p, uint8_t channel);

/* ----------------------------------------------------------------- events */

typedef enum ppcp_event_kind {
    PPCP_EVENT_NONE = 0,
    PPCP_EVENT_HELLO,            /* a counterpart introduced itself */
    PPCP_EVENT_CONNECTED,        /* a wire version is agreed */
    PPCP_EVENT_DECLARE,          /* a counterpart declared; `msg` holds the Peer */
    PPCP_EVENT_DECLARE_ACK,
    PPCP_EVENT_SESSION_OPEN,
    PPCP_EVENT_SESSION_JOINED,
    PPCP_EVENT_SESSION_RESUME,
    PPCP_EVENT_SESSION_STATE,
    PPCP_EVENT_SESSION_CLOSE,
    PPCP_EVENT_CONTEXT_CHANGE,
    PPCP_EVENT_STREAM_OPEN,
    PPCP_EVENT_STREAM_OPEN_ACK,
    PPCP_EVENT_STREAM_CLOSE,
    PPCP_EVENT_ARM,              /* 5.2a: answer with ppcp_peer_readiness() */
    PPCP_EVENT_DISARM,
    PPCP_EVENT_READINESS,
    PPCP_EVENT_INTERRUPTION,
    PPCP_EVENT_RELATION_UPDATE,  /* L9 */
    PPCP_EVENT_CALIBRATION_UPDATE,
    PPCP_EVENT_DISCONTINUITY,
    PPCP_EVENT_CANDIDATE,        /* L10 */
    PPCP_EVENT_SHOT,             /* L10 */
    PPCP_EVENT_CAPTURE,          /* L7 */
    PPCP_EVENT_PAYLOAD,          /* L7 */
    PPCP_EVENT_ANNOTATION,       /* L11 */
    PPCP_EVENT_SHOT_LINK,
    PPCP_EVENT_SESSION_LINK,
    PPCP_EVENT_ERROR,            /* an `error` arrived; `status` says if fatal */
    PPCP_EVENT_UNKNOWN           /* MSG 1b: an unknown type, carried not dropped */
} ppcp_event_kind;

#define PPCP_PEER_EVENT_QUEUE 4

typedef struct ppcp_event {
    ppcp_event_kind kind;
    /* The decoded message.  Borrowed: it is valid until PPCP_PEER_EVENT_QUEUE
     * further events have been queued.  A `declare`'s nested Sources point
     * into the engine's declaration arena and are valid until the counterpart
     * declares again — which is what 3.3a's complete-snapshot rule means for
     * storage. */
    const ppcp_msg *msg;
    ppcp_result     status;
} ppcp_event;

/* PPCP_ERR_NOT_FOUND when the queue is empty. */
PPCP_API ppcp_result ppcp_peer_next_event(ppcp_peer *p, ppcp_event *out);

/* ------------------------------------------------------------ origination */

/* ENC 2.1a — the dialler mints 16 bytes from a CSPRNG, fresh per link.  The
 * library has no random source (ground rule 8) so the embedding supplies
 * them, exactly as RV 7.2a does for the pairing nonces. */
PPCP_API ppcp_result ppcp_peer_set_link_id(ppcp_peer *p,
                                           const uint8_t link_id[PPCP_LINK_ID_BYTES]);
/* Queues `link_bind` as the first frame on `channel`.  2.1d: channel 0's must
 * precede its `hello`, and a bulk channel may be opened at any later point in
 * the session with the same `link_id`. */
PPCP_API ppcp_result ppcp_peer_open_channel(ppcp_peer *p, uint8_t channel);

PPCP_API ppcp_result ppcp_peer_hello(ppcp_peer *p);
/* MSG 3.3a: a complete snapshot.  `generation` increments here, so a caller
 * cannot send two snapshots with one number. */
PPCP_API ppcp_result ppcp_peer_declare(ppcp_peer *p, const ppcp_peer_desc *self);

/* CORE 4.1d / 5.10e: the two arbitration parameters travel if and only if the
 * Session has a host, which ppcp_session_make_hosted/_hostless already made
 * structural.  A peer that is not `role: host` may still originate the
 * hostless form — CORE 4.1b requires it to record one in its bundle. */
PPCP_API ppcp_result ppcp_peer_session_open(ppcp_peer *p, const ppcp_session *s);
PPCP_API ppcp_result ppcp_peer_session_state(ppcp_peer *p, ppcp_session_state state,
                                             ppcp_completeness completeness);
PPCP_API ppcp_result ppcp_peer_session_close(ppcp_peer *p, const char *reason);
PPCP_API ppcp_result ppcp_peer_context_change(ppcp_peer *p, const ppcp_context_change *c);

PPCP_API ppcp_result ppcp_peer_stream_open(ppcp_peer *p, const ppcp_stream *s);
/* 5.1d: EITHER peer may close a Stream — the owner because it can no longer
 * produce, the consumer because it no longer wants the data. */
PPCP_API ppcp_result ppcp_peer_stream_close(ppcp_peer *p, const char *stream_id,
                                            const ppcp_instant *closed_at,
                                            const char *reason);

/* MSG 5.2: an EMPTY list means every open capture Stream.  CORE 7.3a makes
 * arming host-controlled, so a peer that is not `role: host` is refused here
 * as well as by C2. */
PPCP_API ppcp_result ppcp_peer_arm(ppcp_peer *p, const ppcp_id *stream_ids, size_t count);
PPCP_API ppcp_result ppcp_peer_disarm(ppcp_peer *p, const ppcp_id *stream_ids, size_t count);
/* 5.2a / 7.3c: emitted in response to `arm` and whenever `settled` changes.
 * Conferred by Capture, not Live, so a hostless peer records it. */
PPCP_API ppcp_result ppcp_peer_readiness(ppcp_peer *p, const ppcp_readiness *r,
                                         const ppcp_id *stream_ids, size_t count);
PPCP_API ppcp_result ppcp_peer_interruption(ppcp_peer *p, const char *kind,
                                            const ppcp_interval *interval, bool recovered,
                                            const ppcp_id *stream_ids, size_t count);

/* ---------------------------------------- MSG §8 — Captures and bulk transfer
 *
 * The engine keeps the owner's transfer table (5.14f) behind these, so
 * `confirmed` arrives through `capture_committed` and through nothing else.
 * `ppcp_capture` has no `achieved_frames` field at all, which is I30 made
 * structural: the per-frame series cannot ride on `capture_announce` because
 * there is nowhere to put them. */
PPCP_API ppcp_result ppcp_peer_capture_announce(ppcp_peer *p, const ppcp_capture *c,
                                                bool is_preview,
                                                const char *thumbnail_format,
                                                const uint8_t *thumbnail,
                                                size_t thumbnail_len);
PPCP_API ppcp_result ppcp_peer_capture_update(ppcp_peer *p,
                                              const ppcp_body_capture_update *u);
/* 8.4a — sent by the RECEIVER, when it holds the payload durably.  A peer
 * calling this about its own Capture is telling itself nothing; the engine
 * does not stop that, because "receiver" is a role in one transfer and not a
 * property of a peer.  What it does stop is an owner setting `confirmed`
 * without one arriving (8.4b, ppcp_transfer_set). */
PPCP_API ppcp_result ppcp_peer_capture_committed(ppcp_peer *p, const char *capture_id,
                                                 const ppcp_digest *digest);

/* ENC §6 — `chunk_bytes` is passed to every chunk call rather than remembered,
 * so `offset` (6b) and the chunk digest (6c) are computed here from the index
 * and cannot disagree with what the sender believes. */
PPCP_API ppcp_result ppcp_peer_payload_begin(ppcp_peer *p, uint8_t channel,
                                             const char *capture_id, uint64_t bytes,
                                             const ppcp_digest *digest,
                                             uint32_t chunk_bytes,
                                             const ppcp_achieved_frames *frames);
PPCP_API ppcp_result ppcp_peer_payload_chunk(ppcp_peer *p, uint8_t channel,
                                             const char *capture_id, uint32_t index,
                                             uint32_t chunk_bytes,
                                             const uint8_t *data, size_t len);
PPCP_API ppcp_result ppcp_peer_payload_ack(ppcp_peer *p, uint8_t channel,
                                           const char *capture_id, uint32_t index);
PPCP_API ppcp_result ppcp_peer_payload_end(ppcp_peer *p, uint8_t channel,
                                           const char *capture_id,
                                           const ppcp_digest *digest);
PPCP_API ppcp_result ppcp_peer_payload_abort(ppcp_peer *p, uint8_t channel,
                                             const char *capture_id, const char *reason);
PPCP_API ppcp_result ppcp_peer_payload_resume(ppcp_peer *p, uint8_t channel,
                                              const char *capture_id, uint32_t from_index);

/* The owner's view of every Capture this peer announced or was told about.
 * Read-only from outside: the transitions are the functions above and the
 * messages that arrive. */
PPCP_API const ppcp_transfer_table *ppcp_peer_transfers(const ppcp_peer *p);

/* The general form, and what every function above is built on: queue one
 * message on one channel.  Still C2-checked and still channel-checked, so it
 * is an escape hatch for the messages later work packages add, not a way past
 * the rules.  `msg_id` is assigned here (ENC 5c). */
PPCP_API ppcp_result ppcp_peer_send(ppcp_peer *p, uint8_t channel, ppcp_msg *m);

/* MSG §10 — answer an `error`.  `code` is one of the PPCP_ERRCODE_* spellings;
 * a fatal code closes the engine after the frame is queued (10b). */
PPCP_API ppcp_result ppcp_peer_error(ppcp_peer *p, uint8_t channel, const char *code,
                                     const char *message, bool has_in_reply_to,
                                     uint64_t in_reply_to);

/* ------------------------------------------------------------- accessors */

PPCP_API ppcp_peer_state ppcp_peer_get_state(const ppcp_peer *p);
/* The agreed wire version, or NULL before `hello_accept`. */
PPCP_API const char *ppcp_peer_version(const ppcp_peer *p);
/* The counterpart's declaration, or NULL before it declared.  Valid until it
 * declares again (3.3a). */
PPCP_API const ppcp_peer_desc *ppcp_peer_counterpart(const ppcp_peer *p);
PPCP_API bool   ppcp_peer_declares(const ppcp_peer *p, const char *profile);
PPCP_API bool   ppcp_peer_is_armed(const ppcp_peer *p);
PPCP_API size_t ppcp_peer_stream_count(const ppcp_peer *p);
PPCP_API const ppcp_stream *ppcp_peer_stream_at(const ppcp_peer *p, size_t index);
PPCP_API const ppcp_stream *ppcp_peer_stream_find(const ppcp_peer *p, const char *stream_id);
/* The Session this peer joined, or NULL.  I16: `timebase_ref` is immutable and
 * there is no setter — a second `session_open` naming a different one is
 * refused on receipt. */
PPCP_API const ppcp_id *ppcp_peer_session_id(const ppcp_peer *p);
PPCP_API const ppcp_id *ppcp_peer_timebase_ref(const ppcp_peer *p);

/* CORE §10.1 version selection, exported because it is the one piece of
 * §10.1c that is arithmetic rather than state: the highest MINOR the responder
 * supports within the highest MAJOR common to both.  Returns PPCP_ERR_NOT_FOUND
 * where there is no common MAJOR, which is what `unsupported_version` answers. */
PPCP_API ppcp_result ppcp_version_select(const ppcp_id *offered, size_t offered_count,
                                         const ppcp_id *supported, size_t supported_count,
                                         ppcp_id *out);
/* CORE 10.1e: is `version` inside the window [min_version, ..]? */
PPCP_API bool ppcp_version_in_window(const ppcp_id *version, const ppcp_id *min_version);

#ifdef __cplusplus
}
#endif
#endif /* PPCP_PEER_H */
