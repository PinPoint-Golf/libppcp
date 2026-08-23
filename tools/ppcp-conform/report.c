/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * report.c — the two artefacts a conformance run produces.
 *
 * JSON, because plan §8 says the orchestrator updates the matrix "from the JSON
 * emitted by ppcp-conform" and nobody marks a cell `pass` by hand.  Every row
 * carries the command that produced it, so ground rule 4's "a reproducible
 * command" is in the artefact rather than in a memory of one.
 *
 * Markdown, in the row format of `docs/conformance/matrix.md`, because the
 * matrix is a table with one column per implementation and a run can honestly
 * fill exactly one of them.  So the fragment is the matrix's row shape with the
 * three implementation columns replaced by the single column this run measured,
 * named in the header.  Merging it is the orchestrator's; inventing the other
 * two columns would not be.
 */
#include "conform.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

const char *cf_verdict_name(cf_verdict v)
{
    switch (v) {
    case CF_PASS:    return "pass";
    case CF_FAIL:    return "fail";
    case CF_NA:      return "n/a";
    default:         return "skipped";
    }
}

/* The cell vocabulary of plan §8.  A FAILED row is `impl` and not `fail`: the
 * matrix says "code exists; the test has not been run or does not pass", which
 * is exactly what a failing row means and is the vocabulary the document
 * already has.  A row that did not apply is `—`. */
const char *cf_verdict_cell(cf_verdict v)
{
    switch (v) {
    case CF_PASS:    return "pass";
    case CF_FAIL:    return "impl";
    case CF_NA:      return "n/a";
    default:         return "—";
    }
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

bool cf_write_json(const cf_opts *o, const cf_result *res, size_t count,
                   int exit_code, const char *path)
{
    FILE  *f = (path == NULL || strcmp(path, "-") == 0) ? stdout : fopen(path, "w");
    size_t i;
    size_t pass = 0, fail = 0, na = 0, skipped = 0;

    if (f == NULL)
        return false;
    for (i = 0; i < count; i++) {
        switch (res[i].verdict) {
        case CF_PASS: pass++; break;
        case CF_FAIL: fail++; break;
        case CF_NA:   na++;   break;
        default:      skipped++; break;
        }
    }

    fputs("{\n", f);
    fputs("  \"tool\": \"ppcp-conform\",\n", f);
    fputs("  \"spec\": \"ppcp/1.0\",\n", f);
    fputs("  \"conf\": \"PPCP-CONF 1.0 §1, §3, §4\",\n", f);
    fprintf(f, "  \"generated_unix_s\": %lld,\n", (long long)time(NULL));
    fputs("  \"column\": ", f);         json_string(f, o->column ? o->column : "");
    fputs(",\n  \"role\": ", f);        json_string(f, o->role ? o->role : "");
    fputs(",\n  \"transport\": ", f);
    json_string(f, o->self ? "self (ppcp-sim stand-in over loopback)"
                           : (o->connect_host != NULL ? "dialled" : "listened"));
    fputs(",\n  \"profiles\": [", f);
    for (i = 0; i < o->profile_count; i++) {
        if (i) fputs(", ", f);
        json_string(f, o->profiles[i]);
    }
    fputs("],\n  \"rows\": [\n", f);
    for (i = 0; i < count; i++) {
        const cf_row *r = res[i].row;
        fputs("    {", f);
        fputs("\"id\": ", f);           json_string(f, r->id);
        fputs(", \"invariant\": ", f);  json_string(f, r->invariant);
        fputs(", \"profile\": ", f);    json_string(f, r->profile);
        fputs(", \"method\": ", f);     json_string(f, r->method);
        fprintf(f, ", \"kind\": \"%s\"",
                r->kind == CF_NEGATIVE ? "negative" : "positive");
        fputs(", \"verdict\": ", f);    json_string(f, cf_verdict_name(res[i].verdict));
        fputs(", \"cell\": ", f);       json_string(f, cf_verdict_cell(res[i].verdict));
        fprintf(f, ", \"exit\": %d, \"ms\": %lld", res[i].exit_code,
                (long long)res[i].ms);
        fputs(", \"declaration\": ", f); json_string(f, r->declaration);
        fputs(", \"scenario\": ", f);    json_string(f, r->scenario);
        fputs(", \"expect\": ", f);      json_string(f, r->expect ? r->expect : "");
        fputs(", \"asserts\": ", f);     json_string(f, r->asserts);
        fputs(", \"command\": ", f);     json_string(f, res[i].command);
        fputs(", \"reason\": ", f);      json_string(f, res[i].reason);
        fputs(i + 1 == count ? "}\n" : "},\n", f);
    }
    fputs("  ],\n", f);
    fprintf(f, "  \"summary\": {\"pass\": %zu, \"fail\": %zu, \"n/a\": %zu, \"skipped\": %zu},\n",
            pass, fail, na, skipped);
    fprintf(f, "  \"exit\": %d\n}\n", exit_code);
    if (f != stdout)
        fclose(f);
    return true;
}

bool cf_write_markdown(const cf_opts *o, const cf_result *res, size_t count,
                       const char *path)
{
    FILE  *f = (path == NULL || strcmp(path, "-") == 0) ? stdout : fopen(path, "w");
    size_t i;
    char   date[32];
    time_t t = time(NULL);
    struct tm tmv;

    if (f == NULL)
        return false;
    gmtime_r(&t, &tmv);
    strftime(date, sizeof(date), "%Y-%m-%d", &tmv);

    fprintf(f, "<!-- generated by ppcp-conform, %s — do not edit by hand -->\n", date);
    fprintf(f, "<!-- profiles claimed:");
    for (i = 0; i < o->profile_count; i++)
        fprintf(f, " %s", o->profiles[i]);
    fprintf(f, " -->\n\n");
    fprintf(f, "| Test | Invariant | Profile | Method | %s |\n",
            o->column != NULL ? o->column : "result");
    fputs("|---|---|---|---|---|\n", f);
    for (i = 0; i < count; i++) {
        const cf_row *r = res[i].row;
        fprintf(f, "| %s | %s | %s | %s | %s |\n", r->id, r->invariant, r->profile,
                r->method, cf_verdict_cell(res[i].verdict));
    }
    fputs("\nEvery `pass` above came from a command; the commands are in the JSON "
          "beside this file, one per row, and each re-runs on its own.\n", f);
    if (f != stdout)
        fclose(f);
    return true;
}
