/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * ppcp-fixtures — writes the checked-in bundle fixtures of PPCP-CONF §2b.
 * Work package L15.
 *
 * WHY A TOOL AND NOT A TEST.  CONF 2b makes the fixture format the bundle
 * container of ENC §7 — "there is no second format", and a recorded range
 * session is a regression fixture at no additional cost.  A fixture that a test
 * builds in memory and reads back in the same process proves the codec agrees
 * with itself; a fixture that is WRITTEN by this tool, CHECKED IN, and read
 * back by a test built at a later commit proves the format did not move.  So
 * the bytes live in `tests/fixtures/` under version control, and
 * `tests/test_fixtures.c` reads them from disk.
 *
 * The encoding is deterministic (ENC 4e), so re-running this tool over a clean
 * checkout must produce byte-identical files.  `ctest -R L15-fixtures-stable`
 * is that assertion: it regenerates into a temporary directory and compares.  A
 * fixture that changed is either a codec change that wants explaining or a
 * determinism bug, and both should be loud.
 *
 * Usage:  ppcp-fixtures DIRECTORY
 */
#include "ppcp/ppcp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAP (256u * 1024u)

typedef struct buf { uint8_t b[CAP]; size_t n; } buf;

static void die(const char *what)
{
    fprintf(stderr, "ppcp-fixtures: %s\n", what);
    exit(1);
}

static ppcp_bundle_writer *new_writer(void **mem)
{
    ppcp_bundle_writer *w = NULL;
    *mem = malloc(ppcp_bundle_writer_sizeof());
    if (*mem == NULL) die("out of memory");
    if (ppcp_bundle_writer_new(*mem, ppcp_bundle_writer_sizeof(), &w) != PPCP_OK)
        die("could not construct a bundle writer");
    return w;
}

static void emit(ppcp_bundle_writer *w, buf *o, uint8_t ch, const ppcp_msg *m)
{
    size_t      n = 0;
    ppcp_result rc = ppcp_bundle_writer_append_msg(w, ch, m, o->b + o->n, CAP - o->n, &n);
    if (rc != PPCP_OK) {
        fprintf(stderr, "ppcp-fixtures: message type %d would not append: rc=%d\n",
                (int)m->type, (int)rc);
        die("a message would not append to the bundle");
    }
    o->n += n;
}

static void emit_frames(ppcp_bundle_writer *w, buf *o, const uint8_t *f, size_t len)
{
    size_t n = 0;
    if (ppcp_bundle_writer_append_frames(w, f, len, o->b + o->n, CAP - o->n, &n) != PPCP_OK)
        die("a raw frame would not append to the bundle");
    o->n += n;
}

static void begin(ppcp_bundle_writer *w, buf *o)
{
    size_t n = 0;
    o->n = 0;
    if (ppcp_bundle_writer_begin(w, o->b, CAP, &n) != PPCP_OK)
        die("could not write the ENC §7 header");
    o->n = n;
}

static void write_file(const char *dir, const char *name, const buf *o, size_t len)
{
    char  path[1024];
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "wb");
    if (f == NULL) die("could not open a fixture for writing");
    if (fwrite(o->b, 1, len, f) != len) die("short write");
    fclose(f);
    printf("%-32s %zu bytes\n", name, len);
}

static ppcp_instant inst(const char *tb, int64_t ns)
{
    ppcp_instant i;
    memset(&i, 0, sizeof(i));
    if (ppcp_instant_make_z(&i, tb, ns) != PPCP_OK) die("bad instant");
    return i;
}

static ppcp_digest dig(uint8_t fill)
{
    ppcp_digest d;
    uint8_t     v[PPCP_SHA256_BYTES];
    memset(&d, 0, sizeof(d));
    memset(v, fill, sizeof(v));
    if (ppcp_digest_set(&d, v) != PPCP_OK) die("bad digest");
    return d;
}

/* ------------------------------------------------------------ declaration */

typedef struct decl {
    ppcp_id              profiles[4];
    ppcp_timebase        tb[2];
    size_t               tb_count;
    ppcp_capture_profile cp[1];
    ppcp_source          src[1];
    ppcp_peer_desc       peer;
} decl;

/* `src_kind` is a Source `kind`, which CORE 5.6 makes an OPEN registry — so
 * "com.example.doppler_radar" below is a legal declaration and not a malformed
 * one, which is the half of I13 a closed enumeration would have hidden. */
