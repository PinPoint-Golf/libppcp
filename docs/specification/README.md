# PPCP specification — 1.0, approved

**PinPoint Capture Protocol. An open protocol for time-synchronised capture devices.**

| | |
|---|---|
| Wire version | `ppcp/1.0` |
| Status | **APPROVED for implementation**, 22 August 2026. Revision 9, final — see [`CORE` Annex D](ppcp-core.md#annex-d--change-history) — **plus errata E1–E51**, all normative, listed in [`CORE`'s errata table](ppcp-core.md#errata-after-revision-9). E30–E48 are `PPCP-RV` only — E30–E33 from [CR-01](../changerequests/CR-01-disposition.md), E34–E51 from its [four](../changerequests/CR-01-review-response.md) [review](../changerequests/CR-01-review-response-2.md) [passes](../changerequests/CR-01-review-response-3.md) ([fourth](../changerequests/CR-01-review-response-4.md)), including E48 closing [B17](../changerequests/CR-01-x25519-seam.md) |
| Date | 22 August 2026; errata to 24 August 2026 |
| Errata | **51** — E1–E29 from implementation sessions S1–S5, E30–E33 from change request [CR-01](../changerequests/CR-01-disposition.md), E34–E51 from its [four](../changerequests/CR-01-review-response.md) [review](../changerequests/CR-01-review-response-2.md) [passes](../changerequests/CR-01-review-response-3.md) ([fourth](../changerequests/CR-01-review-response-4.md)) |
| Freeze | **The text is recommended for freeze; `ppcp/1.0` is NOT declared stable.** All ten interoperability pairings of [`CONF` §5](ppcp-conformance.md#5-interoperability) pass, three of them between the two real applications, and both freeze-gate audits run and pass. Five conditions remain — see [`../conformance/freeze-readiness.md`](../conformance/freeze-readiness.md) |
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

**Approved is not stable, and the two are different acts.** The conformance suite passes on both implementations and the [interoperability pairings](ppcp-conformance.md#5-interoperability) are demonstrated — all ten, three of them between the two real applications over TLS. [`freeze-readiness.md`](../conformance/freeze-readiness.md) is the assessment: **freeze the documents against anything but errata; do not declare `ppcp/1.0` stable yet.** What is outstanding is evidence rather than text — a Capture carrying bytes across the two applications, a camera declaration meeting a foreign one on real hardware, and two `review`-method security requirements that no test can discharge and no reviewer has been named for. [Annex B](ppcp-core.md#annex-b--open-issues) lists what is still expected to move; none of it blocks implementation.

**Twenty-nine errata have been taken, and that is the process working rather than failing.** Every one was found by building the thing: two implementations that each invented a different way to associate a peer's connections (E1); a PSK identity that failed one handshake in sixteen because a zero byte truncated it (E21); a document whose only worked example no conformant encoder could reproduce (E5); a `session_offer` that silently rebound the live Session's clock reference (E28); and a sweep that found 27 of the 45 messages required by no normative clause at all (E18). Four more were open questions the implementation had to answer, decided and marked reversible (E24–E27). The full list, with the finding that produced each, is [`CORE`'s errata table](ppcp-core.md#errata-after-revision-9).

[`PPCP-RV`](ppcp-rv.md) is **not** covered by this approval — it is versioned separately, and is now approved for implementation in its own right. **Its revision 9 adds [RV-6, guided pairing](ppcp-rv.md#11-rv-6--guided-pairing) under [CR-01](../changerequests/CR-01-disposition.md); both teams reviewed it four times and both return *ready to implement, no objection to starting*, with twenty findings applied as errata E34–E51.**

## The documents

| Read | Document | Authority | What it settles |
|---|---|---|---|
| 1st | [**PPCP-CORE**](ppcp-core.md) | Normative | Entities, timing contract, session and shot semantics, conformance profiles, thirty-eight invariants |
| 2nd | [**PPCP-MSG**](ppcp-messages.md) | Normative | Forty-five messages, channel semantics, error codes. Annex A holds the nine interaction sequences, now with real message names |
| 3rd | [**PPCP-ENC**](ppcp-encoding.md) | Normative | Framing, CBOR encoding, bulk transfer, the bundle container |
| 4th | [**PPCP-CONF**](ppcp-conformance.md) | Normative | What an implementation must demonstrate, and the seven places it will silently fail |
| — | [**PPCP-RV**](ppcp-rv.md) | Normative when agreed | Rendezvous, pairing, security. **APPROVED**, revision 9 — five review passes on §1–§10, **no open findings there**. Revision 9's [§11, RV-6](ppcp-rv.md#11-rv-6--guided-pairing) is new under [CR-01](../changerequests/CR-01-disposition.md) and has had **four** review passes, which found twenty things including two blocking. Passes 2 and 3 each found a defect the previous pass's fix had introduced; **pass 4 found nothing in the normative clauses at all** — and split [RT-20](ppcp-rv.md#9-conformance), the one test that checks the security property, so that two thirds of it runs before either application writes a line. [Dispositioned separately](rv-review-disposition-2026-08-22.md). Versioned separately from PPCP |
| — | [**Requirements traceability**](requirements-traceability.md) | Audit | All 172 requirements against the specification set — 164 covered or deliberately out of scope, **six findings** |
| — | [**Review disposition**](review-disposition-2026-08-22.md) | Record | Every review comment across all three rounds, what was done with it, and the calls a reviewer may want to reverse |
| — | [**reviews/**](reviews/) | Input | All twenty reviews as submitted |

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
| **Q7** | `PPCP-RV` does not exist | **Closed as a question; the document exists and both teams approve it.** It rests on a measurement taken on the target hardware and has no open findings. Forward secrecy is best-effort by product decision ([§5.4.3](ppcp-rv.md#543-the-decision)), which overrode both reviewers' stated position and was recorded as such. The pairing code — the irreversible part — has survived four passes and four independent recomputations. The deadline stands: `_ppcp._tcp` has shipped in an application bundle, and the code is gated by the first store submission. |

## What is still open

[`PPCP-CORE` Annex B](ppcp-core.md#annex-b--open-issues) has the full list. The ones needing someone to act:

- **`PPCP-RV`** is approved with **no open findings against §1–§10**. What remains there is B13 — whether the absence of forward secrecy should be user-visible — which is a product question for the implementation teams, and B2, per-peer re-keying, which both publishers avoid by emitting `mu: 1` only. **[§11, RV-6](ppcp-rv.md#11-rv-6--guided-pairing) is new**, has had three review passes (B16 and [B17](../changerequests/CR-01-x25519-seam.md) closed; both teams return *ready to implement*), and carries three open issues — B18 (version negotiation, deliberately unanswered) joins: B14 (X25519 — discharged on both platforms, the iOS **device** run outstanding and required before shipping), B15 (the fleet case, which needs B2 first) and B18. **[§11.11](ppcp-rv.md#1111-where-x25519-comes-from) closed B17**: X25519 is a *parameter*, not a dependency — two 32-octet values cross a boundary stated in octets, and the derivation stays a pure function that a build with no curve arithmetic can reproduce. **RT-20, the test the section exists to pass, still cannot run**: it needs two real implementations either side of a deliberate relay.
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

## Errata to 1.0 — from implementation

*Approved is not stable: implementation is expected to find things, and the change belongs here first. **[`PPCP-CORE`'s errata table](ppcp-core.md#errata-after-revision-9) is the authoritative list** — every erratum, its clause, what changed and the finding that produced it. This is the summary. Tracked in [`../implementation/implementation-plan.md` §9](../implementation/implementation-plan.md).*

**Fifty-one errata, E1–E51.** E1–E29 came from implementation sessions S1–S5; E30–E47 came from [CR-01](../changerequests/CR-01-disposition.md), the first change request — E30–E33 the grant, then three review passes over it, each finding a defect the previous one's fix had introduced — which is a different thing and is grouped separately below. Grouped by what they were:

| | Errata | What they are |
|---|---|---|
| **Two implementations disagreed** | E1, E28 | The two most serious. A listener had no way to know which of a peer's streams belonged together (`link_bind`, E1); and a `session_open` naming a different Session silently rebound the live Session's `timebase_ref`, so a host expressed every subsequent `t0` in the exporting device's clock and arbitrated two Sessions as one (E28). Both were invisible from inside one implementation. |
| **A clause was unsatisfiable as written** | E6, E9, E16, E19 | Eight message bodies named a field the envelope reserved and a duplicate key makes malformed. A bundle need not carry `declare`, yet Capture identity is scoped by the minting peer. A preview segment the specification *requires* announcing could not be announced. A hostless conformance run was told to exercise `arm`, which the profile boundary forbids. |
| **A rule was missing where two peers had to agree** | E5, E10, E11, E14, E20, E25 | Deterministic ordering in the worked example; two rounding rules; which vocabularies are closed; **which Candidate sets `t0`**; whether a defaulted optional is emitted; one syntax for a version range. Each is a place where two conformant implementations could differ and neither could tell. |
| **A platform made a clause unimplementable** | E21, E22, E23, E24 | A zero byte in a PSK identity truncated by a `strlen`-lengthed interface — one handshake in sixteen. A listener with no server-side PSK resolver. A clock test iOS does not expose. |
| **The model had no room for the answer** | E7, E12, E13, E17, E29 | No declared container for a payload. No way to say *which* span left the buffer. Camera vocabulary on a stream with no frames. A `closed_at` in a clock the closing peer cannot read. A Candidate excluded for a missing relation and never revisited when it arrived. |
| **Scope and reading** | E2, E3, E4, E8, E15, E18, E26, E27 | `mu` counts pairings, not handshakes. 2c binds the rendezvous paths, not a handed-in socket. A responder's timebase is addressable. Three completeness states, not two. C3 binds requests. And the sweep that found **27 of 45 messages required by no normative clause**. |
| **A test that was wrong, and an erratum that was** | E49, E50, E51 | **The fourth pass, and the first to find nothing in the clauses.** E50 is the important one: **RT-20 — the only test in the set where somebody is actually attacking — had been reported *not moved* in four consecutive passes**, because it was one row bundling a computation, a per-peer behaviour and an interop result, so the whole waited on the last of the three. Split, two thirds run before either application writes a line, and a large run now *measures* the 2⁻²⁰ bound the security argument is quoted in. E49 withdraws two wrong claims from E47 — rotating `rid` renames the service, so the mitigation for a discovery problem would have triggered the condition that breaks discovery. E51: a MUST whose prose carried the failure case and whose **list** did not. |
| **Reviewing the fix's own reasoning** | E43, E44, E45, E46, E47 | **The third pass.** E43 is the serious one: E40's supporting rationale claimed *"`Z` already commits to both public keys by construction"*, and **X25519 is not contributory** — a different public key yields a bit-identical, non-zero shared secret, verified on two implementations. Under that substitution every value E40 excused from binding is identical, and **only the explicit key binding separates the peers** — so the sentence handed a reader the argument for deleting it. Then a triage list naming one silent failure where there are two (E44), a version gap whose fallback is now immediate (E45), unknown map keys undefined (E46), and a reconnection wait that only became live once question 3 was answered (E47). |
| **Reviewing the fix, not only the text** | E40, E41, E42 | **The second pass, and the argument for having one.** E40 is a trap **E34's own fix created**: it summarised its new binding as a general rule, offered explicitly as the safer thing to hold in mind instead of the formulas — and the rule was untrue of the two clauses directly beneath it, in the direction that yields matching digits, matching MACs and a divergent `PRK`. Then a derivation input the document never stated and the vector cannot expose (E41), and two wrong numbers in prose notes whose whole purpose is to prevent wrong numbers (E42) — **both teams recomputed fifteen vector rows twice and neither recomputed the arithmetic beside them.** |
| **A review pass over new work** | E34, E35, E36, E37, E38, E39 | **The first change request's own review, and the argument for having one.** Two blocking: a bootstrap version field carried on the wire and **bound into nothing** (E34), and a rule serialising one side of an exchange and not the other, where the natural implementation is the one that breaks it and the operator finds the collision (E35). Then a clause naming a return value **neither implementation's crypto library produces** (E36 — E23's shape again), an inverted rationale that would have had implementers weigh a MAC as the authentication (E37), a plaintext transcript that is an offline verifier against a weak RNG (E38), and role-partial conformance left undefined while one peer ships initiator-only (E39). **None was visible in the worked vectors, which both teams reproduced byte for byte.** |
| **A requirement the document never served** | E30, E31, E32, E33 | **Not defects.** [CR-01](../changerequests/CR-01-disposition.md) asked for a first pairing with no code carried between two screens, which revision 8 was self-consistent in not providing. Granted in part — the transfer goes, the operator does not — as [RV-6](ppcp-rv.md#11-rv-6--guided-pairing): a committed X25519 exchange authenticated by six digits compared on both screens. 2c is unweakened; the pairing code stays required. |

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
