/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * message.h — the forty-five messages of PPCP-MSG §11 and the error codes of
 * §10.  Work package L5.
 *
 * TWO THINGS LIVE HERE AND THEY ARE DIFFERENT.
 *
 * The **catalogue** is a table: for every message, its class, the channel it
 * belongs on, the profile a peer must have declared to ORIGINATE it, and the
 * MSG section that defines it.  It is machine-readable on purpose — the
 * profile-boundary audit of CONF 5b1 (work package L16) reads it, and the
 * engine of L6 consults it to refuse to originate a message its declared
 * profiles do not confer (C2, I24).
 *
 * The **codec** is a tagged union.  I24 makes every conformant peer parse the
 * complete type vocabulary whatever its profiles, so all forty-five decode
 * here unconditionally; profiles gate origination, never comprehension.  That
 * is CT-S6 assertion 4 and it is why the decoder has no profile parameter.
 *
 * `ppcp_msg` is a large object — about 48 KB — because it holds a Shot's
 * candidate list, a Capture's gap list and a `session_manifest`'s 256 entries
 * inline.  It is the caller's storage like everything else in this library,
 * and a peer holds one at a time.
 *
 * ⚠ SWIFT: DO NOT PUT A `ppcp_msg` ON THE STACK, AND DO NOT ASSIGN THROUGH ITS
 * `body`.
 *
 * The Swift importer presents a C union as a set of COMPUTED properties, so
 * `msg.body.session_manifest.session_id = x` is not a field store: it reads the
 * whole 48 KB union into a temporary, mutates it and writes it back.  An
 * ordinary synchronous test doing that hit SIGBUS on a device (plan §9, D,
 * 22 August 2026).  The pattern that works, and the reason each step is there:
 *
 *     let raw = UnsafeMutableRawPointer.allocate(          // heap, not stack
 *         byteCount: MemoryLayout<ppcp_msg>.size,
 *         alignment: MemoryLayout<ppcp_msg>.alignment)
 *     defer { raw.deallocate() }
 *     raw.initializeMemory(as: UInt8.self, repeating: 0,
 *                          count: MemoryLayout<ppcp_msg>.size)
 *     let m = raw.assumingMemoryBound(to: ppcp_msg.self)
 *     ppcp_msg_init(m, PPCP_MT_SESSION_MANIFEST, 1)
 *     withUnsafeMutablePointer(to: &m.pointee.body) { bodyPtr in
 *         bodyPtr.withMemoryRebound(to: ppcp_body_session_manifest.self,
 *                                   capacity: 1) { b in
 *             b.pointee.capture_count = 0                  // a real field store
 *         }
 *     }
 *
 * `body` is a NAMED union (`ppcp_msg_body`) precisely so the rebind above has
 * a stable type to name.  Where an arm is small — `heartbeat`, `sync_probe`,
 * `payload_ack` — the ordinary Swift path is fine; it is the large arms
 * (`declare`, `session_manifest`, `capture_announce`, `shot`) that copy.
 *
 * Better still, prefer the `ppcp_peer_*` originators in peer.h: every one of
 * them takes the body's own struct — `ppcp_body_session_manifest`,
 * `ppcp_capture`, `ppcp_shot` — and never a whole `ppcp_msg`, so a Swift caller
 * touches at most one arm.
 */
#ifndef PPCP_MESSAGE_H
#define PPCP_MESSAGE_H

#include "ppcp/model.h"
#include "ppcp/envelope.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------- catalogue */

typedef enum ppcp_msg_class {
    PPCP_MSG_REQUEST = 0,   /* expects exactly one response, correlated by reply_to */
    PPCP_MSG_RESPONSE,      /* carries reply_to; never itself requires a response */
    PPCP_MSG_EVENT          /* unsolicited; a receiver that does not implement it ignores it */
} ppcp_msg_class;

typedef enum ppcp_msg_channel {
    PPCP_MSGCH_CONTROL = 0, /* MSG §2: everything but the payload_* family */
    PPCP_MSGCH_BULK,        /* MSG §2: the payload_* family only */
    PPCP_MSGCH_ANY          /* `link_bind` (every channel, first frame) and `error` */
} ppcp_msg_channel;