static void build_decl(decl *d, const char *peer_id, const char *tb_id,
                       const char *src_kind, bool with_wall)
{
    static const char *const prof[] = {
        PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE, PPCP_PROFILE_DETECT, PPCP_PROFILE_OFFLINE
    };
    ppcp_timing   timing;
    ppcp_geometry geom;
    size_t        i;

    memset(d, 0, sizeof(*d));
    for (i = 0; i < 4; i++)
        if (ppcp_id_set_z(&d->profiles[i], prof[i]) != PPCP_OK) die("bad profile");
    if (ppcp_timebase_make(&d->tb[0], tb_id, strlen(tb_id), PPCP_TB_CONTINUOUS, true, 1000)
        != PPCP_OK) die("bad timebase");
    d->tb_count = 1;
    if (with_wall) {
        /* CORE 5.3b / I15: a `wall` timebase is declarable and unusable for any
         * interval.  Declaring one is the only way to test that nothing
         * subtracts on it. */
        if (ppcp_timebase_make(&d->tb[1], "tb:wall", 7, PPCP_TB_WALL, false, 1000000)
            != PPCP_OK) die("bad wall timebase");
        d->tb_count = 2;
    }
    if (ppcp_timing_make_nominal_frame_start(&timing, 120000, PPCP_PROV_ASSUMED) != PPCP_OK)
        die("bad timing");
    if (ppcp_geometry_make_rolling_shutter(&geom, 8000000, PPCP_PROV_ASSUMED,
                                           PPCP_ROLL_TOP_TO_BOTTOM, 1080) != PPCP_OK)
        die("bad geometry");
    if (ppcp_capture_profile_make(&d->cp[0], "cp:1", &timing) != PPCP_OK) die("bad profile");
    if (ppcp_capture_profile_set_camera(&d->cp[0], &geom, PPCP_INTR_PER_FRAME) != PPCP_OK)
        die("bad camera profile");
    if (ppcp_source_make(&d->src[0], "src:1", peer_id, src_kind, tb_id, true, d->cp, 1)
        != PPCP_OK) die("bad source");
    if (ppcp_peer_desc_make(&d->peer, peer_id, PPCP_ROLE_CAPTURE, "1.0", d->profiles, 4,
                            d->tb, d->tb_count) != PPCP_OK) die("bad peer desc");
    if (ppcp_peer_desc_set_sources(&d->peer, d->src, 1) != PPCP_OK) die("bad sources");
}

/* The opening three frames every fixture shares: a HOSTLESS `session_open`
 * (CORE 4.1b — the device records its own), the declaration, and a Stream. */
static void preamble(ppcp_bundle_writer *w, buf *o, decl *d, const char *src_kind,
                     const char *stream_kind, ppcp_continuity win, bool with_wall)
{
    ppcp_session s;
    ppcp_msg     m;

    if (ppcp_session_make_hostless(&s, "sess:fixture", "tb:dev") != PPCP_OK)
        die("bad session");
    if (ppcp_msg_init(&m, PPCP_MT_SESSION_OPEN, 1) != PPCP_OK) die("msg");
    m.body.session_open.session_id   = s.id;
    m.body.session_open.timebase_ref = s.timebase_ref;
    emit(w, o, PPCP_CHANNEL_CONTROL, &m);

    build_decl(d, "peer:dev", "tb:dev", src_kind, with_wall);
    if (ppcp_msg_init(&m, PPCP_MT_DECLARE, 2) != PPCP_OK) die("msg");
    m.body.declare.generation = 1;
    m.body.declare.peer       = d->peer;
    emit(w, o, PPCP_CHANNEL_CONTROL, &m);

    if (stream_kind != NULL) {
        ppcp_stream  st;
        ppcp_instant at = inst("tb:dev", 1000);
        if (ppcp_stream_make(&st, "st:1", "sess:fixture", "src:1", stream_kind, "cp:1",
                             "tb:dev", win, &at) != PPCP_OK) die("bad stream");
        if (ppcp_msg_init(&m, PPCP_MT_STREAM_OPEN, 3) != PPCP_OK) die("msg");
        m.body.stream_open.stream = st;
        emit(w, o, PPCP_CHANNEL_CONTROL, &m);
    }
}

