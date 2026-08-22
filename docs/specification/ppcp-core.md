# PPCP — Core Specification

**PinPoint Capture Protocol. Normative specification of the entity model, timing contract, session and shot semantics.**

| | |
|---|---|
| Document | `PPCP-CORE` |
| Version | **1.0, Draft 1** |
| Wire version | `ppcp/1.0` |
| Status | **Draft — for approval to implement** |
| Date | 22 August 2026 |
| Editor | libppcp maintainers, `PinPoint-Golf/libppcp` |
| Basis | `capture-companion-requirements.md` (21 August 2026) and its review of 22 August 2026; `ppcp-protocol-overview.md` model draft 4 and its review of 22 August 2026 |
| Supersedes | `ppcp-protocol-overview.md` Parts 0, I and III for all normative purposes. That document is not carried in this repository. |
| Companion documents | [`PPCP-MSG`](ppcp-messages.md), [`PPCP-ENC`](ppcp-encoding.md), [`PPCP-CONF`](ppcp-conformance.md), [`PPCP-RV`](ppcp-rv-scope.md) (scope only, unwritten) |
| Licence | Specification: open. Reference implementation `libppcp`: MIT. |

---

## 0. Status of this document

This is **Draft 1 of the formal specification**. It converts model draft 4 of the protocol overview into normative text, resolves the five defects raised in the 22 August 2026 protocol review and the three protocol-affecting points raised in the requirements review, and fixes the message names that the overview marked provisional.

