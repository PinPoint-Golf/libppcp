/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * Markup — work package L11's evidence.  CT-I37.
 *
 * CONF §3 for CT-I37 asks for six things, and the first is asserted somewhere
 * a C program cannot reach:
 *
 *   1. an Annotation reaches no Shot, Candidate, calibration or computed
 *      quantity — "by API surface, not behaviour", so it is in
 *      tests/api_surface.cmake and runs as the ctest `CT-I18-api-surface`;
 *   2. `kind: nav_anchor` is never written as phase data — the same scan: no
 *      function takes an Annotation and returns anything the model computes;
 *   3. an unrecognised `format` round-trips BYTE-IDENTICAL;
 *   4. a LOWER revision for a known `id` is ignored;
 *   5. an EQUAL revision from a different `author_peer_id`, delivered in BOTH
 *      orders, resolves to the same annotation at both ends;
 *   6. `at` follows 5.18g, a view-specific annotation is never rendered on
 *      another Stream (5.18h), and presence of `stream_id` follows `kind`
 *      (5.18j).
 */
#include "ppcp/ppcp.h"

#include "test_util.h"

static ppcp_instant inst(const char *tb, int64_t ns)
{
    ppcp_instant i;
    if (ppcp_instant_make_z(&i, tb, ns) != PPCP_OK)
        abort();
    return i;
}

static void make_annot(ppcp_annotation *a, const char *id, const char *author,
                       const char *kind, const char *format, const uint8_t *body,
                       size_t body_len, uint64_t revision, const char *tb)
{
    ppcp_instant at = inst(tb, 1000000000);
    ppcp_instant created = inst(tb, 1000000000);
    if (ppcp_annotation_make(a, id, "sess:1", "shot:1", &at, author, PPCP_ANNOT_USER,
                             kind, format, body, body_len, &created, revision) != PPCP_OK)
        abort();
}

/* ==================================================== 3 — lossless round trip */

static void test_round_trip(void)
{
    /* A `format` no version of this library has heard of, carrying bytes that
     * are not text, are not valid UTF-8, and include a NUL. */
    static const uint8_t body[] = {
        0x00, 0xFF, 0x80, 0x01, 'p', 'l', 'a', 'n', 'e', 0x00, 0xC3, 0x28, 0x7F
    };
    ppcp_annotation a, out;
    uint8_t         wire[16384];
    size_t          n = 0, consumed = 0;
    ppcp_msg        m;
    ppcp_arena      arena;
    uint8_t         arena_buf[4096];

    TEST("5.18a / 9.0b — an unrecognised `format` round-trips BYTE-IDENTICAL");
    make_annot(&a, "annot:1", "peer:dev", "plane", "com.example.unheard-of",
               body, sizeof(body), 1, "tb:host");
    CHECK_EQ_I(ppcp_annotation_set_stream_id(&a, "stream:dtl"), PPCP_OK);
    CHECK_EQ_I(ppcp_annotation_validate(&a), PPCP_OK);

    CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_ANNOTATION, 7), PPCP_OK);
    m.body.annotation.annotation = a;
    CHECK_EQ_I(ppcp_msg_encode(wire, sizeof(wire), PPCP_CHANNEL_CONTROL, &m, &n), PPCP_OK);

    {
        ppcp_frame_header hdr;
        const uint8_t    *payload = NULL;
        ppcp_msg          got;
        CHECK_EQ_I(ppcp_frame_header_parse(wire, &hdr), PPCP_OK);
        CHECK_EQ_I(ppcp_frame_read(wire, n, &hdr, &payload, &consumed), PPCP_OK);
        ppcp_arena_init(&arena, arena_buf, sizeof(arena_buf));
        memset(&got, 0, sizeof(got));
        CHECK_EQ_I(ppcp_msg_decode(payload, hdr.payload_len,
                                   ppcp_cbor_limits_for_channel(PPCP_CHANNEL_CONTROL),
                                   &arena, &got), PPCP_OK);
        CHECK_EQ_I(got.type, PPCP_MT_ANNOTATION);
        out = got.body.annotation.annotation;
        CHECK_EQ_I(out.body_len, sizeof(body));
        CHECK_BYTES(out.body, out.body_len, body, sizeof(body));
        CHECK(ppcp_cbor_key_is(out.format.v, out.format.len, "com.example.unheard-of"));
        CHECK(out.has_stream_id);
    }

    TEST("5.18f — a `body` over 8 KiB is not constructible");
    {
        static uint8_t big[PPCP_ANNOTATION_BODY_MAX + 1];
        ppcp_annotation too;
        ppcp_instant    at = inst("tb:host", 1), created = inst("tb:host", 1);
        memset(big, 0x5A, sizeof(big));
        CHECK_EQ_I(ppcp_annotation_make(&too, "annot:big", "sess:1", "shot:1", &at,
                                        "peer:dev", PPCP_ANNOT_USER, "text", "text/plain",
                                        big, sizeof(big), &created, 1),
                   PPCP_ERR_INVALID);
    }
}

