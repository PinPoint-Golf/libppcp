/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * The peer engine, driven end to end — work package L6's evidence.
 *
 * Two engines are wired back to back through nothing but byte buffers, which
 * is the whole point of a sans-I/O library: the "transport" here is a memcpy,
 * and it is the same code path a TLS socket and a bundle file will drive.
 *
 * Rows this file carries:
 *
 *   CT-I24 / CT-S6 (1)(2)(3)  comprehension versus origination
 *   CT-I20                    a second `role: host` is refused, and only a
 *                             host arbitrates or arms
 *   CT-I5   (engine half)     a Stream's identity is fixed and either peer
 *                             may close it, without ending the Session
 *   CT-I14  (callback half)   the ingest decision is a function pointer; the
 *                             threshold is the embedding's
 *   CT-I16  (engine half)     `timebase_ref` is immutable on receipt
 *   ENC 2.1 (erratum E1)      link binding, including all three refusals
 */
#include "ppcp/peer.h"

#include "test_util.h"

/* ------------------------------------------------------------------ rigging */

static ppcp_peer *make_peer(void **storage, ppcp_role role, const char *id,
                            const char *const *profiles, size_t nprof, bool listener,
                            ppcp_ingest_policy_fn policy, void *ctx,
                            const char *const *versions, size_t nver,
                            const char *min_version)
{
    ppcp_peer_config cfg;
    ppcp_peer       *p = NULL;
    void            *mem;

    memset(&cfg, 0, sizeof(cfg));
    cfg.role          = role;
    cfg.peer_id       = id;
    cfg.profiles      = profiles;
    cfg.profile_count = nprof;
    cfg.listener      = listener;
    cfg.ingest_policy = policy;
    cfg.ctx           = ctx;
    cfg.versions      = versions;
    cfg.version_count = nver;
    cfg.min_version   = min_version;

    mem = malloc(ppcp_peer_sizeof());
    if (mem == NULL)
        abort();
    if (ppcp_peer_new(mem, ppcp_peer_sizeof(), &cfg, &p) != PPCP_OK)
        abort();
    *storage = mem;
    return p;
}

/* The transport: everything `from` has queued on `ch`, handed to `to`. */
static void pump(ppcp_peer *from, ppcp_peer *to, uint8_t ch)
{
    static uint8_t buf[131072];
    size_t         n = 0, consumed = 0;

    while (ppcp_peer_pending(from, ch) > 0) {
        if (ppcp_peer_drain(from, ch, buf, sizeof(buf), &n) != PPCP_OK || n == 0)
            break;
        (void)ppcp_peer_feed(to, ch, buf, n, &consumed);
        if (consumed != n)
            fprintf(stderr, "  pump: %zu of %zu bytes consumed\n", consumed, n);
    }
}

/* Drains every event and reports whether one of `kind` was seen, and the last
 * error code if an ERROR event went by. */
typedef struct seen {
    bool    kinds[PPCP_EVENT_UNKNOWN + 1];
    ppcp_id last_error;
    size_t  count;
} seen;

static void collect(ppcp_peer *p, seen *s)
{
    ppcp_event e;
    memset(s, 0, sizeof(*s));
    while (ppcp_peer_next_event(p, &e) == PPCP_OK) {
        s->count++;
        if ((size_t)e.kind <= (size_t)PPCP_EVENT_UNKNOWN)
            s->kinds[e.kind] = true;
        if (e.kind == PPCP_EVENT_ERROR && e.msg != NULL &&
            e.msg->type == PPCP_MT_ERROR)
            s->last_error = e.msg->body.error.code;
    }
}

static bool error_was(const seen *s, const char *code)
{
    return ppcp_cbor_key_is(s->last_error.v, s->last_error.len, code);
}

static ppcp_instant inst(const char *tb, int64_t ns)
{
    ppcp_instant i;
    memset(&i, 0, sizeof(i));
    if (ppcp_instant_make_z(&i, tb, ns) != PPCP_OK)
        abort();
    return i;
}

/* A declaration good enough to send: one timebase, one camera Source on it. */
typedef struct decl {
    ppcp_id              profiles[8];
    ppcp_timebase        tb[1];
    ppcp_capture_profile cp[1];
    ppcp_source          src[1];
    ppcp_peer_desc       peer;
} decl;

static void build_decl(decl *d, const char *peer_id, ppcp_role role,
                       const char *tb_id, const char *const *profiles, size_t nprof)
{
    ppcp_timing   timing;
    ppcp_geometry geom;
    size_t        i;

    memset(d, 0, sizeof(*d));
    for (i = 0; i < nprof; i++)
        if (ppcp_id_set_z(&d->profiles[i], profiles[i]) != PPCP_OK) abort();
    if (ppcp_timebase_make(&d->tb[0], tb_id, strlen(tb_id), PPCP_TB_CONTINUOUS, true, 1000)
        != PPCP_OK) abort();
    if (ppcp_timing_make_nominal_frame_start(&timing, 120000, PPCP_PROV_ASSUMED) != PPCP_OK)
        abort();
    if (ppcp_geometry_make_rolling_shutter(&geom, 8000000, PPCP_PROV_ASSUMED,
                                           PPCP_ROLL_TOP_TO_BOTTOM, 1080) != PPCP_OK) abort();
    if (ppcp_capture_profile_make(&d->cp[0], "cp:1", &timing) != PPCP_OK) abort();
    if (ppcp_capture_profile_set_camera(&d->cp[0], &geom, PPCP_INTR_PER_FRAME) != PPCP_OK)
        abort();
    if (ppcp_source_make(&d->src[0], "src:1", peer_id, "camera", tb_id, true, d->cp, 1)
        != PPCP_OK) abort();
    if (ppcp_peer_desc_make(&d->peer, peer_id, role, "1.0", d->profiles, nprof, d->tb, 1)
        != PPCP_OK) abort();
    if (ppcp_peer_desc_set_sources(&d->peer, d->src, 1) != PPCP_OK) abort();
}

