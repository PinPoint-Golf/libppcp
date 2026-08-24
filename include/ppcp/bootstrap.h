/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * bootstrap.h — PPCP-RV §11.4's five bootstrap frames (L19) and §11.5's
 * exchange as a sans-I/O engine (L20).
 *
 * RV-6 is a first pairing between peers that have never met, with no code
 * carried between two screens.  Six digits appear on both, a person compares
 * them, and each end's own user affirms.
 *
 * ⛔ THE ONE THING THIS PATH CANNOT BE (11.1d).  The comparison has value ONLY
 * because it crosses a channel the attacker is not on, and the only such
 * channel is a person looking at two screens.  An embedding that matches the
 * digits itself — across a channel the peer also controls, or by accepting the
 * counterpart's assertion that they matched — removes the entire security of
 * the path while leaving every byte on the wire unchanged, and passes every
 * static test in the document.  This library therefore never compares digits
 * and offers no call that would: ppcp_bs_engine_sas() hands them to a screen,
 * and ppcp_bs_engine_affirm() takes a decision made by a person.
 *
 * ⛔ AND THE COMPANION (11.1c, 2c).  A peer never establishes a pairing by
 * accepting an unauthenticated channel and trusting what arrives on it.  Trust
 * on first use is not a permitted reading of §11.
 *
 * What the embedding owns, and this library does not (ground rule 8, 11.11):
 * the socket, the bootstrap window and its timers (3.7b, 11.3e), the CSPRNG,
 * the X25519 keypair, the shared secret `Z`, the screen and the user's
 * affirmation.  This header takes an inbound frame and gives back an outbound
 * one or an abort.
 */
#ifndef PPCP_BOOTSTRAP_H
#define PPCP_BOOTSTRAP_H

/* Both, and each is load-bearing: rv.h for the derivation and the key sizes,
 * frame.h for PPCP_FRAME_HEADER_BYTES, which PPCP_BS_MAX_FRAME is built on.
 * ⚠ This header MUST compile alone.  PinPointCapture consumes it as a Clang
 * module, which is how every Swift consumer builds it, and a module gets no
 * help from whatever a .c file happened to include first — omitting frame.h
 * here broke that build while leaving this one green (found by PinPointCapture,
 * C1).  tests/header_self_contained.cmake is what stops it recurring. */
#include "ppcp/rv.h"
#include "ppcp/frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================= L19 — the frames */

/* 11.4a — the PPCP-ENC §3 header with `channel` set to 255, which ENC 2a
 * reserves and no PPCP channel may use.
 *
 * ⛔ THE TRAP THAT IS EXACTLY BACKWARDS, AND IT IS THE FIRST THING THAT BLOCKS
 * YOU.  ppcp_channel_validate() rejects 255 and MUST GO ON REJECTING IT — that
 * rejection IS 11.4a's fail-closed property.  It is what stops a bootstrap
 * frame being half-understood on a PPCP link, and a PPCP frame from being
 * half-understood here.  An implementer meeting it while adding §11 and
 * concluding the validator needs relaxing would delete the safety argument in
 * the course of implementing the clause that relies on it — and every test
 * would still pass.  So: a bootstrap frame is written and read by the SEPARATE
 * path below, which does not consult the PPCP channel rule at all, because a
 * bootstrap connection is not a PPCP link (1.3c1).  CA6. */
#define PPCP_BS_CHANNEL 255u

/* 11.4b — the bootstrap format version, unrelated to the PPCP wire version
 * (which is negotiated in `hello`, inside TLS, after the pairing exists). */
#define PPCP_BS_VERSION 1u

/* The largest bootstrap frame is `bs_accept` at 45 payload octets.  The cap is
 * deliberately tight rather than the channel's megabyte: ENC 3a's "reject
 * before allocating" is strongest when the bound is the real one, and §11's
 * vocabulary is CLOSED (11.10a), so nothing here ever grows. */
#define PPCP_BS_MAX_PAYLOAD 64u
#define PPCP_BS_MAX_FRAME   (PPCP_FRAME_HEADER_BYTES + PPCP_BS_MAX_PAYLOAD)

/* 11.4b's table.  `ty` is an unsigned integer naming the frame's type. */
typedef enum ppcp_bs_type {
    PPCP_BS_OFFER   = 1,   /* initiator: v, ct   — ct only, NEVER pk_i (11.5b) */
    PPCP_BS_ACCEPT  = 2,   /* acceptor:  v, pk                                 */
    PPCP_BS_REVEAL  = 3,   /* initiator: pk                                    */
    PPCP_BS_CONFIRM = 4,   /* either:    mac                                   */
    PPCP_BS_ABORT   = 5    /* either:    rc                                    */
} ppcp_bs_type;

