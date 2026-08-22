/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * bundle.h — the bundle container.  Work package L8.
 *
 * CORE §9 and §8.5c; ENC §7; MSG §9.1–9.2.
 *
 * A BUNDLE IS NOT A FORMAT.  It is a recorded PPCP message stream with a
 * sixteen-byte header in front of it (CORE 9a, ENC 7a).  So there is no
 * importer here and no second schema: `ppcp_bundle_reader` streams the frames
 * through `ppcp_peer_feed()`, the same function a socket drives, and
 * `ppcp_bundle_writer` emits the frames a peer would have sent.  Three
 * artefacts collapse into this one path — the export bundle, the regression
 * fixture and the store-and-forward session.
 *
 * The embedding owns the file.  Neither object opens, reads or writes one:
 * the writer hands back bytes to append and the reader is handed bytes that
 * were read.  Ground rule 8, and it is what makes a bundle testable without a
 * filesystem.
 *
 * WHAT THE WRITER REFUSES, AND WHY EACH REFUSAL IS STRUCTURAL
 *
 *   ENC 7g   no `link_bind` in a bundle.  A file has one stream and its
 *            channels are the header byte; there is nothing to bind.
 *   ENC 7c   no `payload_*` frame before `session_manifest`, so an
 *            interrupted read still yields an analysable session.
 *   CORE 7.3b  no `arm` or `disarm` once a HOSTLESS `session_open` has been
 *            recorded.  Those are control messages conferred by Live, and
 *            with nobody controlling there is no command to record.  The
 *            bundle carries the effect — Streams, `readiness`, Captures — not
 *            a command nobody sent.
 *   ENC 7e   no trailing index, footer or table of contents.  Random access
 *            is deliberately not in ppcp/1.0, so there is no finish-time
 *            frame and ppcp_bundle_writer_finish() emits no bytes.
 */
#ifndef PPCP_BUNDLE_H
#define PPCP_BUNDLE_H

#include "ppcp/peer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================== the writer */

typedef struct ppcp_bundle_writer ppcp_bundle_writer;

PPCP_API size_t      ppcp_bundle_writer_sizeof(void);
PPCP_API ppcp_result ppcp_bundle_writer_new(void *storage, size_t storage_len,
                                            ppcp_bundle_writer **out);

/* Emits the ENC §7 header — "PPCPBNDL", major 1, minor 0, reserved 0 — and
 * must be the first call.  16 bytes. */
PPCP_API ppcp_result ppcp_bundle_writer_begin(ppcp_bundle_writer *w, uint8_t *out,
                                              size_t cap, size_t *out_len);

/* Appends one message, framed exactly as it would have been sent (ENC 7a).
 * `msg_id` is taken from `m->env`, so a caller replaying its own peer's
 * sequence keeps it; a caller composing a bundle directly sets it. */
PPCP_API ppcp_result ppcp_bundle_writer_append_msg(ppcp_bundle_writer *w, uint8_t channel,
                                                   const ppcp_msg *m, uint8_t *out,
                                                   size_t cap, size_t *out_len);

/* Appends frames that are ALREADY framed — which is exactly what
 * ppcp_peer_drain() produces.  A device recording its own session writes what
 * its engine would have sent, byte for byte, and that is what makes "live
 * bytes are bundle bytes" a fact about this library rather than a claim.
 * Several whole frames may be presented at once; a partial frame is
 * PPCP_ERR_TRUNCATED and nothing is written. */
PPCP_API ppcp_result ppcp_bundle_writer_append_frames(ppcp_bundle_writer *w,
                                                      const uint8_t *frames, size_t len,
                                                      uint8_t *out, size_t cap,
                                                      size_t *out_len);

/* No bytes: ENC 7e forbids a footer.  It exists to answer "was this bundle
 * finished on purpose", and to refuse a further append. */
PPCP_API ppcp_result ppcp_bundle_writer_finish(ppcp_bundle_writer *w);

PPCP_API size_t ppcp_bundle_writer_frame_count(const ppcp_bundle_writer *w);
/* True once a `session_manifest` has been recorded (ENC 7c). */
PPCP_API bool   ppcp_bundle_writer_has_manifest(const ppcp_bundle_writer *w);
/* True once a `session_open` WITHOUT the two arbitration parameters has been
 * recorded — which is the bundle stating that no arbitration occurred
 * (CORE 4.1d, 5.10e). */
