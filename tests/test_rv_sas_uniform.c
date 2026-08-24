/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * PPCP-RV RT-20a(b) — the STATISTICAL half of the interposition property.
 * Work package L22.
 *
 * RT-20a(a) is the required part and it is deterministic: §10.4's published
 * interposer quadruple, `849063` against `576027`, no key agreement needed.
 * It lives in test_rv_bootstrap and it passes.  This file is the SHOULD beside
 * it: over a large run of random quadruples, assert that **no collision
 * occurs** and that the displayed digits are **uniform**, which is the
 * property the one-in-a-million bound is computed FROM.
 *
 * ⛔ THE RATE ITSELF IS NEVER ASSERTED, AND THAT IS THE POINT OF THE ROW'S
 * WORDING (E54).  Separating 10⁻⁶ from a 5% neighbour needs of order 10⁹
 * trials for 1.5 σ.  A row demanding a measured rate is a row nobody can run,
 * and **a row nobody can run is a row that gets ticked**.  §11.8's force is
 * the SINGLE ATTEMPT, not the width — 1 in 1 000 000 and 1 in 1 048 576 are
 * the same claim in every way that matters, which is what E54 settled after
 * finding the published figure 4.86% wrong.
 *
 * ⚠ WHAT `Z` IS HERE, STATED PLAINLY BECAUSE THE ROW SAYS "WHERE KEY
 * AGREEMENT IS AVAILABLE" AND IN THIS LIBRARY IT IS NOT.  X25519 never enters
 * `libppcp` (ground rule 13, CA1, A16), so the quadruples below draw `Z` from
 * this file's own PRNG rather than from a curve.  That is sound for what is
 * being measured and it is not a dodge: 11.11c makes 11.6c–11.6e a PURE
 * FUNCTION of `Z`, `v`, `pk_i` and `pk_a`, so the distribution of the digits
 * is a property of HKDF over its inputs, and a uniform `Z` is if anything a
 * cleaner input than a curve's.
 *
 * ⛔ AND WHAT IT THEREFORE DOES NOT SHOW: that X25519's own outputs are well
 * distributed, and that two legs of a REAL interposition differ.  The first is
 * OpenSSL's and CryptoKit's to answer.  The second is RT-20b's, it needs the
 * relay, and `ppcp-relay --selftest` demonstrates it over sockets with keys
 * nobody chose in advance.  Neither is claimed here.
 *
 * The run is SEEDED and therefore reproducible: a conformance row whose
 * verdict changes between runs is a row that gets re-run until it is green.
 */
#include "ppcp/rv.h"
#include "test_util.h"

/* The number of quadruples, and it matches PinPointCapture's own RT-20a run
 * (200 000, χ² 933.6 over 999 dof) so the two implementations' figures are
 * comparable rather than merely both green. */
#define TRIALS 200000u
#define BINS   1000u          /* 999 degrees of freedom */

/* SplitMix64 — a few lines, no dependency, and deterministic.  ⛔ NOT a
 * CSPRNG and not used as one: 11.5a's fresh keypair is the EMBEDDING's
 * obligation from a real CSPRNG (7.2a), and nothing here ever becomes a key.
 * These are test inputs to a pure function. */
static uint64_t sm_state;

static uint64_t sm_next(void)
{
    uint64_t z = (sm_state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

static void fill(uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        p[i] = (uint8_t)(sm_next() >> 24);
}

int main(void)
{
    static uint32_t hist[BINS];
    uint8_t   pk_i[32], pk_m1[32], pk_m2[32], pk_a[32], z1[32], z2[32];
    uint64_t  collisions = 0;
    uint64_t  samples    = 0;
    double    chi2       = 0.0;
    double    expect;
    unsigned  i;
    uint32_t  b;

    sm_state = 0x505043502d525636ull;   /* "PPCP-RV6" */

    TEST("RT-20a(b) — no collision between an interposer's two legs, and the "
         "digits are uniform by chi-squared");

    for (i = 0; i < TRIALS; i++) {
        ppcp_rv_bootstrap leg1, leg2;

        /* The shape of an interposition: the honest initiator's key, the
         * attacker's two, and the honest acceptor's.  Leg 1 is what the
         * initiator sees; leg 2 is what the acceptor sees.  Each leg binds
         * its OWN pk_i ‖ pk_a into sas_raw (11.6c), which is the binding
         * 11.6c2 forbids dropping and the only thing separating the two
         * peers under substitution. */
        fill(pk_i, sizeof(pk_i));
        fill(pk_m1, sizeof(pk_m1));
        fill(pk_m2, sizeof(pk_m2));
        fill(pk_a, sizeof(pk_a));
        fill(z1, sizeof(z1));
        fill(z2, sizeof(z2));
        z1[0] |= 1u;                     /* never the all-zero Z of 11.6b */
        z2[0] |= 1u;

        CHECK_EQ_I(ppcp_rv_bootstrap_derive(z1, 1u, pk_i, pk_m1, &leg1), PPCP_OK);
        CHECK_EQ_I(ppcp_rv_bootstrap_derive(z2, 1u, pk_m2, pk_a, &leg2), PPCP_OK);

        if (leg1.sas == leg2.sas)
            collisions++;

        hist[leg1.sas / (1000000u / BINS)]++;
        hist[leg2.sas / (1000000u / BINS)]++;
        samples += 2;

        /* Trap 6 — every exit path, including this loop's 200 000 of them. */
        ppcp_rv_bootstrap_wipe(&leg1);
        ppcp_rv_bootstrap_wipe(&leg2);
    }

    /* ⛔ NO COLLISION over the run.  At 10⁻⁶ per trial and 2×10⁵ trials the
     * expected count is 0.2, so a single collision is unremarkable and two is
     * not yet evidence of anything — but the row as E54 words it says "assert
     * no collision occurs", and on this seed none does.  A future seed that
     * produced one would not be a defect and this comment is here so nobody
     * treats it as one. */
    CHECK_EQ_I((int)collisions, 0);

    /* Uniformity over 10⁶, binned into 1000 buckets of 1000 consecutive
     * values.  χ² = Σ (observed − expected)² / expected, 999 dof, mean 999,
     * σ = √(2×999) ≈ 44.7.  The band below is ±4 σ, which is wide on purpose:
     * the assertion is that the digits are not visibly skewed, not that this
     * particular seed lands near the mean. */
    expect = (double)samples / (double)BINS;
    for (b = 0; b < BINS; b++) {
        double d = (double)hist[b] - expect;
        chi2 += (d * d) / expect;
    }

    /* Printed rather than merely asserted: the row's value to a reader is the
     * NUMBER, and a green tick with no figure beside it is what E54 was
     * complaining about. */
    fprintf(stderr, "RT-20a(b): %llu quadruples, %llu digits, %llu collisions, "
                    "chi2 = %.1f over %u dof\n",
            (unsigned long long)TRIALS, (unsigned long long)samples,
            (unsigned long long)collisions, chi2, BINS - 1u);

    CHECK(chi2 > 820.0);
    CHECK(chi2 < 1180.0);

    TEST_MAIN_END();
}
