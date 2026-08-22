# PPCP — Conformance

**What an implementation must demonstrate, and how.**

| | |
|---|---|
| Document | `PPCP-CONF` |
| Version | **1.0, Draft 2** |
| Status | **Draft — for approval to implement** |
| Date | 22 August 2026 |
| Depends on | [`PPCP-CORE`](ppcp-core.md), [`PPCP-MSG`](ppcp-messages.md), [`PPCP-ENC`](ppcp-encoding.md) |

---

## 1. Claiming conformance

An implementation claims conformance to `ppcp/1.0` **for a stated set of profiles**. The claim is:

> *This implementation implements the Core, Capture, Detect, Mint and Offline profiles of PPCP 1.0, and passes every test in `PPCP-CONF` §3 and §4 carrying those profiles.*

- **(1a) MUST** A claim names its profiles. "PPCP-conformant" without a profile set is not a claim.
- **(1b) MUST** Every test in [§3](#3-the-invariant-test-matrix) whose profile the implementation declares passes.
- **(1c) MUST** Every test in [§4](#4-the-silent-failure-tests) whose precondition the implementation meets passes. These are the ones normal use will not surface.
- **(1d) MUST** An implementation passes the **negative** tests for profiles it does **not** declare: it parses those messages and refuses to originate them (I24).
- **(1e) SHOULD** The conformance run is reproducible from fixtures in the `libppcp` repository, not from a phone and a golf swing.

**Test method** in the tables below is one of:

| Method | Meaning |
|---|---|
| **static** | Decidable from a declaration or a recorded stream without running the implementation against a peer. |
| **fixture** | Replay a recorded PPCP stream through the implementation and assert on its output. |
| **paired** | Run against the reference peer or the simulator, live. |
| **injected** | Requires the injectable clock, a simulated offset/skew, or a synthetic declaration the implementation would not otherwise meet. |
| **rig** | Requires the LED timecode rig — physical ground truth. |

---

## 2. Required test infrastructure

Build order matters here: each of these is needed to test the layer above it, and building them late makes the layers below untestable in practice rather than merely untested.

- **(2a) MUST** **An injectable clock.** Offset, skew and discontinuity are simulated, not waited for. Nothing in [§3](#3-the-invariant-test-matrix) that touches time is testable without it.
- **(2b) MUST** **A fixture format**, which is the bundle container of [`PPCP-ENC` §7](ppcp-encoding.md#7-bundle-container). There is no second format. A recorded range session is a regression fixture at no additional cost.
- **(2c) MUST** **A software peer simulator** that both sides develop against, capable of presenting a **declaration different from the implementer's own** — a different `timing.convention`, a different `geometry`, a foreign profile set. Without that last capability, §4's tests cannot be written at all.
- **(2d) SHOULD** **The LED timecode rig** — a host-driven LED flashing a binary-coded pattern at ~1 kHz in view of every camera, so each frame decodes an absolute host timestamp. It is the only source of end-to-end ground truth, and because different sensor rows decode different codes it measures rolling-shutter `readout_ns` in the same experiment.

2c is the one most likely to be skipped, and it is the one that makes I19, I22, I24 and I31 testable. An implementation tested only against itself will pass every one of them by accident — most dangerously I31, where an unmeasured offset declared as `0` is correct relative to another implementation that also declared `0`.

---

## 3. The invariant test matrix

Thirty-two invariants, thirty-two tests. Identifiers match [`PPCP-CORE` §11](ppcp-core.md#11-invariants).

| Test | Invariant | Profile | Method | Assertion |
|---|---|---|---|---|
| **CT-I1** | I1 | Core | static | No timestamp is encodable without a `tb`. Attempt to emit an `Instant` with a missing or empty `tb` and assert the encoder refuses. Decode a stream containing one and assert `malformed`. |
| **CT-I2** | I2 | Core | fixture | Replay a stream with a dropped frame in the middle of a `Series`. Assert the implementation does not reconstruct the missing time by index arithmetic, and that no output timestamp is derived from position. |
| **CT-I3** | I3 | Core | static | An `affine` relation missing `offset_sigma_ns` or `skew_sigma_ppm` is rejected as malformed on receipt, and cannot be constructed for emission. |
| **CT-I4** | I4 | Core | static | Two Sources declared on one clock share one `timebase_id`. Assert no `TimebaseRelation` with `from == to` is emitted, and that identity is never asserted by relation. |
| **CT-I5** | I5 | Capture | paired | Change a Source's calibration mid-session. Assert the open Stream closes, a new Stream opens, and the **Session does not end**. Assert no Capture spans the boundary. |
| **CT-I6** | I6 | **Mint**, Arbitrate | static | Every Shot in the output references ≥1 Candidate somewhere in the Session. Assert also the negative: a Shot with zero Candidates *from one peer* is legal. |
| **CT-I7** | I7 | Mint, Arbitrate | paired | Deliver a Candidate after the Shot has been issued. Assert it attaches, and that `t0` is **byte-identical** before and after. |
| **CT-I8** | I8 | Mint, Arbitrate | paired | Arbitrate with one candidate excluded for an over-wide sigma; assert the excluded Candidate is present in `Shot.candidates` and its evidence reference survives. **Then assert two Candidates of the *same* `basis` from *different* peers — a host microphone and a device microphone — are both retained and both appear.** An arbiter that keeps one candidate per modality silently drops the second, which destroys the only thing a second acoustic nominator is for. Finally assert an unpromoted Candidate in a hostless session is emitted and retained with no Shot referencing it. |
| **CT-I9** | I9 | Offline | static | The implementation exposes **no operation** that merges or rewrites a Shot on reconciliation. Assert by API surface, not by behaviour. |
| **CT-I10** | I10 | Capture | paired | Withhold a payload. Assert the receiver does not mark the Capture `absent`, and that `absent` appears only when the owner asserted it. |
| **CT-I11** | I11 | Capture | fixture | A `continuous` stream with a dropout produces an explicit gap; no consumer interpolates across it. Assert gaps on a `shot_windowed` stream are rejected or ignored. |
| **CT-I12** | I12 | Capture | fixture | A video-only bundle, an IMU-only bundle and an empty-stream Session all load and are valid. |
| **CT-I13** | I13 | Core | fixture | Replay a stream containing an unknown message type, an unknown map key at three nesting levels, an unknown `Source.kind` and an unknown `Candidate.basis`. Assert none is fatal and the surrounding data survives. |
| **CT-I14** | I14 | Core | static | Grep the implementation's protocol layer for a frame-rate, resolution, quality or confidence constant. Assert every such threshold lives in a policy layer above it. |
| **CT-I15** | I15 | Offline | fixture | A bundle whose wall clock steps mid-session. Assert no interval, duration or ordering decision is computed from the `wall` timebase. |
| **CT-I16** | I16 | Offline | paired | Import a bundle, re-solve the clock mapping. Assert `Session.timebase_ref` is unchanged and the improvement appears as a new `TimebaseRelation` **from** it. |
| **CT-I17** | I17 | Capture | **injected** | See [CT-S1](#41-ct-s1--the-canonical-instant-conversion). |
| **CT-I18** | I18 | Core | paired | A peer with three timebases. Assert one probe sequence per timebase and three directly-measured relations; assert no relation is emitted that was not measured. |
| **CT-I19** | I19 | Core | **injected** | See [CT-S3](#43-ct-s3--host-side-declaration). |
| **CT-I20** | I20 | Arbitrate | paired | A second peer declaring `role: host` is refused with `role_conflict`. A non-host peer attempting to arbitrate is refused. |
| **CT-I21** | I21 | Live | paired | A **host** with two timebases runs a probe sequence per timebase and declares both relations. Asserted against the host, not only the device. |
| **CT-I22** | I22 | Capture | static | `frame_start_to_exposure_offset_ns` present with `nominal_frame_start` and absent otherwise. Assert a profile declaring `nominal_frame_start` without it is rejected, and one declaring it with `convention: start` is also rejected. Assert an explicit zero is accepted and a defaulted zero is not producible. |
| **CT-I23** | I23 | Mint | **injected** | See [CT-S4](#44-ct-s4--the-zero-host-path). |
| **CT-I29** | I29 | Detect | static | A `Candidate.tof_correction` carrying `value_ns` without `sigma_ns`, or the reverse, is rejected as malformed and is not constructible for emission. |
| **CT-I30** | I30 | Capture | paired | Capture a 3 s clip at the implementation's highest declared rate with `intrinsics: per_frame`. Assert `capture_announce` carries no per-frame series and measure its encoded size; assert `payload_begin` carries `achieved_frames`. Repeat with exposure locked and assert the constant series are sent as scalars. |
| **CT-I31** | I31 | Capture | static | `frame_start_to_exposure_offset_ns`, `rolling_shutter.readout_ns` and `AchievedFrames.exposure_ns` each carry provenance. Assert a value that has not been measured for this device model is `assumed`, that `measured` is never emitted for a vendor-documented or inherited figure, and that `exposure_provenance: per_frame` is not emitted where the platform does not attach the value to the sample. |
| **CT-I32** | I32 | Mint | **injected** | A host that receives a Candidate and never issues a `shot`. Assert the peer does not mint before `issue_hold_ns + heartbeat_interval_ms`, and does mint after. Assert two peers with the same declared parameters agree on whether a Shot exists. |
| **CT-I24** | I24 | Core | **injected** | See [CT-S6](#46-ct-s6--comprehension-versus-origination). |
| **CT-I25** | I25 | Offline | static | Creating a `SessionLink` alters neither Session. Assert byte-equality of both Sessions before and after. Assert no operation composes a `SessionLink` with a `TimebaseRelation`. |
| **CT-I26** | I26 | Detect | static | A Candidate whose `source_id` names no declared Source, or a Source with no declared Timebase, is rejected. Assert a filesystem-imported record is not emitted as a Candidate. |
| **CT-I27** | I27 | Capture | static | Every Capture carries exactly one of `anchor.shot_id` and `anchor.candidate_id`. Assert neither-both nor-neither is constructible. |
| **CT-I28** | I28 | Capture | static | A profile with no self-test carries no `measured`. Assert the implementation never synthesises one from claimed values or a device-profile table, and that a short onboarding sample is emitted as `method: cold_sample`. |

---

## 4. The silent-failure tests

Six places an implementation will appear to work while being wrong. **Each needs an explicit test, because normal use will not surface it**, and in three of the six the reference implementation will pass by accident if the test is written lazily.

### 4.1 CT-S1 — the canonical instant conversion

*Invariants I17, I22. Method: injected. Profile: Capture.*

The conversion spans two entities and, for the default mobile path, three inputs. Two implementers can each apply part of it and both believe themselves compliant. The resulting error is exposure-dependent, so it looks exactly like clock bias — and is then absorbed by a bias estimator and mis-attributed.

**Setup.** A synthetic peer declaring `timing.convention: nominal_frame_start` with `frame_start_to_exposure_offset_ns: 120000`, against an implementation whose own cameras declare `start`. Exposure deliberately **varies** frame to frame in `achieved.exposure_ns`.

**Assertions.**

1. The worked examples of [`PPCP-CORE` §6.1.1](ppcp-core.md#611-worked-examples) reproduce exactly, to the nanosecond.
2. Setting `frame_start_to_exposure_offset_ns` to 0 and to 120000 produces outputs differing by exactly 120000 ns. *An implementation that ignores the field passes every other test in this suite.*
3. Doubling every exposure duration changes the converted instants; an implementation using the profile's exposure *range* rather than the per-frame value fails here.
4. Round-trip: convert to canonical and back to the source convention, and recover the original timestamp bit-for-bit.
5. A rolling-shutter profile's row-`r` instants match [`PPCP-CORE` §6.2d](ppcp-core.md#62-rolling-shutter) under **both** `top_to_bottom` and `bottom_to_top`, including the `R == 1` case.

Assertion 2 is the whole test. The other four are why it is worth writing carefully.

### 4.2 CT-S2 — `nominal_frame_start` on the real device

*Invariant I22. Method: rig. Profile: Capture.*

`nominal_frame_start` is what every AVFoundation source declares, so it is the default path for the entire mobile side, and a wrong `frame_start_to_exposure_offset_ns` is a small constant error that a bias estimator will absorb and attribute to the clock.

**Assertions.**

1. The declared offset for each shipped device profile is measured on the LED rig, not assumed and not taken from a vendor document.
2. Residual end-to-end alignment error against rig ground truth is independent of exposure duration across the profile's exposure range. **A residual that varies with exposure is a wrong offset, not a noisy clock.**
3. The measured value is shipped as data keyed by device model, never as code.

### 4.3 CT-S3 — host-side declaration

*Invariant I19. Method: injected. Profile: Core.*

A single-vendor implementation satisfies I19 **by accident**: its host conventions are correct in hardcoded form, so every test passes and nothing is on the wire. This is the failure most likely to survive to release, because the reference host will always pass it.

**Assertions.**

1. The host emits `declare` carrying `timing`, `geometry` and `intrinsics` for **every** Source it owns, and emits `declare` with an empty `sources` list when it owns none.
2. Against a synthetic host declaring a *different* convention from the implementation's own cameras, converted instants change accordingly. Hardcoding is detected here and nowhere else.
3. No code path infers a convention, geometry or readout time from `Peer.product`, from `role`, or from a platform identifier.

### 4.4 CT-S4 — the zero-host path

*Invariants I20, I23. Method: injected. Profile: Mint.*

Never exercised in a studio, and it is what v1 ships.

**Assertions.**

1. A session with no `host` runs end to end: declare, stream open, arm, candidates, shots, captures, bundle write, bundle read.
2. Two candidates 10 ms apart produce **either two Shots, or one Shot and one unpromoted Candidate — and both Candidates are emitted and retained with their evidence either way.** No coincidence window is applied, and no Shot carries more than one Candidate.
3. Every Shot carries `authority: device`.
4. The same two candidates, replayed into a session *with* a host and a 50 ms window, produce **one** Shot carrying **both** Candidates. Assertions 2 and 4 together are the test; either alone passes for the wrong reason.
5. The Mint profile is declared. A peer that mints without declaring Mint fails [§1d](#1-claiming-conformance).

**Assertion 2 changed in Draft 2 and the reason is worth keeping.** It previously read "two candidates 10 ms apart produce two Shots, not one" — which certified a defect. The detector is required to discriminate ball-into-screen, roughly 9 ms after impact at 3 m, from the impact itself, and the retention design encourages emitting both so a rejected nomination keeps its audio. Under the old assertion, a device that did exactly that minted two Shots for one swing and passed. The invariant now constrains the *shape* — no window, one Candidate per Shot, nothing discarded — and leaves promotion to the detector, where I14 already puts every other threshold.

### 4.5 CT-S5 — relation composition

*Invariant I18. Method: paired. Profile: Core.*

An implementer will compose relations silently, because it is convenient and appears to work, and because composed sigmas look plausible.

**Assertions.**

1. A peer with timebases A, B and C, having measured A→B and B→C, does **not** emit A→C.
2. Asked for A→C, it measures it — a fresh probe sequence is observable on the wire — or reports `relation_missing`.
3. A peer whose network stack timestamps on B while its camera is on A runs the sync exchange **per timebase** and declares A→host directly ([`PPCP-CORE` §5.4.1](ppcp-core.md#541-the-replacement-obligation)).
4. Applied to a **host** with several cameras on independent clocks, not only to a device (I21).

### 4.6 CT-S6 — comprehension versus origination

*Invariant I24. Method: injected. Profile: Core.*

An implementation that only ever talks to itself never receives a message from a profile it lacks.

**Assertions.**

1. A peer declaring `Core + Arbitrate + Live + Offline` and **not** Detect parses `candidate` completely — every field, including unknown `basis` values — and arbitrates over the result.
2. That peer never originates `candidate`.
3. A peer receiving a request whose behaviour it does not implement answers `error` / `profile_not_supported` and **does not close the transport**.
4. Every message type in [`PPCP-MSG` §11](ppcp-messages.md#11-message-index) decodes on a peer declaring only Core.

---

## 5. Interoperability

Conformance to the document is necessary and not sufficient. Two implementations that each pass §3 and §4 alone can still fail to interoperate.

- **(5a) MUST** Before `ppcp/1.0` is declared stable, the following pairings pass a full session — establish, sync, arm, shots, captures, export, reconcile:

| A | B | Principally proves |
|---|---|---|
| Reference device ↔ reference host | | The happy path. Proves the least. |
| Reference device ↔ **synthetic third-party host** declaring different camera conventions | | I19, I22, and the open-protocol commitment |
| Reference device, **no host** → bundle → reference host import | | I20, I23, I16, I9 |
| Reference host ↔ **observer-only peer** (`Core + Live`) | | I24 |
| Reference host ↔ peer declaring `unrelated` timebases | | I3, and that an honest degraded peer is not silently mishandled |
| Reference host **owning its own acoustic Source** ↔ device with an acoustic Source | | I8 — two nominators of the same `basis`, both retained. A per-modality slot drops one and the failure is silent |
| Reference host that never issues a `shot` ↔ nominating peer | | I32 — both ends agree on when the peer may mint |
| Bundle written by A → read by B, both directions | | [`PPCP-ENC` §7a](ppcp-encoding.md#7-bundle-container) — that live and file are one format |

- **(5b) MUST** The `unrelated` pairing asserts the host **refuses or excludes with a reason**, and never substitutes a zero offset ([`PPCP-MSG` §10c](ppcp-messages.md#10-errors)).
- **(5c) SHOULD** At least one pairing uses an implementation not written by the reference team. Until that happens, "open protocol" is an intention rather than a demonstrated property.

---

## 6. What conformance does not cover

Stated so the boundary is not mistaken for an omission.

| Not tested here | Why |
|---|---|
| Frame rate, resolution, optical quality thresholds | Host ingest policy, deliberately outside the protocol (I14). |
| Detection accuracy, classifier quality, false-positive rate | Device-internal. The protocol carries `confidence`; it does not judge it. |
| **Which candidates a Mint peer promotes** | Detector tuning, and therefore I14 territory. The suite tests the *shape* of the result — one Candidate per Shot, nothing discarded, no window — never the choice. |
| Rendezvous, pairing, key derivation | [`PPCP-RV`](ppcp-rv-scope.md), which does not exist yet. **No pairing interoperability is currently testable at all.** |
| Byte-transfer performance, throughput, latency | Transport. PPCP declares uncertainty; it does not require a quality. |
| Battery, thermal endurance, sustained capture rate | Product requirements, verified against the device, not the protocol. Note that `MeasuredCapability.method` exists precisely so a cold figure cannot be presented as a sustained one (I28). |
