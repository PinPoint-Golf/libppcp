/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * sync.h — clock synchronisation, the relation set, and liveness.
 * Work package L9.
 *
 * CORE §5.4.1, §6.3, §7.4, §8.3g; MSG §5.4, §6.
 *
 * THREE THINGS LIVE HERE AND THEY ARE DELIBERATELY SEPARATE.
 *
 *   1. `ppcp_sync_estimator` — four timestamps in, one TimebaseRelation out.
 *      One estimator per timebase (I21, 6.3b): there is no composition here
 *      and there is no function that takes two relations (I18).
 *   2. `ppcp_relation_set` — the relations a peer currently holds, and the one
 *      conversion built on them.  A conversion whose relation is missing or
 *      `unrelated` FAILS; it never falls back to a zero offset (5.4b, 8.2i1).
 *   3. Liveness — heartbeat, three missed intervals, link lost, and the
 *      zero-host regime of 8.3g.
 *
 * ⚠ 6.3d — THE HEARTBEAT RATE DOES NOT SET THE SYNC RATE.  Liveness and
 * measurement are separate concerns that happen to share a channel, and one
 * exchange per heartbeat converges far too slowly on skew.  That separation is
 * structural here: the sync scheduler carries its own cadence, the liveness
 * tracker carries the Session's `heartbeat_interval_ms`, and no function takes
 * one and returns the other.
 */
#ifndef PPCP_SYNC_H
#define PPCP_SYNC_H

#include "ppcp/model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================================================== CORE §6.3 — estimator
 *
 * The exchange is four timestamps (MSG §6.1): `t1` the probe's send instant in
 * the prober's timebase, `t2` and `t3` the responder's receive and send
 * instants in the responder's timebase, and `t4` the reply's arrival, recorded
 * locally by the prober and never transmitted.
 *
 * From one exchange:
 *
 *     offset = ((t2 − t1) + (t3 − t4)) / 2      value of `to` minus `from`
 *     rtt    = (t4 − t1) − (t3 − t2)            residence time removed
 *
 * 6.3f RECOMMENDS minimum-RTT filtering and mandates neither the estimator nor
 * a particular filter — what it mandates is that offset AND rate are estimated
 * and that both sigmas are declared.  What this one does, so a reader of the
 * published relation knows what the numbers mean:
 *
 *   - it keeps a window of the most recent exchanges and retains only those
 *     whose RTT is in the lowest-RTT half of the window, which is the
 *     tightness of the latency distribution's left tail;
 *   - it fits offset against local time by least squares over the retained
 *     set, so `skew_ppm` is the fitted slope and is a measurement rather than
 *     an assumption (6.3a — a one-shot handshake does not satisfy this, and
 *     ppcp_sync_estimator_relation() returns PPCP_ERR_NOT_FOUND until two
 *     exchanges separated in time exist);
 *   - it publishes a FILTERED offset, never a stepped one (6.3e): each new fit
 *     moves the published value by a fraction of the difference, so an outlier
 *     that survived the RTT gate cannot put a step into fused output;
 *   - `offset_sigma_ns` combines the residual dispersion of the retained fit
 *     with the irreducible half-RTT asymmetry bound, because a symmetric-delay
 *     assumption is exactly the thing a sigma exists to admit to.
 */

#define PPCP_SYNC_WINDOW      32u   /* exchanges retained per timebase */
/* Minimum-RTT filtering (6.3f): the fit runs over the lowest-RTT half of the
 * window, never fewer than two.  A fraction rather than a latency constant,
 * because a USB tunnel and congested 2.4 GHz WiFi have nothing in common
 * except that their left tails are the honest part. */
#define PPCP_SYNC_ADMIT_DIV   2u
/* 6.3e — filtered, never stepped.  Each fit moves the published offset by a
 * quarter of the difference from where the previous fit predicted it would
 * be, so an outlier that survived the RTT gate cannot put a step into fused
 * output. */
#define PPCP_SYNC_FILTER_DIV  4
/* 6.3c — "a burst of 10–20 exchanges", so the burst is a count and not a
 * quality judgement; it is the specification's own number (CT-I14). */
#define PPCP_SYNC_BURST       16u
#define PPCP_SYNC_BURST_GAP_MS   20u    /* spacing inside a burst */
/* 6.3g — "at least one exchange per five seconds while the session is open". */
#define PPCP_SYNC_MAINTENANCE_MS 5000u

typedef struct ppcp_sync_estimator ppcp_sync_estimator;

PPCP_API size_t ppcp_sync_estimator_sizeof(void);

/* One estimator per LOCAL timebase (I21, 6.1d): the prober runs a separate
 * probe sequence per timebase of its own and declares each resulting relation
 * directly.  `remote_tb` may be NULL, in which case it is learned from the
 * first `sync_reply` — a prober does not know which clock a responder stamps
 * on until the responder says so (6.1b). */
PPCP_API ppcp_result ppcp_sync_estimator_new(void *storage, size_t storage_len,
                                             const char *local_tb, const char *remote_tb,
                                             ppcp_sync_estimator **out);

/* The four timestamps of §6.3, in nanoseconds: `t1`/`t4` in the local
 * timebase, `t2`/`t3` in the remote one.  A negative RTT is PPCP_ERR_MALFORMED
 * — it means the exchange was mis-stamped, and folding it in would corrupt the
 * fit rather than widen it. */
PPCP_API ppcp_result ppcp_sync_estimator_observe(ppcp_sync_estimator *e,
                                                 int64_t t1, int64_t t2,
                                                 int64_t t3, int64_t t4);

/* 6.1b — the responder's timebase, learned from its first `sync_reply` where
 * the constructor was not told it.  Changing it once samples exist is
 * PPCP_ERR_INVALID: a responder that started stamping on a different clock has
 * not improved the estimate, it has invalidated it. */