static void manifest(ppcp_bundle_writer *w, buf *o, bool with_stream, size_t captures,
                     uint64_t msg_id)
{
    ppcp_msg m;
    if (ppcp_msg_init(&m, PPCP_MT_SESSION_MANIFEST, msg_id) != PPCP_OK) die("msg");
    if (ppcp_id_set_z(&m.body.session_manifest.session_id, "sess:fixture") != PPCP_OK)
        die("id");
    if (with_stream) {
        if (ppcp_id_set_z(&m.body.session_manifest.streams[0], "st:1") != PPCP_OK) die("id");
        m.body.session_manifest.stream_count = 1;
    }
    if (captures > 0) {
        if (ppcp_id_set_z(&m.body.session_manifest.captures[0].capture_id, "cap:1")
            != PPCP_OK) die("id");
        m.body.session_manifest.captures[0].digest = dig(0x33);
        m.body.session_manifest.captures[0].bytes  = 6;
        if (ppcp_id_set_z(&m.body.session_manifest.captures[0].stream_id, "st:1") != PPCP_OK)
            die("id");
        m.body.session_manifest.capture_count  = 1;
        m.body.session_manifest.count_captures = (uint32_t)captures;
    }
    m.body.session_manifest.completeness = PPCP_UNKNOWN;
    emit(w, o, PPCP_CHANNEL_CONTROL, &m);
}

/* ============================================================ the fixtures */

/* CT-I12 — "a video-only bundle, an IMU-only bundle and an empty-stream Session
 * all load and are valid".  The video one also carries CT-I34's two awkward
 * Captures: a `complete` one whose transfer is still `pending` and therefore
 * has NO digest, and an `absent` one that will never have one.  An importer
 * keyed on the digest duplicates both, which is the mistake I34 exists to
 * catch. */
static void fixture_i12(const char *dir, const char *name, const char *stream_kind)
{
    void *wm = NULL;
    ppcp_bundle_writer *w = new_writer(&wm);
    static buf o;
    decl  d;
    ppcp_msg m;

    begin(w, &o);
    preamble(w, &o, &d, "camera", stream_kind, PPCP_SHOT_WINDOWED, false);
    if (stream_kind != NULL) {
        if (ppcp_msg_init(&m, PPCP_MT_CAPTURE_ANNOUNCE, 5) != PPCP_OK) die("msg");
        if (ppcp_msg_set_session_id(&m, "sess:fixture") != PPCP_OK) die("id");
        if (ppcp_capture_make_shot(&m.body.capture_announce.capture, "cap:1", "shot:1",
                                   "st:1", PPCP_COMPLETE) != PPCP_OK) die("capture");
        emit(w, &o, PPCP_CHANNEL_CONTROL, &m);

        if (ppcp_msg_init(&m, PPCP_MT_CAPTURE_ANNOUNCE, 6) != PPCP_OK) die("msg");
        if (ppcp_msg_set_session_id(&m, "sess:fixture") != PPCP_OK) die("id");
        if (ppcp_capture_make_shot(&m.body.capture_announce.capture, "cap:gone", "shot:2",
                                   "st:1", PPCP_ABSENT) != PPCP_OK) die("capture");
        if (ppcp_capture_set_absent_reason(&m.body.capture_announce.capture,
                                           PPCP_ABSENT_OUTSIDE_BUFFER) != PPCP_OK)
            die("absent reason");
        emit(w, &o, PPCP_CHANNEL_CONTROL, &m);
    }
    manifest(w, &o, stream_kind != NULL, stream_kind != NULL ? 1u : 0u, 7);
    if (ppcp_bundle_writer_finish(w) != PPCP_OK) die("finish");
    write_file(dir, name, &o, o.n);
    free(wm);
}

/* CT-I2 and CT-I11 in one Session, because they are the two halves of the same
 * mistake and a reader that gets one right and the other wrong is the case
 * worth having a fixture for.
 *
 *   I2   `AchievedFrames.frames` is an explicit Series with a DROPPED sample in
 *        the middle — the spacing is 8 333 333 ns except across the gap, where
 *        it is twice that.  A consumer that reconstructed frame times from
 *        index and rate would produce the uniform sequence and be wrong from
 *        the gap onwards, silently.
 *   I11  the Capture declares an explicit `gap` interval on a `continuous`
 *        Stream: data was LOST inside a segment that otherwise exists, which is
 *        a different fact from an `absent` segment nobody retained (5.11c3). */
