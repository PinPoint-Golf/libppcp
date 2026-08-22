# PPCP specification — Draft 2

**PinPoint Capture Protocol. An open protocol for time-synchronised capture devices.**

| | |
|---|---|
| Wire version | `ppcp/1.0` |
| Status | **Draft 2 — approved to implement by both first-party teams** |
| Date | 22 August 2026 |
| Reviews | [`reviews/`](reviews/) — PinPointCapture (mobile) and PinPointStudio (host) |
| Reference implementation | `libppcp`, MIT, this repository |

---

## What this is

The formal specification of PPCP: normative field tables, a fixed message catalogue, a wire encoding and a conformance suite.

**This folder is the single authority on PPCP.** Earlier drafts and working documents are not carried here; any copy still in circulation is superseded by what follows.

**Both implementation teams have reviewed Draft 1 and returned *approve to implement*,** each with changes requested. Those changes are in Draft 2. Implementation may proceed; `ppcp/1.0` is declared stable when the conformance suite passes on both implementations and the [interoperability pairings](ppcp-conformance.md#5-interoperability) are demonstrated.

## The documents

| Read | Document | Authority | What it settles |
|---|---|---|---|
| 1st | [**PPCP-CORE**](ppcp-core.md) | Normative | Entities, timing contract, session and shot semantics, conformance profiles, thirty-two invariants |
| 2nd | [**PPCP-MSG**](ppcp-messages.md) | Normative | Forty-two messages, channel semantics, error codes. Annex A holds the nine interaction sequences, now with real message names |
| 3rd | [**PPCP-ENC**](ppcp-encoding.md) | Normative | Framing, CBOR encoding, bulk transfer, the bundle container |
| 4th | [**PPCP-CONF**](ppcp-conformance.md) | Normative | What an implementation must demonstrate, and the six places it will silently fail |
| — | [**PPCP-RV**](ppcp-rv.md) | Normative when agreed | Rendezvous, pairing, security. **Draft 1, unreviewed** — service type, pairing code, key derivation, TLS profile, security model, test vectors |
| — | [**Review disposition**](review-disposition-2026-08-22.md) | Record | Every review comment across all three rounds, what was done with it, and the calls a reviewer may want to reverse |
| — | [**reviews/**](reviews/) | Input | The two Draft 1 reviews as submitted |

If you have an hour, read `PPCP-CORE` §2 (profiles), §5 (the model) and §6.1 (the canonical instant), then `PPCP-MSG` Annex A. If you have twenty minutes, read the review disposition and `PPCP-CORE` §6.1.

## What changed in Draft 2

Both teams approved and both asked for changes. These are them. Thirty-two invariants now; I1–I28 keep their numbers.

| | Change | Raised by |
|---|---|---|
| **1** | **The zero-host regime no longer turns every Candidate into a Shot.** A Mint peer *promotes* a subset of its own candidates; nothing is discarded. The old rule made a device that correctly detected the ball-into-screen transient — ~9 ms after impact — mint two Shots for one swing, and the conformance suite certified it. I23 rewritten, I8 extended. | Studio |
| **2** | **`Session.issue_hold_ns`** added. The protocol never said *when* a host may issue, so a nominating peer had no deadline after which to mint, and one field was doing duty as both a pairwise tolerance and a collection deadline. I32 added. | Studio |
| **3** | **A third launch-monitor shape.** The monitor that exists writes one row per shot to a file, rewritten in place, attributable only by arrival order — so it is neither a live nominator nor a multi-record export. `ShotLink` gains `basis: arrival_pairing`. Annex B4 reopened. | Studio |
| **4** | **Time-of-flight estimation no longer collides with I5.** The applied correction moves onto the Candidate, where it is consumed; `Calibration` keeps the surveyed position. Previously a converging estimate could only be published by closing and reopening the audio Stream — up to fifty times a session. | Capture |
| **5** | **`tof_correction` carries its sigma.** It was the one estimate in the specification without one, and it is a *converging* estimate, so its uncertainty is the whole point. I29 added. | Capture |
| **6** | **`capture_announce` is small again.** Achieved capability splits into a summary on control and per-frame series with the payload; constant series may be sent as a scalar. The "small, immediate" message measured ~44–70 KB. I30 added. | Capture |
| **7** | **Provenance on timing constants nobody has measured yet.** A declared `frame_start_to_exposure_offset_ns` of `0` was indistinguishable from an unmeasured one; same for `readout_ns` and per-frame exposure. I31 added. | Capture |
| **8** | **Support window settled** — two MINOR versions back or twelve months, whichever is longer, with the supported range carried in `unsupported_version` so a user learns which end is stale. Closes Annex B6. | Both |

Draft 1's own changes — the `nominal_frame_start` offset, the `Mint` profile, origination-not-comprehension, `SessionLink`, `Capture.anchor` — are in [`PPCP-CORE` §0.1](ppcp-core.md#01-what-changed-in-draft-1).

## The seven questions Draft 1 asked

All answered, and both teams agreed on every one. Four are now closed.

| | Question | Outcome |
|---|---|---|
| **Q1** | CBOR with text keys | **Closed — keep.** Agreed without reservation. Field diagnosis happens on a phone at a driving range and in a user-attached bundle; a wire readable in a hex dump is worth more than 40% of a message class totalling ~4 KB per burst. |
| **Q2** | `SessionLink` defined now | **Closed — keep, provisional.** The original objection was withdrawn. The host would trade it if effort were scarce, preferring the issue-timing hole — which Draft 2 fixes anyway. |
| **Q3** | `t + offset + d/2` | **Closed — confirmed** by both teams independently. |
| **Q4** | 50 ms coincidence default | **Open, but sharpened.** The field is now split from `issue_hold_ns`, so a measurement will know which quantity it is estimating. Two measurements are needed: intra-bay spread sets the floor, adjacent-bay separation the ceiling. [Annex B8](ppcp-core.md#annex-b--open-issues) |
| **Q5** | Version support window | **Closed.** Two MINOR back or twelve months, whichever is longer. |
| **Q6** | Unbounded candidate audio retention | **Closed — confirmed** by both. The application owns the bound; the protocol owns expressibility and assertable absence. |
| **Q7** | `PPCP-RV` does not exist | **Open — now drafted, not agreed.** [Draft 1](ppcp-rv.md) was written while the Draft 2 reviews were out. The deadline moved earlier: `_ppcp._tcp` has already shipped in an application bundle, and the pairing code is gated by the first store submission rather than by `ppcp/1.0`. |

## What is still open

[`PPCP-CORE` Annex B](ppcp-core.md#annex-b--open-issues) has the full list. The ones needing someone to act:

- **`PPCP-RV`** — [Draft 1](ppcp-rv.md) is written and needs both teams' review. §4, the pairing code, is the part that cannot be changed after the first code is printed, and should get the hardest reading. Before the first store submission, not before v1.0.
- **Two timing defaults**, neither measured: the coincidence window and the issue hold. Rig work.
- **The rig itself.** `frame_start_to_exposure_offset_ns` and `readout_ns` are `assumed` on every device until it exists; provenance now makes that visible rather than silent.
- **The synthetic peer simulator.** Four of the eight silent-failure tests are untestable without a peer that declares something the reference implementation would not.

## Two things that are easy to miss

**The transport must supply two independently flow-controlled channels.** Not one connection with interleaving — two. A 25 MB capture in flight on a single stream head-of-line blocks the next shot's event. This is expensive to retrofit and invisible in every sequence diagram, because both channels are drawn as one lifeline. [`CORE` §3.1](ppcp-core.md#31-why-two-channels-is-not-negotiable)

**Four of the eight silent-failure tests pass by accident when an implementation is tested only against itself.** Host-side declaration, the zero-host path, comprehension-versus-origination and timing-constant provenance all require a synthetic peer that declares something the reference implementation would not. Building that simulator early is what makes them testable at all. [`CONF` §2c](ppcp-conformance.md#2-required-test-infrastructure)

## Changing this specification

The invariants are the conformance surface, and their identifiers are stable — I1–I21 keep the numbers model draft 4 and its review used, even where the text was amended. New invariants append.

If implementation shows something here to be wrong, that is the expected outcome rather than a failure of the specification — but **the change belongs in the specification first and the code second**, or the document stops describing the system.
