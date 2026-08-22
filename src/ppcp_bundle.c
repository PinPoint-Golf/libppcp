/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * The bundle container — CORE §9, ENC §7, MSG §9.1–9.2.  Work package L8.
 *
 * There is almost no code here, and that is the point.  A bundle is a recorded
 * message stream, so the reader's whole job is to strip sixteen bytes off the
 * front and hand the rest to ppcp_peer_feed() — the same function a socket
 * drives.  What remains is the four rules ENC §7 adds on top of the live path,
 * and the accounting a consumer needs afterwards.
 */
#include "ppcp/bundle.h"
#include "ppcp_codec.h"

#include <string.h>

/* Big enough for a `declare` carrying a realistic Source list; a bundle that
 * exceeds it reports PPCP_ERR_LIMIT rather than truncating a declaration. */
#define PPCP_BUNDLE_ARENA_BYTES (32u * 1024u)

/* ============================================================== the writer */

#define PPCP_BUNDLE_MAX_PREVIEW 8

struct ppcp_bundle_writer {
    bool     begun;
    bool     finished;
    bool     has_manifest;    /* ENC 7c */
    bool     saw_session_open;
    bool     hostless;        /* CORE 4.1d / 7.3b */
    size_t   frame_count;
    /* CORE 5.11j: a preview Capture is live-only and MUST NOT be written to a
     * bundle.  A Capture does not carry its Stream's `kind`, so the writer
     * learns which Streams are preview from the `stream_open` frames it has
     * already been given — which in a bundle always precede the Captures on
     * them, because they would have on a socket too (ENC 7b). */
    ppcp_id  preview_streams[PPCP_BUNDLE_MAX_PREVIEW];
    size_t   preview_count;
    ppcp_msg scratch;
    ppcp_arena arena;
    uint8_t  arena_buf[4096];
};

size_t ppcp_bundle_writer_sizeof(void) { return sizeof(struct ppcp_bundle_writer); }