/* 11.4's abort reason codes. */
typedef enum ppcp_bs_reason {
    PPCP_BS_RC_UNSUPPORTED_VERSION = 1,  /* `v` not implemented (11.4e)        */
    PPCP_BS_RC_COMMITMENT_MISMATCH = 2,  /* revealed pk does not hash to ct    */
    PPCP_BS_RC_INVALID_KEY         = 3,  /* 11.6b — an ATTACK SIGNAL           */
    PPCP_BS_RC_REJECTED            = 4,  /* the user declined, OR a MAC failed */
    PPCP_BS_RC_TIMEOUT             = 5,  /* 11.3e                              */
    PPCP_BS_RC_WINDOW_CLOSED       = 6,  /* no window, or one already running  */
    PPCP_BS_RC_MALFORMED           = 7   /* 11.4c                              */
} ppcp_bs_reason;

/* ⛔ 11.4f — A USER'S REFUSAL AND A FAILED CONFIRMATION MAC ARE THE SAME CODE,
 * `rejected`, and are indistinguishable to the counterpart.  There is no
 * separate code for either and none may be added.  This is 7.7c's principle
 * narrowed to the one pair of cases where it bites; the other five codes
 * describe a peer's own state before any secret exists and reveal nothing.
 *
 * ⚠ And the reasoning under it is the opposite of the obvious one (E37).  AN
 * INTERPOSED ATTACKER CANNOT FAIL THIS MAC: it holds `Z` on both legs, so
 * `K_c` on both, and forges both MACs correctly and trivially — that is what
 * winning the comparison means.  A MAC failure is therefore evidence that no
 * such attacker is present and that something else is wrong, overwhelmingly an
 * implementation disagreement of the PRK-divergence class §10.4 warns about.
 * THE MAC IS NOT THE AUTHENTICATION CHECK.  The comparison is the
 * authentication; the MAC is an agreement-and-liveness proof.  Getting that
 * backwards leads an implementer to weigh the MAC as the security boundary and
 * the digits as ceremony, which is the exact inversion 11.1d exists to
 * prevent. */

/* One decoded frame, held by value.  11.4g: `bs_abort` carries NOTHING beyond
 * `rc` — no message, no diagnostic string, no peer name — and there is no
 * field here for one. */
typedef struct ppcp_bs_frame {
    ppcp_bs_type   ty;
    uint8_t        v;                              /* offer, accept  */
    uint8_t        ct [PPCP_RV_BS_CT_BYTES];       /* offer          */
    uint8_t        pk [PPCP_RV_BS_KEY_BYTES];      /* accept, reveal */
    uint8_t        mac[PPCP_RV_BS_MAC_BYTES];      /* confirm        */
    ppcp_bs_reason rc;                             /* abort          */
} ppcp_bs_frame;

/* Deterministic CBOR (4.3a), so a given frame is byte-identical everywhere,
 * with `v` first in `bs_offer` and `bs_accept` by 4.3b's construction (11.4d).
 * Writes the 8-byte header itself, with channel 255 — see PPCP_BS_CHANNEL. */
PPCP_API ppcp_result ppcp_bs_frame_write(const ppcp_bs_frame *f, uint8_t *out,
                                         size_t cap, size_t *out_len);

/* Reads one frame.  PPCP_ERR_TRUNCATED when the buffer holds less than a whole
 * one; the caller reads more and retries.
 *
 * ⛔ EVERYTHING ELSE IS PPCP_ERR_MALFORMED, AND THAT INCLUDES A MAP KEY IT DOES
 * NOT RECOGNISE (11.4c1, erratum E46).  Bootstrap frames are a CLOSED
 * vocabulary and this is the ONE place in the protocol set where that is so:
 * 3.3a has a TXT receiver ignore unrecognised keys, 4.2c has a pairing-code
 * decoder do the same at every nesting level, and A4 chose CBOR partly for
 * that tolerance.  §11 is the exception, matching 11.10a's "the five frames of
 * §11.4 are its entire vocabulary" and the fail-closed posture of the reserved
 * channel byte.  Two implementations could have differed here without either
 * being wrong and §10.4 could never have shown it.  The consequence is
 * accepted deliberately: a future `v2` cannot extend a v1 frame, so B18's
 * negotiation question, if ever answered yes, is a v2 designed with it from
 * the start. */
PPCP_API ppcp_result ppcp_bs_frame_read(const uint8_t *buf, size_t len,
                                        ppcp_bs_frame *out, size_t *out_consumed);

