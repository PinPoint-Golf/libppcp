/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * main.c — `ppcp-relay`, work package L21.
 */
#include "relay.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PPCP_AGREE_HELPER
#define PPCP_AGREE_HELPER "x25519-agree.sh"
#endif

static void usage(void)
{
    fputs(
"ppcp-relay — a deliberate man in the middle for PPCP-RV §11 (RT-20b, RT-20c)\n"
"\n"
"  Acceptor toward one peer, initiator toward the other, with its own fresh\n"
"  keypair on each leg.  It runs libppcp's own exchange engine twice, so the\n"
"  two halves it presents are the same code both applications embed.\n"
"\n"
"MODES\n"
"  --selftest                 ⛔ RT-20b(v): each of the relay's own legs\n"
"                             completes on demand, plus the interposition end\n"
"                             to end and both mirrors of 11.5c's ordering.\n"
"                             Needs no application.  RUN THIS FIRST — every\n"
"                             other row is worthless until it passes, because\n"
"                             a broken relay fails a peer in a way that looks\n"
"                             exactly like a defect in the peer.\n"
"  --listen P --connect H:P   the relay proper.  A peer dials P; the relay\n"
"                             dials H:P.  Both legs' digits are printed.\n"
"  --probe order-acceptor --connect H:P\n"
"                             11.5c against an ACCEPTOR: withhold bs_reveal\n"
"                             and check pk_a already arrived.\n"
"  --probe order-initiator --listen P\n"
"                             11.5b/11.5d against an INITIATOR: never reply\n"
"                             and check no pk_i follows.\n"
"  --probe decline --connect H:P\n"
"                             RT-20b(iii)/(iv): decline, then check the window\n"
"                             did not reopen.\n"
"  --peer initiator|acceptor  an HONEST stand-in.  ⛔ NOT a conformant peer:\n"
"                             it affirms its own comparison in software, which\n"
"                             is the one thing 11.1d forbids.  It claims\n"
"                             nothing and exists so the relay has a\n"
"                             counterpart before either application does.\n"
"\n"
"OPTIONS\n"
"  --listen PORT      --connect HOST:PORT     --v N (bootstrap version, 1..255)\n"
"  --helper PATH      the §11.11 agreement helper (default: installed beside\n"
"                     this binary).  ⛔ X25519 is NOT in libppcp and never will\n"
"                     be (ground rule 13, CA1): the private scalar lives only\n"
"                     in the helper and only `pk` and `Z` cross (11.11d).\n"
"  --observe-ms N     how long a probe watches for silence (default 3000)\n"
"  --json PATH        --markdown PATH    --column NAME\n"
"\n"
"⛔ THIS TOOL NEVER COMPARES DIGITS FOR ANYONE (11.1d, trap 8).  It prints its\n"
"  legs' digits so a PERSON can hold them against two screens.  And it emits\n"
"  no RV-6 aggregate: 9g forbids one while RT-20c is unrun, and RT-20c needs\n"
"  both shipping implementations either side of this relay.\n",
    stderr);
}

static bool split_hostport(char *s, const char **host, int *port)
{
    char *c = strrchr(s, ':');
    if (c == NULL)
        return false;
    *c    = '\0';
    *host = s;
    *port = atoi(c + 1);
    return *port > 0;
}

