/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * main.c — the command line of `ppcp-sim`.  Work package L13.
 *
 *   ppcp-sim --role capture|host|observer
 *            --listen PORT | --connect HOST:PORT
 *            --declaration tools/scenarios/<file>.json
 *            --scenario <name>
 *            [--psk-ke-only] [--expect name=value] [--run-ms N]
 *            [--port-file PATH] [--log-prefix NAME] [--quiet]
 *
 * Exits 0 when the run completed and every expectation held; 1 with a one-line
 * reason on stderr on any protocol violation it observed, any unmet
 * expectation, or any transport failure.  Every frame is logged as one line on
 * stderr, which is what makes a failed interoperability run readable.
 */
#include "sim.h"
#include "sim_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *f)
{
    fprintf(f,
"ppcp-sim — the synthetic peer of PPCP-CONF 2c\n"
"\n"
"  ppcp-sim --role capture|host|observer\n"
"           --listen PORT | --connect HOST:PORT\n"
"           --declaration FILE.json --scenario NAME\n"
"           [--psk-ke-only] [--expect NAME=VALUE]... [--run-ms MS]\n"
"           [--port-file PATH] [--log-prefix NAME] [--quiet]\n"
"           [--list-scenarios] [--help]\n"
"\n"
"  --role            the role the declaration must state (5.2a: fixed for the\n"
"                    Session's lifetime, so it is checked and never imposed)\n"
"  --listen PORT     accept two TCP connections; PORT 0 picks a free one and\n"
"                    --port-file is where the chosen port is written\n"
"  --connect H:P     dial two TCP connections and mint the link_id (ENC 2.1a)\n"
"  --declaration     the Peer declaration to present, as JSON\n"
"  --scenario        what to do with it; --list-scenarios prints the table\n"
"  --psk-ke-only     TLS 1.3 offering psk_ke and nothing else, so a host's\n"
"                    refusal is demonstrated rather than asserted (RT-4).\n"
"                    Needs --connect; --psk HEX and --psk-identity name the key.\n"
"                    A REFUSED handshake exits 0: the refusal is the property.\n"
"  --expect N=V      assert a counter at exit; N>=V and N<=V also read.\n"
"                    Repeatable and comma-separable\n"
"  --run-ms          how long to run before closing (default 4000)\n");
}

static void list_scenarios(void)
{
    size_t i;
    printf("%-24s %-10s %s\n", "scenario", "role", "serves");
    printf("%-24s %-10s %s\n", "------------------------", "----------",
           "------------------------------------");
    for (i = 0; i < sim_scenario_count(); i++) {
        const sim_scenario *sc = sim_scenario_at(i);
        printf("%-24s %-10s %s\n", sc->name, sc->roles, sc->serves);
        printf("%-24s %-10s %s\n", "", "", sc->desc);
    }
}

static bool add_expect(sim_opts *o, const char *spec)
{
    char  buf[256];
    char *save = NULL;
    char *tok;

    snprintf(buf, sizeof(buf), "%s", spec);
    for (tok = strtok_r(buf, ",", &save); tok != NULL; tok = strtok_r(NULL, ",", &save)) {
        char   *eq = strchr(tok, '=');
        sim_cmp cmp = SIM_CMP_EQ;
        char   *name_end;

        if (eq == NULL) {
            fprintf(stderr, "ppcp-sim: --expect wants NAME=VALUE, NAME>=VALUE or "
                            "NAME<=VALUE, got `%s`\n", tok);
            return false;
        }
        if (o->expect_count >= SIM_MAX_EXPECT) {
            fprintf(stderr, "ppcp-sim: at most %d expectations\n", SIM_MAX_EXPECT);
            return false;
        }
        name_end = eq;
        if (eq > tok && (eq[-1] == '>' || eq[-1] == '<')) {
            cmp = (eq[-1] == '>') ? SIM_CMP_GE : SIM_CMP_LE;
            name_end = eq - 1;
        }
        *name_end = '\0';
        snprintf(o->expect[o->expect_count].name,
                 sizeof(o->expect[o->expect_count].name), "%s", tok);
        o->expect[o->expect_count].cmp   = cmp;
        o->expect[o->expect_count].value = (int64_t)strtoll(eq + 1, NULL, 10);
        o->expect_count++;
    }
    return true;
}