typedef enum ppcp_msg_type {
    PPCP_MT_UNKNOWN = 0,
    PPCP_MT_LINK_BIND,          PPCP_MT_HELLO,              PPCP_MT_HELLO_ACCEPT,
    PPCP_MT_DECLARE,            PPCP_MT_DECLARE_ACK,        PPCP_MT_RELATION_UPDATE,
    PPCP_MT_CALIBRATION_UPDATE, PPCP_MT_DISCONTINUITY,      PPCP_MT_SESSION_OPEN,
    PPCP_MT_SESSION_JOINED,     PPCP_MT_SESSION_RESUME,     PPCP_MT_SESSION_STATE,
    PPCP_MT_CONTEXT_CHANGE,     PPCP_MT_SESSION_CLOSE,      PPCP_MT_STREAM_OPEN,
    PPCP_MT_STREAM_OPEN_ACK,    PPCP_MT_STREAM_CLOSE,       PPCP_MT_ARM,
    PPCP_MT_DISARM,             PPCP_MT_READINESS,          PPCP_MT_INTERRUPTION,
    PPCP_MT_HEARTBEAT,          PPCP_MT_HEARTBEAT_ACK,      PPCP_MT_SYNC_PROBE,
    PPCP_MT_SYNC_REPLY,         PPCP_MT_SYNC_RESIDUAL,      PPCP_MT_CANDIDATE,
    PPCP_MT_SHOT,               PPCP_MT_CAPTURE_REQUEST,    PPCP_MT_CAPTURE_ANNOUNCE,
    PPCP_MT_CAPTURE_UPDATE,     PPCP_MT_CAPTURE_COMMITTED,  PPCP_MT_ANNOTATION,
    PPCP_MT_PAYLOAD_BEGIN,      PPCP_MT_PAYLOAD_CHUNK,      PPCP_MT_PAYLOAD_ACK,
    PPCP_MT_PAYLOAD_END,        PPCP_MT_PAYLOAD_ABORT,      PPCP_MT_PAYLOAD_RESUME,
    PPCP_MT_SESSION_OFFER,      PPCP_MT_SESSION_ACCEPT,     PPCP_MT_SESSION_MANIFEST,
    PPCP_MT_SHOT_LINK,          PPCP_MT_SESSION_LINK,       PPCP_MT_ERROR,

    /* Erratum E58–E60 — CR-02.  APPENDED rather than interleaved into MSG
     * §11's order, so the numbering the two applications already compiled
     * against does not move.  `msg_table` carries §11's order; nothing here
     * indexes it by enumerator. */
    PPCP_MT_DEVICE_STATUS,      PPCP_MT_BUFFER_STATUS,
    PPCP_MT_ACTUATOR_COMMAND,   PPCP_MT_ACTUATOR_COMMAND_ACK,
    PPCP_MT_ACTUATOR_STATE
} ppcp_msg_type;

#define PPCP_MSG_COUNT 50

typedef struct ppcp_msg_info {
    const char      *type;                 /* the wire spelling */
    ppcp_msg_type    id;
    ppcp_msg_class   cls;
    ppcp_msg_channel channel;
    /* The profile a peer MUST have declared to ORIGINATE this message.  NULL
     * where no profile confers it — `link_bind`, `hello`, `hello_accept` and
     * `error`, which precede or transcend declaration. */
    const char      *originating_profile;
    const char      *section;              /* MSG section, e.g. "3.1" */
} ppcp_msg_info;

PPCP_API size_t                ppcp_msg_count(void);
PPCP_API const ppcp_msg_info  *ppcp_msg_at(size_t index);
PPCP_API const ppcp_msg_info  *ppcp_msg_for(ppcp_msg_type t);
/* An unknown type is PPCP_ERR_NOT_FOUND, never fatal (I13, I24, MSG 1b). */
PPCP_API ppcp_result           ppcp_msg_lookup(const char *type, size_t len,
                                               ppcp_msg_info *out);

/* --------------------------------------------------------------- errors */

