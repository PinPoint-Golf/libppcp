/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * main.c — ppcp-conform's command line.  Work package L14.
 *
 * EXIT CODES, because a harness reads them and a human reads the JSON:
 *
 *   0  every applicable row passed
 *   1  at least one applicable row failed
 *   2  the invocation was wrong — an unknown profile, no target, no `ppcp-sim`
 *   3  no row was applicable, which is not a pass: a claim naming profiles this
 *      tool has no row for has not been measured, and saying "0 failures" about
 *      an empty run is the failure mode this code exists to avoid
 */
#include "conform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PPCP_SIM_PATH
#define PPCP_SIM_PATH "ppcp-sim"
#endif
#ifndef PPCP_SCENARIO_DIR
#define PPCP_SCENARIO_DIR "tools/scenarios"
#endif

static const char *const known_profiles[] = {
    "core", "capture", "detect", "mint", "arbitrate", "live", "offline", "markup"
};

static void usage(void)
{
    fputs(
"ppcp-conform — drives a peer under test through PPCP-CONF §3 and §4's paired\n"
"and injected rows, using ppcp-sim as the counterpart, and reports a verdict.\n"
"\n"
"  ppcp-conform --profiles LIST --role host|capture|observer\n"
"               ( --connect HOST:PORT | --listen PORT | --self )\n"
"               [--json PATH] [--markdown PATH] [--column NAME]\n"
"               [--only ROW[,ROW...]] [--sim PATH] [--scenarios DIR]\n"
"               [--psk HEX [--psk-identity TEXT]] [--list] [--quiet] [--help]\n"
"\n"
"  --profiles   the claim (CONF 1a).  Comma-separated, from: core capture\n"
"               detect mint arbitrate live offline markup.  Rows for declared\n"
"               profiles run POSITIVE (1b); rows for undeclared ones run\n"
"               NEGATIVE (1d) — parsed and never originated.\n"
"  --role       the role of the PEER UNDER TEST.  The counterpart takes the\n"
"               complementary role the row names.\n"
"  --connect    the peer under test listens and this tool dials it.\n"
"  --listen     the peer under test dials and this tool listens.\n"
"  --self       the reference pairing: a second ppcp-sim stands in for the peer\n"
"               under test over loopback.  This is how libppcp fills its own\n"
"               matrix column with the same instrument the applications use.\n"
"  --column     the matrix column name the Markdown fragment fills.\n"
"  --list       print the row table and exit 0.\n"
"\n"
"Exit: 0 all applicable rows passed, 1 a row failed, 2 bad invocation,\n"
"      3 no row applied to this claim and role.\n", stderr);
}

static bool profile_known(const char *p)
{
    size_t i;
    for (i = 0; i < sizeof(known_profiles) / sizeof(known_profiles[0]); i++)
        if (strcmp(p, known_profiles[i]) == 0)
            return true;
    return false;
}

bool cf_declares(const cf_opts *o, const char *profile)
{
    size_t i;
    for (i = 0; i < o->profile_count; i++)
        if (strcmp(o->profiles[i], profile) == 0)
            return true;
    return false;
}

/* Comma-separated membership without copying: the row table's `profiles` field
 * is a literal and nothing here may modify it. */
static bool list_has(const char *list, const char *item)
{
    size_t n = strlen(item);
    const char *p = list;
    while (p != NULL && *p != '\0') {
        const char *e = strchr(p, ',');
        size_t      len = (e != NULL) ? (size_t)(e - p) : strlen(p);
        if (len == n && strncmp(p, item, n) == 0)
            return true;
        p = (e != NULL) ? e + 1 : NULL;
    }
    return false;
}

static bool row_needs_all(const cf_opts *o, const char *list)
{
    const char *p = list;
    while (p != NULL && *p != '\0') {
        const char *e = strchr(p, ',');
        size_t      len = (e != NULL) ? (size_t)(e - p) : strlen(p);
        char        buf[32];
        if (len >= sizeof(buf))
            return false;
        memcpy(buf, p, len);
        buf[len] = '\0';
        if (!cf_declares(o, buf))
            return false;
        p = (e != NULL) ? e + 1 : NULL;
    }
    return true;
}