static void fixture_i2_i11(const char *dir)
{
    void *wm = NULL;
    ppcp_bundle_writer *w = new_writer(&wm);
    static buf o;
    decl  d;
    ppcp_msg m;
    static int64_t frames[8];
    ppcp_achieved_frames af;
    ppcp_per_frame_i64   exposure;
    ppcp_interval        gap;
    ppcp_instant         seg_from, seg_to;
    size_t i;

    /* 120 fps, and frame index 4 never arrived. */
    for (i = 0; i < 4; i++)
        frames[i] = 1000000000LL + (int64_t)i * 8333333LL;
    for (i = 4; i < 8; i++)
        frames[i] = 1000000000LL + (int64_t)(i + 1) * 8333333LL;

    begin(w, &o);
    preamble(w, &o, &d, "camera", PPCP_STREAM_KIND_VIDEO, PPCP_CONTINUOUS, false);

    if (ppcp_msg_init(&m, PPCP_MT_CAPTURE_ANNOUNCE, 5) != PPCP_OK) die("msg");
    if (ppcp_msg_set_session_id(&m, "sess:fixture") != PPCP_OK) die("id");
    seg_from = inst("tb:dev", 1000000000LL);
    seg_to   = inst("tb:dev", 1000000000LL + 9LL * 8333333LL);
    {
        ppcp_interval seg;
        memset(&seg, 0, sizeof(seg));
        seg.tb       = seg_from.tb;
        seg.start_ns = seg_from.ns;
        seg.end_ns   = seg_to.ns;
        if (ppcp_capture_make_segment(&m.body.capture_announce.capture, "cap:1", "st:1",
                                      PPCP_PARTIAL, &seg) != PPCP_OK)
            die("segment capture");
    }
    memset(&gap, 0, sizeof(gap));
    gap.tb       = seg_from.tb;
    gap.start_ns = frames[3] + 8333333LL;
    gap.end_ns   = frames[4];
    if (ppcp_capture_add_gap(&m.body.capture_announce.capture, &gap) != PPCP_OK)
        die("gap");
    emit(w, &o, PPCP_CHANNEL_CONTROL, &m);

    manifest(w, &o, true, 1, 6);

    /* The Series rides with the payload, which is where I30 puts per-frame
     * arrays: off the latency-critical control channel. */
    if (ppcp_msg_init(&m, PPCP_MT_PAYLOAD_BEGIN, 7) != PPCP_OK) die("msg");
    if (ppcp_id_set_z(&m.body.payload_begin.capture_id, "cap:1") != PPCP_OK) die("id");
    m.body.payload_begin.bytes       = 6;
    m.body.payload_begin.digest      = dig(0x33);
    m.body.payload_begin.chunk_bytes = 262144;   /* 8.3f's recommended value */
    if (ppcp_achieved_frames_make(&af, "tb:dev", frames, 8) != PPCP_OK) die("frames");
    memset(&exposure, 0, sizeof(exposure));
    exposure.form   = PPCP_PER_FRAME_SCALAR;
    exposure.scalar = 4000000;
    if (ppcp_achieved_frames_set_exposure(&af, &exposure, PPCP_EXP_LOCKED_CONSTANT)
        != PPCP_OK) die("exposure");
    m.body.payload_begin.has_achieved_frames = true;
    m.body.payload_begin.achieved_frames     = af;
    emit(w, &o, PPCP_CHANNEL_BULK, &m);

    if (ppcp_msg_init(&m, PPCP_MT_PAYLOAD_END, 8) != PPCP_OK) die("msg");
    if (ppcp_id_set_z(&m.body.payload_end.capture_id, "cap:1") != PPCP_OK) die("id");
    m.body.payload_end.digest = dig(0x33);
    emit(w, &o, PPCP_CHANNEL_BULK, &m);

    if (ppcp_bundle_writer_finish(w) != PPCP_OK) die("finish");
    write_file(dir, "ct-i2-i11-series-gap.ppcpb", &o, o.n);
    free(wm);
}

/* CT-I15 — a bundle whose WALL clock steps mid-session.  The step is real: the
 * two `context_change` frames are 3 600 000 000 000 ns apart on `tb:wall` and
 * 2 000 000 000 ns apart on `tb:dev`, so anything that subtracts on the wall
 * timebase gets an hour where a device clock says two seconds. */
