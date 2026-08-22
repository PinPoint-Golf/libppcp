# PPCP specification — 1.0, approved

**PinPoint Capture Protocol. An open protocol for time-synchronised capture devices.**

| | |
|---|---|
| Wire version | `ppcp/1.0` |
| Status | **APPROVED for implementation**, 22 August 2026. Revision 8 — see [`CORE` Annex D](ppcp-core.md#annex-d--change-history) |
| Date | 22 August 2026 |
| Reviews | [`reviews/`](reviews/) — three rounds each from PinPointCapture (mobile) and PinPointStudio (host) |
| Reference implementation | `libppcp`, MIT, this repository |

---

## What this is

The formal specification of PPCP: normative field tables, a fixed message catalogue, a wire encoding and a conformance suite.

**This folder is the single authority on PPCP.** Earlier drafts and working documents are not carried here; any copy still in circulation is superseded by what follows.

**Both implementation teams reviewed three times and signed off at every round.** Their closing findings are carried in this text, and both have confirmed they build against it as it stands.

| Round | Reviewed | Findings | Outcome |
|---|---|---|---|
| 1 | The protocol overview and the companion requirements | 5 + 3 | Draft 1 |
| 2 | Draft 1 | 4 host + 4 mobile | Draft 2 |
| 3 | Draft 2 | 4 host + 5 consistency, 3 mobile | Draft 3 |
| 4 | Draft 3 | 3 host + 2 consistency, 2 mobile | **Approved** |

**Approved is not stable.** Implementation proceeds against this text. `ppcp/1.0` freezes — errata only — when the conformance suite passes on both implementations and the [interoperability pairings](ppcp-conformance.md#5-interoperability) are demonstrated. [Annex B](ppcp-core.md#annex-b--open-issues) lists what is still expected to move; none of it blocks implementation.

[`PPCP-RV`](ppcp-rv.md) is **not** covered by this approval — it is at Draft 7 with its own review cycle, and both teams approve it.

## The documents

| Read | Document | Authority | What it settles |
|---|---|---|---|
| 1st | [**PPCP-CORE**](ppcp-core.md) | Normative | Entities, timing contract, session and shot semantics, conformance profiles, thirty-eight invariants |
| 2nd | [**PPCP-MSG**](ppcp-messages.md) | Normative | Forty-two messages, channel semantics, error codes. Annex A holds the nine interaction sequences, now with real message names |
| 3rd | [**PPCP-ENC**](ppcp-encoding.md) | Normative | Framing, CBOR encoding, bulk transfer, the bundle container |
| 4th | [**PPCP-CONF**](ppcp-conformance.md) | Normative | What an implementation must demonstrate, and the seven places it will silently fail |
| — | [**PPCP-RV**](ppcp-rv.md) | Normative when agreed | Rendezvous, pairing, security. **Draft 7** — four review passes, approved by both teams, **no open findings**. The device measurement is in and unfavourable, so §5 stands as written. [Dispositioned separately](rv-review-disposition-2026-08-22.md). Not covered by the PPCP approval |
| — | [**Requirements traceability**](requirements-traceability.md) | Audit | All 172 requirements against the specification set — 164 covered or deliberately out of scope, **six findings** |
| — | [**Review disposition**](review-disposition-2026-08-22.md) | Record | Every review comment across all three rounds, what was done with it, and the calls a reviewer may want to reverse |
| — | [**reviews/**](reviews/) | Input | All eighteen reviews as submitted |

If you have an hour, read `PPCP-CORE` §2 (profiles), §5 (the model) and §6.1 (the canonical instant), then `PPCP-MSG` Annex A. If you have twenty minutes, read the review disposition and `PPCP-CORE` §6.1.

## The seven questions Draft 1 asked

All answered, and both teams agreed on every one. Four are now closed.

| | Question | Outcome |
|---|---|---|
| **Q1** | CBOR with text keys | **Closed — keep.** Agreed without reservation. Field diagnosis happens on a phone at a driving range and in a user-attached bundle; a wire readable in a hex dump is worth more than 40% of a message class totalling ~4 KB per burst. |
| **Q2** | `SessionLink` defined now | **Closed — keep, provisional.** The original objection was withdrawn. The host would trade it if effort were scarce, preferring the issue-timing hole — which Draft 2 fixes anyway. |
| **Q3** | `t + offset + d/2` | **Closed — confirmed** by both teams independently. |
| **Q4** | 50 ms coincidence default | **Open, and sharpened again.** The floor must be measured **per nominator class** — acoustic-to-acoustic is tight, a live external nominator with a coarse clock may be an order of magnitude wider — because if the second exceeds the adjacent-bay ceiling there is no single conformant value. A per-`basis` override is additive rather than breaking, so it is not added speculatively; the measurement design is what had to change. [Annex B8](ppcp-core.md#annex-b--open-issues) |
| **Q5** | Version support window | **Closed.** Two MINOR back or twelve months, whichever is longer. |
| **Q6** | Unbounded candidate audio retention | **Closed — confirmed** by both. The application owns the bound; the protocol owns expressibility and assertable absence. |
| **Q7** | `PPCP-RV` does not exist | **Closed as a question; the document exists and both teams approve it.** [Draft 7](ppcp-rv.md) rests on a measurement taken on the target hardware and has no open findings. Forward secrecy is best-effort by product decision ([§5.4.3](ppcp-rv.md#543-the-decision)), which overrode both reviewers' stated position and was recorded as such. The pairing code — the irreversible part — has survived four passes and four independent recomputations. The deadline stands: `_ppcp._tcp` has shipped in an application bundle, and the code is gated by the first store submission. |

## What is still open

[`PPCP-CORE` Annex B](ppcp-core.md#annex-b--open-issues) has the full list. The ones needing someone to act:

- **`PPCP-RV`** — [Draft 7](ppcp-rv.md) is approved by both teams with **no open findings**. The last question — whether the sensitivity judgement covers candidate audio — was answered: it does, and 5.4j is deleted with the reasoning kept. What remains is B13, a product question about whether the absence of forward secrecy is user-visible.
- **Two timing defaults**, neither measured: the coincidence window and the issue hold. The window's floor must be measured per nominator class, not pooled — see [Annex B8](ppcp-core.md#annex-b--open-issues). Rig work.
- **The rig itself.** `frame_start_to_exposure_offset_ns` and `readout_ns` are `assumed` on every device until it exists; provenance now makes that visible rather than silent.
- **The synthetic peer simulator.** Four of the seven silent-failure tests are untestable without a peer that declares something the reference implementation would not.

## Two things that are easy to miss

**The transport must supply two independently flow-controlled channels.** Not one connection with interleaving — two. A 25 MB capture in flight on a single stream head-of-line blocks the next shot's event. This is expensive to retrofit and invisible in every sequence diagram, because both channels are drawn as one lifeline. [`CORE` §3.1](ppcp-core.md#31-why-two-channels-is-not-negotiable)

**Four of the seven silent-failure tests pass by accident when an implementation is tested only against itself.** Host-side declaration, the zero-host path, comprehension-versus-origination and timing-constant provenance all require a synthetic peer that declares something the reference implementation would not. Building that simulator early is what makes them testable at all. [`CONF` §2c](ppcp-conformance.md#2-required-test-infrastructure)

## Changing this specification

The invariants are the conformance surface, and their identifiers are stable — I1–I21 keep the numbers model draft 4 and its review used, even where the text was amended. New invariants append.

If implementation shows something here to be wrong, that is the expected outcome rather than a failure of the specification — but **the change belongs in the specification first and the code second**, or the document stops describing the system.

---

# Change history

*Newest first. What changed between drafts, kept at the back because a first reader does not need it.*

## What changed in revision 6

Both teams reviewed revision 5 and approved it. Three findings were made independently by both.

| | Change |
|---|---|
| **1** | **A deliberately-shed interval was indistinguishable from a failed one.** `gaps` mean **loss**; an `absent` segment means **nothing was captured**. Deliberate non-retention is now the second, never the first — and `interval` is mandatory on every stream-anchored Capture including `absent`, which is what lets a peer say "nothing was recorded for this span" at all. |
| **2** | **A preview profile is a derived view, not a mode a Source can enter.** No camera runs two configurations at once. Its realised rate and format are derived while a capture Stream is open, it is activatable only on a `preview` Stream, and it declares `intrinsics: none`. |
| **3** | **I36 read an honestly truncated bundle as a defect** — and the bundle is the v1 path. The obligation binds a `complete` Session; a hole *between* segments is a defect in any Session, time *after* the last one is the incompleteness already declared. |
| **4** | **Preview Captures would have been queued and bundled**, because `pending` is where an announced Capture starts. Preview is now **live-only**: never queued, never bundled, and what was dropped is announced absent. |
| **5** | `MeasuredCapability` describes its profile **running alone** — a concurrent preview makes it optimistic, and the ingest decision is taken before the preview is opened. |
| **6** | Either peer may close a Stream, with a reason; `evidence_ref` split into `evidence_stream_id` and `evidence_capture_id`; `preview` added to the `kind` enumeration; the control-channel volume stated and accepted. |

## What changed in revision 5

Since approval. One defect, found by tracing a host-side requirement for live on-screen feedback rather than by review.

| | Change |
|---|---|
| **1** | **A `continuous` Stream could carry nothing.** Every payload message is keyed on `capture_id` and every Capture anchored to a Shot or a Candidate — so the interval a continuity flag exists to describe was the one with no carriage. Continuous attitude and gravity, the raw sensor-arrival evidence a bundle must carry, and `imu`/`wrist` while armed were all unmeetable obligations. `Capture.anchor` gains `{ stream: true }`; I27 amended, **I36** added for the coverage rule. No new message. |
| **2** | **`preview` defined** — a second Stream from an existing Source, low rate, `continuous`, never used for measurement, on its own bulk channel and the first thing dropped under contention. It is what a consumer needs to see that a capture peer reflects what the user is doing; heartbeat only proves the link is up. |

Additive: no field removed, no type narrowed, no meaning changed. It lands in `1.0` because `1.0` is approved and not yet frozen, and because a flag with nothing behind it is a defect rather than a missing feature.

## What changed on approval

The closing round. All findings attached to both sign-offs are carried. Thirty-five invariants, all numbers unchanged; I9 moved profile.

| | Change | Raised by |
|---|---|---|
| **1** | **A peer told to mint may have had no expressible `t0`.** The commonest reason a host stays silent is that it excluded the Candidate for a missing or too-uncertain clock relation — which is exactly the condition under which the peer cannot express `t0` either. A peer that cannot express it now mints nothing and retains the Candidate. The interoperability pairing that found this is also the one that proves it closed. | Studio |
| **2** | **`ShotLink` moves from Offline to Core**, and I9 with it. Mint and Arbitrate both carried obligations discharged through `shot_link`, which only Offline conferred — a live-only host could not satisfy I35 without violating C2. The alternative would have made it implement a bundle container to resolve a socket race. | Studio |
| **3** | **The zero-host regime has two entry conditions** — no host in the roster, and a host that stopped answering — and they were described in the same words. Now separated, with I23 binding a Shot at issuance rather than for ever. | Both |
| **4** | **Who owns which `Shot` field.** Draft 3 let two peers send `shot` for one id; `id`, `t0`, `authority` and `issued_by` are the issuer's, `candidates` is extensible, and extension is additive and order-independent so both ends converge. | Studio |
| **5** | An empty `intrinsics` array had no defined behaviour; `confirmed_by: observer` was worded for arrival pairing and did not cover `shared_candidate`. | Both |
| **6** | **A fourth profile-boundary defect, found by audit rather than review**: a hostless peer would have recorded `arm` into its bundle, and `arm` is conferred by Live. The audit is now required before `ppcp/1.0` freezes. | This pass |

## What changed in Draft 3

Both teams reviewed again, both approved again, and both independently found the same defect in the fix that closed the first round's most serious one.

| | Change | Raised by |
|---|---|---|
| **1** | **The issue-hold fix had reintroduced "every Candidate becomes a Shot" in the live regime**, and `CT-I32` certified it. Nothing obliges a host to answer a Candidate it declines, so after the deadline a peer minted a Shot for every nomination the host had rejected. The mint is now conditioned on the peer's own promotion policy. | Both |
| **2** | **The mint/issue race is closed rather than narrowed.** The host's issue window is bounded so it cannot overlap the mint window; a host receiving a device-minted Shot attaches to it instead of competing; where both fire, they link by `shared_candidate`. I35 added. | Both |
| **3** | **`Candidate.at` is the canonical instant, converted by the nominator.** §8.2a had told the host to convert using that frame's exposure — which a Candidate does not carry. Harmless for acoustic by accident, ~1 ms of silent systematic error for a `motion` candidate. I33 added. | Studio |
| **4** | **I30 narrowed** to admit the one `capture_update` exception it was contradicting in `PPCP-MSG`. | Studio |
| **5** | **The scalar form disambiguated for `intrinsics`**, whose element type is itself an array — the one field the scalar form was most worth having for. | Studio |
| **6** | **Capture identity is `Capture.id`, not the digest.** An absent capture has no payload to hash, and those are the most important content of a partial session. I34 added. | Capture |
| **7** | **The two arbitration parameters are present only when there is a host** — I23 expressed structurally rather than in prose. | Capture |
| **8** | **`ShotLink.confirmed_by`** separates an observer's live assertion from a human decision. One boolean had come to do what three enumerations do elsewhere. Closes Annex B9. | Studio |
| **9** | **A rule for writing invariants** ([§11.1](ppcp-core.md#111-the-rule-for-writing-an-invariant)): an invariant constrains the shape of an output, never a choice. The same defect had now been found twice, in Draft 1 and in Draft 2's fix for it. | Studio |

## What changed in Draft 2

Thirty-two invariants at that point; I1–I28 kept their numbers.

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

Draft 1's own changes — the `nominal_frame_start` offset, the `Mint` profile, origination-not-comprehension, `SessionLink`, `Capture.anchor` — are in [`PPCP-CORE` Annex D](ppcp-core.md#annex-d--change-history).

The full disposition of all three rounds, including what was **not** actioned and why, is in [`review-disposition-2026-08-22.md`](review-disposition-2026-08-22.md).
