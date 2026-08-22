/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * ppcp.h — the umbrella header, and the port surface.
 *
 * Plan A3: the public surface is this one header over the per-area headers
 * below, and it is what PinPointStudio and PinPointCapture bind to.  Nothing
 * else is public.
 *
 * WHAT IS IMPLEMENTED (work packages L0–L5 and L12):
 *
 *   ppcp/common.h    result codes, PPCP_API, the Id type
 *   ppcp/version.h   library version and the wire version it speaks
 *   ppcp/cbor.h      L1 — the deterministic encoder and limit-enforcing decoder
 *   ppcp/frame.h     L1 — the frame header, the channel rules, the bundle magic
 *   ppcp/envelope.h  L1 — the message envelope of ENC §5
 *   ppcp/time.h      L2 — the injectable clock, Instant, Series, Interval,
 *                    Estimate, Timebase, TimebaseRelation, ClockDiscontinuity
 *   ppcp/timing.h    L3 — the canonical instant, rolling shutter, AchievedFrames
 *   ppcp/hash.h      SHA-256, HMAC-SHA256, HKDF-SHA256
 *   ppcp/rv.h        L12 — the pairing code, key derivation, identities, resolver
 *   ppcp/model.h     L4 — the entity vocabulary of CORE §5, with validation
 *   ppcp/message.h   L5 — the forty-five messages of MSG §11 and the §10 codes
 *   ppcp/peer.h      L6 — the sans-I/O peer engine and the ENC §2.1 link binder
 *   ppcp/bundle.h    L8 — the bundle writer and reader, and the ENC §7 container
 *
 * WHAT IS DECLARED BUT NOT YET BUILT:
 *
 *   ppcp/planned.h   captures and transfer (L7), the sync estimator (L9),
 *                    detect/mint/arbitrate (L10) and annotation supersession
 *                    (L11).
 *
 * ⚠ planned.h declares symbols that DO NOT EXIST in libppcp.a.  Including this
 * header is safe and linking is unaffected; CALLING one of those functions
 * fails at link time with an undefined symbol that names the function, which is
 * the diagnostic an application wants while it is coding ahead of the library.
 * Every one carries the work package that will implement it.
 */
#ifndef PPCP_PPCP_H
#define PPCP_PPCP_H

#include "ppcp/version.h"
#include "ppcp/common.h"
#include "ppcp/cbor.h"
#include "ppcp/frame.h"
#include "ppcp/envelope.h"
#include "ppcp/time.h"
#include "ppcp/timing.h"
#include "ppcp/hash.h"
#include "ppcp/rv.h"
#include "ppcp/model.h"
#include "ppcp/message.h"
#include "ppcp/peer.h"
#include "ppcp/bundle.h"
#include "ppcp/planned.h"

#endif /* PPCP_PPCP_H */
