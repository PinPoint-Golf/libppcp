/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * CT-I39 — an Actuator's `control` decides the SHAPE of every message that
 * names it.  Erratum E58 (CR-02) added the invariant; erratum E63 (CR-02
 * review round 1) extended it, because both application teams independently
 * found that `state: {}` satisfied the schema while contradicting 12.1c's
 * prose.
 *
 * SIX assertions, not two.  The row asks the same question of three carriers —
 * `actuator_command`, `actuator_command_ack.state` (verdict `applied`) and
 * `actuator_state.state` — against each of the two declared `control` kinds:
 *
 *   1  on_off / actuator_command             4  level / actuator_command
 *   2  on_off / actuator_command_ack.state   5  level / actuator_command_ack.state
 *   3  on_off / actuator_state.state         6  level / actuator_state.state
 *
 * ⚠ HOW EACH IS ATTACKED, AND WHY IT IS NOT THE CONSTRUCTORS.
 *
 * L28 enforced I39 BY SHAPE (decision CB7): `ppcp_actuator_setting` has
 * `_on_off()` and `_level()` and no `_make()`, so "never neither, never both"
 * is a property of the type and a test that only called the constructors would
 * prove nothing the compiler had not already guaranteed.  So the invariant is
 * attacked from the two directions a hostile peer actually has:
 *
 *   (a) THE DECODE PATH, with CBOR this library's own encoder cannot emit.
 *       Every "neither / both" case below is a hand-built body written
 *       straight through ppcp_message_encode() with a body writer of this
 *       file's own — `{}`, `{on, level}` — and asserted to come back
 *       PPCP_ERR_MALFORMED from ppcp_msg_decode().  That is the half no
 *       constructor is involved in: bytes arriving from a peer we do not
 *       control.
 *
 *   (b) THE DECLARATION-RELATIVE HALF, which decoding cannot see at all.
 *       "`level` on an `on_off` Actuator" is well-formed CBOR — the message
 *       carries exactly one field — and is malformed only against the
 *       Actuator's DECLARED `control`.  No decoder has that declaration, so
 *       these cases are driven through two engines: a well-formed command in
 *       the wrong shape is sent with ppcp_peer_send() (bypassing
 *       ppcp_peer_actuator_command()'s own 12.1a pre-check, which is asserted
 *       separately) and the responder is required to answer `error` /
 *       `malformed` and NOT to act.
 *
 * The paired half of the row — `not_declared` for an undeclared `actuator_id`
 * (12.1d) and a refusal for a command from a peer that is not the Session's
 * `role: host` (12a) — runs over real sockets as CT-I39-sockets-* in
 * tests/CMakeLists.txt, because CONF §2c says an implementation tested only
 * against itself passes by accident.
 *
 * ⚠ WHO ANSWERS, SINCE L30.  The engine no longer writes the `applied` ack: a
 * well-formed, declared, host-originated command is handed to the embedding as
 * PPCP_EVENT_ACTUATOR_COMMAND and answered with
 * ppcp_peer_actuator_command_applied().  12.1c says `state` is what the
 * Actuator is ACTUALLY doing and not an echo of the request, and a sans-I/O
 * library has no hardware to read.  So the `dev` rig below carries a simulated
 * driver — a `level` Actuator that moves in discrete steps — and the ack the
 * host reads carries 0.25 for a request of 0.5.  An engine that echoed could
 * not fail that assertion, which is why it is here.
 */
#include "ppcp/ppcp.h"
#include "ppcp/peer.h"
#include "ppcp/message.h"
#include "ppcp/envelope.h"
#include "ppcp/frame.h"

#include "test_util.h"

/* ===================================================== (a) the decode path
 *
 * A body writer per message, driven by a description a constructor would
 * refuse.  ppcp_message_encode() writes the header, the envelope and the body
 * in one buffer; the writer is in deterministic order, so body keys are
 * emitted in RFC 8949 §4.2.1 order with ppcp_envelope_before() interleaving
 * the reserved ones.  Getting that order wrong fails the encode loudly rather
 * than producing a frame that means something else.
 */

typedef struct raw_setting {
    bool   with_on;
    bool   on;
    bool   with_level;
    double level;
} raw_setting;

static ppcp_result put_key(ppcp_cbor_writer *w, ppcp_envelope_writer *ew, const char *k)
{
    ppcp_result rc = ppcp_envelope_before(w, ew, k, strlen(k));
    if (rc != PPCP_OK)
        return rc;
    return ppcp_cbor_write_text_z(w, k);
}

/* `actuator_command` carries `on` / `level` FLAT (MSG 12.1). */
typedef struct raw_cmd {
    const char *actuator_id;
    raw_setting s;
} raw_cmd;

static ppcp_result write_cmd(ppcp_cbor_writer *w, ppcp_envelope_writer *ew, void *ctx)
{
    raw_cmd *c = (raw_cmd *)ctx;
    if (c->s.with_on) {                       /* "on" (2) sorts first of all */
        (void)put_key(w, ew, "on");
        (void)ppcp_cbor_write_bool(w, c->s.on);
    }
    if (c->s.with_level) {                    /* "level" (5) */
        (void)put_key(w, ew, "level");
        (void)ppcp_cbor_write_double(w, c->s.level);
    }
    (void)put_key(w, ew, "actuator_id");      /* (11), last */
    (void)ppcp_cbor_write_text_z(w, c->actuator_id);
    return ppcp_cbor_writer_status(w);
}

