# PPCP — Core Specification

**PinPoint Capture Protocol. Normative specification of the entity model, timing contract, session and shot semantics.**

| | |
|---|---|
| Document | `PPCP-CORE` |
| Version | **1.0** |
| Wire version | `ppcp/1.0` |
| Status | **APPROVED for implementation**, 22 August 2026 |
| Revision | 9 — final |
| Date | 22 August 2026 |
| Editor | libppcp maintainers, `PinPoint-Golf/libppcp` |
| Basis | `capture-companion-requirements.md` (21 August 2026) and its review of 22 August 2026; `ppcp-protocol-overview.md` model draft 4 and its review of 22 August 2026 |
| Reviews | [`reviews/`](reviews/) — PinPointCapture and PinPointStudio reviews of Draft 1, both **approve to implement** |
| Companion documents | [`PPCP-MSG`](ppcp-messages.md), [`PPCP-ENC`](ppcp-encoding.md), [`PPCP-CONF`](ppcp-conformance.md), [`PPCP-RV`](ppcp-rv.md) (approved, versioned separately) |
| Licence | Specification: open. Reference implementation `libppcp`: MIT. |

---

## 0. Status of this document

**This is the approved text.** Both first-party implementation teams reviewed three times and signed off at each round; the closing findings of the third round are carried here. The reviews are in [`reviews/`](reviews/) and every finding across all four rounds is dispositioned in [`review-disposition-2026-08-22.md`](review-disposition-2026-08-22.md).

| Round | Reviewed | Findings | Outcome |
|---|---|---|---|
| 1 | The protocol overview and the companion requirements | 5 + 3 | Draft 1 |
| 2 | Draft 1 | 4 host + 4 mobile | Draft 2 |
| 3 | Draft 2 | 4 host + 5 consistency, 3 mobile | Draft 3 |
| 4 | Draft 3 | 3 host + 2 consistency, 2 mobile | **Approved** |
| 5 | Revision 5 | 6 host, 5 mobile | Revision 6 |
| — | Requirements traceability audit | 6 findings | Revision 7 |
| 6 | Revision 7 | 3 host + 2 mobile | Revision 8 |
| 7 | Revision 8 | 5 host | **Revision 9 — final** |

