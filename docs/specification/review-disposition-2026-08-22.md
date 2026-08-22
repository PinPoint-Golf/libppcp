# Review disposition — 22 August 2026

**How each review comment was handled in Draft 1 of the specification.**

| | |
|---|---|
| Status | Record of decisions. Non-normative. Complete: the specification is **approved**. |
| Date | 22 August 2026 |
| Rounds covered | **Round 1** — the reviews of the protocol overview and the companion requirements, which produced Draft 1. **Round 2** — the reviews of Draft 1, which produced Draft 2. **Round 3** — the reviews of Draft 2, which produced Draft 3. **Round 4** — the closing reviews of Draft 3, which produced the approved text. All six team reviews are in [`reviews/`](reviews/). |

Every point is dispositioned. Points **not** actioned are listed with reasons, because a review that gets a silent partial response is a review that gets repeated.

**Round 2 is in [§5](#5-round-2--pinpointstudio-host-review-of-draft-1) and [§6](#6-round-2--pinpointcapture-mobile-review-of-draft-1); round 3 is in [§10](#10-round-3--the-draft-2-reviews).** Sections 1–4 record round 1 and are unchanged except where a later round reopened something.

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

Q1, Q3, Q6 confirmed. Q2 kept. Q5 settled — see [§7](#7-q5-settled). Q4 sharpened rather than settled, per [§5.2](#52-f2--the-protocol-never-modelled-when-a-host-may-issue). Q7: agreement that RV-2 comes before any release shipping a QR, which is now recorded in [`PPCP-RV` Annex A1](ppcp-rv.md#annex-a--decisions-and-alternatives).

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

Recorded in [`PPCP-RV` Annex A1](ppcp-rv.md#annex-a--decisions-and-alternatives), and [Draft 1](ppcp-rv.md) was written while the Draft 2 reviews were out. Nothing in it is agreed.

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


---

# Round 3 — the Draft 2 reviews

Both teams reviewed again and both returned **approve to implement** again. The host review states that Draft 3 carrying its four findings and five consistency items leaves it with no further findings and that PinPointStudio will build against it as it stands.

## 10. Round 3 — the Draft 2 reviews

### 10.1 The finding both teams made independently

**Accepted in full, from both angles, and it is the most important thing in this round.**

The two reviews approach it differently and the fixes are complementary rather than alternative, which is why both were taken.

**The host's R1 — the fix reintroduced the defect it was fixing.** Draft 2 closed the host's own F2 with `issue_hold_ns` and I32: after the deadline, with no `shot`, a nominating peer MAY mint. But **nothing obliges a host to answer a Candidate it declines.** A host that correctly rejects a dropped club, a club-on-mat or a shot in the next bay issues nothing at all, so the branch that fires on the device is the silent one — and it fired for *every* candidate the host had declined, including ones the device's own detector never believed. That is precisely the defect Draft 2 had just removed from the hostless regime, reappearing in the live one through the clause that fixed its sibling. `CT-I32` asserted it as a pass.

**The mobile team's 1.1 — the deadline narrows a race rather than closing it.** A host that issues at the deadline over a link that then stalls produces a `shot` arriving after the device has minted: two Shots for one swing, both immutable under I7, both unmergeable under I9, and deliberately no `withdraw` or `supersede` message to reach for. Two rows in the session library for one swing, and the same clip transferred twice.

The combined resolution, none of which needs a new message:

| Change | Source | Where |
|---|---|---|
| The post-deadline mint is conditioned on the peer's **own promotion policy**. Host silence does not promote a Candidate the peer did not believe. | Host R1(a) | [`CORE` §8.2i](ppcp-core.md#82-arbitration) |
| The host's issue window is **bounded at both ends**, so it cannot overlap the mint window | Host R1(b), adapted | [`CORE` §8.2h](ppcp-core.md#82-arbitration) |
| A minting peer sends `shot` immediately | Mobile 1.1 | [`CORE` §8.2j](ppcp-core.md#82-arbitration) |
| A host that receives a device-minted `shot` for a Candidate it holds **attaches to it** rather than issuing its own | Mobile 1.1 | [`CORE` §8.2k](ppcp-core.md#82-arbitration), **I35** |
| Where both fire anyway, they are **linked** by `basis: shared_candidate`, never withdrawn | New | [`CORE` §8.2l](ppcp-core.md#82-arbitration) |
| §7.1 gains the third row so the state is described rather than merely permitted | Host R1(c) | [`CORE` §7.1](ppcp-core.md#71-roles) |
| `CT-I32` gains the negative half; `CT-S4` gains a live-regime assertion | Host R1(d) | [`CONF` §3](ppcp-conformance.md#3-the-invariant-test-matrix), [§4.4](ppcp-conformance.md#44-ct-s4--the-zero-host-path) |

**One departure, stated because the host wrote the clause.** R1(b) proposed the host issue *"no earlier than `issue_hold_ns` … and no later."* Taken literally that makes a host non-conformant for a few milliseconds of scheduling jitter. The bound taken is *no later than the mint deadline* — `issue_hold_ns` plus one heartbeat interval — which is what makes the two windows non-overlapping, is the property R1(b) was reaching for, and leaves a second of slack for a real system.

**Why the direction of 8.2k is forced rather than chosen.** The device's Shot may already anchor an extracted Capture. Making the host win would require withdrawing a Shot that Captures reference, which the model has no room for and should not grow. The accepted cost is that in this rare case `t0` is the device's rather than the host's arbitrated value — the worse estimate — and it arises only when a host has already exceeded 8.2h. One slightly worse `t0` is a much smaller harm than two Shots for one swing.

### 10.2 Host R3 — `Candidate.at` had no stated convention

**Accepted.** Present since Draft 1 and missed by everyone, including two prior reviews.

§8.2a told the host to convert every Candidate using the relation **and** the canonical-instant conversion. That conversion needs `d`, the exposure of *that frame*, and **a Candidate carries no frame reference and no exposure**: `evidence_ref` points at a Capture, and for an acoustic candidate that Capture is the audio window.

For acoustic it was harmless by accident — a microphone profile has no `format`, so §6.1d fixes `convention: mid`. For a `motion` candidate from a camera Source declaring `nominal_frame_start` it was not, and the error is about 1 ms: comfortably inside any plausible coincidence window, so **arbitration still succeeds and nothing looks broken**, while `t0` carries a systematic error that moves with exposure. That is the exact signature §6.1 spends a page warning about, in the one place the conversion was not being applied consistently.

The nominator converts, because it is the only party holding the frame and its exposure. `Candidate.at` is the canonical instant ([`CORE` §5.12e](ppcp-core.md#512-candidate)), §8.2a drops the clause, the sequence note is corrected, and **I33** and `CT-I33` make it testable.

**Slightly beyond what was asked:** `canonical_correction_ns` was added so the correction is recoverable, on the reviewer's own reasoning that this is *"the same division as acoustic time of flight: the observer corrects, and the correction is visible."* `tof_correction` is visible; this now is too.

### 10.3 Host R2 — a contradiction between two normative documents

**Accepted.** `CORE` I30 and 5.8g forbade `AchievedFrames` on the control channel; `MSG` 8.2b permitted it on `capture_update`, citing the invariant it violated as its authority. An implementer coding to one would refuse what the other requires, and `CT-I30` tested only `capture_announce`, so neither was caught.

The reviewer's diagnosis is right: **the exception is correct and the invariant needed narrowing.** A `complete` + `failed` capture is a range session whose link died; the frames are never coming, and the timeline is what tells a consumer what it lost. I30's real intent was that the *immediate correlation message* stays small, not that control never carries a series. I30 and 5.8g now say so, and `CT-I30` asserts the exception applies only where `transfer` is `failed`.

### 10.4 Host R4 — the scalar form was ambiguous for `intrinsics`

**Accepted, wording adopted.** The major-type rule works for `exposure_ns` and `iso` and fails for `intrinsics`, whose element type is itself a CBOR array — so `[f64 × 9]` and `[[f64 × 9], …]` are both major type 4. That is the field most likely to *be* constant, because focus is locked for a session, and therefore the field the scalar form was most worth having for. [`ENC` 4.1d](ppcp-encoding.md#41-composite-types) now disambiguates by the type of the first element, and `CT-I30` exercises it specifically.

### 10.5 Mobile 1.2 — `Capture.digest` identity had a hole

**Accepted.** 8.5c named the digest as Capture identity, and two ordinary cases never have one: `completeness: absent` has no payload to hash, and a `complete` + `pending` Capture may reach a bundle before hashing — a case Draft 2's own 8.1e deliberately permits, at this reviewer's request, so the announce need not wait for the clip.

Absent captures are the most important content of a partial session — they are what I10 exists to make assertable — and identifying them by a hash they cannot have would have duplicated them on exactly the second import the rule exists to make safe. Identity is now `Capture.id` scoped by `Session.id` and the owning `Peer.id`; the digest is a **content** check where present. **I34** and `CT-I34`.

### 10.6 Mobile 1.3 — arbitration parameters mandatory in sessions that never arbitrate

**Accepted.** `coincidence_window_ns` and `issue_hold_ns` were `Card. 1` while `heartbeat_interval_ms` in the same table was correctly conditional. Both are now present **if and only if** the Session has a host ([`CORE` §5.10e](ppcp-core.md#510-session)) — which is I23 expressed structurally rather than in prose, and which stops every range bundle carrying two numbers nothing consults. The reviewer's second reason is the decisive one: a mandatory field cannot be made optional after 1.0.

### 10.7 Mobile §2 — the coincidence window may not admit a single value

**The measurement design is accepted; the field change is not made, and the reason is that it is not needed.**

The contribution is a good one: if a live external nominator disagrees with an acoustic one by more than the adjacent-bay separation, no single window is both wide enough to pair them and narrow enough to avoid merging two golfers. The reviewer was careful to mark the 41 ms figure as illustrative design copy rather than a measurement, which is the right way to raise it.

The proposed insurance was to let the field carry a scalar **or** a per-basis map now, on the grounds that scalar → map is a breaking change. **It is not.** Only an *arbitrating host* consumes the window; a device never reads it. So an optional per-`basis` override can be added in a MINOR version, ignored by peers that do not implement it, with no divergence — because there is only ever one implementation applying it. Adding a variant type today for a change that is additive tomorrow buys nothing and costs a decoder branch.

What **is** accepted, and is the actionable half, is that **the measurement design differs**: pooling the nominator classes would produce a number that answers neither question. [`CORE` Annex B8](ppcp-core.md#annex-b--open-issues) now asks for the floor per class — acoustic-to-acoustic between a device mic and a host mic after time-of-flight correction, which the reviewer specifically requested and which is the tightest and most important case; acoustic against a live external nominator; and the adjacent-bay ceiling — and records that if the second exceeds the third, the resolution is a per-basis override.

### 10.8 Host §3 — `ShotLink.confirmed` carried two epistemic states

**Accepted.** Before `arrival_pairing`, `confirmed` meant *a human agreed*. After it, it also meant *a machine asserted this live and no human will ever be asked*. One boolean was doing what `claimed`/`measured`/`achieved`, `cold_sample`/`sustained` and `assumed`/`vendor`/`measured` each do with an enumeration — and this document refuses that conflation everywhere else.

`ShotLink.confirmed_by` — `observer` \| `user` — accompanies `confirmed: true`, and a retrospective basis may only be `user` ([`CORE` §5.16e–f](ppcp-core.md#516-shotlink)). **Annex B9 closes**: the mis-pair the reviewer describes is now visible to a consumer before it happens rather than indistinguishable after.

### 10.9 Mobile §3 — 5.8d was unsatisfiable for an absent capture

**Accepted.** 5.8d required per-frame exposure on any camera Capture; a Capture of `completeness: absent` has no frames and, since `AchievedFrames` travels with the payload, no `AchievedFrames` at all — so a conformance test for 5.8d or I17 would have failed on correctly-formed data. Now conditioned on the Capture having frames.

### 10.10 Host §2 — five consistency items

All accepted.

| | Item | Fix |
|---|---|---|
| 1 | Silent-failure count disagreed in three places, and I31 — the site called most dangerous — had a matrix test but no silent-failure test | **`CT-S7` written.** Seven sites, seven tests, four of which pass by accident when self-tested. Counts corrected in `CONF` and `README` |
| 2 | `CT-S1` still named `achieved.exposure_ns`, and tested only the varying-exposure path while the product ships with exposure locked | Field name corrected; a sixth assertion added that the scalar form and an equivalent constant array give identical canonical instants |
| 3 | §6.1's preamble and I17 still named `Capture.achieved` | Both corrected to `achieved_frames` |
| 4 | 10.1e stated the support window as a fixed MUST, making a more generous host non-conformant | Now **at least** two MINOR back or twelve months |
| 5 | 10.1e said "a peer states its window in `hello_accept`", but an initiator sends `hello` | Now "a responder", with the initiator's `versions` list named as its window |

### 10.11 Host §6 — the same failure mode twice

**Accepted and promoted, and this is the most valuable observation in the round.**

Twice now an invariant has constrained a *choice* rather than a *shape*, with a conformance test written to match, so the suite certified the defect instead of catching it: I23 in Draft 1, and I32 in Draft 2 — in the clause that fixed I23's sibling.

The tell is the same both times: a MUST that names what an implementation should **decide** rather than what its output should **look like**. [`CORE` §11.1](ppcp-core.md#111-the-rule-for-writing-an-invariant) now states this as a rule for writing invariants in this document, with both defects tabulated as the evidence, and requires a new MUST to be read against it before its test is written. `CONF` §6 keeps the row and points at it.

### 10.12 Confirmed, no change needed

- **D8** — time of flight on the Candidate. Both teams now endorse it; the mobile reviewer withdrew the stated preference and noted that option 2 also makes the sigma meaningful, which option 1 would have prevented.
- **D12** — promotion policy left to the peer. Both content. The host notes that two devices with different detectors already report different *candidates*, and that a protocol-level promotion hint would be a threshold that someone would eventually tune the product with.
- **D10 / arrival pairing without confirmation** — the call stands; `confirmed_by` addresses the reviewer's actual concern without reversing it.
- **`exposure_provenance`** — the mobile team states its position: `sampled` for any unlocked source, `locked_constant` under the exposure lock, and `per_frame` not claimed until the platform is verified. That is exactly what the field was added to make expressible.

---

## 11. Round 3 decisions a reviewer may wish to reverse

| # | Decision | Alternative | Where |
|---|---|---|---|
| **D13** | **The host's issue window is bounded at the mint deadline**, not at `issue_hold_ns` exactly | The host reviewer's literal wording, "no later". Rejected because a few milliseconds of scheduling jitter would make a correct host non-conformant, and the non-overlap property is what the clause was for | [`CORE` §8.2h](ppcp-core.md#82-arbitration) |
| **D14** | **The device's Shot wins the residual race**; the host attaches to it | The host's Shot wins, which would need a withdraw or supersede message | [`CORE` §8.2k](ppcp-core.md#82-arbitration) |
| **D15** | **No per-`basis` coincidence window added now** | Add the variant type as insurance. Rejected because only an arbitrating host consumes the field, so the change is additive rather than breaking — but the measurement design was changed, which is the part that could not wait | [`CORE` Annex B8](ppcp-core.md#annex-b--open-issues) |
| **D16** | **`canonical_correction_ns` added** beyond the one clause requested | State the rule and leave the correction invisible. Rejected on the reviewer's own symmetry argument with `tof_correction` | [`CORE` §5.12f](ppcp-core.md#512-candidate) |


---

# Round 4 — the closing reviews, and approval

Both teams signed off on Draft 3. Both attached closing findings, and all of them are carried in the approved text. This section records them and closes the disposition.

The host review's own summary of the three rounds is worth preserving, because it is the argument for having done it this way:

> Round 1 found defects in the **model** — things the entity graph could not express. Round 2 found defects in the **fixes** — a new invariant that constrained a choice, a contradiction between two documents. Round 3 finds defects in the **seams around the fixes** — a peer obliged to emit a value it cannot compute, an obligation discharged through a message its profile does not confer, an amendment with no stated owner. Each round's findings are smaller and further from the centre than the last, which is what convergence looks like.

## 12. Round 4 — PinPointStudio

### 12.1 S1 — a peer told to mint may have no expressible `t0`

**Accepted in full, and it is the most valuable of the three** because the conformance suite reaches it by design rather than by accident.

The chain is short and airtight. 8.2d excludes a Candidate whose relation to `timebase_ref` is missing, `unrelated` or too uncertain — and that is *the commonest reason a host stays silent*. Silence expires the deadline, 8.2i fires, and the peer is told to mint. But 5.13c requires `t0` in `Session.timebase_ref`, and the very condition that caused the exclusion is the condition under which the peer cannot convert into that timebase either. 5.4b rightly forbids the obvious shortcut of a zero offset. The peer was required to mint and unable to produce a conformant Shot.

The reviewer's observation that `CONF` §5 already requires the pairing *"host ↔ peer declaring `unrelated` timebases"* is what makes this urgent rather than theoretical: in that pairing **every** candidate from the device is excluded, so a device with a working detector reaches the undefined state on every swing. The pairing written to prove an honest degraded peer is handled honestly landed on an undefined one.

[`CORE` §8.2i1](ppcp-core.md#82-arbitration): a peer that cannot express `t0` does not mint, and retains the Candidate with no Shot — a state the model already had, and the honest answer. `CT-I32` gains the negative assertion, and the interoperability pairing now names the expected outcome.

### 12.2 S1(b) and the mobile team's 2.1 — the regime has two entry conditions

**Accepted.** Both teams found this, from opposite ends.

A *hostless session* has no host in its roster. A *host-unreachable interval* still has one, and still has its `timebase_ref`, which is immutable under I16 — the host has merely stopped answering. 8.3a and 8.3f used the same words for both, so I23's precondition did not cover the case 8.3f sends a peer into, and §7.1's zero-host row does not apply because `timebase_ref` stays the host's.

The mobile team traced the consequence further: an implementer could read I23 as *permanent* — a minted Shot must carry exactly one Candidate for ever — and therefore refuse the host's attachment under 8.2k on reconnect. They also traced that it never actually fires, because 8.2k needs a *shared* Candidate the host cannot have received while the link was down. Correct on both counts, and the wording is fixed anyway: two clauses using one set of words for two conditions is precisely the shape [§11.1](ppcp-core.md#111-the-rule-for-writing-an-invariant) was written to catch.

[`CORE` §8.3g](ppcp-core.md#83-the-zero-host-regime) separates the conditions and states what does **not** change during an outage; [§8.3h](ppcp-core.md#83-the-zero-host-regime) states that I23 binds at issuance; I23 and Annex C are reworded; `CT-S4` gains a seventh assertion for the outage path.

### 12.3 S2 — obligations discharged through a message the profile did not confer

**Accepted, and the reviewer's preferred fix taken over the blunt one.**

Three Draft 3 clauses discharge through `ShotLink` — 8.2i, 8.2l/I35 and 8.3f — binding Mint and Arbitrate. `shot_link` was conferred by **Offline**, which neither requires. So `Core + Arbitrate + Live`, a legal profile set, could not satisfy I35 without violating C2. Every worked example in §2.2.3 happens to declare Offline, which is exactly why testing would not have surfaced it — the reference implementations pass by accident, in the manner `CONF` §2c warns about.

**This is the third occurrence of the family that produced the Mint profile.** Draft 1's disposition records the first as *"the v1 device performed an operation none of its declared profiles granted."*

The blunt fix — adding Offline to Mint's and Arbitrate's dependencies — was rejected for the reviewer's own reason: Offline confers bundle read and write and carries I15, I16, I25 and I34, so a live-only third-party host would implement a file container to resolve a race that happens on a socket. Instead **`ShotLink` origination moves to Core and I9 with it**. Of its six bases, `arrival_pairing` and `shared_candidate` are asserted live at capture time and `manual` may be; only three are retrospective. `SessionLink` stays in Offline, because relating two sessions genuinely is an import-time operation.

### 12.4 S3 — no rule for who may amend a Shot

**Accepted, wording adopted almost verbatim.**

Draft 3's 8.2k made two peers able to send `shot` for one `shot.id`. Before that exactly one ever did and the question could not arise. I7 protected `t0`; `authority`, `issued_by` and `id` had no stated owner and `candidates` had no stated amender.

[`CORE` §5.13d](ppcp-core.md#513-shot) fixes the ownership and makes `candidates` extensible by any peer holding a Candidate that belongs to the Shot, with the issuer obliged to adopt an extension. [§5.13e](ppcp-core.md#513-shot) carries the reviewer's convergence sentence, which is the part worth having: extension is additive and order-independent, so neither end reasons about who saw what first. `MSG` §7's direction cell becomes `issuer or attaching peer → any`, and `CT-I35` asserts that only `candidates` changes and that two extensions converge in either order.

### 12.5 Consistency items

| | Item | Fix |
|---|---|---|
| 1 | `confirmed_by: observer` was defined as *"saw the arrival"*, which does not describe `shared_candidate` — a host observing a collision, not an arrival — while `confirmed` is mandatory, so the new basis had no correct value for a new MUST | Broadened to *observed the association*; [5.16g](ppcp-core.md#516-shotlink) states the value |
| 2 | The `intrinsics` scalar rule was undecidable for an empty array | An empty array MUST NOT be emitted and is `malformed` on receipt ([`ENC` 4.1d](ppcp-encoding.md#41-composite-types)) |

### 12.6 The one thing to leave alone

The reviewer asked that `canonical_correction_ns` stay a bare integer beside `tof_correction`'s `Estimate`, and that the reason be recorded so it is not "fixed" during implementation. **Agreed, and it is now in [`CORE` §5.12f](ppcp-core.md#512-candidate).** Time of flight is a converging estimate whose dispersion changes shot to shot; the canonical correction is arithmetic over declared values, and its trustworthiness is `frame_start_to_exposure_offset_provenance` on the profile — carried once under I31 rather than invented per candidate.

## 13. Round 4 — PinPointCapture

### 13.1 Two minor points

**2.1 — the two entry conditions.** Same finding as the host's S1(b); dispositioned at [§12.2](#122-s1b-and-the-mobile-teams-21--the-regime-has-two-entry-conditions).

**2.2 — the empty `intrinsics` array.** Same as the host's consistency item 2. Both suggested the same fix; the text takes both halves — a writer MUST NOT emit one, and a decoder rejects it as malformed rather than indexing out of bounds.

### 13.2 Positions recorded, needing nothing

- `exposure_provenance`: `locked_constant` under the exposure lock, `sampled` for an unlocked source, `per_frame` not claimed until the platform is verified. Exactly what the field was added to make expressible.
- Timing-constant provenance: `assumed` on every profile until the LED rig exists. **I31 is what stops that being silent**, which is the whole reason it was added.
- **The mint deadline is a user-visible latency.** With the defaults a declined Candidate has no Shot for up to 1.2 s, so the capture screen shows a candidate before it can show a shot ordinal. A direct consequence of 8.2i, correctly identified as an application concern, and recorded here so it does not arrive later as a surprise.
- Promotion policy and the two-channel requirement: both confirmed, no change.

### 13.3 On the finding they missed

Recorded because the reviewer recorded it: they traced the mint/issue race where a host answers *late* and stopped, without following the branch where a host correctly declines and answers *never*. The host review found that half. **The two reviews caught different halves of the same clause**, which is the argument for having both seats review independently rather than in sequence.

## 14. Found by audit, not by review

`CONF` §5b1 now requires a **profile-boundary audit** before `ppcp/1.0` freezes: for every normative clause requiring a message to be originated, the profile binding the clause must confer that message.

Running it against the approved text immediately found a **fourth** instance of the family, which no reviewer had raised: [`CORE` §7.3b](ppcp-core.md#73-streams-and-capture-control) said a hostless peer's bundle was *"otherwise identical"* to the live path's message sequence — but `arm` and `disarm` are conferred by **Live**, which the v1 offline device does not declare. A bundle recording them would have been non-conformant by C2.

The fix is the honest one rather than a profile change: with nobody controlling, there is no command to record. The bundle carries the *effect* — Streams, `readiness`, Captures — and `readiness` is conferred by Capture, so a hostless peer records it without declaring Live.

Four occurrences, three found by reviewers and one by a script that took minutes to write. That is the argument for 5b1 being mechanical and mandatory.

## 15. Approval

Both teams have signed off. The specification moves to **APPROVED for implementation** at revision 4, 22 August 2026.

**Approved is not stable.** `ppcp/1.0` freezes — errata only — when [`PPCP-CONF`](ppcp-conformance.md) passes on both implementations and the interoperability pairings of `CONF` §5 are demonstrated. Two pieces of test infrastructure gate that, and both teams named them independently:

- **The LED timecode rig.** It gates `measured` provenance on every timing constant under I31, and both Annex B8 defaults. Longest lead time of anything on the critical path, and nothing substitutes for it.
- **The synthetic peer simulator.** It gates four of the seven silent-failure tests. A reference implementation tested against itself passes them by construction.

Neither is a specification problem, and neither is the protocol team's to build.

[`PPCP-RV`](ppcp-rv.md) was not covered by this approval — at the time it was Draft 1 and unreviewed. It has since been approved in its own right; see [`rv-review-disposition-2026-08-22.md`](rv-review-disposition-2026-08-22.md).


---

# Round 5 — revision 7

Both teams reviewed the traceability closures. The mobile team approved; **the host team withheld approval** on three findings, one of which is a direct contradiction between two revisions.

## 16. PPS-C1 — I38 forbade eviction the specification requires elsewhere

**Accepted, and it is the fourth instance of a pattern this document set now names.** As written, I38 said *"whatever its retention policy"* and meant it. Four cases could not reach `confirmed`, and the first is a flat contradiction:

| | Case | Why it could not be confirmed |
|---|---|---|
| a | **Preview** | [5.11j](ppcp-core.md#5112-preview-streams), added one revision earlier, **requires** a peer to discard an undelivered preview Capture. I38 forbade it |
| b | **Any `absent` Capture** | No payload, no digest, so `capture_committed` cannot name it — and 5.11j's own remedy *generates* these |
| c | **`already_present`** | A receiver holding the payload answers with an abort rather than a commit, on the path built to make reconnecting safe |
| d | **Candidate audio** | [5.12.1c](ppcp-core.md#5121-candidate-evidence) already contemplates an *evicted* window, and its count is not bounded by anything the user does |

The error was **scope**. I38 exists for one obligation — shot payload a consumer has not received yet — and was written as though it were about every Capture. [5.14g](ppcp-core.md#514-capture) now names four exits and I38 is scoped to *payload*; `CT-I38` gains all four, since as written it exercised only the first.

## 17. PPC-1 — `confirmed` was unreachable on the path that ships first

**Accepted.** `capture_committed` travels over a live connection, so a hostless session could never confirm anything — and that is the entry-level case the requirements call *"the normal case rather than a fallback"*. The offline path did not rescue it: a host importing a bundle does durably commit those payloads, but nothing said it ever tells the owning peer.

[5.14h](ppcp-core.md#514-capture): a receiver that commits a Capture obtained from a bundle sends `capture_committed` on its next connection with the owning peer. **No new message and no new field** — I34 already made identity `Capture.id` scoped by `Session.id` and the owning `Peer.id`, which is exactly what names a Capture from a session received as a file. [5.14i](ppcp-core.md#514-capture) adds the honest half the reviewer asked for as the alternative: where there is no receiver at all, I38 protects nothing and retention is the peer's own policy.

## 18. PPS-C2 — the convergence claim did not converge

**Accepted; my claim was demonstrably wrong.** Revision 7 said two peers editing concurrently converge on the higher revision. Both hold revision 1, both produce revision 2, each receives an equal revision and *"an equal or lower revision is ignored"* — so they diverge permanently and silently, each believing it converged. A coach at a host and a golfer at a device drawing on one shot is the case markup exists for.

Equal revisions now tiebreak on `author_peer_id`, which was already mandatory. A total order, not a merge — I9 still forbids merging.

## 19. PPS-C3 — an Annotation could not name the view it was drawn on

**Accepted.** `shot_id` plus `at` satisfies the requirement's literal wording and is insufficient in a session with more than one camera, which is the session this protocol exists for. Image coordinates from a down-the-line view rendered on a face-on view are *plausibly* wrong, which is worse than obviously wrong.

`stream_id` added; [5.18g](ppcp-core.md#518-annotation) fixes `at` to that Stream's timebase, which also makes the frame anchor exact instead of a relation-conversion away — a sigma can otherwise land a line on the neighbouring frame. `nav_anchor` falls out correctly: a scrub target is a time rather than a place, so it names no Stream and lives in the session timebase.

## 20. Both teams — `body` at 64 KiB on the control channel

**Accepted.** The mobile team's framing settles it: the cap permitted, on the immediacy-critical channel, roughly what **I30** was written to keep off it. Lowered to **8 KiB** — the document's own 5.18f already argued that anything larger is a different feature — plus [5.18i](ppcp-core.md#518-annotation) requiring a peer to coalesce rapid revisions, since dragging a line resends the whole body each time.

## 21. Smaller, and one endorsement worth keeping

`Source.viewpoint.confidence` is now present **if and only if** `method: classified`. A person who states "down the line" is not expressing a probability, and requiring a number would ask a peer to invent one — the pattern I28 and I31 exist to prevent.

The mobile team offered a **stronger argument for Shape A** than the one in the audit: *Captures are immutable and content-addressed; annotations are edited and deleted.* Shape B would have required mutable Captures, and mutable Captures break the idempotent re-import rule I34 provides. So Shape B was not merely inelegant — it was unimplementable without damaging something already relied on. That reasoning is better than the model-spine argument and is worth carrying.

## 22. The process finding, and a fifth instance found by acting on it

The host review's closing observation: **C1 is the fourth time a new MUST added in one revision has contradicted a MUST added in the previous one, in an adjacent section** — I36 and truncation, I32 and promotion, RT-4 and the TLS relaxation, and now I38 and the preview discard. Each was written correctly against the requirement it was closing and incorrectly against the section next to it. Their proposal: run the traceability sweep *the other way*.

**Accepted as a required check.** [`PPCP-CONF` §5b2](ppcp-conformance.md#5-interoperability) makes an adjacent-MUST sweep mandatory before `ppcp/1.0` freezes.

**Running it immediately found a fifth instance**, which no reviewer had raised: **I8** said a Candidate's evidence is never discarded, while [5.12.1c](ppcp-core.md#5121-candidate-evidence) contemplates an *evicted* window and [5.12.1b](ppcp-core.md#5121-candidate-evidence) makes retention peer policy. I8 and 5.12c now separate the evidence **record**, which is never discarded, from the evidence **payload**, which may be shed with its absence asserted. That is the check earning its place on the first run.


---

# Round 6 — revision 8, and the close

The host team approved revision 8 with five findings. **Two of them were edits from the previous round that never reached the file** — `PPCP-CONF` §5b2, and the rewrites of `CT-I37` and `CT-I38` — which a chained shell command had swallowed. They are written now, and the lesson is the obvious one: an edit is not made until it has been read back.

| | Finding | Disposition |
|---|---|---|
| **D1** | **The fix for C1 over-corrected.** Exit 4 read *"the protocol **or the peer's own declared retention policy** permits the owner to shed it"* — against a requirement reading *nothing unconfirmed is evicted, **regardless of retention policy***. A device could declare a policy, shed shot payload a host had not received, and be conformant. **The hole G2 closed was open again, one revision later, through the clause that closed it.** | **Accepted.** Licence deleted, enumeration kept, [5.14g1](ppcp-core.md#514-capture) added. |
| **D2** | The adjacent-MUST sweep was accepted and the clause was never written. | **Written** — [`PPCP-CONF` §5b2](ppcp-conformance.md#5-interoperability). |
| **D3** | `CT-I38` exercised one of four exits, so the contradiction that produced C1 would still have passed; `CT-I37` still tested the *lower*-revision rule when the **equal**-revision case was the whole of C2. | **Both rewritten.** |
| **D4** | The `body` cap said 8 KiB in three places and **64 KiB in the field table**, which is what a validator is built from. | **Fixed**, and added to [`PPCP-ENC` §8](ppcp-encoding.md#8-limits). |
| **D5** | `stream_id`'s presence rule turned on whether `body` was "interpreted in image coordinates" — and `body` is opaque, so no peer or test could tell. [§11.1](ppcp-core.md#111-the-rule-for-writing-an-invariant)'s own pattern, in a clause added the round before. | **Accepted.** [5.18j](ppcp-core.md#518-annotation) derives presence from `kind`. |
| §2 | A bundle's `capture_committed` may arrive against a **closed** Session. | **Accepted.** [5.14h1](ppcp-core.md#514-capture) — releasing storage stays legitimate after a Session closes. |

## Close

**`PPCP` 1.0 is approved as a specification and approved for implementation**, at revision 9. Seven review rounds, both first-party teams, every finding dispositioned in this document.

Approved is still not the same as **stable**: `ppcp/1.0` freezes when [`PPCP-CONF`](ppcp-conformance.md) passes on both implementations and the interoperability pairings are demonstrated, and two required checks run before it — the [profile-boundary audit](ppcp-conformance.md#5-interoperability) and the [adjacent-MUST sweep](ppcp-conformance.md#5-interoperability). [Annex B](ppcp-core.md#annex-b--open-issues) lists what may still move; none of it blocks implementation.