static size_t cmd_fields(const raw_cmd *c)
{
    return 1u + (c->s.with_on ? 1u : 0u) + (c->s.with_level ? 1u : 0u);
}

/* The `state` sub-map shared by the ack and the event. */
static ppcp_result write_state_map(ppcp_cbor_writer *w, const raw_setting *s)
{
    size_t n = (s->with_on ? 1u : 0u) + (s->with_level ? 1u : 0u);
    ppcp_result rc = ppcp_cbor_write_map(w, n);
    if (rc != PPCP_OK)
        return rc;
    if (s->with_on) {
        (void)ppcp_cbor_write_text_z(w, "on");
        (void)ppcp_cbor_write_bool(w, s->on);
    }
    if (s->with_level) {
        (void)ppcp_cbor_write_text_z(w, "level");
        (void)ppcp_cbor_write_double(w, s->level);
    }
    return ppcp_cbor_writer_status(w);
}

typedef struct raw_ack {
    const char *actuator_id;
    const char *verdict;      /* "applied" or "refused" */
    const char *reason;       /* NULL where absent */
    bool        with_state;
    raw_setting s;
} raw_ack;

static ppcp_result write_ack(ppcp_cbor_writer *w, ppcp_envelope_writer *ew, void *ctx)
{
    raw_ack *a = (raw_ack *)ctx;
    if (a->with_state) {                      /* "state" (5) */
        (void)put_key(w, ew, "state");
        (void)write_state_map(w, &a->s);
    }
    if (a->reason != NULL) {                  /* "reason" (6), after "msg_id" */
        (void)put_key(w, ew, "reason");
        (void)ppcp_cbor_write_text_z(w, a->reason);
    }
    (void)put_key(w, ew, "verdict");          /* (7) */
    (void)ppcp_cbor_write_text_z(w, a->verdict);
    (void)put_key(w, ew, "actuator_id");      /* (11) */
    (void)ppcp_cbor_write_text_z(w, a->actuator_id);
    return ppcp_cbor_writer_status(w);
}

static size_t ack_fields(const raw_ack *a)
{
    return 2u + (a->with_state ? 1u : 0u) + (a->reason != NULL ? 1u : 0u);
}

typedef struct raw_state {
    const char  *actuator_id;
    bool         with_state;
    raw_setting  s;
    ppcp_instant since;
} raw_state;

static ppcp_result write_state(ppcp_cbor_writer *w, ppcp_envelope_writer *ew, void *ctx)
{
    raw_state *e = (raw_state *)ctx;
    (void)put_key(w, ew, "since");            /* "since" (5) sorts before "state" */
    (void)ppcp_instant_encode(w, &e->since);
    if (e->with_state) {
        (void)put_key(w, ew, "state");
        (void)write_state_map(w, &e->s);
    }
    (void)put_key(w, ew, "actuator_id");
    (void)ppcp_cbor_write_text_z(w, e->actuator_id);
    return ppcp_cbor_writer_status(w);
}

static size_t state_fields(const raw_state *e)
{
    return 2u + (e->with_state ? 1u : 0u);
}

/* Encodes one hand-built frame and hands its payload to ppcp_msg_decode().
 * Returns the decoder's verdict, which is the whole assertion. */
static ppcp_result decode_raw(const char *type, size_t body_fields,
                              ppcp_body_writer body, void *ctx, ppcp_msg *out)
{
    static uint8_t   buf[4096];
    static uint8_t   arena_mem[16384];
    ppcp_arena       arena;
    ppcp_envelope    env;
    ppcp_frame_header hdr;
    const uint8_t   *payload = NULL;
    size_t           written = 0, consumed = 0;
    ppcp_result      rc;

    if (ppcp_envelope_init(&env, type, 21) != PPCP_OK)
        return PPCP_ERR_INVALID;
    rc = ppcp_message_encode(buf, sizeof(buf), PPCP_CHANNEL_CONTROL, &env, body_fields,
                             body, ctx, &written);
    if (rc != PPCP_OK)
        return rc;                            /* the WRITER refused: not a decode result */
    rc = ppcp_frame_read(buf, written, &hdr, &payload, &consumed);
    if (rc != PPCP_OK)
        return rc;
    ppcp_arena_init(&arena, arena_mem, sizeof(arena_mem));
    memset(out, 0, sizeof(*out));
    return ppcp_msg_decode(payload, hdr.payload_len,
                           ppcp_cbor_limits_for_channel(PPCP_CHANNEL_CONTROL), &arena, out);
}