static bool accept_all(void *ctx, const ppcp_peer_desc *c, ppcp_id *reason)
{
    (void)ctx; (void)c; (void)reason;
    return true;
}

static bool refuse_all(void *ctx, const ppcp_peer_desc *c, ppcp_id *reason)
{
    int *calls = (int *)ctx;
    (void)c;
    (*calls)++;
    /* I14: the threshold that produced this verdict is the embedding's and is
     * nowhere in the library.  All the protocol carries is a machine-readable
     * name for it (3.4a). */
    (void)ppcp_id_set_z(reason, "com.example.rate_floor");
    return false;
}

/* ============================================================ ENC §2.1 */

static void test_link_binding(void)
{
    static const char *const prof[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE };
    void        *mem = NULL;
    ppcp_peer   *dialler;
    ppcp_link_binder b;
    uint8_t      link_id[PPCP_LINK_ID_BYTES];
    uint8_t      buf[4096];
    size_t       n = 0, consumed = 0, link = 99, link2 = 99;

    memset(link_id, 0xc3, sizeof(link_id));
    dialler = make_peer(&mem, PPCP_ROLE_CAPTURE, "peer:dev", prof, 2, false,
                        NULL, NULL, NULL, 0, NULL);
    ppcp_link_binder_init(&b);

    TEST("2.1a — a listener never mints a link_id");
    {
        void      *m2 = NULL;
        ppcp_peer *listener = make_peer(&m2, PPCP_ROLE_HOST, "peer:host", prof, 2, true,
                                        NULL, NULL, NULL, 0, NULL);
        CHECK_EQ_I(ppcp_peer_set_link_id(listener, link_id), PPCP_ERR_INVALID);
        CHECK_EQ_I(ppcp_peer_open_channel(listener, PPCP_CHANNEL_BULK), PPCP_ERR_INVALID);
        ppcp_peer_free(listener);
        free(m2);
    }

    TEST("2.1d — `link_bind` precedes `hello` on channel 0, by construction");
    CHECK_EQ_I(ppcp_peer_set_link_id(dialler, link_id), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_hello(dialler), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_drain(dialler, PPCP_CHANNEL_CONTROL, buf, sizeof(buf), &n), PPCP_OK);
    CHECK_EQ_I(ppcp_link_binder_offer(&b, PPCP_CHANNEL_CONTROL, buf, n, &consumed, &link),
               PPCP_OK);
    CHECK(consumed < n);          /* the `hello` follows in the same drain */
    CHECK(ppcp_link_binder_is_ready(&b, link));
    CHECK_EQ_I(ppcp_link_binder_count(&b), 1);
    CHECK(memcmp(ppcp_link_binder_id(&b, link), link_id, PPCP_LINK_ID_BYTES) == 0);

    TEST("2.1d — a bulk channel joins the same link, later in the session");
    CHECK_EQ_I(ppcp_peer_open_channel(dialler, PPCP_CHANNEL_BULK), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_drain(dialler, PPCP_CHANNEL_BULK, buf, sizeof(buf), &n), PPCP_OK);
    CHECK_EQ_I(ppcp_link_binder_offer(&b, PPCP_CHANNEL_BULK, buf, n, &consumed, &link2),
               PPCP_OK);
    CHECK_EQ_I(link2, link);      /* the same link, not a second one */
    CHECK_EQ_I(ppcp_link_binder_count(&b), 1);
    CHECK(ppcp_link_binder_has_channel(&b, link, PPCP_CHANNEL_BULK));

    TEST("2.1c — a link_id already holding that channel is refused");
    CHECK_EQ_I(ppcp_link_binder_offer(&b, PPCP_CHANNEL_BULK, buf, n, &consumed, &link2),
               PPCP_ERR_MALFORMED);

    TEST("2.1c — a first frame that is not `link_bind` is refused");
    {
        void      *m3 = NULL;
        ppcp_peer *bare = make_peer(&m3, PPCP_ROLE_CAPTURE, "peer:x", prof, 2, false,
                                    NULL, NULL, NULL, 0, NULL);
        ppcp_link_binder b2;
        size_t           c2 = 0, l2 = 0;
        ppcp_link_binder_init(&b2);
        CHECK_EQ_I(ppcp_peer_hello(bare), PPCP_OK);     /* no link_id: no link_bind */
        CHECK_EQ_I(ppcp_peer_drain(bare, PPCP_CHANNEL_CONTROL, buf, sizeof(buf), &n),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_link_binder_offer(&b2, PPCP_CHANNEL_CONTROL, buf, n, &c2, &l2),
                   PPCP_ERR_MALFORMED);
        ppcp_peer_free(bare);
        free(m3);
    }

    TEST("2.1c — a `channel` disagreeing with the frame header is refused");
    {
        ppcp_msg m;
        size_t   written = 0, c2 = 0, l2 = 0;
        ppcp_link_binder b2;
        ppcp_link_binder_init(&b2);
        CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_LINK_BIND, 1), PPCP_OK);
        memcpy(m.body.link_bind.link_id, link_id, PPCP_LINK_ID_BYTES);
        m.body.link_bind.channel = PPCP_CHANNEL_BULK;          /* says 1 ... */
        CHECK_EQ_I(ppcp_msg_encode(buf, sizeof(buf), PPCP_CHANNEL_CONTROL, &m, &written),
                   PPCP_OK);                                    /* ... in a channel-0 frame */
        CHECK_EQ_I(ppcp_link_binder_offer(&b2, PPCP_CHANNEL_CONTROL, buf, written, &c2, &l2),
                   PPCP_ERR_MALFORMED);

        TEST("ENC 2c — the header's channel must match the stream it arrived on");
        m.body.link_bind.channel = PPCP_CHANNEL_CONTROL;
        CHECK_EQ_I(ppcp_msg_encode(buf, sizeof(buf), PPCP_CHANNEL_CONTROL, &m, &written),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_link_binder_offer(&b2, PPCP_CHANNEL_BULK, buf, written, &c2, &l2),
                   PPCP_ERR_MALFORMED);
    }

    TEST("2.1c — an unknown link_id opens a NEW link, and discard removes it");
    {
        ppcp_msg m;
        size_t   written = 0;
        uint8_t  other[PPCP_LINK_ID_BYTES];
        memset(other, 0x77, sizeof(other));
        CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_LINK_BIND, 1), PPCP_OK);
        memcpy(m.body.link_bind.link_id, other, PPCP_LINK_ID_BYTES);
        m.body.link_bind.channel = PPCP_CHANNEL_CONTROL;
        CHECK_EQ_I(ppcp_msg_encode(buf, sizeof(buf), PPCP_CHANNEL_CONTROL, &m, &written),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_link_binder_offer(&b, PPCP_CHANNEL_CONTROL, buf, written,
                                          &consumed, &link2), PPCP_OK);
        CHECK(link2 != link);
        CHECK_EQ_I(ppcp_link_binder_count(&b), 2);
        CHECK_EQ_I(ppcp_link_binder_discard(&b, link2), PPCP_OK);
        CHECK_EQ_I(ppcp_link_binder_count(&b), 1);
        CHECK(!ppcp_link_binder_is_ready(&b, link2));
    }

    ppcp_peer_free(dialler);
    free(mem);
}

