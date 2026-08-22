/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * sim_json.c — the reader declared in sim_json.h.
 */
#include "sim_json.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct sj_parser {
    sj_doc *doc;
    char   *p;
    int     line;
} sj_parser;

static int sj_fail(sj_parser *ps, const char *what)
{
    if (ps->doc->err[0] == '\0') {
        snprintf(ps->doc->err, sizeof(ps->doc->err), "%s", what);
        ps->doc->err_line = ps->line;
    }
    return -1;
}

static void sj_ws(sj_parser *ps)
{
    for (;;) {
        char c = *ps->p;
        if (c == '\n') {
            ps->line++;
            ps->p++;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            ps->p++;
        } else {
            return;
        }
    }
}

static int sj_node_new(sj_parser *ps, sj_type t)
{
    sj_node *n;
    if (ps->doc->count >= ps->doc->cap)
        return sj_fail(ps, "too many JSON nodes for the parser's table");
    n = &ps->doc->nodes[ps->doc->count];
    memset(n, 0, sizeof(*n));
    n->type  = t;
    n->first = -1;
    n->next  = -1;
    return ps->doc->count++;
}

/* Writes the UTF-8 encoding of `cp` at `out` and returns the byte count.  The
 * output is never longer than the \uXXXX escape that produced it, which is why
 * unescaping in place is safe. */
static size_t sj_utf8(char *out, uint32_t cp)
{
    if (cp < 0x80u) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800u) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    out[0] = (char)(0xE0u | (cp >> 12));
    out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[2] = (char)(0x80u | (cp & 0x3Fu));
    return 3;
}

static int sj_hex4(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    int      i;
    for (i = 0; i < 4; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9')       v |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f')  v |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')  v |= (uint32_t)(c - 'A' + 10);
        else return -1;
    }
    *out = v;
    return 0;
}

/* Consumes a string literal and returns a pointer to its NUL-terminated,
 * unescaped bytes, written over the literal itself. */
static char *sj_string(sj_parser *ps)
{
    char *out;
    char *start;

    if (*ps->p != '"') {
        (void)sj_fail(ps, "expected a string");
        return NULL;
    }
    ps->p++;
    start = ps->p;
    out   = ps->p;
    for (;;) {
        char c = *ps->p;
        if (c == '\0') {
            (void)sj_fail(ps, "unterminated string");
            return NULL;
        }
        if (c == '"') {
            ps->p++;
            *out = '\0';
            return start;
        }
        if (c != '\\') {
            if (c == '\n')
                ps->line++;
            *out++ = c;
            ps->p++;
            continue;
        }
        ps->p++;
        switch (*ps->p) {
        case '"':  *out++ = '"';  ps->p++; break;
        case '\\': *out++ = '\\'; ps->p++; break;
        case '/':  *out++ = '/';  ps->p++; break;
        case 'b':  *out++ = '\b'; ps->p++; break;
        case 'f':  *out++ = '\f'; ps->p++; break;
        case 'n':  *out++ = '\n'; ps->p++; break;
        case 'r':  *out++ = '\r'; ps->p++; break;
        case 't':  *out++ = '\t'; ps->p++; break;
        case 'u': {
            uint32_t cp = 0;
            ps->p++;
            if (sj_hex4(ps->p, &cp) != 0) {
                (void)sj_fail(ps, "bad \\u escape");
                return NULL;
            }
            ps->p += 4;
            /* A surrogate pair is not needed by any declaration file and is
             * refused rather than mis-decoded. */
            if (cp >= 0xD800u && cp <= 0xDFFFu) {
                (void)sj_fail(ps, "surrogate escapes are not supported");
                return NULL;
            }
            out += sj_utf8(out, cp);
            break;
        }
        default:
            (void)sj_fail(ps, "unknown escape");
            return NULL;
        }
    }
}

static int sj_value(sj_parser *ps);

static int sj_number(sj_parser *ps)
{
    char *end = NULL;
    char *s   = ps->p;
    bool  real = false;
    char *q;
    int   idx;

    for (q = s; *q != '\0'; q++) {
        if (*q == '.' || *q == 'e' || *q == 'E')
            real = true;
        else if (!((*q >= '0' && *q <= '9') || *q == '-' || *q == '+'))
            break;
    }
    idx = sj_node_new(ps, real ? SJ_REAL : SJ_INT);
    if (idx < 0)
        return -1;
    errno = 0;
    if (real) {
        ps->doc->nodes[idx].d = strtod(s, &end);
        ps->doc->nodes[idx].i = (int64_t)ps->doc->nodes[idx].d;
    } else {
        ps->doc->nodes[idx].i = (int64_t)strtoll(s, &end, 10);
        ps->doc->nodes[idx].d = (double)ps->doc->nodes[idx].i;
    }
    if (end == s)
        return sj_fail(ps, "expected a number");
    ps->p = end;
    return idx;
}

