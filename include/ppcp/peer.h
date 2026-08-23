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
#include "ppcp/sync.h"

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

/* Offers the FIRST frame of a newly accepted stream.
 *
 * ⚠ THE CHANNEL COMES FROM THE FRAME HEADER, not from a parameter.  A
 * stream-per-connection listener — a TCP accept, an NWConnection — meets a
 * freshly accepted stream carrying no channel number of its own; the channel
 * is in the `link_bind` frame's header and in its body, and 2.1c's job is to
 * check those two against each other.  Asking the listener for a third copy it
 * does not have was the L6 signature and it was unusable on both platforms
 * (plan §9, D, 22 August 2026).  `out_channel` reports what the header said,
 * and that is the value the embedding then passes to ppcp_peer_feed().
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
 * that stream's later bytes to the ppcp_peer it associates with the link.
 * `out_channel` may be NULL if the caller does not want it, but a listener
 * that does not want it has nothing to feed. */
PPCP_API ppcp_result ppcp_link_binder_offer(ppcp_link_binder *b,
                                            const uint8_t *bytes, size_t len,
                                            size_t *out_consumed, size_t *out_link,
                                            uint8_t *out_channel);

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

    /* The embedding's clock.  L9 reads it for `sync_probe`/`sync_reply` and
     * for nothing else; every other timestamp in this engine came off a wire
     * or out of a caller's hand. */
    ppcp_clock  clock;
    /* 5.2a / 7.3c — the readiness this peer reports when it is armed.  A
     * measurement, not a state name (5.15a). */
    ppcp_result (*health)(void *ctx, ppcp_readiness *out);

    /* MSG 5.4 / CORE 7.4b — what `heartbeat_ack` carries.  A callback because
     * the library has no thermometer, no battery and no filesystem, and
     * because reporting degradation rather than silently accepting worse data
     * is the only reason the message exists. */
    ppcp_health_fn health_report;

    /* 6.1b — the timebase this peer stamps `t2`/`t3` on when it ANSWERS a
     * `sync_probe`: whichever clock its network stack timestamps with.  It
     * MUST be one of the peer's declared timebases.  NULL means this peer does
     * not answer probes, and one arriving is answered
     * `error`/`profile_not_supported` rather than with a fabricated instant. */
    const char *sync_timebase;
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
 * (ENC 8a) and returns PPCP_ERR_FATAL_LIMIT with the engine closed.
 *
 * ⚠ FEED STOPS WHEN THE EVENT QUEUE IS FULL (F-L13-1, plan §9, 23 Aug 2026).
 *
 * The event queue is PPCP_PEER_EVENT_QUEUE deep and a frame may raise two
 * events (a `hello` raises PPCP_EVENT_HELLO and PPCP_EVENT_CONNECTED).  Before
 * S4 this function consumed every whole frame the caller offered and the ring
 * dropped the OLDEST event to make room — so one socket read carrying a
 * replayed bundle silently lost `capture_announce`.  It no longer does: the
 * loop stops before a frame it cannot report, `*out_consumed` says how far it
 * got, and the caller drains with ppcp_peer_next_event() and re-presents the
 * remainder.  The return is still PPCP_OK — running out of event room is not
 * an error, it is backpressure — so the loop an embedding wants is:
 *
 *     size_t off = 0, took = 0;
 *     while (off < len) {
 *         if (ppcp_peer_feed(p, ch, bytes + off, len - off, &took) != PPCP_OK)
 *             break;
 *         off += took;
 *         drain_every_event(p);                  // ppcp_peer_next_event()
 *         if (took == 0 && !ppcp_peer_feed_stalled(p))
 *             break;                             // partial frame: need bytes
 *     }
 *
 * ppcp_peer_feed_stalled() is what tells "no room for events" apart from "not
 * a whole frame yet", which are the only two reasons a feed returns short. */