It is published to obtain **approval to implement**. It is not yet stable: section [Annex B](#annex-b--open-issues) lists what is expected to move. Implementations may begin against this draft; the wire version `ppcp/1.0` will not be declared stable until Draft 1 has been reviewed by the PinPointCapture and PinPointStudio teams and the conformance suite of [`PPCP-CONF`](ppcp-conformance.md) passes on both.

**Where this document and any earlier draft disagree, this document wins.** `docs/specification/` is the single authority on PPCP, and the protocol overview that preceded it is a working document deliberately not carried in this repository. This specification is therefore self-contained: the rationale motivating each decision is restated here rather than referenced out.

**Invariant identifiers are stable.** I1–I21 keep the numbering the overview and the review used, even where their text has been amended. New invariants are appended as I22–I28. Conformance documents get quoted by number; renumbering is a cost with no benefit.

### 0.1 What changed from model draft 4

| # | Review point | Disposition |
|---|---|---|
| 1 | `nominal_frame_start` obligation with nowhere to write it | **Accepted.** `timing.frame_start_to_exposure_offset_ns` added ([§5.7](#57-captureprofile)); conversion restated as `t + offset + d/2` ([§6.1](#61-canonical-instant)); I17 amended to name three inputs; I22 added. |
| 2 | Device-minted shot issuance has no profile | **Accepted.** New **Mint** profile ([§2.2](#22-conformance-profiles)) separates *issuing* a Shot from *arbitrating* between Candidates. I6 reassigned from Detect to Mint. |
| 3 | Profiles gate emission, not comprehension | **Accepted.** Stated normatively in [§2.2.2](#222-what-a-profile-confers); explicit dependency table replaces "depends on Core and nothing else"; I24 added. |
| 4 | Cross-session time has no home | **Resolved rather than recorded.** `SessionLink` added ([§5.17](#517-sessionlink)); I25 added. Marked provisional — see [Annex B](#annex-b--open-issues). |
| 5 | Stale invariant count | **Fixed.** Twenty-eight invariants, counted in one place ([§11](#11-invariants)). |
| R2 | Requirements review: file-imported launch monitor records are not Candidates | **Accepted.** [§8.1](#81-nomination) restricts nomination to live nominators; I26 added. |
| R4a | Requirements review: `measured` cannot be obtained honestly at onboarding | **Accepted.** `MeasuredCapability.method` and `.duration_ns` are mandatory; absence of `measured` means not measured ([§5.8](#58-capability)); I28 added. |
| — | Found while writing: Captures anchored to Candidates had nowhere to attach | **Fixed.** `Capture.anchor` is exactly one of a Shot or a Candidate ([§5.14](#514-capture)); `Candidate.id` added; I27 added. |
| — | Found while writing: rolling-shutter row formula was ambiguous | **Fixed.** `readout_ns` and the row formula defined exactly ([§6.2](#62-rolling-shutter)). |

The full disposition, including points deliberately **not** actioned, is in [`review-disposition-2026-08-22.md`](review-disposition-2026-08-22.md).

---

## 1. Introduction

*Non-normative.*

PPCP is an open protocol by which one or more **capture devices** and an optional **host** agree on what happened, and precisely when, during a golf swing — or, more generally, during any event observed by several independently-clocked sensors that buffer locally and deliver retrospectively.

Two stances run through the whole design and explain most of its shape:

> **The protocol describes what happened; it does not assert what is true.** Devices report, hosts interpret. Wherever a value could be either a measurement or a conclusion, PPCP carries the measurement.

> **Prefer structural enforcement to stated rules.** Where an invariant can be made unwriteable rather than merely forbidden, it is.

PPCP is deliberately *not* a streaming protocol. It assumes a device that timestamps at the source, buffers locally, detects events independently, and delivers clips after the fact — a shape not covered by RTP/WebRTC/ST 2110 (continuous streaming, no retrospective retrieval) or GigE Vision over IEEE 1588 (wired, genlockable, no phone implementation).

### 1.1 Document set

| Document | Authority | Contents |
|---|---|---|
| **PPCP-CORE** (this) | Normative | Entity model, timing contract, session and shot semantics, invariants, profiles |
| [**PPCP-MSG**](ppcp-messages.md) | Normative | Message catalogue, channel semantics, error codes; informative sequence annex |
| [**PPCP-ENC**](ppcp-encoding.md) | Normative | Framing, CBOR encoding, bulk transfer, bundle container |
| [**PPCP-CONF**](ppcp-conformance.md) | Normative | Conformance requirements and the invariant-to-test matrix |
| [**PPCP-RV**](ppcp-rv-scope.md) | Scope only | Rendezvous, pairing, security. **Not yet written.** Versioned independently. |

### 1.2 Reading guide

Two entities carry nearly all the difficulty: **Timebase** ([§5.3](#53-timebase)) and **Shot** ([§8](#8-shot-determination)). [§6.1](#61-canonical-instant) is the single most likely site of silent non-conformance and should be read even by reviewers reading nothing else.

---

## 2. Conformance

### 2.1 Requirement keywords

The keywords **MUST**, **MUST NOT**, **REQUIRED**, **SHALL**, **SHALL NOT**, **SHOULD**, **SHOULD NOT**, **RECOMMENDED**, **MAY** and **OPTIONAL** are to be interpreted as described in BCP 14 (RFC 2119, RFC 8174) when, and only when, they appear in all capitals.

A **peer** is any implementation of PPCP. A **conformant peer** implements the Core profile, declares the set of profiles it implements, and satisfies every invariant of [§11](#11-invariants) that its declared profiles carry.

Text marked *non-normative* or placed in an annex marked non-normative constrains nothing.

### 2.2 Conformance profiles

An implementation need not implement all of PPCP. Requiring that would be hostile to the open-protocol commitment: a video-only offline capture device has no business carrying arbitration logic.

| Profile | Confers the ability to originate | Requires | Invariants |
|---|---|---|---|
| **Core** | Peer, Timebase, TimebaseRelation, ClockDiscontinuity declaration; version and extension negotiation | — | I1, I2, I3, I4, I13, I14, I18, I19, I24 |
| **Capture** | Source, CaptureProfile, Stream, Capture; arm/disarm response; readiness | Core | I5, I10, I11, I12, I17, I22, I27, I28 |
| **Detect** | Candidate | Core | I26 |
| **Mint** | Shot issuance from the peer's **own** Candidates, `authority: device` | Core, Detect | I6, I7, I23 |
| **Arbitrate** | Shot issuance from **any peer's** Candidates: coincidence window, canonical t₀, `authority: host` | Core | I6, I7, I8, I20 |
| **Live** | Sync exchange, heartbeat, event/payload split, session control over a live link | Core | I21 |
| **Offline** | Bundle read and write, ShotLink, SessionLink, reconciliation | Core | I9, I15, I16, I25 |

**Core is mandatory.** Every other profile has the dependencies stated above and no others.

**Arbitrate is available only to a peer with `role: host`** (I20). Mint is available to any peer.

**Arbitrate does not depend on Mint**, although both issue Shots. Mint issues from the peer's *own* Candidates and therefore needs Detect; Arbitrate issues from *any* peer's Candidates, which it reads without being able to emit them (I24). Making Arbitrate depend on Mint would force a camera-less third-party host to declare Detect, which is exactly the case the profile split exists to keep clean.

#### 2.2.1 Why Mint exists

Model draft 4 placed "Shot issuance, coincidence window, canonical t₀" in Arbitrate and made Arbitrate host-only, while simultaneously requiring an offline device with no host to mint Shots. The v1 PinPointCapture device therefore performed an operation none of its declared profiles granted.

The two operations are genuinely different and are now separate:

| | **Mint** | **Arbitrate** |
|---|---|---|
| Input | this peer's own Candidates | Candidates from every peer |
| Coincidence window | **not applied** | applied |
| Output | one Shot per Candidate | one Shot per coincident group |
| `Shot.authority` | `device` | `host` |
| Available to | any peer | `role: host` only |

Applying a coincidence window in a zero-host session would collapse distinct candidates and produce different output from the same acoustic evidence. "No arbitration" is a materially different regime from "arbitration with a single nominator", and the profile split makes that testable (I23).

#### 2.2.2 What a profile confers

**A profile confers the right to *originate* messages and the obligation to implement the corresponding behaviour. It does not gate comprehension.**

- **(C1) MUST** Every conformant peer parses the complete type vocabulary of this specification, regardless of which profiles it implements. A peer that does not implement Detect still parses a `candidate` message and every field of a `Candidate`.
- **(C2) MUST NOT** A peer originate a message whose profile it has not declared.
- **(C3) MUST** A peer receiving a message it understands but whose behaviour it does not implement respond with `error` / `profile_not_supported`, never by closing the connection.

This is why a third-party host may declare `Core + Arbitrate + Live + Offline` with no Detect: it arbitrates over Candidates it can read but cannot emit. The distinction is load-bearing for sizing implementation work — the type vocabulary is common; the behaviour is not.

#### 2.2.3 Worked examples

| Implementation | Profiles |
|---|---|
| Offline-only video capture device (v1 PinPointCapture) | Core + Capture + Detect + **Mint** + Offline |
| Full mobile capture device | Core + Capture + Detect + Mint + Live + Offline |
| PinPointStudio host | Core + Capture + Detect + Arbitrate + Live + Offline |
| Second-screen observer (UC-5) | Core + Live |
| Third-party host with no cameras | Core + Arbitrate + Live + Offline |
| Bundle-reading analysis tool | Core + Offline |

Note that the v1 device's profile set changed: **Mint is new and is what v1 actually ships**. An implementation plan that omits it is short a step.

### 2.3 Invariants are conformance tests

The twenty-eight invariants of [§11](#11-invariants) are the conformance surface. An implementation that violates one is non-conformant, whatever else it does. [`PPCP-CONF`](ppcp-conformance.md) maps each to a required test.

---

## 3. Transport contract

PPCP is transport-agnostic but not transport-indifferent. An implementation MUST supply a transport meeting this contract. Discovery, addressing and authentication are **not** part of it — see [`PPCP-RV`](ppcp-rv-scope.md).

- **(T1) MUST** Ordered, reliable, bidirectional delivery per channel. PPCP does not retransmit, reorder or checksum.
- **(T2) MUST** **At least two logically independent channels with independent flow control**: one **control** channel and at least one **bulk** channel. See [§3.1](#31-why-two-channels-is-not-negotiable).
- **(T3) MUST** Message boundaries, either from the transport or from the PPCP framing of [`PPCP-ENC`](ppcp-encoding.md).
- **(T4) MUST NOT** PPCP assumes nothing about addressing, discovery or authentication.
- **(T5) MUST** The transport preserves the two channels' independence end to end. Multiplexing both onto one flow-controlled stream does not satisfy T2 however the multiplexing is done.

Channel numbering, and the mapping from channels to transport streams, is specified in [`PPCP-ENC` §2](ppcp-encoding.md#2-channels).

### 3.1 Why two channels is not negotiable

Shot events must arrive immediately while video is permitted to lag. **A single TCP connection cannot satisfy this.** A 25 MB capture in flight head-of-line blocks every subsequent message, including the next shot's event — so a golfer hitting again during a transfer would see the second shot's correlation delayed behind the first shot's video.

| Acceptable | Not acceptable |
|---|---|
| Two TCP connections | One TCP connection carrying both |
| QUIC streams | Any scheme without per-channel flow control |
| Application-level multiplexing with interleaving **and per-channel windows** | Interleaving without independent windows |

This is a transport requirement derived from a protocol requirement, and it is easy to miss because both channels are drawn as one lifeline in every sequence diagram.

### 3.2 Transport guidance

*Non-normative.* PPCP mandates no transport quality, because **uncertainty is declared and acceptance is host policy**. A WiFi session honestly reporting 2 ms sigma is conformant; whether that is good enough is the host's decision, not the protocol's.

The mechanism worth understanding: minimum-RTT filtering estimates clock offset from the *shape* of the latency distribution, specifically the tightness of its left tail. That determines convergence speed and residual sigma.

| Transport | Left tail | Practical effect |
|---|---|---|
| USB tunnel | very tight, stable | Fastest convergence, lowest sigma. Best available. |
| Wired host + WiFi device | moderate | Removes host-side variance only. |
| 5 GHz WiFi, host hotspot | moderate | Preferred wireless configuration. |
| 5 GHz WiFi, shared infrastructure | heavy-tailed | More exchanges to converge; higher residual sigma. |
| 2.4 GHz or congested WiFi | very heavy-tailed | May not reach useful sigma at all. |

Device-side caveats worth designing around: WiFi power-save can inject tens of milliseconds of latency, and Apple's AWDL shares the radio with infrastructure WiFi, so using both degrades each. At a range or any public venue, assume multicast fails.

---

## 4. Peer contract

Minimum to implement any profile:

- **(P1) MUST** A monotonic clock with declared resolution, and the ability to timestamp samples **at the source** rather than on receipt.
- **(P2) MUST** A clock that does not halt across device sleep, or an honest declaration that it does (`epoch_stable: false`) plus discontinuity reporting ([§6.4](#64-clock-discontinuity)).
- **(P3) MUST** Sufficient storage to hold at least one complete shot window for every `shot_windowed` Stream it declares.
- **(P4) SHOULD** Sub-millisecond timestamp resolution. Coarser is expressible — `Timebase.resolution_ns` is declared — but a host will likely reject it under its own policy.

Deliberately **not** assumed: a camera, a microphone, a display, a network, a host, or any particular frame rate. A pure `observer` peer satisfies P1–P4 and owns no Sources at all.

---

## 5. Data model

### 5.1 Notation and primitive types

Field tables use the following column meanings: **Card.** is cardinality — `1` mandatory, `0..1` optional, `0..n` a possibly-empty list, `1..n` a non-empty list. Encoding of each type is specified in [`PPCP-ENC` §4](ppcp-encoding.md#4-primitive-types).

| Type | Definition |
|---|---|
| `Id` | Opaque UTF-8 string, 1–64 bytes, unique within its stated scope. Implementations MUST NOT parse structure out of an `Id` they did not mint. |
| `Instant` | A point in time: `{ tb: Id, ns: int64 }` — a timebase identifier and a signed nanosecond count in that timebase. **There is no `Instant` without a `tb`** (I1). |
| `Series` | Many points in one timebase: `{ tb: Id, ns: [int64] }`. Still carries `tb`, so I1 holds. |
| `Duration` | `int64` nanoseconds. Timebase-free: a duration is not a point in time. |
| `Interval` | `{ tb: Id, start_ns: int64, end_ns: int64 }`, `start_ns <= end_ns`, half-open `[start, end)`. |
| `Sigma` | Non-negative standard deviation, in the unit of the quantity it qualifies. Named `*_sigma`. |
| `Kind` | Open-registry string. See [§10.3](#103-registries). |
| `Digest` | `{ alg: "sha-256", value: bytes }`. |

**Every optional field's absence has a stated meaning.** Where absence means "not measured" or "not known", that is said explicitly; absence never means zero, and a peer MUST NOT substitute a default for a value it did not measure.

### 5.2 Peer

The participant. **`Peer` is not a synonym for device**: a host is a Peer, declares Timebases, and owns Sources exactly as a phone does.

| Field | Type | Card. | Notes |
|---|---|---|---|
| `id` | `Id` | 1 | Stable across sessions and reconnects. See [§5.2.1](#521-peer-identity). |
| `role` | `host` \| `capture` \| `observer` | 1 | **Per-session, not intrinsic.** |
| `protocol` | `{ version: string, extensions: [Kind] }` | 1 | See [§10](#10-versioning-extensions-and-registries). |
| `profiles` | `[Kind]` | 1..n | Declared profiles from [§2.2](#22-conformance-profiles). MUST include `core`. |
| `timebases` | `[Timebase]` | 1..n | |
| `relations` | `[TimebaseRelation]` | 0..n | |
| `sources` | `[Source]` | 0..n | A Peer owning no Sources participates fully. |
| `product` | `{ vendor, model, version }` | 0..1 | Informational. MUST NOT be used to infer behaviour that the protocol requires be declared (I19). |

- **(5.2a) MUST** `role` is declared at session join and does not change for the lifetime of the session.
- **(5.2b) MUST** A Session contains at most one peer with `role: host` (I20). A peer that receives a second `host` declaration responds `error` / `role_conflict`.
- **(5.2c) MUST NOT** A peer infer any capability, convention or geometry from `product`. Everything the protocol requires is declared.

`observer` exists for the second-screen case: a peer that receives but contributes no Streams.

#### 5.2.1 Peer identity

- **(5.2.1a) MUST** `Peer.id` is generated by the peer and persisted in its own store.
- **(5.2.1b) MUST NOT** `Peer.id` is a platform device identifier, an advertising identifier, or any value the platform's privacy rules restrict.
- **(5.2.1c)** Consequence, accepted: reinstalling an application creates a new `Peer.id`, and bundles exported before the reinstall carry the old one. **Reconciliation is on Session, not on Peer** ([§8.5](#85-reconciliation)).

Whether a peer may rejoin a session after reconnecting without re-pairing is a rendezvous question, delegated to [`PPCP-RV`](ppcp-rv-scope.md).

### 5.3 Timebase

The hardest entity, and the one whose shape everything else inherits. Every timestamp in PPCP names one.

| Field | Type | Card. | Notes |
|---|---|---|---|
| `id` | `Id` | 1 | Local to the declaring peer, stable for that peer's lifetime. |
| `kind` | `monotonic` \| `continuous` \| `wall` | 1 | `monotonic` halts across sleep; `continuous` does not; `wall` is subject to jumps and is **label-only**. |
| `epoch_stable` | `bool` | 1 | Does the epoch survive device sleep? |
| `resolution_ns` | `Duration` | 1 | Nominal tick. |
| `origin` | string | 0..1 | Opaque and informational, e.g. `"CMClockGetHostTimeClock"`. MUST NOT be interpreted. |

- **(5.3a) MUST** A peer declares every Timebase any of its Sources references, before or with the Source that references it.
- **(5.3b) MUST NOT** Any timestamp be expressed in a `wall` timebase for the purpose of computing an interval (I15).

**Identity is a shared `id`, not a relation.** Two Sources on the same clock reference the **same** `Timebase.id` (I4). On iOS, camera and microphone Sources both reference `tb:hosttime` and no relation exists because none is needed. On an Android device reporting `SENSOR_INFO_TIMESTAMP_SOURCE == UNKNOWN`, camera and microphone reference *different* ids, so a relation is structurally required and its absence is a detectable error rather than a silent assumption of zero.

This is the mechanism, not merely the rule: a port that assumes camera and mic share a timebase must either declare one id — a checkable claim — or declare two and supply a relation. There is no third option and no silent default.

### 5.4 TimebaseRelation

| Field | Type | Card. | Notes |
|---|---|---|---|
| `from`, `to` | `Id` | 1 | Timebase ids. Directed. |
| `class` | `affine` \| `unrelated` | 1 | |
| `offset_ns` | `int64` | affine: 1 | Value of `to` minus value of `from` at `observed_at`. |
| `skew_ppm` | float | affine: 1 | Rate of `to` relative to `from`, parts per million. |
| `offset_sigma_ns` | `Sigma` | affine: 1 | **Mandatory.** |
| `skew_sigma_ppm` | `Sigma` | affine: 1 | **Mandatory.** |
| `method` | `declared` \| `measured` \| `estimated_online` | 1 | |
| `observed_at` | `Instant` | 1 | Expressed in `from`. |
| `evidence_ref` | `Id` | 0..1 | Stream carrying raw evidence. |

- **(5.4a) MUST** `class: affine` carries all four of `offset_ns`, `skew_ppm`, `offset_sigma_ns`, `skew_sigma_ppm`. A relation missing either sigma is malformed and MUST be rejected (I3).
- **(5.4b) MUST** `class: unrelated` carries none of them. **`unrelated` is a legal, complete declaration.** An honest Android `UNKNOWN` device declares `unrelated` and remains a conformant peer; the host may refuse it under its own policy. The alternative — a fabricated offset — is the failure mode this contract exists to prevent.
- **(5.4c) MUST NOT** Relations be composed (I18). If A→B and B→C are both `affine`, A→C is **not** derived. A peer needing A→C measures and declares it directly.

Composition is forbidden for two reasons, the second decisive. Publishing a composed relation would let a peer emit a conclusion rather than a measurement, and composed sigmas are optimistic unless correlation is carried, which the model does not carry. And the motivating case dissolves: on Android under `TIMESTAMP_SOURCE == UNKNOWN`, camera→system is *precisely the relation that does not exist*, so composition is unavailable in the exact case that motivated it.

#### 5.4.1 The replacement obligation

One composition case survives and is handled explicitly, or implementers will compose silently.

Network clock sync runs on whichever timebase the network stack timestamps on — call it `tb:B`. If the camera is on `tb:A`, the peer's counterpart needs A→host, which is A→B composed with B→host.

- **(5.4.1a) MUST** A peer with more than one Timebase runs the synchronisation exchange of [§6.3](#63-clock-synchronisation) **once per Timebase**, and declares each resulting relation directly (I21).
- **(5.4.1b)** This binds **every multi-clock peer, hosts included**. A host with several cameras on independent clocks carries the same duty toward every other peer in the session.

One relation type serves three uses, differing only in `method`: device↔host network sync, device↔BLE-sensor offline mapping, and camera↔microphone on a multi-clock device.

### 5.5 ClockDiscontinuity

The only record a peer writes about **its own clock, mid-session**. An observed step is a measurement, not an absence.

| Field | Type | Card. | Notes |
|---|---|---|---|
| `timebase_id` | `Id` | 1 | The clock that stepped. |
| `observed_at` | `Instant` | 1 | In a reference timebase that did **not** step. |
| `magnitude_ns` | `int64` | 1 | Signed. |
| `cause` | `sleep` \| `ntp_correction` \| `manual` \| `timezone` \| `unknown` | 1 | Open registry. |

- **(5.5a) MUST** A peer that observes a step in one of its declared timebases reports it, whether or not `epoch_stable` predicted it.
- **(5.5b) MUST NOT** `observed_at` be expressed in the timebase that stepped.

A discontinuity is the evidence that any interval computed across it from that clock would have been wrong. It is what gives I15 something to point at.

### 5.6 Source

The physical capture source: a camera, a microphone, an IMU, a relayed BLE sensor, a connected launch monitor.

| Field | Type | Card. | Notes |
|---|---|---|---|
| `id` | `Id` | 1 | Unique within the owning peer. |
| `peer_id` | `Id` | 1 | Owning peer. Negotiable at session start. |
| `kind` | `camera` \| `microphone` \| `imu` \| `wrist` \| `launch_monitor` \| … | 1 | Open registry. |
| `timebase_id` | `Id` | 1 | Which clock its samples are in. |
| `physical` | `bool` | 1 | `false` for a virtual multi-lens device. See below. |
| `profiles` | `[CaptureProfile]` | 1..n | Supported profiles, each carrying its own measured results. |
| `calibration` | `Calibration` | 0..1 | |
| `label` | string | 0..1 | Human-readable, informational. |

- **(5.6a) MUST** Every Source declares `timebase_id`, and every CaptureProfile it offers declares `timing`, `geometry` and `intrinsics` — **regardless of which peer owns the Source** (I19). No convention is implied by peer role, product or platform.
- **(5.6b) SHOULD NOT** A capture peer open a virtual multi-lens device. Where it does, it MUST declare `physical: false`, because such devices switch physical lenses automatically on scene and focus distance, silently changing intrinsics mid-session.
- **(5.6c) MUST** Where a device and a host are both capable of owning a sensor connection, ownership is settled at session start and expressed solely by `peer_id`. The same wrist sensor is the same Source whichever peer holds the BLE connection.

**Why the layer exists.** A phone Peer has a camera and a microphone with *different* calibrations, *different* profile sets and potentially *different* timebases. Only one of those can hang off the Peer.

**Calibration unifies three concepts the requirements treat separately:**

| Source kind | Calibration is |
|---|---|
| `camera` | intrinsics, distortion, extrinsics |
| `microphone` | position — which *is* the acoustic time-of-flight constant |
| `imu`, `wrist` | bias, alignment |
| `launch_monitor` | position and orientation relative to the rig |

All are the mapping from a Source to the physical world, all estimated the same way, all carrying uncertainty.

#### 5.6.1 Symmetric declaration

Every field of a Source and its profiles is declared by **both** sides. This is not an additional requirement; it is a statement that the existing structure applies without exception.

| Declared by a phone Source | Declared by a host Source |
|---|---|
| `timing.convention: nominal_frame_start` (AVFoundation) | `timing.convention: start` (FLIR) |
| `geometry: rolling_shutter { readout_ns, direction }` | `geometry: global` |
| `intrinsics: per_frame` | `intrinsics: fixed` |
| `measured` per profile | `measured` per profile |
| `calibration` | `calibration` |

If only devices declared, a host would hardcode its own cameras' convention. That works for one implementation and fails the open-protocol commitment immediately: a third-party host with different cameras cannot participate, because the conversion has been baked into an implementation instead of carried by the protocol. **Source count is not a role marker** — a host owning no capture Sources participates fully.

### 5.7 CaptureProfile

A mode a Source can operate in. Not independently addressable outside its Source.

| Field | Type | Card. | Notes |
|---|---|---|---|
| `id` | `Id` | 1 | Unique within the Source. |
| `format` | `{ codec, width, height, pixel_format }` | 0..1 | Camera and other framed sources. |
| `rate` | `{ nominal_mhz, min_mhz, max_mhz }` | 0..1 | Millihertz, so 150 fps is `150000`. Avoids a float on the wire for a value used in scheduling. |
| `optical` | `{ exposure_min_ns, exposure_max_ns, iso_min, iso_max, noise_figure }` | 0..1 | Camera only. `noise_figure` absent means not measured. |
| `geometry` | `global` \| `rolling_shutter { readout_ns, direction, rows }` | camera: 1 | Per **profile**, not per source: readout time differs per mode. |
| `timing` | `Timing` — see below | 1 | |
| `intrinsics` | `per_frame` \| `fixed` \| `none` | camera: 1 | |
| `measured` | `MeasuredCapability` | 0..1 | **Absence means not measured** (I28). |

**`Timing`:**

| Field | Type | Card. | Notes |
|---|---|---|---|
| `convention` | `mid` \| `start` \| `end` \| `nominal_frame_start` | 1 | The source's native timestamp convention. |
| `frame_start_to_exposure_offset_ns` | `int64` | see below | Signed. Fixed offset from the nominal frame start to the actual start of exposure. |

- **(5.7a) MUST** `frame_start_to_exposure_offset_ns` is present **if and only if** `convention == nominal_frame_start` (I22).
- **(5.7b) MUST** Where it is present it is declared explicitly, including when it is zero. A declared zero is a checkable claim; an omitted field is not.
- **(5.7c) MUST** `measured` results attach **per profile**. 1080p240 and 1080p120 are separate self-tests with separate results.
- **(5.7d) MUST NOT** Any frame-rate, resolution, quality or confidence threshold appear in a profile or anywhere else in this specification (I14). Acceptance is host policy, expressed outside the protocol.

`frame_start_to_exposure_offset_ns` is new in this draft and closes the defect the review raised first. `nominal_frame_start` is what **every AVFoundation source declares**, so it is the default path for the entire mobile side, and this offset is exactly the quantity that makes the conversion of [§6.1](#61-canonical-instant) correct rather than approximately correct.

### 5.8 Capability

Three distinct things, routinely different, all on the wire: **claimed** (the profile's own fields), **measured** (self-test), **achieved** (this capture).

**`MeasuredCapability`** — what the peer observed itself sustaining.

| Field | Type | Card. | Notes |
|---|---|---|---|
| `method` | `cold_sample` \| `sustained` | 1 | **Mandatory.** See below. |
| `duration_ns` | `Duration` | 1 | How long the self-test ran. |
| `sustained_rate_mhz` | int | 1 | Realised rate over `duration_ns`. |
| `dropped_frames` | int | 1 | |
| `achieved_exposure_ns` | `{ min, max, median }` | 0..1 | |
| `achieved_iso` | `{ min, max, median }` | 0..1 | |
| `noise_figure` | float | 0..1 | Absent means not measured. |
| `thermal_at_end` | `ThermalLevel` | 0..1 | |
| `observed_at` | `Instant` | 1 | |

- **(5.8a) MUST** Absence of `measured` on a profile means **not measured**. A peer MUST NOT synthesise a `MeasuredCapability` from claimed values, from a device profile table, or from a previous device model (I28).
- **(5.8b) MUST** `method: sustained` is used only for a measurement taken under sustained thermal load. A short sample taken during onboarding is `cold_sample`, and a consumer MUST NOT treat it as a sustained figure.
- **(5.8c) SHOULD** A peer re-measure after an operating-system or firmware update.

5.8a and 5.8b answer a specific implementation finding: onboarding affords seconds, sustained verification wants tens of minutes, and without `method` the cold number quietly becomes the displayed one.

**`AchievedCapability`** — what actually happened on one Capture. Carried on `Capture.achieved`.

| Field | Type | Card. | Notes |
|---|---|---|---|
| `frames` | `Series` | 0..1 | Per-frame source timestamps, in the stream's timebase. |
| `exposure_ns` | `[int64]` | 0..1 | **Per frame**, parallel to `frames.ns`. Required for [§6.1](#61-canonical-instant) on camera streams. |
| `iso` | `[int]` | 0..1 | Parallel to `frames.ns`. |
| `intrinsics` | `[Matrix3]` | 0..1 | Parallel to `frames.ns`, where `intrinsics: per_frame`. |
| `dropped_frames` | int | 0..1 | |
| `thermal` | `[{ at: Instant, level: ThermalLevel }]` | 0..n | Timeline, not a single value. |

- **(5.8d) MUST** On a Capture from a Source whose profile declares `intrinsics`/`geometry` for a camera, `exposure_ns` is present and parallel in length to `frames.ns`. Without it the canonical-instant conversion is impossible (I17).
- **(5.8e) MUST NOT** Time be inferred from frame index anywhere (I2). Frames drop; indices lie. Sequence numbers, where present, are for loss detection only.

**`ThermalLevel`** is an ordinal protocol vocabulary, not a platform passthrough: `nominal` < `elevated` < `serious` < `critical`. A peer MUST map its platform's states onto it and MAY additionally carry an opaque `vendor_label`.

| PPCP | iOS `ProcessInfo.ThermalState` | Android `PowerManager` thermal status |
|---|---|---|
| `nominal` | `.nominal` | `NONE`, `LIGHT` |
| `elevated` | `.fair` | `MODERATE` |
| `serious` | `.serious` | `SEVERE` |
| `critical` | `.critical` | `CRITICAL`, `EMERGENCY`, `SHUTDOWN` |

### 5.9 Calibration

| Field | Type | Card. | Notes |
|---|---|---|---|
| `id` | `Id` | 1 | |
| `source_id` | `Id` | 1 | |
| `kind` | `intrinsics` \| `position` \| `bias_alignment` \| `pose` | 1 | Open registry. |
| `parameters` | kind-specific map | 1 | |
| `uncertainty` | kind-specific map | 1 | **Mandatory.** |
| `method` | `factory` \| `per_frame` \| `solved` \| `user_measured` \| `estimated_online` | 1 | |
| `observed_at` | `Instant` | 1 | |

- **(5.9a) MUST** A Calibration is fixed for the lifetime of any Stream that references its Source. A calibration change closes the Stream and opens a new one within the same Session (I5).
- **(5.9b) MUST** For `kind: position` on a `microphone` Source, `parameters` carries the geometry from which acoustic time of flight is computed. This is where the time-of-flight constant lives; it is not a separate concept.

### 5.10 Session

| Field | Type | Card. | Notes |
|---|---|---|---|
| `id` | `Id` | 1 | UUID. Assigned by the host where one exists, otherwise minted by the capturing peer. |
| `peers` | `[Peer]` | 1..n | With roles. |
| `timebase_ref` | `Id` | 1 | The session's canonical timebase. **IMMUTABLE once set** (I16). |
| `epoch` | `{ wall_utc_ns: int64, at: Instant }` | 0..1 | Wall-clock **label** only. Never used to compute an interval (I15). |
| `coincidence_window_ns` | `Duration` | 1 | See [§8.2](#82-arbitration). Default `50000000` (50 ms). |
| `heartbeat_interval_ms` | int | Live: 1 | See [§7.4](#74-liveness). |
| `streams` | `[Stream]` | 0..n | |
| `shots` | `[Shot]` | 0..n | |
| `contexts` | `[ContextChange]` | 0..n | |
| `state` | `open` \| `closed` | 1 | |
| `completeness` | `complete` \| `partial` \| `unknown` | 1 | Explicitly asserted, never inferred from arrival (I10). |

- **(5.10a) MUST** `timebase_ref` is set when the Session opens and never changes. Online it is a host Timebase; offline it is the capturing peer's. Making it a field means the offline case is the same structure with a different value, not a special mode.
- **(5.10b) MUST** A host that re-solves a clock mapping on import expresses its improved estimate as a **new `TimebaseRelation` from `timebase_ref`**, never as a rewrite of it. Otherwise re-solving becomes exactly the destructive merge [§8.5](#85-reconciliation) forbids.
- **(5.10c) MUST** A Session is valid with any subset of Streams, including none and including video-only (I12).
- **(5.10d) MUST** `completeness` is asserted by the peer that owns the data. A partially transferred session MUST NOT present as whole.

**`ContextChange`** is a timestamped change, not a per-shot attribute: "7-iron from shot 12" is one record, not twelve.

| Field | Type | Card. |
|---|---|---|
| `id` | `Id` | 1 |
| `at` | `Instant` | 1 |
| `kind` | `club` \| `shot_type` \| … (open registry) | 1 |
| `value` | string | 1 |

### 5.11 Stream

The **contract**: what is invariant for the stream's lifetime.

| Field | Type | Card. | Notes |
|---|---|---|---|
| `id` | `Id` | 1 | Unique within the Session. |
| `session_id` | `Id` | 1 | |
| `source_id` | `Id` | 1 | |
| `kind` | `video` \| `audio` \| `imu` \| `wrist` \| `event` \| `metadata` \| … | 1 | Open registry. |
| `profile_id` | `Id` | 1 | Activated from the Source's supported set. |
| `calibration_id` | `Id` | 0..1 | Restated for locality; fixed for the stream's lifetime. |
| `timebase_id` | `Id` | 1 | Inherited from the Source, restated for locality. |
| `continuity` | `continuous` \| `shot_windowed` | 1 | See below. |
| `opened_at` | `Instant` | 1 | |
| `closed_at` | `Instant` | 0..1 | |

- **(5.11a) MUST** A Stream's `source_id`, `profile_id`, `timebase_id` and `calibration_id` are fixed for **the stream's** lifetime. A change closes the Stream and opens another *within the same Session* (I5).

Stream lifetime, not session lifetime. A knocked tripod does not end a session; it closes one Stream and opens another. A useful consequence: which shots share a calibration reads straight off the data, because Captures partition by `stream_id`.

**Continuity is load-bearing because it changes what absence means:**

| `continuity` | Absence between shots means |
|---|---|
| `shot_windowed` | correct and expected — nothing needed recording |
| `continuous` | a dropout, recorded as an explicit gap |

| Stream kind | Continuity |
|---|---|
| `video` | always `shot_windowed` — the ring buffer discards everything else; the continuous stream is never materialised |
| `audio` | `shot_windowed`, windowed on **Candidate** rather than Shot ([§5.12](#512-candidate)) |
| `imu`, `wrist` | either — continuous while armed, or windowed per shot |
| `event`, `metadata` | always `continuous` |

### 5.12 Candidate

A nomination: one observer's claim that an event occurred at a time it measured.

| Field | Type | Card. | Notes |
|---|---|---|---|
| `id` | `Id` | 1 | Minted by the nominating peer, unique within the Session. |
| `peer_id` | `Id` | 1 | |
| `source_id` | `Id` | 1 | **Mandatory** — see below. |
| `basis` | `acoustic` \| `motion` \| `external` \| … | 1 | Open registry. |
| `at` | `Instant` | 1 | In the Source's timebase, **after** any time-of-flight correction. |
| `tof_correction_ns` | `int64` | 0..1 | The correction applied, so a host can undo it. |
| `confidence` | float `0..1` | 1 | |
| `classifier` | basis-specific map | 0..1 | For `acoustic`: the transient taxonomy. Meaningless for `external`. |
| `evidence_ref` | `Id` | 0..1 | A Capture id — the audio window attached to this Candidate. |

- **(5.12a) MUST** `source_id` names a Source owned by a Peer in the Session, with a declared Timebase (I26). See [§8.1](#81-nomination) for why, and for what to do with records that have no clock.
- **(5.12b) MUST** `classifier` is interpreted only in the context of `basis`. A consumer that applies an acoustic taxonomy to an `external` candidate is in error.
- **(5.12c) MUST** Candidates are never discarded, including losers, including ones from peers whose clocks later proved badly offset, and neither is their evidence (I8). Arbitration is a conclusion; candidates are the evidence, and a host may re-derive t₀ later with a better clock estimate.

**`Candidate.id` is new in this draft.** Model draft 4 had no identifier on Candidate, which left `Shot.candidates`, evidence references and diagnostic tooling with nothing to name.

#### 5.12.1 Candidate evidence

Audio is retained in short windows attached to **Candidates**, not to Shots, and not as a continuous track.

The reasoning is diagnostic rather than verificational. The value of retained audio is explaining **why detection fired** — including when it fired wrongly: a noise in the bay, a shot in the next bay, a phone notification, music. That means the audio must survive for candidates that lost or were rejected, and a rejected candidate has no Shot.

- **(5.12.1a) MUST** Where audio evidence is retained, it is a Capture on a separate `audio` Stream anchored to the Candidate ([§5.14](#514-capture)), not muxed into the video capture.
- **(5.12.1b) MUST NOT** The protocol constrain the retention window, the emission threshold, or a retention cap. These are peer policy, exactly as frame-rate floors are host policy (I14).
- **(5.12.1c) MUST** Absence of evidence is assertable — an evicted or never-retained window is `completeness: absent` with a reason, never a dangling reference (I10).

Muxing audio into the video clip would retain the full video window of room audio per shot when the diagnostic need is a short window centred on the transient — a privacy cost taken by accident, for no benefit. The privacy consequences of candidate-attached retention, including that the candidate count is not bounded by anything the user does, are an application concern and are addressed in the requirements review rather than here; the protocol's obligation is to make the retention expressible and its absence assertable.

### 5.13 Shot

The canonical event, with a canonical instant.

| Field | Type | Card. | Notes |
|---|---|---|---|
| `id` | `Id` | 1 | Host-issued or device-minted. |
| `session_id` | `Id` | 1 | |
| `t0` | `Instant` | 1 | The canonical instant, in `Session.timebase_ref`. |
| `authority` | `host` \| `device` | 1 | |
| `issued_by` | `Id` | 1 | Peer id of the issuer. |
| `candidates` | `[Id]` | 1..n | **All of them, always** — winners and losers. |
| `captures` | `[Id]` | 0..n | |

- **(5.13a) MUST** Every Shot references at least one Candidate somewhere in the Session. A Shot MAY have zero candidates from any given peer (I6).
- **(5.13b) MUST NOT** `t0` be revised after the Shot is issued (I7). A late candidate attaches; it does not move t₀. Revision would invalidate captures already extracted against it.
- **(5.13c) MUST** `t0` is expressed in `Session.timebase_ref`.

### 5.14 Capture

The **realisation** of a Shot or a Candidate on one Stream.

| Field | Type | Card. | Notes |
|---|---|---|---|
| `id` | `Id` | 1 | |
| `anchor` | `{ shot_id: Id }` \| `{ candidate_id: Id }` | 1 | **Exactly one** (I27). |
| `stream_id` | `Id` | 1 | |
| `interval` | `Interval` | 0..1 | In the Stream's timebase. Absent when `completeness: absent`. |
| `completeness` | `complete` \| `partial` \| `absent` | 1 | Asserted, never inferred (I10). |
| `absent_reason` | `Kind` | absent: 1 | e.g. `outside_buffer`, `not_retained`, `storage_full`, `not_armed`. |
| `gaps` | `[Interval]` | 0..n | Meaningful only on `continuous` streams (I11). |
| `achieved` | `AchievedCapability` | 0..1 | |
| `transfer` | `pending` \| `in_flight` \| `present` \| `failed` | 1 | |
| `digest` | `Digest` | 0..1 | Of the payload bytes. Present once known; the basis of idempotent re-import. |
| `bytes` | int64 | 0..1 | Payload size. |

- **(5.14a) MUST** `completeness` and `transfer` are independent axes. A Capture may be `complete` + `pending` (captured fine, not yet sent) or `partial` + `present` (arrived intact, sensor dropped mid-swing).
- **(5.14b) MUST NOT** Gaps be interpolated across or implicitly spanned (I11).
- **(5.14c) MUST** `achieved` carries the per-frame exposure durations on which [§6.1](#61-canonical-instant) depends.

The two-level pattern — **Stream** as contract, **Capture** as realisation — is the same relationship as declared profile to achieved capability, and it recurs deliberately. Putting Streams inside Shots would make per-shot profile variation *expressible*, and an invariant is better enforced by having nowhere to write the violation.

Named `Capture` rather than `Segment` because for video the continuous stream is never materialised — only the clips are written, so "segment of a stream" described a fiction, and "segment" collides with HLS/DASH usage.

### 5.15 Readiness

What crosses the wire in place of a device state machine.

| Field | Type | Card. | Notes |
|---|---|---|---|
| `settled` | `bool` | 1 | Would the next shot have settled exposure? |
| `estimated_ready_ms` | int | `settled == false`: 1 | Estimated time to settled. |
| `blocked_reason` | `Kind` | 0..1 | Set where the peer cannot become ready at all: `storage_full`, `thermal_limit`, `permission_denied`, `no_source`. |

- **(5.15a) MUST NOT** A device state-machine name (`cold`, `warm`, `armed` or any equivalent) cross the wire.

Readiness is a measurement, not a state name. The host's actual question is "if I arm now, will the first shot have settled exposure?", which is a measurement. Exporting the state names would export a platform-shaped concept whose settling costs differ elsewhere; a measurement is portable and answers the question asked.

### 5.16 ShotLink

Reconciliation produces a **link, not a merge**.

| Field | Type | Card. | Notes |
|---|---|---|---|
| `id` | `Id` | 1 | |
| `local_shot_id` | `Id` | 1 | |
| `foreign_shot_id` | `Id` | 1 | |
| `foreign_session_id` | `Id` | 0..1 | Present where the shots are in different Sessions. |
| `basis` | `interval_alignment` \| `acoustic_correlation` \| `sequence_alignment` \| `manual` | 1 | Open registry. |
| `confidence` | float `0..1` | 1 | |
| `confirmed` | `bool` | 1 | |

- **(5.16a) MUST NOT** A conformant implementation provide any operation that merges or rewrites Shots on reconciliation (I9). Unconfirmed links are visible and reversible; nothing is rewritten. There is no merge operation in the model to invoke by accident.
- **(5.16b) MUST** A ShotLink is presented for confirmation before it is treated as confirmed. Sequence alignment over ~50 ordered shots with inter-shot intervals is a well-determined problem, but the confirmation requirement is about the cost of being wrong, not the difficulty of being right.

### 5.17 SessionLink

*Provisional — see [Annex B](#annex-b--open-issues).*

Two offline peers produce two Sessions on two unrelated canonical timebases. Aligning them at import — by acoustic cross-correlation of the impact transient, or by shot sequence alignment — produces, in substance, a relationship between the two sessions' canonical timebases.

That relationship is not a `TimebaseRelation`: no peer declared it, and neither peer observed the other's clock. It is not a `ShotLink`: it relates timebases, not shots. And it must not mutate either Session, both of which have immutable `timebase_ref` (I16). Model draft 4 had nowhere to put it.

| Field | Type | Card. | Notes |
|---|---|---|---|
| `id` | `Id` | 1 | |
| `from_session_id`, `to_session_id` | `Id` | 1 | Directed. |
| `from_timebase_id`, `to_timebase_id` | `Id` | 1 | The two `timebase_ref` values. |
| `class` | `affine` \| `unrelated` | 1 | |
| `offset_ns`, `skew_ppm`, `offset_sigma_ns`, `skew_sigma_ppm` | | affine: 1 | Same semantics and same mandatory-sigma rule as [§5.4](#54-timebaserelation). |
| `basis` | `acoustic_correlation` \| `sequence_alignment` \| `manual` | 1 | |
| `derived_by` | `Id` | 1 | Peer that derived it. |
| `confirmed` | `bool` | 1 | |

- **(5.17a) MUST NOT** A SessionLink alter `timebase_ref`, any Shot, or any Capture in either Session (I25).
- **(5.17b) MUST** A SessionLink carries both sigmas when `class: affine`, for the same reason a TimebaseRelation does.
- **(5.17c) MUST NOT** A SessionLink be composed with a TimebaseRelation to derive a further relation (I18 applies).

This is a derived measurement over retained evidence, which is why it is expressible at all: it carries its uncertainty and its basis, and it is a link that can be withdrawn. Support is OPTIONAL within the Offline profile at v1.

---

## 6. Timing

### 6.1 Canonical instant

**The canonical instant of a frame is mid-exposure.** This is the single most likely site of silent non-conformance in the whole protocol, because the conversion spans two entities and two implementers can each apply half of it and both believe themselves compliant. The resulting error is exposure-dependent and looks exactly like clock bias — which then corrupts the consumer's own bias estimator.

Given a sample timestamped `t` on a Stream whose profile declares `timing.convention`, and an exposure duration `d` taken from that frame's entry in `Capture.achieved.exposure_ns`:

| `convention` | Canonical instant |
|---|---|
| `mid` | `t` |
| `start` | `t + d/2` |
| `end` | `t − d/2` |
| `nominal_frame_start` | `t + frame_start_to_exposure_offset_ns + d/2` |

- **(6.1a) MUST** A consumer converts to the canonical instant before comparing timestamps across Sources, in either direction, and before computing any quantity that mixes them.
- **(6.1b) MUST** For `nominal_frame_start` the conversion uses all three of `convention`, `frame_start_to_exposure_offset_ns` and the per-frame `d`. None of the three alone, and no two of the three, are sufficient (I17).
- **(6.1c) MUST** `d` is taken per frame from `achieved`, not from the profile's exposure range. Exposure varies frame to frame under any automatic mode. Locking exposure is what makes the correction stable in practice, but the protocol MUST NOT assume the lock held.
- **(6.1d)** Where a profile's `format` is absent (a non-framed source such as an IMU), the canonical instant is `t` and `convention` MUST be `mid`.

The `nominal_frame_start` row is the change from model draft 4, which stated the offset obligation without providing a field for it. It matters because `nominal_frame_start` is what every AVFoundation source declares.

#### 6.1.1 Worked examples

*Normative by example: an implementation MUST reproduce these.*

**A — AVFoundation-style device.** `convention: nominal_frame_start`, `frame_start_to_exposure_offset_ns: 120000`, `t = 1000000000`, `d = 2000000`.

```
canonical = 1000000000 + 120000 + 1000000 = 1001120000
```

**B — FLIR-style host camera.** `convention: start`, `t = 1000000000`, `d = 500000`.

```
canonical = 1000000000 + 250000 = 1000250000
```

**C — `end` convention.** `t = 1000000000`, `d = 500000`.

```
canonical = 1000000000 − 250000 = 999750000
```

**D — the cost of skipping the conversion.** Take A and B, with both timestamps already expressed in a common timebase and both nominally 1 s. Comparing the raw timestamps gives a difference of 0. Comparing the canonical instants gives:

```
1001120000 − 1000250000 = 870000 ns = 0.87 ms
```

At 150 fps a frame period is 6.67 ms, so an implementation that skips the conversion carries a systematic error of 13% of a frame — and because `d` varies with light, the error *moves*, which is what makes it indistinguishable from clock drift.

### 6.2 Rolling shutter

For a profile with `geometry: rolling_shutter { readout_ns, direction, rows }`:

- **(6.2a) MUST** `readout_ns` is the interval between the **exposure start of the first row read** and the **exposure start of the last row read**. It is not the frame period and not the total sensor readout including blanking.
- **(6.2b) MUST** `rows` is the number of rows in the delivered image, `R`.
- **(6.2c) MUST** The canonical instant computed in [§6.1](#61-canonical-instant) is that **of the first row read**.
- **(6.2d) MUST** The canonical instant of image row `r` (0-based, `r = 0` at the top of the delivered image, `R > 1`) is:

| `direction` | Canonical instant of row `r` |
|---|---|
| `top_to_bottom` | `canonical_first + readout_ns × r / (R − 1)` |
| `bottom_to_top` | `canonical_first + readout_ns × (R − 1 − r) / (R − 1)` |

Where `R == 1`, the row instant is `canonical_first`.

Model draft 4 gave this as `readout_ns × r/R` without defining `readout_ns`'s endpoints or the row origin, which is ambiguous by one row period and reverses under `bottom_to_top`. The definition above is what the LED timecode rig measures directly — different sensor rows decode different codes, giving first-row and last-row exposure starts in one experiment.

No public platform API exposes `readout_ns`. It is calibrated per device model and shipped as data keyed by model, never as code.

### 6.3 Clock synchronisation

- **(6.3a) MUST** Peers estimate the relation between their timebases by **two-way timestamp exchange**, estimating both **offset and rate**. A one-shot handshake does not satisfy this.
- **(6.3b) MUST** The exchange runs **once per Timebase** on a multi-clock peer, and each resulting relation is declared directly ([§5.4.1](#541-the-replacement-obligation), I21).
- **(6.3c) MUST** A burst of **10–20 exchanges** is performed on connect, after any network change, and after a thermal event.
- **(6.3d) MUST NOT** The heartbeat rate set the sync rate. Liveness and measurement are separate concerns that happen to share a channel; one exchange per heartbeat converges far too slowly on skew.
- **(6.3e) MUST** The estimate is **filtered, never stepped**. A stepped offset mid-session produces a discontinuity in fused output that is very hard to diagnose later.
- **(6.3f) SHOULD** Minimum-RTT filtering. The estimator is not mandated; the declared result and its sigma are.
- **(6.3g) SHOULD** After the initial burst, maintenance exchanges continue at a rate sufficient to keep `offset_sigma_ns` within the consumer's stated policy, and at least one exchange per five seconds while the session is open.
- **(6.3h) SHOULD** A peer computes and reports the per-shot residual between its acoustic fiducial and its network clock estimate.

The exchange itself is four timestamps — `t1` send in A's timebase, `t2` receive and `t3` send in B's timebase, `t4` receive in A's timebase — carried by `sync_probe` and `sync_reply` ([`PPCP-MSG` §6](ppcp-messages.md#6-clock-synchronisation)).

**Why skew estimation is mandatory rather than an optimisation.** At 20 ppm — the measured cross-device figure, about 1.2 ms per minute — a full 150 fps frame slips every ~5.5 minutes.

Thermal events trigger a re-burst because oscillator frequency shifts with temperature: the skew estimate goes stale, not just the offset.

### 6.4 Clock discontinuity

- **(6.4a) MUST** A peer detects steps in its declared timebases and reports each as a `ClockDiscontinuity` ([§5.5](#55-clockdiscontinuity)), carrying magnitude, cause and the instant observed.
- **(6.4b) MUST** A discontinuity is reported as an **observation**, not merely as a property of the clock. `epoch_stable` declares what a clock *should* do; a discontinuity records what it *did*.

### 6.5 Wall clock

- **(6.5a) MUST** **Wall clock labels; monotonic measures.** No interval is ever computed from a `wall` timebase (I15).
- **(6.5b) SHOULD** A peer records both a wall-clock label and the monotonic instant at which the label was taken, so the two can be related without either being derived from the other. This is `Session.epoch`.

A device wall clock jumps on NTP correction, timezone change, manual adjustment and daylight saving. A recorded discontinuity is precisely the evidence that an interval computed across it would have been wrong.

---

## 7. Sessions, roles and capture control

### 7.1 Roles

| Hosts in session | `Session.timebase_ref` | `Shot.authority` | Arbitration |
|---|---|---|---|
| 0 | a capturing peer's timebase | `device` | **none occurs** ([§8.3](#83-the-zero-host-regime)) |
| 1 | the host's timebase | `host` | the host arbitrates |

- **(7.1a) MUST** At most one peer per Session declares `role: host` (I20). Not exactly one: sessions exist with no host at all, and that is the normal case for an entry-level capture device.
- **(7.1b)** Multi-host is unmodelled. Two offline peers are two independent Sessions, reconciled at import ([§8.5](#85-reconciliation)).

### 7.2 Session lifecycle

1. Peers connect and negotiate version and extensions ([§10](#10-versioning-extensions-and-registries)).
2. Both peers **declare** — peer, timebases, relations, sources, profiles, calibration. Declaration is symmetric and neither side may skip it.
3. The recipient of a declaration applies its own ingest policy and accepts or rejects, with a reason.
4. Synchronisation burst, per timebase.
5. Session opens with `timebase_ref` and parameters fixed.
6. Streams open; capture control proceeds.
7. Session closes, or the link drops and the peer continues under [§8.3](#83-the-zero-host-regime).

- **(7.2a) MUST** A peer declares before it originates any Source-, Stream- or Candidate-bearing message.
- **(7.2b) MUST** Rejection under ingest policy carries a machine-readable reason and does not close the connection. Whether a rate, resolution or optical figure is acceptable is host policy and appears nowhere in this specification (I14).

### 7.3 Streams and capture control

- **(7.3a) MUST** In a session with a host, capture start and stop are **host-controlled**. A capture peer does not arm itself.
- **(7.3b) MUST** With no host, the capturing peer controls its own arming; the sequence of messages recorded in the bundle is otherwise identical.
- **(7.3c) MUST** A capture peer reports `Readiness` in response to `arm` and again whenever `settled` changes.
- **(7.3d) MUST** A capture peer reports and recovers from platform interruptions — an incoming call, an audio session interruption, backgrounding — with automatic re-arm where it was armed, and with the resulting gap recorded explicitly.
- **(7.3e) MUST** Arm and disarm cycle freely within a single open session. Armed-and-reviewing is a normal state, not an edge case.

### 7.4 Liveness

- **(7.4a) MUST** In a live session the host sends `heartbeat` at `Session.heartbeat_interval_ms` (default 1000).
- **(7.4b) MUST** The heartbeat acknowledgement carries the peer's current `ThermalLevel`, free storage and battery state, so a host reports degradation rather than silently accepting worse data.
- **(7.4c) MUST** A peer treats the link as lost after three consecutive missed intervals and enters [§8.3](#83-the-zero-host-regime).
- **(7.4d) MUST NOT** Loss of the link cost a single captured frame. **Capture is non-recoverable; transfer is retryable.** Under any resource constraint, transfer, replay and reporting degrade before capture does.

---

## 8. Shot determination

### 8.1 Nomination

- **(8.1a) MUST** A Candidate names a `source_id` belonging to a Source owned by a Peer in the Session, whose Timebase is declared (I26).
- **(8.1b) MUST** A **file-imported or otherwise externally-recorded shot record with no peer, no timebase and no clock relation is not a Candidate**. It is reconciled after the fact through `ShotLink` ([§8.5](#85-reconciliation)) and never enters arbitration.
- **(8.1c) MUST** A live external nominator — a launch monitor connected as a peer, or a Source owned by a peer — is modelled as a Source with `kind: launch_monitor`, with its own clock and calibration, and nominates normally.

8.1b is a change of emphasis from model draft 4 and answers a specific finding: the launch monitor this project actually integrates with today is a filesystem-watched CSV, not a socket peer. A CSV record has no Peer, no Timebase and no clock relation, so it cannot be a Source in the sense 8.1a requires. Left unstated, an implementer reads the nomination rule and the reconciliation rule as describing one path and builds a clock relation for a CSV.

There are therefore **two distinct paths, deliberately not unified**:

| | Live nomination | Offline record |
|---|---|---|
| Has a clock relation | yes | no |
| Enters arbitration | yes | no |
| Represented as | `Candidate` | reconciled to a Shot by `ShotLink` |
| Confirmation required | no | **yes** |

`source_id` stays mandatory on Candidate. An optional `source_id` would strand a Candidate with no calibration to apply — and for an acoustic candidate, calibration is where the time-of-flight constant lives.

- **(8.1d) MUST** An acoustic nominator corrects for acoustic time of flight before emitting `at`, using its Source's calibration, and SHOULD report the correction applied in `tof_correction_ns`.

At 343 m/s the correction is ~2.9 ms per metre; a device 2 m from the ball lags 5.8 ms, which is most of a frame at 150 fps. Host and device microphone distances differ, which is why two microphones are two Sources with two calibrations rather than one shared constant.

### 8.2 Arbitration

Available only to a peer with `role: host` (I20).

- **(8.2a) MUST** The host converts every Candidate into `Session.timebase_ref` using the current `TimebaseRelation` set **and** the canonical-instant conversion of [§6.1](#61-canonical-instant), before comparing them.
- **(8.2b) MUST** Two Candidates are treated as nominating the same Shot if their converted instants fall within `Session.coincidence_window_ns`.
- **(8.2c) MUST** `coincidence_window_ns` is a declared Session parameter, not a constant. Acoustic time-of-flight spread sets its floor and that is rig-dependent. The default is 50 ms.
- **(8.2d) MUST** A Candidate whose relation to `timebase_ref` is missing, `unrelated`, or too uncertain under host policy is **excluded from arbitration and retained** (I8). Exclusion is a conclusion; the Candidate remains evidence.
- **(8.2e) MUST** A Candidate arriving after the Shot has been issued attaches to that Shot. `t0` is **not** revised (I7). The host MAY re-derive t₀ offline from the retained candidate set; that produces a new analysis, not a mutation of the Shot.
- **(8.2f) MUST** The issued Shot retains references to **every** contributing and excluded Candidate.

### 8.3 The zero-host regime

- **(8.3a) MUST** In a Session with no `host`, **every Candidate becomes exactly one Shot** with `authority: device`, and **no coincidence window is applied** (I23).
- **(8.3b) MUST** A peer that issues Shots implements the **Mint** profile ([§2.2](#22-conformance-profiles)).
- **(8.3c) MUST** Shot ids minted by a peer are unique within the Session and SHOULD be UUIDs. A peer MUST NOT mint an id in another peer's namespace.
- **(8.3d) MUST** A peer whose host link drops mid-session enters this regime for the duration, mints Shots locally, queues Captures as `transfer: pending`, and reconciles the minted Shots on reconnect through `ShotLink`.

This is a **different regime**, not a special case of single-nominator arbitration. Applying a coincidence window here would collapse distinct candidates and produce subtly different output from the same acoustic evidence — which is precisely why it is a separately-testable invariant.

### 8.4 Orphan capture requests

- **(8.4a) MUST** A capture peer serves a capture request for a `t0` it never nominated, converting `t0` into its own timebase and locating the interval in its buffer.
- **(8.4b) MUST** Where the interval is no longer retained, the peer responds with a Capture of `completeness: absent` and `absent_reason: outside_buffer`. Absence is asserted, never inferred from a missing payload (I10).

This is why `Shot.candidates` is non-empty **per Session** and not per peer: a Shot may have zero candidates from a peer and a Capture from that same peer.

### 8.5 Reconciliation

- **(8.5a) MUST** Reconciliation creates **links**. No entity is rewritten or merged (I9).
- **(8.5b) MUST NOT** An implementation auto-merge. Candidate matches are surfaced and confirmation is required.
- **(8.5c) MUST** Re-import of a session already held is a no-op, never a duplicate. Identity is `Session.id` plus the minting `Peer.id`; Capture identity is `Capture.digest`.
- **(8.5d) MUST** A host that re-solves a clock mapping on import declares a **new relation from `Session.timebase_ref`** and leaves `timebase_ref` untouched (I16).
- **(8.5e)** Cross-session alignment, where implemented, is a `SessionLink` ([§5.17](#517-sessionlink)) and mutates neither Session (I25).

The host may already hold partial data for the same session — a launch monitor record, or an online portion captured before the link failed. A silent mis-merge corrupts the session record in a way that is hard to notice and harder to undo.

---

## 9. Offline sessions and bundles

**An exported offline session is a recorded PPCP message stream replayed from a file.** A consumer gains a *file transport*, not an importer.

- **(9a) MUST** A bundle contains the same messages, in the same encoding and framing, as the live path ([`PPCP-ENC` §6](ppcp-encoding.md#7-bundle-container)). An implementation that can parse a live session can parse a bundle.
- **(9b) MUST** A bundle carries metadata and non-bulk streams **before** bulk payload, so an interrupted transfer still yields an analysable session.
- **(9c) MUST** Bulk transfer is chunked, resumable and content-addressed.
- **(9d) MUST** `Session.completeness` is explicit ([§5.10](#510-session)).
- **(9e) MUST** Any subset of Streams is a valid bundle — video-only, sensor-only, or any combination (I12).
- **(9f) MUST** Sensor dropout is recorded as an explicit gap with timestamps, never interpolated across (I11). Offline there is no host to notice a dropout.

### 9.1 Clock authority inverts

Offline, the capturing peer is the session's time authority and must do the job a host's fusion layer otherwise performs.

- **(9.1a) MUST** A peer relaying a sensor estimates the peer↔sensor clock mapping **live and continuously**, per sensor, using the machinery of [§6.3](#63-clock-synchronisation) pointed at the sensor link.
- **(9.1b) MUST** The bundle carries **both** the estimated mapping **and** the raw arrival evidence — packet arrival times, connection-interval jitter, round-trip characteristics.
- **(9.1c) MUST** Every offline sample is expressed in a declared timebase with stated uncertainty.
- **(9.1d)** The peer's estimate is a **prior**, not authoritative. A consumer may re-solve on import with better algorithms, subject to 8.5d.

9.1b is the requirement most expensive to retrofit. The evidence needed to solve the sensor↔peer mapping exists only at capture time; a peer that records raw sensor timestamps and defers reconciliation to import has destroyed the information required to do it well, irrecoverably.

---

## 10. Versioning, extensions and registries

### 10.1 Version negotiation

- **(10.1a) MUST** Version and extension negotiation occurs in the first message each peer sends ([`PPCP-MSG` §3](ppcp-messages.md#3-connection-and-declaration)).
- **(10.1b) MUST** The wire version is `MAJOR.MINOR`. A change that removes a field, narrows a type, or changes the meaning of an existing field increments MAJOR. Additive change increments MINOR.
- **(10.1c) MUST** Peers operate at the highest MINOR both support within a common MAJOR. No common MAJOR is a connection failure with `error` / `unsupported_version`.
- **(10.1d) MUST** Unknown fields, unknown `kind` values, unknown `basis` values and unknown profile fields are **ignored, never fatal**, on both ends (I13).
- **(10.1e) SHOULD** An implementation publish its support window — how many versions back it accepts — and a deprecation path. Old-client/new-host is the permanent normal case for an application distributed through an app store, not an edge case.

### 10.2 Extensions

- **(10.2a) MUST** An extension is identified by a reverse-DNS string and declared in `Peer.protocol.extensions`.
- **(10.2b) MUST NOT** An extension change the meaning of any field defined in this specification.
- **(10.2c) MUST** A peer that does not implement a declared extension ignores its messages and fields.

### 10.3 Registries

`Source.kind`, `Stream.kind`, `Candidate.basis`, `Calibration.kind`, `ContextChange.kind`, `ShotLink.basis`, `ClockDiscontinuity.cause` and `Capture.absent_reason` are **open registries**.

- **(10.3a) MUST** Unknown values are ignored, never fatal (I13).
- **(10.3b) MUST** Vendor-defined values are namespaced with a reverse-DNS prefix — `com.example.forceplate` — so third parties may extend without coordination.
- **(10.3c) SHOULD** Unprefixed values are reserved for the published registry, maintained in the `libppcp` repository.
- **(10.3d) SHOULD** A vendor value that proves generally useful is proposed for the unprefixed registry rather than remaining vendor-scoped indefinitely.

Without 10.3b the first third party to add a sensor type either collides with a future core value or forks the protocol.

---

## 11. Invariants

**Twenty-eight invariants.** Each is a conformance test; [`PPCP-CONF`](ppcp-conformance.md) maps each to its required test. Identifiers I1–I21 are unchanged in number from model draft 4; I6 and I17 are amended in text, and I22–I28 are new.

| # | Invariant | Profile |
|---|---|---|
| **I1** | Every timestamp carries a `timebase_id`. There is no default timebase. | Core |
| **I2** | No sample's time is derivable from its index. Sequence numbers are for loss detection only. | Core |
| **I3** | Every `TimebaseRelation` is `affine` or `unrelated`; `affine` without both sigma fields is malformed. | Core |
| **I4** | Two Sources on the same clock share a timebase id. Identity is never asserted by relation. | Core |
| **I5** | A Stream's source, profile, timebase and calibration are fixed for **the stream's** lifetime. A change closes the Stream and opens another within the same Session. | Capture |
| **I6** | Every Shot references ≥1 Candidate somewhere in the Session; a Shot may have 0 candidates from any given peer. *(Reassigned from Detect: a Detect-only peer never issues a Shot, so I6 could not be tested against it. It binds both profiles that do.)* | **Mint, Arbitrate** |
| **I7** | `t0` is never revised after the Shot is issued. | Mint, Arbitrate |
| **I8** | Candidates are never discarded, including losers and excluded ones, and neither is their evidence. | Arbitrate |
| **I9** | Reconciliation creates links; no entity is rewritten or merged. | Offline |
| **I10** | `completeness` is asserted, never inferred from arrival. | Capture |
| **I11** | Gaps are explicit, never spanned, and meaningful only on `continuous` streams. | Capture |
| **I12** | A Session is valid with any subset of streams, including video-only. | Capture |
| **I13** | Unknown fields, `kind` values, `basis` values and profile fields are ignored, never fatal. | Core |
| **I14** | No frame-rate, resolution, quality or confidence threshold appears in the model. | Core |
| **I15** | Wall-clock values are never used to compute an interval. | Offline |
| **I16** | `Session.timebase_ref` is immutable. An improved estimate is a new relation, never a rewrite. | Offline |
| **I17** | Converting a sample to its canonical instant requires the profile's `timing.convention`, that frame's exposure duration from `achieved`, **and — where `convention == nominal_frame_start` — `timing.frame_start_to_exposure_offset_ns`**. No subset is sufficient. *(Amended: model draft 4 named two inputs, so an implementation could satisfy it and still be wrong on the default mobile path.)* | Capture |
| **I18** | `TimebaseRelation` is never composed. A needed relation is measured and declared directly. | Core |
| **I19** | Every Source declares `timing`, `geometry` and `intrinsics` regardless of which peer owns it. No convention is implied by peer role, product or platform. | Core |
| **I20** | A Session has at most one peer with `role: host`. Arbitrate is available only to that peer. | Arbitrate |
| **I21** | The per-timebase sync obligation binds every multi-clock peer, hosts included. | Live |
| **I22** | `timing.frame_start_to_exposure_offset_ns` is present if and only if `convention == nominal_frame_start`, and is declared explicitly even when zero. | Capture |
| **I23** | In a Session with no host, every Candidate becomes exactly one Shot and no coincidence window is applied. | Mint |
| **I24** | Profiles gate origination, not comprehension. Every conformant peer parses the complete type vocabulary; a peer originates only messages its declared profiles confer. | Core |
| **I25** | Cross-session alignment is a `SessionLink`. It mutates neither Session and is never composed with a `TimebaseRelation`. | Offline |
| **I26** | A Candidate references a Source owned by a Peer in the Session with a declared Timebase. A record without one is reconciled by `ShotLink`, never nominated. | Detect |
| **I27** | Every Capture anchors to exactly one of a Shot or a Candidate. | Capture |
| **I28** | `MeasuredCapability`, where present, declares `method` and `duration_ns`; its absence means not measured and MUST NOT be inferred or synthesised. | Capture |

---

## 12. Security considerations

PPCP itself defines **no security model**. Pairing, authentication, encryption, key derivation, replay resistance and the question of whether a peer may rejoin a session after reconnecting without re-pairing are all delegated to [`PPCP-RV`](ppcp-rv-scope.md).

This is defensible only if the companion document exists. It does not yet. Until it does, **PPCP has no security model at all, delegated or otherwise**, and that is a gap rather than a design ([Annex B](#annex-b--open-issues)).

What this specification does require:

- **(12a) MUST** An implementation that carries capture payload over an untrusted network does so over an authenticated, encrypted transport. PPCP does not provide one.
- **(12b) MUST NOT** A peer accept declarations, capture requests or session control from an unauthenticated counterpart.
- **(12c)** `Peer.id` is a persistent identifier with a privacy dimension ([§5.2.1](#521-peer-identity)). Its generation and lifetime rules are normative here; its exposure during rendezvous is a `PPCP-RV` concern.

## 13. Privacy considerations

- **(13a) MUST** Retention of Candidate-attached audio evidence is expressible, bounded by peer policy, and its absence assertable ([§5.12.1](#5121-candidate-evidence)).
- **(13b) MUST NOT** The protocol require, or an implementation silently perform, retention of a continuous audio track.
- **(13c)** Candidate-attached retention means audio is kept for events that were **not** shots — an adjacent player, a dropped club, speech. The count of such events is not bounded by anything the user does. An implementation's user-visible retention statement is an application obligation, but the protocol's shape is what makes an honest statement possible, and implementers should not read "audio attaches to candidates" as a smaller retention posture than it is.
- **(13d) MUST NOT** PPCP carry telemetry. Diagnostic export is user-initiated and out of band.

---

## Annex A — Non-normative implementation guidance

### A.1 Build order

The dependency structure is stricter than it looks, because later stages need earlier ones as **test infrastructure** rather than only as code.

1. **Timebase, relations and the injectable clock.** Everything depends on this and nothing tests it without simulated offset and skew.
2. **The canonical-instant conversion** ([§6.1](#61-canonical-instant)) with the worked examples. Cheap now, and the most likely site of silent non-conformance.
3. **Declaration and capability, both directions.** Implement the host side declaring its own Sources at the same time, or symmetry becomes an afterthought and I19 goes untested.
4. **Fixture format and the software simulator.** Before capture, because everything after this is easier to test against a fixture than against a phone and a golf swing.
5. **Bundle read and write.** Ahead of the live path: it exercises the same messages with no timing pressure, and it is what v1 ships.
6. **Capture, ring buffer, arm and disarm.**
7. **Candidate nomination** (Detect).
8. **Shot minting** (Mint). *New step — this is the operation the v1 offline device actually performs, and model draft 4's build order omitted it entirely.*
9. **Live sync, heartbeat, transfer.**
10. **Arbitration** (Arbitrate, host only).

Step 5 before step 9 is the ordering most likely to be reversed by instinct. Resist it: the bundle path is the same protocol without the clock pressure, so bugs found there are cheaper.

### A.2 Where conformance will silently fail

Six places an implementation will appear to work while being wrong. Each needs an explicit test, because normal use will not surface it. [`PPCP-CONF` §4](ppcp-conformance.md#4-the-silent-failure-tests) specifies them.

| Site | Why it survives normal use |
|---|---|
| **The canonical-instant conversion (I17, I22)** | Spans two entities and now three inputs, so two implementers can each apply part of the correction and both believe themselves compliant. The error is exposure-dependent and looks exactly like clock bias. |
| **`nominal_frame_start`'s offset (I22)** | It is the default path on the entire mobile side, and a missing offset is a small constant error that a bias estimator will absorb and mis-attribute. |
| **Host-side declaration (I19)** | A single-vendor implementation satisfies it *by accident*, because its host conventions are correct in hardcoded form. Test against a synthetic host declaring a different convention. |
| **The zero-host path (I20, I23)** | Never exercised in a studio. Test a hostless session end to end, including that no coincidence window is applied. |
| **Relation composition (I18)** | An implementer will compose relations silently because it is convenient and appears to work. |
| **Comprehension versus origination (I24)** | An implementation that only ever talks to itself never sees a message from a profile it lacks. |

### A.3 One implementation, both ends

`libppcp` is the reference implementation and both ends link it. This is not a convenience: two hand-written implementations of a wire format always drift, and the drift surfaces as timing bugs that look like hardware faults.

The corollary for a mobile team: the protocol layer is not Swift or Kotlin. It is the shared portable core, wrapped natively, with no platform type crossing that boundary.

### A.4 What to write down when you disagree

[Annex B](#annex-b--open-issues) lists what is known to be unsettled. If implementation reveals any of it to be wrong, that is the expected outcome, not a failure of the specification — but **the change belongs in the specification first and the code second**, or the document stops describing the system.

---

## Annex B — Open issues

Tracked against Draft 1. Each is expected to close before `ppcp/1.0` is declared stable.

| # | Issue | Status |
|---|---|---|
| **B1** | **`PPCP-RV` does not exist.** Service type and TXT contents, QR payload format and its version marker, PSK derivation and TLS-PSK identity format, and the optional SSID/passphrase extension are all unspecified. Two conformant peers cannot find one another. | **Blocking interoperability**, not blocking implementation. [Scope drafted](ppcp-rv-scope.md). |
| **B2** | **`SessionLink` is untested** ([§5.17](#517-sessionlink)). Resolved rather than deferred so implementers do not invent divergent forms, but nothing has exercised it. Support is OPTIONAL at v1. | Provisional. Re-examine when offline multi-device is built. |
| **B3** | **Source ownership transfer mid-session.** Ownership is settled at session start. Whether it may move afterwards — relevant if a host disconnects and a capture peer should take over a wrist sensor rather than lose it — is unspecified. Probably wants to be legal. | Open. |
| **B4** | **Launch monitor as a Source `kind`.** A call taken rather than deferred. Defensible to reverse if connected launch monitors turn out not to have a stable clock or calibration worth modelling — but note that the *file-imported* case is now explicitly a different path ([§8.1](#81-nomination)). | Settled unless implementation contradicts it. |
| **B5** | **Per-timebase sync obligation stated in the model** ([§5.4.1](#541-the-replacement-obligation)) rather than left to the message layer. Arguably a protocol behaviour rather than a structural fact. | Settled; recorded so it is not re-litigated silently. |
| **B6** | **Support window** ([§10.1e](#101-version-negotiation)) is a SHOULD with no stated value. Needs a number and a deprecation path before v1.0. | Open — needs a product decision. |
| **B7** | **Candidate audio retention has no protocol-level bound**, by design (I14). The consequence for a user-visible retention statement is an application obligation, and the two teams should confirm they are content with that division. | Open — needs confirmation, not a protocol change. |
| **B8** | **Coincidence window default of 50 ms** is a proposal carried forward from the model, not a measurement. | Open — settle from rig data. |

---

## Annex C — Terminology

| Term | Meaning here |
|---|---|
| **Peer** | Any PPCP implementation participating in a Session. Not a synonym for device. |
| **Host** | The peer with `role: host`. At most one per Session. Arbitrates. |
| **Capture peer** | A peer with `role: capture`. Owns Sources and produces Captures. |
| **Nominate** | To emit a Candidate. |
| **Mint** | To issue a Shot from one's own Candidates, `authority: device`. |
| **Arbitrate** | To issue a Shot from the Candidates of several peers, `authority: host`. |
| **Canonical instant** | Mid-exposure, per [§6.1](#61-canonical-instant). |
| **Bundle** | A Session serialised as a recorded PPCP message stream. Not a distinct entity. |
| **Declaration** | The symmetric exchange in which each peer states its timebases, sources, profiles and calibration. |