/* ================================================= L20 — the exchange engine */

/* §11.5's five frames as a sans-I/O state machine: it takes an inbound frame
 * and gives back an outbound one, or an abort.  It owns no socket, no timer,
 * no clock and no storage.  The application supplies the bytes, the keypair,
 * `Z`, the window, the timers and the user's affirmation (ground rule 8).
 *
 * CA2 puts it here once rather than twice in two applications: four of the
 * nine traps live in this exchange, and one implementation of them is one
 * place to get them right.  Both roles, because the relay needs both.
 *
 * ONE ATTEMPT PER ENGINE.  11.5a requires a FRESH keypair per attempt, 11.3d
 * and 11.3d1 allow only one attempt at a time at either end, and 11.9b forbids
 * reopening without a further explicit user action.  So an engine is
 * single-shot: once it reaches a terminal state every call refuses, and a
 * second attempt means a new engine with a newly drawn keypair.  The engine
 * cannot check the keypair is fresh — that is the embedding's, and RT-23
 * reads it. */

typedef enum ppcp_bs_role {
    /* 11.3a — neither term implies a product role.  The peer that dials the
     * window is the initiator; the one that opened it and accepted is the
     * acceptor.  A host, a capture peer or an observer may be either, and
     * 11.2b has the two SWAP for the §5 connection that follows. */
    PPCP_BS_ROLE_INITIATOR = 0,
    PPCP_BS_ROLE_ACCEPTOR  = 1
} ppcp_bs_role;

typedef enum ppcp_bs_state {
    PPCP_BS_ST_NEW = 0,        /* initialised; the initiator has yet to start */
    PPCP_BS_ST_AWAIT_ACCEPT,   /* initiator: bs_offer sent                    */
    PPCP_BS_ST_AWAIT_REVEAL,   /* acceptor:  bs_offer seen, bs_accept SENT    */
    PPCP_BS_ST_AWAIT_SECRET,   /* the counterpart's pk is in hand; Z is owed  */
    PPCP_BS_ST_COMPARE,        /* derived: show the digits, ask THIS user     */
    PPCP_BS_ST_PAIRED,         /* 11.5g satisfied — and only then             */
    PPCP_BS_ST_DONE            /* terminal; everything is wiped               */
} ppcp_bs_state;

typedef enum ppcp_bs_event {
    PPCP_BS_EV_NONE = 0,
    /* Compute Z with the crypto you already link, then call
     * ppcp_bs_engine_supply_secret().  `peer_pk` is the counterpart's public
     * key (CA1: X25519 is a parameter, not a callback). */
    PPCP_BS_EV_NEED_SECRET,
    /* The digits exist.  Display them and ask THIS device's user whether they
     * MATCH.  ⛔ Do not compare them in software (11.1d). */
    PPCP_BS_EV_COMPARE,
    /* 11.5g: this side affirmed AND the counterpart's MAC verified.  Only now
     * does the pairing exist. */
    PPCP_BS_EV_PAIRED,
    /* The attempt is over and nothing survives it.  `rc` says why. */
    PPCP_BS_EV_ABORTED
} ppcp_bs_event;

/* What one call produced.  `out` is bytes to put on the connection; `close`
 * says the connection is finished with (11.5h on success, 11.9a on any abort). */
typedef struct ppcp_bs_step {
    ppcp_bs_event  event;
    bool           has_out;
    size_t         out_len;
    uint8_t        out[PPCP_BS_MAX_FRAME];
    bool           close;
    ppcp_bs_reason rc;                              /* when event == ABORTED  */
    uint8_t        peer_pk[PPCP_RV_BS_KEY_BYTES];   /* when event == NEED_SECRET */
} ppcp_bs_step;

/* What a completed exchange yields, and 11.1a is the point of its shape: from
 * here the pairing is INDISTINGUISHABLE from one established by a scanned
 * code, so §5, §7.4 and §7.5 apply verbatim and are unchanged by §11.  These
 * are exactly the values the pairing-code path already hands the embedding. */
typedef struct ppcp_bs_pairing {
    uint8_t      sid[PPCP_RV_SID_BYTES];
    ppcp_rv_keys keys;                     /* prk, k_tls, k_id */
} ppcp_bs_pairing;