/* ==================================================== the connection path */

static void test_handshake(void)
{
    static const char *const host_prof[] = {
        PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE, PPCP_PROFILE_ARBITRATE,
        PPCP_PROFILE_LIVE, PPCP_PROFILE_OFFLINE
    };
    static const char *const dev_prof[] = {
        PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE, PPCP_PROFILE_DETECT,
        PPCP_PROFILE_MINT, PPCP_PROFILE_LIVE, PPCP_PROFILE_OFFLINE
    };
    void      *hm = NULL, *dm = NULL;
    ppcp_peer *host, *dev;
    seen       s;
    int        refusals = 0;
    decl       hd, dd;

    host = make_peer(&hm, PPCP_ROLE_HOST, "peer:host", host_prof, 5, true,
                     accept_all, NULL, NULL, 0, NULL);
    dev  = make_peer(&dm, PPCP_ROLE_CAPTURE, "peer:dev", dev_prof, 6, false,
                     accept_all, NULL, NULL, 0, NULL);

    TEST("MSG §3 — hello / hello_accept agree a version at both ends");
    CHECK_EQ_I(ppcp_peer_hello(dev), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_get_state(dev), PPCP_PEER_HELLO_SENT);
    pump(dev, host, PPCP_CHANNEL_CONTROL);
    collect(host, &s);
    CHECK(s.kinds[PPCP_EVENT_HELLO]);
    CHECK(s.kinds[PPCP_EVENT_CONNECTED]);
    CHECK_EQ_I(ppcp_peer_get_state(host), PPCP_PEER_CONNECTED);
    pump(host, dev, PPCP_CHANNEL_CONTROL);
    collect(dev, &s);
    CHECK(s.kinds[PPCP_EVENT_CONNECTED]);
    CHECK_EQ_I(ppcp_peer_get_state(dev), PPCP_PEER_CONNECTED);
    CHECK(ppcp_peer_version(dev) != NULL && strcmp(ppcp_peer_version(dev), "1.0") == 0);
    CHECK(ppcp_peer_version(host) != NULL && strcmp(ppcp_peer_version(host), "1.0") == 0);

    TEST("MSG 3.3 — declaration is symmetric and neither side may skip it");
    build_decl(&dd, "peer:dev", PPCP_ROLE_CAPTURE, "tb:dev", dev_prof, 6);
    build_decl(&hd, "peer:host", PPCP_ROLE_HOST, "tb:host", host_prof, 5);
    CHECK_EQ_I(ppcp_peer_declare(dev, &dd.peer), PPCP_OK);
    pump(dev, host, PPCP_CHANNEL_CONTROL);
    collect(host, &s);
    CHECK(s.kinds[PPCP_EVENT_DECLARE]);
    CHECK(ppcp_peer_counterpart(host) != NULL);
    CHECK_EQ_I(ppcp_peer_counterpart(host)->source_count, 1);
    pump(host, dev, PPCP_CHANNEL_CONTROL);
    collect(dev, &s);
    CHECK(s.kinds[PPCP_EVENT_DECLARE_ACK]);
    CHECK_EQ_I(ppcp_peer_declare(host, &hd.peer), PPCP_OK);
    pump(host, dev, PPCP_CHANNEL_CONTROL);
    collect(dev, &s);
    CHECK(s.kinds[PPCP_EVENT_DECLARE]);
    pump(dev, host, PPCP_CHANNEL_CONTROL);
    (void)ppcp_peer_next_event(host, NULL);

    TEST("5.2a — a peer declaring the wrong id or role cannot declare at all");
    {
        decl wrong;
        build_decl(&wrong, "peer:someone_else", PPCP_ROLE_CAPTURE, "tb:dev", dev_prof, 6);
        CHECK_EQ_I(ppcp_peer_declare(dev, &wrong.peer), PPCP_ERR_INVALID);
    }

    TEST("CORE §7.2 / MSG §4 — the Session opens and the peer joins");
    {
        ppcp_session sess;
        CHECK_EQ_I(ppcp_session_make_hosted(&sess, "sess:1", "tb:host",
                                            PPCP_DEFAULT_COINCIDENCE_WINDOW_NS,
                                            PPCP_DEFAULT_ISSUE_HOLD_NS), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_session_open(host, &sess), PPCP_OK);
        pump(host, dev, PPCP_CHANNEL_CONTROL);
        collect(dev, &s);
        CHECK(s.kinds[PPCP_EVENT_SESSION_OPEN]);
        CHECK(ppcp_peer_session_id(dev) != NULL);
        CHECK(ppcp_cbor_key_is(ppcp_peer_timebase_ref(dev)->v,
                               ppcp_peer_timebase_ref(dev)->len, "tb:host"));
        pump(dev, host, PPCP_CHANNEL_CONTROL);
        collect(host, &s);
        CHECK(s.kinds[PPCP_EVENT_SESSION_JOINED]);
        CHECK_EQ_I(ppcp_peer_get_state(dev), PPCP_PEER_JOINED);
    }

    TEST("CT-I16 / 4.1a — `timebase_ref` is immutable, and a second one is refused");
    {
        ppcp_session sess2;
        CHECK_EQ_I(ppcp_session_make_hosted(&sess2, "sess:1", "tb:elsewhere",
                                            PPCP_DEFAULT_COINCIDENCE_WINDOW_NS,
                                            PPCP_DEFAULT_ISSUE_HOLD_NS), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_session_open(host, &sess2), PPCP_OK);
        pump(host, dev, PPCP_CHANNEL_CONTROL);
        /* unchanged, which is what the invariant is about: the Session was not
         * re-opened under a second reference clock. */
        CHECK(ppcp_cbor_key_is(ppcp_peer_timebase_ref(dev)->v,
                               ppcp_peer_timebase_ref(dev)->len, "tb:host"));
        CHECK(ppcp_peer_get_state(dev) != PPCP_PEER_CLOSED);
        /* the refusal reaches the sender, which is the end that got it wrong */
        pump(dev, host, PPCP_CHANNEL_CONTROL);
        collect(host, &s);
        CHECK(s.kinds[PPCP_EVENT_ERROR]);
        CHECK(error_was(&s, PPCP_ERRCODE_MALFORMED));
        /* MSG 10b: `malformed` is not fatal and both ends stay up. */
        CHECK(ppcp_peer_get_state(host) != PPCP_PEER_CLOSED);
        collect(dev, &s);
    }

    TEST("MSG §5 / CT-I5 — a Stream opens, is acknowledged, and either peer closes it");
    {
        ppcp_stream st;
        ppcp_instant at = inst("tb:dev", 1000);
        CHECK_EQ_I(ppcp_stream_make(&st, "st:1", "sess:1", "src:1",
                                    PPCP_STREAM_KIND_VIDEO, "cp:1", "tb:dev",
                                    PPCP_SHOT_WINDOWED, &at), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_stream_open(dev, &st), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_stream_count(dev), 1);
        pump(dev, host, PPCP_CHANNEL_CONTROL);
        collect(host, &s);
        CHECK(s.kinds[PPCP_EVENT_STREAM_OPEN]);
        CHECK_EQ_I(ppcp_peer_stream_count(host), 1);
        pump(host, dev, PPCP_CHANNEL_CONTROL);
        collect(dev, &s);
        CHECK(s.kinds[PPCP_EVENT_STREAM_OPEN_ACK]);

        /* 5.1a: the identity is fixed for its lifetime, so a second open of
         * the same id is not a re-open. */
        CHECK_EQ_I(ppcp_peer_stream_open(dev, &st), PPCP_ERR_INVALID);

        /* 5.1d: the CONSUMER closes it — it no longer wants the data — and
         * 5.1b says the Session does not end. */
        {
            ppcp_instant closed = inst("tb:dev", 9000);
            CHECK_EQ_I(ppcp_peer_stream_close(host, "st:1", &closed, "not_needed"),
                       PPCP_OK);
            CHECK_EQ_I(ppcp_peer_stream_count(host), 0);
            pump(host, dev, PPCP_CHANNEL_CONTROL);
            collect(dev, &s);
            CHECK(s.kinds[PPCP_EVENT_STREAM_CLOSE]);
            CHECK_EQ_I(ppcp_peer_stream_count(dev), 0);
            CHECK_EQ_I(ppcp_peer_get_state(dev), PPCP_PEER_JOINED);
            CHECK(ppcp_peer_session_id(dev) != NULL);
        }
    }

    TEST("CORE 7.3a — arm is host-controlled; a capture peer cannot arm itself");
    CHECK_EQ_I(ppcp_peer_arm(dev, NULL, 0), PPCP_ERR_INVALID);
    CHECK(!ppcp_peer_is_armed(dev));

    TEST("MSG 5.2 — an empty stream_ids list means every open capture Stream");
    CHECK_EQ_I(ppcp_peer_arm(host, NULL, 0), PPCP_OK);
    pump(host, dev, PPCP_CHANNEL_CONTROL);
    collect(dev, &s);
    CHECK(s.kinds[PPCP_EVENT_ARM]);
    CHECK(ppcp_peer_is_armed(dev));

    TEST("5.2a / 5.2b — readiness answers arm, and carries no state-machine name");
    {
        ppcp_readiness r;
        CHECK_EQ_I(ppcp_readiness_not_settled(&r, 250), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_readiness(dev, &r, NULL, 0), PPCP_OK);
        pump(dev, host, PPCP_CHANNEL_CONTROL);
        collect(host, &s);
        CHECK(s.kinds[PPCP_EVENT_READINESS]);
    }

    TEST("7.3e — arm and disarm cycle inside one open Session");
    CHECK_EQ_I(ppcp_peer_disarm(host, NULL, 0), PPCP_OK);
    pump(host, dev, PPCP_CHANNEL_CONTROL);
    collect(dev, &s);
    CHECK(s.kinds[PPCP_EVENT_DISARM]);
    CHECK(!ppcp_peer_is_armed(dev));
    CHECK_EQ_I(ppcp_peer_get_state(dev), PPCP_PEER_JOINED);

    TEST("5.3a — an interruption is reported with the interval it covered");
    {
        ppcp_interval iv;
        CHECK_EQ_I(ppcp_interval_make(&iv, "tb:dev", 6, 1000, 5000), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_interruption(dev, "phone_call", &iv, true, NULL, 0), PPCP_OK);
        pump(dev, host, PPCP_CHANNEL_CONTROL);
        collect(host, &s);
        CHECK(s.kinds[PPCP_EVENT_INTERRUPTION]);
    }

    TEST("MSG §4 — session_close ends the Session and not the connection");
    CHECK_EQ_I(ppcp_peer_session_close(host, "user_ended"), PPCP_OK);
    pump(host, dev, PPCP_CHANNEL_CONTROL);
    collect(dev, &s);
    CHECK(s.kinds[PPCP_EVENT_SESSION_CLOSE]);
    CHECK(ppcp_peer_session_id(dev) == NULL);
    CHECK_EQ_I(ppcp_peer_get_state(dev), PPCP_PEER_DECLARED);

    (void)refusals;
    ppcp_peer_free(host); free(hm);
    ppcp_peer_free(dev);  free(dm);
}

