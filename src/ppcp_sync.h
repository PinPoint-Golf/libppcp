/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * ppcp_sync.h — INTERNAL.  The estimator's storage, so the peer engine can
 * hold one per declared timebase inside its own flat struct without an
 * allocator.  Nothing here carries PPCP_API and nothing here is in
 * include/ppcp (plan A3).
 */
#ifndef PPCP_SYNC_INTERNAL_H
#define PPCP_SYNC_INTERNAL_H

#include "ppcp/sync.h"

typedef struct ppcp_sync_sample {
    int64_t t_local_ns;   /* the exchange's midpoint in the local timebase */
    int64_t offset_ns;    /* remote minus local, from this exchange alone */
    int64_t rtt_ns;
} ppcp_sync_sample;

struct ppcp_sync_estimator {
    ppcp_id local_tb;
    ppcp_id remote_tb;
    bool    has_remote;

    ppcp_sync_sample s[PPCP_SYNC_WINDOW];
    size_t           count;   /* filled slots, <= PPCP_SYNC_WINDOW */
    size_t           next;    /* ring write position */
    uint64_t         observed;
    int64_t          min_rtt_ns;

    /* the published estimate — filtered, never stepped (6.3e) */
    bool    has_estimate;
    double  offset_ns;
    double  skew_ppm;
    double  offset_sigma_ns;
    double  skew_sigma_ppm;
    int64_t observed_at_ns;   /* in the local timebase (5.4: expressed in `from`) */
};

#endif /* PPCP_SYNC_INTERNAL_H */
