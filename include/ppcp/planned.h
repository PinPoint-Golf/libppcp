/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * planned.h — the public surface that is DECLARED but NOT YET IMPLEMENTED.
 *
 * Why this header exists.  PinPointStudio and PinPointCapture build against
 * `libppcp` one session behind it (plan §7), so they need the names before the
 * bodies.  Everything here is the port surface both applications will bind to
 * (plan A3), with the work package that will implement it named on every block.
 *
 * ⚠ NOTHING IN THIS HEADER LINKS.  These are declarations only; there is no
 * definition in libppcp.a for any of them.  That is deliberate and it is why
 * they are in a separate header from the implemented surface: an application
 * may include ppcp/ppcp.h, compile against these names, and link successfully
 * so long as it does not CALL one.  The moment it calls one the link fails with
 * an undefined symbol naming the function — which is a better diagnostic than a
 * stub returning PPCP_ERR_UNIMPLEMENTED at runtime, because it happens at build
 * time and it names the missing package.
 *
 * A symbol an application needs that is not here is reported to the
 * orchestrator and added to team L's queue.  It is never implemented
 * application-side (plan §7).
 */
#ifndef PPCP_PLANNED_H
#define PPCP_PLANNED_H

#include "ppcp/bundle.h"
#include "ppcp/transfer.h"
#include "ppcp/rv.h"

#ifdef __cplusplus
extern "C" {
#endif

/* L9 landed: the sync estimator, the relation set and liveness are in
 * ppcp/sync.h and ppcp/peer.h, and the bundle replay of MSG §9.1 is in
 * ppcp/bundle.h.  Nothing about clock synchronisation is planned any more. */

/* ======================================================================
 * L10 — Detect, Mint, Arbitrate (CORE §5.12, §5.13, §5.16, §8)
 * ====================================================================== */

/* L10 — not yet implemented.  Promotion is a callback, not a threshold: I14
 * again, and I32's negative half depends on the peer having a policy of its
 * own to decline with. */
typedef bool (*ppcp_promotion_policy)(void *ctx, const ppcp_candidate *c);
/* L10 — not yet implemented.  Exclude-and-retain on a missing, unrelated or
 * over-uncertain relation; the excluded Candidate stays in `Shot.candidates`
 * (I8). */
typedef bool (*ppcp_arbitration_policy)(void *ctx, const ppcp_candidate *c,
                                        const ppcp_timebase_relation *rel);

/* L10 — not yet implemented.  Emits a Candidate.  `at` is the canonical
 * instant, converted here by the nominator from its raw instant, profile and
 * exposure (I33) — a consumer never applies the conversion a second time. */
PPCP_API ppcp_result ppcp_peer_nominate(ppcp_peer *p, const ppcp_candidate *c);
/* L10 — not yet implemented.  Mint installs the promotion policy; the 8.2i
 * deadline is issue_hold_ns plus one heartbeat interval, and 8.2i1 refuses to
 * mint at all with no affine relation to `timebase_ref`. */
PPCP_API ppcp_result ppcp_peer_set_promotion_policy(ppcp_peer *p,
                                                    ppcp_promotion_policy fn, void *ctx);
/* L10 — not yet implemented.  Arbitrate is available only to the peer with
 * role host (I20). */
PPCP_API ppcp_result ppcp_peer_set_arbitration_policy(ppcp_peer *p,
                                                      ppcp_arbitration_policy fn, void *ctx);
/* L10 — not yet implemented.  Attaches a late Candidate to an issued Shot
 * without moving `t0` (I7), additively and order-independently (5.13d–e). */
PPCP_API ppcp_result ppcp_shot_attach_candidate(ppcp_shot *s, const ppcp_candidate *c);

/* ⚠ There is deliberately NO ppcp_shot_merge() and no ppcp_session_merge().
 * I9: reconciliation creates links; no entity is rewritten or merged.  CT-I9
 * asserts that by API surface rather than by behaviour, which is why the
 * absence is documented here rather than left to be noticed. */

/* ======================================================================
 * L11 — Markup (CORE §5.18; MSG §9.0)
 * ====================================================================== */

/* L11 — not yet implemented.  The codec landed in L4 (model.h), because I24
 * makes every peer parse an `annotation` whether or not it declares Markup.
 * What is still missing is the BEHAVIOUR: supersession by `id`, then
 * `revision`, then bytewise `author_peer_id` — a total order, so both delivery
 * orders converge. */
PPCP_API int ppcp_annotation_supersedes(const ppcp_annotation *a,
                                        const ppcp_annotation *b);

/* ⚠ There is deliberately no path from an Annotation to a Shot, a Candidate, a
 * calibration or any computed quantity.  I37 is asserted by API surface. */

#ifdef __cplusplus
}
#endif
#endif /* PPCP_PLANNED_H */