#define PPCP_ERRCODE_UNSUPPORTED_VERSION  "unsupported_version"
#define PPCP_ERRCODE_ROLE_CONFLICT        "role_conflict"
#define PPCP_ERRCODE_MALFORMED            "malformed"
#define PPCP_ERRCODE_PROFILE_NOT_SUPPORTED "profile_not_supported"
#define PPCP_ERRCODE_POLICY_REJECT        "policy_reject"
#define PPCP_ERRCODE_UNKNOWN_SESSION      "unknown_session"
#define PPCP_ERRCODE_UNKNOWN_STREAM       "unknown_stream"
#define PPCP_ERRCODE_UNKNOWN_CAPTURE      "unknown_capture"
#define PPCP_ERRCODE_NOT_DECLARED         "not_declared"
#define PPCP_ERRCODE_RELATION_MISSING     "relation_missing"
#define PPCP_ERRCODE_RELATION_UNCERTAIN   "relation_uncertain"
#define PPCP_ERRCODE_NOT_ARMED            "not_armed"
#define PPCP_ERRCODE_OUTSIDE_BUFFER       "outside_buffer"
#define PPCP_ERRCODE_STORAGE_FULL         "storage_full"
#define PPCP_ERRCODE_ALREADY_PRESENT      "already_present"
#define PPCP_ERRCODE_RESOURCE_EXHAUSTED   "resource_exhausted"
#define PPCP_ERRCODE_INTERNAL             "internal"

typedef struct ppcp_error_info {
    const char *code;
    bool        fatal;    /* MSG 10b: only a fatal code closes the transport */
} ppcp_error_info;

PPCP_API size_t                 ppcp_error_code_count(void);
PPCP_API const ppcp_error_info *ppcp_error_code_at(size_t index);
/* MSG §10: exactly two codes are fatal — `unsupported_version` and
 * `role_conflict`.  An unknown code is NOT fatal: a peer that closed on a code
 * it did not recognise would make every future MINOR addition a disconnection. */
PPCP_API bool ppcp_msg_error_is_fatal(const char *code, size_t len);

/* ---------------------------------------------------------------- bodies */

#define PPCP_MAX_VERSIONS      8
#define PPCP_MAX_PROFILES      16
#define PPCP_MAX_EXTENSIONS    16
#define PPCP_MAX_STREAM_IDS    32
#define PPCP_MAX_MINTED_SHOTS  64
#define PPCP_MAX_PENDING       64
#define PPCP_MAX_MANIFEST      256
#define PPCP_MAX_HAVE_DIGESTS  64
#define PPCP_MAX_NOTES         32
#define PPCP_LINK_ID_BYTES     16      /* ENC 2.1a — 16 bytes from a CSPRNG */

typedef struct ppcp_body_link_bind {
    uint8_t link_id[PPCP_LINK_ID_BYTES];
    uint8_t channel;      /* 2.1a: equal to the frame header's */
} ppcp_body_link_bind;

typedef struct ppcp_body_hello {
    ppcp_id      versions[PPCP_MAX_VERSIONS];   /* 3.1b: most-preferred first, >= 1 */
    size_t       version_count;
    ppcp_id      peer_id;
    ppcp_role    role;
    ppcp_id      profiles[PPCP_MAX_PROFILES];
    size_t       profile_count;
    ppcp_id      extensions[PPCP_MAX_EXTENSIONS];
    size_t       extension_count;
    ppcp_product product;
} ppcp_body_hello;

typedef struct ppcp_body_hello_accept {
    ppcp_id      version;       /* the single selected wire version */
    ppcp_id      min_version;   /* 3.2d / CORE 10.1e — the support window */
    ppcp_id      peer_id;
    ppcp_role    role;
    ppcp_id      profiles[PPCP_MAX_PROFILES];
    size_t       profile_count;
    ppcp_id      extensions[PPCP_MAX_EXTENSIONS];
    size_t       extension_count;
    ppcp_product product;
} ppcp_body_hello_accept;

typedef struct ppcp_body_declare {
    uint64_t       generation;   /* 3.3a: a complete snapshot, never a delta */
    ppcp_peer_desc peer;         /* the head, plus the three lists assembled in */
} ppcp_body_declare;

