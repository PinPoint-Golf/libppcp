/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * PPCP-RV §11.5 — the five-frame exchange as a sans-I/O state machine, both
 * roles (CA2).
 *
 * ⛔⛔ READ 11.5c BEFORE CHANGING ANYTHING IN THIS FILE.  The acceptor replies
 * `bs_accept` HAVING SEEN ONLY A COMMITMENT to the initiator's key, and that
 * ordering is the whole of what stops an attacker choosing the digits.  An
 * implementation that sends `pk_a` only after receiving `pk_i` — a
 * natural-looking reordering that saves a round trip — destroys the security
 * of this path entirely: the interposer picks its key after seeing the honest
 * one and grinds until both legs display the same six digits, which is seconds
 * of work.  ⛔ NOTHING ON THE WIRE CHANGES AND NO STATIC TEST CAN SEE IT.
 * RT-20b(ii) is the only thing that catches it, and it needs the relay.
 *
 * The defence here is structural rather than a comment: `pk_own` is taken at
 * init, before any frame exists, and recv() returns `bs_accept` in the same
 * call that consumed `bs_offer`.  There is no state in which this engine holds
 * an offer and waits, and no entry point through which a caller could make one.
 *
 * ⛔ AND THE OTHER HALF, WHICH IS NOT THIS FILE'S TO ENFORCE.  The digits are
 * compared BY A PERSON, across a channel the attacker is not on (11.1d).  This
 * engine never compares them, never receives the counterpart's opinion of them,
 * and offers no call that would.  A peer that automated it would pass every
 * test in this repository and authenticate nothing.
 */
#include "ppcp/bootstrap.h"

#include <string.h>

/* 11.4c — a second frame of a type already received is `malformed`.  One bit
 * per type; the exchange is five frames long and there is nothing to
 * resynchronise to. */
#define SEEN_OFFER   0x01u
#define SEEN_ACCEPT  0x02u
#define SEEN_REVEAL  0x04u
#define SEEN_CONFIRM 0x08u

static void wipe(void *p, size_t n)
{
    volatile unsigned char *q = (volatile unsigned char *)p;
    while (n-- > 0u)
        *q++ = 0u;
}

static void step_init(ppcp_bs_step *step)
{
    memset(step, 0, sizeof(*step));
}

void ppcp_bs_engine_wipe(ppcp_bs_engine *e)
{
    if (e == NULL)
        return;
    /* 11.6f as amended by E51, and 11.7f.  EVERYTHING: the ephemeral private
     * material, the digits, and — because until 11.5g is met the pairing does
     * not exist — `PRK`, `K_tls`, `K_id` and `sid` too. */
    ppcp_rv_bootstrap_wipe(&e->bs);
    wipe(&e->pairing, sizeof(e->pairing));
    wipe(e->pk_i, sizeof(e->pk_i));
    wipe(e->pk_a, sizeof(e->pk_a));
    wipe(e->pk_own, sizeof(e->pk_own));
    wipe(e->peer_mac, sizeof(e->peer_mac));
    e->has_pairing       = false;
    e->affirmed          = false;
    e->peer_verified     = false;
    e->peer_confirm_held = false;
    e->state             = PPCP_BS_ST_DONE;
}

ppcp_bs_state ppcp_bs_engine_state(const ppcp_bs_engine *e)
{
    return (e == NULL) ? PPCP_BS_ST_DONE : e->state;
}

/* Ends the attempt and puts `bs_abort` on the wire.  11.9a: any abort ends the
 * attempt, closes the window and leaves NO pairing at either peer — so the
 * erasure happens here, on the way out, for every reason code alike. */
static ppcp_result emit_abort(ppcp_bs_engine *e, ppcp_bs_reason rc, ppcp_bs_step *step)
{
    ppcp_bs_frame f;
    ppcp_result   rc_write;

    memset(&f, 0, sizeof(f));
    f.ty = PPCP_BS_ABORT;
    f.rc = rc;

    rc_write = ppcp_bs_frame_write(&f, step->out, sizeof(step->out), &step->out_len);
    step->has_out = (rc_write == PPCP_OK);
    step->event   = PPCP_BS_EV_ABORTED;
    step->rc      = rc;
    step->close   = true;

    ppcp_bs_engine_wipe(e);
    return PPCP_OK;
}

/* 11.3c — the acceptor's first frame only: close WITHOUT REPLY.  The line
 * between this and an abort is whether the counterpart has demonstrated it
 * speaks this protocol; something that has not gets nothing to learn from. */