static bool row_needs_none(const cf_opts *o, const char *list)
{
    const char *p = list;
    while (p != NULL && *p != '\0') {
        const char *e = strchr(p, ',');
        size_t      len = (e != NULL) ? (size_t)(e - p) : strlen(p);
        char        buf[32];
        if (len >= sizeof(buf))
            return true;
        memcpy(buf, p, len);
        buf[len] = '\0';
        if (cf_declares(o, buf))
            return false;
        p = (e != NULL) ? e + 1 : NULL;
    }
    return true;
}

bool cf_row_applies(const cf_opts *o, const cf_row *r, cf_verdict *out)
{
    if (out != NULL)
        *out = CF_SKIPPED;
    if (o->only != NULL && !list_has(o->only, r->id))
        return false;
    if (strcmp(r->put_role, "any") != 0 && strcmp(r->put_role, o->role) != 0)
        return false;
    if (r->kind == CF_POSITIVE)
        return row_needs_all(o, r->profiles);
    /* ⚠ --self CANNOT RUN A NEGATIVE ROW, and pretending otherwise would be the
     * worst kind of pass.  CONF 1d asks whether a peer that does NOT declare a
     * profile originates its messages; under --self the peer under test is a
     * `ppcp-sim` reading a declaration FILE, and that file's profile set is
     * whatever it is — not whatever --profiles says.  A negative row run that
     * way asserts nothing about the claim and can pass by luck, which is
     * exactly what happened the first time this was tried. */
    if (o->self)
        return false;
    /* CONF 1d: the negative row runs only where NONE of the profiles it names is
     * declared.  A claim that declares them has nothing to prove here and the
     * positive rows carry it instead. */
    return row_needs_none(o, r->profiles);
}

static void list_rows(void)
{
    size_t        n = 0, i;
    const cf_row *rows = cf_rows(&n);
    printf("%-10s %-9s %-10s %-8s %-8s %s\n",
           "row", "method", "profiles", "kind", "put", "counterpart");
    for (i = 0; i < n; i++)
        printf("%-10s %-9s %-10s %-8s %-8s %s / %s\n", rows[i].id, rows[i].method,
               rows[i].profiles, rows[i].kind == CF_NEGATIVE ? "negative" : "positive",
               rows[i].put_role, rows[i].declaration, rows[i].scenario);
}

int main(int argc, char **argv)
{
    cf_opts    o;
    cf_result  results[CF_MAX_ROWS];
    size_t     n = 0, i, used = 0;
    const cf_row *rows;
    int        exit_code = 0;
    bool       any_fail = false, any_ran = false;
    char       profile_buf[256];

    memset(&o, 0, sizeof(o));
    o.sim_path     = PPCP_SIM_PATH;
    o.scenario_dir = PPCP_SCENARIO_DIR;
    o.role         = "host";
    o.column       = "result";
    o.json_path    = NULL;
    o.markdown_path = NULL;

    for (i = 1; i < (size_t)argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < (size_t)argc) ? argv[i + 1] : NULL;
#define NEXT() do { if (v == NULL) { usage(); return 2; } i++; } while (0)
        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) { usage(); return 0; }
        else if (strcmp(a, "--list") == 0) { list_rows(); return 0; }
        else if (strcmp(a, "--quiet") == 0) o.quiet = true;
        else if (strcmp(a, "--self") == 0)  o.self = true;
        else if (strcmp(a, "--profiles") == 0) {
            char *p, *save = NULL;
            NEXT();
            snprintf(profile_buf, sizeof(profile_buf), "%s", v);
            for (p = strtok_r(profile_buf, ",", &save); p != NULL;
                 p = strtok_r(NULL, ",", &save)) {
                if (!profile_known(p)) {
                    fprintf(stderr, "ppcp-conform: `%s` is not a PPCP profile\n", p);
                    return 2;
                }
                if (o.profile_count == CF_MAX_PROFILES) return 2;
                o.profiles[o.profile_count++] = p;
            }
        }
        else if (strcmp(a, "--role") == 0)      { NEXT(); o.role = v; }
        else if (strcmp(a, "--column") == 0)    { NEXT(); o.column = v; }
        else if (strcmp(a, "--json") == 0)      { NEXT(); o.json_path = v; }
        else if (strcmp(a, "--markdown") == 0)  { NEXT(); o.markdown_path = v; }
        else if (strcmp(a, "--only") == 0)      { NEXT(); o.only = v; }
        else if (strcmp(a, "--sim") == 0)       { NEXT(); o.sim_path = v; }
        else if (strcmp(a, "--scenarios") == 0) { NEXT(); o.scenario_dir = v; }
        else if (strcmp(a, "--bundle") == 0)    { NEXT(); o.bundle = v; }
        else if (strcmp(a, "--psk") == 0)       { NEXT(); o.psk_hex = v; }
        else if (strcmp(a, "--psk-identity") == 0) { NEXT(); o.psk_identity = v; }
        else if (strcmp(a, "--listen") == 0)    { NEXT(); o.listen_port = atoi(v); }
        else if (strcmp(a, "--connect") == 0) {
            static char host[128];
            const char *colon;
            NEXT();
            colon = strrchr(v, ':');
            if (colon == NULL) { usage(); return 2; }
            snprintf(host, sizeof(host), "%.*s", (int)(colon - v), v);
            o.connect_host = host;
            o.connect_port = atoi(colon + 1);
        }
        else { fprintf(stderr, "ppcp-conform: unknown option `%s`\n", a); usage(); return 2; }
