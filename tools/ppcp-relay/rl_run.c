/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * rl_run.c — the modes.
 *
 *   --relay     the man in the middle: acceptor toward one peer, initiator
 *               toward the other, two independent keypairs, two independent
 *               exchanges.  This is what RT-20b and RT-20c run against.
 *   --probe     one assertion in isolation, against one peer.
 *   --peer      an HONEST stand-in, so the relay has something to be tested
 *               against before either application exists.
 *   --selftest  ⛔ RT-20b(v), and the C2 gate: each of the relay's own legs
 *               completes on demand.
 *
 * ⛔ WHY --selftest IS NOT OPTIONAL AND IS THE ASSERTION MOST EASILY SKIPPED.
 * RT-20b(v): "each of the relay's own legs completes on demand, OR THE
 * HARNESS IS TESTING ITS OWN BUG."  Every other row this tool emits is a
 * statement about somebody else's code, and each of them is worthless if this
 * one has not run — a peer that fails against a broken relay fails in a way
 * that looks exactly like a defect in the peer, and the cost lands on the
 * team least able to see the cause.  It is also the one row that needs no
 * application at all, which is why it can be, and is, run first.
 *
 * ⛔ WHAT `--peer` IS NOT.  It is a test fixture, it affirms its own
 * comparison in software, and it therefore does the one thing 11.1d says
 * destroys the security of this path (trap 8).  IT CLAIMS NO CONFORMANCE, it
 * appears in no claim file, and it is not a third implementation for
 * §9's purposes — it is `libppcp`'s own engine wearing a socket.  Its only
 * job is to give the relay's two legs a counterpart on a day when neither
 * application has written §11 yet.
 */
#include "relay.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* 11.7d — exactly six decimal digits with leading zeros, grouped the same way
 * at both ends.  `000042` is a valid string and a tool that printed `42`
 * would have invented a seventh way for two screens to disagree. */
static void sas_str(uint32_t sas, char *out, size_t n)
{
    snprintf(out, n, "%03u %03u", (unsigned)(sas / 1000u), (unsigned)(sas % 1000u));
}

static void leg_describe(const rl_leg *l)
{
    char digits[16];

    if (l->have_sas) {
        sas_str(l->sas, digits, sizeof(digits));
        fprintf(stderr, "  %-18s digits %s   %s\n", l->name, digits,
                l->paired ? "paired" : (l->aborted ? "aborted" : "incomplete"));
    } else {
        fprintf(stderr, "  %-18s digits —        %s\n", l->name,
                l->aborted ? "aborted" : (l->failed ? "harness fault" : "incomplete"));
    }
    if (l->aborted)
        fprintf(stderr, "  %-18s abort: %s\n", "", rl_reason_name(l->abort_rc));
    if (l->failed)
        fprintf(stderr, "  %-18s ⛔ HARNESS FAULT: %s\n", "", l->err);
}

/* ------------------------------------------------------------ honest peer */

typedef struct rl_peer_out {
    int      paired;
    unsigned sas;
    int      have_sas;
    int      failed;
} rl_peer_out;

/* Runs one honest leg to completion.  `is_listener` says whether `fd` is a
 * socket already connected to the counterpart or one still to be accepted on
 * — a distinction worth a parameter rather than a convention, because getting
 * it wrong produces a leg that reads protocol frames out of a listening
 * socket and fails in a way that reads as a defect in the peer. */
static void peer_run_fd(ppcp_bs_role role, uint8_t v, int fd, bool is_listener,
                        const char *helper, const rl_leg_ctl *ctl,
                        rl_peer_out *out)
{
    rl_leg  leg;
    rl_leg *legs[1];

    memset(out, 0, sizeof(*out));
    if (is_listener) {
        char err[RL_ERR_LEN] = { 0 };
        int  afd = rl_accept(fd, 30000, err, sizeof(err));
        rl_close(fd);
        if (afd < 0) {
            fprintf(stderr, "peer: %s\n", err);
            out->failed = 1;
            return;
        }
        fd = afd;
    }
    if (!rl_leg_init(&leg, role == PPCP_BS_ROLE_INITIATOR ? "peer-initiator"
                                                          : "peer-acceptor",
                     role, v, fd, helper, ctl)) {
        fprintf(stderr, "peer: %s\n", leg.err);
        out->failed = 1;
        rl_leg_finish(&leg);
        return;
    }
    (void)rl_leg_begin(&leg);
    legs[0] = &leg;
    (void)rl_pump(legs, 1, rl_now_ms() + ctl->exchange_timeout_ms +
                           ctl->affirm_timeout_ms + 5000);

    out->paired   = leg.paired ? 1 : 0;
    out->sas      = leg.sas;
    out->have_sas = leg.have_sas ? 1 : 0;
    out->failed   = leg.failed ? 1 : 0;
    leg_describe(&leg);
    rl_leg_finish(&leg);
}

int rl_peer_main(ppcp_bs_role role, uint8_t v, const char *host, int port,
                 int listen_port, const char *helper, const rl_leg_ctl *ctl)
{
    rl_peer_out out;
    char        err[RL_ERR_LEN] = { 0 };
    int         fd;

    if (host != NULL) {
        fd = rl_connect(host, port, 5000, err, sizeof(err));
    } else {
        int lfd = rl_listen(listen_port, NULL, err, sizeof(err));
        if (lfd < 0) {
            fprintf(stderr, "peer: %s\n", err);
            return 2;
        }
        fd = rl_accept(lfd, 30000, err, sizeof(err));
        rl_close(lfd);
    }
    if (fd < 0) {
        fprintf(stderr, "peer: %s\n", err);
        return 2;
    }
    peer_run_fd(role, v, fd, false, helper, ctl, &out);
    return out.paired ? 0 : 1;
}

/* Forks an honest peer onto an already-open socket, reporting back on a pipe.
 * fork() rather than exec(): the child needs no argv, and a child that cannot
 * be found on $PATH is a failure mode a self-test should not have. */
typedef struct rl_child {
    pid_t pid;
    int   rfd;
} rl_child;

static bool spawn_peer(rl_child *c, ppcp_bs_role role, uint8_t v, int fd,
                       bool is_listener, const char *helper, const rl_leg_ctl *ctl,
                       int *close_in_child, size_t n_close)
{
    int pfd[2];

    if (pipe(pfd) != 0)
        return false;
    c->pid = fork();
    if (c->pid < 0) {
        close(pfd[0]); close(pfd[1]);
        return false;
    }
    if (c->pid == 0) {
        rl_peer_out out;
        size_t      i;
        close(pfd[0]);
        for (i = 0; i < n_close; i++)
            if (close_in_child[i] >= 0)
                rl_close(close_in_child[i]);
        peer_run_fd(role, v, fd, is_listener, helper, ctl, &out);
        (void)!write(pfd[1], &out, sizeof(out));
        close(pfd[1]);
        _exit(out.failed ? 2 : (out.paired ? 0 : 1));
    }
    close(pfd[1]);
    rl_close(fd);          /* the child owns it now */
    c->rfd = pfd[0];
    return true;
}

static bool reap_peer(rl_child *c, rl_peer_out *out)
{
    ssize_t r;
    int     status;

    memset(out, 0, sizeof(*out));
    r = read(c->rfd, out, sizeof(*out));
    close(c->rfd);
    while (waitpid(c->pid, &status, 0) < 0 && errno == EINTR)
        ;
    return r == (ssize_t)sizeof(*out);
}

/* ------------------------------------------------------------------ relay */

typedef struct rl_relay_cfg {
    int         listen_port;      /* where the initiator peer dials us   */
    const char *connect_host;     /* the acceptor peer's window          */
    int         connect_port;
    uint8_t     v;
    const char *helper;
    rl_leg_ctl  ctl_acceptor;     /* our leg toward the initiator peer   */
    rl_leg_ctl  ctl_initiator;    /* our leg toward the acceptor peer    */
    int         accept_timeout_ms;
} rl_relay_cfg;