static ppcp_result close_silently(ppcp_bs_engine *e, ppcp_bs_reason rc,
                                  ppcp_bs_step *step)
{
    step->has_out = false;
    step->out_len = 0;
    step->event   = PPCP_BS_EV_ABORTED;
    step->rc      = rc;
    step->close   = true;
    ppcp_bs_engine_wipe(e);
    return PPCP_OK;
}

ppcp_result ppcp_bs_engine_init(ppcp_bs_engine *e, ppcp_bs_role role, uint8_t v,
                                const uint8_t pk_own[PPCP_RV_BS_KEY_BYTES])
{
    if (e == NULL || pk_own == NULL)
        return PPCP_ERR_INVALID;
    if (role != PPCP_BS_ROLE_INITIATOR && role != PPCP_BS_ROLE_ACCEPTOR)
        return PPCP_ERR_INVALID;
    if (v == 0u)
        return PPCP_ERR_MALFORMED;      /* 11.4h1 — v is 1..255 */

    memset(e, 0, sizeof(*e));
    e->role  = role;
    e->v     = v;
    e->state = PPCP_BS_ST_NEW;
    memcpy(e->pk_own, pk_own, PPCP_RV_BS_KEY_BYTES);
    return PPCP_OK;
}

ppcp_result ppcp_bs_engine_start(ppcp_bs_engine *e, ppcp_bs_step *step)
{
    ppcp_bs_frame f;
    ppcp_result   rc;

    if (e == NULL || step == NULL)
        return PPCP_ERR_INVALID;
    step_init(step);
    if (e->role != PPCP_BS_ROLE_INITIATOR || e->state != PPCP_BS_ST_NEW)
        return PPCP_ERR_INVALID;

    /* 11.5b — `ct` and NOT pk_i.  The initiator commits to its key without
     * revealing it, so that when `pk_a` comes back the acceptor cannot have
     * chosen it in response to `pk_i`. */
    memset(&f, 0, sizeof(f));
    f.ty = PPCP_BS_OFFER;
    f.v  = e->v;
    ppcp_rv_bs_commit(e->pk_own, f.ct);
    memcpy(e->ct, f.ct, PPCP_RV_BS_CT_BYTES);

    rc = ppcp_bs_frame_write(&f, step->out, sizeof(step->out), &step->out_len);
    if (rc != PPCP_OK)
        return rc;
    step->has_out = true;
    e->state      = PPCP_BS_ST_AWAIT_ACCEPT;
    return PPCP_OK;
}

/* 11.5f — verify the counterpart's MAC in constant time, with the reflection
 * guard.  Called either when `bs_confirm` arrives (if the digits already
 * exist) or the moment they do. */
static void verify_peer_confirm(ppcp_bs_engine *e)
{
    const uint8_t *own      = (e->role == PPCP_BS_ROLE_INITIATOR)
                                  ? e->bs.mac_i : e->bs.mac_a;
    const uint8_t *expected = (e->role == PPCP_BS_ROLE_INITIATOR)
                                  ? e->bs.mac_a : e->bs.mac_i;

    /* "A peer that receives its own MAC value aborts with `rejected`."  The
     * two labels of 11.5f already differ, so a reflection fails the ordinary
     * check as well — this is the clause stated explicitly, and both outcomes
     * are `rejected`, which is what makes them indistinguishable (11.4f). */
    if (ppcp_rv_ct_equal(e->peer_mac, own, PPCP_RV_BS_MAC_BYTES)) {
        e->peer_verified = false;
        return;
    }
    e->peer_verified = ppcp_rv_ct_equal(e->peer_mac, expected, PPCP_RV_BS_MAC_BYTES);
}

/* 11.5g — the pairing exists ONLY when a peer has BOTH affirmed at its own end
 * and verified the counterpart's MAC.  Until then it holds nothing and MUST
 * NOT persist, advertise or offer anything derived from the exchange. */