/* ============================================= 4, 5 — supersession converges */

static void test_supersession(void)
{
    ppcp_annotation r1, r2, r2b;
    static const uint8_t b1[] = { 'a' };
    static const uint8_t b2[] = { 'b' };
    static const uint8_t b3[] = { 'c' };

    TEST("5.18e — a HIGHER revision supersedes; a LOWER one is ignored");
    make_annot(&r1, "annot:1", "peer:dev", "text", "text/plain", b1, 1, 1, "tb:host");
    make_annot(&r2, "annot:1", "peer:dev", "text", "text/plain", b2, 1, 2, "tb:host");
    CHECK(ppcp_annotation_supersedes(&r2, &r1) > 0);
    CHECK(ppcp_annotation_supersedes(&r1, &r2) < 0);

    TEST("5.18e — an EQUAL revision from a different author is broken bytewise");
    make_annot(&r2b, "annot:1", "peer:host", "text", "text/plain", b3, 1, 2, "tb:host");
    /* "peer:host" sorts BELOW "peer:dev"? — 'd' < 'h', so peer:host is higher. */
    CHECK(ppcp_annotation_supersedes(&r2b, &r2) > 0);
    CHECK(ppcp_annotation_supersedes(&r2, &r2b) < 0);
    CHECK_EQ_I(ppcp_annotation_supersedes(&r2, &r2), 0);

    TEST("5.18e — annotations with different `id`s are not in each other's order");
    {
        ppcp_annotation other;
        make_annot(&other, "annot:2", "peer:zzz", "text", "text/plain", b1, 1, 99,
                   "tb:host");
        CHECK_EQ_I(ppcp_annotation_supersedes(&other, &r2), 0);
        CHECK_EQ_I(ppcp_annotation_supersedes(&r2, &other), 0);
    }

    TEST("CT-I37 (5) — the equal-revision race converges, in BOTH delivery orders");
    {
        void *ma = malloc(ppcp_annotation_store_sizeof());
        void *mb = malloc(ppcp_annotation_store_sizeof());
        ppcp_annotation_store *A = NULL, *B = NULL;
        const ppcp_annotation *wa, *wb;
        bool replaced = false;

        CHECK(ma != NULL && mb != NULL);
        CHECK_EQ_I(ppcp_annotation_store_new(ma, ppcp_annotation_store_sizeof(), &A),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_annotation_store_new(mb, ppcp_annotation_store_sizeof(), &B),
                   PPCP_OK);

        /* Both ends hold revision 1.  Both produce revision 2 — the coach at
         * the host and the golfer at the device drawing on one shot.  Each
         * then receives the other's. */
        CHECK_EQ_I(ppcp_annotation_store_observe(A, &r1, &replaced), PPCP_OK);
        CHECK(replaced);
        CHECK_EQ_I(ppcp_annotation_store_observe(B, &r1, &replaced), PPCP_OK);

        /* Order one: device's revision 2 first, then the host's. */
        CHECK_EQ_I(ppcp_annotation_store_observe(A, &r2, &replaced), PPCP_OK);
        CHECK(replaced);
        CHECK_EQ_I(ppcp_annotation_store_observe(A, &r2b, &replaced), PPCP_OK);
        CHECK(replaced);
        /* Order two: the host's first, then the device's, which must now be
         * IGNORED — and that is the half revision 7 of the specification got
         * wrong, where both ends ignored each other and diverged silently. */
        CHECK_EQ_I(ppcp_annotation_store_observe(B, &r2b, &replaced), PPCP_OK);
        CHECK(replaced);
        CHECK_EQ_I(ppcp_annotation_store_observe(B, &r2, &replaced), PPCP_OK);
        CHECK(!replaced);

        {
            ppcp_id id;
            CHECK_EQ_I(ppcp_id_set_z(&id, "annot:1"), PPCP_OK);
            wa = ppcp_annotation_store_find(A, &id);
            wb = ppcp_annotation_store_find(B, &id);
        }
        CHECK(wa != NULL && wb != NULL);
        CHECK_EQ_I(wa->revision, 2);
        CHECK(ppcp_id_equal(&wa->author_peer_id, &wb->author_peer_id));
        CHECK(ppcp_cbor_key_is(wa->author_peer_id.v, wa->author_peer_id.len, "peer:host"));
        /* Convergence includes the opaque bytes, because there is no merge:
         * the winner's `body` is the whole of the winner. */
        CHECK_BYTES(wa->body, wa->body_len, wb->body, wb->body_len);
        CHECK_EQ_I(ppcp_annotation_store_count(A), 1);
        CHECK_EQ_I(ppcp_annotation_store_count(B), 1);

        TEST("5.18a — the store returns `body` byte-identical, not re-encoded");
        CHECK_BYTES(wa->body, wa->body_len, b3, sizeof(b3));

        free(ma);
        free(mb);
    }
}

