/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * relay.h — `ppcp-relay`, a DELIBERATE MAN IN THE MIDDLE for PPCP-RV §11.
 * Work package L21.
 *
 * WHAT THIS IS.  Acceptor toward one peer, initiator toward the other, with
 * its own fresh keypair on each leg.  It runs L20's engine twice, so the two
 * halves of §11.5 it presents are the same code both applications embed —
 * which is the point: a relay that reimplemented the exchange would be a
 * fourth thing to keep honest, and a divergence in it would read as a defect
 * in the peer under test.
 *
 * ⛔ WHY IT IS BUILT BEFORE EITHER APPLICATION IMPLEMENTS ITS ROLE (CA3).
 * RT-20b needs one implementation and this; RT-20c needs both and this.  So
 * it is a prerequisite of BOTH tests that touch the security property, and it
 * is the earliest thing that can be built, because it needs no application at
 * all.  §9's own note: "the natural gravity of a test needing two
 * implementations is to slide to the end, where a failure costs most; the
 * ordering here is deliberately the opposite."  Building it also produces the
 * third implementation carrying BOTH roles, which is the only slack the
 * interoperable set has (9e1) — PinPointStudio is initiator-only and
 * PinPointCapture acceptor-only (CA4), so without this there is no peer in
 * the set that can stand on either side.
 *
 * ⛔ WHAT IT EXISTS TO CATCH, AND IT IS ONE CLAUSE.  11.5c: the acceptor
 * sends `bs_accept` BEFORE it has seen `pk_i`.  An implementation that waits
 * for `pk_i` first saves a round trip, reads as an obvious optimisation, and
 * destroys the security of the entire path — an interposer then chooses its
 * key after seeing the honest one and grinds until both legs show the same
 * six digits, which is seconds of work.  ⛔ NOTHING ON THE WIRE CHANGES.  No
 * static test in this repository can see it.  RT-20b(ii) is the only thing
 * that catches it, and only from here — by withholding `bs_reveal` and
 * checking `pk_a` has already arrived, or by not replying at all and checking
 * no `pk_i` follows.
 *
 * ⛔ AND THE ASSERTION MOST EASILY SKIPPED IS RT-20b(v): each of this
 * harness's OWN legs must complete on demand, "or the harness is testing its
 * own bug".  A relay that cannot itself pair proves nothing when a peer fails
 * against it, and the failure looks identical to a defect in the peer.  That
 * is `--selftest`, it needs no application, and it is the C2 gate.
 *
 * ---------------------------------------------------------------------------
 * WHERE THE KEY MATERIAL IS, AND WHY IT IS NOT HERE (CA8, §11.11)
 *
 * This is the first thing in `libppcp` that holds BOTH legs' key material,
 * which makes it the first real test of CA8 — "no credential or key of any
 * kind belongs in `libppcp`", validated by the question *another project
 * ships this*.  Three consequences, and they are structural rather than
 * intentions:
 *
 *   1. It lives in `tools/`, which `tests/purity.cmake` deliberately leaves
 *      outside the gate it puts on `src/` and `include/`.  Nothing here is
 *      part of the library, and no consumer of `libppcp` links it.
 *   2. ⛔ THE PRIVATE SCALAR IS NEVER IN THIS PROCESS.  Ground rule 13 and
 *      CA1 keep X25519 out of `libppcp` entirely, and A16 says why in the
 *      document's own words: this is the implementation that should "neither
 *      hand-roll nor vendor" it.  So the relay supplies itself across exactly
 *      the boundary §11.11 defines for the library — a separate process holds
 *      the scalar and hands back two 32-octet values, `pk` and `Z`, and
 *      nothing else crosses (11.11d).  See x25519-agree.sh.  The relay has no
 *      scalar to erase, which 11.11h calls "half the point of putting the
 *      boundary here".
 *   3. `Z` does transit this process, and it is erased on the way out of
 *      every path; the engine's own material is wiped by
 *      ppcp_bs_engine_wipe(), which the pump calls on every exit including
 *      abort.  ⛔ No key-shaped literal appears anywhere in this tool — not
 *      even §10.4's published scalars, which 10.4 itself says would make a
 *      peer shipping them "trivially impersonable by anyone reading this
 *      document".  Every key is drawn fresh, per attempt, by the helper
 *      (11.5a).
 *   4. Nothing derived is printed but the six digits and `sid`.  ⛔ `PRK`,
 *      `K_tls` and `K_id` are never rendered, logged or written: a harness
 *      that printed them would be a working attack tool rather than a test
 *      instrument, and would put key material in a terminal scrollback and a
 *      CI log.
 *
 * ---------------------------------------------------------------------------
 * WHAT IT DOES NOT DO
 *
 * ⛔ It does not compare digits for anybody and it never will (11.1d, trap 8).
 * It prints its own two legs' digits so a person can hold them against the
 * two screens.  Its `--affirm` is the ATTACKER'S affirmation of its own legs,
 * which is exactly what an attacker has and what makes the demonstration
 * work; it is not, and cannot be made to be, a stand-in for either user's.
 *
 * It does not forward frames.  It runs two INDEPENDENT exchanges with two
 * independent keypairs, which is what an interposer is; a relay that copied
 * bytes across would be a wire, and both legs would show the same digits
 * because there would be only one exchange.
 */