typedef struct ppcp_bs_engine {
    ppcp_bs_role      role;
    ppcp_bs_state     state;
    uint8_t           v;
    uint8_t           pk_own[PPCP_RV_BS_KEY_BYTES];
    uint8_t           pk_i  [PPCP_RV_BS_KEY_BYTES];
    uint8_t           pk_a  [PPCP_RV_BS_KEY_BYTES];
    uint8_t           ct    [PPCP_RV_BS_CT_BYTES];
    bool              affirmed;        /* THIS side's user (11.7c)            */
    bool              peer_verified;   /* the counterpart's MAC (11.5f)       */
    bool              peer_confirm_held;
    uint8_t           peer_mac[PPCP_RV_BS_MAC_BYTES];
    uint8_t           seen;            /* 11.4c — a repeated frame type       */
    ppcp_rv_bootstrap bs;              /* wiped the moment the handshake ends */
    ppcp_bs_pairing   pairing;         /* filled only after 11.5g             */
    bool              has_pairing;
} ppcp_bs_engine;

/* `pk_own` is this peer's own ephemeral public key — pk_i for an initiator,
 * pk_a for an acceptor — drawn FRESH from a CSPRNG for this attempt (11.5a)
 * by the embedding, which keeps the private scalar and erases it afterwards
 * (11.11h).  `v` is the bootstrap format version this peer implements, 1..255.
 *
 * ⛔ THE ACCEPTOR'S KEY IS SUPPLIED HERE, BEFORE ANY FRAME EXISTS, AND THAT IS
 * DELIBERATE.  See ppcp_bs_engine_recv(). */
PPCP_API ppcp_result ppcp_bs_engine_init(ppcp_bs_engine *e, ppcp_bs_role role,
                                         uint8_t v,
                                         const uint8_t pk_own[PPCP_RV_BS_KEY_BYTES]);

/* Initiator only: emits `bs_offer` carrying ct = SHA-256("ppcp1 bs-commit" ||
 * pk_i) and NOT pk_i itself (11.5b).  An acceptor has nothing to send until it
 * has been spoken to and this refuses. */
PPCP_API ppcp_result ppcp_bs_engine_start(ppcp_bs_engine *e, ppcp_bs_step *step);

/* Feeds one inbound frame.  PPCP_ERR_TRUNCATED — with the state untouched —
 * when `len` is short of a whole frame, so the caller reads more and retries.
 *
 * ⛔⛔ THE ORDERING CLAUSE THE ENTIRE SECURITY OF THIS PATH RESTS ON (11.5c).
 * On receiving `bs_offer`, this call returns `bs_accept` IN THE SAME CALL.  The
 * acceptor reveals its key having seen only a COMMITMENT to the initiator's,
 * and it MUST NOT have seen pk_i at that point.
 *
 * Sending `bs_accept` only after `pk_i` arrives saves a round trip, reads as an
 * obvious optimisation, and DESTROYS THE SECURITY OF THE PATH ENTIRELY: an
 * interposer then chooses its key AFTER seeing the honest one and grinds until
 * both legs show the same six digits, which is seconds of work.  ⛔ Nothing on
 * the wire changes and no static test in this repository can see it — RT-20b(ii)
 * is the only thing that catches it, and only via the relay.
 *
 * That is why `pk_own` is taken at init: by the time a frame arrives the
 * acceptor's key is already fixed, so there is no API here that could defer
 * `bs_accept`, and an embedding cannot introduce the trap by using this wrong.
 *
 * 11.3c is honoured for an acceptor's FIRST frame: if it is not a well-formed
 * `bs_offer` the connection closes WITHOUT REPLY (`has_out` false, `close`
 * true) — something that has not demonstrated it speaks this protocol gets
 * nothing to learn from.  Where a window is not open the embedding sends
 * `bs_abort` / `window_closed` itself; the engine is only built when one is. */
PPCP_API ppcp_result ppcp_bs_engine_recv(ppcp_bs_engine *e, const uint8_t *buf,
                                         size_t len, size_t *out_consumed,
                                         ppcp_bs_step *step);

/* Hands over `Z` after PPCP_BS_EV_NEED_SECRET.  32 octets from the key
 * agreement the embedding already links (CA1, 11.11a).
 *
 * ⛔ 11.11f AND ITS OTHER HALF ARE ON YOU.  An agreement that FAILS and one
 * that returns an all-zero Z are the SAME EVENT: OpenSSL fails the call,
 * CryptoKit throws, something else may return zeros.  This function catches
 * the zero; only the caller can see the failure.  Map either to
 * PPCP_BS_RC_INVALID_KEY, and ⛔ NEVER to a transport error and NEVER with a
 * retry — a rejected key is an ATTACK SIGNAL and a retry loop around it eats
 * 3.7b's single-attempt bound, which is what §11.8's whole argument rests on.
 * ppcp_bs_engine_abort(e, PPCP_BS_RC_INVALID_KEY, step) is the failure half. */
