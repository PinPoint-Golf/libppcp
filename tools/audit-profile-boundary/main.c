/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * audit-profile-boundary — PPCP-CONF §5b1, run as a test.  Work package L16.
 *
 * 5b1 requires, before `ppcp/1.0` freezes, that **every clause requiring a peer
 * to ORIGINATE a message is bound to a profile that confers that message**.  The
 * failure it exists to catch is not exotic: a MUST is written in one section
 * ("a capture peer MUST send `shot_link` when …") while the message catalogue
 * binds that message to a profile the clause's subject need not declare.  Every
 * peer that obeys the MUST then violates I24, and every peer that obeys I24
 * violates the MUST.  Neither implementation notices, because each reads only
 * one of the two documents at a time.
 *
 * So this reads BOTH and compares them:
 *
 *   1. the catalogue in `PPCP-MSG` §11 — the specification's own table;
 *   2. the catalogue in `src/ppcp_message.c` — the implementation's;
 *   3. every normative clause in every specification document that requires
 *      originating a named message.
 *
 * and asserts (1) and (2) agree message for message, profile for profile,
 * clause for clause, and that every message (3) requires anyone to originate is
 * one the catalogue confers through a profile.
 *
 * WHAT IT DELIBERATELY DOES NOT DO.  It does not try to infer which profile a
 * clause's SUBJECT holds — "a capture peer", "a host", "an arbitrating peer"
 * are prose and inferring a profile set from prose would be a source of false
 * confidence rather than of evidence.  What it can decide exactly, it decides;
 * what it cannot, it PRINTS, and the printed list is the input to the human
 * sweep of 5b2 rather than a substitute for it.
 *
 * Usage:  audit-profile-boundary SPEC_DIR SRC_DIR
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MSGS   64
#define MAX_NAME   64
#define MAX_LINE   4096
#define MAX_ORIG  512

typedef struct entry {
    char name[MAX_NAME];
    char profile[MAX_NAME];   /* "Core", "Capture", … or "-" for none */
    char clause[16];
    bool in_spec;
    bool in_code;
    bool originated_by_a_must;
    bool marked_opt;          /* MSG §11's "Required by" column says **opt** */
    bool has_required_by;
} entry;

static entry  g_msg[MAX_MSGS];
static size_t g_msg_count;
static int    g_failures;

static entry *find(const char *name)
{
    size_t i;
    for (i = 0; i < g_msg_count; i++)
        if (strcmp(g_msg[i].name, name) == 0)
            return &g_msg[i];
    return NULL;
}

static entry *intern(const char *name)
{
    entry *e = find(name);
    if (e != NULL)
        return e;
    if (g_msg_count == MAX_MSGS)
        return NULL;
    e = &g_msg[g_msg_count++];
    memset(e, 0, sizeof(*e));
    snprintf(e->name, sizeof(e->name), "%s", name);
    snprintf(e->profile, sizeof(e->profile), "-");
    return e;
}

static void fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("FAIL  ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    g_failures++;
}

/* The catalogue is Markdown, so a profile column may be bolded and a class
 * column may be `R/S/E`.  Emphasis is presentation and must not become a
 * difference between two tables that say the same thing. */
static void strip_emphasis(char *s)
{
    size_t n;
    while (s[0] == '*' || s[0] == '_')
        memmove(s, s + 1, strlen(s));
    n = strlen(s);
    while (n > 0 && (s[n-1] == '*' || s[n-1] == '_'))
        s[--n] = '\0';
}

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n' ||
                     s[n - 1] == '\r'))
        s[--n] = '\0';
    {
        char  *p = s;
        while (*p == ' ' || *p == '\t')
            p++;
        if (p != s)
            memmove(s, p, strlen(p) + 1);
    }
}

static FILE *open_in(const char *dir, const char *name)
{
    char path[1024];
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "audit-profile-boundary: cannot read %s\n", path);
        exit(2);
    }
    return f;
}

/* -------------------------------------------- (1) the specification's table */