PPCP_API bool   ppcp_bundle_writer_is_hostless(const ppcp_bundle_writer *w);

/* ============================================================== the reader */

typedef struct ppcp_bundle_reader ppcp_bundle_reader;

PPCP_API size_t      ppcp_bundle_reader_sizeof(void);
/* `sink` is the peer the frames are fed to — the same engine a socket would
 * feed.  It may be NULL, in which case the reader parses and accounts for the
 * frames without delivering them, which is what a fixture validator wants. */
PPCP_API ppcp_result ppcp_bundle_reader_new(void *storage, size_t storage_len,
                                            ppcp_peer *sink, ppcp_bundle_reader **out);

/* Consumes whole frames and reports how many bytes it took; the caller
 * re-presents the tail with more bytes after it, exactly as with
 * ppcp_peer_feed().  A short final frame is not an error here — ENC 3c makes
 * that the reader's decision and ENC 7d makes the decision `partial`. */
PPCP_API ppcp_result ppcp_bundle_reader_feed(ppcp_bundle_reader *r, const uint8_t *bytes,
                                             size_t len, size_t *out_consumed);

/* ENC 7d, and it is worth reading twice.
 *
 * A truncated final frame means the bundle is incomplete.  The reader treats
 * the Session as `partial` ONLY IF THE BUNDLE DID NOT ASSERT OTHERWISE, and
 * never upgrades a partial Session to complete on the strength of what
 * happened to be present (I10).  So:
 *
 *   asserted in the bundle          -> that value, whatever the bytes did
 *   not asserted, bytes truncated   -> partial
 *   not asserted, bytes whole       -> unknown
 *
 * `unknown` rather than `complete` for the last row is the same rule read
 * once more: completeness is ASSERTED by the peer that owns the data and is
 * never inferred by the receiver from what has arrived. */
PPCP_API ppcp_result ppcp_bundle_reader_finish(ppcp_bundle_reader *r,
                                               ppcp_completeness *out_completeness);

/* The two facts `finish` combines, separately — because CT-I36 (c) and (d)
 * turn on telling them apart: a truncated tail in a Session asserted `partial`
 * is the declared incompleteness, and the same truncation in a Session
 * asserted `complete` is a defect. */
PPCP_API bool ppcp_bundle_reader_truncated(const ppcp_bundle_reader *r);
PPCP_API bool ppcp_bundle_reader_asserted(const ppcp_bundle_reader *r,
                                          ppcp_completeness *out);
/* ENC 7f: a `minor` above this reader's own is accepted and its unknown
 * frames ignored (I13).  This is what it was. */
PPCP_API uint16_t ppcp_bundle_reader_minor(const ppcp_bundle_reader *r);
PPCP_API size_t   ppcp_bundle_reader_frame_count(const ppcp_bundle_reader *r);
/* ENC 7c is a MUST on the writer, so a reader that meets a `payload_*` frame
 * before the manifest has met a non-conformant bundle.  It reads on — one
 * misordered frame is not a reason to lose a session — and says so here. */
PPCP_API bool ppcp_bundle_reader_manifest_ordered(const ppcp_bundle_reader *r);

/* ================================= MSG §9.1 — replaying a bundle onto a link
 *
 * A stored Session is a `PPCPBNDL` file (plan A9) and a live Session is a pair
 * of sockets, and ENC 7a makes them the same bytes.  So "offer the host my
 * stored sessions" is not an export format and not an importer: it is this —
 * take the frames out of the file and put them through the peer's TX path,
 * with the msg_ids renumbered into the live sequence (ENC 5c).
 *
 * THE CALL SEQUENCE.  Device side, after `session_accept` arrives with
 * `verdict: accept`:
 *
 *     ppcp_bundle_replay_new(storage, len, peer,
 *                            accept->have_digests, accept->have_digest_count, &rp);
 *     for each chunk of the stored file:
 *         ppcp_bundle_replay_feed(rp, chunk, n, &consumed);
 *         drain the peer onto the socket;                 // see below
 *         re-present chunk + consumed when consumed < n;
 *
 * ⚠ `feed` STOPS when the peer's outbound queue is full, reports what it
 * consumed, and returns PPCP_OK.  A caller that does not drain between feeds
 * will make no progress; that is deliberate, because the alternative is an
 * engine that buffers a whole session.
 *
 * 9.1a — `have_digests` is what the importer already holds, and the payload
 * for such a Capture is not sent again.  The `capture_announce` and the
 * `session_manifest` entry ARE still sent: the importer needs the Capture
 * record, and it is the payload that is redundant, not the fact.  Identity
 * here is `Capture.digest`, which 9.1a states and which is a different rule
 * from I34's re-import identity below — a digest cannot be the key for an
 * `absent` Capture, and an `absent` Capture has no payload to skip.
 */

