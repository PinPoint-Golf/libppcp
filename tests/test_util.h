/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * A test harness small enough to read.  Tests may use stdio and stdlib —
 * tests/purity.cmake gates src/ and include/, which is where the promise is.
 */
#ifndef PPCP_TEST_UTIL_H
#define PPCP_TEST_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#  define PPCP_TEST_UNUSED __attribute__((unused))
#else
#  define PPCP_TEST_UNUSED
#endif

static int ppcp_test_failures;
static const char *ppcp_test_name;

#define TEST(name)                                                            \
    do {                                                                      \
        ppcp_test_name = (name);                                              \
    } while (0)

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "FAIL %s:%d [%s] %s\n", __FILE__, __LINE__,       \
                    ppcp_test_name ? ppcp_test_name : "", #cond);             \
            ppcp_test_failures++;                                             \
        }                                                                     \
    } while (0)

#define CHECK_EQ_I(a, b)                                                      \
    do {                                                                      \
        long long va_ = (long long)(a), vb_ = (long long)(b);                 \
        if (va_ != vb_) {                                                     \
            fprintf(stderr, "FAIL %s:%d [%s] %s == %s (%lld vs %lld)\n",      \
                    __FILE__, __LINE__, ppcp_test_name ? ppcp_test_name : "", \
                    #a, #b, va_, vb_);                                        \
            ppcp_test_failures++;                                             \
        }                                                                     \
    } while (0)

PPCP_TEST_UNUSED static void ppcp_hexdump(const char *label, const unsigned char *p, size_t n)
{
    size_t i;
    fprintf(stderr, "  %s (%zu):", label, n);
    for (i = 0; i < n; i++)
        fprintf(stderr, " %02x", p[i]);
    fprintf(stderr, "\n");
}

#define CHECK_BYTES(got, got_len, want, want_len)                             \
    do {                                                                      \
        size_t gl_ = (got_len), wl_ = (want_len);                             \
        if (gl_ != wl_ || memcmp((got), (want), wl_) != 0) {                  \
            fprintf(stderr, "FAIL %s:%d [%s] bytes differ\n", __FILE__,       \
                    __LINE__, ppcp_test_name ? ppcp_test_name : "");          \
            ppcp_hexdump("got ", (const unsigned char *)(got), gl_);          \
            ppcp_hexdump("want", (const unsigned char *)(want), wl_);         \
            ppcp_test_failures++;                                             \
        }                                                                     \
    } while (0)

#define TEST_MAIN_END()                                                       \
    do {                                                                      \
        if (ppcp_test_failures != 0) {                                        \
            fprintf(stderr, "%d check(s) failed\n", ppcp_test_failures);      \
            return EXIT_FAILURE;                                              \
        }                                                                     \
        return EXIT_SUCCESS;                                                  \
    } while (0)

/* Parses a hex literal from the specification into bytes, so a test can carry
 * the vector in the form the document prints it. */
PPCP_TEST_UNUSED static size_t ppcp_unhex(const char *hex, unsigned char *out, size_t cap)
{
    size_t n = 0;
    int    hi = -1;
    for (; *hex; hex++) {
        int v;
        if (*hex == ' ' || *hex == '\n' || *hex == '\t' || *hex == '_')
            continue;
        if (*hex >= '0' && *hex <= '9') v = *hex - '0';
        else if (*hex >= 'a' && *hex <= 'f') v = *hex - 'a' + 10;
        else if (*hex >= 'A' && *hex <= 'F') v = *hex - 'A' + 10;
        else { fprintf(stderr, "bad hex '%c'\n", *hex); exit(EXIT_FAILURE); }
        if (hi < 0) { hi = v; continue; }
        if (n >= cap) { fprintf(stderr, "hex overflow\n"); exit(EXIT_FAILURE); }
        out[n++] = (unsigned char)((hi << 4) | v);
        hi = -1;
    }
    if (hi >= 0) { fprintf(stderr, "odd hex\n"); exit(EXIT_FAILURE); }
    return n;
}

#endif /* PPCP_TEST_UTIL_H */
