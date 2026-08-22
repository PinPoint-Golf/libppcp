/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * sim.h — the synthetic peer of PPCP-CONF §2c.  Work package L13.
 *
 * WHAT THIS IS.  A command-line PPCP peer over two TCP connections that
 * presents whatever declaration a JSON file describes and behaves however a
 * named scenario says.  It exists because CONF 2c requires "a software peer
 * simulator that both sides develop against, capable of presenting a
 * declaration different from the implementer's own", and because without one
 * CT-S3, CT-S4, CT-S6 and CT-S7 cannot be written at all — an implementation
 * tested only against itself passes I19, I22, I24 and I31 by accident.
 *
 * WHY THE SOCKETS ARE HERE AND NOT IN src/.  libppcp owns no socket, no thread,
 * no timer, no clock and no file (plan ground rule 8, CORE A.3); tests/
 * purity.cmake gates src/ and include/ and nothing else.  This is test
 * infrastructure, so plain POSIX sockets in C11 are the right answer.
 *
 * WHY IT IS PLAINTEXT BY DEFAULT.  RV §2's `direct` path is conformant without
 * rendezvous, and TLS in the simulator would test OpenSSL rather than PPCP.
 * The one exception is --psk-ke-only, which exists so a host's refusal of a
 * PSK-only key exchange can be DEMONSTRATED rather than asserted (RT-4).
 *
 * THE SEPARATION THAT MAKES IT USEFUL.  The DECLARATION varies by file; the
 * BEHAVIOUR varies by scenario.  A host that never answers a Candidate is
 * `--scenario silent-host` over any host declaration; a peer with a foreign
 * convention is any scenario over `foreign-capture.json`.  Neither is a code
 * change, which is what lets the two applications drive it from their own test
 * suites.
 */
#ifndef PPCP_SIM_H
#define PPCP_SIM_H

#include "ppcp/ppcp.h"
#include "ppcp/peer.h"
#include "ppcp/shot.h"
#include "ppcp/sync.h"
#include "ppcp/bundle.h"
#include "ppcp/message.h"
#include "ppcp/frame.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------ declaration */

#define SIM_MAX_TB       6
#define SIM_MAX_REL     12
#define SIM_MAX_SRC      8
#define SIM_MAX_CP       4
#define SIM_MAX_PROF     8
#define SIM_ERR_LEN    256

/* One clock, several timebases.  A peer declaring three timebases is one
 * process reading one monotonic clock through three offsets and three skews,
 * which is CONF 2a's injectable clock made real without waiting for anything. */
typedef struct sim_clock {
    struct {
        char    id[PPCP_ID_MAX + 1];
        int64_t offset_ns;
        double  skew_ppm;
    } tb[SIM_MAX_TB];
    size_t  count;
    int64_t origin_ns;     /* the process's own monotonic zero */
} sim_clock;

int64_t     sim_now_ns(void);                     /* CLOCK_MONOTONIC, nanoseconds */
ppcp_result sim_clock_now(void *ctx, const char *timebase_id, int64_t *out_ns);

typedef struct sim_decl {
    char        peer_id[PPCP_ID_MAX + 1];
    ppcp_role   role;
    char        profile_text[SIM_MAX_PROF][PPCP_ID_MAX + 1];
    const char *profile_ptr[SIM_MAX_PROF];
    ppcp_id     profiles[SIM_MAX_PROF];
    size_t      profile_count;

    ppcp_timebase          tb[SIM_MAX_TB];
    size_t                 tb_count;
    ppcp_timebase_relation rel[SIM_MAX_REL];
    size_t                 rel_count;

    ppcp_capture_profile   cp[SIM_MAX_SRC][SIM_MAX_CP];
    size_t                 cp_count[SIM_MAX_SRC];
    ppcp_source            src[SIM_MAX_SRC];
    size_t                 src_count;

    ppcp_peer_desc         desc;

    char        sync_tb[PPCP_ID_MAX + 1];
    bool        has_sync_tb;

    /* The Session this peer opens when the scenario says it opens one.  A
     * capture peer carries it too, because a hostless bundle records one
     * (CORE 4.1b) and because `timebase_ref` is what the mint deadline is
     * measured in. */
    char             session_id[PPCP_ID_MAX + 1];
    char             timebase_ref[PPCP_ID_MAX + 1];
    ppcp_duration_ns coincidence_window_ns;
    ppcp_duration_ns issue_hold_ns;
    uint32_t         heartbeat_ms;

    sim_clock clock;
} sim_decl;