/* ============================================== 6 — 5.18g, 5.18h and 5.18j */

static void test_placement(void)
{
    ppcp_stream  st;
    ppcp_instant opened = inst("tb:dev", 0);
    ppcp_id      ref;
    ppcp_annotation a;
    static const uint8_t body[] = { 1, 2, 3 };

    CHECK_EQ_I(ppcp_id_set_z(&ref, "tb:host"), PPCP_OK);
    CHECK_EQ_I(ppcp_stream_make(&st, "stream:dtl", "sess:1", "src:cam",
                                PPCP_STREAM_KIND_VIDEO, "cp:1", "tb:dev",
                                PPCP_SHOT_WINDOWED, &opened), PPCP_OK);

    TEST("5.18j — a view-specific `kind` carries `stream_id`");
    CHECK_EQ_I(ppcp_annotation_kind_view("line", 4), PPCP_KIND_VIEW_SPECIFIC);
    CHECK_EQ_I(ppcp_annotation_kind_view("plane", 5), PPCP_KIND_VIEW_SPECIFIC);
    CHECK_EQ_I(ppcp_annotation_kind_view("text", 4), PPCP_KIND_NOT_VIEW_SPECIFIC);
    CHECK_EQ_I(ppcp_annotation_kind_view("nav_anchor", 10), PPCP_KIND_NOT_VIEW_SPECIFIC);
    CHECK_EQ_I(ppcp_annotation_kind_view("com.example.blob", 16), PPCP_KIND_UNREGISTERED);

    make_annot(&a, "annot:line", "peer:host", "line", "application/json", body,
               sizeof(body), 1, "tb:dev");
    CHECK_EQ_I(ppcp_annotation_set_stream_id(&a, "stream:dtl"), PPCP_OK);
    /* 5.18g — `at` is in THAT Stream's timebase, which is the device's clock
     * and not the Session's reference. */
    CHECK_EQ_I(ppcp_annotation_validate_placement(&a, &ref, &st), PPCP_OK);

    TEST("5.18h — a view-specific annotation is refused against another Stream");
    {
        ppcp_stream other;
        CHECK_EQ_I(ppcp_stream_make(&other, "stream:faceon", "sess:1", "src:cam2",
                                    PPCP_STREAM_KIND_VIDEO, "cp:1", "tb:dev",
                                    PPCP_SHOT_WINDOWED, &opened), PPCP_OK);
        CHECK_EQ_I(ppcp_annotation_validate_placement(&a, &ref, &other), PPCP_ERR_INVALID);
    }

    TEST("5.18g — a view-specific annotation stamped in the Session's clock is refused");
    {
        ppcp_annotation wrong = a;
        wrong.at = inst("tb:host", 1000000000);
        CHECK_EQ_I(ppcp_annotation_validate_placement(&wrong, &ref, &st), PPCP_ERR_INVALID);
    }

    TEST("5.18j — a view-specific `kind` with no `stream_id` is refused");
    {
        ppcp_annotation bare;
        make_annot(&bare, "annot:bare", "peer:host", "line", "application/json", body,
                   sizeof(body), 1, "tb:host");
        CHECK(!bare.has_stream_id);
        CHECK_EQ_I(ppcp_annotation_validate_placement(&bare, &ref, NULL), PPCP_ERR_INVALID);
    }

    TEST("5.18j / 5.18g — `text` carries no `stream_id`, and `at` is in timebase_ref");
    {
        ppcp_annotation note;
        make_annot(&note, "annot:note", "peer:host", "text", "text/plain", body,
                   sizeof(body), 1, "tb:host");
        CHECK_EQ_I(ppcp_annotation_validate_placement(&note, &ref, NULL), PPCP_OK);
        CHECK_EQ_I(ppcp_annotation_set_stream_id(&note, "stream:dtl"), PPCP_ERR_INVALID);
    }

    TEST("5.18j — an UNREGISTERED kind is view-specific iff `stream_id` is present");
    {
        ppcp_annotation vendor;
        make_annot(&vendor, "annot:v", "peer:host", "com.example.blob",
                   "application/octet-stream", body, sizeof(body), 1, "tb:host");
        /* Without `stream_id`: not view-specific, so `at` is in timebase_ref. */
        CHECK_EQ_I(ppcp_annotation_validate_placement(&vendor, &ref, NULL), PPCP_OK);
        /* With it: view-specific, so `at` must be the Stream's. */
        vendor.at = inst("tb:dev", 1000000000);
        CHECK_EQ_I(ppcp_annotation_set_stream_id(&vendor, "stream:dtl"), PPCP_OK);
        CHECK_EQ_I(ppcp_annotation_validate_placement(&vendor, &ref, &st), PPCP_OK);
    }

    TEST("a `stream_id` naming a Stream nobody has is a dangling reference");
    CHECK_EQ_I(ppcp_annotation_validate_placement(&a, &ref, NULL), PPCP_ERR_NOT_FOUND);
}