static void fixture_i15(const char *dir)
{
    void *wm = NULL;
    ppcp_bundle_writer *w = new_writer(&wm);
    static buf o;
    decl  d;
    ppcp_msg m;

    begin(w, &o);
    preamble(w, &o, &d, "camera", PPCP_STREAM_KIND_VIDEO, PPCP_SHOT_WINDOWED, true);

    if (ppcp_msg_init(&m, PPCP_MT_CAPTURE_ANNOUNCE, 5) != PPCP_OK) die("msg");
    if (ppcp_msg_set_session_id(&m, "sess:fixture") != PPCP_OK) die("id");
    if (ppcp_capture_make_shot(&m.body.capture_announce.capture, "cap:1", "shot:1",
                               "st:1", PPCP_COMPLETE) != PPCP_OK) die("capture");
    emit(w, &o, PPCP_CHANNEL_CONTROL, &m);

    if (ppcp_msg_init(&m, PPCP_MT_CAPTURE_ANNOUNCE, 6) != PPCP_OK) die("msg");
    if (ppcp_msg_set_session_id(&m, "sess:fixture") != PPCP_OK) die("id");
    if (ppcp_capture_make_shot(&m.body.capture_announce.capture, "cap:2", "shot:2",
                               "st:1", PPCP_COMPLETE) != PPCP_OK) die("capture");
    emit(w, &o, PPCP_CHANNEL_CONTROL, &m);

    manifest(w, &o, true, 1, 7);
    if (ppcp_bundle_writer_finish(w) != PPCP_OK) die("finish");
    write_file(dir, "ct-i15-wall-step.ppcpb", &o, o.n);
    free(wm);
}

/* CT-I13 — "an unknown message type, an unknown map key at three nesting
 * levels, an unknown `Source.kind` and an unknown `Candidate.basis`.  Assert
 * none is fatal and the surrounding data survives."
 *
 * The unknown TYPE and the unknown KEYS cannot come out of this library's
 * encoder, which only writes what it knows — so they are hand-built here with
 * the public CBOR writer and appended as a raw frame.  That is the point: a
 * fixture whose forward-compatibility case a conformant encoder could produce
 * would not be testing forward compatibility. */
static void fixture_i13(const char *dir)
{
    void *wm = NULL;
    ppcp_bundle_writer *w = new_writer(&wm);
    static buf o;
    decl  d;
    ppcp_msg m;
    uint8_t  frame[1024];
    uint8_t  body[900];
    size_t   body_len = 0, frame_len = 0;
    ppcp_cbor_writer cw;

    begin(w, &o);
    /* The unknown Source kind is in the declaration: 5.6's registry is open, so
     * this is a legal peer and not a malformed one. */
    preamble(w, &o, &d, "com.example.doppler_radar", PPCP_STREAM_KIND_VIDEO,
             PPCP_SHOT_WINDOWED, false);

    /* An unknown message type carrying unknown keys at three nesting levels,
     * with a known field after each. */
    ppcp_cbor_writer_init(&cw, body, sizeof(body));
    ppcp_cbor_write_map(&cw, 5);
    ppcp_cbor_write_text_z(&cw, "t1");
    ppcp_cbor_write_map(&cw, 2);
    ppcp_cbor_write_text_z(&cw, "ns"); ppcp_cbor_write_int(&cw, 1723000000000LL);
    ppcp_cbor_write_text_z(&cw, "tb"); ppcp_cbor_write_text_z(&cw, "tb:dev");
    ppcp_cbor_write_text_z(&cw, "type");
    ppcp_cbor_write_text_z(&cw, "com.example.future_event");
    ppcp_cbor_write_text_z(&cw, "msg_id"); ppcp_cbor_write_uint(&cw, 40);
    ppcp_cbor_write_text_z(&cw, "unknown_a");
    ppcp_cbor_write_map(&cw, 2);
    ppcp_cbor_write_text_z(&cw, "k"); ppcp_cbor_write_uint(&cw, 1);
    ppcp_cbor_write_text_z(&cw, "unknown_b");
    ppcp_cbor_write_array(&cw, 2);
    ppcp_cbor_write_map(&cw, 1);
    ppcp_cbor_write_text_z(&cw, "unknown_c");
    ppcp_cbor_write_bool(&cw, true);
    ppcp_cbor_write_text_z(&cw, "trailing");
    ppcp_cbor_write_text_z(&cw, "unknown_z"); ppcp_cbor_write_int(&cw, -9);
    if (ppcp_cbor_writer_finish(&cw, &body_len) != PPCP_OK) die("cbor");

    if (ppcp_frame_write(frame, sizeof(frame), PPCP_CHANNEL_CONTROL, body, body_len,
                         &frame_len) != PPCP_OK)
        die("frame");
    emit_frames(w, &o, frame, frame_len);

    /* An unknown `Candidate.basis`, which 5.12's registry also leaves open. */
    if (ppcp_msg_init(&m, PPCP_MT_CANDIDATE, 41) != PPCP_OK) die("msg");
    {
        ppcp_instant at = inst("tb:dev", 1200000000LL);
        if (ppcp_candidate_make(&m.body.candidate.candidate, "cand:1", "peer:dev", "src:1",
                                "com.example.doppler_return", &at, 0.7) != PPCP_OK)
            die("candidate");
    }
    emit(w, &o, PPCP_CHANNEL_CONTROL, &m);

    /* A known frame AFTER all of it: "the surrounding data survives" is the
     * assertion, and it is only an assertion if something follows. */
    if (ppcp_msg_init(&m, PPCP_MT_CAPTURE_ANNOUNCE, 42) != PPCP_OK) die("msg");
    if (ppcp_msg_set_session_id(&m, "sess:fixture") != PPCP_OK) die("id");
    if (ppcp_capture_make_shot(&m.body.capture_announce.capture, "cap:1", "shot:1", "st:1",
                               PPCP_COMPLETE) != PPCP_OK) die("capture");
    emit(w, &o, PPCP_CHANNEL_CONTROL, &m);

    manifest(w, &o, true, 1, 43);
    if (ppcp_bundle_writer_finish(w) != PPCP_OK) die("finish");
    write_file(dir, "ct-i13-unknowns.ppcpb", &o, o.n);
    free(wm);
}