typedef struct rl_relay_out {
    rl_leg a;    /* relay as ACCEPTOR,  facing the peer that initiates  */
    rl_leg b;    /* relay as INITIATOR, facing the peer that accepts    */
    bool   have_a, have_b;
    char   err[RL_ERR_LEN];
} rl_relay_out;

static bool relay_run(const rl_relay_cfg *cfg, int lfd, rl_relay_out *out)
{
    char    err[RL_ERR_LEN] = { 0 };
    int     fda = -1, fdb = -1;
    rl_leg *legs[RL_MAX_LEGS];
    size_t  n = 0;

    memset(out, 0, sizeof(*out));

    /* An interposer waits to be dialled, then dials the victim's window.
     * Doing it in that order is what a relay on the link actually does, and
     * it keeps the two exchanges inside one another's 11.3e budget. */
    fda = rl_accept(lfd, cfg->accept_timeout_ms, err, sizeof(err));
    if (fda < 0) {
        snprintf(out->err, sizeof(out->err), "acceptor leg: %s", err);
        return false;
    }
    if (cfg->connect_host != NULL) {
        fdb = rl_connect(cfg->connect_host, cfg->connect_port, 5000, err, sizeof(err));
        if (fdb < 0) {
            snprintf(out->err, sizeof(out->err), "initiator leg: %s", err);
            rl_close(fda);
            return false;
        }
    }

    /* ⛔ TWO ENGINES, TWO KEYPAIRS, TWO EXCHANGES.  Not a wire.  A relay that
     * forwarded frames would produce ONE exchange and both peers would see
     * the SAME digits — which is precisely the outcome §11.8 says an
     * interposer cannot obtain, so a forwarding "relay" would report a pass
     * for the property it was built to break. */
    if (!rl_leg_init(&out->a, "leg-A (acceptor)", PPCP_BS_ROLE_ACCEPTOR, cfg->v,
                     fda, cfg->helper, &cfg->ctl_acceptor)) {
        snprintf(out->err, sizeof(out->err), "leg A: %s", out->a.err);
        out->have_a = true;
        if (fdb >= 0) rl_close(fdb);
        return false;
    }
    out->have_a = true;
    legs[n++] = &out->a;

    if (fdb >= 0) {
        if (!rl_leg_init(&out->b, "leg-B (initiator)", PPCP_BS_ROLE_INITIATOR,
                         cfg->v, fdb, cfg->helper, &cfg->ctl_initiator)) {
            snprintf(out->err, sizeof(out->err), "leg B: %s", out->b.err);
            out->have_b = true;
            return false;
        }
        out->have_b = true;
        legs[n++] = &out->b;
    }

    for (size_t i = 0; i < n; i++)
        (void)rl_leg_begin(legs[i]);

    (void)rl_pump(legs, n, rl_now_ms() + cfg->ctl_acceptor.exchange_timeout_ms +
                           cfg->ctl_acceptor.affirm_timeout_ms + 5000);
    return true;
}

static void relay_report(const rl_relay_out *o)
{
    fprintf(stderr, "\nrelay legs:\n");
    if (o->have_a) leg_describe(&o->a);
    if (o->have_b) leg_describe(&o->b);
    if (o->have_a && o->have_b && o->a.have_sas && o->b.have_sas) {
        char da[16], db[16];
        sas_str(o->a.sas, da, sizeof(da));
        sas_str(o->b.sas, db, sizeof(db));
        fprintf(stderr, "\n  the two legs show %s and %s — %s\n",
                da, db, o->a.sas == o->b.sas ? "⛔ THE SAME" : "DIFFERENT");
        fprintf(stderr, "  ⛔ Compare each against the screen it faces. This tool "
                        "does not\n     compare them for you and never will (11.1d).\n");
    }
}

/* ------------------------------------------------------------- the assertions */

static void assert_selftest_leg(rl_report *rep, const char *id, const char *what,
                                const rl_leg *relay_leg, const rl_peer_out *peer,
                                const char *cmd)
{
    rl_row *r = rl_row_add(rep, id, what, "paired",
                           "the relay's own leg reaches 11.5g against an honest "
                           "counterpart, and both ends show the same six digits");
    if (r == NULL)
        return;
    snprintf(r->command, sizeof(r->command), "%s", cmd);

    if (relay_leg->failed) {
        rl_row_set(r, RL_FAIL, "harness fault on the relay's leg: %s", relay_leg->err);
        return;
    }
    if (peer->failed) {
        rl_row_set(r, RL_FAIL, "harness fault on the stand-in peer");
        return;
    }
    if (!relay_leg->paired) {
        rl_row_set(r, RL_FAIL, "the relay's own leg did NOT complete (%s) — "
                               "⛔ the harness is testing its own bug",
                   relay_leg->aborted ? rl_reason_name(relay_leg->abort_rc)
                                      : "no pairing, no abort");
        return;
    }
    if (!peer->paired) {
        rl_row_set(r, RL_FAIL, "the relay paired but the honest counterpart did not");
        return;
    }
    if (!relay_leg->have_sas || !peer->have_sas ||
        relay_leg->sas != (uint32_t)peer->sas) {
        rl_row_set(r, RL_FAIL, "digits disagree across an HONEST pair: relay %06u, "
                               "peer %06u — the two ends derived different values "
                               "from the same exchange",
                   (unsigned)relay_leg->sas, peer->sas);
        return;
    }
    rl_row_set(r, RL_PASS, "leg completed to 11.5g; both ends show %06u; "
                           "MACs verified in both directions",
               (unsigned)relay_leg->sas);
}

int rl_selftest(const char *helper, uint8_t v, rl_report *rep, bool quiet);

/* ================================================================ selftest */

