# Review disposition — 22 August 2026

**How each review comment was handled in Draft 1 of the specification.**

| | |
|---|---|
| Status | Record of decisions. Non-normative. |
| Date | 22 August 2026 |
| Rounds covered | **Round 1** — the reviews of the protocol overview and the companion requirements, which produced Draft 1. **Round 2** — the two implementation-team reviews of Draft 1, held in [`reviews/`](reviews/), which produced Draft 2. |

Every point is dispositioned. Points **not** actioned are listed with reasons, because a review that gets a silent partial response is a review that gets repeated.

**Round 2 is in [§5](#5-round-2--pinpointstudio-host-review-of-draft-1) and [§6](#6-round-2--pinpointcapture-mobile-review-of-draft-1).** Sections 1–4 record round 1 and are unchanged except where a later round reopened something.

---

## 1. Protocol review

### 1.1 `nominal_frame_start` has a normative obligation with nowhere to write it

**Accepted in full.** The reviewer was right that the obligation was unsatisfiable, and right that it is not an edge case: `nominal_frame_start` is what every AVFoundation source declares, so it is the default path for the entire mobile side.

| Change | Where |
|---|---|
| `timing.frame_start_to_exposure_offset_ns` added, mandatory iff `convention == nominal_frame_start`, declared explicitly even when zero | [`CORE` §5.7](ppcp-core.md#57-captureprofile) |
| **I17 amended** to name all three inputs, so an implementation can no longer satisfy it and still be wrong | [`CORE` §11](ppcp-core.md#11-invariants) |
| **I22 added** — the presence rule, which is statically checkable and therefore a cheaper test than I17 | [`CORE` §11](ppcp-core.md#11-invariants) |
| Conversion restated with the offset in it, and worked numerically | [`CORE` §6.1](ppcp-core.md#61-canonical-instant) |
| A dedicated silent-failure test whose central assertion is that setting the offset to 0 versus 120 µs changes the output | [`CONF` §4.1](ppcp-conformance.md#41-ct-s1--the-canonical-instant-conversion) |

**One thing resolved beyond the suggestion.** The review's wording, carried from the model, was *"canonical is `t + d/2`, and the profile must additionally declare any fixed offset"* — which leaves where the offset goes ambiguous. The specification states it as `t + frame_start_to_exposure_offset_ns + d/2`. Nominal frame start plus the offset is the actual exposure start; half the exposure past that is mid-exposure. If a reviewer intended different placement, this is the line to argue with.

### 1.2 Device-minted shot issuance has no conformance profile

**Accepted in full, resolved by the reviewer's second option.** A new **Mint** profile separates *issuing* a Shot from *arbitrating between* Candidates.

| Change | Where |
|---|---|
| **Mint** profile added: one Shot per own Candidate, `authority: device`, **no coincidence window**. Requires Core and Detect. | [`CORE` §2.2](ppcp-core.md#22-conformance-profiles) |
| **Arbitrate** narrowed to cross-peer arbitration, host-only. It does **not** depend on Mint — see D5 | [`CORE` §2.2](ppcp-core.md#22-conformance-profiles) |
| **I6 reassigned** from Detect to the two profiles that issue Shots, Mint and Arbitrate. The reviewer's diagnosis was exactly right: a Detect-only peer never issues a Shot, so I6 could not be tested against it. | [`CORE` §11](ppcp-core.md#11-invariants) |
| **I23 added** — the substantive difference made testable: in a zero-host session every Candidate becomes exactly one Shot and no window is applied | [`CORE` §11](ppcp-core.md#11-invariants) |
| v1 worked example corrected to `Core + Capture + Detect + Mint + Offline` | [`CORE` §2.2.3](ppcp-core.md#223-worked-examples) |
| Build order gains step 8, "Shot minting" — the operation v1 actually ships, which the previous ordering omitted entirely | [`CORE` Annex A.1](ppcp-core.md#a1-build-order) |
| Silent-failure test asserts two candidates 10 ms apart give **two** Shots hostless and **one** with a host | [`CONF` §4.4](ppcp-conformance.md#44-ct-s4--the-zero-host-path) |

Naming: "Mint" was kept from the review rather than renamed to "Issue", because the review, the model and the implementation team already use the word.

### 1.3 Profiles gate emission, not comprehension

**Accepted in full.** The reviewer identified a real contradiction between the "depends on Core and nothing else" claim and the worked examples.

| Change | Where |
|---|---|
| Stated normatively: a profile confers the right to **originate**; every conformant peer parses the complete type vocabulary | [`CORE` §2.2.2](ppcp-core.md#222-what-a-profile-confers) |
| "Nothing else" replaced by an explicit per-profile dependency column | [`CORE` §2.2](ppcp-core.md#22-conformance-profiles) |
| **I24 added** | [`CORE` §11](ppcp-core.md#11-invariants) |
| Every message table carries a "Profile to originate" column, and 1a/1b make unimplemented behaviour a non-fatal `profile_not_supported` | [`MSG` §1](ppcp-messages.md#1-scope-and-conventions) |
| Negative conformance: an implementation must be tested on profiles it does **not** declare | [`CONF` §1d](ppcp-conformance.md#1-claiming-conformance), [`CONF` §4.6](ppcp-conformance.md#46-ct-s6--comprehension-versus-origination) |

### 1.4 Cross-session time has no home

**Resolved rather than recorded** — this is the one place the specification goes further than the review asked, and it is the change most worth pushing back on.

The reviewer suggested recording the specific shape of the gap and deferring, on the grounds that cross-device is a later version. The counter-argument taken here: the shape is *fully* determined by the constraints already in the model, and leaving a determined shape unwritten invites two implementations to invent divergent forms of it. Writing it costs one type.

`SessionLink` ([`CORE` §5.17](ppcp-core.md#517-sessionlink)) is a **link**, not a relation and not a rewrite: derived by the importing peer over retained evidence, carrying `basis`, both sigmas and a `confirmed` flag, mutating neither Session (**I25**). It is consistent with the stance that the model carries measurements: a cross-correlation alignment is a measurement over evidence, with an uncertainty, exactly like `estimated_online`.

**Guard rails, because it is untested:** support is OPTIONAL within Offline at v1, the type is marked provisional, and it is tracked as [`CORE` Annex B2](ppcp-core.md#annex-b--open-issues). If the reviewer's instinct to defer is right, deleting it costs one type and one invariant.

### 1.5 Stale invariant count

**Fixed.** Twenty-eight invariants, counted in exactly one place ([`CORE` §11](ppcp-core.md#11-invariants)) and never restated in prose.

**Numbering is stable.** I1–I21 keep their identifiers even where amended (I6 reassigned, I17 extended), because conformance documents get quoted by number and renumbering costs traceability against both reviews for no benefit. New invariants are I22–I28.

### 1.6 "What I checked and found sound"

Recorded and preserved unchanged: the two-channel argument including the explicit "not acceptable: one stream carrying both" ([`CORE` §3.1](ppcp-core.md#31-why-two-channels-is-not-negotiable)); the refusal to compose relations with the replacement obligation stated as a **peer** obligation ([`CORE` §5.4.1](ppcp-core.md#541-the-replacement-obligation)); symmetric declaration including the host-owning-no-Sources corollary ([`CORE` §5.6.1](ppcp-core.md#561-symmetric-declaration), and now a wire-level MUST at [`MSG` §3.3d](ppcp-messages.md#33-declare)); `completeness` and `transfer` as separate axes; readiness as a measurement; and the four silent-failure sites, now six.

---

## 2. Requirements review — protocol-affecting points

### 2.1 REQ-SHOT-6 does not accommodate the launch monitor that actually exists

**Accepted in full, and it changed the specification more than its severity ranking suggested.** The finding is concrete: the launch monitor this project integrates with is a filesystem-watched CSV, which has no Peer, no Timebase and no clock relation, and therefore cannot be a Source in the sense nomination requires.

| Change | Where |
|---|---|
| Nomination restricted to **live** nominators; the two paths tabulated side by side so they cannot be read as one | [`CORE` §8.1](ppcp-core.md#81-nomination) |
| **I26 added** | [`CORE` §11](ppcp-core.md#11-invariants) |
| A wire-level MUST NOT: a record with no peer, timebase or relation is never sent as `candidate` | [`MSG` §7.1c](ppcp-messages.md#71-candidate) |
| The CSV path is `shot_link` with `basis: sequence_alignment` or `manual`, and requires confirmation | [`MSG` §9.3c](ppcp-messages.md#93-shot_link) |
| Sequence A.4 (connected launch monitor) and A.7 (imported record) now show the two paths explicitly | [`MSG` Annex A](ppcp-messages.md#annex-a--interaction-sequences) |

This matters for a v1 screen that is already specified, so it was treated as a v1 defect rather than a clarification.

### 2.2 `MeasuredCapability` cannot be obtained honestly during onboarding

Raised as an implementation note rather than a numbered point; actioned as a protocol change because the fix has to be on the wire.

`MeasuredCapability.method` (`cold_sample` | `sustained`) and `duration_ns` are **mandatory**, absence of `measured` means not measured, and synthesising one is forbidden (**I28**, [`CORE` §5.8](ppcp-core.md#58-capability)). Without `method`, a three-second onboarding sample and a forty-minute sustained figure are the same field, and the cold number quietly becomes the displayed one.

### 2.3 Audio retention arithmetic is computed on shots but attaches to candidates

**No protocol change, deliberately.** The arithmetic error and the privacy-label consequence are application concerns, and a retention cap is exactly the kind of threshold I14 keeps out of the protocol.

What the specification does carry: retention must be *expressible*, absence must be *assertable* rather than a dangling reference ([`CORE` §5.12.1c](ppcp-core.md#5121-candidate-evidence)), and [`CORE` §13c](ppcp-core.md#13-privacy-considerations) states plainly that candidate-attached retention keeps audio for events that were **not** shots and that the count is not bounded by anything the user does — so an implementer cannot read the design as a smaller retention posture than it is.

Tracked as [`CORE` Annex B7](ppcp-core.md#annex-b--open-issues) because the division of responsibility needs the two teams' confirmation, not a protocol change.

### 2.4 The device-capability verdict is host policy applied where no host exists

**No protocol change.** Correctly diagnosed as a companion-app concern. The protocol already provides the two pieces the app needs: a `declare_ack` rejection carrying a machine-readable reason ([`MSG` §3.4](ppcp-messages.md#34-declare_ack)), and the prohibition on any threshold appearing in the protocol at all (I14). Which policy an app applies standalone, and whether it adopts a connected host's, is a product decision.

### 2.5 Diagnostic mode expiry, and `REQ-STATE-6` worth protecting

**No protocol change, both correct.** Diagnostic-mode lifetime is device policy. On the second: the prohibition on state names crossing the wire is now a MUST NOT in two places ([`CORE` §5.15a](ppcp-core.md#515-readiness), [`MSG` §5.2b](ppcp-messages.md#52-arm--disarm--readiness)) rather than a convention, which is the protection the reviewer asked for.

---

## 3. Found while writing, not raised in review

Three defects surfaced only when the model was written out as normative field tables. Recorded here so they are reviewed with the same weight as the review points.

| # | Defect | Resolution |
|---|---|---|
| **W1** | **Candidate-anchored Captures had nowhere to attach.** Audio evidence attaches to Candidates, but `Capture` carried `shot_id`, and a rejected candidate has no Shot — so the retention design the model had already committed to was unwriteable. | `Capture.anchor` is exactly one of `{shot_id}` or `{candidate_id}` (**I27**). `Candidate.id` added — model draft 4 had no identifier on Candidate at all, which also left `Shot.candidates` and every diagnostic tool with nothing to name. |
| **W2** | **The rolling-shutter row formula was ambiguous by one row period** and reverses under `bottom_to_top`. `readout_ns` had no defined endpoints and `r/R` did not say what `r` counts from. | `readout_ns` defined as first-row to last-row exposure start; row instant is `r/(R−1)` with an explicit form for each direction and for `R == 1` ([`CORE` §6.2](ppcp-core.md#62-rolling-shutter)). This is also what the LED rig measures directly. |
| **W3** | **`Stream` referenced a calibration only by implication.** I5 fixes calibration for the stream's lifetime, but nothing carried the id, so "which shots share a calibration" was not readable off the data as claimed. | `Stream.calibration_id` added ([`CORE` §5.11](ppcp-core.md#511-stream)). |

---

## 4. Decisions taken that a reviewer may wish to reverse

Listed separately because they are calls, not corrections. Each is cheap to reverse now and expensive later.

| # | Decision | Alternative | Where |
|---|---|---|---|
| **D1** | **CBOR with text keys** as the encoding | JSON (no binary, float coercion of nanosecond integers), Protobuf (schema compiler in every implementer's build), integer keys (~40% smaller control messages, unreadable in a hex dump) | [`ENC` §9](ppcp-encoding.md#9-design-rationale) |
| **D2** | **An 8-byte frame header even when the transport delimits messages** | Rely on transport boundaries live, and frame only in the bundle — which would make live and file bytes differ and cost the "one parser" property | [`ENC` §3.1](ppcp-encoding.md#31-why-frame-even-when-the-transport-delimits) |
| **D3** | **`SessionLink` defined now** | Record the gap and defer, as the review suggested | [§1.4](#14-cross-session-time-has-no-home). **Upheld in round 2**: the original reviewer withdrew the objection, and the host would trade it only against the issue-timing hole, which Draft 2 fixes anyway. |
| **D4** | **`Mint` requires `Detect`** | Make Mint independent, allowing a peer to issue Shots from Candidates it did not produce — which is arbitration by another name | [`CORE` §2.2](ppcp-core.md#22-conformance-profiles) |
| **D5** | **`Arbitrate` does *not* require `Mint`**, although both issue Shots | Make Arbitrate depend on Mint on the grounds that both issue Shots — rejected, because it would force a camera-less third-party host to declare Detect and break the review's own worked example | [`CORE` §2.2](ppcp-core.md#22-conformance-profiles) |
| **D6** | **An absent capture is answered as `capture_announce`, not `error`** | Answer `error` / `outside_buffer`; rejected because an absent capture is a *result* that must be asserted (I10), and an error is not a result | [`MSG` §7.3b](ppcp-messages.md#73-capture_request) |
| **D7** | **A stream-desynchronising frame length is fatal; every other malformation is not** | Uniform handling; rejected because dropping a live capture session over one over-long string would violate "capture degrades last" | [`ENC` §8](ppcp-encoding.md#8-limits) |


---

# Round 2 — the Draft 1 implementation reviews

Both first-party teams reviewed Draft 1 and both returned **approve to implement**. The reviews are in [`reviews/`](reviews/) and are worth reading in full; this section records what was done with each finding.

The two reviews barely overlap, which is the useful thing about having taken them separately. The mobile review is about what the wire costs to produce; the host review is about whether the model describes the system that exists. Between them they found one defect the conformance suite would have certified, one interoperability hazard between two otherwise-conformant peers, and one place where a round-1 fix had been made against a picture of the integration that the code does not support.

---

## 5. Round 2 — PinPointStudio, host review of Draft 1

### 5.1 F1 — I23 turned every diagnostic Candidate into a Shot

**Accepted in full. This is the most valuable finding in either review**, because Draft 1 did not merely permit the wrong behaviour — it required it, and [`CONF` §4.4](ppcp-conformance.md#44-ct-s4--the-zero-host-path) asserted it as a passing test.

The chain: I23 required every Candidate to become exactly one Shot; I8 and the retention design positively encourage emitting a candidate for the ball-into-screen transient, roughly 9 ms after impact at 3 m; `CT-S4` asserted that two candidates 10 ms apart produce two Shots. A correctly-implemented offline device therefore minted **two Shots for one swing** and passed conformance for it. Its only escape was to suppress the second candidate, destroying exactly the evidence candidate-attached retention exists to preserve.

The reviewer's diagnosis of *where* the defect was is what makes the fix cheap: not the absence of the coincidence window, which is right and stays, but the **identity** between Candidate and Shot.

| Change | Where |
|---|---|
| A Mint peer **promotes** a subset of its own Candidates. Every Candidate is emitted and retained with its evidence, promoted or not. | [`CORE` §8.3](ppcp-core.md#83-the-zero-host-regime) |
| **I23 rewritten** to constrain the shape — no window, one Candidate per Shot — and not the choice | [`CORE` §11](ppcp-core.md#11-invariants) |
| **I8 amended** to cover unpromoted candidates, and extended to Mint, which is where promotion happens | [`CORE` §11](ppcp-core.md#11-invariants) |
| Promotion is named as detector tuning and therefore I14 territory | [`CORE` §8.3c](ppcp-core.md#83-the-zero-host-regime) |
| `CT-S4` assertion 2 rewritten, with the old wording and why it was wrong kept in the test's own text | [`CONF` §4.4](ppcp-conformance.md#44-ct-s4--the-zero-host-path) |
| The Mint/Arbitrate contrast table gains a **Selection** row, so promotion and coincidence are visibly different operations | [`CORE` §2.2.1](ppcp-core.md#221-why-mint-exists) |
| Sequence A.6 now shows both candidates recorded and one promoted | [`MSG` Annex A.6](ppcp-messages.md#a6-offline-capture--the-zero-host-regime) |

The reviewer's suggested wording for I23 was adopted almost verbatim.

### 5.2 F2 — the protocol never modelled when a host may issue

**Accepted in full.** Two distinct problems under one gap, and the second is a genuine interoperability hazard: a peer that nominated to a live, healthy host had no deadline after which to conclude no Shot was coming, so two conformant implementations could disagree about whether a Shot exists at all.

| Change | Where |
|---|---|
| **`Session.issue_hold_ns`** added, default 200 ms, declared in `session_open` | [`CORE` §5.10](ppcp-core.md#510-session), [`MSG` §4.1](ppcp-messages.md#41-session_open) |
| A host SHOULD NOT issue before the hold expires on its earliest contributing Candidate | [`CORE` §8.2h](ppcp-core.md#82-arbitration) |
| **I32** — a nominating peer MUST NOT mint before `issue_hold_ns` plus one heartbeat interval has elapsed with no `shot` | [`CORE` §11](ppcp-core.md#11-invariants) |
| The two quantities are stated as different things — a tolerance and a deadline — with the reviewer's 40 ms-inside-200 ms example | [`CORE` §8.2](ppcp-core.md#82-arbitration) |
| `CT-I32` added, including that two peers with the same parameters agree on whether a Shot exists | [`CONF` §3](ppcp-conformance.md#3-the-invariant-test-matrix) |
| Annex B8 now asks for both numbers to be measured separately | [`CORE` Annex B](ppcp-core.md#annex-b--open-issues) |

The point that one field was silently doing two jobs is the reason Q4 could not have been settled as asked: a rig measurement of "the coincidence window" would not have known which quantity it was estimating.

### 5.3 F4 — the hardened launch-monitor path does not match the launch monitor that exists

**Accepted in full, and `Annex B4` reopened as requested.** This is round 1's own principle arriving early: the change belongs in the specification first, and the specification was reasoning from a design screen while the code says something different.

Round 1 established correctly that a CSV row is not a Candidate. The resolution it received assumed a **multi-record export**, and the integration that exists is a two-line file — one header row, one shot — **rewritten in place**, carrying no trustworthy timestamp and a foreign counter unrelated to anything of ours, attributed by arming a slot when a swing is detected and letting the next arriving reading claim it.

So `basis: sequence_alignment` is inapplicable to the only launch monitor integrated today: there is no sequence in a file holding one row.

| Change | Where |
|---|---|
| **A third shape**: a live record with an observing Peer and **no clock**, attributable by arrival order | [`CORE` §8.1](ppcp-core.md#81-nomination) — three-column table |
| `ShotLink.basis: arrival_pairing`, plus `foreign_system` for a record that is not a PPCP peer | [`CORE` §5.16](ppcp-core.md#516-shotlink) |
| Confirmation is required for **retrospective** bases only; `arrival_pairing` is asserted live because no later evidence exists | [`CORE` §5.16b–c](ppcp-core.md#516-shotlink), [`MSG` §9.3c](ppcp-messages.md#93-shot_link) |
| `sequence_alignment` MUST NOT be used for a single-record source | [`CORE` §8.5g](ppcp-core.md#85-reconciliation) |
| A peer MUST NOT synthesise a Timebase to route a clockless record through nomination | [`CORE` §8.1e](ppcp-core.md#81-nomination) |
| **8.1f**: a live-rewritten single-record source produces nothing to reconcile later — in a hostless session that reading is simply gone, and no product flow may promise otherwise | [`CORE` §8.1f](ppcp-core.md#81-nomination) |
| **Annex B4 reopened.** The third shape is resolved; whether `kind: launch_monitor` is right for the *connected* case awaits a real device | [`CORE` Annex B](ppcp-core.md#annex-b--open-issues) |

The reviewer offered a fallback — narrow the language rather than add the shape. The shape was added instead, because the fallback leaves an implementer with a live signal and only two paths, neither of which fits, and the predictable outcome is a fabricated timebase.

### 5.4 F5 — the timing argument rests on a host capability that does not exist

**Accepted for the part that is ours.** The specification justified the exposure-convention contract by saying a missed conversion "corrupts the consumer's own bias estimator" — and the host has no clock-bias estimator; it is item 1 of an unimplemented proposal.

The contract is right regardless, and now says why without depending on anything existing: the error is exposure-dependent, so it moves with the light and cannot be calibrated out as a constant. A consumer that models clock error absorbs it and misattributes it; a consumer that does not carries it as unexplained systematic error. Neither can detect it from the data. [`CORE` §6.1](ppcp-core.md#61-canonical-instant)

The remaining items in the F5 table — fusion P1, camera intrinsics, stereo extrinsics, HEVC, the untagged buffer timestamp — are claims in the **requirements document**, which is not in this repository. **Referred to its owner.** The decision the reviewer wanted recorded, that the host will be conformant at its protocol edge and remain single-clock internally, is compatible with this specification and needs nothing from it: nothing here requires a peer's internal representation to carry timebase identity, only its wire representation.

### 5.5 F8 — host identifiers are not of the kind the protocol requires

**Accepted, minimally.** A session id that is a filesystem directory path and a shot id that is an ordinal survive neither idempotent re-import nor re-indexing.

`Id` now carries a stability rule: an identifier minted by a peer MUST NOT be derived from mutable local state, because [§8.5c](ppcp-core.md#85-reconciliation) keys idempotency on `Session.id` plus the minting `Peer.id`. [`CORE` §5.1a–b](ppcp-core.md#51-notation-and-primitive-types)

How the host maps its internal identifiers onto conformant ones is its own decision, and it should be taken alongside the persistence question the reviewer flagged.

### 5.6 F3, F6, F7 — host work items

**No specification change; all three correctly diagnosed as host cost rather than protocol defect.** Recorded here because the reviewer asked that they be visible rather than discovered.

- **F3 — the arbiter discards candidates and models three fixed modalities.** I8 is violated at the front door (a refractory that rejects on admission) and at the back (discard on decide), and a phone is a *second acoustic* nominator that collides with the host microphone for one slot. That last one destroys the single thing the phone's microphone is for. **One specification consequence was taken**: `CT-I8` now explicitly asserts that two Candidates of the same `basis` from different peers are both retained, so the collision is caught by the suite rather than in the field. [`CONF` §3](ppcp-conformance.md#3-the-invariant-test-matrix), and a new interoperability pairing in [`CONF` §5](ppcp-conformance.md#5-interoperability).
- **F6 — the host's shot pipeline is serialised** and cannot admit a shot while one is processing, against a protocol whose whole two-channel design assumes events keep arriving while payloads lag. Host re-architecture. Noted in [`CORE` Annex A.1](ppcp-core.md#a1-build-order): the bundle-before-live build order is also what lets a host consume real sessions before doing this work.
- **F7 — `REQ-HOST-1` names the wrong seam**, and §15 of the requirements is two requirements long against eight identified work items. That is the requirements document. **Referred to its owner**, with the reviewer's conclusion endorsed: a PPCP peer is a session-layer participant, not a frame source, and putting it behind a live-push camera interface would either strip what the protocol carries or smuggle a session model in behind a camera.

### 5.7 Answers to Q1–Q7 and the requirements' open decisions

Q1, Q3, Q6 confirmed. Q2 kept. Q5 settled — see [§7](#7-q5-settled). Q4 sharpened rather than settled, per [§5.2](#52-f2--the-protocol-never-modelled-when-a-host-may-issue). Q7: agreement that RV-2 comes before any release shipping a QR, which is now recorded in [`PPCP-RV` §5.1](ppcp-rv-scope.md#51-the-real-deadline-is-earlier-than-ppcp10).

The requirements document's OPEN-3 through OPEN-7 are its owner's to close. The reviewer's position on **OPEN-6** — that v1 ship offline-only rather than tethered-only — is consistent with this specification's build order and is worth the requirements owner weighing: it is the path with no clock pressure, and it is the one a host can consume before doing the F3 and F6 work.

---

## 6. Round 2 — PinPointCapture, mobile review of Draft 1

### 6.1 PPC-1.1 — online time-of-flight estimation conflicted with I5

**Accepted in full. Resolved by the reviewer's second option rather than their first**, and the reasoning for departing is set out below because the reviewer stated a preference.

The conflict was real and Draft 1 contained all three sides of it: time of flight lives in `Calibration`; the estimate must converge online across a session because the microphone-to-ball distance is user-chosen and the golfer will not measure it; and a Calibration change closes every Stream referencing its Source. A fifty-shot session implied up to fifty `stream_close`/`stream_open` cycles on the audio Stream. The reviewer also noticed that `Calibration.method` already offers `estimated_online` — an enum value that, under I5, can be set once and never updated.

**Option 1 (exempt `estimated_online` from I5) was not taken.** It makes `Stream.calibration_id` stop identifying a fixed value, so reproducing a past conversion would require a time-indexed calibration history, and the property I5 exists to provide — that a Capture's geometry is recoverable from the Stream contract alone — would be gone. The churn would be fixed by weakening the invariant that prevents a different and worse problem.

**Option 2 was taken:** the *applied* correction moves onto the Candidate, with its own uncertainty, which is where it is consumed and where its convergence is visible. `Calibration` keeps what is genuinely fixed — a surveyed or solved position, where one exists. I5 is untouched, there is no stream churn, and the mobile side emits nothing it was not already emitting.

| Change | Where |
|---|---|
| `Calibration` `kind: position` carries the **surveyed** geometry; the applied correction does not live there | [`CORE` §5.9b](ppcp-core.md#59-calibration) |
| **5.9c**: a peer MUST NOT close and reopen a Stream to publish a refined online estimate | [`CORE` §5.9c](ppcp-core.md#59-calibration) |
| The reasoning, including why option 1 was declined, is in the specification rather than only here | [`CORE` §5.9](ppcp-core.md#59-calibration) |

### 6.2 PPC-1.2 — `tof_correction_ns` was the one estimate with no sigma

**Accepted in full**, and it resolves cleanly alongside 6.1 as the reviewer predicted.

`tof_correction` is now an `Estimate` — `{ value_ns, sigma_ns }`, both mandatory together (**I29**). The encoding makes it structural: an encoder cannot emit the value without the sigma, in the same way `Instant` makes I1 structural. [`CORE` §5.12](ppcp-core.md#512-candidate), [`ENC` §4.1e](ppcp-encoding.md#41-composite-types), `CT-I29`.

The reviewer's argument that undoing the correction is not a substitute is the right one and is now in the specification: undoing recovers the raw timestamp but says nothing about how far to trust the corrected one, and a consumer that undoes every correction has discarded work the nominating peer was better placed to do.

### 6.3 PPC-2.1 — `capture_announce` was called small and was not

**Accepted in full**, and both of the reviewer's suggestions were taken rather than one, because they solve different halves.

The measurement was the useful part: ~44 KB at 1080p150 for three seconds, ~70 KB at 240 fps with per-frame intrinsics — against a `sync_probe` of 95 bytes, on the one message whose immediacy the entire two-channel design exists to protect.

| Change | Where |
|---|---|
| `AchievedCapability` splits into **`AchievedSummary`** (control, in `capture_announce`) and **`AchievedFrames`** (with the payload) | [`CORE` §5.8](ppcp-core.md#58-capability) |
| **I30** — per-frame series never travel on the control channel | [`CORE` §11](ppcp-core.md#11-invariants) |
| Any per-frame array whose value was constant MAY be sent as a **scalar**; `frames.ns` may not, because I2 | [`CORE` §5.8f](ppcp-core.md#58-capability), [`ENC` §4.1d](ppcp-encoding.md#41-composite-types) |
| `payload_begin` carries `achieved_frames`; `capture_update` MAY, as a fallback for a payload that will never transfer | [`MSG` §8.3g](ppcp-messages.md#83-the-payload_-family), [`MSG` §8.2b](ppcp-messages.md#82-capture_update) |
| **`Capture.digest` MAY be absent from the announce**, resolving the reviewer's separate observation that a whole-payload hash would make the immediate message wait for the clip to be extracted | [`MSG` §8.1e](ppcp-messages.md#81-capture_announce) |
| `CT-I30` measures the announce and asserts the scalar form under a lock | [`CONF` §3](ppcp-conformance.md#3-the-invariant-test-matrix) |

The scalar form is the smaller change and handles the locked case the application actually ships; the split is the structural one and handles the unlocked case, which [§6.1c](ppcp-core.md#61-canonical-instant) forbids assuming away.

### 6.4 PPC-2.2 — a declared offset of zero is indistinguishable from an unmeasured one

**Accepted in full, and extended to a third field.** The reviewer identified this as I28's reasoning applied elsewhere, which is exactly right: without `MeasuredCapability.method` a cold sample and a sustained figure are the same number, and without a provenance marker an unmeasured offset and a measured one are the same number — except that this one silently biases every cross-source comparison rather than merely overstating a frame rate.

`Provenance` — `assumed` \| `vendor` \| `measured` — now attaches to `frame_start_to_exposure_offset_ns`, to `rolling_shutter.readout_ns` as the reviewer suggested, and to per-frame exposure (**I31**). A peer MUST NOT claim a stronger provenance than it has, and a `measured` value SHOULD carry a sigma. [`CORE` §5.7](ppcp-core.md#57-captureprofile), `CT-I31`.

This is also now the fourth silent-failure site that a self-tested implementation passes by accident: an unmeasured `0` is correct relative to another implementation that also declared `0`. [`CONF` §2c](ppcp-conformance.md#2-required-test-infrastructure)

### 6.5 PPC-3 — per-frame exposure may not be available per sample buffer

**Answered in the specification, as the reviewer asked.** The question was fair: the requirement is per-frame exposure, the platform offers a device-level property that must be sampled, and the specification simultaneously forbids assuming the exposure lock held.

`AchievedFrames.exposure_provenance` distinguishes three honest positions: `per_frame` where the pipeline attaches the value to the frame, `sampled` where a device property is read once per frame, and `locked_constant` where one value applied throughout under a lock. A peer MUST NOT claim `per_frame` where the platform does not supply it; whether `sampled` is accurate enough is the consumer's policy, not the protocol's. [`CORE` §5.8](ppcp-core.md#58-capability)

So the honest conformance position for an unlocked source on such a platform is `sampled`, declared. That is conformant. Its accuracy is unquantified and is now [`CORE` Annex B10](ppcp-core.md#annex-b--open-issues).

The reviewer's other implementation notes need nothing from the specification and are recorded because they are the cost of provisions that are correct: preallocated per-frame storage sized to the window at 150 fps, and two channels being comfortable on the platform — the latter noted specifically so it is on record that the transport requirement most expensive to discover late is not contested.

### 6.6 Answers to Q1–Q7

Q1 and Q3 agreed without reservation. Q2 accepted with the original objection withdrawn. Q6 confirmed, with the request that [`CORE` §13c](ppcp-core.md#13-privacy-considerations) — the sentence saying the candidate count is not bounded by anything the user does — be kept. **It is kept**, unchanged, and it should survive future editing for the reason given: it is the sentence that makes the retention posture reviewable.

**Q4** could not be settled, and the reviewer contributed the measurement that matters most: the case to measure is the **adjacent bay**. A shot 4 m away arrives ~12 ms later, so two golfers hitting within 40 ms would merge into one Shot — a failure that produces a *wrong* shot rather than a missing one, which is the worse direction. Annex B8 now asks for a floor and a ceiling rather than a single number.

**Q7** is, from the mobile seat, the highest-priority gap in the whole document set, ahead of anything else in the review — because a guess has already shipped. See [§8](#8-the-rendezvous-deadline-moved).

---

## 7. Q5 settled

Both teams answered compatibly, so the question is closed rather than carried.

- **Two MINOR versions back or twelve months, whichever is longer.** The host proposed it; the mobile constraint — that the window be expressed in released versions rather than elapsed time, because release cadence is not the application's to control — is satisfied by the "whichever is longer" form, which can only help the slower end.
- **`hello_accept.min_version`** states the window on the wire, so a peer learns it before it is refused.
- **`error` / `unsupported_version` MUST carry the sender's supported range**, so the user is told *which end is stale*. The host asked for this; the code is fatal, and without it a fatal error is also an uninformative one.
- The mobile side's request that the **device** state its own dialect and degrade gracefully facing an unknown host is already satisfied by `hello.versions` and the selection rule, now with a SHOULD that a peer surfaces both ranges rather than a generic failure.

[`CORE` §10.1](ppcp-core.md#101-version-negotiation), [`MSG` §3.2](ppcp-messages.md#32-hello_accept). Closes Annex B6.

---

## 8. The rendezvous deadline moved

Both teams reached the same conclusion independently and it changes the schedule rather than the specification.

- **A service type has already shipped by guess.** The mobile application declares `_ppcp._tcp` in its bundle, chosen with nothing to reference, and that string goes to app review as part of the bundle. RV-1 should **ratify** it rather than pick another name.
- **RV-2 — the QR payload — is gated by the first store submission, not by `ppcp/1.0`.** A printed code outlives a release, and the payload is parsed with no chance to negotiate first. A version marker in its first field is the one item that cannot be fixed afterwards.

Recorded in [`PPCP-RV` §5.1](ppcp-rv-scope.md#51-the-real-deadline-is-earlier-than-ppcp10).

---

## 9. Round 2 decisions a reviewer may wish to reverse

As with [§4](#4-decisions-taken-that-a-reviewer-may-wish-to-reverse), these are calls rather than corrections.

| # | Decision | Alternative | Where |
|---|---|---|---|
| **D8** | **Time of flight resolved by moving the correction to the Candidate**, not by exempting `estimated_online` calibrations from I5 | The reviewer's stated first preference. Rejected because it makes `Stream.calibration_id` stop identifying a fixed value and would require a time-indexed calibration history to convert a past candidate | [§6.1](#61-ppc-11--online-time-of-flight-estimation-conflicted-with-i5) |
| **D9** | **A third nominator shape added**, rather than narrowing the launch-monitor language | The reviewer's own fallback. Rejected because it leaves an implementer with a live signal and two paths that do not fit, and the predictable outcome is a fabricated timebase | [§5.3](#53-f4--the-hardened-launch-monitor-path-does-not-match-the-launch-monitor-that-exists) |
| **D10** | **`arrival_pairing` links do not require confirmation** | Require confirmation as every other basis does. Rejected because there is no later moment at which the evidence improves — but the mis-pair case is real and is now Annex B9 | [`CORE` §8.5f](ppcp-core.md#85-reconciliation) |
| **D11** | **Both of the announce-size fixes taken**, split *and* scalar form | Either alone. The scalar form handles the locked case that ships; the split handles the unlocked case the specification forbids assuming away | [§6.3](#63-ppc-21--capture_announce-was-called-small-and-was-not) |
| **D12** | **Promotion policy left entirely to the peer** | Give the protocol a promotion hint, or a minimum inter-shot interval. Rejected as exactly the threshold I14 keeps out — but it does mean two conformant devices given identical audio may report different shot counts, which is a consequence worth being sure about | [`CORE` §8.3c](ppcp-core.md#83-the-zero-host-regime) |