/* CT-I36 (c) and (d) — the same truncation with two different assertions in
 * front of it.  ENC 7d: a truncated tail in a Session asserted `partial` is the
 * declared incompleteness; the same truncation in a Session asserted `complete`
 * is a defect, and a reader must be able to tell them apart. */
static void fixture_i36(const char *dir, const char *name, ppcp_completeness asserted)
{
    void *wm = NULL;
    ppcp_bundle_writer *w = new_writer(&wm);
    static buf o;
    decl  d;
    ppcp_msg m;

    begin(w, &o);
    preamble(w, &o, &d, "camera", PPCP_STREAM_KIND_VIDEO, PPCP_SHOT_WINDOWED, false);

    if (ppcp_msg_init(&m, PPCP_MT_SESSION_STATE, 4) != PPCP_OK) die("msg");
    if (ppcp_id_set_z(&m.body.session_state.session_id, "sess:fixture") != PPCP_OK) die("id");
    m.body.session_state.state        = PPCP_SESSION_CLOSED;
    m.body.session_state.completeness = asserted;
    emit(w, &o, PPCP_CHANNEL_CONTROL, &m);

    if (ppcp_msg_init(&m, PPCP_MT_CAPTURE_ANNOUNCE, 5) != PPCP_OK) die("msg");
    if (ppcp_msg_set_session_id(&m, "sess:fixture") != PPCP_OK) die("id");
    if (ppcp_capture_make_shot(&m.body.capture_announce.capture, "cap:1", "shot:1", "st:1",
                               PPCP_COMPLETE) != PPCP_OK) die("capture");
    emit(w, &o, PPCP_CHANNEL_CONTROL, &m);

    manifest(w, &o, true, 1, 6);
    if (ppcp_bundle_writer_finish(w) != PPCP_OK) die("finish");
    /* Cut the last frame in half: what a transfer that died looks like. */
    write_file(dir, name, &o, o.n - 6);
    free(wm);
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : ".";
    if (argc > 2) {
        fputs("usage: ppcp-fixtures DIRECTORY\n", stderr);
        return 2;
    }
    fixture_i12(dir, "ct-i12-video.ppcpb", PPCP_STREAM_KIND_VIDEO);
    fixture_i12(dir, "ct-i12-imu.ppcpb",   PPCP_STREAM_KIND_IMU);
    fixture_i12(dir, "ct-i12-empty.ppcpb", NULL);
    fixture_i2_i11(dir);
    fixture_i15(dir);
    fixture_i13(dir);
    fixture_i36(dir, "ct-i36-truncated-partial.ppcpb",  PPCP_PARTIAL);
    fixture_i36(dir, "ct-i36-truncated-complete.ppcpb", PPCP_COMPLETE);
    return 0;
}