typedef enum ppcp_verdict {
    PPCP_VERDICT_ACCEPTED = 0,
    PPCP_VERDICT_REJECTED
} ppcp_verdict;

typedef struct ppcp_profile_note {
    ppcp_id      source_id;
    ppcp_id      profile_id;
    ppcp_verdict verdict;
    bool         has_reason;
    ppcp_id      reason;
} ppcp_profile_note;

typedef struct ppcp_body_declare_ack {
    uint64_t          generation;
    ppcp_verdict      verdict;
    bool              has_reason;
    ppcp_id           reason;      /* 3.4a: machine-readable, required when rejected */
    ppcp_profile_note notes[PPCP_MAX_NOTES];
    size_t            note_count;
} ppcp_body_declare_ack;

#define PPCP_MAX_RELATIONS 16

typedef struct ppcp_body_relation_update {
    ppcp_timebase_relation relations[PPCP_MAX_RELATIONS];
    size_t                 relation_count;
} ppcp_body_relation_update;

typedef struct ppcp_body_calibration_update { ppcp_calibration calibration; }
    ppcp_body_calibration_update;
typedef struct ppcp_body_discontinuity { ppcp_clock_discontinuity discontinuity; }
    ppcp_body_discontinuity;

typedef struct ppcp_body_session_open {
    ppcp_id          session_id;
    ppcp_id          timebase_ref;
    ppcp_epoch       epoch;
    /* 4.1d / 5.10e: both present if and only if the Session has a host.  A
     * hostless session recording a `session_open` frame in its bundle omits
     * them, and their absence is the statement that no arbitration occurs. */
    bool             has_arbitration;
    ppcp_duration_ns coincidence_window_ns;
    ppcp_duration_ns issue_hold_ns;
    bool             has_heartbeat_interval;
    uint32_t         heartbeat_interval_ms;
} ppcp_body_session_open;

typedef enum ppcp_join_verdict { PPCP_JOINED = 0, PPCP_REFUSED } ppcp_join_verdict;

typedef struct ppcp_body_session_joined {
    ppcp_id           session_id;
    ppcp_id           peer_id;
    ppcp_join_verdict verdict;
    bool              has_reason;
    ppcp_id           reason;
} ppcp_body_session_joined;

typedef struct ppcp_pending_capture {
    ppcp_id     capture_id;
    ppcp_digest digest;
    uint64_t    bytes;
    bool        has_acked_index;
    uint32_t    acked_index;
} ppcp_pending_capture;

typedef struct ppcp_body_session_resume {
    ppcp_id              session_id;
    ppcp_id              peer_id;
    ppcp_id              minted_shots[PPCP_MAX_MINTED_SHOTS];
    size_t               minted_shot_count;
    ppcp_pending_capture pending[PPCP_MAX_PENDING];
    size_t               pending_count;
} ppcp_body_session_resume;

typedef struct ppcp_body_session_state {
    ppcp_id            session_id;
    ppcp_session_state state;
    ppcp_completeness  completeness;   /* 4.4a / I10: asserted by the owner */
} ppcp_body_session_state;

typedef struct ppcp_body_context_change { ppcp_context_change context; }
    ppcp_body_context_change;

typedef struct ppcp_body_session_close {
    ppcp_id session_id;
    ppcp_id reason;
} ppcp_body_session_close;

typedef struct ppcp_body_stream_open { ppcp_stream stream; } ppcp_body_stream_open;

typedef enum ppcp_stream_verdict { PPCP_STREAM_OPENED = 0, PPCP_STREAM_REFUSED }
    ppcp_stream_verdict;

typedef struct ppcp_body_stream_open_ack {
    ppcp_id             stream_id;
    ppcp_stream_verdict verdict;
    bool                has_reason;
    ppcp_id             reason;
    bool                has_opened_at;
    ppcp_instant        opened_at;
} ppcp_body_stream_open_ack;