ppcp_result ppcp_bundle_writer_new(void *storage, size_t storage_len,
                                   ppcp_bundle_writer **out)
{
    ppcp_bundle_writer *w;
    if (storage == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    if (storage_len < sizeof(*w))
        return PPCP_ERR_NOSPACE;
    w = (ppcp_bundle_writer *)storage;
    memset(w, 0, sizeof(*w));
    ppcp_arena_init(&w->arena, w->arena_buf, sizeof(w->arena_buf));
    *out = w;
    return PPCP_OK;
}

ppcp_result ppcp_bundle_writer_begin(ppcp_bundle_writer *w, uint8_t *out, size_t cap,
                                     size_t *out_len)
{
    ppcp_bundle_header h;
    ppcp_result        rc;

    if (w == NULL || out == NULL || out_len == NULL)
        return PPCP_ERR_INVALID;
    if (w->begun)
        return PPCP_ERR_INVALID;
    if (cap < PPCP_BUNDLE_HEADER_BYTES)
        return PPCP_ERR_NOSPACE;
    h.major    = PPCP_BUNDLE_MAJOR;
    h.minor    = PPCP_BUNDLE_MINOR;
    h.reserved = 0;
    rc = ppcp_bundle_header_write(out, &h);
    if (rc != PPCP_OK)
        return rc;
    w->begun = true;
    *out_len = PPCP_BUNDLE_HEADER_BYTES;
    return PPCP_OK;
}

/* The four rules of ENC §7 that a live transport does not have, applied to one
 * frame that has already been decoded far enough to name its type. */
static ppcp_result writer_admit(ppcp_bundle_writer *w, ppcp_msg_type t,
                                const uint8_t *payload, uint32_t payload_len,
                                uint8_t channel)
{
    switch (t) {
    case PPCP_MT_LINK_BIND:
        /* ENC 7g / 2.1e: a file has one stream and its channels are the header
         * byte.  There is nothing to bind, and a bundle carrying a binding
         * would be asserting a transport it does not have. */
        return PPCP_ERR_INVALID;
    case PPCP_MT_ARM:
    case PPCP_MT_DISARM:
        /* CORE 7.3b: with nobody controlling there is no command to record.
         * The bundle carries the EFFECT — Streams, `readiness`, Captures. */
        if (w->hostless)
            return PPCP_ERR_INVALID;
        return PPCP_OK;
    case PPCP_MT_SESSION_MANIFEST:
        w->has_manifest = true;
        return PPCP_OK;
    case PPCP_MT_PAYLOAD_BEGIN:
    case PPCP_MT_PAYLOAD_CHUNK:
    case PPCP_MT_PAYLOAD_ACK:
    case PPCP_MT_PAYLOAD_END:
    case PPCP_MT_PAYLOAD_ABORT:
    case PPCP_MT_PAYLOAD_RESUME:
        /* ENC 7c / MSG 9.2a: the manifest first, so an interrupted read still
         * yields an analysable session. */
        if (!w->has_manifest)
            return PPCP_ERR_INVALID;
        return PPCP_OK;
    case PPCP_MT_STREAM_OPEN: {
        /* Noted, not refused: a preview Stream in a bundle is legal — 5.11j
         * forbids its CAPTURES, not the Stream that says one existed. */
        ppcp_result rc;
        ppcp_arena_reset(&w->arena);
        rc = ppcp_msg_decode(payload, payload_len,
                             ppcp_cbor_limits_for_channel(channel), &w->arena,
                             &w->scratch);
        if (rc != PPCP_OK)
            return rc;
        if (ppcp_stream_is_preview(&w->scratch.body.stream_open.stream) &&
            w->preview_count < PPCP_BUNDLE_MAX_PREVIEW)
            w->preview_streams[w->preview_count++] =
                w->scratch.body.stream_open.stream.id;
        return PPCP_OK;
    }
    case PPCP_MT_CAPTURE_ANNOUNCE: {
        /* CORE 5.11j / MSG 8.1i — preview is live-only: a peer that cannot
         * deliver a preview segment promptly discards it, and MUST NOT retain
         * it for later transfer or write it to a bundle.  A ninety-minute
         * range session of preview frames would be a substantial fraction of
         * the storage budgeted for shot video, spent on frames 5.11g forbids
         * anyone from using for anything. */
        ppcp_result rc;
        size_t      i;
        if (w->preview_count == 0)
            return PPCP_OK;
        ppcp_arena_reset(&w->arena);
        rc = ppcp_msg_decode(payload, payload_len,
                             ppcp_cbor_limits_for_channel(channel), &w->arena,
                             &w->scratch);
        if (rc != PPCP_OK)
            return rc;
        for (i = 0; i < w->preview_count; i++) {
            if (ppcp_id_equal(&w->preview_streams[i],
                              &w->scratch.body.capture_announce.capture.stream_id))
                return PPCP_ERR_INVALID;
        }
        return PPCP_OK;
    }
    case PPCP_MT_SESSION_OPEN: {
        /* 4.1d / 5.10e: the two arbitration parameters are present if and only
         * if the Session has a host, so their ABSENCE is what makes this
         * bundle hostless and is what 7.3b then keys on. */
        ppcp_result rc;
        ppcp_arena_reset(&w->arena);
        rc = ppcp_msg_decode(payload, payload_len,
                             ppcp_cbor_limits_for_channel(channel), &w->arena,
                             &w->scratch);
        if (rc != PPCP_OK)
            return rc;
        w->saw_session_open = true;
        w->hostless = !w->scratch.body.session_open.has_arbitration;
        return PPCP_OK;
    }
    default:
        return PPCP_OK;
    }
}

static ppcp_result writer_append_one(ppcp_bundle_writer *w, const uint8_t *frame,
                                     size_t frame_len, uint8_t channel,
                                     const uint8_t *payload, uint32_t payload_len,
                                     uint8_t *out, size_t cap, size_t *out_len)
{
    ppcp_envelope env;
    uint32_t      pairs = 0;
    ppcp_msg_info info;
    ppcp_msg_type t = PPCP_MT_UNKNOWN;
    ppcp_result   rc;

    rc = ppcp_envelope_decode(payload, payload_len,
                              ppcp_cbor_limits_for_channel(channel), &env, &pairs);
    if (rc != PPCP_OK)
        return rc;
    /* MSG 1b: an unknown type is carried, not refused.  A bundle written by a
     * newer MINOR is readable by an older reader (ENC 7f), which only works if
     * a writer will also carry a frame it does not itself understand. */
    if (ppcp_msg_lookup(env.type, env.type_len, &info) == PPCP_OK)
        t = info.id;

    rc = writer_admit(w, t, payload, payload_len, channel);
    if (rc != PPCP_OK)
        return rc;

    if (cap < frame_len)
        return PPCP_ERR_NOSPACE;
    memcpy(out, frame, frame_len);
    *out_len = frame_len;
    w->frame_count++;
    return PPCP_OK;
}

ppcp_result ppcp_bundle_writer_append_msg(ppcp_bundle_writer *w, uint8_t channel,
                                          const ppcp_msg *m, uint8_t *out, size_t cap,
                                          size_t *out_len)
{
    size_t            written = 0;
    ppcp_frame_header hdr;
    const uint8_t    *payload;
    size_t            consumed = 0;
    ppcp_result       rc;

    if (w == NULL || m == NULL || out == NULL || out_len == NULL)
        return PPCP_ERR_INVALID;
    if (!w->begun || w->finished)
        return PPCP_ERR_INVALID;
    *out_len = 0;

    /* Encoded straight into the caller's buffer — ENC 7a says the frame in a
     * bundle is byte-identical in form to the one on a socket, so it is the
     * same encoder and there is no bundle-shaped variant of it. */
    rc = ppcp_msg_encode(out, cap, channel, m, &written);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_frame_read(out, written, &hdr, &payload, &consumed);
    if (rc != PPCP_OK)
        return rc;
    /* Admitted after encoding and in place: the bytes are already where they
     * belong, so a refusal simply reports and the caller writes nothing. */
    rc = writer_admit(w, m->type, payload, hdr.payload_len, channel);
    if (rc != PPCP_OK)
        return rc;
    w->frame_count++;
    *out_len = written;
    return PPCP_OK;
}

ppcp_result ppcp_bundle_writer_append_frames(ppcp_bundle_writer *w, const uint8_t *frames,
                                             size_t len, uint8_t *out, size_t cap,
                                             size_t *out_len)
{
    size_t off = 0, written = 0;

    if (w == NULL || frames == NULL || out == NULL || out_len == NULL)
        return PPCP_ERR_INVALID;
    if (!w->begun || w->finished)
        return PPCP_ERR_INVALID;
    *out_len = 0;

    while (off < len) {
        ppcp_frame_header hdr;
        const uint8_t    *payload;
        size_t            consumed = 0, one = 0;
        ppcp_result       rc = ppcp_frame_read(frames + off, len - off, &hdr, &payload,
                                               &consumed);
        if (rc != PPCP_OK)
            return rc;   /* TRUNCATED: a whole frame or nothing */
        rc = writer_append_one(w, frames + off, consumed, hdr.channel, payload,
                               hdr.payload_len, out + written, cap - written, &one);
        if (rc != PPCP_OK)
            return rc;
        written += one;
        off     += consumed;
    }
    *out_len = written;
    return PPCP_OK;
}

ppcp_result ppcp_bundle_writer_finish(ppcp_bundle_writer *w)
{
    if (w == NULL || !w->begun)
        return PPCP_ERR_INVALID;
    /* ENC 7e: no trailing index, footer or table of contents in ppcp/1.0.
     * Random access is a MINOR that appends a frame type, not a container
     * change, so there is nothing to write here and that is deliberate. */
    w->finished = true;
    return PPCP_OK;
}

size_t ppcp_bundle_writer_frame_count(const ppcp_bundle_writer *w)
{
    return (w == NULL) ? 0 : w->frame_count;
}

bool ppcp_bundle_writer_has_manifest(const ppcp_bundle_writer *w)
{
    return (w != NULL) && w->has_manifest;
}

bool ppcp_bundle_writer_is_hostless(const ppcp_bundle_writer *w)
{
    return (w != NULL) && w->saw_session_open && w->hostless;
}

/* ==================================================== I34 — capture identity */

void ppcp_capture_index_init(ppcp_capture_index *ix)
{
    if (ix != NULL)
        memset(ix, 0, sizeof(*ix));
}

static bool key_equal(const ppcp_capture_key *a, const ppcp_capture_key *b)
{
    return ppcp_id_equal(&a->session_id, &b->session_id) &&
           ppcp_id_equal(&a->peer_id, &b->peer_id) &&
           ppcp_id_equal(&a->capture_id, &b->capture_id);
}

bool ppcp_capture_index_contains(const ppcp_capture_index *ix, const ppcp_capture_key *key)
{
    size_t i;
    if (ix == NULL || key == NULL)
        return false;
    for (i = 0; i < ix->count; i++)
        if (key_equal(&ix->keys[i], key))
            return true;
    return false;
}

ppcp_result ppcp_capture_index_observe(ppcp_capture_index *ix, const ppcp_capture_key *key,
                                       bool *out_is_new)
{
    if (ix == NULL || key == NULL || out_is_new == NULL)
        return PPCP_ERR_INVALID;
    if (!ppcp_id_is_set(&key->capture_id))
        return PPCP_ERR_INVALID;
    if (ppcp_capture_index_contains(ix, key)) {
        /* I34: the second import is a no-op.  Users connect twice. */
        *out_is_new = false;
        return PPCP_OK;
    }
    if (ix->count == PPCP_CAPTURE_INDEX_MAX)
        return PPCP_ERR_LIMIT;
    ix->keys[ix->count++] = *key;
    *out_is_new = true;
    return PPCP_OK;
}

size_t ppcp_capture_index_count(const ppcp_capture_index *ix)
{
    return (ix == NULL) ? 0 : ix->count;
}

/* ============================================================== the reader */

struct ppcp_bundle_reader {
    ppcp_peer *sink;
    bool       header_done;
    bool       finished;
    uint16_t   minor;
    size_t     frame_count;
    bool       tail_pending;        /* the last feed ended mid-frame (ENC 7d) */
    bool       manifest_seen;
    bool       manifest_ordered;    /* ENC 7c held */
    bool       has_asserted;
    ppcp_completeness asserted;
    bool       has_owner;
    ppcp_id    owner_peer_id;
    ppcp_capture_index index;
    ppcp_msg   msg;
    ppcp_arena arena;
    uint8_t    arena_buf[PPCP_BUNDLE_ARENA_BYTES];
    uint8_t    sink_sink[4096];     /* where the sink's answers go to die */
};

size_t ppcp_bundle_reader_sizeof(void) { return sizeof(struct ppcp_bundle_reader); }

ppcp_result ppcp_bundle_reader_new(void *storage, size_t storage_len, ppcp_peer *sink,
                                   ppcp_bundle_reader **out)
{
    ppcp_bundle_reader *r;
    if (storage == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    if (storage_len < sizeof(*r))
        return PPCP_ERR_NOSPACE;
    r = (ppcp_bundle_reader *)storage;
    memset(r, 0, sizeof(*r));
    r->sink             = sink;
    r->manifest_ordered = true;
    ppcp_capture_index_init(&r->index);
    ppcp_arena_init(&r->arena, r->arena_buf, sizeof(r->arena_buf));
    *out = r;
    return PPCP_OK;
}

ppcp_capture_index *ppcp_bundle_reader_index(ppcp_bundle_reader *r)
{
    return (r == NULL) ? NULL : &r->index;
}

/* The sink is being replayed a recorded conversation, so its answers —
 * `session_joined`, `declare_ack`, `stream_open_ack` — have nowhere to go.
 * They are drained and discarded rather than left to fill the queue.
 *
 * ⚠ One answer is NOT discardable and is the embedding's obligation, not
 * this function's: CORE 5.14h requires `capture_committed` to be queued for
 * the owning peer on its NEXT CONNECTION.  A bundle importer records what it
 * received and sends it when that peer appears; nothing in a file can. */
static void reader_discard_answers(ppcp_bundle_reader *r)
{
    uint8_t ch;
    if (r->sink == NULL)
        return;
    for (ch = 0; ch < 3; ch++) {
        size_t n = 0;
        while (ppcp_peer_pending(r->sink, ch) > 0) {
            if (ppcp_peer_drain(r->sink, ch, r->sink_sink, sizeof(r->sink_sink), &n)
                != PPCP_OK || n == 0)
                break;
        }
    }
}

static void reader_inspect(ppcp_bundle_reader *r, const ppcp_msg *m)
{
    switch (m->type) {
    case PPCP_MT_DECLARE:
        /* The bundle's own peer: I34 scopes Capture identity by session and
         * OWNING PEER, and in a bundle the owner is the peer that declared. */
        if (!r->has_owner) {
            r->owner_peer_id = m->body.declare.peer.id;
            r->has_owner     = true;
        }
        break;
    case PPCP_MT_SESSION_OFFER:
        if (!r->has_owner) {
            r->owner_peer_id = m->body.session_offer.minting_peer_id;
            r->has_owner     = true;
        }
        if (m->body.session_offer.completeness != PPCP_UNKNOWN) {
            r->has_asserted = true;
            r->asserted     = m->body.session_offer.completeness;
        }
        break;
    case PPCP_MT_SESSION_MANIFEST:
        r->manifest_seen = true;
        /* 4.4a / I10: asserted by the peer that owns the data.  `unknown` is
         * not an assertion — it is the absence of one — so it does not stop
         * ENC 7d's truncation rule from speaking. */
        if (m->body.session_manifest.completeness != PPCP_UNKNOWN) {
            r->has_asserted = true;
            r->asserted     = m->body.session_manifest.completeness;
        }
        break;
    case PPCP_MT_SESSION_STATE:
        if (m->body.session_state.completeness != PPCP_UNKNOWN) {
            r->has_asserted = true;
            r->asserted     = m->body.session_state.completeness;
        }
        break;
    case PPCP_MT_CAPTURE_ANNOUNCE: {
        ppcp_capture_key key;
        bool             is_new = false;
        memset(&key, 0, sizeof(key));
        if (m->env.has_session_id)
            key.session_id = m->env.session_id;
        if (r->has_owner)
            key.peer_id = r->owner_peer_id;
        key.capture_id = m->body.capture_announce.capture.id;
        /* The digest is deliberately not in the key: a `complete` + `pending`
         * Capture has none yet and an `absent` one never will, and keying on
         * it would import both of those twice (I34, CT-I34). */
        (void)ppcp_capture_index_observe(&r->index, &key, &is_new);
        break;
    }
    case PPCP_MT_PAYLOAD_BEGIN:
    case PPCP_MT_PAYLOAD_CHUNK:
    case PPCP_MT_PAYLOAD_ACK:
    case PPCP_MT_PAYLOAD_END:
    case PPCP_MT_PAYLOAD_ABORT:
    case PPCP_MT_PAYLOAD_RESUME:
        /* ENC 7c is a MUST on the writer.  A reader that meets a payload frame
         * first has met a non-conformant bundle; it reads on — one misordered
         * frame is not a reason to lose a session — and records the fact. */
        if (!r->manifest_seen)
            r->manifest_ordered = false;
        break;
    default:
        break;
    }
}

ppcp_result ppcp_bundle_reader_feed(ppcp_bundle_reader *r, const uint8_t *bytes, size_t len,
                                    size_t *out_consumed)
{
    size_t      off = 0;
    ppcp_result rc;

    if (r == NULL || out_consumed == NULL || (len > 0 && bytes == NULL))
        return PPCP_ERR_INVALID;
    *out_consumed = 0;
    if (r->finished)
        return PPCP_ERR_INVALID;

    if (!r->header_done) {
        ppcp_bundle_header h;
        if (len < PPCP_BUNDLE_HEADER_BYTES) {
            r->tail_pending = true;
            return PPCP_OK;
        }
        rc = ppcp_bundle_header_parse(bytes, &h);
        if (rc != PPCP_OK)
            return rc;   /* wrong magic, or a MAJOR this reader cannot read */
        /* ENC 7f: a `minor` above this reader's own is accepted and the frames
         * it does not understand are ignored (I13).  That is the whole of
         * forward compatibility for the container. */
        r->minor       = h.minor;
        r->header_done = true;
        off            = PPCP_BUNDLE_HEADER_BYTES;
    }

    while (off + PPCP_FRAME_HEADER_BYTES <= len) {
        ppcp_frame_header hdr;
        const uint8_t    *payload;
        size_t            consumed = 0, fed = 0;

        rc = ppcp_frame_header_parse(bytes + off, &hdr);
        if (rc != PPCP_OK) {
            *out_consumed = off;
            return rc;   /* ENC 8a: a length past the limit is fatal, in a file too */
        }
        rc = ppcp_frame_read(bytes + off, len - off, &hdr, &payload, &consumed);
        if (rc == PPCP_ERR_TRUNCATED)
            break;       /* ENC 3c/7d: not an error in a bundle */
        if (rc != PPCP_OK) {
            *out_consumed = off;
            return rc;
        }

        /* CORE 9a / plan A10 — the same feed a socket drives.  There is no
         * importer, and this line is the reason there is not one. */
        if (r->sink != NULL) {
            (void)ppcp_peer_feed(r->sink, hdr.channel, bytes + off, consumed, &fed);
            reader_discard_answers(r);
        }

        ppcp_arena_reset(&r->arena);
        memset(&r->msg, 0, sizeof(r->msg));
        if (ppcp_msg_decode(payload, hdr.payload_len,
                            ppcp_cbor_limits_for_channel(hdr.channel),
                            &r->arena, &r->msg) == PPCP_OK)
            reader_inspect(r, &r->msg);

        r->frame_count++;
        off += consumed;
    }

    r->tail_pending = (off < len);
    *out_consumed   = off;
    return PPCP_OK;
}

ppcp_result ppcp_bundle_reader_finish(ppcp_bundle_reader *r, ppcp_completeness *out)
{
    if (r == NULL)
        return PPCP_ERR_INVALID;
    r->finished = true;
    if (out == NULL)
        return PPCP_OK;
    if (r->has_asserted) {
        /* I10: the owner said so.  A truncated tail does not overrule it, and
         * a whole file does not promote it — 7d says the reader never upgrades
         * a partial Session on the strength of what happened to be present. */
        *out = r->asserted;
    } else if (r->tail_pending || !r->header_done) {
        *out = PPCP_PARTIAL;      /* ENC 7d */
    } else {
        /* Not `complete`: completeness is asserted, never inferred, and a file
         * that simply ended is not a peer saying it had everything. */
        *out = PPCP_UNKNOWN;
    }
    return PPCP_OK;
}

bool ppcp_bundle_reader_truncated(const ppcp_bundle_reader *r)
{
    return (r != NULL) && (r->tail_pending || !r->header_done);
}

bool ppcp_bundle_reader_asserted(const ppcp_bundle_reader *r, ppcp_completeness *out)
{
    if (r == NULL || !r->has_asserted)
        return false;
    if (out != NULL)
        *out = r->asserted;
    return true;
}

uint16_t ppcp_bundle_reader_minor(const ppcp_bundle_reader *r)
{
    return (r == NULL) ? 0 : r->minor;
}

size_t ppcp_bundle_reader_frame_count(const ppcp_bundle_reader *r)
{
    return (r == NULL) ? 0 : r->frame_count;
}

bool ppcp_bundle_reader_manifest_ordered(const ppcp_bundle_reader *r)
{
    return (r == NULL) ? false : r->manifest_ordered;
}

/* ================================= MSG §9.1 — replaying a bundle onto a link
 *
 * See include/ppcp/bundle.h for the call sequence.  The whole of this is: read
 * frames out of the file, decide whether 9.1a lets us skip the payload, and
 * put the rest through ppcp_peer_send().  There is no translation step,
 * because ENC 7a says there is nothing to translate.
 */

struct ppcp_bundle_replay {
    ppcp_peer *tx;
    bool       header_done;
    uint16_t   minor;
    size_t     sent;
    size_t     skipped;

    /* 9.1a — the digests the importer said it holds, and the Capture ids they
     * turned out to name in this bundle. */
    ppcp_digest have[PPCP_MAX_HAVE_DIGESTS];
    size_t      have_count;
    ppcp_id     held[PPCP_REPLAY_SKIP_MAX];
    size_t      held_count;

    ppcp_msg   msg;
    ppcp_arena arena;
    uint8_t    arena_buf[PPCP_BUNDLE_ARENA_BYTES];
};

size_t ppcp_bundle_replay_sizeof(void) { return sizeof(struct ppcp_bundle_replay); }

ppcp_result ppcp_bundle_replay_new(void *storage, size_t storage_len, ppcp_peer *tx,
                                   const ppcp_digest *have, size_t have_count,
                                   ppcp_bundle_replay **out)
{
    ppcp_bundle_replay *r;
    size_t              i;

    if (storage == NULL || out == NULL || tx == NULL)
        return PPCP_ERR_INVALID;
    if (storage_len < sizeof(*r))
        return PPCP_ERR_NOSPACE;
    if (have_count > PPCP_MAX_HAVE_DIGESTS)
        return PPCP_ERR_LIMIT;
    if (have_count > 0 && have == NULL)
        return PPCP_ERR_INVALID;

    r = (ppcp_bundle_replay *)storage;
    memset(r, 0, sizeof(*r));
    r->tx = tx;
    for (i = 0; i < have_count; i++)
        r->have[i] = have[i];
    r->have_count = have_count;
    ppcp_arena_init(&r->arena, r->arena_buf, sizeof(r->arena_buf));
    *out = r;
    return PPCP_OK;
}

static bool replay_has_digest(const ppcp_bundle_replay *r, const ppcp_digest *d)
{
    size_t i;
    if (d == NULL || !d->present)
        return false;
    for (i = 0; i < r->have_count; i++)
        if (ppcp_digest_equal(&r->have[i], d))
            return true;
    return false;
}

static bool replay_is_held(const ppcp_bundle_replay *r, const ppcp_id *capture_id)
{
    size_t i;
    for (i = 0; i < r->held_count; i++)
        if (ppcp_id_equal(&r->held[i], capture_id))
            return true;
    return false;
}

static void replay_hold(ppcp_bundle_replay *r, const ppcp_id *capture_id)
{
    if (!ppcp_id_is_set(capture_id) || replay_is_held(r, capture_id))
        return;
    if (r->held_count == PPCP_REPLAY_SKIP_MAX)
        return;   /* the importer keeps the payload it already had; no harm done */
    r->held[r->held_count++] = *capture_id;
}

/* Which Captures in this frame does the importer already hold?  Learned from
 * the manifest (9.2, and ENC 7c puts it before any payload) and from each
 * `capture_announce` that carries a digest. */
static void replay_learn(ppcp_bundle_replay *r, const ppcp_msg *m)
{
    size_t i;
    switch (m->type) {
    case PPCP_MT_SESSION_MANIFEST:
        for (i = 0; i < m->body.session_manifest.capture_count; i++)
            if (replay_has_digest(r, &m->body.session_manifest.captures[i].digest))
                replay_hold(r, &m->body.session_manifest.captures[i].capture_id);
        break;
    case PPCP_MT_CAPTURE_ANNOUNCE:
        if (replay_has_digest(r, &m->body.capture_announce.capture.digest))
            replay_hold(r, &m->body.capture_announce.capture.id);
        break;
    default:
        break;
    }
}

/* 9.1a — a payload frame for a Capture the importer holds is not re-sent.  The
 * announce and the manifest entry still are: the importer needs the Capture
 * record and it is only the bytes that are redundant. */
static bool replay_skip(const ppcp_bundle_replay *r, const ppcp_msg *m)
{
    const ppcp_id *id = NULL;
    switch (m->type) {
    case PPCP_MT_PAYLOAD_BEGIN:  id = &m->body.payload_begin.capture_id;  break;
    case PPCP_MT_PAYLOAD_CHUNK:  id = &m->body.payload_chunk.capture_id;  break;
    case PPCP_MT_PAYLOAD_END:    id = &m->body.payload_end.capture_id;    break;
    case PPCP_MT_PAYLOAD_ABORT:  id = &m->body.payload_abort.capture_id;  break;
    case PPCP_MT_PAYLOAD_RESUME: id = &m->body.payload_resume.capture_id; break;
    default: return false;
    }
    return replay_is_held(r, id);
}

ppcp_result ppcp_bundle_replay_feed(ppcp_bundle_replay *r, const uint8_t *bytes, size_t len,
                                    size_t *out_consumed)
{
    size_t      off = 0;
    ppcp_result rc;

    if (r == NULL || out_consumed == NULL || (len > 0 && bytes == NULL))
        return PPCP_ERR_INVALID;
    *out_consumed = 0;

    if (!r->header_done) {
        ppcp_bundle_header h;
        if (len < PPCP_BUNDLE_HEADER_BYTES)
            return PPCP_OK;
        rc = ppcp_bundle_header_parse(bytes, &h);
        if (rc != PPCP_OK)
            return rc;
        r->minor       = h.minor;
        r->header_done = true;
        off            = PPCP_BUNDLE_HEADER_BYTES;
    }

    while (off + PPCP_FRAME_HEADER_BYTES <= len) {
        ppcp_frame_header hdr;
        const uint8_t    *payload;
        size_t            consumed = 0;

        rc = ppcp_frame_header_parse(bytes + off, &hdr);
        if (rc != PPCP_OK) {
            *out_consumed = off;
            return rc;
        }
        rc = ppcp_frame_read(bytes + off, len - off, &hdr, &payload, &consumed);
        if (rc == PPCP_ERR_TRUNCATED)
            break;
        if (rc != PPCP_OK) {
            *out_consumed = off;
            return rc;
        }

        ppcp_arena_reset(&r->arena);
        memset(&r->msg, 0, sizeof(r->msg));
        rc = ppcp_msg_decode(payload, hdr.payload_len,
                             ppcp_cbor_limits_for_channel(hdr.channel), &r->arena, &r->msg);
        if (rc != PPCP_OK) {
            /* A frame this library cannot decode is a frame it cannot decide
             * about.  Sent verbatim would be a lie about `msg_id`; skipping it
             * silently would lose data.  It stops the replay. */
            *out_consumed = off;
            return rc;
        }

        replay_learn(r, &r->msg);

        /* ENC 7g — there is no `link_bind` in a bundle, and if one is there it
         * belongs to a link that no longer exists. */
        if (r->msg.type == PPCP_MT_LINK_BIND) {
            off += consumed;
            continue;
        }
        if (replay_skip(r, &r->msg)) {
            r->skipped++;
            off += consumed;
            continue;
        }

        rc = ppcp_peer_send(r->tx, hdr.channel, &r->msg);
        if (rc == PPCP_ERR_NOSPACE) {
            /* The link is behind.  Stop where we are and tell the caller how
             * far we got; it drains and re-presents the tail.  An engine that
             * buffered the rest would be buffering a whole session. */
            break;
        }
        if (rc != PPCP_OK) {
            *out_consumed = off;
            return rc;
        }
        r->sent++;
        off += consumed;
    }

    *out_consumed = off;
    return PPCP_OK;
}

size_t ppcp_bundle_replay_sent(const ppcp_bundle_replay *r)
{
    return (r == NULL) ? 0 : r->sent;
}

size_t ppcp_bundle_replay_skipped(const ppcp_bundle_replay *r)
{
    return (r == NULL) ? 0 : r->skipped;
}

size_t ppcp_bundle_replay_held_count(const ppcp_bundle_replay *r)
{
    return (r == NULL) ? 0 : r->held_count;
}