static void maybe_pair(ppcp_bs_engine *e, ppcp_bs_step *step)
{
    if (!e->affirmed || !e->peer_verified)
        return;

    memcpy(e->pairing.sid,        e->bs.sid,   PPCP_RV_SID_BYTES);
    memcpy(e->pairing.keys.prk,   e->bs.prk,   PPCP_RV_KEY_BYTES);
    memcpy(e->pairing.keys.k_tls, e->bs.k_tls, PPCP_RV_KEY_BYTES);
    memcpy(e->pairing.keys.k_id,  e->bs.k_id,  PPCP_RV_KEY_BYTES);
    e->has_pairing = true;

    /* ⛔ TRAP 6, DONE HERE SO A CALLER CANNOT GET IT WRONG.  The handshake has
     * ended, so 11.6f erases the ephemeral half NOW — BK, K_c, sas_raw, the
     * digits and both MACs — rather than leaving them alive inside a struct
     * the caller keeps because it holds the PRK.  11.7f is the same
     * instruction for the digits: they are a function of two ephemeral keys
     * and are meaningless outside the attempt that produced them. */
    ppcp_rv_bootstrap_wipe(&e->bs);
    wipe(e->peer_mac, sizeof(e->peer_mac));

    e->state    = PPCP_BS_ST_PAIRED;
    step->event = PPCP_BS_EV_PAIRED;
    /* 11.5h — the bootstrap connection is closed once both MACs have verified.
     * It is not reused, not upgraded in place and not held open; the peers
     * reconnect under §5, in whichever direction 11.2b puts them. */
    step->close = true;
}

ppcp_result ppcp_bs_engine_supply_secret(ppcp_bs_engine *e,
                                         const uint8_t z[PPCP_RV_BS_KEY_BYTES],
                                         ppcp_bs_step *step)
{
    ppcp_result rc;

    if (e == NULL || z == NULL || step == NULL)
        return PPCP_ERR_INVALID;
    step_init(step);
    if (e->state != PPCP_BS_ST_AWAIT_SECRET)
        return PPCP_ERR_INVALID;

    /* One call, one transcript construction (11.6c), initiator first. */
    rc = ppcp_rv_bootstrap_derive(z, e->v, e->pk_i, e->pk_a, &e->bs);
    if (rc == PPCP_ERR_RV_INVALID_KEY) {
        /* 11.6b — an all-zero Z: abort with `invalid_key`, derive nothing.
         * ⛔ NOT a transport error, and there is no retry here or anywhere
         * above it: a rejected key is an attack signal and a retry loop eats
         * 3.7b's single-attempt bound (trap 7).  The engine is single-shot, so
         * this really is the end of the attempt. */
        return emit_abort(e, PPCP_BS_RC_INVALID_KEY, step);
    }
    if (rc != PPCP_OK) {
        ppcp_bs_engine_wipe(e);
        return rc;
    }

    e->state    = PPCP_BS_ST_COMPARE;
    step->event = PPCP_BS_EV_COMPARE;

    /* The counterpart may have affirmed and sent `bs_confirm` before this side
     * was handed Z — its user is not waiting on our local arithmetic.  That
     * frame is in order (11.5f follows 11.5d for the receiver) and was held
     * because K_c did not exist yet; it is verified now. */
    if (e->peer_confirm_held) {
        verify_peer_confirm(e);
        if (!e->peer_verified)
            return emit_abort(e, PPCP_BS_RC_REJECTED, step);
        maybe_pair(e, step);
    }
    return PPCP_OK;
}

ppcp_result ppcp_bs_engine_sas(const ppcp_bs_engine *e, uint32_t *out_sas)
{
    if (e == NULL || out_sas == NULL)
        return PPCP_ERR_INVALID;
    /* 11.7e — nothing before 11.5d is complete: there is nothing to compare
     * yet and a progressive display would leak the value to whichever side an
     * attacker reached first.  11.7f — and nothing after the attempt ends. */
    if (e->state != PPCP_BS_ST_COMPARE)
        return PPCP_ERR_INVALID;
    *out_sas = e->bs.sas;
    return PPCP_OK;
}

ppcp_result ppcp_bs_engine_affirm(ppcp_bs_engine *e, ppcp_bs_step *step)
{
    ppcp_bs_frame f;
    ppcp_result   rc;

    if (e == NULL || step == NULL)
        return PPCP_ERR_INVALID;
    step_init(step);
    if (e->state != PPCP_BS_ST_COMPARE)
        return PPCP_ERR_INVALID;
    if (e->affirmed)
        return PPCP_ERR_INVALID;     /* one affirmation, one attempt */

    /* 11.5e / 11.7c — neither peer sends `bs_confirm` before ITS OWN user has
     * affirmed, and the counterpart's `bs_confirm` never stands in for it. */
    memset(&f, 0, sizeof(f));
    f.ty = PPCP_BS_CONFIRM;
    memcpy(f.mac,
           (e->role == PPCP_BS_ROLE_INITIATOR) ? e->bs.mac_i : e->bs.mac_a,
           PPCP_RV_BS_MAC_BYTES);

    rc = ppcp_bs_frame_write(&f, step->out, sizeof(step->out), &step->out_len);
    if (rc != PPCP_OK)
        return rc;
    step->has_out = true;
    e->affirmed   = true;

    maybe_pair(e, step);
    return PPCP_OK;
}