typedef struct ppcp_body_stream_close {
    ppcp_id      stream_id;
    /* ⚠ CORE 5.11 has `Stream.closed_at` at cardinality 0..1 and MSG §11 lists
     * it as a field of `stream_close` with no marking; 5.1d lets EITHER peer
     * close, and a consumer closing a Stream it does not own has no reading of
     * the owner's timebase to put here.  Erratum queued (F-H4-2): either
     * 5.11a1 names the closing peer's timebase, or `closed_at` is optional.
     * Until it is settled the library ENCODES it when present and TOLERATES
     * its absence on receipt, which is the only reading under which a
     * consumer-originated close is expressible at all. */
    bool         has_closed_at;
    ppcp_instant closed_at;
    ppcp_id      reason;   /* 5.1d: thermal_limit, storage_full, not_needed, … */
} ppcp_body_stream_close;

/* `arm` and `disarm` share a shape: an EMPTY list means every open capture
 * Stream (MSG 5.2). */
typedef struct ppcp_body_stream_ids {
    ppcp_id stream_ids[PPCP_MAX_STREAM_IDS];
    size_t  stream_id_count;
} ppcp_body_stream_ids;

typedef struct ppcp_body_readiness {
    ppcp_readiness readiness;
    ppcp_id        stream_ids[PPCP_MAX_STREAM_IDS];
    size_t         stream_id_count;
} ppcp_body_readiness;

/* MSG 5.5 / 5.6 — each carries its entity VERBATIM: the body's keys are the
 * entity's own keys, flat.  The struct nests only so the union arm has a name. */
typedef struct ppcp_body_device_status {
    ppcp_device_status status;
} ppcp_body_device_status;

typedef struct ppcp_body_buffer_status {
    ppcp_buffer_margin margin;
} ppcp_body_buffer_status;

typedef struct ppcp_body_interruption {
    ppcp_id       kind;
    ppcp_interval interval;
    bool          recovered;
    ppcp_id       stream_ids[PPCP_MAX_STREAM_IDS];
    size_t        stream_id_count;
} ppcp_body_interruption;

typedef struct ppcp_body_heartbeat { uint64_t seq; } ppcp_body_heartbeat;

typedef struct ppcp_body_heartbeat_ack {
    uint64_t           seq;
    ppcp_thermal_level thermal;         /* 5.4b: first-class, so degradation is reported */
    bool               has_vendor_label;
    ppcp_id            vendor_thermal_label;
    uint64_t           storage_free_bytes;
    bool               has_battery_pct;
    uint32_t           battery_pct;
    bool               has_charging;
    bool               charging;
} ppcp_body_heartbeat_ack;

typedef struct ppcp_body_sync_probe {
    uint64_t     probe_seq;
    ppcp_id      timebase_id;   /* 6.1d: one probe sequence per timebase (I21) */
    ppcp_instant t1;
} ppcp_body_sync_probe;

typedef struct ppcp_body_sync_reply {
    uint64_t     probe_seq;
    ppcp_instant t1;   /* 6.1a: echoed unmodified, including its `tb` */
    ppcp_instant t2;   /* 6.1b: t2 and t3 in the SAME responder timebase */
    ppcp_instant t3;
} ppcp_body_sync_reply;

typedef struct ppcp_body_sync_residual {
    ppcp_id shot_id;
    ppcp_id timebase_id;
    int64_t residual_ns;
    ppcp_id basis;
} ppcp_body_sync_residual;

typedef struct ppcp_body_candidate { ppcp_candidate candidate; } ppcp_body_candidate;
typedef struct ppcp_body_shot      { ppcp_shot shot; }           ppcp_body_shot;

typedef struct ppcp_body_capture_request {
    ppcp_id          shot_id;
    ppcp_instant     t0;
    ppcp_id          stream_ids[PPCP_MAX_STREAM_IDS];
    size_t           stream_id_count;
    ppcp_duration_ns pre_ns;
    ppcp_duration_ns post_ns;
} ppcp_body_capture_request;

#define PPCP_THUMBNAIL_MAX 65536   /* MSG 8.1d / ENC §8 */

typedef struct ppcp_body_capture_announce {
    ppcp_capture   capture;
    bool           has_thumbnail;
    ppcp_id        thumbnail_format;
    const uint8_t *thumbnail;      /* points into the caller's frame buffer */
    size_t         thumbnail_len;
} ppcp_body_capture_announce;