static void decode_half(void)
{
    ppcp_msg m;

    /* ------------------------------------------------- 1 & 4, the command */

    TEST("I39/E63 — `actuator_command` carrying BOTH `on` and `level` is malformed");
    {
        raw_cmd c;
        c.actuator_id = "act:torch";
        c.s.with_on = true;  c.s.on = true;
        c.s.with_level = true; c.s.level = 0.5;
        CHECK_EQ_I(decode_raw("actuator_command", cmd_fields(&c), write_cmd, &c, &m),
                   PPCP_ERR_MALFORMED);
    }

    TEST("I39/E63 — `actuator_command` carrying NEITHER is malformed");
    {
        raw_cmd c;
        c.actuator_id = "act:torch";
        c.s.with_on = false; c.s.on = false;
        c.s.with_level = false; c.s.level = 0.0;
        CHECK_EQ_I(decode_raw("actuator_command", cmd_fields(&c), write_cmd, &c, &m),
                   PPCP_ERR_MALFORMED);
    }

    TEST("I39 — `on` alone decodes, and is the on/off shape");
    {
        raw_cmd c;
        c.actuator_id = "act:torch";
        c.s.with_on = true; c.s.on = true;
        c.s.with_level = false; c.s.level = 0.0;
        CHECK_EQ_I(decode_raw("actuator_command", cmd_fields(&c), write_cmd, &c, &m), PPCP_OK);
        CHECK(m.body.actuator_command.setting.has_on);
        CHECK(!m.body.actuator_command.setting.has_level);
        CHECK(m.body.actuator_command.setting.on);
    }

    TEST("I39 — `level` alone decodes, and is the level shape");
    {
        raw_cmd c;
        c.actuator_id = "act:lamp";
        c.s.with_on = false; c.s.on = false;
        c.s.with_level = true; c.s.level = 0.25;
        CHECK_EQ_I(decode_raw("actuator_command", cmd_fields(&c), write_cmd, &c, &m), PPCP_OK);
        CHECK(!m.body.actuator_command.setting.has_on);
        CHECK(m.body.actuator_command.setting.has_level);
        CHECK(m.body.actuator_command.setting.level == 0.25);
    }

    /* MSG 12.1's domain, which is a DOMAIN and not a threshold (CT-I14, trap
     * 10): a `level` outside 0.0..1.0 is not a value this field can hold. */
    TEST("MSG 12.1 — a `level` outside 0.0..1.0 is malformed, and is not clamped");
    {
        raw_cmd c;
        c.actuator_id = "act:lamp";
        c.s.with_on = false; c.s.on = false;
        c.s.with_level = true; c.s.level = 1.5;
        CHECK_EQ_I(decode_raw("actuator_command", cmd_fields(&c), write_cmd, &c, &m),
                   PPCP_ERR_MALFORMED);
    }

    /* --------------------------------- 2 & 5, actuator_command_ack.state
     *
     * ⭐ E63's own case: `state: {}` on an `applied` ack.  It satisfied the
     * schema — `state` is a map, and it was there — and contradicted 12.1c,
     * which says the field reports what the Actuator is ACTUALLY doing. */

    TEST("E63 / 12.1c1 — an `applied` ack with `state: {}` is malformed");
    {
        raw_ack a;
        a.actuator_id = "act:torch"; a.verdict = "applied"; a.reason = NULL;
        a.with_state = true;
        a.s.with_on = false; a.s.on = false; a.s.with_level = false; a.s.level = 0.0;
        CHECK_EQ_I(decode_raw("actuator_command_ack", ack_fields(&a), write_ack, &a, &m),
                   PPCP_ERR_MALFORMED);
    }

    TEST("E63 / 12.1c1 — an `applied` ack carrying BOTH `on` and `level` is malformed");
    {
        raw_ack a;
        a.actuator_id = "act:torch"; a.verdict = "applied"; a.reason = NULL;
        a.with_state = true;
        a.s.with_on = true; a.s.on = false; a.s.with_level = true; a.s.level = 0.0;
        CHECK_EQ_I(decode_raw("actuator_command_ack", ack_fields(&a), write_ack, &a, &m),
                   PPCP_ERR_MALFORMED);
    }

    TEST("12.1c1 — an `applied` ack carries exactly one of the two, either way");
    {
        raw_ack a;
        a.actuator_id = "act:torch"; a.verdict = "applied"; a.reason = NULL;
        a.with_state = true;
        a.s.with_on = true; a.s.on = true; a.s.with_level = false; a.s.level = 0.0;
        CHECK_EQ_I(decode_raw("actuator_command_ack", ack_fields(&a), write_ack, &a, &m),
                   PPCP_OK);
        CHECK(m.body.actuator_command_ack.has_state);
        CHECK(m.body.actuator_command_ack.state.has_on);
        CHECK(!m.body.actuator_command_ack.state.has_level);

        a.actuator_id = "act:lamp";
        a.s.with_on = false; a.s.with_level = true; a.s.level = 0.75;
        CHECK_EQ_I(decode_raw("actuator_command_ack", ack_fields(&a), write_ack, &a, &m),
                   PPCP_OK);
        CHECK(m.body.actuator_command_ack.state.has_level);
        CHECK(!m.body.actuator_command_ack.state.has_on);
    }

    /* 12.1b / 12.1c1 — presence itself, which is the other half of "neither":
     * an `applied` ack with NO `state` at all, and a `refused` one carrying
     * one, are both malformed. */
    TEST("12.1c1 — an `applied` ack with no `state` at all is malformed");
    {
        raw_ack a;
        a.actuator_id = "act:torch"; a.verdict = "applied"; a.reason = NULL;
        a.with_state = false;
        a.s.with_on = false; a.s.on = false; a.s.with_level = false; a.s.level = 0.0;
        CHECK_EQ_I(decode_raw("actuator_command_ack", ack_fields(&a), write_ack, &a, &m),
                   PPCP_ERR_MALFORMED);
    }

    TEST("12.1b — `reason` iff `refused`, and `state` iff `applied`");
    {
        raw_ack a;
        a.actuator_id = "act:torch"; a.verdict = "refused"; a.reason = "thermal_limit";
        a.with_state = true;
        a.s.with_on = true; a.s.on = false; a.s.with_level = false; a.s.level = 0.0;
        CHECK_EQ_I(decode_raw("actuator_command_ack", ack_fields(&a), write_ack, &a, &m),
                   PPCP_ERR_MALFORMED);
        a.with_state = false;
        CHECK_EQ_I(decode_raw("actuator_command_ack", ack_fields(&a), write_ack, &a, &m),
                   PPCP_OK);
        CHECK_EQ_I(m.body.actuator_command_ack.verdict, PPCP_ACTUATOR_REFUSED);
        CHECK(!m.body.actuator_command_ack.has_state);
        CHECK(m.body.actuator_command_ack.has_reason);
    }

    /* --------------------------------------- 3 & 6, actuator_state.state */

    TEST("E63 / 12.2a1 — an `actuator_state` with `state: {}` is malformed");
    {
        raw_state e;
        e.actuator_id = "act:torch"; e.with_state = true;
        e.s.with_on = false; e.s.on = false; e.s.with_level = false; e.s.level = 0.0;
        CHECK_EQ_I(ppcp_instant_make_z(&e.since, "tb:dev", 1000), PPCP_OK);
        CHECK_EQ_I(decode_raw("actuator_state", state_fields(&e), write_state, &e, &m),
                   PPCP_ERR_MALFORMED);
    }

    TEST("E63 / 12.2a1 — an `actuator_state` carrying BOTH is malformed");
    {
        raw_state e;
        e.actuator_id = "act:torch"; e.with_state = true;
        e.s.with_on = true; e.s.on = true; e.s.with_level = true; e.s.level = 1.0;
        CHECK_EQ_I(ppcp_instant_make_z(&e.since, "tb:dev", 1000), PPCP_OK);
        CHECK_EQ_I(decode_raw("actuator_state", state_fields(&e), write_state, &e, &m),
                   PPCP_ERR_MALFORMED);
    }

    TEST("12.2a1 — an `actuator_state` carries exactly one of the two, either way");
    {
        raw_state e;
        e.actuator_id = "act:torch"; e.with_state = true;
        e.s.with_on = true; e.s.on = true; e.s.with_level = false; e.s.level = 0.0;
        CHECK_EQ_I(ppcp_instant_make_z(&e.since, "tb:dev", 1000), PPCP_OK);
        CHECK_EQ_I(decode_raw("actuator_state", state_fields(&e), write_state, &e, &m),
                   PPCP_OK);
        CHECK(m.body.actuator_state.state.has_on);
        CHECK(!m.body.actuator_state.state.has_level);

        e.actuator_id = "act:lamp";
        e.s.with_on = false; e.s.with_level = true; e.s.level = 0.1;
        CHECK_EQ_I(decode_raw("actuator_state", state_fields(&e), write_state, &e, &m),
                   PPCP_OK);
        CHECK(m.body.actuator_state.state.has_level);
        CHECK(!m.body.actuator_state.state.has_on);
    }

    TEST("12.2a — `actuator_state` with no `state` at all is malformed");
    {
        raw_state e;
        e.actuator_id = "act:torch"; e.with_state = false;
        e.s.with_on = false; e.s.on = false; e.s.with_level = false; e.s.level = 0.0;
        CHECK_EQ_I(ppcp_instant_make_z(&e.since, "tb:dev", 1000), PPCP_OK);
        CHECK_EQ_I(decode_raw("actuator_state", state_fields(&e), write_state, &e, &m),
                   PPCP_ERR_MALFORMED);
    }

    /* CB7 stated the other way round: the library's own ENCODER cannot put a
     * two-field or a no-field setting on the wire either, so the malformed
     * bodies above could only have been written by the hand-rolled writer at
     * the top of this file. */
    TEST("CB7 — the encoder refuses a setting no constructor could have built");
    {
        ppcp_msg out;
        CHECK_EQ_I(ppcp_msg_init(&out, PPCP_MT_ACTUATOR_STATE, 3), PPCP_OK);
        CHECK_EQ_I(ppcp_id_set_z(&out.body.actuator_state.actuator_id, "act:torch"), PPCP_OK);
        CHECK_EQ_I(ppcp_instant_make_z(&out.body.actuator_state.since, "tb:dev", 1000),
                   PPCP_OK);
        out.body.actuator_state.state.has_on    = true;
        out.body.actuator_state.state.has_level = true;
        {
            uint8_t enc[1024];
            size_t  n = 0;
            CHECK(ppcp_msg_encode(enc, sizeof(enc), PPCP_CHANNEL_CONTROL, &out, &n)
                  != PPCP_OK);
        }
        CHECK_EQ_I(ppcp_actuator_setting_validate(&out.body.actuator_state.state),
                   PPCP_ERR_INVALID);
        out.body.actuator_state.state.has_on    = false;
        out.body.actuator_state.state.has_level = false;
        CHECK_EQ_I(ppcp_actuator_setting_validate(&out.body.actuator_state.state),
                   PPCP_ERR_INVALID);
    }
}