#undef NEXT
    }

    if (o.profile_count == 0) {
        fputs("ppcp-conform: --profiles is the claim, and a claim without a "
              "profile set is not a claim (CONF 1a)\n", stderr);
        return 2;
    }
    if (!cf_declares(&o, "core")) {
        fputs("ppcp-conform: every claim includes Core (CORE 2.2)\n", stderr);
        return 2;
    }
    if (!o.self && o.connect_host == NULL && o.listen_port == 0) {
        fputs("ppcp-conform: one of --connect, --listen or --self\n", stderr);
        return 2;
    }
    if (access(o.sim_path, X_OK) != 0) {
        fprintf(stderr, "ppcp-conform: no ppcp-sim at `%s` (use --sim)\n", o.sim_path);
        return 2;
    }

    rows = cf_rows(&n);
    for (i = 0; i < n && used < CF_MAX_ROWS; i++) {
        cf_verdict skip;
        if (!cf_row_applies(&o, &rows[i], &skip))
            continue;
        any_ran = true;
        if (!o.quiet)
            fprintf(stderr, "== %-10s %-9s %s\n", rows[i].id, rows[i].method,
                    rows[i].kind == CF_NEGATIVE ? "(negative)" : "");
        cf_run_row(&o, &rows[i], &results[used]);
        if (!o.quiet)
            fprintf(stderr, "   %-8s %s\n", cf_verdict_name(results[used].verdict),
                    results[used].reason);
        if (results[used].verdict == CF_FAIL)
            any_fail = true;
        used++;
    }

    if (!any_ran) {
        fputs("ppcp-conform: no row applied to this claim and role.  That is not "
              "a pass — nothing was measured.\n", stderr);
        exit_code = 3;
    } else {
        exit_code = any_fail ? 1 : 0;
    }

    if (o.json_path != NULL)
        (void)cf_write_json(&o, results, used, exit_code, o.json_path);
    if (o.markdown_path != NULL)
        (void)cf_write_markdown(&o, results, used, o.markdown_path);
    if (!o.quiet) {
        size_t pass = 0, fail = 0, na = 0;
        for (i = 0; i < used; i++) {
            if (results[i].verdict == CF_PASS) pass++;
            else if (results[i].verdict == CF_FAIL) fail++;
            else if (results[i].verdict == CF_NA) na++;
        }
        fprintf(stderr, "ppcp-conform: %zu pass, %zu fail, %zu n/a, of %zu rows run\n",
                pass, fail, na, used);
    }
    return exit_code;
}