static int sj_container(sj_parser *ps, bool is_obj)
{
    int  idx = sj_node_new(ps, is_obj ? SJ_OBJ : SJ_ARR);
    int  prev = -1;
    char close = is_obj ? '}' : ']';

    if (idx < 0)
        return -1;
    ps->p++;                 /* '{' or '[' */
    sj_ws(ps);
    if (*ps->p == close) {
        ps->p++;
        return idx;
    }
    for (;;) {
        char *key = NULL;
        int   child;

        sj_ws(ps);
        if (is_obj) {
            key = sj_string(ps);
            if (key == NULL)
                return -1;
            sj_ws(ps);
            if (*ps->p != ':')
                return sj_fail(ps, "expected ':' after a member name");
            ps->p++;
            sj_ws(ps);
        }
        child = sj_value(ps);
        if (child < 0)
            return -1;
        ps->doc->nodes[child].key = key;
        if (prev < 0)
            ps->doc->nodes[idx].first = child;
        else
            ps->doc->nodes[prev].next = child;
        prev = child;
        sj_ws(ps);
        if (*ps->p == ',') {
            ps->p++;
            continue;
        }
        if (*ps->p == close) {
            ps->p++;
            return idx;
        }
        return sj_fail(ps, is_obj ? "expected ',' or '}'" : "expected ',' or ']'");
    }
}

static int sj_literal(sj_parser *ps, const char *word, sj_type t, bool bval)
{
    size_t n = strlen(word);
    int    idx;
    if (strncmp(ps->p, word, n) != 0)
        return sj_fail(ps, "unexpected token");
    idx = sj_node_new(ps, t);
    if (idx < 0)
        return -1;
    ps->doc->nodes[idx].b = bval;
    ps->p += n;
    return idx;
}

static int sj_value(sj_parser *ps)
{
    sj_ws(ps);
    switch (*ps->p) {
    case '{': return sj_container(ps, true);
    case '[': return sj_container(ps, false);
    case '"': {
        char *s = sj_string(ps);
        int   idx;
        if (s == NULL)
            return -1;
        idx = sj_node_new(ps, SJ_STR);
        if (idx < 0)
            return -1;
        ps->doc->nodes[idx].str = s;
        return idx;
    }
    case 't': return sj_literal(ps, "true", SJ_BOOL, true);
    case 'f': return sj_literal(ps, "false", SJ_BOOL, false);
    case 'n': return sj_literal(ps, "null", SJ_NULL, false);
    default:  return sj_number(ps);
    }
}

bool sj_parse(sj_doc *doc, char *text, sj_node *nodes, int node_cap)
{
    sj_parser ps;

    memset(doc, 0, sizeof(*doc));
    doc->text  = text;
    doc->nodes = nodes;
    doc->cap   = node_cap;
    doc->count = 0;

    ps.doc  = doc;
    ps.p    = text;
    ps.line = 1;

    if (sj_value(&ps) < 0)
        return false;
    sj_ws(&ps);
    if (*ps.p != '\0') {
        (void)sj_fail(&ps, "trailing bytes after the top-level value");
        return false;
    }
    return true;
}

char *sj_read_file(const char *path, char *err, size_t err_len)
{
    FILE  *f = fopen(path, "rb");
    char  *buf;
    long   len;
    size_t got;

    if (f == NULL) {
        snprintf(err, err_len, "cannot open %s: %s", path, strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        snprintf(err, err_len, "cannot size %s", path);
        fclose(f);
        return NULL;
    }
    buf = (char *)malloc((size_t)len + 1u);
    if (buf == NULL) {
        snprintf(err, err_len, "out of memory reading %s", path);
        fclose(f);
        return NULL;
    }
    got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

const sj_node *sj_root(const sj_doc *doc)
{
    return (doc->count > 0) ? &doc->nodes[0] : NULL;
}

const sj_node *sj_get(const sj_doc *doc, const sj_node *obj, const char *key)
{
    int i;
    if (obj == NULL || obj->type != SJ_OBJ)
        return NULL;
    for (i = obj->first; i >= 0; i = doc->nodes[i].next) {
        if (doc->nodes[i].key != NULL && strcmp(doc->nodes[i].key, key) == 0)
            return &doc->nodes[i];
    }
    return NULL;
}

int sj_len(const sj_doc *doc, const sj_node *arr)
{
    int i, n = 0;
    if (arr == NULL || (arr->type != SJ_ARR && arr->type != SJ_OBJ))
        return 0;
    for (i = arr->first; i >= 0; i = doc->nodes[i].next)
        n++;
    return n;
}

const sj_node *sj_at(const sj_doc *doc, const sj_node *arr, int index)
{
    int i, n = 0;
    if (arr == NULL || (arr->type != SJ_ARR && arr->type != SJ_OBJ))
        return NULL;
    for (i = arr->first; i >= 0; i = doc->nodes[i].next, n++) {
        if (n == index)
            return &doc->nodes[i];
    }
    return NULL;
}

const char *sj_str_or(const sj_node *n, const char *fallback)
{
    return (n != NULL && n->type == SJ_STR) ? n->str : fallback;
}

int64_t sj_int_or(const sj_node *n, int64_t fallback)
{
    if (n == NULL)
        return fallback;
    if (n->type == SJ_INT || n->type == SJ_REAL)
        return n->i;
    return fallback;
}

double sj_real_or(const sj_node *n, double fallback)
{
    if (n == NULL)
        return fallback;
    if (n->type == SJ_INT || n->type == SJ_REAL)
        return n->d;
    return fallback;
}

bool sj_bool_or(const sj_node *n, bool fallback)
{
    return (n != NULL && n->type == SJ_BOOL) ? n->b : fallback;
}