int rl_selftest(const char *helper, uint8_t v, rl_report *rep, bool quiet)
{
    rl_leg_ctl ctl;
    int        rc = 0;
    (void)quiet;

    memset(&ctl, 0, sizeof(ctl));
    ctl.exchange_timeout_ms = 20000;
    ctl.affirm_timeout_ms   = 20000;

    /* ---- 1. the relay's INITIATOR leg, against an honest acceptor -------- */
    {
        char        err[RL_ERR_LEN] = { 0 };
        int         port = 0;
        int         lfd  = rl_listen(0, &port, err, sizeof(err));
        rl_child    ch;
        rl_peer_out po;
        rl_leg      leg;
        rl_leg     *legs[1];
        int         afd;

        fprintf(stderr, "\n=== 1. the relay's own INITIATOR leg ===\n");
        if (lfd < 0) {
            fprintf(stderr, "selftest: %s\n", err);
            return 2;
        }
        /* The honest acceptor owns the listening socket in the child. */
        {
            int nothing = -1;
            if (!spawn_peer(&ch, PPCP_BS_ROLE_ACCEPTOR, v, lfd, true, helper, &ctl,
                            &nothing, 1)) {
                fprintf(stderr, "selftest: fork failed\n");
                return 2;
            }
        }
        afd = rl_connect("127.0.0.1", port, 5000, err, sizeof(err));
        if (afd < 0) {
            fprintf(stderr, "selftest: %s\n", err);
            return 2;
        }
        if (!rl_leg_init(&leg, "leg-B (initiator)", PPCP_BS_ROLE_INITIATOR, v,
                         afd, helper, &ctl)) {
            fprintf(stderr, "selftest: %s\n", leg.err);
            rl_leg_finish(&leg);
            return 2;
        }
        (void)rl_leg_begin(&leg);
        legs[0] = &leg;
        (void)rl_pump(legs, 1, rl_now_ms() + 45000);
        leg_describe(&leg);
        (void)reap_peer(&ch, &po);
        assert_selftest_leg(rep, "RT-20b(v)/initiator",
                            "the relay's initiator leg completes on demand",
                            &leg, &po, "ppcp-relay --selftest");
        if (rep->rows[rep->count - 1].verdict != RL_PASS)
            rc = 1;
        rl_leg_finish(&leg);
    }

    /* ---- 2. the relay's ACCEPTOR leg, against an honest initiator -------- */
    {
        char        err[RL_ERR_LEN] = { 0 };
        int         port = 0;
        int         lfd  = rl_listen(0, &port, err, sizeof(err));
        rl_child    ch;
        rl_peer_out po;
        rl_leg      leg;
        rl_leg     *legs[1];
        int         cfd, afd;

        fprintf(stderr, "\n=== 2. the relay's own ACCEPTOR leg ===\n");
        if (lfd < 0) {
            fprintf(stderr, "selftest: %s\n", err);
            return 2;
        }
        cfd = rl_connect("127.0.0.1", port, 5000, err, sizeof(err));
        if (cfd < 0) {
            fprintf(stderr, "selftest: %s\n", err);
            rl_close(lfd);
            return 2;
        }
        {
            int close_lfd = lfd;
            if (!spawn_peer(&ch, PPCP_BS_ROLE_INITIATOR, v, cfd, false, helper, &ctl,
                            &close_lfd, 1)) {
                fprintf(stderr, "selftest: fork failed\n");
                return 2;
            }
        }
        afd = rl_accept(lfd, 10000, err, sizeof(err));
        rl_close(lfd);
        if (afd < 0) {
            fprintf(stderr, "selftest: %s\n", err);
            return 2;
        }
        if (!rl_leg_init(&leg, "leg-A (acceptor)", PPCP_BS_ROLE_ACCEPTOR, v,
                         afd, helper, &ctl)) {
            fprintf(stderr, "selftest: %s\n", leg.err);
            rl_leg_finish(&leg);
            return 2;
        }
        (void)rl_leg_begin(&leg);
        legs[0] = &leg;
        (void)rl_pump(legs, 1, rl_now_ms() + 45000);
        leg_describe(&leg);
        (void)reap_peer(&ch, &po);
        assert_selftest_leg(rep, "RT-20b(v)/acceptor",
                            "the relay's acceptor leg completes on demand",
                            &leg, &po, "ppcp-relay --selftest");
        if (rep->rows[rep->count - 1].verdict != RL_PASS)
            rc = 1;
        rl_leg_finish(&leg);
    }


    /* ---- 3. the interposition itself, end to end ------------------------ */
    /* peer-initiator -> [relay leg A | relay leg B] -> peer-acceptor.
     *
     * ⛔ THIS IS THE PROPERTY, NOT A WARM-UP.  Two honest peers, a real
     * interposer between them holding a real key on each leg, and the six
     * digits each honest peer sees MUST DIFFER.  That is §11.8 in one run,
     * and it is what an operator comparing two screens is being asked to
     * notice.  RT-20a(a) shows the same thing as arithmetic over a published
     * quadruple; this shows it over sockets, with keys nobody chose in
     * advance and an attacker that had to commit blind (11.5c).
     *
     * ⚠ AND IT IS STILL NOT RT-20c.  Both honest ends here are `libppcp`'s
     * own engine.  RT-20c needs PinPointStudio and PinPointCapture in those
     * two places, and nothing smaller substitutes for it. */
    {
        char        err[RL_ERR_LEN] = { 0 };
        int         port_a = 0, port_b = 0;
        int         lfd_a, lfd_b, cfd_i;
        rl_child    ch_i, ch_a;
        rl_peer_out po_i, po_a;
        rl_relay_cfg cfg;
        rl_relay_out out;
        rl_row      *r;

        fprintf(stderr, "\n=== 3. the interposition, end to end ===\n");
        lfd_a = rl_listen(0, &port_a, err, sizeof(err));
        lfd_b = rl_listen(0, &port_b, err, sizeof(err));
        if (lfd_a < 0 || lfd_b < 0) {
            fprintf(stderr, "selftest: %s\n", err);
            return 2;
        }

        /* The honest acceptor opens the window the relay will dial. */
        {
            int close_a = lfd_a;
            if (!spawn_peer(&ch_a, PPCP_BS_ROLE_ACCEPTOR, v, lfd_b, true, helper,
                            &ctl, &close_a, 1)) {
                fprintf(stderr, "selftest: fork failed\n");
                return 2;
            }
        }
        /* The honest initiator dials what it believes is that window. */
        cfd_i = rl_connect("127.0.0.1", port_a, 5000, err, sizeof(err));
        if (cfd_i < 0) {
            fprintf(stderr, "selftest: %s\n", err);
            return 2;
        }
        {
            int close_a = lfd_a;
            if (!spawn_peer(&ch_i, PPCP_BS_ROLE_INITIATOR, v, cfd_i, false, helper,
                            &ctl, &close_a, 1)) {
                fprintf(stderr, "selftest: fork failed\n");
                return 2;
            }
        }

        memset(&cfg, 0, sizeof(cfg));
        cfg.v                 = v;
        cfg.helper            = helper;
        cfg.connect_host      = "127.0.0.1";
        cfg.connect_port      = port_b;
        cfg.accept_timeout_ms = 10000;
        cfg.ctl_acceptor      = ctl;
        cfg.ctl_initiator     = ctl;

        (void)relay_run(&cfg, lfd_a, &out);
        rl_close(lfd_a);
        relay_report(&out);
        (void)reap_peer(&ch_i, &po_i);
        (void)reap_peer(&ch_a, &po_a);

        r = rl_row_add(rep, "RT-20b(i)/relay",
                       "an interposer's two legs show DIFFERENT six digits",
                       "paired",
                       "two honest peers either side of a real relay; each peer's "
                       "digits equal 11.6c for its own leg, and the two legs differ");
        snprintf(r->command, sizeof(r->command), "ppcp-relay --selftest");

        if (out.err[0] != '\0') {
            rl_row_set(r, RL_FAIL, "harness fault: %s", out.err);
            rc = 1;
        } else if (!out.a.paired || !out.b.paired) {
            rl_row_set(r, RL_FAIL, "a relay leg did not complete (A %s, B %s)",
                       out.a.paired ? "paired" : "no", out.b.paired ? "paired" : "no");
            rc = 1;
        } else if (!po_i.paired || !po_a.paired) {
            rl_row_set(r, RL_FAIL, "an honest peer did not complete "
                                   "(initiator %s, acceptor %s)",
                       po_i.paired ? "paired" : "no", po_a.paired ? "paired" : "no");
            rc = 1;
        } else if (out.a.sas != (uint32_t)po_i.sas ||
                   out.b.sas != (uint32_t)po_a.sas) {
            /* Each honest peer and the relay leg facing it are the two ends of
             * ONE exchange, so they must agree — this is RT-18's property on
             * each leg, and it is what proves the relay is a real peer rather
             * than a thing that happens to move bytes. */
            rl_row_set(r, RL_FAIL, "a leg's two ends disagree: A relay %06u vs "
                                   "peer %06u; B relay %06u vs peer %06u",
                       (unsigned)out.a.sas, po_i.sas,
                       (unsigned)out.b.sas, po_a.sas);
            rc = 1;
        } else if (out.a.sas == out.b.sas) {
            /* ⛔ Either the relay is forwarding rather than interposing, or
             * §11.8 does not hold.  One in a million says it is the former. */
            rl_row_set(r, RL_FAIL, "⛔ BOTH LEGS SHOW %06u — an interposer that "
                                   "the comparison would not catch",
                       (unsigned)out.a.sas);
            rc = 1;
        } else {
            rl_row_set(r, RL_PASS, "the two honest peers saw %06u and %06u — "
                                   "different, so a person comparing two screens "
                                   "sees the interposition",
                       po_i.sas, po_a.sas);
        }
        if (out.have_a) rl_leg_finish(&out.a);
        if (out.have_b) rl_leg_finish(&out.b);
    }

    /* ---- 4. the ordering, ACCEPTOR mirror ------------------------------- */
    /* ⛔ RT-20b(ii) AND THE REASON THIS TOOL EXISTS.  The relay is the
     * initiator: it sends the commitment and then WITHHOLDS `bs_reveal`, so
     * the peer under test never sees `pk_i`.  A conforming acceptor has
     * already sent `pk_a` (11.5c).  An acceptor carrying trap 2 is still
     * waiting — and would be waiting having produced a byte-identical wire
     * trace up to this point. */
    {
        char        err[RL_ERR_LEN] = { 0 };
        int         port = 0;
        int         lfd  = rl_listen(0, &port, err, sizeof(err));
        rl_child    ch;
        rl_peer_out po;
        rl_leg      leg;
        rl_leg     *legs[1];
        rl_leg_ctl  probe = ctl;
        /* The stand-in gets a SHORT 11.3e budget so the row finishes in
         * seconds.  It is a harness parameter and not a claim: 11.3e is a
         * SHOULD about how long a peer waits, and shortening it changes when
         * the honest side gives up, not what either side asserts. */
        rl_leg_ctl  probe_peer = ctl;
        rl_row     *r;
        int         cfd;

        fprintf(stderr, "\n=== 4. 11.5c, the acceptor mirror ===\n");
        probe.withhold_reveal     = true;
        probe.exchange_timeout_ms = 3000;
        probe.affirm_timeout_ms   = 3000;
        probe_peer.exchange_timeout_ms = 4000;
        probe_peer.affirm_timeout_ms   = 4000;

        if (lfd < 0) { fprintf(stderr, "selftest: %s\n", err); return 2; }
        {
            int nothing = -1;
            if (!spawn_peer(&ch, PPCP_BS_ROLE_ACCEPTOR, v, lfd, true, helper,
                            &probe_peer, &nothing, 1)) {
                fprintf(stderr, "selftest: fork failed\n");
                return 2;
            }
        }
        cfd = rl_connect("127.0.0.1", port, 5000, err, sizeof(err));
        if (cfd < 0) { fprintf(stderr, "selftest: %s\n", err); return 2; }
        if (!rl_leg_init(&leg, "probe (initiator)", PPCP_BS_ROLE_INITIATOR, v,
                         cfd, helper, &probe)) {
            fprintf(stderr, "selftest: %s\n", leg.err);
            rl_leg_finish(&leg);
            return 2;
        }
        (void)rl_leg_begin(&leg);
        legs[0] = &leg;
        (void)rl_pump(legs, 1, rl_now_ms() + 12000);
        (void)reap_peer(&ch, &po);

        r = rl_row_add(rep, "RT-20b(ii)/acceptor",
                       "the acceptor sends bs_accept BEFORE it has seen pk_i (11.5c)",
                       "injected",
                       "relay sends only the commitment and withholds bs_reveal; "
                       "pk_a must already have arrived");
        snprintf(r->command, sizeof(r->command), "ppcp-relay --selftest");
        if (leg.failed) {
            rl_row_set(r, RL_FAIL, "harness fault: %s", leg.err);
            rc = 1;
        } else if (leg.accept_before_reveal) {
            rl_row_set(r, RL_PASS, "bs_accept carrying pk_a arrived while the relay "
                                   "had sent nothing but ct — the peer committed "
                                   "BLIND, which is the whole security of the path");
        } else if (leg.saw_accept) {
            rl_row_set(r, RL_FAIL, "bs_accept arrived only after bs_reveal — the "
                                   "harness revealed pk_i and the assertion is void");
            rc = 1;
        } else {
            rl_row_set(r, RL_FAIL, "⛔ NO bs_accept while pk_i was withheld: the "
                                   "peer is waiting for pk_i before choosing pk_a "
                                   "(11.5c, trap 2) — this destroys the path");
            rc = 1;
        }
        fprintf(stderr, "  %s\n", r->reason);
        rl_leg_finish(&leg);
    }

    /* ---- 5. the ordering, INITIATOR mirror ------------------------------ */
    /* E54's addition, finding R-23: 9e1 lets a peer claim ONE role, and one
     * implementation here claims each, so each runs its own mirror.  The
     * relay is the acceptor and simply does not reply.  A conforming
     * initiator sends `bs_offer` carrying only `ct` (11.5b) and sends `pk_i`
     * ONLY after `bs_accept` arrives (11.5d) — so no `bs_reveal` may follow. */
    {
        char        err[RL_ERR_LEN] = { 0 };
        int         port = 0;
        int         lfd  = rl_listen(0, &port, err, sizeof(err));
        rl_child    ch;
        rl_peer_out po;
        rl_leg      leg;
        rl_leg     *legs[1];
        rl_leg_ctl  probe = ctl;
        rl_leg_ctl  probe_peer = ctl;
        rl_row     *r;
        int         cfd, afd;

        fprintf(stderr, "\n=== 5. 11.5b/11.5d, the initiator mirror ===\n");
        probe.no_reply           = true;
        probe.exchange_timeout_ms = 3000;
        probe.affirm_timeout_ms   = 3000;
        probe_peer.exchange_timeout_ms = 4000;
        probe_peer.affirm_timeout_ms   = 4000;

        if (lfd < 0) { fprintf(stderr, "selftest: %s\n", err); return 2; }
        cfd = rl_connect("127.0.0.1", port, 5000, err, sizeof(err));
        if (cfd < 0) { fprintf(stderr, "selftest: %s\n", err); rl_close(lfd); return 2; }
        {
            int close_lfd = lfd;
            if (!spawn_peer(&ch, PPCP_BS_ROLE_INITIATOR, v, cfd, false, helper,
                            &probe_peer, &close_lfd, 1)) {
                fprintf(stderr, "selftest: fork failed\n");
                return 2;
            }
        }
        afd = rl_accept(lfd, 10000, err, sizeof(err));
        rl_close(lfd);
        if (afd < 0) { fprintf(stderr, "selftest: %s\n", err); return 2; }
        if (!rl_leg_init(&leg, "probe (acceptor)", PPCP_BS_ROLE_ACCEPTOR, v,
                         afd, helper, &probe)) {
            fprintf(stderr, "selftest: %s\n", leg.err);
            rl_leg_finish(&leg);
            return 2;
        }
        (void)rl_leg_begin(&leg);
        legs[0] = &leg;
        (void)rl_pump(legs, 1, rl_now_ms() + 12000);
        (void)reap_peer(&ch, &po);

        r = rl_row_add(rep, "RT-20b(ii)/initiator",
                       "the initiator sends only ct, and pk_i only after bs_accept "
                       "(11.5b, 11.5d)",
                       "injected",
                       "relay accepts the offer and never replies; no bs_reveal may "
                       "follow, and the offer itself must carry no pk_i");
        snprintf(r->command, sizeof(r->command), "ppcp-relay --selftest");
        if (leg.failed) {
            rl_row_set(r, RL_FAIL, "harness fault: %s", leg.err);
            rc = 1;
        } else if (!leg.saw_offer) {
            rl_row_set(r, RL_FAIL, "no well-formed bs_offer arrived at all");
            rc = 1;
        } else if (leg.saw_reveal) {
            rl_row_set(r, RL_FAIL, "⛔ bs_reveal followed an offer this relay NEVER "
                                   "answered: the peer sent pk_i without bs_accept "
                                   "(11.5d)");
            rc = 1;
        } else {
            /* The offer decoded as a `bs_offer`, and §11's vocabulary is
             * CLOSED (11.4c1/E46) — an unrecognised map key is MALFORMED — so
             * a decoded offer is one that carried `v` and `ct` and no `pk_i`.
             * That is 11.5b checked by the reader rather than by inspection. */
            rl_row_set(r, RL_PASS, "bs_offer carried v and ct only; the relay stayed "
                                   "silent and no bs_reveal followed (%zu octets did, "
                                   "none of them a reveal)", leg.bytes_after_silence);
        }
        fprintf(stderr, "  %s\n", r->reason);
        rl_leg_finish(&leg);
    }

    /* ---- 6. THE NEGATIVE CONTROL ---------------------------------------- */
    /* ⛔ THE PROBE MUST BE ABLE TO FAIL, OR IT IS A ROW THAT GETS TICKED.
     * Steps 4 and 5 assert that a conforming peer passes.  Neither shows the
     * probe would NOTICE a peer that did not — and trap 2 is invisible on the
     * wire, so nothing else would either.  So here the stand-in acceptor is
     * given trap 2 deliberately (it holds `bs_accept` until `pk_i` arrives),
     * the same probe is run against it, and the row PASSES ONLY IF THE PROBE
     * REPORTS A FAILURE.  This is the one place in the harness where a red
     * verdict is the correct answer. */
    {
        char        err[RL_ERR_LEN] = { 0 };
        int         port = 0;
        int         lfd  = rl_listen(0, &port, err, sizeof(err));
        rl_child    ch;
        rl_peer_out po;
        rl_leg      leg;
        rl_leg     *legs[1];
        rl_leg_ctl  probe   = ctl;
        rl_leg_ctl  trapped = ctl;
        rl_row     *r;
        int         cfd;

        fprintf(stderr, "\n=== 6. negative control: an acceptor carrying trap 2 ===\n");
        probe.withhold_reveal     = true;
        probe.exchange_timeout_ms = 3000;
        probe.affirm_timeout_ms   = 3000;
        trapped.defer_accept      = true;
        trapped.exchange_timeout_ms = 4000;
        trapped.affirm_timeout_ms   = 4000;

        if (lfd < 0) { fprintf(stderr, "selftest: %s\n", err); return 2; }
        {
            int nothing = -1;
            if (!spawn_peer(&ch, PPCP_BS_ROLE_ACCEPTOR, v, lfd, true, helper,
                            &trapped, &nothing, 1)) {
                fprintf(stderr, "selftest: fork failed\n");
                return 2;
            }
        }
        cfd = rl_connect("127.0.0.1", port, 5000, err, sizeof(err));
        if (cfd < 0) { fprintf(stderr, "selftest: %s\n", err); return 2; }
        if (!rl_leg_init(&leg, "probe (initiator)", PPCP_BS_ROLE_INITIATOR, v,
                         cfd, helper, &probe)) {
            fprintf(stderr, "selftest: %s\n", leg.err);
            rl_leg_finish(&leg);
            return 2;
        }
        (void)rl_leg_begin(&leg);
        legs[0] = &leg;
        (void)rl_pump(legs, 1, rl_now_ms() + 12000);
        (void)reap_peer(&ch, &po);

        r = rl_row_add(rep, "RT-20b(ii)/control",
                       "the ordering probe DETECTS a peer that sends bs_accept "
                       "only after pk_i (trap 2)", "injected",
                       "against a deliberately trap-2 stand-in the probe must "
                       "report a FAILURE; if it reports a pass the probe is blind");
        snprintf(r->command, sizeof(r->command), "ppcp-relay --selftest");
        if (leg.failed) {
            rl_row_set(r, RL_FAIL, "harness fault: %s", leg.err);
            rc = 1;
        } else if (leg.accept_before_reveal) {
            rl_row_set(r, RL_FAIL, "⛔ THE PROBE IS BLIND: it saw pk_a arrive "
                                   "before the reveal from a peer built to "
                                   "withhold it — RT-20b(ii) would pass a trap-2 "
                                   "implementation");
            rc = 1;
        } else {
            rl_row_set(r, RL_PASS, "no bs_accept arrived while pk_i was withheld — "
                                   "the probe caught trap 2, so its pass in step 4 "
                                   "is a measurement and not a formality");
        }
        fprintf(stderr, "  %s\n", r->reason);
        rl_leg_finish(&leg);
    }

    /* ---- 7-9. THE INJECTED ROWS ----------------------------------------- */
    /* RT-19, RT-21 and RT-24 against an honest stand-in.  libppcp's own suite
     * already asserts each of these against its own engine; running them from
     * here proves the PROBE works, so that when H and D point it at their
     * implementations a red row is about their code and not about this tool.
     * Same discipline as step 6: an instrument is not trusted until it has
     * been shown to produce the answer on a case whose answer is known. */
    {
        struct {
            const char    *id;
            const char    *what;
            const char    *asserts;
            rl_rewrite     rewrite;
            ppcp_bs_role   relay_role;
            ppcp_bs_reason want;
        } probes[3];
        size_t k;

        probes[0].id      = "RT-19";
        probes[0].what    = "a reveal that does not hash to the commitment aborts "
                            "with commitment_mismatch, and nothing is derived (11.5d)";
        probes[0].asserts = "one bit of pk_i flipped after the commitment was sent";
        probes[0].rewrite = RL_RW_BAD_REVEAL;
        probes[0].relay_role = PPCP_BS_ROLE_INITIATOR;
        probes[0].want    = PPCP_BS_RC_COMMITMENT_MISMATCH;

        probes[1].id      = "RT-21";
        probes[1].what    = "an all-zero pk aborts with invalid_key, nothing is "
                            "derived, and it is NOT retried (11.6b, 11.11f)";
        probes[1].asserts = "an HONEST commitment to an all-zero key, so the peer "
                            "reaches key agreement rather than stopping at 11.5d";
        probes[1].rewrite = RL_RW_ZERO_KEY;
        probes[1].relay_role = PPCP_BS_ROLE_INITIATOR;
        probes[1].want    = PPCP_BS_RC_INVALID_KEY;

        probes[2].id      = "RT-24";
        probes[2].what    = "bs_accept.v different from the offered v aborts (11.4h)";
        probes[2].asserts = "the acceptor's v incremented on the wire";
        probes[2].rewrite = RL_RW_WRONG_V;
        probes[2].relay_role = PPCP_BS_ROLE_ACCEPTOR;
        probes[2].want    = PPCP_BS_RC_UNSUPPORTED_VERSION;

        for (k = 0; k < 3; k++) {
            char        err[RL_ERR_LEN] = { 0 };
            int         port = 0;
            int         lfd, cfd, afd = -1;
            rl_child    ch;
            rl_peer_out po;
            rl_leg      leg;
            rl_leg     *legs[1];
            rl_leg_ctl  probe = ctl, peer_ctl = ctl;
            rl_row     *r;
            bool        relay_is_initiator =
                            (probes[k].relay_role == PPCP_BS_ROLE_INITIATOR);

            fprintf(stderr, "\n=== %zu. %s ===\n", 7 + k, probes[k].id);
            probe.rewrite             = probes[k].rewrite;
            probe.exchange_timeout_ms = 4000;
            probe.affirm_timeout_ms   = 4000;
            peer_ctl.exchange_timeout_ms = 5000;
            peer_ctl.affirm_timeout_ms   = 5000;

            lfd = rl_listen(0, &port, err, sizeof(err));
            if (lfd < 0) { fprintf(stderr, "selftest: %s\n", err); return 2; }

            if (relay_is_initiator) {
                int nothing = -1;
                if (!spawn_peer(&ch, PPCP_BS_ROLE_ACCEPTOR, v, lfd, true, helper,
                                &peer_ctl, &nothing, 1)) {
                    fprintf(stderr, "selftest: fork failed\n"); return 2;
                }
                cfd = rl_connect("127.0.0.1", port, 5000, err, sizeof(err));
                if (cfd < 0) { fprintf(stderr, "selftest: %s\n", err); return 2; }
                afd = cfd;
            } else {
                int close_lfd = lfd;
                cfd = rl_connect("127.0.0.1", port, 5000, err, sizeof(err));
                if (cfd < 0) {
                    fprintf(stderr, "selftest: %s\n", err);
                    rl_close(lfd); return 2;
                }
                if (!spawn_peer(&ch, PPCP_BS_ROLE_INITIATOR, v, cfd, false, helper,
                                &peer_ctl, &close_lfd, 1)) {
                    fprintf(stderr, "selftest: fork failed\n"); return 2;
                }
                afd = rl_accept(lfd, 10000, err, sizeof(err));
                rl_close(lfd);
                if (afd < 0) { fprintf(stderr, "selftest: %s\n", err); return 2; }
            }

            if (!rl_leg_init(&leg, "probe", probes[k].relay_role, v, afd,
                             helper, &probe)) {
                fprintf(stderr, "selftest: %s\n", leg.err);
                rl_leg_finish(&leg);
                return 2;
            }
            (void)rl_leg_begin(&leg);
            legs[0] = &leg;
            (void)rl_pump(legs, 1, rl_now_ms() + 15000);
            (void)reap_peer(&ch, &po);

            r = rl_row_add(rep, probes[k].id, probes[k].what, "injected",
                           probes[k].asserts);
            snprintf(r->command, sizeof(r->command), "ppcp-relay --selftest");
            if (leg.failed) {
                rl_row_set(r, RL_FAIL, "harness fault: %s", leg.err);
                rc = 1;
            } else if (!leg.rewrote) {
                rl_row_set(r, RL_FAIL, "the probe never got far enough to rewrite "
                                       "a frame; nothing was measured");
                rc = 1;
            } else if (po.paired || leg.paired) {
                rl_row_set(r, RL_FAIL, "⛔ A PAIRING WAS ESTABLISHED over a frame "
                                       "this probe deliberately corrupted");
                rc = 1;
            } else if (leg.saw_abort && leg.peer_abort_rc == probes[k].want) {
                rl_row_set(r, RL_PASS, "the peer aborted with `%s`, and no pairing "
                                       "exists at either end",
                           rl_reason_name(probes[k].want));
            } else if (leg.saw_abort) {
                rl_row_set(r, RL_FAIL, "the peer aborted with `%s`, expected `%s` — "
                                       "the right refusal for the wrong reason is "
                                       "still a divergence",
                           rl_reason_name(leg.peer_abort_rc),
                           rl_reason_name(probes[k].want));
                rc = 1;
            } else {
                rl_row_set(r, RL_FAIL, "no bs_abort arrived; the peer neither "
                                       "paired nor refused");
                rc = 1;
            }
            fprintf(stderr, "  %s\n", r->reason);
            rl_leg_finish(&leg);
        }
    }

    /* ---- 10. the decline, RT-20b(iii) ----------------------------------- */
    /* The `--probe decline` path is code H and D will run against their own
     * applications, and untested probe code produces a FALSE RED that lands
     * in the wrong repository — the same failure the withheld-`bs_confirm`
     * bug would have produced.  So it is exercised here first.
     *
     * ⛔ AND ONLY HALF OF IT IS, DELIBERATELY.  RT-20b(iv) — "the window
     * closes and does not reopen without a further user action" — is NOT
     * self-tested and MUST NOT BE.  A `--peer` stand-in has no bootstrap
     * window: it accepts one connection and exits.  A second dial therefore
     * finds nothing listening and the probe would report `pass` for a
     * property the stand-in never had.  That is a manufactured green of
     * exactly the kind 3.7b and 11.9b exist to prevent
     * anyone claiming, and it would be worse than no coverage because it
     * would read as coverage.  RT-20b(iv) is first exercised against
     * PinPointCapture, which has a real window. */
    {
        char        err[RL_ERR_LEN] = { 0 };
        int         port = 0;
        int         lfd, cfd;
        rl_child    ch;
        rl_peer_out po;
        rl_leg      leg;
        rl_leg     *legs[1];
        rl_leg_ctl  probe = ctl, peer_ctl = ctl;
        rl_row     *r;

        fprintf(stderr, "\n=== 10. RT-20b(iii), the decline ===\n");
        probe.decline             = true;
        probe.exchange_timeout_ms = 5000;
        probe.affirm_timeout_ms   = 5000;
        peer_ctl.exchange_timeout_ms = 6000;
        peer_ctl.affirm_timeout_ms   = 6000;

        lfd = rl_listen(0, &port, err, sizeof(err));
        if (lfd < 0) { fprintf(stderr, "selftest: %s\n", err); return 2; }
        {
            int nothing = -1;
            if (!spawn_peer(&ch, PPCP_BS_ROLE_ACCEPTOR, v, lfd, true, helper,
                            &peer_ctl, &nothing, 1)) {
                fprintf(stderr, "selftest: fork failed\n"); return 2;
            }
        }
        cfd = rl_connect("127.0.0.1", port, 5000, err, sizeof(err));
        if (cfd < 0) { fprintf(stderr, "selftest: %s\n", err); return 2; }
        if (!rl_leg_init(&leg, "probe (initiator)", PPCP_BS_ROLE_INITIATOR, v,
                         cfd, helper, &probe)) {
            fprintf(stderr, "selftest: %s\n", leg.err);
            rl_leg_finish(&leg);
            return 2;
        }
        (void)rl_leg_begin(&leg);
        legs[0] = &leg;
        (void)rl_pump(legs, 1, rl_now_ms() + 20000);
        (void)reap_peer(&ch, &po);

        r = rl_row_add(rep, "RT-20b(iii)/control",
                       "a declined comparison pairs NEITHER end (11.5g, 11.7c)",
                       "injected",
                       "the relay reaches the digits and declines; neither it nor "
                       "the honest counterpart may hold a pairing");
        snprintf(r->command, sizeof(r->command), "ppcp-relay --selftest");
        if (leg.failed) {
            rl_row_set(r, RL_FAIL, "harness fault: %s", leg.err);
            rc = 1;
        } else if (!leg.have_sas) {
            rl_row_set(r, RL_FAIL, "the probe never reached the comparison, so the "
                                   "decline was never exercised");
            rc = 1;
        } else if (leg.paired) {
            rl_row_set(r, RL_FAIL, "⛔ the declining end paired anyway");
            rc = 1;
        } else if (po.paired) {
            /* 11.7c is the clause: one end's affirmation does not establish a
             * pairing at the other, and a peer MUST NOT treat the arrival of
             * the counterpart's bs_confirm as standing in for its own user's. */
            rl_row_set(r, RL_FAIL, "⛔ THE COUNTERPART PAIRED against a peer that "
                                   "declined — 11.5g needs BOTH its own "
                                   "affirmation and a verified MAC");
            rc = 1;
        } else {
            rl_row_set(r, RL_PASS, "declined at %06u with `rejected`; neither end "
                                   "holds a pairing. ⚠ RT-20b(iv), the window not "
                                   "reopening, is NOT covered here — a stand-in has "
                                   "no window and a pass would be manufactured",
                       (unsigned)leg.sas);
        }
        fprintf(stderr, "  %s\n", r->reason);
        rl_leg_finish(&leg);
    }

    return rc;
}

