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

#include "ppcp/rv.h"

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

#ifdef __cplusplus
}
#endif
#endif /* PPCP_BOOTSTRAP_H */
