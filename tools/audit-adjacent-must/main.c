/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * audit-adjacent-must — the checklist generator of PPCP-CONF §5b2.
 * Work package L16.
 *
 * WHY THIS EXISTS, in the specification's own words.  Revision 8 accepted a
 * process change after the FOURTH instance of the same defect: "a new MUST
 * contradicting one in an adjacent section".  Every instance was found by a
 * reviewer reading two sections at once, and every one was missed by everyone
 * reading them one at a time.  5b2 makes the sweep a required check before
 * `ppcp/1.0` freezes.  A sweep needs a checklist, and a checklist that a human
 * writes from memory is the thing that failed four times.
 *
 * So: this generates the checklist mechanically.  For every normative clause
 * in the specification set it records the clause id, its modal (MUST, MUST
 * NOT, SHOULD, MAY), the section it lives in, and its text; and it groups them
 * so that reading one group is reading everything that binds one subject at
 * once.  KEYED BY REVISION DIFF: given a list of changed clause ids — from
 * `git diff`, from a review, from a changelog — it emits only the groups those
 * clauses land in, which is the sweep 5b2 actually asks for.  Given none, it
 * emits the whole set, which is the freeze-time sweep.
 *
 * ⚠ IT DOES NOT DECIDE ANYTHING.  A contradiction between two MUSTs is a
 * question about meaning and this program cannot read.  What it removes is the
 * excuse: after this, nobody sweeps from memory.
 *
 * Usage:
 *   audit-adjacent-must SPEC_DIR [--changed 5.14g,7.3a,...] [--out PATH]
 *   audit-adjacent-must SPEC_DIR --changed-from FILE
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CLAUSES 4096
#define MAX_LINE    8192
#define MAX_ID        24
#define MAX_SECTION  192
#define MAX_TEXT     600

typedef struct clause {
    char id[MAX_ID];
    char modal[12];
    char section[MAX_SECTION];
    char doc[32];
    char text[MAX_TEXT];
} clause;

static clause g_c[MAX_CLAUSES];
static size_t g_n;

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n-1]=='\n' || s[n-1]=='\r' || s[n-1]==' ' || s[n-1]=='\t'))
        s[--n] = '\0';
}

/* The specification writes every normative clause as
 *     - **(5.14g) MUST** text…
 * which is the one convention the whole set keeps, and the reason a checklist
 * can be generated at all. */
static bool parse_clause(const char *line, clause *out)
{
    const char *p = strstr(line, "- **(");
    const char *close, *modal_end;
    size_t      len;

    if (p == NULL)
        return false;
    p += 5;
    close = strchr(p, ')');
    if (close == NULL)
        return false;
    len = (size_t)(close - p);
    if (len == 0 || len >= MAX_ID)
        return false;
    memcpy(out->id, p, len);
    out->id[len] = '\0';

    p = close + 1;
    while (*p == ' ')
        p++;
    modal_end = strstr(p, "**");
    if (modal_end == NULL)
        return false;
    len = (size_t)(modal_end - p);
    if (len >= sizeof(out->modal))
        len = sizeof(out->modal) - 1;
    memcpy(out->modal, p, len);
    out->modal[len] = '\0';
    trim(out->modal);

    p = modal_end + 2;
    while (*p == ' ')
        p++;
    snprintf(out->text, sizeof(out->text), "%s", p);
    trim(out->text);
    return out->modal[0] != '\0';
}

static void read_doc(const char *dir, const char *file, const char *label)
{
    char  path[1024];
    FILE *f;
    char  line[MAX_LINE];
    char  section[MAX_SECTION] = "(front matter)";

    snprintf(path, sizeof(path), "%s/%s", dir, file);
    f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "audit-adjacent-must: cannot read %s\n", path);
        exit(2);
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        if (line[0] == '#') {
            const char *p = line;
            while (*p == '#') p++;
            while (*p == ' ') p++;
            snprintf(section, sizeof(section), "%s", p);
            trim(section);
            continue;
        }
        if (g_n == MAX_CLAUSES)
            break;
        if (parse_clause(line, &g_c[g_n])) {
            snprintf(g_c[g_n].section, sizeof(g_c[g_n].section), "%s", section);
            snprintf(g_c[g_n].doc, sizeof(g_c[g_n].doc), "%s", label);
            g_n++;
        }
    }
    fclose(f);
}

static bool listed(const char *list, const char *id)
{
    const char *p = list;
    size_t      n = strlen(id);
    if (list == NULL || *list == '\0')
        return true;                  /* no filter: the freeze-time sweep */
    while (p != NULL && *p != '\0') {
        const char *e = strchr(p, ',');
        size_t      len = (e != NULL) ? (size_t)(e - p) : strlen(p);
        while (len > 0 && (*p == ' ' || *p == '\n')) { p++; len--; }
        while (len > 0 && (p[len-1] == ' ' || p[len-1] == '\n')) len--;
        if (len == n && strncmp(p, id, n) == 0)
            return true;
        p = (e != NULL) ? e + 1 : NULL;
    }
    return false;
}