/* Fills `d` from the JSON at `path`.  Returns false with a one-line reason. */
bool sim_decl_load(sim_decl *d, const char *path, char *err, size_t err_len);

/* --------------------------------------------------------------- scenarios */

#define SIM_F_SESSION_OPEN   0x00000001u  /* a host opens the Session */
#define SIM_F_SYNC           0x00000002u  /* probe per declared timebase (I21) */
#define SIM_F_HEARTBEAT      0x00000004u  /* CORE 7.4 */
#define SIM_F_ARM            0x00000008u  /* CORE 7.3a — host-controlled */
#define SIM_F_STREAMS        0x00000010u  /* open a video Stream per camera Source */
#define SIM_F_PREVIEW        0x00000020u  /* plus a preview and a continuous Stream */
#define SIM_F_NOMINATE       0x00000040u  /* emit Candidates (7.1d: every one) */
#define SIM_F_MINT           0x00000080u  /* run the Mint engine (8.2i) */
#define SIM_F_ARBITRATE      0x00000100u  /* run the Arbitrate engine (I20) */
#define SIM_F_NEVER_ISSUE    0x00000200u  /* observe Candidates, issue no Shot */
#define SIM_F_CAPTURES       0x00000400u  /* announce and transfer a Capture per Shot */
#define SIM_F_OFFER          0x00000800u  /* offer a stored Session, replay on accept */
#define SIM_F_ACCEPT_OFFER   0x00001000u  /* accept an offered Session */
#define SIM_F_REPLAY_TWICE   0x00002000u  /* replay the accepted bundle twice (I34) */
#define SIM_F_OBSERVER       0x00004000u  /* originate nothing past hello/declare */
#define SIM_F_LATE_NOMINATE  0x00008000u  /* the second Candidate arrives late (I7) */

typedef struct sim_scenario {
    const char *name;
    const char *roles;        /* "host", "capture", "observer" or "any" */
    const char *serves;       /* the CT / IOP rows this scenario is written for */
    const char *desc;
    uint32_t    flags;
    int         nominate_count;
    int64_t     nominate_gap_ms;     /* wall delay between emissions */
    int64_t     candidate_step_ns;   /* instant separation between Candidates */
    int64_t     arb_delay_ms;        /* extra hold before the arbiter is pumped */
} sim_scenario;

const sim_scenario *sim_scenario_find(const char *name);
const sim_scenario *sim_scenario_at(size_t index);
size_t              sim_scenario_count(void);

/* ------------------------------------------------------------- the process */

#define SIM_MAX_EXPECT 16

typedef struct sim_counter {
    /* Everything --expect can name.  One flat table so the expectation parser
     * and the report are the same list. */
    int64_t frames_rx, frames_tx;
    int64_t declares_rx;
    int64_t candidates_rx, candidates_tx;
    int64_t shots_rx, shots_tx;
    int64_t shot_candidates_max;
    int64_t t0_revisions;
    int64_t captures_rx, captures_unique, captures_duplicate;
    int64_t payload_frames_rx;
    int64_t relations_rx, relations_tx;
    int64_t probe_timebases, probes_tx, replies_rx;
    int64_t heartbeats_rx;
    int64_t errors_rx;
    int64_t minted, retained, issued, late_issues, arbiter_observed;
    int64_t offers_rx, offers_tx, accepts_rx, replays;
    int64_t sessions_joined, streams_rx, arms_rx;
    int64_t relations_held;
    int64_t violations;
} sim_counter;

/* `=` is the usual assertion; `>=` and `<=` exist because a few counters are
 * honestly timing-dependent — how many `relation_update` frames crossed a link
 * in four seconds is not a protocol property, and asserting an exact number
 * would make a conformance row flap for a reason unrelated to conformance. */