/* ============================================ (b) the declaration-relative half
 *
 * `{level: 0.5}` is a perfectly well-formed body.  Whether it is I39-malformed
 * depends on a fact no decoder holds: the `control` the owning peer DECLARED
 * for that Actuator.  So this half runs two engines, one of which declares a
 * torch (`on_off`) and an indicator LED (`level`).
 */

static const char *const PROF[] = { PPCP_PROFILE_CORE, PPCP_PROFILE_LIVE,
                                    PPCP_PROFILE_ACTUATE };

typedef struct rig {
    void                *mem;
    ppcp_peer           *p;
    ppcp_id              profiles[4];
    ppcp_timebase        tb[1];
    ppcp_capture_profile cp[1];
    ppcp_source          src[1];
    ppcp_actuator        act[2];
    ppcp_peer_desc       desc;
} rig;

static void rig_new(rig *r, ppcp_role role, const char *id, const char *tb_id,
                    bool with_actuators)
{
    ppcp_peer_config cfg;
    ppcp_timing      timing;
    size_t           i;

    memset(r, 0, sizeof(*r));
    memset(&cfg, 0, sizeof(cfg));
    cfg.role          = role;
    cfg.peer_id       = id;
    cfg.profiles      = PROF;
    cfg.profile_count = 3;
    cfg.health_report = ppcp_test_health;    /* F-H5-3: Live needs one */
    r->mem = malloc(ppcp_peer_sizeof());
    if (r->mem == NULL) abort();
    if (ppcp_peer_new(r->mem, ppcp_peer_sizeof(), &cfg, &r->p) != PPCP_OK) abort();
    for (i = 0; i < 3; i++)
        if (ppcp_id_set_z(&r->profiles[i], PROF[i]) != PPCP_OK) abort();
    if (ppcp_timebase_make(&r->tb[0], tb_id, strlen(tb_id), PPCP_TB_MONOTONIC, true, 1000)
        != PPCP_OK) abort();
    if (ppcp_timing_make(&timing, PPCP_CONV_MID) != PPCP_OK) abort();
    if (ppcp_capture_profile_make(&r->cp[0], "cp:mic", &timing) != PPCP_OK) abort();
    if (ppcp_source_make(&r->src[0], "src:mic", id, "microphone", tb_id, true, r->cp, 1)
        != PPCP_OK) abort();
    if (ppcp_peer_desc_make(&r->desc, id, role, "1.0", r->profiles, 3, r->tb, 1)
        != PPCP_OK) abort();
    if (ppcp_peer_desc_set_sources(&r->desc, r->src, 1) != PPCP_OK) abort();
    if (with_actuators) {
        /* 5.19b — an Actuator's `kind` is disjoint from every Source's. */
        if (ppcp_actuator_make(&r->act[0], "act:torch", id, PPCP_ACTUATOR_KIND_TORCH,
                               PPCP_ACTUATOR_CONTROL_ON_OFF) != PPCP_OK) abort();
        if (ppcp_actuator_make(&r->act[1], "act:lamp", id,
                               PPCP_ACTUATOR_KIND_INDICATOR_LED,
                               PPCP_ACTUATOR_CONTROL_LEVEL) != PPCP_OK) abort();
        if (ppcp_peer_desc_set_actuators(&r->desc, r->act, 2) != PPCP_OK) abort();
    }
}