#define PPCP_REPLAY_SKIP_MAX 256

typedef struct ppcp_bundle_replay ppcp_bundle_replay;

PPCP_API size_t ppcp_bundle_replay_sizeof(void);

/* `tx` is the peer whose outbound path the frames are put onto — the live
 * link.  `have` may be NULL with `have_count` 0, in which case every payload
 * is sent. */
PPCP_API ppcp_result ppcp_bundle_replay_new(void *storage, size_t storage_len,
                                            ppcp_peer *tx,
                                            const ppcp_digest *have, size_t have_count,
                                            ppcp_bundle_replay **out);

/* Consumes whole frames and reports how many bytes it took, like every other
 * feed in this library.  A short final frame is left for the next call. */
PPCP_API ppcp_result ppcp_bundle_replay_feed(ppcp_bundle_replay *r, const uint8_t *bytes,
                                             size_t len, size_t *out_consumed);

PPCP_API size_t ppcp_bundle_replay_sent(const ppcp_bundle_replay *r);
/* Payload frames not re-sent because the importer said it holds them (9.1a). */
PPCP_API size_t ppcp_bundle_replay_skipped(const ppcp_bundle_replay *r);
/* Captures the importer declared it already holds, recognised in this bundle. */
PPCP_API size_t ppcp_bundle_replay_held_count(const ppcp_bundle_replay *r);

/* =================================================== I34 — re-import identity
 *
 * "Identity is `Capture.id` scoped by session and owning peer, and `digest`
 * where present is checked as CONTENT rather than used as the key."
 *
 * The index lives here rather than in either application because both need it
 * and it is the same rule for both (ground rule 1).  It is deliberately not
 * persistent: the embedding owns storage and seeds this from whatever it kept.
 *
 * ⚠ There is no `digest` in the key.  A `complete` + `pending` Capture has no
 * digest yet and an `absent` one never will; keying on the digest would import
 * both of those twice, which is exactly what CT-I34 replays. */
#define PPCP_CAPTURE_INDEX_MAX 512

typedef struct ppcp_capture_key {
    ppcp_id session_id;
    ppcp_id peer_id;
    ppcp_id capture_id;
} ppcp_capture_key;

typedef struct ppcp_capture_index {
    ppcp_capture_key keys[PPCP_CAPTURE_INDEX_MAX];
    size_t           count;
} ppcp_capture_index;

PPCP_API void ppcp_capture_index_init(ppcp_capture_index *ix);
PPCP_API bool ppcp_capture_index_contains(const ppcp_capture_index *ix,
                                          const ppcp_capture_key *key);
/* Returns PPCP_OK and sets `*out_is_new` — false means this Capture has been
 * imported before and the import is a no-op, which is I34.  PPCP_ERR_LIMIT
 * when the index is full; the embedding then owns a larger one. */
PPCP_API ppcp_result ppcp_capture_index_observe(ppcp_capture_index *ix,
                                                const ppcp_capture_key *key,
                                                bool *out_is_new);
PPCP_API size_t ppcp_capture_index_count(const ppcp_capture_index *ix);

/* The reader's own index, filled from every `capture_announce` it meets, so a
 * second read of the same bundle through the same index reports every Capture
 * as already held. */
PPCP_API ppcp_capture_index *ppcp_bundle_reader_index(ppcp_bundle_reader *r);

#ifdef __cplusplus
}
#endif
#endif /* PPCP_BUNDLE_H */