typedef enum sim_cmp { SIM_CMP_EQ = 0, SIM_CMP_GE, SIM_CMP_LE } sim_cmp;

typedef struct sim_expect {
    char    name[48];
    sim_cmp cmp;
    int64_t value;
} sim_expect;

typedef struct sim_opts {
    const char *role;
    const char *declaration;
    const char *scenario;
    const char *connect_host;
    int         connect_port;
    int         listen_port;
    const char *port_file;
    const char *log_prefix;
    int64_t     run_ms;
    bool        psk_ke_only;
    const char *psk_hex;        /* RV §5.1 K_tls, as hex; 32 zero bytes by default */
    const char *psk_identity;   /* RV §5.3 identity tag, as text for this mode */
    bool        quiet;
    sim_expect  expect[SIM_MAX_EXPECT];
    size_t      expect_count;
} sim_opts;

/* Runs the peer to completion.  0 on success; 1 on a protocol violation, an
 * unmet expectation or a transport failure, with a one-line reason on stderr. */
int sim_run(const sim_opts *o, sim_decl *d, const sim_scenario *sc);

/* Reports a violation: the one-line reason, and the exit code that follows. */
void sim_violation(const char *fmt, ...);
bool sim_had_violation(void);
const char *sim_violation_reason(void);

/* ---------------------------------------------------------------- the wire */

#define SIM_CH_COUNT 2
#define SIM_RX_CAP   (1024u * 1024u)   /* ENC §8's control limit; bulk is chunked */

/* One channel is one TCP connection (plan A6).  The receive buffer is the
 * caller-owned tail ppcp_peer_feed() reports back: the engine buffers nothing,
 * so a partial frame lives here until more bytes arrive. */
typedef struct sim_chan {
    int      fd;
    uint8_t *rx;
    size_t   rx_len;
    bool     open;
} sim_chan;

typedef struct sim_link {
    sim_chan ch[SIM_CH_COUNT];
} sim_link;

/* A listener that accepts SIM_CH_COUNT connections and binds each to a channel
 * through ppcp_link_binder (ENC §2.1), then feeds the leftover bytes of the
 * binding frame into the engine.  Returns false with a reason on failure. */
bool sim_listen(sim_link *l, int port, const char *port_file, int64_t timeout_ms,
                ppcp_peer *p, char *err, size_t err_len);
/* A dialler that opens SIM_CH_COUNT connections and sends `link_bind` first on
 * each (ENC 2.1a, 2.1d). */
bool sim_connect(sim_link *l, const char *host, int port, int64_t timeout_ms,
                 ppcp_peer *p, char *err, size_t err_len);
void sim_link_close(sim_link *l);

/* Logs every frame in `bytes` as one line each on stderr (ENC §3 header plus
 * the decoded type name), which is what makes a failed interop run readable. */
void sim_log_frames(const char *who, const char *dir, uint8_t channel,
                    const uint8_t *bytes, size_t len);
/* The name this process logs under, and whether it logs at all.  Set once,
 * before the transport is brought up, because the binding phase logs too. */
void sim_log_configure(const char *who, bool quiet);

/* --------------------------------------------------------------- TLS (RT-4)
 *
 * Built only where OpenSSL was found at configure time.  The mode offers
 * `psk_ke` and nothing else, so a conformant host's refusal (RV 5.2f, 5.4b2)
 * is observable rather than asserted. */
bool sim_tls_available(void);
/* Dials the host and offers TLS 1.3 with `psk_ke` as the ONLY key-exchange
 * mode, then reports what happened.
 *
 * ⚠ THE EXIT CODE IS INVERTED ON PURPOSE.  A conformant host refuses this
 * handshake (RV 5.2f, 5.4b2: no forward secrecy, no plaintext fallback), so a
 * REFUSAL is the pass and a completed handshake is the failure.  That is what
 * makes RT-4 demonstrable rather than asserted — the property is the peer's
 * behaviour, and behaviour needs something to behave against. */
int sim_run_psk_ke_only(const sim_opts *o);

#endif /* PPCP_SIM_H */
