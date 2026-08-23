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

/* ============================ F-L13-1: feeding past the event queue's depth
 *
 * ppcp_peer_feed() stops rather than overrun the 4-deep event ring, so a test
 * that hands it more frames than that has to drain between calls.  These are
 * the loops peer.h and bundle.h document, written once.  A test that CARES
 * about the events drains them itself; these are for the tests that do not. */
#ifdef PPCP_PEER_H
PPCP_TEST_UNUSED static size_t ppcp_test_drain_events(ppcp_peer *p)
{
    ppcp_event e;
    size_t     n = 0;
    while (ppcp_peer_next_event(p, &e) == PPCP_OK)
        n++;
    return n;
}

/* Feeds every whole frame in `b`, draining events between calls.  Returns the
 * first non-OK result, and reports the bytes consumed — short only if the tail
 * is a partial frame. */
PPCP_TEST_UNUSED static ppcp_result ppcp_test_feed_all(ppcp_peer *p, uint8_t ch,
                                                       const uint8_t *b, size_t len,
                                                       size_t *out_consumed)
{
    size_t off = 0;
    *out_consumed = 0;
    while (off < len) {
        size_t      took = 0;
        ppcp_result rc = ppcp_peer_feed(p, ch, b + off, len - off, &took);
        if (rc != PPCP_OK) {
            *out_consumed = off + took;
            return rc;
        }
        off += took;
        (void)ppcp_test_drain_events(p);
        if (took == 0 && !ppcp_peer_feed_stalled(p))
            break;   /* a partial frame: more bytes, not more room, are needed */
    }
    *out_consumed = off;
    return PPCP_OK;
}
#endif /* PPCP_PEER_H */

#ifdef PPCP_BUNDLE_H
/* The same loop for a bundle replayed into a live sink.  `sink` may be NULL,
 * in which case it is one call. */
PPCP_TEST_UNUSED static ppcp_result ppcp_test_reader_feed_all(ppcp_bundle_reader *r,
                                                              ppcp_peer *sink,
                                                              const uint8_t *b, size_t len,
                                                              size_t *out_consumed)
{
    size_t off = 0;
    *out_consumed = 0;
    while (off <= len) {
        size_t      took = 0;
        ppcp_result rc = ppcp_bundle_reader_feed(r, b + off, len - off, &took);
        if (rc != PPCP_OK) {
            *out_consumed = off + took;
            return rc;
        }
        off += took;
        if (sink != NULL)
            (void)ppcp_test_drain_events(sink);
        if (!ppcp_bundle_reader_stalled(r))
            break;
    }
    *out_consumed = off;
    return PPCP_OK;
}
#endif /* PPCP_BUNDLE_H */

#endif /* PPCP_TEST_UTIL_H */
