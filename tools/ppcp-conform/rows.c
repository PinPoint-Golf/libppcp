/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * rows.c — the conformance rows this tool can decide from OUTSIDE the
 * implementation, and only those.
 *
 * ⚠ THE SELECTION RULE, because it is the whole design.
 *
 * CONF §1's method vocabulary has five entries and this tool implements two of
 * them: *paired* and *injected*.  A `static` row is decidable from a
 * declaration and a `fixture` row from a recorded stream; both are answered by
 * the implementation's own suite, against files in this repository, and a tool
 * that drove a socket to answer them would be answering them worse.  So the
 * table below is the paired and injected rows, and each one is written as a
 * property the COUNTERPART can observe on the wire.
 *
 * That last clause does real work.  CT-I34 — re-import is idempotent — is a
 * paired row in the matrix and is NOT here: the duplicate a receiver drops is
 * dropped inside the receiver, and nothing on the wire distinguishes an
 * importer that de-duplicated from one that imported twice.  It is a fixture
 * row for the implementation's own suite.  A conformance instrument that
 * claimed it from outside would be claiming something it could not see.
 *
 * Every `expect` name below is a counter `ppcp-sim` already maintains and
 * already prints; `tools/scenarios/README.md` maps every declaration and
 * scenario to the row it serves.
 */
#include "conform.h"

#include <stddef.h>

/* The counterpart also refuses, on its own account and without an --expect,
 * every violation in the scenarios README's table: a revised `t0` (I7), a
 * message originated by a peer whose declared profiles do not confer it (I24,
 * CONF 1d), `authority: host` from a peer that declared `role: capture` (I20),
 * a malformed or over-long frame (ENC 4, 8a), a held relation spanning two
 * clocks of one peer (I18, 5.4c), and a first frame that is not `link_bind`
 * (ENC 2.1c).  `violations=0` on every row is therefore doing more work than it
 * looks like it is. */