static void rig_free(rig *r) { ppcp_peer_free(r->p); free(r->mem); }

/* ------------------------------------------- 12.1c: the embedding's driver
 *
 * A `level` Actuator that moves in FOUR discrete steps, which is 12.1c's own
 * example ("a torch driver rounding to a discrete step").  The achieved value
 * is the step at or below what was asked — never brighter than the request —
 * so a command of 0.5 achieves 0.25 and the ack the host reads is provably not
 * an echo.  An `on_off` Actuator has nothing between on and off, so there the
 * achieved state IS the request; that is a fact about switches, not a licence
 * to echo. */
static double dev_step(double want)
{
    if (want >= 1.0)  return 1.0;
    if (want >= 0.75) return 0.75;
    if (want >= 0.25) return 0.25;
    return 0.0;
}

/* MSG 1c — the responder OWES an answer, and since L30 the engine does not
 * write it.  `status` is how the embedding knows which commands it owes one
 * for: anything but PPCP_OK means the engine already answered (12.1d, 12.1a,
 * 12a) and a second answer would be two replies to one request. */
static void drain_and_answer(ppcp_peer *p)
{
    ppcp_event e;
    while (ppcp_peer_next_event(p, &e) == PPCP_OK) {
        const ppcp_body_actuator_command *c;
        ppcp_actuator_setting achieved;
        if (e.kind != PPCP_EVENT_ACTUATOR_COMMAND || e.msg == NULL || e.status != PPCP_OK)
            continue;
        c = &e.msg->body.actuator_command;
        if (c->setting.has_on) {
            if (ppcp_actuator_setting_on_off(&achieved, c->setting.on) != PPCP_OK)
                continue;
        } else if (ppcp_actuator_setting_level(&achieved, dev_step(c->setting.level))
                   != PPCP_OK) {
            continue;
        }
        CHECK_EQ_I(ppcp_peer_actuator_command_applied(p, c->actuator_id.v, &achieved,
                                                      e.msg->env.msg_id), PPCP_OK);
    }
}

