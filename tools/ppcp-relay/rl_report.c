/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * rl_report.c — the rows a relay run produces.
 *
 * ⛔ THE FORMAT IS `ppcp-conform`'s AND NOT A SECOND ONE.  L21 says the relay
 * "emits matrix rows in the format ppcp-conform already uses, and lives
 * beside it so both teams run THE SAME RELAY rather than two" — so the JSON
 * carries the same keys and the Markdown is the matrix's own row shape with
 * the three implementation columns replaced by the single column this run
 * measured.  Merging it is the orchestrator's; inventing the other two
 * columns would not be.
 *
 * ⛔ AND ONE ROW IS NEVER EMITTED FROM HERE.  There is no RV-6 aggregate and
 * there is no code path that could produce one (9g, CA7, ground rule 11).
 * RT-20c is not this tool's to declare either: it needs BOTH shipping
 * implementations either side of this relay, and a relay that reported it
 * from one leg would be reporting the exact false green RT-20 exists to
 * prevent.
 */
#include "relay.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

const char *rl_verdict_name(rl_verdict v)
{
    switch (v) {
    case RL_PASS: return "pass";
    case RL_FAIL: return "fail";
    default:      return "unrun";
    }
}

/* The cell vocabulary of plan §8, and `ppcp-conform`'s own reading of it: a
 * FAILED row is `impl` — "code exists; the test has not been run or does not
 * pass" — which is exactly what a failing row means. */
const char *rl_verdict_cell(rl_verdict v)
{
    switch (v) {
    case RL_PASS: return "pass";
    case RL_FAIL: return "impl";
    default:      return "unrun";
    }
}

rl_row *rl_row_add(rl_report *rep, const char *id, const char *invariant,
                   const char *method, const char *asserts)
{
    rl_row *r;
    if (rep->count >= RL_MAX_ROWS)
        return NULL;
    r = &rep->rows[rep->count++];
    memset(r, 0, sizeof(*r));
    r->id        = id;
    r->invariant = invariant;
    r->profile   = "RV-6";
    r->method    = method;
    r->asserts   = asserts;
    r->verdict   = RL_UNRUN;
    snprintf(r->reason, sizeof(r->reason), "not run");
    return r;
}

void rl_row_set(rl_row *r, rl_verdict v, const char *fmt, ...)
{
    va_list ap;
    if (r == NULL)
        return;
    r->verdict = v;
    va_start(ap, fmt);
    vsnprintf(r->reason, sizeof(r->reason), fmt, ap);
    va_end(ap);
}

static void json_string(FILE *f, const char *s)
{
    fputc('"', f);
    for (; s != NULL && *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\')      fprintf(f, "\\%c", c);
        else if (c == '\n')             fputs("\\n", f);
        else if (c == '\r')             fputs("\\r", f);
        else if (c == '\t')             fputs("\\t", f);
        else if (c < 0x20)              fprintf(f, "\\u%04x", c);
        else                            fputc((int)c, f);
    }
    fputc('"', f);
}

bool rl_write_json(const rl_report *rep, const char *path)
{
    FILE  *f = (path == NULL || strcmp(path, "-") == 0) ? stdout : fopen(path, "w");
    size_t i, pass = 0, failn = 0, unrun = 0;

    if (f == NULL)
        return false;
    for (i = 0; i < rep->count; i++) {
        switch (rep->rows[i].verdict) {
        case RL_PASS: pass++;  break;
        case RL_FAIL: failn++; break;
        default:      unrun++; break;
        }
    }

    fputs("{\n", f);
    fputs("  \"tool\": \"ppcp-relay\",\n", f);
    fputs("  \"spec\": \"PPCP-RV 1.0 §11, revision 9 as amended by E30–E55\",\n", f);
    fputs("  \"conf\": \"PPCP-RV 1.0 §9 (RT-20b)\",\n", f);
    fprintf(f, "  \"generated_unix_s\": %lld,\n", (long long)time(NULL));
    fputs("  \"column\": ", f); json_string(f, rep->column ? rep->column : "");
    fputs(",\n  \"mode\": ", f); json_string(f, rep->mode ? rep->mode : "");
    /* ⛔ Stated in the artefact, not only in the prose, because an aggregate
     * is what a reader of a green report reaches for. */
    fputs(",\n  \"rv6_aggregate\": null,\n", f);
    fputs("  \"rv6_aggregate_note\": \"9g: no aggregate pass for RV-6 is "
          "reported while RT-20c is unrun, and RT-20c is unrun. RT-20b "
          "passing is not a claim that §11.8's property holds between two "
          "implementations.\",\n", f);
    fputs("  \"rows\": [\n", f);
    for (i = 0; i < rep->count; i++) {
        const rl_row *r = &rep->rows[i];
        fputs("    {", f);
        fputs("\"id\": ", f);           json_string(f, r->id);
        fputs(", \"invariant\": ", f);  json_string(f, r->invariant);
        fputs(", \"profile\": ", f);    json_string(f, r->profile);
        fputs(", \"method\": ", f);     json_string(f, r->method);
        fputs(", \"verdict\": ", f);    json_string(f, rl_verdict_name(r->verdict));
        fputs(", \"cell\": ", f);       json_string(f, rl_verdict_cell(r->verdict));
        fputs(", \"asserts\": ", f);    json_string(f, r->asserts);
        fputs(", \"command\": ", f);    json_string(f, r->command);
        fputs(", \"reason\": ", f);     json_string(f, r->reason);
        fputs(i + 1 == rep->count ? "}\n" : "},\n", f);
    }
    fputs("  ],\n", f);
    fprintf(f, "  \"summary\": {\"pass\": %zu, \"fail\": %zu, \"unrun\": %zu}\n}\n",
            pass, failn, unrun);
    if (f != stdout)
        fclose(f);
    return true;
}

bool rl_write_markdown(const rl_report *rep, const char *path)
{
    FILE     *f = (path == NULL || strcmp(path, "-") == 0) ? stdout : fopen(path, "w");
    size_t    i;
    char      date[32];
    time_t    t = time(NULL);
    struct tm tmv;

    if (f == NULL)
        return false;
    gmtime_r(&t, &tmv);
    strftime(date, sizeof(date), "%Y-%m-%d", &tmv);

    fprintf(f, "<!-- generated by ppcp-relay, %s — do not edit by hand -->\n", date);
    fprintf(f, "<!-- mode: %s -->\n\n", rep->mode ? rep->mode : "");
    fprintf(f, "| Test | Invariant | Profile | Method | %s |\n",
            rep->column != NULL ? rep->column : "result");
    fputs("|---|---|---|---|---|\n", f);
    for (i = 0; i < rep->count; i++) {
        const rl_row *r = &rep->rows[i];
        fprintf(f, "| %s | %s | %s | %s | %s |\n", r->id, r->invariant,
                r->profile, r->method, rl_verdict_cell(r->verdict));
    }
    fputs("\n⛔ **No RV-6 aggregate is reported here and none may be derived "
          "from these rows** ([9g](../specification/ppcp-rv.md#9-conformance)). "
          "RT-20c needs both shipping implementations either side of this "
          "relay and is unrun.\n", f);
    if (f != stdout)
        fclose(f);
    return true;
}