int main(int argc, char **argv)
{
    sim_opts            o;
    sim_decl            d;
    const sim_scenario *sc;
    char                err[SIM_ERR_LEN];
    char                hostbuf[256];
    int                 i;

    memset(&o, 0, sizeof(o));
    o.run_ms      = 4000;
    o.listen_port = -1;
    o.connect_port = -1;
    o.log_prefix  = "sim";

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
#define NEXT()  ((i + 1 < argc) ? argv[++i] : NULL)
        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(a, "--list-scenarios") == 0) {
            list_scenarios();
            return 0;
        } else if (strcmp(a, "--role") == 0) {
            o.role = NEXT();
        } else if (strcmp(a, "--declaration") == 0) {
            o.declaration = NEXT();
        } else if (strcmp(a, "--scenario") == 0) {
            o.scenario = NEXT();
        } else if (strcmp(a, "--listen") == 0) {
            const char *v = NEXT();
            o.listen_port = (v != NULL) ? atoi(v) : -1;
        } else if (strcmp(a, "--connect") == 0) {
            const char *v = NEXT();
            const char *colon;
            if (v == NULL || (colon = strrchr(v, ':')) == NULL) {
                fprintf(stderr, "ppcp-sim: --connect wants HOST:PORT\n");
                return 1;
            }
            snprintf(hostbuf, sizeof(hostbuf), "%.*s", (int)(colon - v), v);
            o.connect_host = hostbuf;
            o.connect_port = atoi(colon + 1);
        } else if (strcmp(a, "--port-file") == 0) {
            o.port_file = NEXT();
        } else if (strcmp(a, "--log-prefix") == 0) {
            o.log_prefix = NEXT();
        } else if (strcmp(a, "--run-ms") == 0) {
            const char *v = NEXT();
            o.run_ms = (v != NULL) ? (int64_t)strtoll(v, NULL, 10) : 4000;
        } else if (strcmp(a, "--psk-ke-only") == 0) {
            o.psk_ke_only = true;
        } else if (strcmp(a, "--psk") == 0) {
            o.psk_hex = NEXT();
        } else if (strcmp(a, "--psk-identity") == 0) {
            o.psk_identity = NEXT();
        } else if (strcmp(a, "--quiet") == 0) {
            o.quiet = true;
        } else if (strcmp(a, "--expect") == 0) {
            const char *v = NEXT();
            if (v == NULL || !add_expect(&o, v))
                return 1;
        } else {
            fprintf(stderr, "ppcp-sim: unknown option `%s`\n", a);
            usage(stderr);
            return 1;
        }
#undef NEXT
    }

    if (o.psk_ke_only) {
        if (!sim_tls_available()) {
            fprintf(stderr, "ppcp-sim: --psk-ke-only needs OpenSSL, which was not found "
                            "when this tool was configured\n");
            return 1;
        }
        if (o.connect_port < 0) {
            fprintf(stderr, "ppcp-sim: --psk-ke-only dials, so it wants --connect HOST:PORT\n");
            return 1;
        }
        return sim_run_psk_ke_only(&o);
    }

    if (o.declaration == NULL || o.scenario == NULL) {
        fprintf(stderr, "ppcp-sim: --declaration and --scenario are required\n");
        usage(stderr);
        return 1;
    }
    if ((o.listen_port < 0) == (o.connect_port < 0)) {
        fprintf(stderr, "ppcp-sim: exactly one of --listen and --connect\n");
        return 1;
    }
    sc = sim_scenario_find(o.scenario);
    if (sc == NULL) {
        fprintf(stderr, "ppcp-sim: unknown scenario `%s`; --list-scenarios prints them\n",
                o.scenario);
        return 1;
    }
    if (!sim_decl_load(&d, o.declaration, err, sizeof(err))) {
        fprintf(stderr, "ppcp-sim: %s\n", err);
        return 1;
    }
    if (o.role != NULL && strcmp(o.role, ppcp_role_str(d.role)) != 0) {
        /* 5.2a — a Peer's role is fixed for the Session's lifetime and belongs
         * to the declaration.  --role states what the caller expected; a
         * disagreement is a mistake, not something to reconcile silently. */
        fprintf(stderr, "ppcp-sim: --role %s but `%s` declares role %s\n",
                o.role, o.declaration, ppcp_role_str(d.role));
        return 1;
    }
    if (strcmp(sc->roles, "any") != 0 && strcmp(sc->roles, ppcp_role_str(d.role)) != 0) {
        fprintf(stderr, "ppcp-sim: scenario `%s` is written for role %s, and this "
                        "declaration is %s\n", sc->name, sc->roles, ppcp_role_str(d.role));
        return 1;
    }

    fprintf(stderr, "ppcp-sim: %s, role %s, scenario %s (%s)\n",
            d.peer_id, ppcp_role_str(d.role), sc->name, sc->serves);
    return sim_run(&o, &d, sc);
}