/* ================================================= I20 and version refusal */

static void test_role_conflict(void)
{
    static const char *const prof[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_ARBITRATE };
    void      *am = NULL, *bm = NULL;
    ppcp_peer *a, *b;
    seen       s;

    a = make_peer(&am, PPCP_ROLE_HOST, "peer:a", prof, 2, false, NULL, NULL, NULL, 0, NULL);
    b = make_peer(&bm, PPCP_ROLE_HOST, "peer:b", prof, 2, true, NULL, NULL, NULL, 0, NULL);

    TEST("CT-I20 / 3.2c — a second `role: host` is refused with role_conflict");
    CHECK_EQ_I(ppcp_peer_hello(a), PPCP_OK);
    pump(a, b, PPCP_CHANNEL_CONTROL);
    collect(b, &s);
    /* The responder answered, and closed: role_conflict is one of exactly two
     * fatal codes, because two hosts have no session to have. */
    CHECK_EQ_I(ppcp_peer_get_state(b), PPCP_PEER_CLOSED);
    pump(b, a, PPCP_CHANNEL_CONTROL);
    collect(a, &s);
    CHECK(s.kinds[PPCP_EVENT_ERROR]);
    CHECK(error_was(&s, PPCP_ERRCODE_ROLE_CONFLICT));
    CHECK_EQ_I(ppcp_peer_get_state(a), PPCP_PEER_CLOSED);

    ppcp_peer_free(a); free(am);
    ppcp_peer_free(b); free(bm);
}