typedef struct ppcp_body_capture_update {
    ppcp_id             capture_id;
    bool                has_completeness; ppcp_completeness completeness;
    bool                has_transfer;     ppcp_transfer_state transfer;
    ppcp_interval       gaps[PPCP_CAPTURE_MAX_GAPS];
    size_t              gap_count;
    ppcp_digest         digest;
    /* 8.2b / I30's one exception: `achieved_frames` MAY ride here for a
     * Capture whose payload will not transfer — a `complete` + `failed` clip
     * whose frame timeline is what tells a consumer what it lost. */
    bool                 has_achieved_frames;
    ppcp_achieved_frames achieved_frames;
} ppcp_body_capture_update;

typedef struct ppcp_body_capture_committed {
    ppcp_id     capture_id;
    ppcp_digest digest;
} ppcp_body_capture_committed;

typedef struct ppcp_body_annotation { ppcp_annotation annotation; } ppcp_body_annotation;

typedef struct ppcp_body_payload_begin {
    ppcp_id              capture_id;
    uint64_t             bytes;
    ppcp_digest          digest;     /* 8.1e: MUST be present by payload_begin */
    uint32_t             chunk_bytes;
    /* ENC 6g / MSG 8.3h (erratum E15): the IANA media type of the payload
     * bytes — "video/quicktime", "audio/wav".  Present whenever the bytes are a
     * container-framed file; absent only for raw samples the Stream's profile
     * describes in full.  ENC 6h forbids inferring it from `format.codec`, from
     * `Stream.kind` or by sniffing: H.264 lives in QuickTime, in fragmented MP4
     * and in Annex B, and those are three different files. */
    bool                 has_container;
    ppcp_id              container;
    bool                 has_achieved_frames;   /* 8.3g: a camera Capture carries it */
    ppcp_achieved_frames achieved_frames;
} ppcp_body_payload_begin;

typedef struct ppcp_body_payload_chunk {
    ppcp_id        capture_id;
    uint32_t       index;
    uint64_t       offset;    /* ENC 6b: index x chunk_bytes, for every chunk */
    const uint8_t *data;      /* points into the caller's frame buffer */
    size_t         data_len;
    ppcp_digest    digest;    /* ENC 6c: SHA-256 of `data` only */
} ppcp_body_payload_chunk;

typedef struct ppcp_body_payload_ack { ppcp_id capture_id; uint32_t index; }
    ppcp_body_payload_ack;
typedef struct ppcp_body_payload_end { ppcp_id capture_id; ppcp_digest digest; }
    ppcp_body_payload_end;
typedef struct ppcp_body_payload_abort { ppcp_id capture_id; ppcp_id reason; }
    ppcp_body_payload_abort;
typedef struct ppcp_body_payload_resume { ppcp_id capture_id; uint32_t from_index; }
    ppcp_body_payload_resume;

typedef struct ppcp_body_session_offer {
    ppcp_id           session_id;
    ppcp_id           minting_peer_id;
    ppcp_epoch        epoch;
    ppcp_completeness completeness;
    bool              has_bytes_estimate;
    uint64_t          bytes_estimate;
} ppcp_body_session_offer;

typedef enum ppcp_offer_verdict {
    PPCP_OFFER_ACCEPT = 0,
    PPCP_OFFER_ALREADY_HELD,
    PPCP_OFFER_REFUSE
} ppcp_offer_verdict;

typedef struct ppcp_body_session_accept {
    ppcp_id            session_id;
    ppcp_offer_verdict verdict;
    bool               has_reason;
    ppcp_id            reason;
    ppcp_digest        have_digests[PPCP_MAX_HAVE_DIGESTS];
    size_t             have_digest_count;
} ppcp_body_session_accept;

typedef struct ppcp_manifest_entry {
    ppcp_id     capture_id;
    ppcp_digest digest;
    uint64_t    bytes;
    ppcp_id     stream_id;
} ppcp_manifest_entry;

