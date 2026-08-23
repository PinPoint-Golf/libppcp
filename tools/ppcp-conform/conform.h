/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * conform.h — the conformance driver of PPCP-CONF §1, §3 and §4.
 * Work package L14.
 *
 * WHAT THIS IS.  A tool that drives a PEER UNDER TEST from outside, through its
 * real transport, using the synthetic peer of CONF §2c as the counterpart, and
 * emits a machine-readable verdict per conformance row.  It is what makes the
 * two applications testable by the same instrument the reference implementation
 * is tested by (plan A11): an application tested only by its own unit tests is
 * the single-implementation trap CONF §2c describes.
 *
 * WHY IT DRIVES `ppcp-sim` AS A PROCESS rather than linking its innards.  Three
 * reasons, and the third is the one that matters.  A row that crashes takes its
 * own process and not the run.  A row is then reproducible by hand — the JSON
 * carries the exact command line, which is what plan ground rule 4 means by "a
 * pass comes from a command that can be re-run".  And the counterpart the
 * applications already know how to run is `ppcp-sim`; a second, differently
 * built peer inside this binary would be a second implementation to keep
 * honest.
 *
 * WHAT IT DOES NOT DO.  It does not drive the peer under test.  A conformance
 * instrument that told the implementation what to send would be testing its own
 * script.  Every assertion here is made on the WIRE, by the counterpart, from
 * counters `ppcp-sim` maintains: a `t0` that moved, a message no declared
 * profile confers, a relation spanning two clocks of one peer.  A row that
 * cannot be observed from outside is not in this table — it is a `fixture` or a
 * `static` row and it belongs in the implementation's own suite (CONF §1's
 * method vocabulary, and L15's C tests).
 */
#ifndef PPCP_CONFORM_H
#define PPCP_CONFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CF_MAX_PROFILES 16
#define CF_MAX_ROWS     64
#define CF_REASON_LEN   256
#define CF_CMD_LEN      1024

/* CONF §1b and §1d are two different questions, so they are two kinds of row.
 *
 *   POSITIVE  the implementation DECLARES these profiles, so the behaviour they
 *             confer must be demonstrable.  1b.
 *   NEGATIVE  the implementation does NOT declare them, so it must parse the
 *             messages and never originate them.  1d — and it is the half an
 *             implementation talking to itself never checks. */
typedef enum cf_kind { CF_POSITIVE = 0, CF_NEGATIVE } cf_kind;

typedef enum cf_verdict {
    CF_PASS = 0,
    CF_FAIL,
    CF_NA,        /* an undeclared profile whose negative row passed (cell `n/a`) */
    CF_SKIPPED    /* not applicable to this role or this claim (cell `—`) */
} cf_verdict;

typedef struct cf_row {
    const char *id;           /* "CT-I7" — the matrix row this answers */
    const char *invariant;    /* the matrix's second column */
    const char *profile;      /* the matrix's third column, as the matrix spells it */
    const char *method;       /* CONF §1's method vocabulary */
    cf_kind     kind;
    /* Comma-separated.  POSITIVE: every one must be declared for the row to
     * run.  NEGATIVE: the row runs only where NONE of them is declared. */
    const char *profiles;
    /* The role the PEER UNDER TEST must hold for this row to mean anything —
     * "host", "capture", "observer", or "any". */
    const char *put_role;
    /* What the counterpart IS and what it DOES: the separation `ppcp-sim`'s
     * scenarios directory exists to keep (declaration in a file, behaviour in a
     * named scenario). */
    const char *sim_role;
    const char *declaration;  /* a file in the scenarios directory */
    const char *scenario;
    const char *expect;       /* the counterpart's --expect list, or NULL */
    /* For --self, the reference pairing: what the stand-in peer under test is
     * and does.  NULL means this row cannot be run against the reference. */
    const char *self_declaration;
    const char *self_scenario;
    const char *self_expect;
    /* The role the stand-in holds under --self.  It follows its declaration and
     * not the run's --role, because a negative row's stand-in is deliberately a
     * peer of a different shape — an observer, for the rows that assert a peer
     * declaring neither Detect nor Offline originates neither. */
    const char *self_role;
    int         run_ms;
    const char *asserts;      /* one line, for the report */
} cf_row;

const cf_row *cf_rows(size_t *out_count);

/* ------------------------------------------------------------- the results */

typedef struct cf_result {
    const cf_row *row;
    cf_verdict    verdict;
    char          reason[CF_REASON_LEN];
    char          command[CF_CMD_LEN];
    int           exit_code;
    int64_t       ms;
} cf_result;

typedef struct cf_opts {
    const char *profiles[CF_MAX_PROFILES];
    size_t      profile_count;
    const char *role;            /* the peer under test's role */
    const char *connect_host;
    int         connect_port;
    int         listen_port;
    bool        self;
    const char *sim_path;
    const char *scenario_dir;
    const char *bundle;
    const char *json_path;
    const char *markdown_path;
    const char *column;          /* the matrix column this run fills */
    const char *only;            /* comma-separated row ids, or NULL */
    const char *psk_hex;
    const char *psk_identity;
    bool        quiet;
} cf_opts;

bool cf_declares(const cf_opts *o, const char *profile);
bool cf_row_applies(const cf_opts *o, const cf_row *r, cf_verdict *out_skip_reason);

/* Runs one row and fills `res`.  Never returns a verdict it did not observe:
 * a counterpart that could not reach the peer under test is CF_FAIL with the
 * transport's reason, not a silent skip. */
void cf_run_row(const cf_opts *o, const cf_row *r, cf_result *res);

/* ------------------------------------------------------------- the reports */

bool cf_write_json(const cf_opts *o, const cf_result *res, size_t count,
                   int exit_code, const char *path);
bool cf_write_markdown(const cf_opts *o, const cf_result *res, size_t count,
                       const char *path);
const char *cf_verdict_name(cf_verdict v);
const char *cf_verdict_cell(cf_verdict v);

#endif /* PPCP_CONFORM_H */