/* ⚠ `drain` is not a detail.  ppcp_peer_feed() stops rather than overrun the
 * 4-deep event ring (F-L13-1), so a pump that never drained would stall — and
 * one that ALWAYS drained would throw away the very ack this test is reading.
 * So the direction whose events are inspected keeps them, and the other does
 * not — and the draining direction is also the one that ANSWERS, because the
 * device's events are where a command arrives. */
static void pump(ppcp_peer *from, ppcp_peer *to, uint8_t ch, bool drain)
{
    static uint8_t buf[65536];
    size_t         n = 0, consumed = 0;
    while (ppcp_peer_pending(from, ch) > 0) {
        if (ppcp_peer_drain(from, ch, buf, sizeof(buf), &n) != PPCP_OK || n == 0)
            break;
        (void)ppcp_peer_feed(to, ch, buf, n, &consumed);
        if (drain)
            drain_and_answer(to);
    }
}

#define TO_DEV(h, d)  pump((h), (d), PPCP_CHANNEL_CONTROL, true)
#define TO_HOST(d, h) pump((d), (h), PPCP_CHANNEL_CONTROL, false)

/* The last ack / error / state this peer saw, and nothing else. */
typedef struct seen {
    bool                  ack;
    ppcp_actuator_verdict verdict;
    bool                  has_state;
    ppcp_actuator_setting state;
    bool                  err;
    ppcp_id               err_code;
    bool                  event_state;
    ppcp_actuator_setting event_setting;
} seen;

static void collect(ppcp_peer *p, seen *s)
{
    ppcp_event e;
    memset(s, 0, sizeof(*s));
    while (ppcp_peer_next_event(p, &e) == PPCP_OK) {
        if (e.msg == NULL)
            continue;
        if (e.kind == PPCP_EVENT_ACTUATOR_COMMAND_ACK) {
            s->ack       = true;
            s->verdict   = e.msg->body.actuator_command_ack.verdict;
            s->has_state = e.msg->body.actuator_command_ack.has_state;
            s->state     = e.msg->body.actuator_command_ack.state;
        } else if (e.kind == PPCP_EVENT_ERROR && e.msg->type == PPCP_MT_ERROR) {
            s->err      = true;
            s->err_code = e.msg->body.error.code;
        } else if (e.kind == PPCP_EVENT_ACTUATOR_STATE) {
            s->event_state   = true;
            s->event_setting = e.msg->body.actuator_state.state;
        }
    }
}

/* A command in whatever shape the caller asks for, sent past
 * ppcp_peer_actuator_command()'s own 12.1a pre-check.  That check is asserted
 * separately; this is how a peer that does NOT have it reaches the wire. */
static void send_raw_command(ppcp_peer *p, const char *actuator_id,
                             const ppcp_actuator_setting *s)
{
    ppcp_msg m;
    if (ppcp_msg_init(&m, PPCP_MT_ACTUATOR_COMMAND, 1) != PPCP_OK) abort();
    if (ppcp_id_set_z(&m.body.actuator_command.actuator_id, actuator_id) != PPCP_OK) abort();
    m.body.actuator_command.setting = *s;
    if (ppcp_peer_send(p, PPCP_CHANNEL_CONTROL, &m) != PPCP_OK) abort();
}