#ifndef PPCP_RELAY_H
#define PPCP_RELAY_H

#include "ppcp/bootstrap.h"
#include "ppcp/rv.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define RL_ERR_LEN   256
#define RL_HEX_LEN   (2 * PPCP_RV_BS_KEY_BYTES + 1)
#define RL_MAX_LEGS  2
#define RL_RX_CAP    (4 * PPCP_BS_MAX_FRAME)

/* 11.3e's two timers, which the engine cannot own because it owns no clock
 * (ground rule 8).  The window's own 180-second bound (3.7b) belongs to
 * whoever opened the window and is not the relay's to enforce. */
#define RL_TIMEOUT_EXCHANGE_MS 30000   /* to reach 11.5f            */
#define RL_TIMEOUT_AFFIRM_MS   60000   /* awaiting an affirmation   */

/* ============================================ the §11.11 boundary (agree.c) */

typedef enum rl_agree_rc {
    RL_AGREE_OK = 0,
    /* ⛔ 11.11f — the supplier REJECTED the peer's key.  This is an ATTACK
     * SIGNAL, it maps to `invalid_key` (11.6b), and it is NEVER retried:
     * a retry loop around it eats 3.7b's single-attempt bound, which is what
     * §11.8's whole argument rests on (trap 7). */
    RL_AGREE_REJECTED,
    /* The helper itself is broken or absent — a harness fault, and reported
     * as one.  ⛔ Distinguished from RL_AGREE_REJECTED deliberately: folding
     * the two together is exactly the mistake 11.11f forbids, one direction
     * reporting an attack as a hiccup and the other a hiccup as an attack. */
    RL_AGREE_BROKEN
} rl_agree_rc;

typedef struct rl_agree {
    pid_t pid;
    int   to_fd;     /* commands to the helper   */
    int   from_fd;   /* replies from the helper  */
    bool  open;
    bool  keyed;
} rl_agree;

bool        rl_agree_open(rl_agree *a, const char *helper, char *err, size_t errlen);
/* 11.5a — one keypair per attempt, and the helper refuses a second. */
bool        rl_agree_keygen(rl_agree *a, uint8_t pk[PPCP_RV_BS_KEY_BYTES],
                            char *err, size_t errlen);
rl_agree_rc rl_agree_shared(rl_agree *a, const uint8_t peer_pk[PPCP_RV_BS_KEY_BYTES],
                            uint8_t z[PPCP_RV_BS_KEY_BYTES], char *err, size_t errlen);
void        rl_agree_close(rl_agree *a);

/* ================================================================= sockets */