static void test_version(void)
{
    static const char *const prof[] = { PPCP_PROFILE_CORE };
    static const char *const v2[]   = { "2.0" };
    static const char *const v1[]   = { "1.0" };
    void      *am = NULL, *bm = NULL;
    ppcp_peer *a, *b;
    seen       s;

    TEST("CORE 10.1c — no common MAJOR is unsupported_version, and it is fatal");
    a = make_peer(&am, PPCP_ROLE_CAPTURE, "peer:a", prof, 1, false, NULL, NULL, v2, 1, NULL);
    b = make_peer(&bm, PPCP_ROLE_HOST, "peer:b", prof, 1, true, NULL, NULL, v1, 1, NULL);
    CHECK_EQ_I(ppcp_peer_hello(a), PPCP_OK);
    pump(a, b, PPCP_CHANNEL_CONTROL);
    CHECK_EQ_I(ppcp_peer_get_state(b), PPCP_PEER_CLOSED);
    pump(b, a, PPCP_CHANNEL_CONTROL);
    collect(a, &s);
    CHECK(s.kinds[PPCP_EVENT_ERROR]);
    CHECK(error_was(&s, PPCP_ERRCODE_UNSUPPORTED_VERSION));
    CHECK_EQ_I(ppcp_peer_get_state(a), PPCP_PEER_CLOSED);
    ppcp_peer_free(a); free(am);
    ppcp_peer_free(b); free(bm);

    TEST("CORE 10.1e — a version below the responder's window is refused, with the window");
    {
        static const char *const v10[] = { "1.0" };
        static const char *const v13[] = { "1.3" };
        void      *cm = NULL, *dm = NULL;
        ppcp_peer *c, *d;
        ppcp_event e;
        bool       carried = false;
        c = make_peer(&cm, PPCP_ROLE_CAPTURE, "peer:c", prof, 1, false, NULL, NULL,
                      v10, 1, NULL);
        /* A responder that has moved on: it speaks 1.3 and accepts nothing
         * older than 1.2. */
        d = make_peer(&dm, PPCP_ROLE_HOST, "peer:d", prof, 1, true, NULL, NULL,
                      v13, 1, "1.2");
        CHECK_EQ_I(ppcp_peer_hello(c), PPCP_OK);
        pump(c, d, PPCP_CHANNEL_CONTROL);
        pump(d, c, PPCP_CHANNEL_CONTROL);
        while (ppcp_peer_next_event(c, &e) == PPCP_OK) {
            if (e.kind == PPCP_EVENT_ERROR && e.msg->type == PPCP_MT_ERROR) {
                /* 10.1f: the sender's full supported range, so the user can be
                 * told WHICH END is stale rather than that something failed. */
                CHECK(e.msg->body.error.has_detail_supported);
                CHECK_EQ_I(e.msg->body.error.detail_supported_count, 1);
                CHECK(ppcp_cbor_key_is(e.msg->body.error.detail_supported[0].v,
                                       e.msg->body.error.detail_supported[0].len, "1.3"));
                carried = true;
            }
        }
        CHECK(carried);
        ppcp_peer_free(c); free(cm);
        ppcp_peer_free(d); free(dm);
    }

    TEST("10.1c — a newer MINOR meets an older one at the older MINOR");
    {
        ppcp_id offered[2], supported[2], out;
        CHECK_EQ_I(ppcp_id_set_z(&offered[0], "1.3"), PPCP_OK);
        CHECK_EQ_I(ppcp_id_set_z(&supported[0], "1.0"), PPCP_OK);
        CHECK_EQ_I(ppcp_version_select(offered, 1, supported, 1, &out), PPCP_OK);
        CHECK(ppcp_cbor_key_is(out.v, out.len, "1.0"));
        CHECK_EQ_I(ppcp_id_set_z(&offered[0], "2.1"), PPCP_OK);
        CHECK_EQ_I(ppcp_id_set_z(&offered[1], "1.4"), PPCP_OK);
        CHECK_EQ_I(ppcp_id_set_z(&supported[0], "1.2"), PPCP_OK);
        CHECK_EQ_I(ppcp_id_set_z(&supported[1], "2.0"), PPCP_OK);
        /* the highest MAJOR common to both, then the highest MINOR both reach */
        CHECK_EQ_I(ppcp_version_select(offered, 2, supported, 2, &out), PPCP_OK);
        CHECK(ppcp_cbor_key_is(out.v, out.len, "2.0"));
        CHECK_EQ_I(ppcp_id_set_z(&supported[0], "3.0"), PPCP_OK);
        CHECK_EQ_I(ppcp_id_set_z(&supported[1], "4.0"), PPCP_OK);
        CHECK_EQ_I(ppcp_version_select(offered, 2, supported, 2, &out), PPCP_ERR_NOT_FOUND);
    }
}