static void declared_half(void)
{
    rig  host, dev;
    seen s;
    ppcp_actuator_setting on_off, level;
    ppcp_instant          since;
    size_t                i;

    rig_new(&host, PPCP_ROLE_HOST,    "peer:host", "tb:host", false);
    rig_new(&dev,  PPCP_ROLE_CAPTURE, "peer:dev",  "tb:dev",  true);

    CHECK_EQ_I(ppcp_peer_hello(host.p), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_declare(host.p, &host.desc), PPCP_OK);
    CHECK_EQ_I(ppcp_peer_declare(dev.p, &dev.desc), PPCP_OK);
    /* hello, hello_accept, both declarations and both `declare_ack`s: run the
     * exchange to quiescence so nothing below is answering a stale frame. */
    for (i = 0; i < 4; i++) {
        pump(host.p, dev.p, PPCP_CHANNEL_CONTROL, true);
        pump(dev.p, host.p, PPCP_CHANNEL_CONTROL, true);
    }
    CHECK_EQ_I(ppcp_peer_pending(host.p, PPCP_CHANNEL_CONTROL), 0);
    CHECK_EQ_I(ppcp_peer_pending(dev.p, PPCP_CHANNEL_CONTROL), 0);

    TEST("E66 / 3.3f — `actuators` crosses `declare` and both are visible to the host");
    {
        const ppcp_peer_desc *cp = ppcp_peer_counterpart(host.p);
        CHECK(cp != NULL);
        if (cp != NULL) {
            CHECK_EQ_I(cp->actuator_count, 2);
            CHECK_EQ_I(cp->source_count, 1);
        }
    }

    CHECK_EQ_I(ppcp_actuator_setting_on_off(&on_off, true), PPCP_OK);
    CHECK_EQ_I(ppcp_actuator_setting_level(&level, 0.5), PPCP_OK);

    /* ------------------------------------- 1: on_off, actuator_command */

    TEST("I39 (1) — a `level` command at an `on_off` Actuator is `error`/`malformed`");
    {
        /* The originator's own 12.1a check first: it never leaves this peer. */
        CHECK_EQ_I(ppcp_peer_actuator_command(host.p, "act:torch", &level),
                   PPCP_ERR_INVALID);
        CHECK_EQ_I(ppcp_peer_pending(host.p, PPCP_CHANNEL_CONTROL), 0);
        /* And a peer without that check, answered by ours. */
        send_raw_command(host.p, "act:torch", &level);
        TO_DEV(host.p, dev.p);
        TO_HOST(dev.p, host.p);
        collect(host.p, &s);
        CHECK(s.err);
        CHECK(ppcp_cbor_key_is(s.err_code.v, s.err_code.len, PPCP_ERRCODE_MALFORMED));
        CHECK(!s.ack);                          /* refused, not acted on */
    }

    TEST("I39 (1) — an `on` command at an `on_off` Actuator is accepted");
    {
        CHECK_EQ_I(ppcp_peer_actuator_command(host.p, "act:torch", &on_off), PPCP_OK);
        TO_DEV(host.p, dev.p);
        TO_HOST(dev.p, host.p);
        collect(host.p, &s);
        CHECK(s.ack);
        CHECK_EQ_I(s.verdict, PPCP_ACTUATOR_APPLIED);
        CHECK(!s.err);
    }

    /* --------------------------------------- 4: level, actuator_command */

    TEST("I39 (4) — an `on` command at a `level` Actuator is `error`/`malformed`");
    {
        CHECK_EQ_I(ppcp_peer_actuator_command(host.p, "act:lamp", &on_off),
                   PPCP_ERR_INVALID);
        CHECK_EQ_I(ppcp_peer_pending(host.p, PPCP_CHANNEL_CONTROL), 0);
        send_raw_command(host.p, "act:lamp", &on_off);
        TO_DEV(host.p, dev.p);
        TO_HOST(dev.p, host.p);
        collect(host.p, &s);
        CHECK(s.err);
        CHECK(ppcp_cbor_key_is(s.err_code.v, s.err_code.len, PPCP_ERRCODE_MALFORMED));
        CHECK(!s.ack);
    }

    TEST("I39 (4) — a `level` command at a `level` Actuator is accepted");
    {
        CHECK_EQ_I(ppcp_peer_actuator_command(host.p, "act:lamp", &level), PPCP_OK);
        TO_DEV(host.p, dev.p);
        TO_HOST(dev.p, host.p);
        collect(host.p, &s);
        CHECK(s.ack);
        CHECK_EQ_I(s.verdict, PPCP_ACTUATOR_APPLIED);
    }

    /* ------------------------ 2 & 5: actuator_command_ack.state, per kind
     *
     * 12.1c1 read against the DECLARATION: the ack's `state` is in the shape
     * that Actuator's `control` names, and never the other one. */

    TEST("I39 (2) — the `applied` ack for an `on_off` Actuator carries `on`, not `level`");
    {
        CHECK_EQ_I(ppcp_peer_actuator_command(host.p, "act:torch", &on_off), PPCP_OK);
        TO_DEV(host.p, dev.p);
        TO_HOST(dev.p, host.p);
        collect(host.p, &s);
        CHECK(s.ack && s.has_state);
        CHECK(s.state.has_on);
        CHECK(!s.state.has_level);
        CHECK_EQ_I(ppcp_actuator_setting_validate(&s.state), PPCP_OK);
    }

    TEST("I39 (5) — the `applied` ack for a `level` Actuator carries `level`, not `on`");
    {
        CHECK_EQ_I(ppcp_peer_actuator_command(host.p, "act:lamp", &level), PPCP_OK);
        TO_DEV(host.p, dev.p);
        TO_HOST(dev.p, host.p);
        collect(host.p, &s);
        CHECK(s.ack && s.has_state);
        CHECK(s.state.has_level);
        CHECK(!s.state.has_on);
        CHECK_EQ_I(ppcp_actuator_setting_validate(&s.state), PPCP_OK);
    }

    /* ------------------------------------------------------------ 12.1c
     *
     * ⭐ THE ACHIEVED VALUE, NOT THE REQUESTED ONE.  0.5 was asked for; the
     * driver above moves in steps and reaches 0.25; the ack says 0.25.  The
     * engine cannot produce this answer — it has no hardware — and the version
     * of this library that wrote the ack itself could only ever have echoed
     * 0.5, which is the failure both application teams reported. */
    TEST("12.1c — `state` is what the Actuator ACHIEVED, and not an echo of the request");
    {
        CHECK_EQ_I(ppcp_peer_actuator_command(host.p, "act:lamp", &level), PPCP_OK);
        TO_DEV(host.p, dev.p);
        TO_HOST(dev.p, host.p);
        collect(host.p, &s);
        CHECK(s.ack && s.has_state);
        CHECK_EQ_I(s.verdict, PPCP_ACTUATOR_APPLIED);
        CHECK(s.state.level == 0.25);           /* the clamped step */
        CHECK(s.state.level != level.level);    /* 0.5 was what was asked for */
    }

    /* MSG 1c — the answer is the EMBEDDING's, so the engine holds it to the
     * same rules it held its own to.  None of these reaches the wire. */
    TEST("12.1b / 12.1c1 — the embedding cannot build an ack the wire forbids");
    {
        /* I39 — a `level` achieved state on an `on_off` Actuator. */
        CHECK_EQ_I(ppcp_peer_actuator_command_applied(dev.p, "act:torch", &level, 7u),
                   PPCP_ERR_INVALID);
        /* …and an `on` one on a `level` Actuator. */
        CHECK_EQ_I(ppcp_peer_actuator_command_applied(dev.p, "act:lamp", &on_off, 7u),
                   PPCP_ERR_INVALID);
        /* 12.1c1 — an `applied` ack with no state at all is unconstructible. */
        CHECK_EQ_I(ppcp_peer_actuator_command_applied(dev.p, "act:torch", NULL, 7u),
                   PPCP_ERR_INVALID);
        /* 12.1b — a `refused` ack with no `reason` is too. */
        CHECK_EQ_I(ppcp_peer_actuator_command_refused(dev.p, "act:torch", NULL, 7u),
                   PPCP_ERR_INVALID);
        CHECK_EQ_I(ppcp_peer_actuator_command_refused(dev.p, "act:torch", "", 7u),
                   PPCP_ERR_INVALID);
        /* 5.19a — and neither verdict can be given for an Actuator this peer
         * never declared: the engine has already answered `not_declared`. */
        CHECK_EQ_I(ppcp_peer_actuator_command_applied(dev.p, "act:nonesuch", &on_off, 7u),
                   PPCP_ERR_NOT_FOUND);
        CHECK_EQ_I(ppcp_peer_actuator_command_refused(dev.p, "act:nonesuch", "busy", 7u),
                   PPCP_ERR_NOT_FOUND);
        CHECK_EQ_I(ppcp_peer_pending(dev.p, PPCP_CHANNEL_CONTROL), 0);
        /* The one that IS well-formed: a refusal with a registry reason. */
        CHECK_EQ_I(ppcp_peer_actuator_command_refused(dev.p, "act:torch",
                                                      "thermal_limit", 7u), PPCP_OK);
        TO_HOST(dev.p, host.p);
        collect(host.p, &s);
        CHECK(s.ack);
        CHECK_EQ_I(s.verdict, PPCP_ACTUATOR_REFUSED);
        CHECK(!s.has_state);
    }

    /* ------------------------------ 3 & 6: actuator_state.state, per kind */

    CHECK_EQ_I(ppcp_instant_make_z(&since, "tb:dev", 5000), PPCP_OK);

    TEST("I39 (3) — `actuator_state` for an `on_off` Actuator refuses a `level`, "
         "and carries `on`");
    {
        CHECK_EQ_I(ppcp_peer_actuator_state(dev.p, "act:torch", &level, &since),
                   PPCP_ERR_INVALID);
        CHECK_EQ_I(ppcp_peer_pending(dev.p, PPCP_CHANNEL_CONTROL), 0);
        CHECK_EQ_I(ppcp_peer_actuator_state(dev.p, "act:torch", &on_off, &since), PPCP_OK);
        TO_HOST(dev.p, host.p);
        collect(host.p, &s);
        CHECK(s.event_state);
        CHECK(s.event_setting.has_on);
        CHECK(!s.event_setting.has_level);
    }

    TEST("I39 (6) — `actuator_state` for a `level` Actuator refuses an `on`, "
         "and carries `level`");
    {
        CHECK_EQ_I(ppcp_peer_actuator_state(dev.p, "act:lamp", &on_off, &since),
                   PPCP_ERR_INVALID);
        CHECK_EQ_I(ppcp_peer_pending(dev.p, PPCP_CHANNEL_CONTROL), 0);
        CHECK_EQ_I(ppcp_peer_actuator_state(dev.p, "act:lamp", &level, &since), PPCP_OK);
        TO_HOST(dev.p, host.p);
        collect(host.p, &s);
        CHECK(s.event_state);
        CHECK(s.event_setting.has_level);
        CHECK(!s.event_setting.has_on);
    }

    /* 5.19a — an Actuator this peer never declared has no state to report,
     * which is the emitter's half of the same rule the paired row drives from
     * the responder's side. */
    TEST("5.19a / 12.1d — neither end names an Actuator that was never declared");
    {
        CHECK_EQ_I(ppcp_peer_actuator_state(dev.p, "act:nonesuch", &on_off, &since),
                   PPCP_ERR_NOT_FOUND);
        CHECK_EQ_I(ppcp_peer_actuator_command(host.p, "act:nonesuch", &on_off),
                   PPCP_ERR_NOT_FOUND);
        CHECK_EQ_I(ppcp_peer_pending(dev.p, PPCP_CHANNEL_CONTROL), 0);
        CHECK_EQ_I(ppcp_peer_pending(host.p, PPCP_CHANNEL_CONTROL), 0);
    }

    /* ⚠ 12.1d's RESPONDER half and 12a are the paired assertions of this row
     * and are NOT asserted here — see CT-I39-sockets-not-declared and
     * CT-I39-sockets-non-host in tests/CMakeLists.txt.  The engine's refusals
     * above are the originator's half only, and CONF §2c is explicit that one
     * implementation talking to itself is not the demonstration. */

    rig_free(&host);
    rig_free(&dev);
}

int main(void)
{
    decode_half();
    declared_half();
    TEST_MAIN_END();
}