typedef struct ppcp_body_session_manifest {
    ppcp_id             session_id;
    ppcp_id             streams[PPCP_MAX_STREAM_IDS];
    size_t              stream_count;
    ppcp_manifest_entry captures[PPCP_MAX_MANIFEST];
    size_t              capture_count;
    ppcp_completeness   completeness;
    uint64_t            count_shots;
    uint64_t            count_candidates;
    uint64_t            count_captures;
} ppcp_body_session_manifest;

typedef struct ppcp_body_shot_link    { ppcp_shot_link link; }    ppcp_body_shot_link;
typedef struct ppcp_body_session_link { ppcp_session_link link; } ppcp_body_session_link;

#define PPCP_ERROR_MESSAGE_MAX 256

/* ------------------------------------------- MSG §12 — actuator control
 *
 * Erratum E58 — CR-02.  I39, as extended by E63, is carried by
 * ppcp_actuator_setting: one type, two constructors, no third shape.  Nothing
 * below re-checks "never neither, never both" because nothing below can build
 * a value that breaks it.
 */
typedef struct ppcp_body_actuator_command {
    ppcp_id               actuator_id;
    ppcp_actuator_setting setting;   /* the body's own `on` / `level`, flat */
} ppcp_body_actuator_command;

typedef enum ppcp_actuator_verdict { PPCP_ACTUATOR_APPLIED = 0, PPCP_ACTUATOR_REFUSED }
    ppcp_actuator_verdict;

typedef struct ppcp_body_actuator_command_ack {
    ppcp_id               actuator_id;
    ppcp_actuator_verdict verdict;
    bool                  has_reason;   /* 12.1b: present iff `refused` */
    ppcp_id               reason;       /* no_actuator, busy, thermal_limit, … */
    /* 12.1c: what the Actuator is ACTUALLY doing after the command applied —
     * never an echo of the request.  Present iff `applied` (12.1b, 12.1c1). */
    bool                  has_state;
    ppcp_actuator_setting state;
} ppcp_body_actuator_command_ack;

typedef struct ppcp_body_actuator_state {
    ppcp_id               actuator_id;
    ppcp_actuator_setting state;
    ppcp_instant          since;
} ppcp_body_actuator_state;

typedef struct ppcp_body_error {
    ppcp_id  code;
    char     message[PPCP_ERROR_MESSAGE_MAX + 1];
    size_t   message_len;
    bool     has_in_reply_to;
    uint64_t in_reply_to;
    /* 10.1f: `unsupported_version` MUST carry `detail.supported` — the
     * sender's full supported range — so the receiving peer can tell its user
     * WHICH END IS STALE.  Without it the code is fatal and uninformative. */
    bool     has_detail_supported;
    ppcp_id  detail_supported[PPCP_MAX_VERSIONS];
    size_t   detail_supported_count;
    bool     has_detail_reason;
    ppcp_id  detail_reason;
} ppcp_body_error;

/* ------------------------------------------------------- the tagged union */