/* ==================================================== either direction, C2 */

static void test_wire(void)
{
    static const char *const markup[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_MARKUP };
    static const char *const plain[]  = { PPCP_PROFILE_CORE, PPCP_PROFILE_CAPTURE };
    ppcp_peer_config cfg;
    ppcp_peer       *host = NULL, *dev = NULL, *quiet = NULL;
    void            *hm, *dm, *qm;
    ppcp_annotation  a;
    static const uint8_t body[] = { 'x' };
    uint8_t          wire[8192];
    size_t           n = 0, consumed = 0;
    ppcp_event       ev;
    bool             seen = false;

    memset(&cfg, 0, sizeof(cfg));
    cfg.role = PPCP_ROLE_HOST;   cfg.peer_id = "peer:host";
    cfg.profiles = markup;       cfg.profile_count = 2;
    hm = malloc(ppcp_peer_sizeof());
    CHECK_EQ_I(ppcp_peer_new(hm, ppcp_peer_sizeof(), &cfg, &host), PPCP_OK);
    cfg.role = PPCP_ROLE_CAPTURE; cfg.peer_id = "peer:dev";
    dm = malloc(ppcp_peer_sizeof());
    CHECK_EQ_I(ppcp_peer_new(dm, ppcp_peer_sizeof(), &cfg, &dev), PPCP_OK);
    cfg.profiles = plain;
    cfg.peer_id  = "peer:quiet";
    qm = malloc(ppcp_peer_sizeof());
    CHECK_EQ_I(ppcp_peer_new(qm, ppcp_peer_sizeof(), &cfg, &quiet), PPCP_OK);

    make_annot(&a, "annot:1", "peer:host", "text", "text/plain", body, sizeof(body), 1,
               "tb:host");

    TEST("9.0a / 5.18d — `annotation` travels EITHER direction");
    CHECK_EQ_I(ppcp_peer_annotate(host, &a), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_drain(host, PPCP_CHANNEL_CONTROL, wire, sizeof(wire), &n),
               PPCP_OK);
    CHECK_EQ_I(ppcp_peer_feed(dev, PPCP_CHANNEL_CONTROL, wire, n, &consumed), PPCP_OK);
    while (ppcp_peer_next_event(dev, &ev) == PPCP_OK)
        if (ev.kind == PPCP_EVENT_ANNOTATION)
            seen = true;
    CHECK(seen);
    /* And back the other way, which no other content in PPCP does. */
    CHECK_EQ_I(ppcp_id_set_z(&a.author_peer_id, "peer:dev"), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_annotate(dev, &a), PPCP_OK);

    TEST("C2 / CONF 1d — a peer that does not declare Markup cannot originate one");
    CHECK_EQ_I(ppcp_peer_annotate(quiet, &a), PPCP_ERR_INVALID);

    TEST("I24 — but it PARSES one completely, whatever its profiles");
    {
        size_t m = 0;
        seen = false;
        CHECK_EQ_I(ppcp_peer_drain(dev, PPCP_CHANNEL_CONTROL, wire, sizeof(wire), &m),
                   PPCP_OK);
        CHECK_EQ_I(ppcp_peer_feed(quiet, PPCP_CHANNEL_CONTROL, wire, m, &consumed),
                   PPCP_OK);
        while (ppcp_peer_next_event(quiet, &ev) == PPCP_OK)
            if (ev.kind == PPCP_EVENT_ANNOTATION && ev.msg != NULL) {
                seen = true;
                CHECK_BYTES(ev.msg->body.annotation.annotation.body,
                            ev.msg->body.annotation.annotation.body_len,
                            body, sizeof(body));
            }
        CHECK(seen);
        CHECK(ppcp_peer_get_state(quiet) != PPCP_PEER_CLOSED);
    }

    ppcp_peer_free(host);  free(hm);
    ppcp_peer_free(dev);   free(dm);
    ppcp_peer_free(quiet); free(qm);
}

int main(void)
{
    test_round_trip();
    test_supersession();
    test_placement();
    test_wire();
    TEST_MAIN_END();
}