PPCP_API ppcp_result ppcp_sync_estimator_set_remote_tb(ppcp_sync_estimator *e,
                                                       const char *tb, size_t len);

/* The current estimate as a relation with both sigmas, `method:
 * estimated_online`, `observed_at` in the LOCAL timebase (5.4: `observed_at`
 * is expressed in `from`).
 *
 * PPCP_ERR_NOT_FOUND until the exchange has produced a rate as well as an
 * offset — 6.3a, and it is the reason this returns a result rather than
 * writing an optimistic relation. */
PPCP_API ppcp_result ppcp_sync_estimator_relation(const ppcp_sync_estimator *e,
                                                  ppcp_timebase_relation *out);

/* 6.3c — a network change or a thermal event makes the FIT stale, not merely
 * the offset: oscillator frequency shifts with temperature.  This discards the
 * window and keeps the published offset, so the next burst re-measures without
 * the published value stepping while it does. */
PPCP_API void   ppcp_sync_estimator_restart(ppcp_sync_estimator *e);
PPCP_API size_t ppcp_sync_estimator_count(const ppcp_sync_estimator *e);
PPCP_API bool   ppcp_sync_estimator_has_estimate(const ppcp_sync_estimator *e);
PPCP_API int64_t ppcp_sync_estimator_min_rtt_ns(const ppcp_sync_estimator *e);
PPCP_API const ppcp_id *ppcp_sync_estimator_local_tb(const ppcp_sync_estimator *e);
PPCP_API const ppcp_id *ppcp_sync_estimator_remote_tb(const ppcp_sync_estimator *e);

/* ================================================ the relations a peer holds
 *
 * ⚠ There is no compose, chain or derive here, and there never will be: I18
 * and 5.4c forbid deriving A→C from A→B and B→C, and 5.4.1a says what to do
 * instead — run the exchange once per timebase and declare each relation
 * directly.  `ppcp_relations_convert` applies AT MOST ONE relation, and says
 * so by refusing when it holds no direct one.
 */

#define PPCP_RELATION_SET_MAX 32

typedef struct ppcp_relation_set {
    ppcp_timebase_relation r[PPCP_RELATION_SET_MAX];
    size_t                 count;
} ppcp_relation_set;

PPCP_API void ppcp_relations_init(ppcp_relation_set *rs);
/* Replaces any relation with the same (`from`, `to`) — a relation is a current
 * measurement, not a history. */
PPCP_API ppcp_result ppcp_relations_put(ppcp_relation_set *rs,
                                        const ppcp_timebase_relation *r);
PPCP_API const ppcp_timebase_relation *ppcp_relations_find(const ppcp_relation_set *rs,
                                                           const ppcp_id *from,
                                                           const ppcp_id *to);
PPCP_API size_t ppcp_relations_count(const ppcp_relation_set *rs);

/* Converts one instant into `to_tb`.
 *
 *   same timebase   -> copied through.  I4: identity is identity, and is never
 *                      asserted as a relation with `from == to`.
 *   affine          -> ppcp_relation_apply, exactly once.
 *   `unrelated`     -> PPCP_ERR_INVALID.  5.4b makes `unrelated` a complete
 *                      and legal declaration, and the honest consequence is
 *                      that the value cannot be expressed there.
 *   no relation     -> PPCP_ERR_NOT_FOUND.  Not composed, not assumed zero
 *                      (8.2i1, 5.4b).
 */
PPCP_API ppcp_result ppcp_relations_convert(const ppcp_relation_set *rs,
                                            const ppcp_instant *in,
                                            const ppcp_id *to_tb,
                                            ppcp_instant *out);

/* The timing uncertainty a conversion carries, in nanoseconds: the relation's
 * `offset_sigma_ns` grown by the skew sigma over the elapsed interval since
 * `observed_at`.  Zero for the identity conversion.  It is what an arbitration
 * policy is handed to decide "too uncertain" (8.2d) — the library holds no
 * threshold of its own (I14). */
PPCP_API ppcp_result ppcp_relations_sigma_ns(const ppcp_relation_set *rs,
                                             const ppcp_instant *in,
                                             const ppcp_id *to_tb,
                                             double *out_sigma_ns);

/* ============================================== CORE §7.4, §8.3g — liveness */

typedef enum ppcp_link_state {
    PPCP_LINK_LIVE = 0,
    /* 7.4c — three consecutive missed intervals.  8.3g: the Session is
     * unchanged; what changes is that no arbitration occurs. */
    PPCP_LINK_LOST
} ppcp_link_state;

/* What `heartbeat_ack` carries (MSG 5.4, CORE 7.4b), gathered from the
 * embedding because the library has no thermometer, no battery and no
 * filesystem.  Reporting degradation rather than silently accepting worse data
 * is the whole point of the message. */
typedef struct ppcp_health {
    ppcp_thermal_level thermal;
    bool     has_vendor_label;
    ppcp_id  vendor_thermal_label;
    uint64_t storage_free_bytes;
    bool     has_battery_pct;
    uint32_t battery_pct;
    bool     has_charging;
    bool     charging;
} ppcp_health;

typedef ppcp_result (*ppcp_health_fn)(void *ctx, ppcp_health *out);

/* 6.3c — the three events that trigger a burst.  The library has no network
 * stack and no thermometer, so each is an event the embedding delivers. */
typedef enum ppcp_sync_trigger {
    PPCP_SYNC_ON_CONNECT = 0,
    PPCP_SYNC_ON_NETWORK_CHANGE,
    PPCP_SYNC_ON_THERMAL_EVENT
} ppcp_sync_trigger;

#ifdef __cplusplus
}
#endif
#endif /* PPCP_SYNC_H */