typedef struct ppcp_msg {
    ppcp_envelope  env;
    ppcp_msg_type  type;
    /* The wire spelling as it arrived, so an UNKNOWN type is still reportable
     * and still ignorable rather than fatal (I13, MSG 1b). */
    ppcp_id        type_name;
    /* Tagged rather than unnamed so the Swift importer gives it a stable name
     * (`ppcp_msg_body`) instead of a synthesised `__Unnamed_union_body`.
     * Plan A5 makes this header a Swift-facing artefact; that costs one word
     * here and saves a rename in CaptureCore later. */
    union ppcp_msg_body {
        ppcp_body_link_bind          link_bind;
        ppcp_body_hello              hello;
        ppcp_body_hello_accept       hello_accept;
        ppcp_body_declare            declare;
        ppcp_body_declare_ack        declare_ack;
        ppcp_body_relation_update    relation_update;
        ppcp_body_calibration_update calibration_update;
        ppcp_body_discontinuity      discontinuity;
        ppcp_body_session_open       session_open;
        ppcp_body_session_joined     session_joined;
        ppcp_body_session_resume     session_resume;
        ppcp_body_session_state      session_state;
        ppcp_body_context_change     context_change;
        ppcp_body_session_close      session_close;
        ppcp_body_stream_open        stream_open;
        ppcp_body_stream_open_ack    stream_open_ack;
        ppcp_body_stream_close       stream_close;
        ppcp_body_stream_ids         arm;          /* and disarm */
        ppcp_body_readiness          readiness;
        ppcp_body_interruption       interruption;
        ppcp_body_heartbeat          heartbeat;
        ppcp_body_heartbeat_ack      heartbeat_ack;
        ppcp_body_sync_probe         sync_probe;
        ppcp_body_sync_reply         sync_reply;
        ppcp_body_sync_residual      sync_residual;
        ppcp_body_candidate          candidate;
        ppcp_body_shot               shot;
        ppcp_body_capture_request    capture_request;
        ppcp_body_capture_announce   capture_announce;
        ppcp_body_capture_update     capture_update;
        ppcp_body_capture_committed  capture_committed;
        ppcp_body_annotation         annotation;
        ppcp_body_payload_begin      payload_begin;
        ppcp_body_payload_chunk      payload_chunk;
        ppcp_body_payload_ack        payload_ack;
        ppcp_body_payload_end        payload_end;
        ppcp_body_payload_abort      payload_abort;
        ppcp_body_payload_resume     payload_resume;
        ppcp_body_session_offer      session_offer;
        ppcp_body_session_accept     session_accept;
        ppcp_body_session_manifest   session_manifest;
        ppcp_body_shot_link          shot_link;
        ppcp_body_session_link       session_link;
        ppcp_body_error              error;
        ppcp_body_device_status        device_status;
        ppcp_body_buffer_status        buffer_status;
        ppcp_body_actuator_command     actuator_command;
        ppcp_body_actuator_command_ack actuator_command_ack;
        ppcp_body_actuator_state       actuator_state;
    } body;
} ppcp_msg;

/* Decodes one frame payload — envelope and body together.
 *
 * `arena` carries the nested lists of `declare` (Sources and their profiles)
 * and of `payload_begin`'s AchievedFrames; every other message fits inline.
 * A NULL arena is legal and those two then report PPCP_ERR_LIMIT rather than
 * writing somewhere they were not given.
 *
 * An unknown `type` is PPCP_OK with `type == PPCP_MT_UNKNOWN` and the body
 * untouched: MSG 1b forbids closing the transport on one, and I13 makes it
 * ignorable rather than fatal. */
PPCP_API ppcp_result ppcp_msg_decode(const uint8_t *payload, size_t len,
                                     ppcp_cbor_limits lim, ppcp_arena *arena,
                                     ppcp_msg *out);

/* Encodes one whole frame — header, envelope and body — into `out`.
 * `m->env` supplies `type` is ignored: the type comes from `m->type`, so a
 * body and a type name cannot disagree. */
PPCP_API ppcp_result ppcp_msg_encode(uint8_t *out, size_t cap, uint8_t channel,
                                     const ppcp_msg *m, size_t *out_written);

/* Fills the envelope for a message of type `t` with `msg_id`, and sets
 * `m->type`.  The one constructor, so a caller cannot spell a type wrong. */
PPCP_API ppcp_result ppcp_msg_init(ppcp_msg *m, ppcp_msg_type t, uint64_t msg_id);
PPCP_API ppcp_result ppcp_msg_set_session_id(ppcp_msg *m, const char *session_id);
PPCP_API ppcp_result ppcp_msg_set_reply_to(ppcp_msg *m, uint64_t reply_to);

/* MSG §2 — is this message allowed on this channel?  2a and 2b are two rules,
 * not one: no `payload_chunk` on control, and no control message on bulk.
 * `link_bind` is not a control message (2b) and is legal on every channel. */
PPCP_API ppcp_result ppcp_msg_check_channel(ppcp_msg_type t, uint8_t channel);

/* CONF 5b1 / I24 — does a peer declaring these profiles confer origination of
 * this message?  Comprehension is never gated; this answers only the other
 * question. */
PPCP_API bool ppcp_msg_profiles_confer(ppcp_msg_type t, const ppcp_id *profiles,
                                       size_t profile_count);

#ifdef __cplusplus
}
#endif
#endif /* PPCP_MESSAGE_H */