PPCP_API ppcp_result ppcp_bs_engine_supply_secret(ppcp_bs_engine *e,
                                                  const uint8_t z[PPCP_RV_BS_KEY_BYTES],
                                                  ppcp_bs_step *step);

/* The six digits, valid only between PPCP_BS_EV_COMPARE and the end of the
 * attempt (11.7e forbids showing any part of them earlier, 11.7f forbids
 * reusing, caching or re-showing them after — so this refuses outside that
 * window, and the value is gone from memory the moment the handshake ends).
 *
 * ⛔ Render as EXACTLY six decimal digits with leading zeros, "%06u": `000042`
 * is a valid string.  11.7d: group them identically at both ends (`435 948`),
 * ask whether the numbers MATCH rather than whether to trust or continue, and
 * do NOT pre-select the affirmative control or put it where a stray tap lands.
 * A dialogue whose default is *Continue* authenticates whatever is on the
 * other end. */
PPCP_API ppcp_result ppcp_bs_engine_sas(const ppcp_bs_engine *e, uint32_t *out_sas);

/* THIS device's own user affirmed that the numbers match (11.7c).  Emits
 * `bs_confirm`.
 *
 * ⛔ Call this ONLY for an affirmative act by a person at this end.  11.7c: a
 * single affirmation at one end does not establish a pairing at the other, and
 * a peer MUST NOT treat the arrival of the counterpart's `bs_confirm` as
 * standing in for its own user's.  The engine cannot tell a real affirmation
 * from a synthesised one — a peer that called this automatically would pass
 * every static test in the document and authenticate nothing (11.1d, trap 8). */
PPCP_API ppcp_result ppcp_bs_engine_affirm(ppcp_bs_engine *e, ppcp_bs_step *step);

/* Ends the attempt: emits `bs_abort` with `rc`, erases everything, closes.
 *
 * This is where the embedding's own reasons arrive — 11.3e's 30- and 60-second
 * timers and 3.7b's 180-second window (PPCP_BS_RC_TIMEOUT), and a user who
 * declined (PPCP_BS_RC_REJECTED).
 *
 * ⛔ 11.9c — a mismatch or a MAC failure is NOT reported to the user in terms
 * that invite a retry.  Those two mean either an implementation is wrong or
 * someone is on the link, and "the numbers did not match — do not retry until
 * you know why" is the honest message.  A timeout or a closed connection
 * carries no such implication and may be reported as the ordinary failure it
 * is.  This is the one signal the path produces that an attack is under way,
 * and a dialogue whose reflex is *try again* converts a one-shot bound into an
 * unbounded one by way of muscle memory. */
PPCP_API ppcp_result ppcp_bs_engine_abort(ppcp_bs_engine *e, ppcp_bs_reason rc,
                                          ppcp_bs_step *step);

/* Takes the pairing out, and ERASES IT FROM THE ENGINE as it goes.  Refuses in
 * any state but PPCP_BS_ST_PAIRED, which is 11.5g in one line: nothing is held
 * until this side has affirmed AND the counterpart's MAC has verified.
 *
 * ⛔ THIS IS TRAP 6, AND THE LIBRARY DOES THE DANGEROUS HALF FOR YOU.  Keeping
 * the derivation struct because it holds the PRK is the natural thing to do
 * with it, and it keeps `K_c` and the digits alive against 11.6f and 11.7f.
 * So the engine wipes the whole ephemeral half — BK, K_c, sas_raw, the digits,
 * both MACs — the instant the handshake ends, success or failure, and this
 * call moves out what may be kept and wipes that too.  On a FAILED or aborted
 * handshake the erasure includes `PRK`, `K_tls`, `K_id` and `sid` as well
 * (11.6f as amended by E51): a peer computes the whole chain the moment it
 * holds Z, up to the 60 seconds 11.3e allows, and until 11.5g is met that is a
 * PRK for a pairing that does not exist and never will.  Computing is not
 * holding. */
PPCP_API ppcp_result ppcp_bs_engine_take_pairing(ppcp_bs_engine *e,
                                                 ppcp_bs_pairing *out);

/* Erases everything and makes the engine terminal.  Idempotent, and safe to
 * call on any path the embedding abandons — a dropped connection, a closed
 * window, a user walking away.  Every terminal transition inside the engine
 * calls it already; this is for the paths outside. */
PPCP_API void ppcp_bs_engine_wipe(ppcp_bs_engine *e);

PPCP_API ppcp_bs_state ppcp_bs_engine_state(const ppcp_bs_engine *e);

#ifdef __cplusplus
}
#endif
#endif /* PPCP_BOOTSTRAP_H */