static void read_spec_catalogue(const char *spec_dir)
{
    FILE *f = open_in(spec_dir, "ppcp-messages.md");
    char  line[MAX_LINE];
    size_t rows = 0;

    while (fgets(line, sizeof(line), f) != NULL) {
        char *fields[8];
        size_t n = 0;
        char *p, *save = NULL;
        char  name[MAX_NAME];
        entry *e;

        if (strncmp(line, "| `", 3) != 0)
            continue;
        for (p = strtok_r(line, "|", &save); p != NULL && n < 8;
             p = strtok_r(NULL, "|", &save))
            fields[n++] = p;
        if (n < 5)
            continue;
        trim(fields[0]);
        trim(fields[3]);
        trim(fields[4]);
        /* The catalogue's rows are exactly `name` | class | channel | profile |
         * clause; other backticked tables in the document have different
         * shapes and are skipped by the class column's one-letter form. */
        {
            char *cls = fields[1];
            trim(cls);
            strip_emphasis(cls);
            /* `R`, `S`, `E` — and `R/S/E` for `error`, which is all three. */
            if (strcmp(cls, "R") != 0 && strcmp(cls, "S") != 0 &&
                strcmp(cls, "E") != 0 && strcmp(cls, "R/S/E") != 0)
                continue;
        }
        if (fields[0][0] != '`')
            continue;
        snprintf(name, sizeof(name), "%s", fields[0] + 1);
        {
            char *tick = strchr(name, '`');
            if (tick == NULL)
                continue;
            *tick = '\0';
        }
        e = intern(name);
        if (e == NULL) { fail("more than %d messages in the catalogue", MAX_MSGS); break; }
        e->in_spec = true;
        /* An em dash means no profile confers it — link_bind, hello,
         * hello_accept, error.  Normalised to "-" so the two tables compare. */
        strip_emphasis(fields[3]);
        if (strcmp(fields[3], "\xe2\x80\x94") == 0 || strcmp(fields[3], "-") == 0)
            snprintf(e->profile, sizeof(e->profile), "-");
        else
            snprintf(e->profile, sizeof(e->profile), "%s", fields[3]);
        /* Erratum E18 added a "Required by" column, so the section link is the
         * LAST field rather than the fifth.  Taking it from the end keeps this
         * working the next time a column is added — and the clause comparison
         * silently stopped running when the column landed, which is the failure
         * mode a gate must not have. */
        if (n >= 6) {
            char *req = fields[4];
            trim(req);
            strip_emphasis(req);
            e->has_required_by = true;
            e->marked_opt = (strcmp(req, "opt") == 0);
        }
        /* `[4.3](#43-session_resume)` -> `4.3` */
        {
            char *open = strchr(fields[n - 1], '[');
            char *close = (open != NULL) ? strchr(open, ']') : NULL;
            if (open != NULL && close != NULL) {
                size_t len = (size_t)(close - open - 1);
                if (len >= sizeof(e->clause)) len = sizeof(e->clause) - 1;
                memcpy(e->clause, open + 1, len);
                e->clause[len] = '\0';
            }
        }
        rows++;
    }
    fclose(f);
    printf("  spec catalogue: %zu messages\n", rows);
    if (rows < 40)
        fail("only %zu rows parsed from PPCP-MSG §11; the table's shape moved", rows);
}

/* ------------------------------------------- (2) the implementation's table */

static const struct { const char *macro; const char *name; } profile_names[] = {
    { "PPCP_PROFILE_CORE",      "Core"      },
    { "PPCP_PROFILE_CAPTURE",   "Capture"   },
    { "PPCP_PROFILE_DETECT",    "Detect"    },
    { "PPCP_PROFILE_MINT",      "Mint"      },
    { "PPCP_PROFILE_ARBITRATE", "Arbitrate" },
    { "PPCP_PROFILE_LIVE",      "Live"      },
    { "PPCP_PROFILE_OFFLINE",   "Offline"   },
    { "PPCP_PROFILE_MARKUP",    "Markup"    },
    { NULL, NULL }
};