PPCP_API ppcp_result ppcp_peer_feed(ppcp_peer *p, uint8_t channel,
                                    const uint8_t *bytes, size_t len,
                                    size_t *out_consumed);

/* Bytes out, per channel.  Copies at most `cap` bytes of whole frames; a frame
 * larger than `cap` is PPCP_ERR_NOSPACE and stays queued. */
PPCP_API ppcp_result ppcp_peer_drain(ppcp_peer *p, uint8_t channel,
                                     uint8_t *out, size_t cap, size_t *out_len);
PPCP_API size_t      ppcp_peer_pending(const ppcp_peer *p, uint8_t channel);

/* ------------------------------------------------- partial writes (L9 queue)
 *
 * ⚠ ppcp_peer_drain() DEQUEUES.  It hands back whole frames and assumes the
 * embedding wrote all of them, which is true of a file and false of a socket:
 * under CORE T2 backpressure `write()` returns short and the bytes it did not
 * take are bytes the engine has already forgotten (plan §9, H, 22 August 2026).
 *
 * So there is a second path, and it is the one a socket wants:
 *
 *     const uint8_t *b; size_t n;
 *     ppcp_peer_drain_peek(p, ch, &b, &n);
 *     ssize_t wrote = send(fd, b, n, 0);
 *     if (wrote > 0) ppcp_peer_drain_commit(p, ch, (size_t)wrote);
 *
 * `commit` removes EXACTLY the byte count it is given — not a whole number of
 * frames.  A channel is an ordered byte stream and half a frame on the wire is
 * half a frame the peer will reassemble; rounding down to a frame boundary
 * would re-send bytes that had already left.
 *
 * The two paths do not mix.  Once a partial commit has left the head frame
 * half-written, ppcp_peer_drain() returns PPCP_ERR_INVALID rather than handing
 * out a fragment it describes as a frame; peek and commit continue from where
 * they were. */
PPCP_API ppcp_result ppcp_peer_drain_peek(const ppcp_peer *p, uint8_t channel,
                                          const uint8_t **out, size_t *out_len);
PPCP_API ppcp_result ppcp_peer_drain_commit(ppcp_peer *p, uint8_t channel, size_t written);
/* True when the head of this channel's queue is mid-frame, which is the state
 * a short write leaves behind and the state ppcp_peer_drain() refuses. */
PPCP_API bool        ppcp_peer_drain_is_partial(const ppcp_peer *p, uint8_t channel);

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
    PPCP_EVENT_UNKNOWN,          /* MSG 1b: an unknown type, carried not dropped */

    /* L9 onward.  Appended rather than interleaved so the numbering the two
     * applications already compiled against does not move. */
    PPCP_EVENT_HEARTBEAT,        /* L9 — `heartbeat` or `heartbeat_ack` */
    PPCP_EVENT_SYNC,             /* L9 — `sync_probe`, `sync_reply`, `sync_residual` */
    PPCP_EVENT_LINK_LOST,        /* L9 — 7.4c: three missed intervals; 8.3g follows */
    PPCP_EVENT_LINK_RESTORED,    /* L9 — a heartbeat arrived after a loss */
    PPCP_EVENT_CAPTURE_REQUEST,  /* L10 — 8.4: answer it, never with an error */
    PPCP_EVENT_SESSION_OFFER,    /* L9 queue — a device offers a stored Session */
    PPCP_EVENT_SESSION_ACCEPT,
    PPCP_EVENT_SESSION_MANIFEST
} ppcp_event_kind;

#define PPCP_PEER_EVENT_QUEUE 4