/* ================================================ CT-S6 / CT-I24 / CT-I14 */

static void test_comprehension_versus_origination(void)
{
    /* CT-S6 assertion 1's peer exactly: Core + Arbitrate + Live + Offline,
     * and NOT Detect. */
    static const char *const arb_prof[] = {
        PPCP_PROFILE_CORE, PPCP_PROFILE_ARBITRATE, PPCP_PROFILE_LIVE, PPCP_PROFILE_OFFLINE
    };
    static const char *const dev_prof[] = {
        PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE, PPCP_PROFILE_DETECT,
        PPCP_PROFILE_MINT, PPCP_PROFILE_LIVE
    };
    void      *am = NULL, *dm = NULL;
    ppcp_peer *arb, *dev;
    seen       s;
    int        refusals = 0;
    decl       dd;

    arb = make_peer(&am, PPCP_ROLE_HOST, "peer:arb", arb_prof, 4, true,
                    refuse_all, &refusals, NULL, 0, NULL);
    dev = make_peer(&dm, PPCP_ROLE_CAPTURE, "peer:dev", dev_prof, 5, false,
                    accept_all, NULL, NULL, 0, NULL);

    CHECK_EQ_I(ppcp_peer_hello(dev), PPCP_OK);
    pump(dev, arb, PPCP_CHANNEL_CONTROL);
    pump(arb, dev, PPCP_CHANNEL_CONTROL);
    collect(dev, &s);
    collect(arb, &s);

    TEST("CT-S6.1 — a peer with no Detect parses `candidate` completely");
    {
        ppcp_msg      m;
        ppcp_instant  at = inst("tb:host", 5000);
        ppcp_estimate tof;
        ppcp_event    e;
        bool          got = false;

        CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_CANDIDATE, 1), PPCP_OK);
        CHECK_EQ_I(ppcp_candidate_make(&m.body.candidate.candidate, "cand:1", "peer:dev",
                                       "src:1",
                                       /* an unknown basis: 10.3a, ignored not fatal */
                                       "com.example.forceplate", &at, 0.7), PPCP_OK);
        CHECK_EQ_I(ppcp_estimate_make(&tof, -8700000, 400000.0), PPCP_OK);
        CHECK_EQ_I(ppcp_candidate_set_tof_correction(&m.body.candidate.candidate, &tof),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_peer_send(dev, PPCP_CHANNEL_CONTROL, &m), PPCP_OK);
        pump(dev, arb, PPCP_CHANNEL_CONTROL);
        while (ppcp_peer_next_event(arb, &e) == PPCP_OK) {
            if (e.kind != PPCP_EVENT_CANDIDATE)
                continue;
            got = true;
            /* every field, including the unknown `basis` and the Estimate */
            CHECK(ppcp_cbor_key_is(e.msg->body.candidate.candidate.basis.v,
                                   e.msg->body.candidate.candidate.basis.len,
                                   "com.example.forceplate"));
            CHECK(e.msg->body.candidate.candidate.has_tof_correction);
            CHECK_EQ_I(e.msg->body.candidate.candidate.tof_correction.value_ns, -8700000);
            CHECK_EQ_I(e.msg->body.candidate.candidate.at.ns, 5000);
        }
        CHECK(got);
        /* and it did NOT answer profile_not_supported to an event it merely
         * consumes: C3 is about behaviour it must perform, not about a message
         * it is entitled to read (C1). */
        CHECK_EQ_I(ppcp_peer_pending(arb, PPCP_CHANNEL_CONTROL), 0);
    }

    TEST("CT-S6.2 / C2 — that peer never originates `candidate`");
    {
        ppcp_msg     m;
        ppcp_instant at = inst("tb:host", 6000);
        CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_CANDIDATE, 1), PPCP_OK);
        CHECK_EQ_I(ppcp_candidate_make(&m.body.candidate.candidate, "cand:2", "peer:arb",
                                       "src:h", "acoustic", &at, 0.9), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_send(arb, PPCP_CHANNEL_CONTROL, &m), PPCP_ERR_INVALID);
        CHECK_EQ_I(ppcp_peer_pending(arb, PPCP_CHANNEL_CONTROL), 0);

        /* and the positive half: what its profiles DO confer, it may send */
        CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_CAPTURE_REQUEST, 1), PPCP_OK);
        CHECK_EQ_I(ppcp_id_set_z(&m.body.capture_request.shot_id, "shot:1"), PPCP_OK);
        m.body.capture_request.t0      = at;
        m.body.capture_request.pre_ns  = 500000000;
        m.body.capture_request.post_ns = 1500000000;
        CHECK_EQ_I(ppcp_peer_send(arb, PPCP_CHANNEL_CONTROL, &m), PPCP_OK);
        CHECK(ppcp_peer_pending(arb, PPCP_CHANNEL_CONTROL) > 0);
        pump(arb, dev, PPCP_CHANNEL_CONTROL);
        (void)ppcp_peer_next_event(dev, NULL);
        collect(dev, &s);
        pump(dev, arb, PPCP_CHANNEL_CONTROL);
        collect(arb, &s);
    }

    TEST("CT-S6.3 / C3 — a request whose behaviour it lacks is answered, not closed");
    {
        /* `stream_open` is a request and the responder needs Capture, which
         * this arbitrating host does not declare. */
        ppcp_stream  st;
        ppcp_instant at = inst("tb:dev", 1000);
        CHECK_EQ_I(ppcp_stream_make(&st, "st:9", "sess:1", "src:1",
                                    PPCP_STREAM_KIND_VIDEO, "cp:1", "tb:dev",
                                    PPCP_SHOT_WINDOWED, &at), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_stream_open(dev, &st), PPCP_OK);
        pump(dev, arb, PPCP_CHANNEL_CONTROL);
        CHECK(ppcp_peer_pending(arb, PPCP_CHANNEL_CONTROL) > 0);
        pump(arb, dev, PPCP_CHANNEL_CONTROL);
        collect(dev, &s);
        CHECK(s.kinds[PPCP_EVENT_ERROR]);
        CHECK(error_was(&s, PPCP_ERRCODE_PROFILE_NOT_SUPPORTED));
        /* and the transport stayed open at BOTH ends, which is the assertion */
        CHECK(ppcp_peer_get_state(arb) != PPCP_PEER_CLOSED);
        CHECK(ppcp_peer_get_state(dev) != PPCP_PEER_CLOSED);
        CHECK(!ppcp_msg_error_is_fatal(PPCP_ERRCODE_PROFILE_NOT_SUPPORTED,
                                       strlen(PPCP_ERRCODE_PROFILE_NOT_SUPPORTED)));
    }

    TEST("CT-I14 / 3.4a — the ingest verdict is the embedding's, and a refusal "
         "carries a reason and does not close");
    {
        ppcp_event e;
        bool       saw = false;
        build_decl(&dd, "peer:dev", PPCP_ROLE_CAPTURE, "tb:dev", dev_prof, 5);
        CHECK_EQ_I(ppcp_peer_declare(dev, &dd.peer), PPCP_OK);
        pump(dev, arb, PPCP_CHANNEL_CONTROL);
        CHECK_EQ_I(refusals, 1);              /* the callback was consulted */
        pump(arb, dev, PPCP_CHANNEL_CONTROL);
        while (ppcp_peer_next_event(dev, &e) == PPCP_OK) {
            if (e.kind != PPCP_EVENT_DECLARE_ACK)
                continue;
            saw = true;
            CHECK_EQ_I(e.msg->body.declare_ack.verdict, PPCP_VERDICT_REJECTED);
            CHECK(e.msg->body.declare_ack.has_reason);
            CHECK(ppcp_cbor_key_is(e.msg->body.declare_ack.reason.v,
                                   e.msg->body.declare_ack.reason.len,
                                   "com.example.rate_floor"));
        }
        CHECK(saw);
        CHECK(ppcp_peer_get_state(dev) != PPCP_PEER_CLOSED);
        CHECK(ppcp_peer_get_state(arb) != PPCP_PEER_CLOSED);
        /* and the counterpart's declaration is held whichever way the verdict
         * went — the peer said what it is, and that is a fact (3.3a) */
        CHECK(ppcp_peer_counterpart(arb) != NULL);
    }

    TEST("MSG 1b / I13 — an unknown message type is carried, not fatal");
    {
        ppcp_envelope env;
        uint8_t       buf[512];
        size_t        written = 0, consumed = 0;
        CHECK_EQ_I(ppcp_envelope_init(&env, "com.example.telemetry", 900), PPCP_OK);
        CHECK_EQ_I(ppcp_message_encode(buf, sizeof(buf), PPCP_CHANNEL_CONTROL, &env, 0,
                                       NULL, NULL, &written), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_feed(arb, PPCP_CHANNEL_CONTROL, buf, written, &consumed),
                   PPCP_OK);
        CHECK_EQ_I(consumed, written);
        collect(arb, &s);
        CHECK(s.kinds[PPCP_EVENT_UNKNOWN]);
        CHECK(ppcp_peer_get_state(arb) != PPCP_PEER_CLOSED);
    }

    TEST("ENC 5d — an undecodable payload is answered, and the link survives");
    {
        /* A well-framed frame whose payload is not a CBOR map at all. */
        uint8_t buf[32];
        size_t  written = 0, consumed = 0;
        static const uint8_t junk[] = { 0x83, 0x01, 0x02, 0x03 };
        CHECK_EQ_I(ppcp_frame_write(buf, sizeof(buf), PPCP_CHANNEL_CONTROL, junk,
                                    sizeof(junk), &written), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_feed(arb, PPCP_CHANNEL_CONTROL, buf, written, &consumed),
                   PPCP_OK);
        CHECK_EQ_I(consumed, written);
        CHECK(ppcp_peer_pending(arb, PPCP_CHANNEL_CONTROL) > 0);
        CHECK(ppcp_peer_get_state(arb) != PPCP_PEER_CLOSED);
        pump(arb, dev, PPCP_CHANNEL_CONTROL);
        collect(dev, &s);
        CHECK(error_was(&s, PPCP_ERRCODE_MALFORMED));
    }

    ppcp_peer_free(arb); free(am);
    ppcp_peer_free(dev); free(dm);
}