static const cf_row rows[] = {

/* ============================================ the peer under test is a HOST */

{ "CT-I7", "I7", "Mint, Arbitrate", "paired", CF_POSITIVE,
  "arbitrate", "host",
  "capture", "reference-capture.json", "late-candidate-capture",
  "violations=0,t0_revisions=0,shots_rx>=1",
  "reference-host.json", "reference-host", "violations=0", "host", 6000,
  "A Candidate delivered 700 ms after the Shot was issued attaches, and `t0` is "
  "byte-identical before and after." },

{ "CT-I8", "I8", "Mint, Arbitrate", "paired", CF_POSITIVE,
  "arbitrate,detect", "host",
  "capture", "reference-capture.json", "nominating-capture",
  "violations=0,shots_rx>=1",
  "acoustic-host.json", "acoustic-host", "violations=0", "host", 6000,
  "Every Candidate is retained and appears in `Shot.candidates`; losers are not "
  "dropped.  The two-nominators-one-basis half needs the host to own an acoustic "
  "Source, which its own declaration decides." },

{ "CT-I20", "I20", "Arbitrate", "paired", CF_POSITIVE,
  "arbitrate", "host",
  "capture", "reference-capture.json", "reference-capture",
  "violations=0",
  "reference-host.json", "reference-host", "violations=0", "host", 5000,
  "No `shot` carrying `authority: host` reaches the wire from a peer that "
  "declared `role: capture`, and the counterpart refuses one if it does." },

{ "CT-I21", "I21", "Live", "paired", CF_POSITIVE,
  "live", "host",
  "capture", "three-timebase-capture.json", "reference-capture",
  "violations=0,relations_composed=0,probe_timebases=3",
  "three-timebase-host.json", "reference-host", "violations=0", "host", 8000,
  "A probe sequence per timebase against a counterpart declaring three, and no "
  "relation emitted that was not measured (I18, 5.4c)." },

{ "CT-I36a", "I36", "Capture", "paired", CF_POSITIVE,
  "capture,live", "host",
  "capture", "preview-capture.json", "preview-capture",
  "violations=0",
  "reference-host.json", "reference-host", "violations=0", "host", 6000,
  "A `continuous` Stream's segment and a `preview` Stream's discarded window "
  "reach the host as `absent` / `not_retained`, on the channel 5.11h puts them "
  "on." },

{ "CT-S5", "I18", "Core", "paired", CF_POSITIVE,
  "live", "host",
  "capture", "three-timebase-capture.json", "reference-capture",
  "violations=0,relations_composed=0,probe_timebases=3",
  "three-timebase-host.json", "reference-host", "violations=0", "host", 8000,
  "Relations are measured and never composed, in both directions." },

{ "CT-S6", "I24", "Core", "injected", CF_POSITIVE,
  "core", "host",
  "observer", "observer-core.json", "observer",
  "violations=0",
  "reference-host.json", "reference-host", "violations=0", "host", 5000,
  "An observer declaring Core and Live parses everything, originates nothing "
  "past `hello`, `declare` and its heartbeat acks, and the transport stays "
  "open." },

{ "IOP-5", "I3, 8.2i1", "Core", "paired", CF_POSITIVE,
  "arbitrate", "host",
  "capture", "unrelated-capture.json", "unrelated-capture",
  "violations=0,shots_rx=0",
  "reference-host.json", "reference-host", "violations=0", "host", 6000,
  "A counterpart declaring its clock `unrelated` (5.4b) is excluded and "
  "RETAINED: no Shot is issued over it and no zero offset is substituted." },

{ "CT-I12", "I12", "Offline", "paired", CF_POSITIVE,
  "offline", "host",
  "capture", "reference-capture.json", "offer-session",
  "violations=0,offers_tx>=1,accepts_rx>=1",
  "reference-host.json", "reference-host", "violations=0", "host", 8000,
  "A stored Session offered over the live link is accepted and its bundle "
  "replayed into the same ingest path a socket feeds (CORE 9a)." },

/* ========================================= the peer under test is a CAPTURE */

{ "CT-S4", "I20, I23", "Mint", "injected", CF_POSITIVE,
  "mint", "capture",
  "host", "reference-host.json", "silent-host",
  "violations=0,issued=0",
  "reference-capture.json", "nominating-capture", "violations=0,minted>=1", "capture", 8000,
  "A host that receives every Candidate and issues no Shot: the only thing that "
  "fires is the nominating peer's own 8.2i deadline, and it fires only for a "
  "Candidate its promotion policy would have promoted hostless (I32)." },

{ "CT-I35", "I35", "Arbitrate", "injected", CF_POSITIVE,
  "mint", "capture",
  "host", "reference-host.json", "late-host",
  "violations=0,t0_revisions=0",
  "reference-capture.json", "nominating-capture", "violations=0", "capture", 8000,
  "A host arbitrating only after the device was entitled to mint attaches to "
  "the device's Shot rather than issuing a competing one (8.2k), and `t0` does "
  "not move." },

{ "CT-I18", "I18", "Core", "paired", CF_POSITIVE,
  "live", "capture",
  "host", "three-timebase-host.json", "reference-host",
  "violations=0,relations_composed=0,probe_timebases>=1",
  "three-timebase-capture.json", "reference-capture", "violations=0", "capture", 8000,
  "A counterpart with three clocks probes each directly; nothing is composed "
  "from two relations (5.4c)." },

{ "CT-S3", "I19", "Core", "injected", CF_POSITIVE,
  "capture", "host",
  "capture", "foreign-capture.json", "nominating-capture",
  "violations=0",
  "reference-host.json", "reference-host", "violations=0", "host", 6000,
  "A counterpart whose camera is not a phone — `convention: start`, `geometry: "
  "global`, `intrinsics: fixed`, a `continuous` timebase with 17 ppm of skew — "
  "is accepted on its declaration and not on the implementer's own habits." },

{ "CT-S7", "I31", "Capture", "injected", CF_POSITIVE,
  "capture", "host",
  "capture", "measured-capture.json", "nominating-capture",
  "violations=0",
  "reference-host.json", "reference-host", "violations=0", "host", 6000,
  "A counterpart declaring `provenance: measured` with a NON-ZERO offset: an "
  "assumed zero is correct against another assumed zero and wrong against this "
  "one, which is the accident CONF §2c exists to prevent." },

/* ================================================= CONF §1d — the NEGATIVES
 *
 * These run only where the profile is NOT declared, and each asserts the two
 * halves of I24 together: the messages are PARSED (the transport stays open and
 * the counterpart reaches the end of its scenario) and never ORIGINATED (the
 * counter is zero, and `ppcp-sim` refuses the frame on its own account if one
 * arrives). */

/* ⚠ THE COUNTERPART IS A MINTING CAPTURE PEER, AND HAS TO BE.
 *
 * This row used to run `reference-host` against the peer under test.  For a
 * peer under test that is itself a host — PinPointStudio, which declares
 * Arbitrate and not Mint and is exactly who this row is for — CORE 5.2b and MSG
 * 3.2c make that `role_conflict`, which is FATAL: the row died at `hello` and
 * asserted nothing.  A capture peer that mints on its own authority is the
 * counterpart the negative half needs, because it puts a device-authority
 * `shot` on the wire for the peer under test to parse (C1) without conferring
 * anything on it.
 *
 * And the assertion is `minted_shots_rx`, not `shots_rx`.  A host that declares
 * Arbitrate may legitimately send `shot` — the catalogue binds it to the SET
 * Mint / Arbitrate — and under 8.2k it re-sends the DEVICE's Shot with
 * `issued_by`, `t0` and `authority` unchanged, so `shots_rx` is expected to be
 * non-zero here.  What Mint confers is issuing on one's OWN authority, and that
 * is the counter. */
{ "CT-I6", "I6", "Mint", "injected", CF_NEGATIVE,
  "mint", "any",
  "capture", "reference-capture.json", "nominating-capture",
  "violations=0,minted_shots_rx=0",
  "arbiter-no-detect.json", "arbiter-no-detect", "violations=0", "host", 6000,
  "A peer that does not declare Mint parses a device-authority `shot` and never "
  "issues one on its own authority (8.3d), whatever else it may send." },

{ "CT-I20n", "I20", "Arbitrate", "injected", CF_NEGATIVE,
  "arbitrate", "any",
  "capture", "reference-capture.json", "nominating-capture",
  "violations=0,shots_rx=0",
  "reference-host.json", "silent-host", "violations=0", "host", 6000,
  "A peer that does not declare Arbitrate parses `candidate` completely and "
  "originates neither a host-authority `shot` nor a `capture_request`." },

{ "CT-I26", "I26", "Detect", "injected", CF_NEGATIVE,
  "detect", "any",
  "host", "reference-host.json", "reference-host",
  "violations=0,candidates_rx=0",
  "observer-core.json", "observer", "violations=0", "observer", 5000,
  "A peer that does not declare Detect parses `candidate` and never nominates." },

{ "CT-I25", "I25", "Offline", "injected", CF_NEGATIVE,
  "offline", "any",
  "host", "reference-host.json", "reference-host",
  "violations=0,offers_rx=0",
  "observer-core.json", "observer", "violations=0", "observer", 5000,
  "A peer that does not declare Offline parses `session_offer` and never offers "
  "a stored Session." }
};

const cf_row *cf_rows(size_t *out_count)
{
    if (out_count != NULL)
        *out_count = sizeof(rows) / sizeof(rows[0]);
    return rows;
}