typedef struct ppcp_event {
    ppcp_event_kind kind;
    /* The channel the frame arrived on.  CORE 5.11h puts preview payload on a
     * bulk channel DISTINCT from shot payload, and without this a receiver
     * cannot check that it did (F-H4-1/2, PinPointStudio H4).  Zero for an
     * event the engine raised itself rather than decoded — link loss, link
     * restored. */
    uint8_t         channel;
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

/* How many events are waiting, and how many the queue holds.  The capacity is
 * PPCP_PEER_EVENT_QUEUE; it is a function too so a Swift or C++ caller that
 * never saw the macro can still ask. */
PPCP_API size_t ppcp_peer_events_pending(const ppcp_peer *p);
PPCP_API size_t ppcp_peer_events_capacity(void);

/* True when the last ppcp_peer_feed() (or ppcp_bundle_reader_feed() into this
 * peer) returned before the end of the caller's bytes because the event queue
 * had no room for the next frame's events.  Cleared by the next feed that gets
 * past that point.  Drain and feed the remainder. */
PPCP_API bool ppcp_peer_feed_stalled(const ppcp_peer *p);

/* Events lost to a full queue over this peer's lifetime.  With the S4 feed
 * this can only be raised by the engine's OWN events — link lost, link
 * restored, raised from ppcp_peer_tick() rather than from a frame — so a
 * non-zero value means the embedding is not draining between ticks.  A
 * conformance harness asserts it is zero. */
PPCP_API uint64_t ppcp_peer_events_dropped(const ppcp_peer *p);

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
 * produce, the consumer because it no longer wants the data.
 *
 * `closed_at` MAY be NULL, and for a consumer-originated close it usually must
 * be: the instant is in the STREAM'S timebase, which is the owner's clock, and
 * a consumer has no reading of it.  The field is then omitted rather than
 * invented.  CORE 5.11 already has `closed_at` at cardinality 0..1; MSG §11
 * lists it unmarked, and reconciling the two is erratum F-H4-2. */
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
/* ⚠ Where the engine has seen the named Stream opened, the Capture is checked
 * against it before the frame is queued: 5.14d (`{stream: true}` only on a
 * `continuous` Stream), I11 (gaps only there), the interval's timebase, and
 * 5.11j's preview rule.  `is_preview` must then agree with the Stream — the
 * parameter exists because a Capture does not carry the Stream's `kind`, not
 * because the caller gets to choose one. */
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

/* ------------------------------------- MSG §6 / CORE §6.3 — clock sync (L9)
 *
 * I21 and 6.1d: one probe sequence per LOCAL timebase, and each resulting
 * relation declared directly.  So an estimator is registered per timebase and
 * there is no way to ask this engine for a relation it did not measure.
 *
 * The remote timebase is learned from the first `sync_reply` (6.1b), because a
 * prober does not know which clock a responder stamps on until it says so.
 */

/* PPCP_ERR_LIMIT past PPCP_PEER_MAX_SYNC; PPCP_ERR_INVALID for a timebase
 * already registered.  `remote_tb` may be NULL. */
PPCP_API ppcp_result ppcp_peer_sync_add_timebase(ppcp_peer *p, const char *local_tb,
                                                 const char *remote_tb);
PPCP_API size_t ppcp_peer_sync_count(const ppcp_peer *p);
PPCP_API const ppcp_sync_estimator *ppcp_peer_sync_estimator_at(const ppcp_peer *p,
                                                                size_t index);
PPCP_API const ppcp_sync_estimator *ppcp_peer_sync_estimator_for(const ppcp_peer *p,
                                                                 const char *local_tb);

/* Queues one `sync_probe` on `local_tb`, reading `t1` from the embedding's
 * clock.  `probe_seq` runs per timebase, so two sequences never collide. */
PPCP_API ppcp_result ppcp_peer_sync_probe(ppcp_peer *p, const char *local_tb);

/* 6.1c — an embedding that can stamp closer to the socket than a clock read at
 * decode time supplies the four timestamps itself.  The automatic path (a
 * `sync_reply` arriving, `t4` read from the clock as it is handled) is the
 * convenient one; this is the accurate one, and both feed the same estimator. */
PPCP_API ppcp_result ppcp_peer_sync_observe(ppcp_peer *p, const char *local_tb,
                                            int64_t t1, int64_t t2, int64_t t3, int64_t t4);

/* 6.3c — a burst of PPCP_SYNC_BURST exchanges on every registered timebase.
 * Driven by the embedding's events because the library has neither a network
 * stack nor a thermometer.  A network change or a thermal event also restarts
 * each estimator's window: the FIT is stale, not merely the offset, because
 * oscillator frequency shifts with temperature. */
PPCP_API ppcp_result ppcp_peer_sync_trigger(ppcp_peer *p, ppcp_sync_trigger why);

/* Queues whatever is due — burst probes at PPCP_SYNC_BURST_GAP_MS, maintenance
 * probes at PPCP_SYNC_MAINTENANCE_MS (6.3g).  `now_ns` is any monotonic
 * nanosecond count of the embedding's choosing: it schedules and never enters
 * a measurement.
 *
 * ⚠ 6.3d — this cadence is the sync cadence and nothing here consults
 * `heartbeat_interval_ms`.  Liveness has its own pump below. */
PPCP_API ppcp_result ppcp_peer_sync_pump(ppcp_peer *p, int64_t now_ns, size_t *out_probes);

/* 6.1f — publishes the current estimate for every registered timebase that has
 * one, as a single `relation_update`.  Filtered, never stepped; that property
 * is the estimator's and is documented there. */
PPCP_API ppcp_result ppcp_peer_publish_relations(ppcp_peer *p, size_t *out_count);
/* The general form: publish relations the embedding measured or declared
 * itself — a device's camera-to-microphone mapping, a host's re-solve on
 * import (8.5d).  Every relation is validated, so I3 holds on the way out. */
PPCP_API ppcp_result ppcp_peer_relation_update(ppcp_peer *p,
                                               const ppcp_timebase_relation *rels,
                                               size_t count);
/* MSG 6.2 / CORE 6.3h — the per-shot residual between an acoustic fiducial and
 * the network clock estimate. */
PPCP_API ppcp_result ppcp_peer_sync_residual(ppcp_peer *p, const char *shot_id,
                                             const char *timebase_id, int64_t residual_ns,
                                             const char *basis);

/* --------------------------------------- MSG §5.4 / CORE §7.4 — liveness (L9)
 *
 * The host sends `heartbeat` at `Session.heartbeat_interval_ms`; every peer
 * answers `heartbeat_ack` carrying thermal, storage and battery from
 * `health_report`.  Three consecutive missed intervals is a lost link (7.4c),
 * and a lost link is the second entry into the zero-host regime (8.3g) — the
 * first being a Session that never had a host at all.
 */

/* Host only (7.4a), and refused otherwise by C2 as well as by role. */
PPCP_API ppcp_result ppcp_peer_heartbeat(ppcp_peer *p);

/* Drives liveness from the embedding's clock, because the library owns no
 * timer.  Call at least once per heartbeat interval with a monotonic `now_ns`.
 * A host also queues its due `heartbeat` here, so one call serves both ends. */
PPCP_API ppcp_result ppcp_peer_liveness_pump(ppcp_peer *p, int64_t now_ns);

PPCP_API ppcp_link_state ppcp_peer_link_state(const ppcp_peer *p);
PPCP_API uint32_t        ppcp_peer_missed_heartbeats(const ppcp_peer *p);

/* 8.3g — "a Session with an unreachable host is not a Session with no host."
 * True when this peer must mint under 8.3a–c: either the Session has no host
 * in its roster, or the host has been unreachable for three heartbeat
 * intervals.  Nothing about the Session changes when it becomes true, which is
 * what ppcp_peer_session_params() below exists to let a test assert. */
PPCP_API bool ppcp_peer_zero_host(const ppcp_peer *p);

/* The Session parameters of the open Session, or NULL before one.  I16 and
 * 8.3g: `timebase_ref`, `coincidence_window_ns` and `issue_hold_ns` are what
 * they were, whatever the link is doing.
 *
 * ⚠ EITHER END.  Before S4 this was recorded on the RECEIVING path only, so
 * the peer that ORIGINATED `session_open` — every host, and every device
 * opening the hostless Session of CORE 4.1b — read NULL here and kept a second
 * copy of its own parameters that drifted (F-H5-2, F-D6-3, plan §9).  It is
 * now set by ppcp_peer_session_open() and ppcp_peer_session_resume() as well,
 * and it is what ppcp_peer_zero_host() is derived from. */
PPCP_API const ppcp_body_session_open *ppcp_peer_session_params(const ppcp_peer *p);

/* -------------------------------- MSG §9.1–9.2 — offering a stored Session
 *
 * The user-facing flow is not a file picker: a CONNECTED capture device offers
 * the Sessions it holds and the host chooses (plan §9, 22 August 2026).  These
 * are the three messages that carries, and `ppcp_bundle_replay` in bundle.h is
 * what puts the accepted Session's frames onto the live link.
 *
 * DEVICE SIDE                          HOST SIDE
 *   ppcp_peer_session_offer(...)  ->   PPCP_EVENT_SESSION_OFFER
 *                                      ppcp_peer_session_accept(verdict,
 *                                        have_digests, in_reply_to)
 *   PPCP_EVENT_SESSION_ACCEPT     <-
 *   ppcp_bundle_replay_new(peer, have_digests, ...)
 *   ppcp_bundle_replay_feed(bytes of the stored PPCPBNDL)
 *                                 ->   the ordinary ingest path: manifest,
 *                                      declare, session_open, captures,
 *                                      payloads — the same frames a live
 *                                      session would have carried.
 *
 * `session_manifest` has an originator of its own because ENC 7c makes it the
 * one frame a bundle MUST contain, and until L9 it was the one frame with no
 * `ppcp_peer_*` entry point (plan §9, D, 22 August 2026). */
PPCP_API ppcp_result ppcp_peer_session_offer(ppcp_peer *p,
                                             const ppcp_body_session_offer *offer);
PPCP_API ppcp_result ppcp_peer_session_accept(ppcp_peer *p,
                                              const ppcp_body_session_accept *accept,
                                              uint64_t in_reply_to);
PPCP_API ppcp_result ppcp_peer_session_manifest(ppcp_peer *p,
                                                const ppcp_body_session_manifest *manifest);

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
/* This peer's own id and role.  5.2a: both are fixed for the Session's life,
 * which is why they are accessors and not setters. */
PPCP_API const ppcp_id *ppcp_peer_id(const ppcp_peer *p);
PPCP_API ppcp_role      ppcp_peer_get_role(const ppcp_peer *p);
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
/* The relations this peer currently holds — filled from every
 * `relation_update` that arrives, from ppcp_peer_relation_update() and from
 * ppcp_peer_publish_relations().  Mutable, because an embedding that measured
 * a relation the wire never carried (a device's camera-to-microphone mapping)
 * puts it here.
 *
 * ⚠ Nothing here composes.  ppcp_relations_convert() applies at most one
 * relation and refuses when it holds no direct one (I18, 5.4c, 8.2i1). */
PPCP_API ppcp_relation_set *ppcp_peer_relations(ppcp_peer *p);

/* 5.12a / I26 — the Sources this peer declared, and the Timebase each names.
 * A `candidate` naming anything else is refused before it reaches a wire. */
PPCP_API size_t ppcp_peer_own_source_count(const ppcp_peer *p);
PPCP_API bool   ppcp_peer_owns_source(const ppcp_peer *p, const ppcp_id *source_id,
                                      ppcp_id *out_timebase_id);
PPCP_API bool   ppcp_peer_declares_timebase(const ppcp_peer *p, const ppcp_id *timebase_id);

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