**Approved is not the same as stable.** Implementation proceeds against this text. `ppcp/1.0` is declared **stable** — and this document frozen against anything but errata — when the conformance suite of [`PPCP-CONF`](ppcp-conformance.md) passes on both implementations and the interoperability pairings of [`PPCP-CONF` §5](ppcp-conformance.md#5-interoperability) are demonstrated. [Annex B](#annex-b--open-issues) lists what is still expected to move; none of it blocks implementation.

[`PPCP-RV`](ppcp-rv.md) is versioned and approved separately. It is now **approved for implementation** with no open findings.

**Where this document and any earlier draft disagree, this document wins.** `docs/specification/` is the single authority on PPCP, and the specification is self-contained: the rationale motivating each decision is restated here rather than referenced out.

**Invariant identifiers are stable.** I1–I21 keep the numbering used before the specification existed, even where their text has been amended. New invariants append. Conformance documents get quoted by number; renumbering is a cost with no benefit.

**The change history is [Annex D](#annex-d--change-history)**, at the back. What changed between drafts matters to the people who argued about it and to nobody reading this for the first time.

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
| [**PPCP-RV**](ppcp-rv.md) | Normative | Rendezvous, pairing, security. **Approved for implementation**, versioned independently. Implementing it is OPTIONAL; implementing PPCP is not. |

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
| **Core** | Peer, Timebase, TimebaseRelation, ClockDiscontinuity declaration; version and extension negotiation; **`ShotLink`** | — | I1, I2, I3, I4, **I9**, I13, I14, I18, I19, I24 |
| **Capture** | Source, CaptureProfile, Stream, Capture; arm/disarm response; readiness | Core | I5, I10, I11, I12, I17, I22, I27, I28, I30, I31, I36, I38 |
| **Detect** | Candidate | Core | I26, I29, I33 |
| **Mint** | Shot issuance from the peer's **own** Candidates, `authority: device` | Core, Detect | I6, I7, I8, I23, I32 |
| **Arbitrate** | Shot issuance from **any peer's** Candidates: coincidence window, canonical t₀, `authority: host` | Core | I6, I7, I8, I20, I35 |
| **Live** | Sync exchange, heartbeat, event/payload split, session control over a live link | Core | I21 |
| **Markup** | `Annotation` — user artefacts and device-advisory navigation anchors, in either direction | Core | I37 |
| **Offline** | Bundle read and write, SessionLink, session-level reconciliation | Core | I15, I16, I25, I34 |

**Core is mandatory.** Every other profile has the dependencies stated above and no others.

**Arbitrate is available only to a peer with `role: host`** (I20). Mint is available to any peer.

**`ShotLink` is a Core type, not an Offline one.** Three obligations discharge through it — a Mint peer reconciling a Shot minted during an outage ([§8.3f](#83-the-zero-host-regime)), a Mint peer reconciling one minted after the deadline ([§8.2i](#82-arbitration)), and a host linking a crossed pair ([§8.2l](#82-arbitration), I35) — and none of them involves a bundle. Of its six bases, `arrival_pairing` and `shared_candidate` are asserted **live at capture time** and `manual` may be; only `interval_alignment`, `acoustic_correlation` and `sequence_alignment` are retrospective.

Leaving origination in Offline made `Core + Arbitrate + Live` — a legal, constructible profile set — unable to satisfy I35 without violating C2. The alternative fix, adding Offline to Mint's and Arbitrate's dependencies, would make a live-only third-party host implement a bundle container to resolve a race that happens on a socket. `SessionLink` stays in Offline, because relating two sessions really is an import-time operation.

**Arbitrate does not depend on Mint**, although both issue Shots. Mint issues from the peer's *own* Candidates and therefore needs Detect; Arbitrate issues from *any* peer's Candidates, which it reads without being able to emit them (I24). Making Arbitrate depend on Mint would force a camera-less third-party host to declare Detect, which is exactly the case the profile split exists to keep clean.

#### 2.2.1 Why Mint exists

Model draft 4 placed "Shot issuance, coincidence window, canonical t₀" in Arbitrate and made Arbitrate host-only, while simultaneously requiring an offline device with no host to mint Shots. The v1 PinPointCapture device therefore performed an operation none of its declared profiles granted.

The two operations are genuinely different and are now separate:

| | **Mint** | **Arbitrate** |
|---|---|---|
| Input | this peer's own Candidates | Candidates from every peer |
| Selection | promotion, by the peer's own detector policy | coincidence, by the declared window |
| Coincidence window | **not applied** | applied |
| Output | one Shot per **promoted** Candidate | one Shot per coincident group |
| `Shot.authority` | `device` | `host` |
| Available to | any peer | `role: host` only |

Applying a coincidence window in a zero-host session would collapse distinct candidates and produce different output from the same acoustic evidence. "No arbitration" is a materially different regime from "arbitration with a single nominator", and the profile split makes that testable (I23).

**Promotion is not arbitration.** Draft 1 required every Candidate to become a Shot, which is a different error: a device that correctly nominates both the impact and the ball-into-screen transient — roughly 9 ms later at 3 m, and exactly the discrimination the detector is required to attempt — minted two Shots for one swing. Promotion lets the peer decide which of its **own** candidates represents a shot, while a coincidence window decides whether **two nominators** saw the same event. Only the second is arbitration, and only the second is withheld from a hostless session.

Which candidates a peer promotes is detector tuning, and is therefore no more the protocol's business than an emission threshold is (I14). What the protocol requires is that **every** candidate is emitted and retained with its evidence, promoted or not (I8) — so a consumer can see what the detector saw and disagree with it later.

#### 2.2.2 What a profile confers

**A profile confers the right to *originate* messages and the obligation to implement the corresponding behaviour. It does not gate comprehension.**

- **(C1) MUST** Every conformant peer parses the complete type vocabulary of this specification, regardless of which profiles it implements. A peer that does not implement Detect still parses a `candidate` message and every field of a `Candidate`.
- **(C2) MUST NOT** A peer originate a message whose profile it has not declared.
- **(C3) MUST** A peer receiving a **request** it understands but whose behaviour it does not implement respond with `error` / `profile_not_supported`, never by closing the connection.
- **(C3a) MUST** *Erratum E15, 23 August 2026.* C3 binds the **REQUEST** class of [`PPCP-MSG` §11](ppcp-messages.md#11-message-index) and nothing else. An **event** a peer cannot act on is parsed and dropped, silently: `error` is a response where it answers a request and an event where it does not ([`PPCP-MSG` 10a](ppcp-messages.md#10-errors)), and answering every unactionable event with an error would put a message on the wire for every `candidate` reaching a host with no Detect — a host that is nonetheless *required* to understand it and, if it declares Arbitrate, to arbitrate over it.
- **(C3b) MUST NOT** The "Profile to originate" column of `PPCP-MSG` §11 be read as the profile a **responder** needs. The two are different questions and the catalogue answers only the first: `candidate` is conferred by **Detect** and consumed by **Arbitrate**, so a host with no Detect must not conclude from that column that it may answer a Candidate `profile_not_supported`. The profile a responder needs is the one conferring the behaviour the message asks for, which is stated in the clause defining that behaviour (finding F-L6-1, `libppcp`, session S2).

This is why a third-party host may declare `Core + Arbitrate + Live + Offline` with no Detect: it arbitrates over Candidates it can read but cannot emit. The distinction is load-bearing for sizing implementation work — the type vocabulary is common; the behaviour is not.

#### 2.2.3 Worked examples

| Implementation | Profiles |
|---|---|
| Offline-only video capture device (v1 PinPointCapture) | Core + Capture + Detect + **Mint** + Offline |
| Full mobile capture device | Core + Capture + Detect + Mint + Live + Offline + Markup |
| PinPointStudio host | Core + Capture + Detect + Arbitrate + Live + Offline + Markup |
| Second-screen observer (UC-5) | Core + Live |
| Third-party host with no cameras | Core + Arbitrate + Live + Offline |
| Bundle-reading analysis tool | Core + Offline |

Note that the v1 device's profile set changed: **Mint is new and is what v1 actually ships**. An implementation plan that omits it is short a step.

### 2.3 Invariants are conformance tests

The thirty-eight invariants of [§11](#11-invariants) are the conformance surface. An implementation that violates one is non-conformant, whatever else it does. [`PPCP-CONF`](ppcp-conformance.md) maps each to a required test.

---

## 3. Transport contract

PPCP is transport-agnostic but not transport-indifferent. An implementation MUST supply a transport meeting this contract. Discovery, addressing and authentication are **not** part of it — see [`PPCP-RV`](ppcp-rv.md).

- **(T1) MUST** Ordered, reliable, bidirectional delivery per channel. PPCP does not retransmit, reorder or checksum.
- **(T2) MUST** **At least two logically independent channels with independent flow control**: one **control** channel and at least one **bulk** channel. See [§3.1](#31-why-two-channels-is-not-negotiable).
- **(T3) MUST** Message boundaries, either from the transport or from the PPCP framing of [`PPCP-ENC`](ppcp-encoding.md).
- **(T4) MUST NOT** PPCP assumes nothing about addressing, discovery or authentication.
- **(T5) MUST** The transport preserves the two channels' independence end to end. Multiplexing both onto one flow-controlled stream does not satisfy T2 however the multiplexing is done.

Channel numbering, and the mapping from channels to transport streams, is specified in [`PPCP-ENC` §2](ppcp-encoding.md#2-channels). Where each channel is its own stream, the streams of one peer are bound into a link by `link_bind` ([`PPCP-ENC` §2.1](ppcp-encoding.md#21-binding-streams-to-a-link), erratum E1).

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
| `Estimate` | A value with its dispersion: `{ value_ns: int64, sigma_ns: Sigma }`. Both fields are mandatory together — a point estimate with no dispersion is what silently corrupts fusion. |
| `Instant` | A point in time: `{ tb: Id, ns: int64 }` — a timebase identifier and a signed nanosecond count in that timebase. **There is no `Instant` without a `tb`** (I1). |
| `Series` | Many points in one timebase: `{ tb: Id, ns: [int64] }`. Still carries `tb`, so I1 holds. |
| `Duration` | `int64` nanoseconds. Timebase-free: a duration is not a point in time. |
| `Interval` | `{ tb: Id, start_ns: int64, end_ns: int64 }`, `start_ns <= end_ns`, half-open `[start, end)`. |
| `Sigma` | Non-negative standard deviation, in the unit of the quantity it qualifies. Named `*_sigma`. |
| `Kind` | Open-registry string. See [§10.3](#103-registries). |
| `Digest` | `{ alg: "sha-256", value: bytes }`. |

- **(5.1a) MUST** An `Id` minted by a peer is stable for the lifetime of the entity it names and is **not derived from mutable local state** — a filesystem path, a directory name, an ordinal that renumbers on reindex. Idempotent re-import keys on `Session.id` plus the minting `Peer.id` ([§8.5c](#85-reconciliation)), and a Capture on its digest; an identifier that changes when storage is reorganised silently defeats both.
- **(5.1b) SHOULD** Minted identifiers are UUIDs. Where they are not, they carry the minting peer's own namespace so two peers cannot collide.

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

Whether a peer may rejoin a session after reconnecting without re-pairing is a rendezvous question, delegated to [`PPCP-RV` §7.5](ppcp-rv.md#75-reconnecting-within-a-session).

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
- **(5.3c) MUST** *Erratum E11, 23 August 2026.* `Timebase.kind` is a **closed** enumeration — `monotonic`, `continuous`, `wall` — and is **not** one of the open registries of [§10.3](#103-registries). A declaration carrying an unrecognised `kind` is rejected as `malformed`; a peer MUST NOT ignore it and MUST NOT default it. It is the one `kind` in this specification that cannot be skipped: whether a clock halts across sleep and whether it is subject to jumps are the two facts every conversion, every relation and I15 depend on, and a peer that ignored an unknown value would be choosing one of those answers silently. [10.1d](#101-version-negotiation)'s "unknown `kind` values are ignored, never fatal" is scoped by this clause to the open registries §10.3 lists. Widening the enumeration is a MAJOR change (finding by `libppcp`, session S1; the closed reading was implemented and is confirmed here).

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
| `evidence_stream_id` | `Id` | 0..1 | The **Stream** carrying raw evidence for this relation — sensor packet arrivals, connection-interval jitter. Carried as stream-anchored Captures ([§5.11.1](#5111-how-a-continuous-stream-is-carried)). |

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
| `optics` | `Kind` | 0..1 | Which lens this Source is — `wide`, `ultra_wide`, `telephoto`, … Open registry. |
| `viewpoint` | `{ label: Kind, method: declared \| classified, confidence: float }` | 0..1 | Where this Source is looking from — `dtl`, `face_on`, `rear`, … Open registry. `confidence` is present **if and only if** `method: classified`. |
| `label` | string | 0..1 | Human-readable, informational. |

- **(5.6a) MUST** Every Source declares `timebase_id`, and every CaptureProfile it offers declares `timing`, `geometry` and `intrinsics` — **regardless of which peer owns the Source** (I19). No convention is implied by peer role, product or platform.
- **(5.6b) SHOULD NOT** A capture peer open a virtual multi-lens device. Where it does, it MUST declare `physical: false`, because such devices switch physical lenses automatically on scene and focus distance, silently changing intrinsics mid-session.
- **(5.6c) MUST** Where a device and a host are both capable of owning a sensor connection, ownership is settled at session start and expressed solely by `peer_id`. The same wrist sensor is the same Source whichever peer holds the BLE connection.
- **(5.6d) MUST** **A physically distinct lens is a distinct Source.** A peer MUST NOT present two lenses as one Source, and `optics` names which one. Lens choice is calibration-affecting, so it is fixed for a Stream's lifetime by I5 like everything else about a Source — but a consumer that cannot tell *which* lens produced a bundle cannot interpret its calibration, and a device offering the same profile on both a wide and an ultra-wide lens makes that ambiguity real.
- **(5.6e) MUST** `viewpoint`, where present, declares **how it was arrived at** — `declared` where a user or an installer stated it, `classified` where the peer worked it out — and carries `confidence` **only** in the second case. A person who states "down the line" is not expressing a probability, and requiring a number there would be asking a peer to invent one, which is the pattern I28 and I31 exist to prevent. A consumer MAY disagree with either. It is a self-report, not a fact.

`viewpoint` answers a requirement that a device classify where it is looking from and *report* it, rather than asking a user to configure it. Handedness is not part of it: which way the golfer swings is a property of the session, not of a camera, and it is a `ContextChange` ([§5.10](#510-session)).

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
| `geometry` | `global` \| `rolling_shutter { readout_ns, readout_provenance, readout_sigma_ns, direction, rows }` | camera: 1 | Per **profile**, not per source: readout time differs per mode. |
| `timing` | `Timing` — see below | 1 | |
| `intrinsics` | `per_frame` \| `fixed` \| `none` | camera: 1 | |
| `measured` | `MeasuredCapability` | 0..1 | **Absence means not measured** (I28). |

**`Timing`:**

| Field | Type | Card. | Notes |
|---|---|---|---|
| `convention` | `mid` \| `start` \| `end` \| `nominal_frame_start` | 1 | The source's native timestamp convention. |
| `frame_start_to_exposure_offset_ns` | `int64` | see below | Signed. Fixed offset from the nominal frame start to the actual start of exposure. |
| `frame_start_to_exposure_offset_provenance` | `Provenance` | with the offset: 1 | Where the value came from. See below. |
| `frame_start_to_exposure_offset_sigma_ns` | `Sigma` | 0..1 | SHOULD be present where provenance is `measured`. |

**`Provenance`** — `assumed` \| `vendor` \| `measured`.

| Value | Meaning |
|---|---|
| `assumed` | Not measured for this device model. A placeholder, frequently zero. |
| `vendor` | From a vendor document or a platform API that states it. |
| `measured` | Measured directly on **this device model**, by the declaring project. |

- **(5.7a) MUST** `frame_start_to_exposure_offset_ns` is present **if and only if** `convention == nominal_frame_start` (I22).
- **(5.7b) MUST** Where it is present it is declared explicitly, including when it is zero, and **always with its provenance** (I31). A declared zero is a checkable claim; an omitted field is not — but a declared zero with no provenance is indistinguishable from an unmeasured one, which is the same defect `MeasuredCapability.method` exists to prevent one layer up.
- **(5.7e) MUST** `rolling_shutter.readout_ns` carries `readout_provenance` on the same terms, and for the same reason: no public platform API exposes it, so an implementation that has not been through a timecode rig is guessing (I31).
- **(5.7f) MUST NOT** A peer declare `measured` for a value it obtained from anything other than a direct measurement of that device model.
- **(5.7c) MUST** `measured` results attach **per profile**. 1080p240 and 1080p120 are separate self-tests with separate results.
- **(5.7d) MUST NOT** Any frame-rate, resolution, quality or confidence threshold appear in a profile or anywhere else in this specification (I14). Acceptance is host policy, expressed outside the protocol.

`nominal_frame_start` is what **every AVFoundation source declares**, so it is the default path for the entire mobile side, and this offset is exactly the quantity that makes the conversion of [§6.1](#61-canonical-instant) correct rather than approximately correct.

Provenance is not bureaucracy. Both quantities come from a timecode rig, per device model, and at the time of writing **no model has been through one** — so every early implementation declares a placeholder. Without provenance a host cannot distinguish a measured zero from an unmeasured one, and an unmeasured offset does not merely overstate a capability: it silently biases every cross-source comparison the host makes, in a way that moves with exposure and therefore looks like clock drift ([§6.1.1](#611-worked-examples), example D).

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

- **(5.8k)** A `MeasuredCapability` describes its profile **running alone** unless the peer states otherwise. A peer SHOULD re-measure, or qualify the figure, where it expects a `preview` or any second Stream from the same Source to run concurrently.

5.8a and 5.8b answer a specific implementation finding: onboarding affords seconds, sustained verification wants tens of minutes, and without `method` the cold number quietly becomes the displayed one. 5.8k is the same principle applied to concurrency, which revision 5 made reachable: every self-test is taken with one profile running, and a second concurrent encode shares the same hardware encoder, thermal budget and bulk path. The order of events is what makes it bite — a consumer reads `measured`, accepts the peer under its ingest policy, and *then* opens a preview, so the acceptance was decided against a figure the next action invalidated. `AchievedCapability` reports the truth per shot, which means it surfaces eventually as dropped frames on real swings.

**Achieved capability is split in two**, because the two halves have different sizes, different consumers and different urgency.

**`AchievedSummary`** — carried on `Capture.achieved_summary`, on the **control** channel, in `capture_announce`.

| Field | Type | Card. | Notes |
|---|---|---|---|
| `frame_count` | int | 0..1 | |
| `dropped_frames` | int | 0..1 | |
| `realised_rate_mhz` | int | 0..1 | Over the Capture's interval. |
| `exposure_ns` | `{ min, max, median }` | 0..1 | |
| `iso` | `{ min, max, median }` | 0..1 | |
| `thermal` | `[{ at: Instant, level: ThermalLevel }]` | 0..n | Timeline, not a single value. |

**`AchievedFrames`** — the per-frame series. Carried **with the payload**, never in `capture_announce` (I30).

| Field | Type | Card. | Notes |
|---|---|---|---|
| `frames` | `Series` | 0..1 | Per-frame source timestamps, in the stream's timebase. |
| `exposure_ns` | `[int64]` or `int64` | 0..1 | **Per frame**, parallel to `frames.ns` — or a single scalar meaning constant across this Capture. |
| `exposure_provenance` | `per_frame` \| `sampled` \| `locked_constant` | with `exposure_ns`: 1 | See below. |
| `iso` | `[int]` or `int` | 0..1 | Same parallel-or-scalar rule. |
| `intrinsics` | `[Matrix3]` or `Matrix3` | 0..1 | Same rule, where `intrinsics: per_frame`. |

- **(5.8d) MUST** On a Capture from a camera Source **that has frames** — that is, `completeness` is `complete` or `partial` — `AchievedFrames.exposure_ns` is present, in parallel or scalar form. Without it the canonical-instant conversion is impossible (I17). A Capture of `completeness: absent` has no frames and carries no `AchievedFrames`. **A `preview` Stream is exempt — see 5.8j.**
- **(5.8j)** A Capture on a `preview` Stream is exempt from 5.8d: nothing is measured from it ([§5.11g](#5112-preview-streams)), so the conversion it feeds does not exist. It still carries `frames`, because every sample is placed in time whatever it is for (I1, I2).
- **(5.8l)** *Erratum E13, 23 August 2026.* **`AchievedSummary` is optional on every Capture and its fields are individually optional**, which is what the table above already says and what a reader of [5.11b](#5111-how-a-continuous-stream-is-carried) needed stated: 5.11b requires a stream-anchored Capture on *every* `continuous` Stream, and a 100 Hz attitude Stream has no frames, no exposure and no ISO. On a Capture from a Source that produces **samples rather than frames**, the summary is read as follows where it is present at all — `frame_count` counts **samples**, `dropped_frames` counts samples the Source did not deliver, `realised_rate_mhz` is the realised **sample** rate over the Capture's interval, and `exposure_ns`, `iso` and `thermal` are absent unless the Source genuinely has them. A peer MUST NOT synthesise a camera figure to fill the shape, and a consumer MUST NOT read the absence of `achieved_summary` as a defect (finding F-D4-3, PinPointCapture, session S3).
- **(5.8e) MUST NOT** Time be inferred from frame index anywhere (I2). Frames drop; indices lie. Sequence numbers, where present, are for loss detection only. **`frames.ns` therefore has no scalar form** — a nominal rate is not a substitute for measured timestamps.
- **(5.8f) MUST** A parallel array has exactly `frames.ns` length. A scalar means the value was constant for every frame in the Capture, and MUST NOT be used to mean "unknown" or "not sampled".
- **(5.8g) MUST NOT** `AchievedFrames` be carried on `capture_announce` (I30). It travels with the payload it describes, which is also the only context in which it is interpretable. **One exception**: `capture_update` MAY carry it for a Capture whose payload will not transfer ([`PPCP-MSG` §8.2b](ppcp-messages.md#82-capture_update)), because the series would otherwise be lost with the payload — a `complete` + `failed` clip is exactly a session whose link died, and the frame timeline is what tells a consumer what it lost.

**`exposure_provenance`** answers a question the specification was previously silent on: some platforms do not attach exposure to the frame.

| Value | Meaning | Honest use |
|---|---|---|
| `per_frame` | The value the capture pipeline attached to that frame. | Only where the platform actually supplies it. |
| `sampled` | A device-level exposure property, sampled once per frame. | Exact while exposure is locked; approximate otherwise. |
| `locked_constant` | One value, applied to every frame because exposure was locked and not observed to change. | With the scalar form, and only under a lock. |

- **(5.8h) MUST NOT** A peer declare `per_frame` unless the platform attaches the value to the sample. Declaring the stronger provenance is the same error as reporting a cold sample as sustained (I31).
- **(5.8i)** A consumer decides for itself whether `sampled` is good enough for what it is computing. That is policy, and the protocol carries the fact rather than the judgement (I14).

Splitting `achieved` is a direct consequence of measuring it. At 1080p150 for three seconds the per-frame series run to roughly 44 KB — some 460 times the size of a `sync_probe` — and at 240 fps with per-frame intrinsics, roughly 70 KB. That is well inside the control-channel limit and entirely on the wrong side of the event/payload split: `capture_announce` exists so a host can correlate and display a shot *immediately*, and the per-frame series are not interpretable without the frames they describe, which arrive on bulk. The scalar form additionally collapses the common case, since a locked exposure and a locked focus make every value in three of the four series identical.

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
- **(5.9b) MUST** For `kind: position` on a `microphone` Source, `parameters` carries the **surveyed or solved** geometry from which acoustic time of flight is computed, where such a geometry exists.
- **(5.9c) MUST NOT** A peer close and reopen a Stream solely to publish a refined online estimate. A quantity that converges over a session is carried **on the observation it was applied to**, not by replacing the Stream contract.

5.9b and 5.9c together resolve a conflict Draft 1 contained. Time of flight must be estimated online and continuously — the microphone-to-ball distance is user-chosen and the golfer will not measure it — and the estimate improves with every shot. Draft 1's only home for it was `Calibration`, which I5 fixes for a Stream's lifetime, so a fifty-shot session implied up to fifty `stream_close`/`stream_open` cycles on the audio Stream, fragmenting it into dozens of Streams differing only by a converging scalar.

The resolution is not to weaken I5. Exempting `method: estimated_online` from I5 would make `Stream.calibration_id` stop identifying a fixed value, so reproducing a past conversion would need a time-indexed calibration history — and the property I5 exists to give, that a Capture's geometry is recoverable from the Stream contract alone, would be gone. Instead the **applied correction moves to the Candidate** ([§5.12](#512-candidate)), with its own uncertainty, which is where it is consumed and where its convergence is visible. `Calibration` keeps what is genuinely fixed: a surveyed position, where one has been surveyed.

### 5.10 Session

| Field | Type | Card. | Notes |
|---|---|---|---|
| `id` | `Id` | 1 | UUID. Assigned by the host where one exists, otherwise minted by the capturing peer. |
| `peers` | `[Peer]` | 1..n | With roles. |
| `timebase_ref` | `Id` | 1 | The session's canonical timebase. **IMMUTABLE once set** (I16). |
| `epoch` | `{ wall_utc_ns: int64, at: Instant }` | 0..1 | Wall-clock **label** only. Never used to compute an interval (I15). |
| `coincidence_window_ns` | `Duration` | host: 1 | **Pairwise tolerance**: are two nominations the same event. See [§8.2](#82-arbitration). Default `50000000` (50 ms). |
| `issue_hold_ns` | `Duration` | host: 1 | **Deadline**: how long a host collects before issuing. See [§8.2](#82-arbitration). Default `200000000` (200 ms). |
| `heartbeat_interval_ms` | int | Live: 1 | See [§7.4](#74-liveness). |
| `streams` | `[Stream]` | 0..n | |
| `shots` | `[Shot]` | 0..n | |
| `contexts` | `[ContextChange]` | 0..n | |
| `state` | `open` \| `closed` | 1 | |
| `completeness` | `complete` \| `partial` \| `unknown` | 1 | Explicitly asserted, never inferred from arrival (I10). |

- **(5.10a) MUST** `timebase_ref` is set when the Session opens and never changes. Online it is a host Timebase; offline it is the capturing peer's. Making it a field means the offline case is the same structure with a different value, not a special mode.
- **(5.10b) MUST** A host that re-solves a clock mapping on import expresses its improved estimate as a **new `TimebaseRelation` from `timebase_ref`**, never as a rewrite of it. Otherwise re-solving becomes exactly the destructive merge [§8.5](#85-reconciliation) forbids.
- **(5.10c) MUST** A Session is valid with any subset of Streams, including none and including video-only (I12).
- **(5.10e) MUST** `coincidence_window_ns` and `issue_hold_ns` are present **if and only if** the Session has a peer with `role: host`. Their absence is the structural statement that no arbitration occurs in this Session — the same fact I23 states in prose, expressed where it cannot be got wrong.

5.10e matters more than it looks. Both are arbitration parameters and neither has any meaning in a hostless session, which is the **normal** case for an entry-level capture device. Making them unconditionally mandatory would have every range bundle carry two numbers nothing consults, from which a reader could reasonably infer arbitration was in play — and a mandatory field cannot be made optional after 1.0.
- **(5.10d) MUST** `completeness` is asserted by the peer that owns the data. A partially transferred session MUST NOT present as whole.

**`ContextChange`** is a timestamped change, not a per-shot attribute: "7-iron from shot 12" is one record, not twelve.

| Field | Type | Card. |
|---|---|---|
| `id` | `Id` | 1 |
| `at` | `Instant` | 1 |
| `kind` | `club` \| `shot_type` \| `handedness` \| `location` \| `weather` \| … (open registry) | 1 |
| `value` | string | 1 |

- **(5.10f) MUST** `location` and `weather` are **labels**. Like `Session.epoch` they are recorded and never used to compute anything (I15's principle, applied to context rather than to time). They are carried from v1 whether or not anything consumes them, because they cannot be recovered afterwards.
- **(5.10g) MUST** `handedness` is a property of the session, not of a Source. Which way a golfer swings does not belong on a camera.

### 5.11 Stream

The **contract**: what is invariant for the stream's lifetime.

| Field | Type | Card. | Notes |
|---|---|---|---|
| `id` | `Id` | 1 | Unique within the Session. |
| `session_id` | `Id` | 1 | |
| `source_id` | `Id` | 1 | |
| `kind` | `video` \| `preview` \| `audio` \| `imu` \| `wrist` \| `event` \| `metadata` \| … | 1 | Open registry. `preview` is defined normatively in [§5.11.2](#5112-preview-streams). |
| `profile_id` | `Id` | 1 | Activated from the Source's supported set. |
| `calibration_id` | `Id` | 0..1 | Restated for locality; fixed for the stream's lifetime. |
| `timebase_id` | `Id` | 1 | Inherited from the Source, restated for locality. |
| `continuity` | `continuous` \| `shot_windowed` | 1 | See below. |
| `opened_at` | `Instant` | 1 | |
| `closed_at` | `Instant` | 0..1 | |

- **(5.11a) MUST** A Stream's `source_id`, `profile_id`, `timebase_id` and `calibration_id` are fixed for **the stream's** lifetime. A change closes the Stream and opens another *within the same Session* (I5).
- **(5.11a1) MUST** **Either peer may close a Stream** — the owner because it can no longer produce, the consumer because it no longer wants the data — and `reason` says why, from the same open vocabulary as `Readiness.blocked_reason`: `thermal_limit`, `storage_full`, `not_needed`, `calibration_changed`. A peer under sustained thermal load closes a `preview` rather than keeping it nominally open and announcing absence for the rest of the session.

Stream lifetime, not session lifetime. A knocked tripod does not end a session; it closes one Stream and opens another. A useful consequence: which shots share a calibration reads straight off the data, because Captures partition by `stream_id`.

**Continuity is load-bearing because it changes what absence means:**

| `continuity` | Absence between shots means |
|---|---|
| `shot_windowed` | correct and expected — nothing needed recording |
| `continuous` | a dropout, recorded as an explicit gap |

| Stream kind | Continuity |
|---|---|
| `video` | always `shot_windowed` — the ring buffer discards everything else; the capture stream is never materialised continuously |
| `preview` | always `continuous` — a low-rate view for live monitoring, never for measurement. See [§5.11.2](#5112-preview-streams) |
| `audio` | `shot_windowed`, windowed on **Candidate** rather than Shot ([§5.12](#512-candidate)) |
| `imu`, `wrist` | either — continuous while armed, or windowed per shot |
| `event`, `metadata` | always `continuous` |

#### 5.11.1 How a continuous Stream is carried

- **(5.11b) MUST** A `continuous` Stream is realised as a sequence of Captures anchored to **an interval of the Stream itself** ([§5.14](#514-capture)), not to any Shot or Candidate.
- **(5.11c) MUST** Those Captures and their declared `gaps` together account for the whole of the Stream's open interval — from `opened_at` to `closed_at`, or to the present. **Time accounted for by neither is a defect, not a dropout** (I36): a dropout is asserted, never inferred (I10, I11).
- **(5.11c1) MUST** The obligation binds a Session asserted `completeness: complete`. In a `partial` or `unknown` Session, time **after** the last announced Capture is the incompleteness the Session already declares, not a defect; time unaccounted for **between** announced Captures is a defect in either case. Nothing truncates a bundle in the middle.
- **(5.11c2) MUST** There are **two** ways to account for time that carries no data, and they are not interchangeable:

  | | Means | Use |
  |---|---|---|
  | **`gaps`** on a Capture | data was **lost** inside a segment that otherwise exists | a sensor dropped out mid-segment, a link stalled |
  | **an `absent` segment** — a stream-anchored Capture with `completeness: absent`, its `interval`, and an `absent_reason` | **nothing was captured** for that span | storage filled, the source was shed deliberately, a thermal limit closed it |

- **(5.11c3) MUST** **Deliberate non-retention is an `absent` segment with `absent_reason: not_retained`, never a gap.** `gaps` mean loss (I11), so a peer that sheds a preview frame on purpose and records a gap is reporting a dropout it did not have — which is exactly the conflation the continuity flag exists to prevent.
- **(5.11d) MUST** Accounting is over Captures that have been **announced**, not over payload that has arrived. `completeness` and `transfer` remain independent axes ([§5.14a](#514-capture)).
- **(5.11e)** The window length of each Capture is **the producing peer's alone** and appears nowhere in this specification (I14). A shorter window costs more messages and delivers sooner. A consumer **cannot negotiate it in `ppcp/1.0`**; what a consumer controls is which Streams it opens at all, and what it asks for is a profile, not a window.
- **(5.11e1)** **Control traffic now scales with session length rather than with shot count**, and implementers should size for it. Three continuous Streams at a one-second window is of order three `capture_announce` messages per second, against roughly fifty shot events in a session. They are small, and the control channel is protected against large *payloads* rather than against message count ([§3.1](#31-why-two-channels-is-not-negotiable)) — a shot event queued behind a few hundred bytes of announce is delayed immeasurably. The volume is understood and accepted; it is not a reason to move announces off the control channel.

Until revision 5 a `continuous` Stream could carry nothing at all. Every payload message is keyed on `capture_id`, and every Capture anchored to a Shot or a Candidate — so the interval a continuity flag exists to describe was the one interval with no carriage. Three stated obligations were unmeetable: continuous device attitude and gravity on a `metadata` Stream, which this table says is *always* continuous; the raw sensor-arrival evidence [§9.1b](#91-clock-authority-inverts) requires a bundle to carry, which `TimebaseRelation.evidence_stream_id` names; and `imu` or `wrist` running continuously while armed.

#### 5.11.2 Preview streams

A consumer watching a capture peer needs to see that the link is live **and that it reflects what the user is doing**. Heartbeat proves the first; only frames prove the second.

- **(5.11f)** A `preview` Stream is a second Stream from an existing Source, with its own `profile_id` — typically a low rate and a small frame — and `continuity: continuous`. It needs no new Source, no new message and no new machinery: it is [§5.11.1](#5111-how-a-continuous-stream-is-carried) applied to a camera.
- **(5.11g) MUST NOT** A `preview` Stream be used for measurement, pose, arbitration, or any quantity that reaches a result. It exists to be looked at.
- **(5.11h) SHOULD** A peer carry preview payload on a **bulk channel distinct** from the one carrying shot payload ([`PPCP-CORE` §3](#3-transport-contract) permits more than one), so a preview never queues behind a clip.
- **(5.11i) MUST** Under contention, preview degrades **before** transfer, which degrades before capture. Capture degrades last ([§7.4d](#74-liveness)); a preview frame is the cheapest thing in the session to drop.
- **(5.11j) MUST** A preview Capture is **live-only**. A peer that cannot deliver one promptly **discards** it rather than queueing it, and MUST NOT retain it for later transfer or write it to a bundle. A consumer therefore never sees `transfer: pending` on a preview Capture **that holds payload**. What was discarded is recorded as an `absent` segment with `absent_reason: not_retained` (5.11c3), which costs one small message and is the honest account — and *that* Capture holds no payload, has nothing to queue, and carries whatever transfer state the owner holds it in, which means nothing ([`PPCP-MSG` 8.1i1](ppcp-messages.md#81-capture_announce), erratum E16).

5.11j exists because the default state of an announced Capture is `pending`, and a queue told nothing else will do the wrong thing twice over: it fills with the cheapest data in the session, competing for the same bulk capacity as shot payload — the exact inversion 5.11i forbids, arriving through the transfer queue rather than through the channel — and then it reaches the bundle, because an exported session *is* the recorded message stream. A preview running for a ninety-minute range session would be a substantial fraction of the storage budgeted for shot video, spent on frames 5.11g forbids anyone from using for anything.

- **(5.11k) MUST** Where a `preview` Stream is open **alongside a capture Stream from the same Source**, its realised rate and format are **derived** from the active capture profile — a decimation, a downscale, or both — and its declared profile is a **request rather than an independent mode**. `AchievedSummary` on each segment reports what was actually produced. Where a `preview` Stream is the only Stream open on that Source, its profile is activated normally.
- **(5.11l) MUST** A preview profile MAY therefore describe a **derived view** rather than a mode the Source can enter, which no other `CaptureProfile` may. It is activatable only on a Stream of `kind: preview`; a peer MUST refuse it for any other Stream kind, and a consumer MUST NOT select it for capture.
- **(5.11m) MUST** A preview profile declares `intrinsics: none`. Decimation and downscaling change the intrinsic matrix, so declaring the capture profile's would be false — and 5.11g forbids anything that would consume it. `geometry` is the sensor's readout and is unchanged by decimation, so it is declared honestly.

**A camera runs one configuration at a time.** A Source capturing at its highest rate cannot simultaneously operate a small low-rate mode; the preview is produced by decimating and downscaling what the capture stream is already producing. So the same declared profile is an independent mode in one situation — preview alone, during setup and framing, which is its main use — and a derived view in the other. 5.11k puts the answer where every other realised-versus-claimed question in this specification is already answered: in `achieved`. 5.11l stops the meaning of "profile" widening silently, and stops a consumer activating a preview profile for capture and being refused a profile the peer itself advertised.

Opening one is an ordinary `stream_open` from the consumer that wants it. A peer that does not offer a suitable profile simply refuses, and nothing else changes — which is the conformant way for a peer to decline preview entirely until it has measured what a second concurrent encode costs it.

### 5.12 Candidate

A nomination: one observer's claim that an event occurred at a time it measured.

| Field | Type | Card. | Notes |
|---|---|---|---|
| `id` | `Id` | 1 | Minted by the nominating peer, unique within the Session. |
| `peer_id` | `Id` | 1 | |
| `source_id` | `Id` | 1 | **Mandatory** — see below. |
| `basis` | `acoustic` \| `motion` \| `external` \| … | 1 | Open registry. |
| `at` | `Instant` | 1 | The **canonical instant** ([§6.1](#61-canonical-instant)) in the Source's timebase, after time-of-flight correction. See 5.12e. |
| `tof_correction` | `Estimate` | 0..1 | `{ value_ns, sigma_ns }` — the correction applied and its uncertainty. Both mandatory together (I29). |
| `canonical_correction_ns` | `int64` | 0..1 | The canonical-instant correction applied, so a consumer can recover the raw timestamp. |
| `confidence` | float `0..1` | 1 | |
| `classifier` | basis-specific map | 0..1 | For `acoustic`: the transient taxonomy. Meaningless for `external`. |
| `evidence_capture_id` | `Id` | 0..1 | The **Capture** holding this Candidate's evidence — the audio window. |

- **(5.12a) MUST** `source_id` names a Source owned by a Peer in the Session, with a declared Timebase (I26). See [§8.1](#81-nomination) for why, and for what to do with records that have no clock.
- **(5.12b) MUST** `classifier` is interpreted only in the context of `basis`. A consumer that applies an acoustic taxonomy to an `external` candidate is in error.
- **(5.12c) MUST** Candidates are never discarded — losers, excluded ones, ones a minting peer chose not to promote, and ones from peers whose clocks later proved badly offset — and neither is the **record** of their evidence (I8). What is never discarded is the Candidate and its reference to evidence; the evidence **payload** may be shed under the peer's own retention policy ([§5.12.1b](#5121-candidate-evidence)), and its absence is then asserted rather than left dangling ([§5.12.1c](#5121-candidate-evidence)). Arbitration and promotion are conclusions; candidates are the evidence, and a consumer may re-derive t₀ later with a better clock estimate.
- **(5.12d) MUST** Where `tof_correction` is present it carries **both** `value_ns` and `sigma_ns` (I29). A correction with no dispersion is a point estimate of exactly the kind [§5.4a](#54-timebaserelation) refuses for clock offsets, and for the same reason.

- **(5.12e) MUST** `Candidate.at` is the **canonical instant** of the observation ([§6.1](#61-canonical-instant)), converted by the nominating peer **before emission** (I33). A consumer MUST NOT apply the canonical-instant conversion to a Candidate a second time.
- **(5.12f) SHOULD** A nominator whose Source profile declares a `convention` other than `mid` reports the correction it applied in `canonical_correction_ns`, on the same principle as `tof_correction`: the observer corrects, and the correction is visible.

**`canonical_correction_ns` is deliberately a bare integer while `tof_correction` beside it is an `Estimate` with a mandatory sigma.** The asymmetry is intended and should not be tidied away. Time of flight is a *converging* estimate whose dispersion changes shot to shot, and its sigma is the only way a consumer knows where in that convergence a given shot sits. The canonical correction is **arithmetic over declared values** — the profile's convention, its `frame_start_to_exposure_offset_ns`, and that frame's measured exposure. Its trustworthiness is not a per-shot quantity: it is `frame_start_to_exposure_offset_provenance`, which already lives on the profile under I31 and is one hop away through `source_id`. Adding an `Estimate` here would duplicate that and invite a peer to invent a per-candidate sigma it does not have.

**Why the nominator converts and not the consumer.** The conversion of [§6.1](#61-canonical-instant) needs that frame's exposure duration, and **a Candidate carries no frame reference and no exposure**. `evidence_capture_id` points at a Capture, and for an acoustic candidate that Capture is the *audio* window — there is no route from a Candidate to the exposure of the frame it came from. The nominating peer is the only party holding both.

For an acoustic candidate this was harmless by accident: a microphone Source's profile has no `format`, so [§6.1d](#61-canonical-instant) fixes `convention: mid` and the canonical instant is `t`. For a `motion` candidate — a camera-side detection, one of the three nominators the model admits — it is not. Its Source is a camera declaring `start` or `nominal_frame_start`, and a host instructed to apply `t + offset + d/2` has no `d` in reach.

The discrepancy is around 1 ms at a 6.67 ms frame period with a 2 ms exposure, which is comfortably inside any plausible coincidence window — so **arbitration still succeeds and nothing looks broken**, while `t0` carries a systematic error that moves with exposure. That is precisely the signature [§6.1](#61-canonical-instant) exists to warn about, arriving in the one place the conversion was not being applied consistently.

**Why the correction carries its own uncertainty.** Acoustic time of flight is ~2.9 ms per metre, so a device 2 m from the ball lags 5.8 ms — most of a frame at 150 fps. Where the distance is estimated online rather than surveyed ([§5.9](#59-calibration)) that estimate is *converging*: wide early in a session, tight late, and the difference is the whole point. Being able to undo the correction is not a substitute — it recovers the raw timestamp but says nothing about how far to trust the corrected one, and a consumer that undoes every correction has discarded work the nominating peer was better placed to do.

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
- **(5.13c) MUST** `t0` is expressed in `Session.timebase_ref`. A peer that cannot express it there does not issue a Shot ([§8.2i1](#82-arbitration)).
- **(5.13d) MUST** A Shot's `id`, `t0`, `authority` and `issued_by` are set by the issuer and are **never changed by another peer**. Its `candidates` list MAY be extended by any peer holding a Candidate that belongs to that Shot, by re-sending `shot` with the extended list and every other field unchanged ([§8.2e](#82-arbitration), [§8.2k](#82-arbitration)). A peer receiving an extension to a Shot it issued **MUST** adopt the extended list.
- **(5.13e)** Extension is additive and order-independent, so two peers converge on the same candidate set regardless of arrival order. Neither end has to reason about who saw what first.

5.13d exists because Draft 3 made two peers able to send `shot` for one `shot.id` — the host attaching under 8.2k to a Shot the device issued. Before that, exactly one peer ever did, and the question could not arise. I7 already protected `t0`; `authority`, `issued_by` and `id` had no stated owner and `candidates` had no stated amender, which is precisely the gap in which two reasonable implementations diverge silently and meet only at integration.

### 5.14 Capture

The **realisation** of a Shot or a Candidate on one Stream.

| Field | Type | Card. | Notes |
|---|---|---|---|
| `id` | `Id` | 1 | |
| `anchor` | `{ shot_id: Id }` \| `{ candidate_id: Id }` \| `{ stream: true }` | 1 | **Exactly one key** (I27). `{ stream: true }` is a segment of the Capture's own `continuous` Stream, belonging to no event. |
| `stream_id` | `Id` | 1 | |
| `interval` | `Interval` | 0..1 | In the Stream's timebase. **Mandatory** on a stream-anchored Capture, including an `absent` one (5.14d); **permitted on any `absent` Capture whatever its anchor** (5.14d1). For a segment the interval *is* the claim. |
| `completeness` | `complete` \| `partial` \| `absent` | 1 | Asserted, never inferred (I10). |
| `absent_reason` | `Kind` | absent: 1 | e.g. `outside_buffer`, `not_retained`, `storage_full`, `not_armed`, `thermal_limit`, `link_lost`. Open registry. |
| `gaps` | `[Interval]` | 0..n | Meaningful only on `continuous` streams (I11). |
| `achieved_summary` | `AchievedSummary` | 0..1 | Travels on control, in `capture_announce`. |
| `achieved_frames` | `AchievedFrames` | 0..1 | Travels with the payload, never on control (I30). |
| `transfer` | `pending` \| `in_flight` \| `present` \| `confirmed` \| `failed` | 1 | The **owner's** view. See 5.14f. |
| `digest` | `Digest` | 0..1 | Of the payload bytes. Present once known; the basis of idempotent re-import. |
| `bytes` | int64 | 0..1 | Payload size. |

- **(5.14a) MUST** `completeness` and `transfer` are independent axes. A Capture may be `complete` + `pending` (captured fine, not yet sent) or `partial` + `present` (arrived intact, sensor dropped mid-swing).
- **(5.14d) MUST** `anchor` carries **exactly one** key (I27). `{ stream: true }` is permitted only on a Stream whose `continuity` is `continuous`, and `interval` is then mandatory **including when `completeness: absent`** — a segment with no interval says nothing about what it covers, and an `absent` segment *with* an interval is how a peer states that a named span was not recorded.
- **(5.14d1) SHOULD** *Erratum E12, 23 August 2026.* An **`absent`** Capture MAY carry `interval` whatever its anchor, and SHOULD where the peer knows the span it could not supply. The case that forced this is [8.4b](#84-orphan-capture-requests): a peer answering an orphan capture request whose interval has left the buffer returns a **shot-anchored** Capture of `completeness: absent` and `absent_reason: outside_buffer`, and as first written the field table forbade the one field that says *which* span was lost — so a hostless device could report that its buffer no longer reached back far enough without being able to say how far back it did reach. `absent_reason` names the cause; the interval names the extent, and a consumer wants both (finding F-D4-2, PinPointCapture, session S3).
- **(5.14e) MUST NOT** A stream-anchored Capture overlap another on the same Stream. Segments abut or leave a declared gap; they do not overlap, because two accounts of one interval are two answers to one question.
- **(5.14f) MUST** `transfer` is the **owner's** view of where a payload has got to. `pending` is held locally and unsent; `in_flight` and `present` are sent; **`confirmed` means the receiver has asserted that it holds the payload durably**, which only the receiver can say ([`PPCP-MSG` §8.4](ppcp-messages.md#84-capture_committed)).
- **(5.14g) MUST NOT** A peer evict a Capture **that holds payload the owner has not had confirmed**, whatever its retention policy (I38). A Capture is evictable when any of these holds:

  | | Exit | Why |
  |---|---|---|
  | 1 | `transfer` is `confirmed` | The receiver asserted it holds the payload durably |
  | 2 | `completeness` is `absent` | There is no payload to evict, and no digest for `capture_committed` to name |
  | 3 | The receiver answered `payload_abort` / `already_present` ([`PPCP-MSG` §8.3c](ppcp-messages.md#83-the-payload_-family)) | It demonstrably holds the payload durably; that answer is equivalent to a commit for this purpose |
  | 4 | **The protocol** permits the owner to shed it — [5.11j](#5112-preview-streams) for a preview segment, [5.12.1b](#5121-candidate-evidence) for candidate evidence, or payload withheld under a rule in this specification set that permits withholding | It was never going to be sent, so no receiver will ever confirm it. Candidate evidence matters most here: its count is not bounded by anything the user does ([§13c](#13-privacy-considerations)), so a rule forbidding its eviction would retain indefinitely the material the privacy section is about |

- **(5.14g1) MUST NOT** A peer's own retention policy extend that list. **Shot-anchored payload is never sheddable by policy** while it holds payload no receiver has confirmed. Every exit above is a case where *the protocol itself* says no receiver will ever confirm it; a policy exit would be the licence the requirement this invariant serves explicitly forbids — *nothing unconfirmed is evicted, **regardless of retention policy***. A peer under storage pressure refuses to arm ([§9](#9-offline-sessions-and-bundles)) rather than dropping swings a consumer has not received.
- **(5.14h) MUST** A receiver that durably commits a Capture obtained **from a bundle** sends `capture_committed` for it on its next connection with the owning peer. Identity is sufficient without anything new: I34 makes it `Capture.id` scoped by `Session.id` and the owning `Peer.id`, which is exactly what lets a consumer name a Capture from a session it received as a file.
- **(5.14h1) MUST** A `capture_committed` naming a Session whose `state` is `closed` is **accepted**, not answered `unknown_session`. It may arrive days after the bundle was imported, and releasing storage is the one operation that stays legitimate after a Session closes.
- **(5.14i)** In a session with **no receiver at all** — hostless, and nothing exported yet — no Capture can be `confirmed` and I38 therefore constrains nothing. Retention there is the peer's own policy, and a peer MUST NOT read I38 as protection it does not have in that case.

5.14f and 5.14g close a hole that made a stated obligation unsatisfiable. A capture peer is required to keep an independent store with per-shot sync state — *local, sent, confirmed* — and to evict nothing unconfirmed. But `payload_ack` acknowledges a **chunk arriving**, `payload_end` travels from sender to receiver, and until revision 7 nothing came back at all. The third state was unreachable, so "evict nothing unconfirmed" was satisfiable only by evicting nothing ever: safe, and unbounded across a season of sessions.

**5.14g's exits, and 5.14h and 5.14i, are revision 8's correction of revision 7.** As first written, I38 said *whatever its retention policy* and meant it: it forbade discarding a preview segment that [5.11j](#5112-preview-streams) **requires** a peer to discard — a MUST and a MUST NOT one section apart, both added in the last two revisions — and it forbade evicting an `absent` Capture, which has no payload and so can never be confirmed at all. It also caught the `already_present` path, where a receiver that demonstrably holds a payload answers with an abort rather than a commit; and candidate-audio windows, which [§5.12.1c](#5121-candidate-evidence) already contemplates being *evicted*.

The error was scope. I38 exists for one obligation — **shot payload a consumer has not received yet** — and was written as though it were about every Capture. And 5.14h matters more than it looks: without it `confirmed` was unreachable on the **offline path**, which is the path an entry-level capture device spends most of its life in, so the gap G2 closed for live sessions stayed open for the normal case.
- **(5.14b) MUST NOT** Gaps be interpolated across or implicitly spanned (I11).
- **(5.14c) MUST** `achieved_frames` carries the per-frame exposure durations on which [§6.1](#61-canonical-instant) depends, and reaches a consumer before it converts. The split between the two halves is [§5.8](#58-capability).

**Three anchors, because a realisation can belong to three things.** A Shot — the ordinary case, a clip extracted around `t0`. A Candidate — the audio window that explains why detection fired, which must survive for nominations that lost ([§5.12.1](#5121-candidate-evidence)). And the Stream itself — a segment of something recorded continuously, belonging to no event at all: attitude and gravity, sensor arrival evidence, a preview frame. The third was missing until revision 5, which made `continuity: continuous` a flag with nothing behind it.

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
| `basis` | `arrival_pairing` \| `shared_candidate` \| `interval_alignment` \| `acoustic_correlation` \| `sequence_alignment` \| `manual` | 1 | Open registry. See [§8.5](#85-reconciliation). |
| `foreign_system` | `Kind` | 0..1 | Reverse-DNS identifier of the system the foreign record came from, where it is not a PPCP peer. |
| `confidence` | float `0..1` | 1 | |
| `confirmed` | `bool` | 1 | |
| `confirmed_by` | `observer` \| `user` | `confirmed`: 1 | **Which kind of confirmation this is.** See 5.16e. |

- **(5.16a) MUST NOT** A conformant implementation provide any operation that merges or rewrites Shots on reconciliation (I9). Unconfirmed links are visible and reversible; nothing is rewritten. There is no merge operation in the model to invoke by accident.
- **(5.16b) MUST** A ShotLink whose `basis` is **retrospective** — `interval_alignment`, `acoustic_correlation`, `sequence_alignment` — is presented for confirmation before `confirmed: true` is set. Sequence alignment over ~50 ordered shots with inter-shot intervals is a well-determined problem, but the confirmation requirement is about the cost of being wrong, not the difficulty of being right.
- **(5.16c)** `basis: arrival_pairing` is **not** retrospective: it records an association a peer made live, at capture time, from the order in which a record arrived. It carries `confidence` like any other link and MAY be set `confirmed` by the peer that observed the arrival, because there is no later moment at which the evidence would be better. See [§8.5](#85-reconciliation).
- **(5.16d) MUST NOT** A ShotLink of any basis influence `t0`, or be converted into a `TimebaseRelation`. It associates; it does not time.
- **(5.16e) MUST** Where `confirmed` is true, `confirmed_by` states which kind of confirmation it was: `observer` is a live assertion by the peer that **observed the association**; `user` is a human decision. A consumer MUST NOT treat them as equivalent.
- **(5.16f) MUST** A retrospective basis — `interval_alignment`, `acoustic_correlation`, `sequence_alignment` — may only be `confirmed_by: user`.
- **(5.16g) MUST** `basis: shared_candidate` links two Shots that reference the same Candidate. It is exact by construction, arises from [§8.2l](#82-arbitration), and is `confirmed_by: observer` — the observed association is a collision rather than an arrival, which is why 5.16e is worded to cover both.

`confirmed_by` exists because a single boolean had come to carry two different epistemic states: before `arrival_pairing`, `confirmed` meant *a human agreed*; after it, it also meant *a machine asserted this live and no human will ever be asked*. That is exactly the conflation this specification refuses everywhere else — `claimed`/`measured`/`achieved`, `cold_sample`/`sustained`, `assumed`/`vendor`/`measured` — and one boolean was doing what three enumerations do elsewhere. The mis-pair it guards against is real: a host that arms a slot on detection and lets the next arriving record claim it will pair to the wrong swing if a record arrives after a second swing has displaced the slot.

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

### 5.18 Annotation

A **user artefact**: something a person drew, wrote or marked, or a coarse navigation target a peer derived for scrubbing. It is not an observation, and the distinction is the point of the type existing.

| Field | Type | Card. | Notes |
|---|---|---|---|
| `id` | `Id` | 1 | Minted by the authoring peer, unique within the Session. |
| `session_id` | `Id` | 1 | |
| `shot_id` | `Id` | 1 | The Shot this annotation is about. |
| `stream_id` | `Id` | 0..1 | The Stream whose frame it is drawn on. Presence is determined by `kind` — see 5.18j. |
| `at` | `Instant` | 1 | The frame instant it anchors to. Timebase per 5.18g. |
| `author_peer_id` | `Id` | 1 | Who authored it. |
| `provenance` | `user` \| `device_advisory` | 1 | See 5.18b. |
| `kind` | `Kind` | 1 | Open registry — `line`, `plane`, `text`, `nav_anchor`, … |
| `format` | `Kind` | 1 | How to interpret `body`. |
| `body` | bytes | 1 | Opaque to the protocol, **at most 8 KiB** (5.18f). |
| `created_at` | `Instant` | 1 | |
| `revision` | uint | 1 | Increments on edit. A higher revision for the same `id` supersedes. |
| `deleted` | `bool` | 0..1 | A revision may retract rather than replace. |

- **(5.18a) MUST** An Annotation is carried **losslessly**: `body` is opaque, and a peer that does not understand its `format` stores and returns it unchanged rather than dropping or rewriting it. Round-tripping is the requirement; interpreting it is not.
- **(5.18b) MUST** `provenance` distinguishes a **user** artefact from a **device-advisory** one — a coarse scrub target a peer derived, such as an impact marker or a top-of-backswing anchor.
- **(5.18c) MUST NOT** An Annotation of any provenance contribute to a Shot, a Candidate, a calibration, or any computed quantity (I37). It is never derived data in the sense the rest of this model means, and `kind: nav_anchor` in particular is **never** persisted or interpreted as phase data.
- **(5.18d) MUST** Annotations flow in **either direction**. This is the only content in PPCP that does: every payload elsewhere describes a Capture the sender owns, and an annotation authored on one peer must reach the other.
- **(5.18e) MUST** Supersession is by `id`, then `revision`, then `author_peer_id`. A peer holding a revision and receiving a **higher** one replaces; receiving a **lower** one ignores it; receiving an **equal** one replaces **if and only if** the incoming `author_peer_id` sorts higher bytewise. The comparison is total and identical at both ends, so two peers editing concurrently converge on the same annotation without merging and without either needing to know who acted first (I9).
- **(5.18g) MUST** Where `stream_id` is present, `at` is expressed in **that Stream's timebase** and names a frame that Stream contains. Where it is absent the annotation is not view-specific — a text note, a `nav_anchor` — and `at` is in `Session.timebase_ref`.
- **(5.18h) MUST NOT** A consumer render a view-specific annotation on any Stream other than the one it names.
- **(5.18j) MUST** The `kind` registry marks each value **view-specific** or not. `line` and `plane` are view-specific; `text` and `nav_anchor` are not. **An annotation of a view-specific kind carries `stream_id`; one of a non-view-specific kind does not.** A consumer that does not recognise a `kind` treats it as view-specific **if `stream_id` is present** — the conservative default, since it then renders it only on the Stream named, or not at all.

5.18j replaces a presence rule that could not be checked. It first read *"present for any annotation whose `body` is interpreted in image coordinates"* — but `body` is opaque and `format` is an open registry, so no peer and no test could determine whether a given annotation was view-specific, and 5.18h had nothing to bind to. That is exactly the pattern [§11.1](#111-the-rule-for-writing-an-invariant) names: a rule constraining a **judgement** rather than the **shape** of an output. Deriving presence from `kind`, which is on the wire, makes it statically checkable and gives an unrecognised vendor kind a defined and conservative behaviour.
- **(5.18f) MUST NOT** `body` exceed **8 KiB**. A finger-drawn plane is a few hundred bytes and a text note less; anything approaching the cap is a different feature, and `annotation` travels on the **control** channel, where I30 already keeps far smaller things off.
- **(5.18i) SHOULD** A peer **coalesces** rapid revisions and sends the latest rather than every intermediate. Dragging a line produces a continuous stream of edits, and each revision resends the whole `body`; the channel it lands on is the one carrying shot events.

**Why an annotation names a Stream.** A Shot in a studio has Captures from a face-on camera, a down-the-line camera and a phone behind the golfer. An alignment or swing-plane line is a set of **image coordinates**, and coordinates mean something on the image they were drawn on and nowhere else: rendered on another view they are not merely wrong but *plausibly* wrong, which is worse. Naming the Stream also makes the anchor exact — `at` is then in that Stream's own timebase and matches a frame it actually contains, instead of converting through a relation whose sigma can land a line on the neighbouring frame.

**Why equal revisions tiebreak on the author.** Revision 7 claimed two peers editing concurrently converge on the higher revision. They do not: both hold revision 1, both produce revision 2, each receives an equal revision and ignores it, and the two ends diverge permanently and silently while each believes it converged. A coach at a host and a golfer at a device drawing on one shot is the case markup exists for, not a race. `author_peer_id` is already mandatory, so a total order costs a comparison and no field — and it is a tiebreak, not a merge, which I9 forbids.

**Why this is a distinct type and not a Stream.** Every payload elsewhere in PPCP is a `Capture`, and a Capture realises a Shot, a Candidate or an interval of a Stream — all of them observations produced by a `Source`, which has a clock, a calibration and an owning peer. **A person has none of those.** Modelling markup as a `Source` of some virtual kind would put a human being in the position the model reserves for instruments, and blur the one distinction it is most careful about: that Sources observe and everything else interprets.

The cost of a separate type is one entity and one message. The cost of the alternative is the model's spine.

---

## 6. Timing

### 6.1 Canonical instant

**The canonical instant of a frame is mid-exposure.** This is the single most likely site of silent non-conformance in the whole protocol, because the conversion spans two entities and two implementers can each apply part of it and both believe themselves compliant.

The reason it matters does not depend on any particular consumer existing. The error a missed conversion produces is **exposure-dependent**, so it varies with the light in the room and cannot be calibrated out as a constant. A consumer that models clock error will absorb it and attribute it to the clock; a consumer that does not will carry it as unexplained systematic error in fused output. Neither can detect it from the data, because it has the shape of the thing they are already trying to estimate.

Given a sample timestamped `t` on a Stream whose profile declares `timing.convention`, and an exposure duration `d` taken from that frame's entry in `Capture.achieved_frames.exposure_ns` ([§5.8](#58-capability)):

| `convention` | Canonical instant |
|---|---|
| `mid` | `t` |
| `start` | `t + d/2` |
| `end` | `t − d/2` |
| `nominal_frame_start` | `t + frame_start_to_exposure_offset_ns + d/2` |

- **(6.1a) MUST** A consumer converts to the canonical instant before comparing timestamps across Sources, in either direction, and before computing any quantity that mixes them.
- **(6.1b) MUST** For `nominal_frame_start` the conversion uses all three of `convention`, `frame_start_to_exposure_offset_ns` and the per-frame `d`. None of the three alone, and no two of the three, are sufficient (I17).
- **(6.1c) MUST** `d` is taken from `AchievedFrames.exposure_ns` — per frame, or from its scalar form where the peer declared the value constant under a lock ([§5.8](#58-capability)) — and never from the profile's exposure range. Exposure varies frame to frame under any automatic mode. Locking exposure is what makes the correction stable in practice, but the protocol MUST NOT assume the lock held.
- **(6.1e)** A consumer converting from `exposure_provenance: sampled` is using a device property sampled per frame rather than a value attached to the frame. That is conformant and honestly declared; whether it is accurate enough is the consumer's policy, not the protocol's (I14).
- **(6.1d)** Where a profile's `format` is absent (a non-framed source such as an IMU), the canonical instant is `t` and `convention` MUST be `mid`.
- **(6.1f) MUST** *Erratum E10, 23 August 2026.* `d/2` for an **odd** `d` is `d` divided by two **truncated toward zero**, and the inverse conversion applies the same truncated half rather than recomputing it — so a round trip is bit-exact for an odd exposure as well as an even one, which is what [`PPCP-CONF` CT-S1](ppcp-conformance.md#41-ct-s1--the-canonical-instant-conversion) assertion 4 asks for. Every worked example of [§6.1.1](#611-worked-examples) has an even `d`, so the choice is not observable there and two implementations could differ by a nanosecond without either example failing (finding by `libppcp`, session S1).

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

- **(6.2e) MUST** *Erratum E10, 23 August 2026.* The division in 6.2d is exact rational arithmetic rounded to the nearest nanosecond, **halves away from zero**. `readout_ns × r` is computed before the division, never after, so no intermediate is truncated; the rule is stated because two conformant implementations rounding differently disagree by one nanosecond on most rows of most frames, and a conformance run comparing row instants to the nanosecond ([`PPCP-CONF` CT-S1](ppcp-conformance.md#41-ct-s1--the-canonical-instant-conversion) assertion 5) cannot be written against an unspecified rounding (finding by `libppcp`, session S1).

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
| 1, for a Candidate the host did not use | the host's timebase | `device` | none — minted under [§8.2i](#82-arbitration), linked by `ShotLink` |

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
- **(7.3b) MUST** With no arbitrating host, the capturing peer controls its own arming, and a hostless bundle contains **no `arm` frame and no `disarm` frame**: those are control messages, conferred by **Live**, and with nobody controlling there is no command to keep. The bundle carries the *effect* — Streams, `readiness`, Captures — not a command nobody sent. Everything else in the bundle is identical to the live path.
- **(7.3c) MUST** A capture peer reports `Readiness` when it is armed, where a command to arm was sent at all, and in any case whenever `settled` changes. `readiness` is conferred by **Capture**, so a hostless peer records it without declaring Live.
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
- **(8.1b) MUST** A shot record **with no peer, no timebase and no clock relation is not a Candidate**. It is associated with a Shot through `ShotLink` ([§8.5](#85-reconciliation)) and never enters arbitration.
- **(8.1c) MUST** A live external nominator — a launch monitor connected as a peer, or a Source owned by a peer — is modelled as a Source with `kind: launch_monitor`, with its own clock and calibration, and nominates normally.

An external shot record can arrive in **three shapes**, and they are deliberately not unified. Conflating them is how an implementer ends up fabricating a timebase for a value that has none — the exact failure [§5.4b](#54-timebaserelation) exists to prevent.

| | **Live nomination** | **Live association** | **Retrospective record** |
|---|---|---|---|
| Owning peer | yes | yes — the peer that observed its arrival | no |
| Clock relation | yes | **no** | **no** |
| Attributable by | its own instant | **arrival order** | matching over a set |
| Enters arbitration | yes | no | no |
| Represented as | `Candidate` | `ShotLink`, `basis: arrival_pairing` | `ShotLink`, `basis: sequence_alignment` etc. |
| Confirmation | n/a | asserted live by the observer | **required** |
| Example | a launch monitor connected as a peer | a launch monitor writing one row per shot to a watched file | a session export from another system |

The middle column is the one that exists today and that Draft 1 could not express. The launch monitor this project integrates with writes a **two-line file — one header row and one shot — rewritten in place** after each shot. It carries no timestamp that can be trusted and a shot counter unrelated to anything of ours. It is attributed by arrival: the host arms a slot when it detects a swing, and the next reading to arrive claims that slot. That is a live association made by a peer, with no clock anywhere in it.

- **(8.1e) MUST NOT** A peer synthesise a Timebase, a `TimebaseRelation`, or an `Instant` for a record that has none, in order to route it through nomination.
- **(8.1f)** A single-record, live-rewritten source produces **nothing to reconcile later**: the row is overwritten by the next shot, so if no peer observed its arrival, that reading is unrecoverable. A hostless session therefore has no launch monitor data to match against afterwards, and a product flow must not promise otherwise.

`source_id` stays mandatory on Candidate. An optional `source_id` would strand a Candidate with no calibration to apply — and for an acoustic candidate, calibration is where the surveyed geometry lives.

- **(8.1d) MUST** An acoustic nominator corrects for acoustic time of flight before emitting `at`, and SHOULD report the correction and its uncertainty in `tof_correction` ([§5.12d](#512-candidate)).

At 343 m/s the correction is ~2.9 ms per metre; a device 2 m from the ball lags 5.8 ms, which is most of a frame at 150 fps. Host and device microphone distances differ, which is why two microphones are two Sources with two calibrations rather than one shared constant.

### 8.2 Arbitration

Available only to a peer with `role: host` (I20).

- **(8.2a) MUST** The host converts every Candidate into `Session.timebase_ref` using the current `TimebaseRelation` set, before comparing them. **The canonical-instant conversion of [§6.1](#61-canonical-instant) has already been applied by the nominator** ([§5.12e](#512-candidate)); a host that applies it again doubles the correction (I33).
- **(8.2b) MUST** Two Candidates are treated as nominating the same Shot if their converted instants fall within `Session.coincidence_window_ns`.
- **(8.2b1) MUST** *Erratum E14, 23 August 2026.* **`t0` is the converted instant of the contributing Candidate with the smallest combined timing uncertainty** — the sigma of that Candidate's relation to `Session.timebase_ref` at that instant, **plus** `tof_correction.sigma_ns` where present — with ties broken by the **earliest** converted instant, so the choice is independent of arrival order. `Candidate.confidence` is **not** consulted: it is a belief that the event happened, not a statement about when, and using it would put a quality judgement in the protocol layer (I14). The two sigmas are added rather than combined in quadrature — the comparison only has to be deterministic and identical at both ends, and the conservative sum is one line an implementer cannot get subtly wrong.

8.2b grouped the Candidates and 8.2h said when to issue, but nothing said **which one sets `t0`** — so two conformant hosts, given the same Candidates, could issue different `t0` for one event, and I7 then froze whichever each had chosen. That is an interoperability failure the suite could not detect, because each host agrees with itself. The rule is 8.2h's own rationale read as an obligation: a sample-accurate acoustic nomination should win over a fast IMU one, and waiting for it is the entire reason the issue hold exists. A host that believes it can do better re-derives offline under 8.2e, which produces a new analysis rather than a mutation (finding by `libppcp`, session S3).

- **(8.2c) MUST** `coincidence_window_ns` is a declared Session parameter, not a constant. Acoustic time-of-flight spread sets its floor and that is rig-dependent. The default is 50 ms.
- **(8.2d) MUST** A Candidate whose relation to `timebase_ref` is missing, `unrelated`, or too uncertain under host policy is **excluded from arbitration and retained** (I8). Exclusion is a conclusion; the Candidate remains evidence.
- **(8.2d1) MUST** *Erratum E29, 23 August 2026.* A Candidate excluded under 8.2d **for want of a relation** — missing, or `unrelated` — is **reconsidered when a relation to `Session.timebase_ref` becomes available**, and enters arbitration then: it joins its group if no Shot has been issued for it, and attaches under [8.2e](#82-arbitration) with `t0` unrevised if one has. A host MUST NOT leave it retained on the strength of having once excluded it.

  8.2d said exclusion is a conclusion and the Candidate remains evidence, and said nothing about the relation arriving afterwards — which on a live link it always does, because [§6.3](#63-clock-synchronisation)'s synchronisation burst is still converging while the first swings are being taken. Under the "need not" reading a peer that nominates early is **silently unarbitrated for the whole Session**, and the failure is invisible from either end: no error, no Shot, and every Candidate present and retained exactly as 8.2d requires. A Candidate whose relation never arrives stays retained, which is unchanged. 8.2h's bound on *issuing* is unchanged too: reconsideration feeds arbitration, it does not extend the window in which a host may issue (finding F-S5-1, session S5).

- **(8.2e) MUST** A Candidate arriving after the Shot has been issued attaches to that Shot. `t0` is **not** revised (I7). The host MAY re-derive t₀ offline from the retained candidate set; that produces a new analysis, not a mutation of the Shot.
- **(8.2f) MUST** The issued Shot retains references to **every** contributing and excluded Candidate.
- **(8.2g) MUST** The host declares `issue_hold_ns` — the interval it collects Candidates for, opened by the **earliest** Candidate contributing to a Shot — in `session_open`.
- **(8.2h) MUST** A host issues a Shot **no earlier than** `issue_hold_ns` after the earliest contributing Candidate, and **no later than** the mint deadline of 8.2i. Issuing early locks `t0` to whichever modality happened to be fastest, which is not the same as whichever is most accurate, and I7 forbids correcting it afterwards. Issuing after the deadline overlaps the window in which a nominating peer is entitled to mint, and produces two Shots for one event with no defect on either side.
- **(8.2i) MUST** A nominating peer MUST NOT mint locally for a Candidate it has sent to a host until `issue_hold_ns` has elapsed since that Candidate's instant, plus a margin of at least one `heartbeat_interval_ms` to cover the link. After that, with no `shot` referencing it, the peer MAY mint — **but only for a Candidate its own promotion policy would have promoted in a hostless session ([§8.3b](#83-the-zero-host-regime)). Host silence does not promote a Candidate the peer did not believe** (I32). The resulting Shot carries `authority: device` and is reconciled to the host's Shots through `ShotLink`, as [§8.3f](#83-the-zero-host-regime) requires of a Shot minted during a link outage.
- **(8.2i1) MUST NOT** A peer mint at all if it **cannot express `t0` in `Session.timebase_ref`** — because it holds no `affine` relation to that timebase, its relation is `unrelated`, or the relation exceeds its own policy. The Candidate is retained with no Shot referencing it, which is already a legal and honest state (I8). A peer MUST NOT substitute a zero offset to make a Shot expressible ([§5.4b](#54-timebaserelation)).
- **(8.2j) MUST** A minting peer sends `shot` immediately on minting, so its counterpart learns of it without waiting for a payload.
- **(8.2k) MUST** A host that receives a device-minted `shot` referencing a Candidate it is still holding **MUST NOT issue a competing Shot for that Candidate**. It attaches its own Candidates to the device's Shot by re-sending `shot` with an extended `candidates` list and the **unchanged** `t0`, exactly as 8.2e requires of a late Candidate (I35).
- **(8.2l) MUST** Where both peers nevertheless issue — because the two messages crossed — neither Shot is withdrawn (I7, I9). The host links them with `ShotLink`, `basis: shared_candidate`, and a consumer MUST NOT count them as two events. Two Shots referencing one Candidate is the detectable signature of this case.

**A tolerance and a deadline are different quantities and Draft 1 had only one field for both.** `coincidence_window_ns` answers *"are these two nominations the same event?"*; `issue_hold_ns` answers *"how long do I wait before deciding?"*. A host may reasonably run a 40 ms tolerance inside a 200 ms hold — a fast IMU nomination followed 30 ms later by a sample-accurate acoustic one should resolve to the acoustic instant, which requires waiting for it. Collapsing the two into one number forces a host to choose between a tolerance that is too wide and a hold that is too short, and neither default can be measured because the measurement would not know which quantity it was estimating.

8.2i closes the other half: without a declared hold, a peer that nominated to a live, healthy host had no deadline after which to conclude no Shot was coming, so two conformant implementations could disagree about whether a Shot exists at all.

**Why the deadline alone was not enough, and what 8.2i–8.2l add.** Draft 2 gave the nominating peer a deadline and stopped there, which introduced two faults that both review teams found independently.

The first is that **nothing obliges a host to answer a Candidate it declines.** A host that correctly rejects a dropped club, a club-on-mat or a shot in the next bay issues no `shot` at all, so the branch that fires on the device is the silent one — and after the deadline it minted a Shot for *every* candidate the host had declined, including ones its own detector never believed. That is the defect [§8.3](#83-the-zero-host-regime) had just removed from the hostless regime, reappearing in the live one through the clause that fixed its sibling. The promotion condition in 8.2i is the fix: host silence does not promote a Candidate the peer did not promote itself.

The second is that a deadline **narrows a race rather than closing it**. A host that issues at the deadline, over a link that then stalls, produces a `shot` arriving after the device has already minted — two Shots, both immutable under I7, both unmergeable under I9, and there is deliberately no `withdraw` or `supersede` message to reach for. 8.2h bounds the host's window so the two do not overlap by design; 8.2j and 8.2k make the residual case resolve in one direction rather than two, and the direction is forced: the device's Shot may already anchor an extracted Capture, so the device's Shot is the one that exists and the host attaches to it.

The cost of 8.2k is that in this rare case `t0` is the device's rather than the host's arbitrated value, which is the worse estimate. That is accepted. It only arises when a host has already exceeded the bound of 8.2h, and one slightly worse `t0` is a much smaller harm than two Shots for one swing.

**8.2i1 closes a hole that the conformance suite reaches by design.** The commonest reason a host stays silent is 8.2d: it excluded the Candidate because the nominating peer's relation to `timebase_ref` was missing, `unrelated`, or too uncertain. But that is exactly the condition under which the peer cannot convert its own instant into `timebase_ref` either — so 8.2i told it to mint a Shot whose `t0` ([§5.13c](#513-shot)) it had no conformant way to express, and [§5.4b](#54-timebaserelation) rightly forbids the obvious shortcut. The required interoperability pairing *"host ↔ peer declaring `unrelated` timebases"* ([`PPCP-CONF` §5](ppcp-conformance.md#5-interoperability)) puts **every** candidate from that peer in this state. The pairing written to prove an honest degraded peer is handled honestly landed on an undefined one. Retaining the Candidate with no Shot is the honest answer, and the model already had the state.

### 8.3 The zero-host regime

- **(8.3a) MUST** With **no arbitrating host** — a *hostless session*, or a *host-unreachable interval* ([§8.3g](#83-the-zero-host-regime)) — **no coincidence window is applied**, and a Shot is **issued** carrying exactly one Candidate, with `authority: device` (I23).
- **(8.3b) MUST** A Mint peer **promotes** a subset of its own Candidates to Shots. Every Candidate is emitted and retained with its evidence whether or not it was promoted (I8).
- **(8.3c) MUST NOT** Promotion policy appear in this specification. Which transients a detector believes are shots is detector tuning, exactly as an emission threshold is (I14).
- **(8.3d) MUST** A peer that issues Shots implements the **Mint** profile ([§2.2](#22-conformance-profiles)).
- **(8.3e) MUST** Shot ids minted by a peer are unique within the Session and SHOULD be UUIDs. A peer MUST NOT mint an id in another peer's namespace.
- **(8.3f) MUST** A peer whose host link drops mid-session enters this regime for the duration, mints Shots locally, queues Captures as `transfer: pending`, and reconciles the minted Shots on reconnect through `ShotLink`.
- **(8.3g) MUST** **A Session with an unreachable host is not a Session with no host.** `Session.peers`, `Session.timebase_ref`, `Session.coincidence_window_ns` and `Session.issue_hold_ns` are unchanged (I16, 5.10e); what changes is that no arbitration occurs and the peer mints under 8.3a–c. For the purposes of I23, a host unreachable for three consecutive heartbeat intervals ([§7.4c](#74-liveness)) is treated as absent.
- **(8.3h) MUST** I23 constrains a Shot **at issuance**. A Shot minted with no arbitrating host MAY later gain Candidates by the ordinary attachment route ([§8.2e](#82-arbitration), [§8.2k](#82-arbitration)) once a host is reachable, and that is not a violation.

This is a **different regime**, not a special case of single-nominator arbitration. Applying a coincidence window here would collapse distinct candidates and produce subtly different output from the same acoustic evidence — which is precisely why it is a separately-testable invariant.

**Promotion is what Draft 1 was missing.** Draft 1 required *every* Candidate to become a Shot, which forced a wrong answer on a correct device: the detector is required to discriminate ball-into-screen — roughly 9 ms after impact at 3 m — from the impact itself, and the diagnostic design positively encourages emitting both so a rejected nomination keeps its audio. Under the old rule that swing minted two Shots, and the conformance suite certified it. The device's only escape was to suppress the second candidate, destroying the evidence that candidate-attached retention exists to preserve.

Breaking the identity keeps everything the regime was protecting — no window, no cross-peer arbitration, one nominator per Shot, `authority: device` — and moves the one genuinely device-internal decision back where it belongs.

**The regime has two entry conditions and they are not the same thing.** A *hostless session* has no host in its roster at all — the entry-level capture case, and the one I23 was written for. A *host-unreachable interval* is a session that still has a host, and still has that host's `timebase_ref` (immutable under I16), which has merely stopped answering. 8.3g says so explicitly because the two were previously described in the same words, and 8.3h says what happens afterwards: a Shot minted during an outage is not frozen at one Candidate for ever, and a peer that read I23 as permanent could have refused the host's attachment on reconnect. That combination never actually fires — 8.2k needs a *shared* Candidate, which a host cannot have received while the link was down — but two clauses using one set of words for two conditions is the shape of thing [§11.1](#111-the-rule-for-writing-an-invariant) exists to catch.

### 8.4 Orphan capture requests

- **(8.4a) MUST** A capture peer serves a capture request for a `t0` it never nominated, converting `t0` into its own timebase and locating the interval in its buffer.
- **(8.4b) MUST** Where the interval is no longer retained, the peer responds with a Capture of `completeness: absent` and `absent_reason: outside_buffer`. Absence is asserted, never inferred from a missing payload (I10).

This is why `Shot.candidates` is non-empty **per Session** and not per peer: a Shot may have zero candidates from a peer and a Capture from that same peer.

### 8.5 Reconciliation

- **(8.5a) MUST** Reconciliation creates **links**. No entity is rewritten or merged (I9).
- **(8.5b) MUST NOT** An implementation auto-merge. Candidate matches are surfaced and confirmation is required.
- **(8.5c) MUST** Re-import of a session already held is a no-op, never a duplicate. Session identity is `Session.id` plus the minting `Peer.id`. **Capture identity is `Capture.id`, scoped by those two** (I34); `Capture.digest` is a **content** check where present, not the identifier.

8.5c named the digest as the identifier until Draft 3, which left two ordinary cases with no identity at all: a Capture of `completeness: absent` has no payload and therefore no hash, and a `complete` + `pending` Capture may reach a bundle before its digest is computed — a case 8.1e of [`PPCP-MSG`](ppcp-messages.md#81-capture_announce) deliberately permits so the announce need not wait for the clip. Absent captures are the most important content of a partial session, and identifying them by a hash they cannot have would have duplicated them on exactly the second import the rule exists to make safe.
- **(8.5d) MUST** A host that re-solves a clock mapping on import declares a **new relation from `Session.timebase_ref`** and leaves `timebase_ref` untouched (I16).
- **(8.5e)** Cross-session alignment, where implemented, is a `SessionLink` ([§5.17](#517-sessionlink)) and mutates neither Session (I25).
- **(8.5f) MUST** A record associated by **arrival order** rather than by any instant uses `basis: arrival_pairing`, is asserted by the peer that observed the arrival, and carries `confidence`. It does not require later confirmation, because there is no later moment at which the evidence improves — but it MUST NOT influence `t0` and MUST NOT be turned into a `TimebaseRelation` ([§5.16d](#516-shotlink)).
- **(8.5g) MUST** `basis: sequence_alignment` presumes a **multi-record export** with recoverable ordering and inter-shot intervals. It MUST NOT be used for a single-record source.

The consumer may already hold partial data for the same session — a launch monitor record, or an online portion captured before the link failed. A silent mis-merge corrupts the session record in a way that is hard to notice and harder to undo. Sequence alignment over ~50 ordered shots is tractable; the confirmation requirement is about the cost of being wrong.

8.5f and 8.5g were added after a review established that the reconciliation path Draft 1 hardened does not fit the launch monitor that actually exists. The two-path split was right; there were three paths.

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
- **(10.1d) MUST** Unknown fields, unknown `kind` values **in the open registries of [§10.3](#103-registries)**, unknown `basis` values and unknown profile fields are **ignored, never fatal**, on both ends (I13). An unrecognised value in a **closed** vocabulary is `malformed` ([§10.3](#103-registries), erratum E11).
- **(10.1e) MUST** A **responder** states its **support window** — the oldest wire version it accepts — in `hello_accept.min_version`. The window is **at least two MINOR versions back or twelve months, whichever is longer**; a peer may support more. An initiator's window is the `versions` list it sends in `hello`.
- **(10.1f) MUST** An `error` / `unsupported_version` carries the sender's full supported range in `detail`, so the receiving peer can tell its user **which end is stale**. Without it the code is fatal and uninformative, and the user is told only that something failed.
- **(10.1g) SHOULD** A peer facing a version it cannot speak reports the two ranges to its user rather than a generic failure.

The window is expressed in released versions with a time floor rather than in elapsed time alone, because the two ends have asymmetric release control: a host distributed as source moves at project pace, while an application distributed through an app store is gated by review and by users who do not update. **Old-application/new-host is the permanent normal case, not an edge case.** The time floor protects a release that could not ship; the version count protects a host from supporting a dialect indefinitely.

### 10.2 Extensions

- **(10.2a) MUST** An extension is identified by a reverse-DNS string and declared in `Peer.protocol.extensions`.
- **(10.2b) MUST NOT** An extension change the meaning of any field defined in this specification.
- **(10.2c) MUST** A peer that does not implement a declared extension ignores its messages and fields.

### 10.3 Registries

`Source.kind`, `Stream.kind`, `Candidate.basis`, `Calibration.kind`, `ContextChange.kind`, `ShotLink.basis`, `ClockDiscontinuity.cause` and `Capture.absent_reason` are **open registries**.

**Every other enumerated vocabulary in this specification is closed** (erratum E11): `Peer.role`, `Timebase.kind` ([5.3c](#53-timebase)), `Stream.continuity`, `Capture.completeness`, `Capture.transfer`, `Shot.authority`, `TimebaseRelation.kind`, `timing.convention`, `provenance`, `exposure_provenance`, `ThermalLevel` and `MeasuredCapability.method`. [10.1d](#101-version-negotiation)'s tolerance of an unknown `kind` value is scoped to the list above; an unrecognised value in a closed vocabulary is `malformed`, because ignoring it would mean silently choosing one of the values it is not.

- **(10.3a) MUST** Unknown values **in the open registries above** are ignored, never fatal (I13).
- **(10.3b) MUST** Vendor-defined values are namespaced with a reverse-DNS prefix — `com.example.forceplate` — so third parties may extend without coordination.
- **(10.3c) SHOULD** Unprefixed values are reserved for the published registry, maintained in the `libppcp` repository.
- **(10.3d) SHOULD** A vendor value that proves generally useful is proposed for the unprefixed registry rather than remaining vendor-scoped indefinitely.

Without 10.3b the first third party to add a sensor type either collides with a future core value or forks the protocol.

---

## 11. Invariants

**Thirty-eight invariants.** Each is a conformance test; [`PPCP-CONF`](ppcp-conformance.md) maps each to its required test. Identifiers are stable: I1–I21 keep the numbers used before the specification existed, I22–I28 were added in Draft 1, I29–I32 in Draft 2, I33–I35 in Draft 3, I36 in revision 5 and I37–I38 in revision 7. I6, I8, I17, I23, I30 and I32 have been amended in text without renumbering.

### 11.1 The rule for writing an invariant

*Normative for this document's own maintenance.*

- **(11.1a) MUST** An invariant constrains the **shape of an implementation's output**, never the **choice** an implementation makes. Where a rule names what an implementation should decide rather than what its result should look like, it is a threshold, and thresholds are host or detector policy (I14).

This rule is stated because the same defect has been found twice, both times by a reviewer and both times after the conformance suite had been written to certify it.

| Draft | The invariant | What it wrongly required |
|---|---|---|
| 1 | I23 — *every Candidate becomes exactly one Shot* | That a device which correctly detected a ball-into-screen transient mint a second Shot for the same swing |
| 2 | I32 — *after the deadline the peer MAY mint* | That a device mint a Shot for every Candidate a host had declined, including ones its own detector never believed |

Both read as constraints and were in fact instructions to decide. The corrected forms — *no window is applied and each Shot carries exactly one Candidate*, and *mint only what the peer's own promotion policy would have promoted* — constrain the result and leave the judgement where I14 already puts every other threshold. **A new MUST should be read against 11.1a before its test is written**, because a test written from a choice-shaped invariant certifies the defect rather than catching it.

| # | Invariant | Profile |
|---|---|---|
| **I1** | Every timestamp carries a `timebase_id`. There is no default timebase. | Core |
| **I2** | No sample's time is derivable from its index. Sequence numbers are for loss detection only. | Core |
| **I3** | Every `TimebaseRelation` is `affine` or `unrelated`; `affine` without both sigma fields is malformed. | Core |
| **I4** | Two Sources on the same clock share a timebase id. Identity is never asserted by relation. | Core |
| **I5** | A Stream's source, profile, timebase and calibration are fixed for **the stream's** lifetime. A change closes the Stream and opens another within the same Session. | Capture |
| **I6** | Every Shot references ≥1 Candidate somewhere in the Session; a Shot may have 0 candidates from any given peer. *(Reassigned from Detect: a Detect-only peer never issues a Shot, so I6 could not be tested against it. It binds both profiles that do.)* | **Mint, Arbitrate** |
| **I7** | `t0` is never revised after the Shot is issued. | Mint, Arbitrate |
| **I8** | Candidates are never discarded — losers, excluded, and unpromoted — and neither is the **record** of their evidence. The evidence payload may be shed under the peer's retention policy, with its absence asserted. A Candidate excluded for want of a relation is **reconsidered when the relation arrives** ([8.2d1](#82-arbitration), erratum E29): retained is not the same as finished with. *(Amended twice: extended to unpromoted candidates and to Mint; then, in revision 8, separated from the payload, because the model contemplates an evicted window and I8 read as forbidding one.)* | Mint, Arbitrate |
| **I9** | Reconciliation creates links; no entity is rewritten or merged. *(Reassigned from Offline to Core: `ShotLink` is originated live by Mint and Arbitrate peers, and this is a Core-shaped prohibition on anyone who originates a link.)* | Core |
| **I10** | `completeness` is asserted, never inferred from arrival. | Capture |
| **I11** | Gaps are explicit, never spanned, and meaningful only on `continuous` streams. | Capture |
| **I12** | A Session is valid with any subset of streams, including video-only. | Capture |
| **I13** | Unknown fields, `kind` values, `basis` values and profile fields are ignored, never fatal. *(Scoped by erratum E11: the `kind` and `basis` values this is about are the **open registries** of [§10.3](#103-registries). An unrecognised value in a **closed** vocabulary — `Timebase.kind`, `Peer.role`, `Capture.completeness` and the rest — is `malformed`, because ignoring it means silently choosing one of the values it is not.)* | Core |
| **I14** | No frame-rate, resolution, quality or confidence threshold appears in the model. | Core |
| **I15** | Wall-clock values are never used to compute an interval. | Offline |
| **I16** | `Session.timebase_ref` is immutable. An improved estimate is a new relation, never a rewrite. *(Erratum E28: a `session_open` naming a **different** `session_id` opens a second Session and changes nothing about the first — an imported Session's `timebase_ref` is its own.)* | Offline |
| **I17** | Converting a sample to its canonical instant requires the profile's `timing.convention`, that frame's exposure duration from `achieved_frames`, **and — where `convention == nominal_frame_start` — `timing.frame_start_to_exposure_offset_ns`**. No subset is sufficient. *(Amended: model draft 4 named two inputs, so an implementation could satisfy it and still be wrong on the default mobile path.)* | Capture |
| **I18** | `TimebaseRelation` is never composed. A needed relation is measured and declared directly. | Core |
| **I19** | Every Source declares `timing`, `geometry` and `intrinsics` regardless of which peer owns it. No convention is implied by peer role, product or platform. | Core |
| **I20** | A Session has at most one peer with `role: host`. Arbitrate is available only to that peer. | Arbitrate |
| **I21** | The per-timebase sync obligation binds every multi-clock peer, hosts included. | Live |
| **I22** | `timing.frame_start_to_exposure_offset_ns` is present if and only if `convention == nominal_frame_start`, and is declared explicitly even when zero. | Capture |
| **I23** | With no arbitrating host — none in the roster, or one unreachable under [§7.4c](#74-liveness) — no coincidence window is applied and a Shot is **issued** carrying exactly one Candidate. A Shot may later gain Candidates by the ordinary attachment route without violating this. *(Amended twice: Draft 1 required every Candidate to become a Shot; Draft 3 scoped it to roster absence alone and read as though it froze a Shot's candidate list for ever.)* | Mint |
| **I24** | Profiles gate origination, not comprehension. Every conformant peer parses the complete type vocabulary; a peer originates only messages its declared profiles confer. | Core |
| **I25** | Cross-session alignment is a `SessionLink`. It mutates neither Session and is never composed with a `TimebaseRelation`. | Offline |
| **I26** | A Candidate references a Source owned by a Peer in the Session with a declared Timebase. A record without one is reconciled by `ShotLink`, never nominated. | Detect |
| **I27** | Every Capture anchors to exactly one of a Shot, a Candidate, or an interval of its own Stream. *(Amended in revision 5: the third form was missing, so a `continuous` Stream could carry nothing.)* | Capture |
| **I37** | An Annotation never contributes to a Shot, a Candidate, a calibration or any computed quantity, and `kind: nav_anchor` is never persisted or interpreted as phase data. | Markup |
| **I38** | A Capture **holding unconfirmed payload** is never evicted; `confirmed` is asserted by the receiver and by nobody else. An `absent` Capture, one the receiver answered `already_present`, and one the protocol permits the owner to shed are all evictable. *(Amended in revision 8: as first written it forbade discarding a preview segment 5.11j requires a peer to discard, and forbade evicting a Capture that could never be confirmed because it has no payload.)* | Capture |
| **I36** | On a `continuous` Stream in a Session asserted `complete`, the announced stream-anchored Captures — present and `absent` alike — and their declared gaps account for its whole open interval. Time unaccounted for **between** announced Captures is a defect, not a dropout, in any Session. *(Amended in revision 6: as first written it read an honestly truncated bundle, and an honestly shed preview, as implementation errors.)* | Capture |
| **I28** | `MeasuredCapability`, where present, declares `method` and `duration_ns`; its absence means not measured and MUST NOT be inferred or synthesised. | Capture |
| **I29** | `Candidate.tof_correction`, where present, carries both `value_ns` and `sigma_ns`. No applied estimate travels without its dispersion. | Detect |
| **I30** | `capture_announce` carries summary capability only. Per-frame series travel with the payload they describe, with one exception: `capture_update` MAY carry `AchievedFrames` for a Capture whose payload will not transfer, because the series would otherwise be lost with the payload. *(Amended: Draft 2 forbade the control channel outright, contradicting the exception `PPCP-MSG` already permitted.)* | Capture |
| **I31** | A timing quantity the peer cannot guarantee from the platform declares its provenance: `frame_start_to_exposure_offset_ns`, `rolling_shutter.readout_ns` and `AchievedFrames.exposure_ns`. A peer never claims a stronger provenance than it has. | Capture |
| **I32** | A nominating peer does not mint a Shot for a Candidate it sent to a host until `issue_hold_ns` plus one heartbeat interval has elapsed with no `shot`, **and then only for a Candidate its own promotion policy would have promoted hostless**. *(Amended: as written in Draft 2 this required a peer to mint for every Candidate a host had declined — see [§11.1](#111-the-rule-for-writing-an-invariant).)* | Mint |
| **I33** | `Candidate.at` is the canonical instant, converted by the nominating peer. A consumer never applies the canonical-instant conversion to a Candidate a second time. | Detect |
| **I34** | Capture identity is `Capture.id`, scoped by `Session.id` and the owning `Peer.id`. `Capture.digest` is a content check where present, never the identifier. | Offline |
| **I35** | A host that has received a device-minted Shot referencing a Candidate it holds attaches to that Shot rather than issuing a competing one. Where both were issued, they are linked, never withdrawn or merged. | Arbitrate |

---

## 12. Security considerations

PPCP itself defines **no security model**. Pairing, authentication, encryption, key derivation, replay resistance and the question of whether a peer may rejoin a session after reconnecting without re-pairing are all delegated to [`PPCP-RV`](ppcp-rv.md).

This is defensible only if the companion document exists and is agreed. **A first draft now exists and nothing in it has been agreed**, so the delegation points somewhere rather than nowhere, but it does not yet point at anything settled ([Annex B](#annex-b--open-issues)).

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

Step 5 before step 9 is the ordering most likely to be reversed by instinct. Resist it: the bundle path is the same protocol without the clock pressure, so bugs found there are cheaper. It is also the order that lets a host consume real captured sessions **before** it has done the concurrency and candidate-retention work the live path demands of it.

### A.2 Where conformance will silently fail

Seven places an implementation will appear to work while being wrong. Each needs an explicit test, because normal use will not surface it. [`PPCP-CONF` §4](ppcp-conformance.md#4-the-silent-failure-tests) specifies them.

| Site | Why it survives normal use |
|---|---|
| **The canonical-instant conversion (I17, I22)** | Spans two entities and now three inputs, so two implementers can each apply part of the correction and both believe themselves compliant. The error is exposure-dependent and looks exactly like clock bias. |
| **`nominal_frame_start`'s offset (I22)** | It is the default path on the entire mobile side, and a missing offset is a small constant error that a bias estimator will absorb and mis-attribute. |
| **Host-side declaration (I19)** | A single-vendor implementation satisfies it *by accident*, because its host conventions are correct in hardcoded form. Test against a synthetic host declaring a different convention. |
| **The zero-host path, and promotion (I20, I23, I32)** | Never exercised in a studio, and it has two entry conditions ([§8.3g](#83-the-zero-host-regime)) of which only one is easy to test. A device that reuses its arbitration code path collapses candidates it should keep separate; one that promotes everything mints two Shots for a swing whose screen strike it correctly detected; one that mints on host silence does the same in a live session. All three look fine until someone counts the shots. |
| **Relation composition (I18)** | An implementer will compose relations silently because it is convenient and appears to work. |
| **Comprehension versus origination (I24)** | An implementation that only ever talks to itself never sees a message from a profile it lacks. |
| **Provenance of unmeasured timing constants (I31)** | An unmeasured offset declared as `0` behaves correctly in every test written against the same implementation, and biases every cross-source comparison against anyone else's. |

### A.3 One implementation, both ends

`libppcp` is the reference implementation and both ends link it. This is not a convenience: two hand-written implementations of a wire format always drift, and the drift surfaces as timing bugs that look like hardware faults.

The corollary for a mobile team: the protocol layer is not Swift or Kotlin. It is the shared portable core, wrapped natively, with no platform type crossing that boundary.

### A.4 What to write down when you disagree

[Annex B](#annex-b--open-issues) lists what is known to be unsettled. If implementation reveals any of it to be wrong, that is the expected outcome, not a failure of the specification — but **the change belongs in the specification first and the code second**, or the document stops describing the system.

---

## Errata after revision 9

*Changes made after the specification was approved, during implementation. Each is normative and each names the finding that produced it.*

| # | Clause | Change |
|---|---|---|
| **E1** | [`PPCP-ENC` §2.1](ppcp-encoding.md#21-binding-streams-to-a-link) | **Added, 22 August 2026.** A link is bound by an explicit `link_bind` first frame carrying a dialler-minted `link_id`. Two implementations had invented two different implicit rules for associating a peer's connections, each correct against itself and neither able to meet the other. |
| **E2** | [`PPCP-MSG` 6.1g](ppcp-messages.md#61-sync_probe--sync_reply) | **Added, 23 August 2026.** Where `sync_probe.timebase_id` names a timebase the **responder** declared, the responder stamps `t2`/`t3` on that timebase. Without it a peer with one clock could not measure two clocks of one counterpart — 6.1d addressed the prober's clocks and 6.1b left the responder's to the responder — so I21's remote half was unreachable (F-H5-1, PinPointStudio, S3). |
| **E3** | [`PPCP-RV` 7.3a, 7.3f, 7.5c](ppcp-rv.md#73-single-use-and-expiry) | **Amended, 23 August 2026.** `mu` counts **pairings**, not handshakes, and invalidates the **code** rather than the pairings made from it. A link is several TLS handshakes over one `K_tls` (§3.1, `ENC` §2.1), so the default `mu: 1` was spent by the control channel and the bulk channel of the same link was refused; and the literal reading made §7.5's reconnection impossible by default (F-H6-1, F-H6-1a, PinPointStudio, S4). |
| **E4** | [`PPCP-RV` 2c, 2c1](ppcp-rv.md#2-rendezvous-paths), [RT-5](ppcp-rv.md#9-conformance) | **Amended, 23 August 2026.** 2c's "there is no unauthenticated path" forbade the plaintext transport that `PPCP-CONF` §2c's own required test infrastructure runs over, while 9a permits it — jointly unsatisfiable for a peer that both claims RV and is testable. 2c1 scopes 2c to the rendezvous paths and states what still binds a claiming peer on a handed-in socket (F-D9-1, PinPointCapture, S4). RT-5 reworded for E3. |
| **E5** | [`PPCP-ENC` §5.1](ppcp-encoding.md#51-worked-example) | **Amended, 23 August 2026.** The worked example is re-emitted in RFC 8949 §4.2.1 deterministic key order — `t1` before `type`, `ns` before `tb` — because the ordering as first written could not be produced by an encoder honouring `ENC` 4e. Same message, same values, same 87 bytes. 5.1a states that the envelope's reserved keys and the body's keys sort into one sequence; 5.1b keeps the old ordering legal on receipt (`libppcp`, S1). |
| **E6** | [`PPCP-ENC` 5a1](ppcp-encoding.md#5-message-envelope) | **Added, 23 August 2026.** Eight message bodies list a `session_id` field that `ENC` 5a reserves and 4d makes malformed as a duplicate — so `session_open` was unencodable. The body's `session_id` **is** the envelope's, written once in the envelope position and read back from the same flat map (`libppcp`, S2). |
| **E7** | [`PPCP-ENC` 6g, 6h](ppcp-encoding.md#6-bulk-transfer), [`PPCP-MSG` 8.3h](ppcp-messages.md#83-the-payload_-family) | **Added, 23 August 2026.** A payload had no declared container: `payload_begin` carried `bytes`, `digest` and `chunk_bytes`, and `format.codec` is a codec three hops away, so a receiver writing a clip to disk had to guess. `payload_begin.container` is an IANA media type, required where the bytes are container-framed, and 6h forbids inferring one (PinPointStudio, S2). |
| **E8** | [`PPCP-ENC` 7d, 7d1](ppcp-encoding.md#7-bundle-container) | **Amended, 23 August 2026.** 7d named two completeness states where the protocol has three, so an unasserted, untruncated bundle was neither `complete` nor `partial`. The reader reports the **assertion** (`unknown` where the bundle asserted nothing) and the **truncation** as two facts — which is also what keeps CT-I36 (c) and (d) distinguishable (`libppcp`, S2). |
| **E9** | [`PPCP-ENC` 7h](ppcp-encoding.md#7-bundle-container) | **Added, 23 August 2026.** A bundle must carry `declare` before any frame naming a Capture, Stream, Shot or Candidate. §8.5c scopes Capture identity by the minting peer and a bundle stated that nowhere else, so a file of bare `capture_announce` frames was unattributable and un-deduplicable — the failure I34 exists to prevent (PinPointStudio, S2). |
| **E10** | [6.1f](#61-canonical-instant), [6.2e](#62-rolling-shutter) | **Added, 23 August 2026.** Neither division said how to round. The row instant of 6.2d rounds **half away from zero** over exact rational arithmetic; `d/2` in 6.1 truncates **toward zero** and the inverse applies the same truncated half, so a round trip is bit-exact for an odd exposure. Every worked example has an even `d`, so two implementations could disagree by a nanosecond with both examples passing (`libppcp`, S1). |
| **E11** | [5.3c](#53-timebase), [§10.3](#103-registries) | **Added, 23 August 2026.** `Timebase.kind` is a **closed** enumeration and an unrecognised value is `malformed`. §10.3 now lists which vocabularies are open and states that every other one is closed, scoping 10.1d's "unknown `kind` values are ignored" — a peer cannot ignore whether a clock halts across sleep, because ignoring it means silently choosing an answer (`libppcp`, S1). |
| **E12** | [5.14d1](#514-capture) | **Added, 23 August 2026.** An `absent` Capture may carry `interval` whatever its anchor. 8.4b's answer to an orphan capture request is shot-anchored and `absent`, and the field table forbade the one field that says which span left the buffer (F-D4-2, PinPointCapture, S3). |
| **E13** | [5.8l](#58-capability) | **Added, 23 August 2026.** `AchievedSummary` is camera vocabulary and 5.11b requires a stream-anchored Capture on every `continuous` Stream, including a 100 Hz attitude Stream with no frames. The summary is optional, and on a sample-producing Source `frame_count` counts samples, `realised_rate_mhz` is the sample rate, and the exposure fields are absent — never synthesised to fill the shape (F-D4-3, PinPointCapture, S3). |
| **E14** | [8.2b1](#82-arbitration) | **Added, 23 August 2026.** §8.2 never said **which** contributing Candidate sets `t0`, so two conformant hosts could issue different `t0` for one event and I7 would freeze both. It is the Candidate with the smallest combined timing uncertainty, ties broken by the earliest instant; `confidence` is deliberately not consulted (`libppcp`, S3). |
| **E15** | [C3, C3a, C3b](#222-what-a-profile-confers) | **Amended, 23 August 2026.** C3 binds the **request** class only — an unactionable event is parsed and dropped — and the catalogue's "Profile to originate" column is not the profile a **responder** needs. `candidate` is conferred by Detect and consumed by Arbitrate, so a host with no Detect would otherwise have read the catalogue as licence to refuse a Candidate it is required to arbitrate over (F-L6-1, `libppcp`, S2). |
| **E16** | [`PPCP-MSG` 8.1i, 8.1i1](ppcp-messages.md#81-capture_announce) | **Amended, 23 August 2026.** 8.1i forbade announcing a preview Capture `transfer: pending`, `pending` is the default state of every announced Capture, and 5.11c3 *requires* announcing the discarded preview segment — which holds no payload and has no other transfer state to carry. 8.1i is scoped to a preview Capture **holding payload**; an `absent` one is exempt (`libppcp`, S2). |
| **E17** | [`PPCP-MSG` 5.1e](ppcp-messages.md#51-stream_open--stream_open_ack--stream_close) | **Amended, 23 August 2026.** `stream_close.closed_at` is **optional**, matching `Stream.closed_at` at 0..1, and its timebase is the Stream's. 5.1d lets the **consumer** close a Stream and a consumer has no reading of the owner's clock, so it omits `closed_at` and the owner stamps it; a receiver MUST NOT substitute its own clock, which would put a timestamp in a timebase it does not belong to (F-H4-2, PinPointStudio, S3). |
| **E18** | [`PPCP-MSG` 1c](ppcp-messages.md#1-scope-and-conventions), [§11](ppcp-messages.md#11-message-index) | **Added, 23 August 2026.** The `CONF` 5b2 sweep of all 45 messages: **27 were required by no normative clause**. Seven were responses nothing obliged a peer to send — the one real hole, closed by 1c; eight had an obligation that did not name the message, now reworded; fifteen are deliberately optional and say so in §11's new **Required by** column. The 5b1 audit asserts the column against the documents on every run, so the answer cannot go stale (`libppcp`, S4/S5). |
| **E19** | [`PPCP-CONF` §3, CT-S1, CT-S4(1), 5a1](ppcp-conformance.md#3-the-invariant-test-matrix) | **Amended, 23 August 2026.** Four corrections to the conformance document. `CT-I36a` is the second **test** of I36 and there is no invariant I36a. CT-S1 said "the other four" over six assertions. **CT-S4 assertion 1 required a hostless session to run `arm` end to end, which 7.3b forbids** — it is `readiness`, the effect, and the same defect is item four of 5b1's own list. And §5's `unrelated` row: "excludes" there means 8.2i1 retention-without-grouping, because 8.2d's uncertainty branch is never reached by a peer with no relation at all (F-D4-4 and `libppcp` S3; the last found by the S5 interoperability runs). |
| **E20** | [`PPCP-RV` 4.3a1](ppcp-rv.md#43-payload) | **Added, 23 August 2026.** 4.3a promised byte-identical codes for a given pairing and did not say whether a defaulted optional is emitted. It is: both §10.3 vectors emit `mu: 1`, and absence versus a defaulted presence is the same meaning and different bytes. Binds encoders only; a decoder still reads the default from absence (`libppcp`, S1). |
| **E21** | [`PPCP-RV` 5.3a1](ppcp-rv.md#53-psk-identity) | **Added, 23 August 2026.** **No octet of the PSK identity may be `0x00`** — the client draws `rn2` again if either it or the tag carries one. Several TLS stacks length a PSK identity with `strlen`, so an embedded zero truncated it and the handshake failed roughly one connection in sixteen: an intermittent failure diagnosed as a network fault. Costs 1.07 HMACs, changes nothing at the server, and §10.2's vector is already free of zeros (PinPointStudio, S1). |
| **E22** | [`PPCP-RV` 5.3c1](ppcp-rv.md#53-psk-identity) | **Scope recorded, 23 August 2026.** The resolved-identity-with-wrong-key case 5.3c and 5.3d equalise is **unreachable** while `K_tls` and `K_id` share one `PRK`: a peer with the wrong secret has the wrong `K_id` and produces an unresolvable identity. The clauses stand for a future schedule that separates the two keys; until then a harness may record RT-11 as *not applicable* on such a code path, and must say so rather than report a pass (F-D1-2, PinPointCapture, S1). |
| **E23** | [`PPCP-RV` 3.5d](ppcp-rv.md#35-who-advertises-and-who-browses) | **Added, 23 August 2026.** A peer whose platform cannot resolve a PSK identity **server-side** must not advertise for reconnection, and 3.5b does not apply to it: the roles reverse under 3.5c and that is the conformant shape rather than a deviation. Measured — `Network.framework`'s listener has no server-side PSK resolver. The required pairing-code path is unaffected (F-D1-1, PinPointCapture, S1). |
| **E24** | [`PPCP-RV` 4.4a2](ppcp-rv.md#44-handling-a-scanned-code) | **Decided by L17, 23 August 2026, reversible.** 4.4a1's "never synchronised since boot" is not readable on iOS. The trigger becomes three individually-optional tests of which only the first — a clock reading earlier than the software's own build date — is required, and a peer that can evaluate only that one is conformant. 7.3e bounds the cost of a false negative to one round trip (F-D7-1, PinPointCapture, S4). |
| **E25** | [`PPCP-RV` 3.3d, 3.3e](ppcp-rv.md#33-txt-record) | **Decided by L17, 23 August 2026, reversible.** `pv`'s `1.0-1.2` was an example defined nowhere, and `MSG` 3.1b and `CORE` 10.1f spelled the same idea two other ways. One **range** syntax — inclusive `LOW-HIGH` within a MAJOR, comma-separated across MAJORs — used by `pv` and by `detail.supported`; `hello.versions` stays an ordered **list**, because an initiator offers rather than describes (F-D7-2, PinPointCapture, S4). |
| **E26** | [`PPCP-RV` 7.4h](ppcp-rv.md#74-persistent-pairings) | **Decided by L17, 23 August 2026, reversible.** A persisted pairing MAY keep the network **name** `wifi.s` and nothing else — never the passphrase — as a hint the user acts on. Without it §7.4's workflow failed at the venue it was written for: 4.4c discards the payload, §7.4 persists only `PRK`, 6b returns the network to the user, so a valid persisted pairing could not reach its publisher (F-D7-3, PinPointCapture, S4). |
| **E27** | [`PPCP-RV` 3.4d1, 3.4d2](ppcp-rv.md#34-resolvable-identifiers) | **Decided by L17, 23 August 2026, reversible.** A peer holding several pairings advertises **one instance at a time**, rotating which pairing on the 15-minute `rn` rotation, recently-used first — and SHOULD browse as well, which resolves every held pairing in one pass. Annex B3 is narrowed: repeated keys and several instances stay unspecified because both leak the count (F-D7-4, PinPointCapture, S4). |
| **E28** | [`PPCP-MSG` 4.1a1, 9.1b](ppcp-messages.md#41-session_open) | **Added, 23 August 2026.** **A `session_open` naming a different `session_id` while a Session is open opens a SECOND Session and changes nothing about the first.** 4.1a's immutability rule was written about *the same* `session_id`, so an implementation reading it literally guarded nothing against a different one — and one `session_offer` accepted mid-session silently rebound the host's `timebase_ref` to the exporting device's clock: every subsequent `t0` was in a timebase the live Session had never declared, and two Sessions' Candidates were arbitrated as one. Nothing on the wire was malformed. Frames of an imported Session are scoped to it by the envelope's `session_id` (F-S5-3, PinPointStudio and PinPointCapture, S5). |
| **E29** | [8.2d1](#82-arbitration) | **Added, 23 August 2026.** A Candidate excluded for want of a relation is **reconsidered when the relation arrives** — it joins its group if no Shot was issued and attaches under 8.2e if one was. 8.2d said exclusion is a conclusion and said nothing about the relation arriving later, which on a live link it always does while §6.3's burst converges; under the other reading a peer nominating early was silently unarbitrated for the whole Session, with no error and every Candidate retained exactly as 8.2d requires (F-S5-1, S5). |

---

## Annex B — Open issues

Tracked against Draft 1. Each is expected to close before `ppcp/1.0` is declared stable.

| # | Issue | Status |
|---|---|---|
| ~~**B11**~~ | ~~A `continuous` Stream has no way to carry its data.~~ | **Closed in revision 5.** `Capture.anchor` gains a third form ([§5.14](#514-capture)), I27 amended, I36 added, and `preview` defined as the stream kind a live consumer wants ([§5.11.2](#5112-preview-streams)). |
| ~~**B14**~~ | ~~Six requirements are not met.~~ | **Closed in revision 7.** All six gaps of [`requirements-traceability.md`](requirements-traceability.md) are answered: `Annotation` and the **Markup** profile ([§5.18](#518-annotation)) cover user artefacts, bidirectional content and navigation anchors; `transfer: confirmed` with a receiver-asserted commit ([§5.14f–g](#514-capture)) covers the sync-state obligation; `Source.optics`, `Source.viewpoint` and three `ContextChange` values cover the rest. |
| **B15** | **The specification has no traceability to the requirements.** Only seven requirement identifiers appear anywhere in the set, because the document deliberately restates reasoning rather than citing sources — which is right for a third-party implementer and means nothing was checking the requirements were all answered. The audit of [`requirements-traceability.md`](requirements-traceability.md) is a point-in-time substitute; keeping it current is manual. | Open — decide whether the matrix is maintained or the citations return. |
| **B0** | **Approved is not stable.** `ppcp/1.0` freezes when [`PPCP-CONF`](ppcp-conformance.md) passes on both implementations and the interoperability pairings are demonstrated. Until then this document takes errata. | Open by design. |
| **B1** | **`PPCP-RV` is drafted but not agreed.** [Draft 1](ppcp-rv.md) specifies the service type, TXT contents, pairing-code payload, key derivation, TLS profile and security model, with test vectors. Until the implementation teams agree it, two conformant peers still cannot be relied on to find one another. | **Blocking interoperability**, not blocking implementation. Awaiting review. |
| **B2** | **`SessionLink` is untested** ([§5.17](#517-sessionlink)). Resolved rather than deferred so implementers do not invent divergent forms, but nothing has exercised it. Support is OPTIONAL at v1. | Provisional. Re-examine when offline multi-device is built. |
| **B3** | **Source ownership transfer mid-session.** Ownership is settled at session start. Whether it may move afterwards — relevant if a host disconnects and a capture peer should take over a wrist sensor rather than lose it — is unspecified. Probably wants to be legal. | Open. |
| **B4** | **Launch monitor shapes. Reopened in Draft 2 on new evidence.** The host's actual integration is a two-line file rewritten in place, attributable only by arrival order — neither a live nominator nor a retrospective export. Draft 2 adds the third shape ([§8.1](#81-nomination), `basis: arrival_pairing`). What remains open is whether `kind: launch_monitor` as a Source is still the right model for the *connected* case, which no implementation has yet exercised. | **Reopened.** Third shape resolved; the connected case awaits a real device. |
| **B5** | **Per-timebase sync obligation stated in the model** ([§5.4.1](#541-the-replacement-obligation)) rather than left to the message layer. Arguably a protocol behaviour rather than a structural fact. | Settled; recorded so it is not re-litigated silently. |
| ~~**B6**~~ | ~~Support window needs a number.~~ | **Closed in Draft 2.** Two MINOR versions back or twelve months, whichever is longer, stated in `hello_accept.min_version`, with the supported range carried in `unsupported_version` ([§10.1](#101-version-negotiation)). Both implementation teams proposed compatible answers. |
| **B7** | **Candidate audio retention has no protocol-level bound**, by design (I14). The consequence for a user-visible retention statement is an application obligation, and the two teams should confirm they are content with that division. | Open — needs confirmation, not a protocol change. |
| **B8** | **Neither timing default is measured, and the window may not admit a single value.** `coincidence_window_ns` (50 ms) and `issue_hold_ns` (200 ms) are proposals. The window needs its **floor** and its **ceiling** measured separately, and the floor **per nominator class**: (i) acoustic-to-acoustic agreement between a device microphone and a host microphone on the same shot after time-of-flight correction — the tightest case and the one the protocol most cares about; (ii) agreement between an acoustic nomination and a live external nominator with a coarser clock, which may be an order of magnitude wider; (iii) the separation to an adjacent bay, which sets the ceiling — a shot 4 m away arrives ~12 ms later, so a window that is too wide merges two real shots into one, a worse failure than missing a merge. **If (ii) exceeds (iii) there is no single conformant value**, and the resolution is a per-`basis` override. That is an additive change: only an arbitrating host consumes the window, so an optional per-basis map can be added in a MINOR version without breaking a device. It is therefore not added speculatively — but the measurement must be designed to answer the question, which means measuring the classes separately rather than pooling them. | Open — rig data, measured per nominator class. |
| ~~**B9**~~ | ~~`arrival_pairing` is asserted without confirmation.~~ | **Closed in Draft 3.** `ShotLink.confirmed_by` separates an observer's live assertion from a human decision ([§5.16e](#516-shotlink)), so a consumer can tell which it has before a mis-pair rather than after. |
| **B10** | **`exposure_provenance: sampled`** ([§5.8](#58-capability)) is conformant but its accuracy under an unlocked exposure is unquantified. Whether a consumer should accept it is policy (I14), but nobody has measured how wrong it gets. | Open — measure on the rig alongside B8. |

---

## Annex C — Terminology

| Term | Meaning here |
|---|---|
| **Peer** | Any PPCP implementation participating in a Session. Not a synonym for device. |
| **Host** | The peer with `role: host`. At most one per Session. Arbitrates. |
| **Capture peer** | A peer with `role: capture`. Owns Sources and produces Captures. |
| **Nominate** | To emit a Candidate. |
| **Mint** | To issue a Shot from one's own Candidates, `authority: device`. |
| **Hostless session** | A Session with no `host` in its roster. |
| **Host-unreachable interval** | A Session that has a host which has stopped answering ([§8.3g](#83-the-zero-host-regime)). Its roster and `timebase_ref` are unchanged. |
| **No arbitrating host** | Either of the above. The condition I23 and [§8.3](#83-the-zero-host-regime) are scoped to. |
| **Arbitrate** | To issue a Shot from the Candidates of several peers, `authority: host`. |
| **Canonical instant** | Mid-exposure, per [§6.1](#61-canonical-instant). |
| **Stream-anchored Capture** | A Capture realising an interval of a `continuous` Stream rather than an event. `anchor: { stream: true }`. |
| **Preview** | A `continuous`, low-rate Stream from a capture Source, for live monitoring and never for measurement. |
| **Annotation** | A user artefact, or a device-advisory navigation anchor, anchored to a Shot and a frame instant. Never an observation. |
| **Bundle** | A Session serialised as a recorded PPCP message stream. Not a distinct entity. |
| **Declaration** | The symmetric exchange in which each peer states its timebases, sources, profiles and calibration. |

---

# Annex D — Change history

*Non-normative. How the specification reached its present form, newest last. Retained so a decision that was argued over is not silently re-litigated.*

## What changed in Draft 1

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

## What changed in Draft 2

From the two implementation-team reviews of Draft 1. Both teams approved; these are the changes they asked for.

| # | Finding | Disposition |
|---|---|---|
| PPS-F1 | **I23 turned every diagnostic Candidate into a Shot.** A ball-into-screen transient ~9 ms after impact is inside the conformance suite's own 10 ms assertion, so a correct offline device minted two Shots per swing — and passed. | **Accepted.** The Candidate-to-Shot *identity* is broken; a Mint peer **promotes** a subset of its own Candidates. No coincidence window, one Candidate per Shot, every Candidate still emitted and retained. I23 rewritten, I8 extended to Mint. [§8.3](#83-the-zero-host-regime) |
| PPS-F2 | **The protocol never modelled *when* a host may issue a Shot**, so a nominating peer had no deadline after which to mint, and `coincidence_window_ns` was silently doing two jobs. | **Accepted.** `Session.issue_hold_ns` added and separated from the pairwise tolerance; I32 added giving a nominating peer a deterministic mint deadline. [§8.2](#82-arbitration) |
| PPS-F4 | **The hardened launch-monitor path does not match the launch monitor that exists** — a two-line CSV rewritten in place, no trustworthy timestamp, attributable only by arrival order. `sequence_alignment` presumes a multi-record export. | **Accepted.** A third association shape added: a live record with an observing Peer and **no clock**, associated by `ShotLink` with `basis: arrival_pairing`. Annex B4 reopened. [§8.5](#85-reconciliation) |
| PPS-F5 | The exposure-convention rationale rested on protecting a clock-bias estimator that does not exist in the host today. | **Accepted.** The justification no longer depends on any estimator existing. [§6.1](#61-canonical-instant) |
| PPS-F8 | Host session ids are filesystem paths and shot ids are ordinals; idempotent re-import keys on neither. | **Accepted.** An `Id` minted by a peer must not be derived from mutable local state. [§5.1](#51-notation-and-primitive-types) |
| PPC-1.1 | **Online time-of-flight estimation conflicted with I5.** The only home for the converging estimate was `Calibration`, and changing a Calibration closes the Stream — up to fifty stream cycles per range session. | **Accepted, resolved differently from the reviewer's first preference.** The *applied* correction moves onto the Candidate, where it is consumed; `Calibration` keeps the surveyed position. I5 is untouched. [§5.12](#512-candidate), [§5.9](#59-calibration) |
| PPC-1.2 | `tof_correction_ns` was the one estimate in the specification carrying no uncertainty. | **Accepted.** `tof_correction { value_ns, sigma_ns }`, both mandatory together. I29 added. |
| PPC-2.1 | **`capture_announce` was called the small message and was ~44–70 KB**, carrying per-frame arrays on the latency-critical channel the two-channel design exists to protect. | **Accepted.** `AchievedCapability` splits into a summary on control and per-frame series with the payload; constant-valued series may be sent as a scalar. I30 added. [§5.8](#58-capability) |
| PPC-2.2 | **A declared `frame_start_to_exposure_offset_ns` of `0` was indistinguishable from an unmeasured one** — and no device model has been through the rig. Same for `readout_ns`. | **Accepted.** Both carry provenance, as `MeasuredCapability.method` does. I31 added, extended to per-frame exposure. [§5.7](#57-captureprofile) |
| PPC-3 | Per-frame exposure may not be attachable per sample buffer on iOS; the specification should state the honest conformance position. | **Answered.** `exposure_provenance` distinguishes a value attached to the frame from a sampled device property from a locked constant. [§5.8](#58-capability) |
| Q5 | Both teams answered the support-window question compatibly. | **Settled**, closing Annex B6. [§10.1](#101-version-negotiation) |
| PPS-F3, F6, F7, PPC-3 (partial) | Host-side and application-side work items, and requirements-document defects. | **No specification change.** Recorded in the disposition and referred on. |

## What changed in Draft 3

From the second round of reviews. Both teams approved again; both independently found the same defect in the fix that closed the first round's most serious one.

| # | Finding | Disposition |
|---|---|---|
| PPS-R1 / PPC-1.1 | **The issue-hold fix reintroduced "every Candidate becomes a Shot" in the live regime**, and `CT-I32` certified it. Nothing obliges a host to answer a Candidate it declines, so after the deadline a peer minted a Shot for every nomination the host had rejected. Separately, a deadline narrows the mint/issue race rather than closing it: a host `shot` arriving after the deadline produces two immutable, unmergeable Shots with no withdraw message to reach for. | **Accepted in full, from both angles.** The mint is now conditioned on the peer's own promotion policy; the host's issue window is bounded so it cannot overlap the mint window; a host that receives a device-minted Shot attaches to it instead of competing; and where both fire, they are linked by `shared_candidate`. I32 amended, I35 added, [§7.1](#71-roles) gains the third row. |
| PPS-R3 | **`Candidate.at` had no stated convention**, and §8.2a told the host to apply the canonical-instant conversion with an input — that frame's exposure — that a Candidate does not carry. Harmless for acoustic by accident; a ~1 ms systematic error for a `motion` candidate, inside any plausible window, so arbitration still succeeds and `t0` is quietly wrong. | **Accepted.** The nominator converts before emission ([§5.12e](#512-candidate)); §8.2a drops the clause. I33 added, and `canonical_correction_ns` keeps the correction visible. |
| PPS-R2 | **`PPCP-MSG` 8.2b permitted what I30 and 5.8g forbade** — a straight contradiction between two normative documents, which the suite did not catch. | **Accepted.** I30 and 5.8g narrowed to admit the one exception, which is right: a `complete` + `failed` capture is a session whose link died, and the frame timeline is what says what was lost. |
| PPS-R4 | **The scalar form was ambiguous for `intrinsics`** — the one field it was added for — because its element type is itself an array. | **Accepted.** [`PPCP-ENC` 4.1d](ppcp-encoding.md#41-composite-types) disambiguates by the type of the first element. |
| PPC-1.2 | **`Capture.digest` was the stated identity for idempotent re-import**, and an absent capture has no payload to hash. | **Accepted.** Identity is `Capture.id` scoped by session and owning peer; the digest is a content check. I34 added. |
| PPC-1.3 | **Two arbitration parameters were mandatory on sessions that never arbitrate.** | **Accepted.** Present if and only if the Session has a host ([§5.10e](#510-session)) — I23 expressed structurally. |
| PPC-3 | 5.8d was unsatisfiable for an absent capture. | **Accepted.** Conditioned on the Capture having frames. |
| PPS §6 | **The same failure mode twice: an invariant that constrains a choice rather than a shape.** | **Accepted and promoted.** [§11.1](#111-the-rule-for-writing-an-invariant) is now a rule for writing invariants in this document. |
| PPS §3 | `ShotLink.confirmed` had come to carry two epistemic states. | **Accepted.** `confirmed_by: observer \| user` ([§5.16e](#516-shotlink)). Closes Annex B9. |
| PPC §2 | The coincidence window's floor and ceiling may be incompatible, so a per-`basis` tolerance may be needed. | **Not added speculatively; the measurement redesigned.** Only an arbitrating host consumes the window, so a per-basis override is additive in a MINOR version. Annex B8 now asks for the floor **per nominator class**. |
| PPS §2 | Five consistency items — counts, stale field names, support-window wording. | **All accepted.** |

## What changed on approval

The closing round. Both teams signed off; these are the findings they attached to their sign-off, and all are carried here.

| # | Finding | Disposition |
|---|---|---|
| PPS-S1 | **A peer told to mint may have no expressible `t0`.** The commonest reason a host stays silent is that it excluded the Candidate under 8.2d — for a missing, `unrelated` or too-uncertain relation to `timebase_ref` — which is exactly the condition under which the peer cannot express `t0` there either. The required `unrelated` interoperability pairing puts every candidate in that state. | **Accepted.** [§8.2i1](#82-arbitration): a peer that cannot express `t0` does not mint, and retains the Candidate with no Shot. |
| PPS-S1(b) / PPC-2.1 | **The zero-host regime has two entry conditions** — no host in the roster, and a host that has stopped answering — described in the same words, with I23 reading as though only the first existed. | **Accepted.** [§8.3g](#83-the-zero-host-regime) separates them, [§8.3h](#83-the-zero-host-regime) states that I23 binds at issuance rather than for ever, and I23 and Annex C are reworded. |
| PPS-S2 | **Mint and Arbitrate carried MUSTs discharged through `shot_link`, which only Offline conferred** — a direct C2/I24 contradiction, and the third occurrence of the family that produced the Mint profile. | **Accepted, by the reviewer's preferred fix.** `ShotLink` origination moves to **Core** and I9 with it; `SessionLink` stays in Offline. The alternative — adding Offline to Mint and Arbitrate — would make a live-only host implement a bundle container to resolve a socket race. |
| PPS-S3 | **§8.2k has one peer amend another peer's Shot, with no rule for who may.** Before Draft 3 exactly one peer ever sent `shot` for a given id. | **Accepted, wording adopted.** [§5.13d–e](#513-shot) fix the ownership of `id`, `t0`, `authority` and `issued_by`, make `candidates` extensible by any holder, and state that extension is additive and order-independent so the two ends converge. |
| PPS §2.1 | `confirmed_by: observer` was defined in arrival-pairing language and did not describe `shared_candidate`. | **Accepted.** Broadened to *observed the association*; 5.16g states the value. |
| PPS §2.2 / PPC-2.2 | The `intrinsics` scalar rule had no answer for an empty array. | **Accepted.** [`PPCP-ENC` 4.1d](ppcp-encoding.md#41-composite-types): an empty array MUST NOT be emitted and is `malformed` on receipt. |
| PPS §3 | `canonical_correction_ns` is a bare integer beside an `Estimate`, and the asymmetry looks like an oversight. | **No change, and the reason recorded** in [§5.12f](#512-candidate) so it is not "fixed" during implementation. |
| PPC §3 | Positions recorded for implementation: `locked_constant` under the exposure lock, `assumed` provenance until the rig exists, and the mint deadline being a user-visible latency. | **No specification change.** All are consequences the document intends. |

## What changed in revision 5

One defect, found by tracing a host-side requirement rather than by review, and the capability it was blocking.

| # | Finding | Disposition |
|---|---|---|
| **B11** | **A `continuous` Stream could carry nothing.** Every payload message is keyed on `capture_id` and every Capture anchored to a Shot or a Candidate (I27) — so the interval a continuity flag exists to describe was the one interval with no carriage. Three stated obligations were unmeetable: continuous attitude and gravity on a `metadata` Stream that [§5.11](#511-stream) calls *always* continuous; the raw sensor-arrival evidence [§9.1b](#91-clock-authority-inverts) requires a bundle to carry; and `imu`/`wrist` running continuously while armed. | **Fixed.** `Capture.anchor` gains a third form, `{ stream: true }` ([§5.14](#514-capture)). I27 amended, **I36** added for the coverage rule. No new message. |
| — | **A consumer had no way to see that a capture peer reflects what the user is doing.** Heartbeat proves the link is live; only frames prove the rest. | **`preview` defined** ([§5.11.2](#5112-preview-streams)) — a second Stream from an existing Source, low rate, `continuous`, never used for measurement, carried on its own bulk channel and the first thing dropped under contention. It is [§5.11.1](#5111-how-a-continuous-stream-is-carried) applied to a camera, and needs nothing new. |

The change is additive: no field is removed, no type narrowed, and no existing field changes meaning. It lands in `1.0` rather than a MINOR bump because `1.0` is approved and **not yet frozen** ([Annex B0](#annex-b--open-issues)) — and because a flag with nothing behind it is a defect rather than a missing feature.

## What changed in revision 6

Both teams reviewed revision 5 and both approved it. Three findings were made independently by both.

| # | Finding | Disposition |
|---|---|---|
| Both | **A deliberately-shed interval was indistinguishable from a failed one**, and a Stream that recorded *nothing* for a span had no way to say so — `interval` was required absent on an `absent` Capture and required present on a stream-anchored one. | **Accepted.** `interval` is mandatory on every stream-anchored Capture including `absent` ([§5.14d](#514-capture)); [5.11c2](#5111-how-a-continuous-stream-is-carried) separates the two accounts — `gaps` mean **loss**, an `absent` segment means **nothing was captured** — and [5.11c3](#5111-how-a-continuous-stream-is-carried) makes deliberate non-retention an `absent` segment, never a gap. |
| Both | **A preview profile is a derived view, not a mode a Source can enter.** No camera runs two configurations at once; the preview is decimated from the active capture stream. | **Accepted.** [5.11k–m](#5112-preview-streams): realised rate and format are derived while a capture Stream is open, the profile is activatable only on a `preview` Stream, and it declares `intrinsics: none` because scaling changes the matrix. |
| Both | `preview` was missing from the `Stream.kind` enumeration. | **Accepted.** |
| PPS-S2 | **I36 read an honestly truncated bundle as a defect** — and the bundle is the v1 path. | **Accepted.** [5.11c1](#5111-how-a-continuous-stream-is-carried): the obligation binds a `complete` Session; a hole **between** segments is a defect in any Session, time **after** the last one is the incompleteness already declared. Nothing truncates a bundle in the middle. |
| PPS-S3 | **Preview Captures would be queued and bundled**, because `pending` is where an announced Capture starts — the inversion 5.11i forbids, arriving through the transfer queue. | **Accepted.** [5.11j](#5112-preview-streams): preview is **live-only**, never queued, never bundled, and what was dropped is announced absent. |
| PPS-S5 | A concurrent preview makes `MeasuredCapability` optimistic, and the acceptance decision is taken before the preview is opened. | **Accepted.** [5.8k](#58-capability): a measurement describes its profile **running alone** unless the peer says otherwise. |
| PPS-S4 | 5.8d appeared to require per-frame exposure on preview Captures. | **Already addressed** at 5.8j in revision 5, which sits below the clauses that follow 5.8d and was easy to miss. 5.8d now points at it. |
| PPS c2 | Two different `evidence_ref` fields meant different things, and revision 5 made the first usable for the first time — so it will now be implemented by someone reading the other's definition. | **Accepted.** Renamed `evidence_stream_id` and `evidence_capture_id`. |
| PPC-3 | Unclear whether a producer may close a preview it has open — the thermal case. | **Accepted.** [5.11a1](#511-stream): either peer may close, with a reason. |
| PPC-4 | Control traffic now scales with session length, and window length has one voice. | **Answered.** [5.11e–e1](#5111-how-a-continuous-stream-is-carried): the window is the producer's and is not negotiable in `1.0`; the volume is stated and accepted, because the control channel is protected against large payloads rather than against message count. |

## What changed in revision 7

The six findings of the [requirements traceability audit](requirements-traceability.md), which cross-checked all 172 numbered requirements against the specification set. Two were MUSTs with no carriage at all.

| # | Finding | Disposition |
|---|---|---|
| G1 | **No carriage for user-authored artefacts, and no host→device content path at all.** Every payload in PPCP is a Capture, and a Capture realises an observation by a Source — which has a clock, a calibration and an owning peer. A person has none of those. | **`Annotation`** ([§5.18](#518-annotation)) and the **Markup** profile. One entity, one message, either direction. I37 added. |
| G2 | **No commit acknowledgement**, so a peer required to track *local / sent / confirmed* could not reach the third state, and "evict nothing unconfirmed" was satisfiable only by evicting nothing ever. | **`transfer: confirmed`** and a receiver-asserted `capture_committed` ([§5.14f–g](#514-capture)). I38 added. |
| G3 | Navigation anchors had no home in the schema and no distinct naming. | `Annotation` with `provenance: device_advisory`, `kind: nav_anchor` — structurally the same shape as markup, differing only in who authored it. |
| G4 | Lens identity was the one sidecar item not carried. | `Source.optics`, and **5.6d**: a physically distinct lens is a distinct Source. |
| G5 | A device classifies its own viewpoint and "reports it", with nowhere to report it. | `Source.viewpoint`, carrying a **confidence and a method** — a self-classification is a conclusion, and this model carries measurements a consumer may disagree with. |
| G6 | Location and weather had no home. | `ContextChange.kind: location` / `weather`, labels only, never computed from. |

Also `ContextChange.kind: handedness` — which way a golfer swings is a property of the session, not of a camera.

## What changed in revision 8

Both teams reviewed revision 7. One finding was a direct contradiction between two revisions, one made a stated convergence property false, and one made the mechanism unreachable on the path that ships first.

| # | Finding | Disposition |
|---|---|---|
| PPS-C1 | **I38 forbade eviction the specification requires elsewhere**, in four places — most sharply a preview segment [5.11j](#5112-preview-streams) *requires* a peer to discard. A MUST and a MUST NOT one section apart, both added in the previous two revisions. | **Accepted.** [5.14g](#514-capture) names four exits and I38 is scoped to *payload*. The error was scope: I38 exists for shot payload a consumer has not received, and was written as though it were about every Capture. |
| PPC-1 | **`confirmed` was unreachable hostless and across the bundle path** — so G2's gap stayed open for the entry-level case, which the requirements call the normal one. | **Accepted.** [5.14h](#514-capture): a receiver that commits a Capture obtained from a bundle sends `capture_committed` on its next connection with the owning peer. Identity was already sufficient under I34. [5.14i](#514-capture) says plainly that I38 protects nothing where there is no receiver. |
| PPS-C2 | **5.18e's convergence claim was false.** Two peers both at revision 1 both produce revision 2, each ignores the other's equal revision, and they diverge permanently while each believes it converged. | **Accepted.** Equal revisions tiebreak on `author_peer_id`, which is already mandatory — a total order, not a merge. |
| PPS-C3 | **An Annotation could not say which view it was drawn on.** Image coordinates from a down-the-line view rendered on a face-on view are plausibly wrong, which is worse than obviously wrong. | **Accepted.** `stream_id` added; [5.18g](#518-annotation) fixes the timebase of `at` to the named Stream, which also makes the frame anchor exact rather than a conversion away. |
| Both | `body` at 64 KiB on the control channel is above what I30 keeps off it. | **Accepted.** Cap lowered to **8 KiB**, and 5.18i requires coalescing rapid revisions. |
| PPS §2.2 | `viewpoint.confidence` had no meaning under `method: declared`. | **Accepted.** Present if and only if `classified`. |
| PPS §6 | **C1 is the fourth instance of a new MUST contradicting one in an adjacent section.** | **Accepted as a process change** — [`PPCP-CONF` §5b2](ppcp-conformance.md#5-interoperability) makes the adjacent-MUST sweep a required check before `ppcp/1.0` freezes. |
| — | **Found by running that sweep immediately: a fifth instance.** I8 said a Candidate's evidence is never discarded; [5.12.1c](#5121-candidate-evidence) contemplates an *evicted* window and [5.12.1b](#5121-candidate-evidence) makes retention peer policy. | **Fixed.** I8 and [5.12c](#512-candidate) now separate the evidence **record**, which is never discarded, from the evidence **payload**, which may be shed with its absence asserted. |

## What changed in revision 9

The final round. Five findings, all seams rather than shapes, and two of them were edits from the previous round that never reached the file.

| # | Finding | Disposition |
|---|---|---|
| PPS-D1 | **Exit 4 reintroduced the phrase the requirement forbids.** Revision 8's fix for C1 read *"the protocol **or the peer's own declared retention policy** permits the owner to shed it"* — a general licence, against a requirement that says *nothing unconfirmed is evicted, **regardless of retention policy***. A device under storage pressure could declare a policy, shed shot payload, and be conformant. The hole G2 closed was open again, one revision later, through the clause that closed it. | **Accepted.** The licence is deleted and the enumeration kept; [5.14g1](#514-capture) forbids a policy extending it. Every remaining exit is a case where *the protocol itself* says no receiver will ever confirm the payload. |
| PPS-D2 | The adjacent-MUST sweep was accepted as a process change and **the clause was never written** — the changelog cross-referenced a `PPCP-CONF` §5b2 that did not exist. | **Written.** [`PPCP-CONF` §5b2](ppcp-conformance.md#5-interoperability). |
| PPS-D3 | **`CT-I37` and `CT-I38` were not updated for the fixes they test.** CT-I38 exercised only the first of four exits, so the contradiction that produced C1 would still have passed; CT-I37 still tested the *lower*-revision rule when the **equal**-revision case was the whole of C2. | **Both rewritten.** Two more instances of the failure this round accepted a check against — inside the round that accepted it. |
| PPS-D4 | The `body` cap contradicted itself: three places said 8 KiB and **the field table still said 64 KiB**, which is what an implementer builds a validator from. | **Fixed**, and the limit added to [`PPCP-ENC` §8](ppcp-encoding.md#8-limits), where a decoder enforces it before allocating. |
| PPS-D5 | **`stream_id`'s presence rule could not be checked.** It turned on whether `body` was "interpreted in image coordinates", and `body` is opaque — so no peer and no test could tell, and 5.18h had nothing to bind to. [§11.1](#111-the-rule-for-writing-an-invariant)'s own pattern, in a clause added the round before. | **Accepted.** [5.18j](#518-annotation) derives presence from `kind`, which is on the wire, and gives an unrecognised kind a conservative default. |
| PPS §2 | A `capture_committed` for a bundle may arrive against a **closed** Session and be refused, achieving nothing. | **Accepted.** [5.14h1](#514-capture): it is accepted, because releasing storage is the one operation that stays legitimate after a Session closes. |