ppcp_result ppcp_bs_engine_abort(ppcp_bs_engine *e, ppcp_bs_reason rc,
                                 ppcp_bs_step *step)
{
    if (e == NULL || step == NULL)
        return PPCP_ERR_INVALID;
    if ((int)rc < (int)PPCP_BS_RC_UNSUPPORTED_VERSION ||
        (int)rc > (int)PPCP_BS_RC_MALFORMED)
        return PPCP_ERR_INVALID;
    step_init(step);
    if (e->state == PPCP_BS_ST_DONE)
        return PPCP_ERR_INVALID;
    return emit_abort(e, rc, step);
}

ppcp_result ppcp_bs_engine_take_pairing(ppcp_bs_engine *e, ppcp_bs_pairing *out)
{
    if (e == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    /* 11.5g in one line.  Every other state holds nothing worth taking, and
     * saying so here is what stops an embedding persisting a `PRK` for a
     * pairing that does not exist. */
    if (e->state != PPCP_BS_ST_PAIRED || !e->has_pairing)
        return PPCP_ERR_INVALID;

    memcpy(out, &e->pairing, sizeof(*out));
    /* Taken means taken: the engine keeps no copy, and is finished. */
    ppcp_bs_engine_wipe(e);
    return PPCP_OK;
}

/* ------------------------------------------------------------------ inbound */

static ppcp_result on_offer(ppcp_bs_engine *e, const ppcp_bs_frame *f,
                            ppcp_bs_step *step)
{
    ppcp_bs_frame reply;
    ppcp_result   rc;

    /* 11.4e — a `v` this peer does not implement: abort with
     * `unsupported_version`, and the embedding reports to its USER that the
     * counterpart requires a newer version of the application, not a generic
     * failure.  11.9d1 then has it offer the pairing code on the FIRST such
     * abort, because a second attempt is guaranteed to fail identically. */
    if (f->v != e->v)
        return emit_abort(e, PPCP_BS_RC_UNSUPPORTED_VERSION, step);

    memcpy(e->ct, f->ct, PPCP_RV_BS_CT_BYTES);

    /* ⛔⛔ 11.5c — `bs_accept` GOES OUT NOW, IN THE SAME CALL, CARRYING A KEY
     * FIXED BEFORE THIS FRAME ARRIVED.  pk_i is not here, has not been seen,
     * and cannot influence pk_a.  Do not move this below the reveal to save a
     * round trip: that is the one change to this file that leaves every byte
     * on the wire identical and deletes the security of the whole path. */
    memset(&reply, 0, sizeof(reply));
    reply.ty = PPCP_BS_ACCEPT;
    /* 11.4h1 — the acceptor ECHOES the `v` it received and never substitutes
     * a different one, so exactly one `v` is in play and 11.6c's transcript is
     * unambiguous: it binds the `v` it received, equal to the one it echoed. */
    reply.v  = f->v;
    memcpy(reply.pk, e->pk_own, PPCP_RV_BS_KEY_BYTES);

    rc = ppcp_bs_frame_write(&reply, step->out, sizeof(step->out), &step->out_len);
    if (rc != PPCP_OK) {
        ppcp_bs_engine_wipe(e);
        return rc;
    }
    step->has_out = true;
    e->state      = PPCP_BS_ST_AWAIT_REVEAL;
    return PPCP_OK;
}

static ppcp_result on_accept(ppcp_bs_engine *e, const ppcp_bs_frame *f,
                             ppcp_bs_step *step)
{
    ppcp_bs_frame reveal;
    ppcp_result   rc;

    /* 11.4h — an initiator aborts with `unsupported_version` if `bs_accept.v`
     * differs from the `v` it sent.  Together with 11.4i's binding this closes
     * the both-directions rewrite: an attacker rewriting `v` down outbound and
     * back up inbound passes this echo check, and then the digits differ and
     * the MACs fail. */
    if (f->v != e->v)
        return emit_abort(e, PPCP_BS_RC_UNSUPPORTED_VERSION, step);

    /* 11.6c's order, fixed by role and not by arrival: initiator first. */
    memcpy(e->pk_i, e->pk_own, PPCP_RV_BS_KEY_BYTES);
    memcpy(e->pk_a, f->pk,     PPCP_RV_BS_KEY_BYTES);

    /* 11.5d — the initiator sends `bs_reveal` carrying pk_i. */
    memset(&reveal, 0, sizeof(reveal));
    reveal.ty = PPCP_BS_REVEAL;
    memcpy(reveal.pk, e->pk_own, PPCP_RV_BS_KEY_BYTES);
    rc = ppcp_bs_frame_write(&reveal, step->out, sizeof(step->out), &step->out_len);
    if (rc != PPCP_OK) {
        ppcp_bs_engine_wipe(e);
        return rc;
    }
    step->has_out = true;

    e->state    = PPCP_BS_ST_AWAIT_SECRET;
    step->event = PPCP_BS_EV_NEED_SECRET;
    memcpy(step->peer_pk, e->pk_a, PPCP_RV_BS_KEY_BYTES);
    return PPCP_OK;
}

static ppcp_result on_reveal(ppcp_bs_engine *e, const ppcp_bs_frame *f,
                             ppcp_bs_step *step)
{
    uint8_t recomputed[PPCP_RV_BS_CT_BYTES];
    bool    ok;

    /* 11.5d — recompute SHA-256("ppcp1 bs-commit" || pk_i), compare to the
     * `ct` received IN CONSTANT TIME, and abort with `commitment_mismatch` on
     * any difference. */
    ppcp_rv_bs_commit(f->pk, recomputed);
    ok = ppcp_rv_ct_equal(recomputed, e->ct, PPCP_RV_BS_CT_BYTES);
    wipe(recomputed, sizeof(recomputed));

    /* "It MUST NOT derive anything from a pk_i that failed this check" — so
     * the key is not stored until the check has passed. */
    if (!ok)
        return emit_abort(e, PPCP_BS_RC_COMMITMENT_MISMATCH, step);

    memcpy(e->pk_i, f->pk,     PPCP_RV_BS_KEY_BYTES);
    memcpy(e->pk_a, e->pk_own, PPCP_RV_BS_KEY_BYTES);

    /* No outbound frame: 11.5e has the acceptor derive, display, and wait for
     * ITS OWN user before `bs_confirm`. */
    e->state    = PPCP_BS_ST_AWAIT_SECRET;
    step->event = PPCP_BS_EV_NEED_SECRET;
    memcpy(step->peer_pk, e->pk_i, PPCP_RV_BS_KEY_BYTES);
    return PPCP_OK;
}

static ppcp_result on_confirm(ppcp_bs_engine *e, const ppcp_bs_frame *f,
                              ppcp_bs_step *step)
{
    memcpy(e->peer_mac, f->mac, PPCP_RV_BS_MAC_BYTES);

    if (e->state == PPCP_BS_ST_AWAIT_SECRET) {
        /* Held rather than refused: the counterpart's user does not wait on
         * this side being handed `Z`, and the frame is in order.  It is
         * verified the moment K_c exists. */
        e->peer_confirm_held = true;
        return PPCP_OK;
    }

    verify_peer_confirm(e);
    if (!e->peer_verified) {
        /* ⛔ 11.4f — `rejected`, the SAME code a user's refusal produces, and
         * indistinguishable from it to the counterpart.
         *
         * ⚠ And E37's correction to the reasoning: an interposed attacker
         * CANNOT fail this MAC — it holds Z on both legs, so K_c on both, and
         * forges both correctly and trivially.  A failure here is evidence
         * that no such attacker is present and that something else is wrong,
         * overwhelmingly an implementation disagreement of the PRK-divergence
         * class §10.4 warns about.  The MAC is not the authentication check;
         * the comparison is. */
        return emit_abort(e, PPCP_BS_RC_REJECTED, step);
    }

    step->event = PPCP_BS_EV_NONE;
    maybe_pair(e, step);
    return PPCP_OK;
}

ppcp_result ppcp_bs_engine_recv(ppcp_bs_engine *e, const uint8_t *buf, size_t len,
                                size_t *out_consumed, ppcp_bs_step *step)
{
    ppcp_bs_frame f;
    ppcp_result   rc;
    size_t        consumed = 0;
    bool          first_frame;
    unsigned      bit = 0u;

    if (e == NULL || buf == NULL || out_consumed == NULL || step == NULL)
        return PPCP_ERR_INVALID;

    /* ⚠ THE FRAME IS DECODED BEFORE THE STEP IS CLEARED, AND THE ORDER IS
     * DELIBERATE.  The natural way to drive two of these — and what the relay
     * of L21 will do — is to hand one engine's `step.out` straight to the
     * other's recv() with that same step as the output:
     *
     *     ppcp_bs_engine_recv(peer, s.out, s.out_len, &n, &s);
     *
     * `buf` then points INTO `step`, so clearing the step first would zero the
     * frame before reading it.  ppcp_bs_frame_read() copies every field out by
     * value, so once it has returned the input buffer is finished with and the
     * aliasing is harmless.  Written the other way round this compiles, passes
     * a test that uses two separate buffers, and fails for the caller who
     * writes the obvious loop. */
    rc = ppcp_bs_frame_read(buf, len, &f, &consumed);

    step_init(step);
    *out_consumed = 0;
    if (e->state == PPCP_BS_ST_DONE || e->state == PPCP_BS_ST_PAIRED)
        return PPCP_ERR_INVALID;

    /* 11.3c's "first frame" is the acceptor's, before it has replied to
     * anything.  An initiator has already spoken, and so has whatever answers
     * it. */
    first_frame = (e->role == PPCP_BS_ROLE_ACCEPTOR && e->state == PPCP_BS_ST_NEW);

    if (rc == PPCP_ERR_TRUNCATED)
        return PPCP_ERR_TRUNCATED;      /* state untouched; read more, retry */
    if (rc != PPCP_OK) {
        if (first_frame)
            return close_silently(e, PPCP_BS_RC_MALFORMED, step);
        return emit_abort(e, PPCP_BS_RC_MALFORMED, step);
    }
    *out_consumed = consumed;

    /* 11.4c — a second frame of a type already received.  Checked before the
     * order table so a repeat reads as the repeat it is. */
    switch (f.ty) {
    case PPCP_BS_OFFER:   bit = SEEN_OFFER;   break;
    case PPCP_BS_ACCEPT:  bit = SEEN_ACCEPT;  break;
    case PPCP_BS_REVEAL:  bit = SEEN_REVEAL;  break;
    case PPCP_BS_CONFIRM: bit = SEEN_CONFIRM; break;
    default:              bit = 0u;           break;   /* abort is terminal */
    }
    if (bit != 0u && (e->seen & bit) != 0u)
        return emit_abort(e, PPCP_BS_RC_MALFORMED, step);
    e->seen |= (uint8_t)bit;

    /* An abort from the counterpart ends the attempt wherever it arrives.
     * 11.9a: no pairing at either peer, and the erasure is unconditional. */
    if (f.ty == PPCP_BS_ABORT) {
        if (first_frame)
            return close_silently(e, f.rc, step);
        step->has_out = false;
        step->event   = PPCP_BS_EV_ABORTED;
        step->rc      = f.rc;
        step->close   = true;
        ppcp_bs_engine_wipe(e);
        return PPCP_OK;
    }

    /* 11.3c — an acceptor whose first frame is not a well-formed `bs_offer`
     * closes WITHOUT REPLY. */
    if (first_frame && f.ty != PPCP_BS_OFFER)
        return close_silently(e, PPCP_BS_RC_MALFORMED, step);

    /* 11.4c — the order of §11.5 is the whole of what is legal here, and a
     * peer that has lost track of where it is in a five-frame exchange has
     * nothing to resynchronise to.  No recovery is attempted. */
    if (e->role == PPCP_BS_ROLE_ACCEPTOR) {
        if (f.ty == PPCP_BS_OFFER && e->state == PPCP_BS_ST_NEW)
            return on_offer(e, &f, step);
        if (f.ty == PPCP_BS_REVEAL && e->state == PPCP_BS_ST_AWAIT_REVEAL)
            return on_reveal(e, &f, step);
        if (f.ty == PPCP_BS_CONFIRM &&
            (e->state == PPCP_BS_ST_AWAIT_SECRET || e->state == PPCP_BS_ST_COMPARE))
            return on_confirm(e, &f, step);
    } else {
        if (f.ty == PPCP_BS_ACCEPT && e->state == PPCP_BS_ST_AWAIT_ACCEPT)
            return on_accept(e, &f, step);
        if (f.ty == PPCP_BS_CONFIRM &&
            (e->state == PPCP_BS_ST_AWAIT_SECRET || e->state == PPCP_BS_ST_COMPARE))
            return on_confirm(e, &f, step);
    }

    return emit_abort(e, PPCP_BS_RC_MALFORMED, step);
}
