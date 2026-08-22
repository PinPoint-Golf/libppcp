# Review disposition — 22 August 2026

**How each review comment was handled in Draft 1 of the specification.**

| | |
|---|---|
| Status | Record of decisions. Non-normative. |
| Date | 22 August 2026 |
| Reviews covered | The review appended to `ppcp-protocol-overview.md` (model draft 4), and the protocol-affecting points of the review appended to `capture-companion-requirements.md` |
| Reviewer basis | A read of both documents, plus implementation experience building the companion app's capability, timebase, session and permission layers on an iPhone 16 |

Every point is dispositioned. Points **not** actioned are listed with reasons, because a review that gets a silent partial response is a review that gets repeated.

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
| **D3** | **`SessionLink` defined now** | Record the gap and defer, as the review suggested | [§1.4](#14-cross-session-time-has-no-home) |
| **D4** | **`Mint` requires `Detect`** | Make Mint independent, allowing a peer to issue Shots from Candidates it did not produce — which is arbitration by another name | [`CORE` §2.2](ppcp-core.md#22-conformance-profiles) |
| **D5** | **`Arbitrate` does *not* require `Mint`**, although both issue Shots | Make Arbitrate depend on Mint on the grounds that both issue Shots — rejected, because it would force a camera-less third-party host to declare Detect and break the review's own worked example | [`CORE` §2.2](ppcp-core.md#22-conformance-profiles) |
| **D6** | **An absent capture is answered as `capture_announce`, not `error`** | Answer `error` / `outside_buffer`; rejected because an absent capture is a *result* that must be asserted (I10), and an error is not a result | [`MSG` §7.3b](ppcp-messages.md#73-capture_request) |
| **D7** | **A stream-desynchronising frame length is fatal; every other malformation is not** | Uniform handling; rejected because dropping a live capture session over one over-long string would violate "capture degrades last" | [`ENC` §8](ppcp-encoding.md#8-limits) |