static void read_code_catalogue(const char *src_dir)
{
    FILE  *f = open_in(src_dir, "ppcp_message.c");
    char   line[MAX_LINE];
    size_t rows = 0;
    bool   in_table = false;

    while (fgets(line, sizeof(line), f) != NULL) {
        char  name[MAX_NAME];
        char *q, *end;
        entry *e;
        size_t i;

        if (strstr(line, "msg_table[PPCP_MSG_COUNT]") != NULL) { in_table = true; continue; }
        if (!in_table)
            continue;
        if (line[0] == '}')
            break;
        q = strchr(line, '"');
        if (q == NULL || strncmp(line, " { \"", 4) != 0)
            continue;
        end = strchr(q + 1, '"');
        if (end == NULL)
            continue;
        {
            size_t len = (size_t)(end - q - 1);
            if (len >= sizeof(name)) len = sizeof(name) - 1;
            memcpy(name, q + 1, len);
            name[len] = '\0';
        }
        e = intern(name);
        if (e == NULL) { fail("more than %d messages in the code table", MAX_MSGS); break; }
        e->in_code = true;
        rows++;

        /* profile */
        {
            const char *got = "-";
            for (i = 0; profile_names[i].macro != NULL; i++)
                if (strstr(line, profile_names[i].macro) != NULL) {
                    got = profile_names[i].name;
                    break;
                }
            if (e->in_spec) {
                /* A row may name a SET — `shot` is "Mint / Arbitrate", because
                 * 7.2a and 7.2b give the same message to two profiles for two
                 * different originators.  The engine carries one
                 * `originating_profile` per message, so the check is
                 * membership: the code's binding must be one the specification
                 * lists, and a code binding the specification does not list at
                 * all is the boundary violation 5b1 is about. */
                if (strstr(e->profile, got) == NULL)
                    fail("`%s`: PPCP-MSG §11 binds it to %s, src/ppcp_message.c to %s",
                         name, e->profile, got);
            } else {
                snprintf(e->profile, sizeof(e->profile), "%s", got);
            }
        }
        /* clause: the last quoted string on the line */
        {
            char *c = end + 1, *last = NULL, *lend = NULL;
            while ((c = strchr(c, '"')) != NULL) {
                char *ce = strchr(c + 1, '"');
                if (ce == NULL) break;
                last = c; lend = ce;
                c = ce + 1;
            }
            if (last != NULL && e->in_spec && e->clause[0] != '\0') {
                char got[16];
                size_t len = (size_t)(lend - last - 1);
                if (len >= sizeof(got)) len = sizeof(got) - 1;
                memcpy(got, last + 1, len);
                got[len] = '\0';
                if (strcmp(got, e->clause) != 0)
                    fail("`%s`: PPCP-MSG §11 cites clause %s, src/ppcp_message.c cites %s",
                         name, e->clause, got);
            }
        }
    }
    fclose(f);
    printf("  code catalogue: %zu messages\n", rows);
}

/* -------------------------------- (3) clauses that require ORIGINATING one */

static const char *const origination_verbs[] = {
    " sends ", " send ", " emits ", " emit ", " originates ", " originate ",
    " issues ", " issue ", " answers ", " answer ", " replies ", " reply ",
    " reports ", " report ", " records ", " record ", " publishes ", " publish ",
    " announces ", " announce ", NULL
};

/* ⚠ The verbs are matched with their surrounding spaces, so markdown emphasis
 * hides them: `a peer **emits** \`discontinuity\`` contains " **emits** " and not
 * " emits ".  That is not a hypothetical — it happened while erratum E18 was
 * being written, and a freeze gate that a bold verb defeats is not a gate.  So
 * every `*` and `_` is dropped before the line is read. */
static void flatten_emphasis(const char *in, char *out, size_t cap)
{
    size_t i = 0;
    for (; *in != '\0' && i + 1 < cap; in++)
        if (*in != '*' && *in != '_')
            out[i++] = *in;
    out[i] = '\0';
}

static bool has_origination_verb(const char *line)
{
    size_t i;
    for (i = 0; origination_verbs[i] != NULL; i++)
        if (strstr(line, origination_verbs[i]) != NULL)
            return true;
    return false;
}