/* ============================================= 5.10e, 7.3b — the hostless form */

static void test_hostless(void)
{
    static const char *const dev_prof[] = {
        PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE, PPCP_PROFILE_DETECT,
        PPCP_PROFILE_MINT, PPCP_PROFILE_OFFLINE
    };
    void        *dm = NULL;
    ppcp_peer   *dev;
    ppcp_session hostless, hosted;

    dev = make_peer(&dm, PPCP_ROLE_CAPTURE, "peer:dev", dev_prof, 5, false,
                    NULL, NULL, NULL, 0, NULL);

    TEST("CORE 4.1b — a capture peer records the hostless session_open itself");
    CHECK_EQ_I(ppcp_session_make_hostless(&hostless, "sess:offline", "tb:dev"), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_session_open(dev, &hostless), PPCP_OK);
    CHECK(ppcp_peer_session_id(dev) != NULL);

    TEST("4.1d / 5.10e — and cannot record the hosted form, which asserts arbitration");
    CHECK_EQ_I(ppcp_session_make_hosted(&hosted, "sess:live", "tb:dev",
                                        PPCP_DEFAULT_COINCIDENCE_WINDOW_NS,
                                        PPCP_DEFAULT_ISSUE_HOLD_NS), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_session_open(dev, &hosted), PPCP_ERR_INVALID);

    TEST("CORE 7.3b — a hostless peer records no `arm`: Live confers it and it has none");
    CHECK_EQ_I(ppcp_peer_arm(dev, NULL, 0), PPCP_ERR_INVALID);
    CHECK(!ppcp_peer_declares(dev, PPCP_PROFILE_LIVE));
    /* 7.3c: `readiness` is conferred by Capture, so the same peer DOES record
     * that.  The distinction is the whole of 7.3b. */
    {
        ppcp_readiness r;
        CHECK_EQ_I(ppcp_readiness_settled(&r), PPCP_OK);
        CHECK_EQ_I(ppcp_peer_readiness(dev, &r, NULL, 0), PPCP_OK);
        CHECK(ppcp_peer_pending(dev, PPCP_CHANNEL_CONTROL) > 0);
    }

    ppcp_peer_free(dev); free(dm);
}

