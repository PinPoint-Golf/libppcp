/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * sim_json.h — a JSON reader, for the declaration files of work package L13.
 *
 * WHY THERE IS ONE HERE.  CONF 2c asks the simulator to present "a declaration
 * different from the implementer's own", and a declaration that lives in C is a
 * declaration only the implementer can write.  So it comes from a file, and a
 * file needs a parser.  It is in tools/ and not in src/ because the library
 * owns no file (ground rule 8) and because nothing in PPCP is JSON: the wire is
 * CBOR and always was.
 *
 * It is deliberately small: objects, arrays, strings with the six escapes and
 * \uXXXX, numbers, true/false/null.  No streaming, no comments, no duplicate-key
 * policy beyond "the first one wins".  Nodes are written into a caller-owned
 * array and strings are unescaped in place in a caller-owned copy of the text,
 * so the parser allocates nothing either.
 */
#ifndef PPCP_SIM_JSON_H
#define PPCP_SIM_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum sj_type {
    SJ_NULL = 0,
    SJ_BOOL,
    SJ_INT,
    SJ_REAL,
    SJ_STR,
    SJ_ARR,
    SJ_OBJ
} sj_type;

typedef struct sj_node {
    sj_type  type;
    char    *key;        /* NUL-terminated member name, or NULL outside an object */
    char    *str;        /* NUL-terminated value, SJ_STR only */
    int64_t  i;
    double   d;
    bool     b;
    int      first;      /* first child index, -1 when none */
    int      next;       /* next sibling index, -1 when none */
} sj_node;

typedef struct sj_doc {
    char    *text;       /* the caller's mutable buffer; strings point into it */
    sj_node *nodes;
    int      count;
    int      cap;
    char     err[128];
    int      err_line;
} sj_doc;

/* Parses `text` in place — it is modified.  `nodes` is the caller's array.
 * Returns true on success; on failure `doc->err` is a one-line reason. */
bool sj_parse(sj_doc *doc, char *text, sj_node *nodes, int node_cap);

/* Reads a whole file into a malloc'd NUL-terminated buffer.  The caller frees
 * it, and must keep it alive for as long as the document is used. */
char *sj_read_file(const char *path, char *err, size_t err_len);

const sj_node *sj_root(const sj_doc *doc);
const sj_node *sj_get(const sj_doc *doc, const sj_node *obj, const char *key);
int            sj_len(const sj_doc *doc, const sj_node *arr);
const sj_node *sj_at(const sj_doc *doc, const sj_node *arr, int index);

/* Typed readers with a default.  A node of the wrong type reads as the
 * default: a declaration file is test infrastructure and a silently wrong
 * field is worse than a loud one, so every caller that cares checks presence
 * with sj_get() first. */
const char *sj_str_or(const sj_node *n, const char *fallback);
int64_t     sj_int_or(const sj_node *n, int64_t fallback);
double      sj_real_or(const sj_node *n, double fallback);
bool        sj_bool_or(const sj_node *n, bool fallback);

#endif /* PPCP_SIM_JSON_H */