int  rl_listen(int port, int *out_port, char *err, size_t errlen);
int  rl_accept(int lfd, int timeout_ms, char *err, size_t errlen);
int  rl_connect(const char *host, int port, int timeout_ms, char *err, size_t errlen);
void rl_close(int fd);
bool rl_write_all(int fd, const uint8_t *buf, size_t len, char *err, size_t errlen);
int64_t rl_now_ms(void);

/* =================================================================== a leg */

/* What the harness is told to do at the points where a conforming peer would
 * consult a person or a clock.  ⛔ Each of these exists to make ONE of
 * RT-20b's assertions observable, and none of them changes what the ENGINE
 * does — the engine still produces `bs_accept` in the same call that consumed
 * `bs_offer` (11.5c).  Withholding happens above it, on the wire, which is
 * the only place a relay can stand. */
typedef enum rl_rewrite {
    RL_RW_NONE = 0,
    /* RT-19 — reveal a `pk_i` that does NOT hash to the `ct` already
     * sent.  11.5d: the acceptor recomputes the commitment, compares in
     * constant time, aborts `commitment_mismatch`, and MUST NOT derive
     * anything from a `pk_i` that failed the check. */
    RL_RW_BAD_REVEAL,
    /* RT-21 — commit to AND reveal an all-zero `pk`.  The commitment is
     * honest, so the peer gets past 11.5d and reaches key agreement,
     * where 11.6b bites: an all-zero `Z` and a reported failure are the
     * same event and both are `invalid_key`.  ⛔ AND IT MUST NOT BE
     * RETRIED (trap 7): a retry loop around an attack signal eats 3.7b's
     * single-attempt bound, which is what §11.8 rests on. */
    RL_RW_ZERO_KEY,
    /* RT-24 — `bs_accept.v` different from the `v` the initiator offered.
     * 11.4h/E34 bound the version into the transcript precisely so this
     * cannot pass silently. */
    RL_RW_WRONG_V
} rl_rewrite;

typedef struct rl_leg_ctl {
    /* RT-20b(ii), acceptor mirror.  Relay is the INITIATOR: it never puts
     * `bs_reveal` on the wire, so the peer under test never sees `pk_i`.
     * A conforming acceptor has ALREADY sent `pk_a`. */
    bool withhold_reveal;
    /* RT-20b(ii), initiator mirror.  Relay is the ACCEPTOR: it never puts
     * `bs_accept` on the wire.  A conforming initiator sends NO `pk_i`. */
    bool no_reply;
    /* RT-20b(iii).  At the comparison, this leg's "user" declines. */
    bool decline;
    /* Sit at the comparison and do nothing, so 11.3e's 60 seconds can be
     * observed rather than asserted. */
    bool never_affirm;
    /* ⛔ TRAP 2, ON THE WIRE, ON PURPOSE — the NEGATIVE CONTROL, and the only
     * thing that shows RT-20b(ii) can fail.  An acceptor leg with this set
     * holds `bs_accept` back until `bs_reveal` arrives, which is exactly the
     * "obvious optimisation" 11.5c forbids and which no static test in this
     * repository can see.  It exists so `--selftest` can prove the probe
     * DISCRIMINATES rather than merely runs: a test that cannot fail is a row
     * that gets ticked.  ⚠ The engine itself cannot be made to do this —
     * `pk_own` is fixed at init — so the reordering is imposed above it, here,
     * which is the only place it could live. */
    bool defer_accept;

    /* ⛔ FRAME REWRITES — the `injected` method of §9, and the reason a relay
     * can run rows a unit test cannot.  Each rewrites ONE field of ONE
     * outbound frame on the wire, above the engine, and asserts what a
     * conforming peer does about it.  libppcp's own suite already covers each
     * of these against its own engine; what these add is the SAME assertion
     * against an implementation that shares no code with it, which is the
     * whole of what CONF §2c is about. */
    rl_rewrite rewrite;
    int  exchange_timeout_ms;
    int  affirm_timeout_ms;
} rl_leg_ctl;

