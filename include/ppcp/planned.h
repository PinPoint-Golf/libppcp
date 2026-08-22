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

/* L10 landed: Detect, Mint and Arbitrate are in ppcp/shot.h, with the two
 * policy callbacks, ppcp_shot_attach_candidate() and the deliberate absence of
 * any merge operation. */

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