/* ============================================ running against a real peer */

int rl_relay_main(int listen_port, const char *host, int port, uint8_t v,
                  const char *helper, rl_report *rep)
{
    char         err[RL_ERR_LEN] = { 0 };
    int          bound = 0;
    int          lfd   = rl_listen(listen_port, &bound, err, sizeof(err));
    rl_relay_cfg cfg;
    rl_relay_out out;
    rl_row      *r;
    int          rc = 0;

    if (lfd < 0) {
        fprintf(stderr, "relay: %s\n", err);
        return 2;
    }
    memset(&cfg, 0, sizeof(cfg));
    cfg.v                 = v;
    cfg.helper            = helper;
    cfg.connect_host      = host;
    cfg.connect_port      = port;
    cfg.accept_timeout_ms = 180000;   /* 3.7b's window bound */
    cfg.ctl_acceptor.exchange_timeout_ms = RL_TIMEOUT_EXCHANGE_MS;
    cfg.ctl_acceptor.affirm_timeout_ms   = RL_TIMEOUT_AFFIRM_MS;
    cfg.ctl_initiator = cfg.ctl_acceptor;

    fprintf(stderr, "relay: listening on %d", bound);
    if (host != NULL)
        fprintf(stderr, ", will dial %s:%d when a peer arrives", host, port);
    fprintf(stderr, "\n");

    (void)relay_run(&cfg, lfd, &out);
    rl_close(lfd);
    relay_report(&out);

    r = rl_row_add(rep, "RT-20b(i)",
                   "each peer's digits are 11.6c's for its own leg, and the two "
                   "legs differ", "paired",
                   "run with both peers; compare each screen against the leg "
                   "facing it. ⛔ The comparison is a PERSON's (11.1d)");
    snprintf(r->command, sizeof(r->command),
             "ppcp-relay --listen %d --connect %s:%d", bound,
             host ? host : "-", port);
    if (out.err[0] != '\0') {
        rl_row_set(r, RL_FAIL, "harness fault: %s", out.err);
        rc = 2;
    } else if (out.have_a && out.have_b && out.a.have_sas && out.b.have_sas) {
        if (out.a.sas == out.b.sas) {
            rl_row_set(r, RL_FAIL, "⛔ both legs show %06u", (unsigned)out.a.sas);
            rc = 1;
        } else {
            rl_row_set(r, RL_PASS, "leg A %06u, leg B %06u — different; each "
                                   "peer's own screen is the other half of this "
                                   "row and a person reads it",
                       (unsigned)out.a.sas, (unsigned)out.b.sas);
        }
    } else {
        rl_row_set(r, RL_UNRUN, "a leg did not reach the comparison");
        rc = 1;
    }

    if (out.have_a) rl_leg_finish(&out.a);
    if (out.have_b) rl_leg_finish(&out.b);
    return rc;
}