static bool section_is_selected(const char *doc, const char *section, const char *changed)
{
    size_t i;
    if (changed == NULL || *changed == '\0')
        return true;
    for (i = 0; i < g_n; i++)
        if (strcmp(g_c[i].doc, doc) == 0 && strcmp(g_c[i].section, section) == 0 &&
            listed(changed, g_c[i].id))
            return true;
    return false;
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : "docs/specification";
    const char *changed = NULL;
    const char *out_path = NULL;
    static char changed_buf[8192];
    FILE       *out;
    size_t      i, j, groups = 0, must_count = 0;
    int         a;

    for (a = 2; a < argc; a++) {
        if (strcmp(argv[a], "--changed") == 0 && a + 1 < argc) {
            changed = argv[++a];
        } else if (strcmp(argv[a], "--changed-from") == 0 && a + 1 < argc) {
            FILE *cf = fopen(argv[++a], "r");
            size_t n = 0;
            if (cf == NULL) {
                fprintf(stderr, "audit-adjacent-must: cannot read %s\n", argv[a]);
                return 2;
            }
            n = fread(changed_buf, 1, sizeof(changed_buf) - 1, cf);
            changed_buf[n] = '\0';
            fclose(cf);
            for (j = 0; j < n; j++)
                if (changed_buf[j] == '\n')
                    changed_buf[j] = ',';
            changed = changed_buf;
        } else if (strcmp(argv[a], "--out") == 0 && a + 1 < argc) {
            out_path = argv[++a];
        } else {
            fprintf(stderr, "audit-adjacent-must: unknown option `%s`\n", argv[a]);
            return 2;
        }
    }

    read_doc(dir, "ppcp-core.md",        "CORE");
    read_doc(dir, "ppcp-messages.md",    "MSG");
    read_doc(dir, "ppcp-encoding.md",    "ENC");
    read_doc(dir, "ppcp-rv.md",          "RV");
    read_doc(dir, "ppcp-conformance.md", "CONF");

    if (g_n == 0) {
        fputs("audit-adjacent-must: no normative clauses parsed — the clause "
              "convention `- **(id) MODAL**` moved, and the sweep is blind\n", stderr);
        return 1;
    }

    out = (out_path == NULL) ? stdout : fopen(out_path, "w");
    if (out == NULL) {
        fprintf(stderr, "audit-adjacent-must: cannot write %s\n", out_path);
        return 2;
    }

    fputs("<!-- generated by tools/audit-adjacent-must — do not edit by hand -->\n\n", out);
    fputs("# The adjacent-MUST sweep — `PPCP-CONF` §5b2\n\n", out);
    if (changed != NULL && *changed != '\0')
        fprintf(out, "Keyed by revision diff. Changed clauses: `%s`.\n\n"
                     "Each section below contains at least one of them. **Read the whole "
                     "section**, not the changed clause: the four defects this check "
                     "exists for were each a new MUST contradicting an OLD one a few "
                     "lines away, and each was found by someone reading both at once.\n\n",
                changed);
    else
        fputs("The freeze-time sweep: every normative clause in the set, grouped by the "
              "section that binds it. A contradiction is a question about meaning and no "
              "program can decide it — what this removes is the excuse for sweeping from "
              "memory.\n\n", out);

    for (i = 0; i < g_n; i++) {
        bool first = true;
        /* One group per (document, section), emitted at its first clause. */
        for (j = 0; j < i; j++)
            if (strcmp(g_c[j].doc, g_c[i].doc) == 0 &&
                strcmp(g_c[j].section, g_c[i].section) == 0) {
                first = false;
                break;
            }
        if (!first)
            continue;
        if (!section_is_selected(g_c[i].doc, g_c[i].section, changed))
            continue;
        groups++;
        fprintf(out, "## `%s` — %s\n\n", g_c[i].doc, g_c[i].section);
        fputs("| | Clause | Modal | Text |\n|---|---|---|---|\n", out);
        for (j = 0; j < g_n; j++) {
            if (strcmp(g_c[j].doc, g_c[i].doc) != 0 ||
                strcmp(g_c[j].section, g_c[i].section) != 0)
                continue;
            fprintf(out, "| ☐ | **%s** | %s | %s |\n", g_c[j].id, g_c[j].modal,
                    g_c[j].text);
            if (strncmp(g_c[j].modal, "MUST", 4) == 0)
                must_count++;
        }
        fputc('\n', out);
    }

    fprintf(out, "---\n\n%zu sections, %zu normative clauses in the set, %zu of them "
                 "MUST or MUST NOT in the sections listed above.\n",
            groups, g_n, must_count);
    if (out != stdout)
        fclose(out);
    fprintf(stderr, "audit-adjacent-must: %zu clauses, %zu sections emitted\n", g_n, groups);
    return 0;
}