int main(int argc, char **argv)
{
    const char *helper     = PPCP_AGREE_HELPER;
    const char *host       = NULL;
    const char *json_path  = NULL;
    const char *md_path    = NULL;
    const char *mode       = NULL;
    const char *probe      = NULL;
    const char *peer_role  = NULL;
    rl_report   rep;
    int         port = 0, listen_port = 0, observe_ms = 3000;
    int         v = PPCP_BS_VERSION;
    int         i, rc = 0;

    memset(&rep, 0, sizeof(rep));
    rep.column = "libppcp";

    /* A helper that dies takes its pipe with it; SIGPIPE would take us. */
    signal(SIGPIPE, SIG_IGN);

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--selftest") == 0)             { mode = "selftest"; }
        else if (strcmp(a, "--listen") == 0 && i + 1 < argc)   listen_port = atoi(argv[++i]);
        else if (strcmp(a, "--connect") == 0 && i + 1 < argc) {
            if (!split_hostport(argv[++i], &host, &port)) {
                fprintf(stderr, "--connect wants HOST:PORT\n");
                return 2;
            }
        }
        else if (strcmp(a, "--probe") == 0 && i + 1 < argc)  { probe = argv[++i]; mode = "probe"; }
        else if (strcmp(a, "--peer") == 0 && i + 1 < argc)   { peer_role = argv[++i]; mode = "peer"; }
        else if (strcmp(a, "--helper") == 0 && i + 1 < argc)   helper = argv[++i];
        else if (strcmp(a, "--v") == 0 && i + 1 < argc)        v = atoi(argv[++i]);
        else if (strcmp(a, "--observe-ms") == 0 && i + 1 < argc) observe_ms = atoi(argv[++i]);
        else if (strcmp(a, "--json") == 0 && i + 1 < argc)      json_path = argv[++i];
        else if (strcmp(a, "--markdown") == 0 && i + 1 < argc)  md_path = argv[++i];
        else if (strcmp(a, "--column") == 0 && i + 1 < argc)    rep.column = argv[++i];
        else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) { usage(); return 0; }
        else {
            fprintf(stderr, "unknown argument: %s\n", a);
            usage();
            return 2;
        }
    }
    if (v < 1 || v > 255) {
        fprintf(stderr, "--v must be 1..255 (11.4b)\n");
        return 2;
    }
    if (mode == NULL && listen_port > 0 && host != NULL)
        mode = "relay";
    if (mode == NULL) {
        usage();
        return 2;
    }
    rep.mode = mode;

    if (strcmp(mode, "selftest") == 0) {
        rc = rl_selftest(helper, (uint8_t)v, &rep, false);
    } else if (strcmp(mode, "relay") == 0) {
        rc = rl_relay_main(listen_port, host, port, (uint8_t)v, helper, &rep);
    } else if (strcmp(mode, "probe") == 0) {
        rc = rl_probe_main(probe, listen_port, host, port, (uint8_t)v, helper,
                           observe_ms, &rep);
    } else if (strcmp(mode, "peer") == 0) {
        rl_leg_ctl ctl;
        memset(&ctl, 0, sizeof(ctl));
        ctl.exchange_timeout_ms = RL_TIMEOUT_EXCHANGE_MS;
        ctl.affirm_timeout_ms   = RL_TIMEOUT_AFFIRM_MS;
        if (peer_role == NULL) { usage(); return 2; }
        if (strcmp(peer_role, "initiator") == 0)
            rc = rl_peer_main(PPCP_BS_ROLE_INITIATOR, (uint8_t)v, host, port,
                              listen_port, helper, &ctl);
        else if (strcmp(peer_role, "acceptor") == 0)
            rc = rl_peer_main(PPCP_BS_ROLE_ACCEPTOR, (uint8_t)v, host, port,
                              listen_port, helper, &ctl);
        else { fprintf(stderr, "--peer wants initiator|acceptor\n"); return 2; }
    }

    if (rep.count > 0) {
        size_t k;
        fprintf(stderr, "\nrows:\n");
        for (k = 0; k < rep.count; k++)
            fprintf(stderr, "  %-24s %s\n", rep.rows[k].id,
                    rl_verdict_name(rep.rows[k].verdict));
        /* ⛔ 9g, printed every run, because an aggregate is what a reader of a
         * page of green reaches for. */
        fprintf(stderr, "\n⛔ No RV-6 aggregate is reported and none may be derived "
                        "from these rows (9g).\n   RT-20c needs BOTH shipping "
                        "implementations either side of this relay, and it is "
                        "unrun.\n");
    }
    if (json_path != NULL)
        (void)rl_write_json(&rep, json_path);
    if (md_path != NULL)
        (void)rl_write_markdown(&rep, md_path);
    return rc;
}
