/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * version.h — the library's own version, and the wire version it speaks.
 *
 * These are two different things and are deliberately not derived from one
 * another.  The wire version is the protocol's (CORE §10.1); the library
 * version is this implementation's, and moves whenever the code does.
 */
#ifndef PPCP_VERSION_H
#define PPCP_VERSION_H

#define PPCP_LIB_VERSION_MAJOR 0
#define PPCP_LIB_VERSION_MINOR 1
#define PPCP_LIB_VERSION_PATCH 0
#define PPCP_LIB_VERSION_STRING "0.1.0"

/* CORE §10.1 — the wire version this library implements.  MAJOR is
 * compatibility; MINOR adds fields that a conformant peer already ignores
 * (I13), which is why a peer accepts a higher MINOR and refuses a higher
 * MAJOR. */
#define PPCP_WIRE_VERSION       "ppcp/1.0"
#define PPCP_WIRE_VERSION_MAJOR 1
#define PPCP_WIRE_VERSION_MINOR 0

/* RV 4.3 — the pairing-code payload version this library implements. */
#define PPCP_RV_PAYLOAD_VERSION 1

#endif /* PPCP_VERSION_H */