typedef struct rl_leg {
    const char     *name;
    int             fd;
    ppcp_bs_role    role;
    uint8_t         v;
    ppcp_bs_engine  eng;
    rl_agree        ag;
    rl_leg_ctl      ctl;

    uint8_t         rx[RL_RX_CAP];
    size_t          rx_len;
    uint8_t         deferred[PPCP_BS_MAX_FRAME];   /* trap 2's held bs_accept */
    size_t          deferred_len;

    int64_t         t_start;
    int64_t         t_compare;

    /* ------------------------------------------------- what was observed */
    bool            started;
    bool            done;
    bool            peer_eof;
    bool            sent_offer, sent_accept, sent_reveal, sent_confirm;
    bool            withheld_reveal, withheld_accept;
    bool            rewrote;
    bool            saw_offer, saw_accept, saw_reveal, saw_confirm, saw_abort;
    ppcp_bs_reason  peer_abort_rc;
    /* ⛔ RT-20b(ii)'s whole finding, in one flag: `bs_accept` carrying `pk_a`
     * arrived while this relay had sent NOTHING but the commitment. */
    bool            accept_before_reveal;
    /* Octets that arrived after the relay deliberately fell silent.  For the
     * initiator mirror this must stay zero: an initiator that sends `pk_i`
     * without `bs_accept` has inverted 11.5b/11.5d. */
    size_t          bytes_after_silence;

    bool            have_sas;
    uint32_t        sas;
    bool            paired;
    uint8_t         sid[PPCP_RV_SID_BYTES];
    bool            aborted;
    ppcp_bs_reason  abort_rc;

    bool            failed;             /* a HARNESS fault, not a verdict */
    char            err[RL_ERR_LEN];
} rl_leg;

bool rl_leg_init(rl_leg *l, const char *name, ppcp_bs_role role, uint8_t v,
                 int fd, const char *helper, const rl_leg_ctl *ctl);
bool rl_leg_begin(rl_leg *l);
bool rl_leg_on_readable(rl_leg *l);
void rl_leg_check_timers(rl_leg *l);
void rl_leg_finish(rl_leg *l);
bool rl_pump(rl_leg **legs, size_t n, int64_t overall_deadline_ms);

/* ================================================================== a row */

typedef enum rl_verdict { RL_PASS = 0, RL_FAIL, RL_UNRUN } rl_verdict;

#define RL_MAX_ROWS 16

typedef struct rl_row {
    const char *id;          /* "RT-20b(ii)" — as the matrix spells it */
    const char *invariant;
    const char *profile;
    const char *method;
    const char *asserts;
    rl_verdict  verdict;
    char        reason[512];
    char        command[1024];
} rl_row;

typedef struct rl_report {
    rl_row  rows[RL_MAX_ROWS];
    size_t  count;
    const char *column;
    const char *mode;
} rl_report;

rl_row *rl_row_add(rl_report *rep, const char *id, const char *invariant,
                   const char *method, const char *asserts);
void    rl_row_set(rl_row *r, rl_verdict v, const char *fmt, ...);
bool    rl_write_json(const rl_report *rep, const char *path);
bool    rl_write_markdown(const rl_report *rep, const char *path);
const char *rl_verdict_name(rl_verdict v);
const char *rl_verdict_cell(rl_verdict v);

/* =================================================================== modes */

int rl_selftest(const char *helper, uint8_t v, rl_report *rep, bool quiet);
int rl_relay_main(int listen_port, const char *host, int port, uint8_t v,
                  const char *helper, rl_report *rep);
int rl_probe_main(const char *what, int listen_port, const char *host, int port,
                  uint8_t v, const char *helper, int observe_ms, rl_report *rep);
int rl_peer_main(ppcp_bs_role role, uint8_t v, const char *host, int port,
                 int listen_port, const char *helper, const rl_leg_ctl *ctl);

const char *rl_reason_name(ppcp_bs_reason rc);
void        rl_hex(const uint8_t *b, size_t n, char *out);

#endif /* PPCP_RELAY_H */