int rl_probe_main(const char *what, int listen_port, const char *host, int port,
                  uint8_t v, const char *helper, int observe_ms, rl_report *rep)
{
    char       err[RL_ERR_LEN] = { 0 };
    rl_leg     leg;
    rl_leg    *legs[1];
    rl_leg_ctl ctl;
    rl_row    *r;
    int        fd, rc = 0;

    memset(&ctl, 0, sizeof(ctl));
    ctl.exchange_timeout_ms = observe_ms;
    ctl.affirm_timeout_ms   = observe_ms;

    if (strcmp(what, "order-acceptor") == 0) {
        /* The peer under test is an ACCEPTOR; we are the initiator. */
        ctl.withhold_reveal = true;
        if (host == NULL) {
            fprintf(stderr, "probe order-acceptor needs --connect host:port\n");
            return 2;
        }
        fd = rl_connect(host, port, 5000, err, sizeof(err));
        if (fd < 0) { fprintf(stderr, "probe: %s\n", err); return 2; }
        if (!rl_leg_init(&leg, "probe (initiator)", PPCP_BS_ROLE_INITIATOR, v,
                         fd, helper, &ctl)) {
            fprintf(stderr, "probe: %s\n", leg.err);
            rl_leg_finish(&leg);
            return 2;
        }
        (void)rl_leg_begin(&leg);
        legs[0] = &leg;
        (void)rl_pump(legs, 1, rl_now_ms() + observe_ms + 5000);

        r = rl_row_add(rep, "RT-20b(ii)/acceptor",
                       "the acceptor sends bs_accept BEFORE it has seen pk_i (11.5c)",
                       "injected",
                       "bs_reveal withheld; pk_a must already have arrived");
        snprintf(r->command, sizeof(r->command),
                 "ppcp-relay --probe order-acceptor --connect %s:%d", host, port);
        if (leg.failed) {
            rl_row_set(r, RL_FAIL, "harness fault: %s", leg.err); rc = 2;
        } else if (leg.accept_before_reveal) {
            rl_row_set(r, RL_PASS, "bs_accept carrying pk_a arrived while only ct "
                                   "had been sent — the peer committed blind");
        } else {
            rl_row_set(r, RL_FAIL, "⛔ no bs_accept while pk_i was withheld — the "
                                   "peer waits for pk_i before choosing pk_a "
                                   "(11.5c, trap 2)");
            rc = 1;
        }
        fprintf(stderr, "%s: %s\n", r->id, r->reason);
        rl_leg_finish(&leg);
        return rc;
    }

    if (strcmp(what, "order-initiator") == 0) {
        /* The peer under test is an INITIATOR; we are the acceptor and we
         * never reply. */
        int bound = 0;
        int lfd;
        ctl.no_reply = true;
        lfd = rl_listen(listen_port, &bound, err, sizeof(err));
        if (lfd < 0) { fprintf(stderr, "probe: %s\n", err); return 2; }
        fprintf(stderr, "probe: listening on %d; dial it with the peer under test\n",
                bound);
        fd = rl_accept(lfd, 180000, err, sizeof(err));
        rl_close(lfd);
        if (fd < 0) { fprintf(stderr, "probe: %s\n", err); return 2; }
        if (!rl_leg_init(&leg, "probe (acceptor)", PPCP_BS_ROLE_ACCEPTOR, v,
                         fd, helper, &ctl)) {
            fprintf(stderr, "probe: %s\n", leg.err);
            rl_leg_finish(&leg);
            return 2;
        }
        (void)rl_leg_begin(&leg);
        legs[0] = &leg;
        (void)rl_pump(legs, 1, rl_now_ms() + observe_ms + 5000);

        r = rl_row_add(rep, "RT-20b(ii)/initiator",
                       "the initiator sends only ct, and pk_i only after bs_accept "
                       "(11.5b, 11.5d)", "injected",
                       "the relay never replies; no bs_reveal may follow");
        snprintf(r->command, sizeof(r->command),
                 "ppcp-relay --probe order-initiator --listen %d", bound);
        if (leg.failed) {
            rl_row_set(r, RL_FAIL, "harness fault: %s", leg.err); rc = 2;
        } else if (!leg.saw_offer) {
            rl_row_set(r, RL_FAIL, "no well-formed bs_offer arrived"); rc = 1;
        } else if (leg.saw_reveal) {
            rl_row_set(r, RL_FAIL, "⛔ bs_reveal followed an unanswered offer — "
                                   "pk_i sent without bs_accept (11.5d)");
            rc = 1;
        } else {
            rl_row_set(r, RL_PASS, "bs_offer carried v and ct only; no bs_reveal "
                                   "followed in %d ms of silence", observe_ms);
        }
        fprintf(stderr, "%s: %s\n", r->id, r->reason);
        rl_leg_finish(&leg);
        return rc;
    }

    if (strcmp(what, "decline") == 0) {
        /* RT-20b(iii)/(iv).  We reach the comparison and this end's "user"
         * declines; the peer under test must not pair, and its window must
         * not reopen without a further user action (3.7b, 11.9b).  The
         * reopening half is measured by dialling again afterwards. */
        bool second_refused = false;
        ctl.decline = true;
        if (host == NULL) {
            fprintf(stderr, "probe decline needs --connect host:port\n");
            return 2;
        }
        fd = rl_connect(host, port, 5000, err, sizeof(err));
        if (fd < 0) { fprintf(stderr, "probe: %s\n", err); return 2; }
        if (!rl_leg_init(&leg, "probe (initiator)", PPCP_BS_ROLE_INITIATOR, v,
                         fd, helper, &ctl)) {
            fprintf(stderr, "probe: %s\n", leg.err);
            rl_leg_finish(&leg);
            return 2;
        }
        (void)rl_leg_begin(&leg);
        legs[0] = &leg;
        (void)rl_pump(legs, 1, rl_now_ms() + observe_ms + 5000);

        r = rl_row_add(rep, "RT-20b(iii)",
                       "a peer whose user declines does not pair", "injected",
                       "the relay declines at the comparison; no bs_confirm may "
                       "follow and nothing may be derived");
        snprintf(r->command, sizeof(r->command),
                 "ppcp-relay --probe decline --connect %s:%d", host, port);
        if (leg.failed) {
            rl_row_set(r, RL_FAIL, "harness fault: %s", leg.err); rc = 2;
        } else if (!leg.have_sas) {
            rl_row_set(r, RL_FAIL, "never reached the comparison"); rc = 1;
        } else if (leg.paired) {
            rl_row_set(r, RL_FAIL, "⛔ paired despite declining"); rc = 1;
        } else {
            rl_row_set(r, RL_PASS, "declined at %06u with `rejected`; no pairing "
                                   "on this side%s", (unsigned)leg.sas,
                       leg.saw_confirm ? " (peer had already confirmed, which is "
                                         "11.7c: its confirm is not this end's "
                                         "affirmation)" : "");
        }
        fprintf(stderr, "%s: %s\n", r->id, r->reason);
        rl_leg_finish(&leg);

        /* 3.7b / 11.9b — the window closes on an aborted attempt and does not
         * reopen without a further user action.  A second dial must not find
         * a live window. */
        {
            char err2[RL_ERR_LEN] = { 0 };
            int  fd2 = rl_connect(host, port, 3000, err2, sizeof(err2));
            if (fd2 < 0) {
                second_refused = true;
            } else {
                rl_leg     l2;
                rl_leg    *ls2[1];
                rl_leg_ctl c2 = ctl;
                c2.decline = false;
                c2.exchange_timeout_ms = 3000;
                c2.affirm_timeout_ms   = 3000;
                if (rl_leg_init(&l2, "second dial", PPCP_BS_ROLE_INITIATOR, v,
                                fd2, helper, &c2)) {
                    (void)rl_leg_begin(&l2);
                    ls2[0] = &l2;
                    (void)rl_pump(ls2, 1, rl_now_ms() + 8000);
                    second_refused = !l2.paired &&
                                     (l2.peer_eof ||
                                      (l2.saw_abort &&
                                       l2.peer_abort_rc == PPCP_BS_RC_WINDOW_CLOSED));
                }
                rl_leg_finish(&l2);
            }
            r = rl_row_add(rep, "RT-20b(iv)",
                           "the window closes and does not reopen without a further "
                           "user action (3.7b, 11.9b)", "injected",
                           "a second dial after an aborted attempt finds no window");
            snprintf(r->command, sizeof(r->command),
                     "ppcp-relay --probe decline --connect %s:%d", host, port);
            if (second_refused)
                rl_row_set(r, RL_PASS, "the second dial found no open window");
            else {
                rl_row_set(r, RL_FAIL, "⛔ a second attempt was served after the "
                                       "first was aborted — 3.7b's single-attempt "
                                       "bound is what §11.8 rests on");
                rc = 1;
            }
            fprintf(stderr, "%s: %s\n", r->id, r->reason);
        }
        return rc;
    }

    /* The three `injected` rows of §9 that need a counterpart rather than a
     * unit test.  Each corrupts ONE field of ONE frame on the wire and
     * asserts the peer's refusal — and asserts the REASON, because the right
     * refusal for the wrong reason is still a divergence and is exactly the
     * class §10.4's counter-vectors exist to catch. */
    if (strcmp(what, "rt19") == 0 || strcmp(what, "rt21") == 0 ||
        strcmp(what, "rt24") == 0) {
        ppcp_bs_role   role;
        ppcp_bs_reason want;
        const char    *id, *inv;
        bool           dial;

        if (strcmp(what, "rt19") == 0) {
            ctl.rewrite = RL_RW_BAD_REVEAL;  role = PPCP_BS_ROLE_INITIATOR;
            want = PPCP_BS_RC_COMMITMENT_MISMATCH;  dial = true;
            id = "RT-19";
            inv = "a reveal that does not hash to the commitment aborts with "
                  "commitment_mismatch, and nothing is derived (11.5d)";
        } else if (strcmp(what, "rt21") == 0) {
            ctl.rewrite = RL_RW_ZERO_KEY;    role = PPCP_BS_ROLE_INITIATOR;
            want = PPCP_BS_RC_INVALID_KEY;   dial = true;
            id = "RT-21";
            inv = "an all-zero pk aborts with invalid_key, nothing is derived, "
                  "and it is NOT retried (11.6b, 11.11f)";
        } else {
            ctl.rewrite = RL_RW_WRONG_V;     role = PPCP_BS_ROLE_ACCEPTOR;
            want = PPCP_BS_RC_UNSUPPORTED_VERSION; dial = false;
            id = "RT-24";
            inv = "bs_accept.v different from the offered v aborts (11.4h)";
        }

        if (dial) {
            if (host == NULL) {
                fprintf(stderr, "probe %s needs --connect host:port\n", what);
                return 2;
            }
            fd = rl_connect(host, port, 5000, err, sizeof(err));
        } else {
            int bound = 0;
            int lfd = rl_listen(listen_port, &bound, err, sizeof(err));
            if (lfd < 0) { fprintf(stderr, "probe: %s\n", err); return 2; }
            fprintf(stderr, "probe: listening on %d; dial it with the peer "
                            "under test\n", bound);
            fd = rl_accept(lfd, 180000, err, sizeof(err));
            rl_close(lfd);
        }
        if (fd < 0) { fprintf(stderr, "probe: %s\n", err); return 2; }
        if (!rl_leg_init(&leg, "probe", role, v, fd, helper, &ctl)) {
            fprintf(stderr, "probe: %s\n", leg.err);
            rl_leg_finish(&leg);
            return 2;
        }
        (void)rl_leg_begin(&leg);
        legs[0] = &leg;
        (void)rl_pump(legs, 1, rl_now_ms() + observe_ms + 10000);

        r = rl_row_add(rep, id, inv, "injected",
                       "one field of one frame corrupted on the wire; the peer "
                       "must refuse, with the reason the clause names");
        snprintf(r->command, sizeof(r->command),
                 "ppcp-relay --probe %s %s", what,
                 dial ? "--connect HOST:PORT" : "--listen PORT");
        if (leg.failed) {
            rl_row_set(r, RL_FAIL, "harness fault: %s", leg.err); rc = 2;
        } else if (!leg.rewrote) {
            rl_row_set(r, RL_FAIL, "the probe never reached the frame it rewrites; "
                                   "nothing was measured");
            rc = 1;
        } else if (leg.paired) {
            rl_row_set(r, RL_FAIL, "⛔ a pairing was established over a frame this "
                                   "probe deliberately corrupted");
            rc = 1;
        } else if (leg.saw_abort && leg.peer_abort_rc == want) {
            rl_row_set(r, RL_PASS, "the peer aborted with `%s`", rl_reason_name(want));
        } else if (leg.saw_abort) {
            rl_row_set(r, RL_FAIL, "the peer aborted with `%s`, expected `%s`",
                       rl_reason_name(leg.peer_abort_rc), rl_reason_name(want));
            rc = 1;
        } else {
            rl_row_set(r, RL_FAIL, "no bs_abort arrived; the peer neither paired "
                                   "nor refused");
            rc = 1;
        }
        fprintf(stderr, "%s: %s\n", r->id, r->reason);
        rl_leg_finish(&leg);
        return rc;
    }

    fprintf(stderr, "unknown probe: %s\n", what);
    return 2;
}