/* ================================================= framing at the boundary */

static void test_partial_feed(void)
{
    static const char *const prof[] = { PPCP_PROFILE_CORE };
    void      *am = NULL, *bm = NULL;
    ppcp_peer *a, *b;
    uint8_t    buf[8192];
    size_t     n = 0, consumed = 0, i;

    a = make_peer(&am, PPCP_ROLE_CAPTURE, "peer:a", prof, 1, false, NULL, NULL, NULL, 0, NULL);
    b = make_peer(&bm, PPCP_ROLE_HOST, "peer:b", prof, 1, true, NULL, NULL, NULL, 0, NULL);

    TEST("ENC 3c — a truncated frame is 'not yet', and the caller keeps the tail");
    CHECK_EQ_I(ppcp_peer_hello(a), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_drain(a, PPCP_CHANNEL_CONTROL, buf, sizeof(buf), &n), PPCP_OK);
    CHECK(n > 12);
    /* One byte at a time: nothing is consumed until the whole frame is there,
     * and the engine has buffered none of it. */
    for (i = 1; i < n; i++) {
        CHECK_EQ_I(ppcp_peer_feed(b, PPCP_CHANNEL_CONTROL, buf, i, &consumed), PPCP_OK);
        CHECK_EQ_I(consumed, 0);
    }
    CHECK_EQ_I(ppcp_peer_feed(b, PPCP_CHANNEL_CONTROL, buf, n, &consumed), PPCP_OK);
    CHECK_EQ_I(consumed, n);
    CHECK_EQ_I(ppcp_peer_get_state(b), PPCP_PEER_CONNECTED);

    TEST("ENC 2c — a frame whose header names another channel is malformed");
    {
        void      *cm = NULL;
        ppcp_peer *c = make_peer(&cm, PPCP_ROLE_HOST, "peer:c", prof, 1, true,
                                 NULL, NULL, NULL, 0, NULL);
        CHECK_EQ_I(ppcp_peer_feed(c, PPCP_CHANNEL_BULK, buf, n, &consumed),
                   PPCP_ERR_MALFORMED);
        CHECK(ppcp_peer_get_state(c) != PPCP_PEER_CLOSED);
        ppcp_peer_free(c); free(cm);
    }

    TEST("ENC 8a — a payload_len past the channel limit is FATAL, not skippable");
    {
        void      *cm = NULL;
        ppcp_peer *c = make_peer(&cm, PPCP_ROLE_HOST, "peer:c", prof, 1, true,
                                 NULL, NULL, NULL, 0, NULL);
        uint8_t    hdr[8];
        hdr[0] = 0xff; hdr[1] = 0xff; hdr[2] = 0xff; hdr[3] = 0xff;   /* ~4 GiB */
        hdr[4] = PPCP_CHANNEL_CONTROL; hdr[5] = 0; hdr[6] = 0; hdr[7] = 0;
        CHECK_EQ_I(ppcp_peer_feed(c, PPCP_CHANNEL_CONTROL, hdr, sizeof(hdr), &consumed),
                   PPCP_ERR_FATAL_LIMIT);
        CHECK_EQ_I(ppcp_peer_get_state(c), PPCP_PEER_CLOSED);
        ppcp_peer_free(c); free(cm);
    }

    ppcp_peer_free(a); free(am);
    ppcp_peer_free(b); free(bm);
}

int main(void)
{
    test_link_binding();
    test_handshake();
    test_role_conflict();
    test_version();
    test_comprehension_versus_origination();
    test_hostless();
    test_partial_feed();
    TEST_MAIN_END();
}
