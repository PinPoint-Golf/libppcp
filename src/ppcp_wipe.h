/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * ppcp_wipe.h — erase a secret-bearing local before its frame dies.
 *
 * ⛔ WHY THIS EXISTS AS ONE HELPER RATHER THAN AS A HABIT.  7.2e, 11.6f (as
 * amended by E51) and 11.11h are MUSTs ABOUT MEMORY, and RT-23 is their
 * review row.  Before this file the library did the right thing in three
 * places and the wrong thing in four: `ppcp_rv_bootstrap.c` and
 * `ppcp_bs_engine.c` each carried a PRIVATE copy of the same volatile-wipe
 * loop, while `ppcp_sha256.c` and `ppcp_rv_derive.c` cleared key-bearing
 * locals with a plain `memset`.  ⚠ `ppcp_hmac_sha256` contradicted itself
 * inside a single function: it wiped `k` through a volatile pointer under a
 * comment explaining that `memset` is not guaranteed against an optimiser,
 * and then cleared `pad` and `inner` with `memset` two lines later — and
 * `pad` holds `key ^ 0x5c`, which INVERTS STRAIGHT BACK TO THE KEY.  For the
 * confirmation MACs of 11.5f that key is `K_c`.
 *
 * Three copies of a rule is three chances to apply two of them.  So: one
 * helper, used wherever a secret-bearing local dies, and no `memset` on
 * anything secret anywhere in `src/`.
 *
 * WHY VOLATILE.  A compiler that can prove an object is dead may delete a
 * `memset` to it — this is a documented and observed optimisation, not a
 * theoretical one, and an optimised-away erasure satisfies neither 7.2e nor
 * 11.6f.  Writes through a `volatile` pointer are side effects the standard
 * requires to happen, so they survive.  `memset_s` and `explicit_bzero` would
 * both do, and neither is portable to every toolchain this builds on;
 * `stdlib.h` is deliberately outside `tests/purity.cmake`'s whitelist anyway.
 *
 * ⚠ WHAT THIS DOES NOT GUARANTEE, stated because RT-23 will be read against
 * it.  It is correct AT SOURCE.  It does not reach a value the compiler has
 * already copied into a register or a spill slot, it does not reach a caller's
 * copy, and nobody here has inspected the generated code — the same limit the
 * machine review recorded about `ppcp_ct_equal` and the zero-`Z` accumulator.
 * It is the strongest portable statement available in C, and it is a bound
 * rather than a proof.
 */
#ifndef PPCP_WIPE_H
#define PPCP_WIPE_H

#include <stddef.h>

static inline void ppcp_wipe(void *p, size_t n)
{
    volatile unsigned char *q = (volatile unsigned char *)p;

    if (p == NULL)
        return;
    while (n-- > 0)
        *q++ = 0;
}

#endif /* PPCP_WIPE_H */