static void scan_clauses(const char *spec_dir, const char *file)
{
    FILE *f = open_in(spec_dir, file);
    char  line[MAX_LINE];

    while (fgets(line, sizeof(line), f) != NULL) {
        const char *p;
        static char flat[MAX_LINE];
        bool must, must_not;

        flatten_emphasis(line, flat, sizeof(flat));
        must     = strstr(flat, "MUST") != NULL;
        must_not = strstr(flat, "MUST NOT") != NULL;

        if (!must || must_not)
            continue;                       /* a prohibition confers nothing */
        if (!has_origination_verb(flat))
            continue;

        /* Every backticked token that names a catalogued message. */
        for (p = line; (p = strchr(p, '`')) != NULL; ) {
            const char *e2 = strchr(p + 1, '`');
            char        tok[MAX_NAME];
            size_t      len;
            entry      *m;
            if (e2 == NULL)
                break;
            len = (size_t)(e2 - p - 1);
            p = e2 + 1;
            if (len == 0 || len >= sizeof(tok))
                continue;
            memcpy(tok, e2 - len, len);
            tok[len] = '\0';
            m = find(tok);
            if (m == NULL)
                continue;
            m->originated_by_a_must = true;
            /* 5b1: the clause requires originating it, so a profile must
             * confer it.  The four that no profile confers are the transport
             * and error paths, which no peer needs a declaration to use — a
             * MUST requiring one of those is fine and is not a boundary
             * violation.  Anything else with no profile IS one. */
        }
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *spec_dir = (argc > 1) ? argv[1] : "docs/specification";
    const char *src_dir  = (argc > 2) ? argv[2] : "src";
    size_t i, unconferred = 0, never_required = 0;

    printf("audit-profile-boundary — PPCP-CONF 5b1\n");
    read_spec_catalogue(spec_dir);
    read_code_catalogue(src_dir);

    for (i = 0; i < g_msg_count; i++) {
        if (g_msg[i].in_spec && !g_msg[i].in_code)
            fail("`%s` is in PPCP-MSG §11 and not in src/ppcp_message.c", g_msg[i].name);
        if (g_msg[i].in_code && !g_msg[i].in_spec)
            fail("`%s` is in src/ppcp_message.c and not in PPCP-MSG §11", g_msg[i].name);
    }

    scan_clauses(spec_dir, "ppcp-core.md");
    scan_clauses(spec_dir, "ppcp-messages.md");
    scan_clauses(spec_dir, "ppcp-encoding.md");
    scan_clauses(spec_dir, "ppcp-rv.md");

    /* 5b1 proper.  A message a MUST requires someone to originate, that no
     * profile confers, is the boundary violation — except the four that
     * precede or survive declaration, which is why they carry no profile in
     * the first place. */
    for (i = 0; i < g_msg_count; i++) {
        entry *e = &g_msg[i];
        bool   pre_declaration =
            strcmp(e->name, "link_bind") == 0 || strcmp(e->name, "hello") == 0 ||
            strcmp(e->name, "hello_accept") == 0 || strcmp(e->name, "error") == 0;
        if (e->originated_by_a_must && strcmp(e->profile, "-") == 0 && !pre_declaration) {
            fail("a MUST requires originating `%s` and no profile confers it (5b1)",
                 e->name);
            unconferred++;
        }
        if (!e->originated_by_a_must)
            never_required++;
        /* CONF 5b2, erratum E18.  §11's "Required by" column is the sweep's
         * recorded answer for every message; this asserts the answer still
         * matches what the documents say.  A message marked **opt** that some
         * clause now requires, or one left with a clause reference that no
         * clause requires any more, is a sweep that has gone stale — which is
         * the only way the 27 could quietly become 28 again. */
        if (e->has_required_by && e->in_spec) {
            if (e->marked_opt && e->originated_by_a_must)
                fail("`%s` is marked **opt** in PPCP-MSG §11 and a MUST now requires "
                     "originating it (5b2)", e->name);
            if (!e->marked_opt && !e->originated_by_a_must)
                fail("PPCP-MSG §11 cites a clause requiring `%s` and no MUST in the set "
                     "does (5b2)", e->name);
        }
    }

    if (never_required > 0) {
        printf("\n  %zu catalogued messages are required by no MUST in any document.\n"
               "  That is not a failure — an OPTIONAL message is a real thing — but it\n"
               "  is the list a 5b2 sweep starts from, because a message nothing\n"
               "  requires is a message nothing tests:\n", never_required);
        for (i = 0; i < g_msg_count; i++)
            if (!g_msg[i].originated_by_a_must)
                printf("    %-22s %s\n", g_msg[i].name, g_msg[i].profile);
    }

    printf("\n  %zu messages, %zu profile bindings checked against both tables, "
           "%zu unconferred origination MUSTs\n",
           g_msg_count, g_msg_count, unconferred);
    if (g_failures != 0) {
        fprintf(stderr, "\naudit-profile-boundary: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("  PPCP-CONF 5b1: the profile boundary holds.\n");
    return 0;
}
