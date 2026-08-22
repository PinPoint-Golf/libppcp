# Requirements traceability

**Every requirement in `capture-companion-requirements.md`, and where the specification set answers it.**

| | |
|---|---|
| Status | Audit record. Non-normative. |
| Date | 22 August 2026 |
| Against | `PinPointCapture/docs/capture-companion-requirements.md`, 21 August 2026 (172 numbered requirements) |
| Covering | `PPCP-CORE` revision 6, `PPCP-MSG`, `PPCP-ENC`, `PPCP-CONF`, `PPCP-RV` Draft 5 |
| Result | **All 172 covered or deliberately out of scope.** Eight were not met when this audit was first run; all six findings were closed in `PPCP-CORE` revision 7. |

---

## 0. Why this audit exists, and what it found

The specification is deliberately self-contained: it restates the reasoning behind each decision rather than citing the requirement that motivated it. That was the right call for a document a third party will implement — but it means **only seven requirement identifiers appear anywhere in the specification set**, so nothing was checking that the requirements were all answered. The protocol overview that preceded the specification had traceability tables under each sequence; the specification lost them.

This document restores that check. It is a semantic audit rather than an identifier match: each requirement was read against the clause that answers it.

**Six findings, eight requirements — all now closed.** Two were MUSTs on the protocol with no carriage at all; four were cases where an open registry made the requirement *expressible* but no value was defined, so two implementations would diverge — the same class of defect that produced the launch-monitor and `Stream.kind` findings in earlier rounds. [§3](#3-the-findings) records each finding and what answers it.

| # | Requirement | Level | Finding |
|---|---|---|---|
| **G1** | REQ-MARK-1, REQ-MARK-2 | MUST | No carriage for user-authored artefacts, and no host→device content path at all. → **`Annotation` and the Markup profile** |
| **G2** | REQ-SESS-3, REQ-SESS-4 | MUST | No commit acknowledgement, so `confirmed` was unobtainable. → **`capture_committed` and `transfer: confirmed`** |
| **G3** | REQ-NAV-1, REQ-NAV-2 | MUST | Navigation anchors had no home. → **`Annotation` with `provenance: device_advisory`, `kind: nav_anchor`** |
| **G4** | REQ-CLIP-1 (part) | MUST | Lens identity was not carried. → **`Source.optics`, and a lens is a distinct Source (5.6d)** |
| **G5** | REQ-SETUP-2 | SHOULD | No field to report a viewpoint in. → **`Source.viewpoint`, with confidence and method** |
| **G6** | REQ-META-2 | SHOULD | Location and weather had no home. → **`ContextChange.kind: location` / `weather`** |

**All six are closed in revision 7**, and none has been reviewed by the implementation teams. The two that needed a design decision — G1 and G2 — are set out in [§3](#3-the-findings) with the reasoning for the shape taken, because those are the ones to disagree with.

---

## 1. Coverage by family

`✓` covered · `◐` partially covered, see notes · `—` deliberately out of scope · `✗` gap

### Protocol core

| Requirement | | Where |
|---|---|---|
| REQ-TIME-1 | ✓ | **I1**; `Instant` carries `tb` and there is no encoding without one ([`ENC` 4.1a](ppcp-encoding.md#41-composite-types)) — unwriteable rather than forbidden |
| REQ-TIME-2 | ✓ | [`CORE` §5.4](ppcp-core.md#54-timebaserelation) `affine`/`unrelated`, **I3** mandatory sigmas, **I4** shared id for known-equal |
| REQ-TIME-3 | ✓ | [`CORE` §7.2b](ppcp-core.md#72-session-lifecycle), **I14**, [`MSG` §3.4](ppcp-messages.md#34-declare_ack) `policy_reject` with a reason |
| REQ-TIME-4 | ✓ | [`CORE` §5.3](ppcp-core.md#53-timebase) `kind`/`epoch_stable`, [§5.5](ppcp-core.md#55-clockdiscontinuity) ClockDiscontinuity, [§6.4](ppcp-core.md#64-clock-discontinuity) |
| REQ-TIME-5 | ✓ | **I2**; `frames.ns` has no scalar form for this reason ([`CORE` §5.8e](ppcp-core.md#58-capability)) |
| REQ-EXP-1 | ✓ | [`CORE` §6.1](ppcp-core.md#61-canonical-instant) |
| REQ-EXP-2 | ✓ | `timing.convention` on the profile + per-frame `exposure_ns` in `AchievedFrames`; **I17** names all three inputs |
| REQ-EXP-2a | ✓ | [`CORE` §5.6.1](ppcp-core.md#561-symmetric-declaration), **I19**, [`MSG` §3.3d](ppcp-messages.md#33-declare) — a host with no Sources still sends `declare` |
| REQ-EXP-3 | ✓ | `geometry: rolling_shutter { readout_ns, direction, rows }`, [`CORE` §6.2](ppcp-core.md#62-rolling-shutter), **I31** provenance |
| REQ-SYNC-1 | ✓ | [`CORE` §6.3a](ppcp-core.md#63-clock-synchronisation), [`MSG` §6.1](ppcp-messages.md#61-sync_probe--sync_reply) four timestamps |
| REQ-SYNC-1a | ✓ | [`CORE` §5.4.1](ppcp-core.md#541-the-replacement-obligation), **I21** — binds hosts too |
| REQ-SYNC-2 | ✓ | [`CORE` §6.3c–d](ppcp-core.md#63-clock-synchronisation); heartbeat MUST NOT set the sync rate |
| REQ-SYNC-3 | ✓ | [`CORE` §6.3e](ppcp-core.md#63-clock-synchronisation) |
| REQ-SYNC-4 | ✓ | [`MSG` §6.2](ppcp-messages.md#62-sync_residual) `sync_residual` |
| REQ-STREAM-1 | ✓ | [`CORE` §5.11](ppcp-core.md#511-stream) open `kind` registry, [§10.3](ppcp-core.md#103-registries) |
| REQ-STREAM-2 | ✓ | **I19**, [`CORE` §5.2c](ppcp-core.md#52-peer); `ThermalLevel` is mapped from platform states, not passed through |
| REQ-STREAM-3 | ✓ | [`CORE` §5.6c](ppcp-core.md#56-source) — ownership is `peer_id` and nothing else |
| REQ-CAP-1 | ✓ | `CaptureProfile` ([`CORE` §5.7](ppcp-core.md#57-captureprofile)) |
| REQ-CAP-2 | ✓ | `MeasuredCapability` per profile, **I28**; **5.8k** adds that it describes the profile running alone |
| REQ-CAP-3 | ✓ | `AchievedSummary` + `AchievedFrames` ([`CORE` §5.8](ppcp-core.md#58-capability)) |
| REQ-CAP-4 | ✓ | `optical{}` on the profile, `achieved_exposure_ns`/`achieved_iso`/`noise_figure` on `measured` |
| REQ-CAP-5 | ✓ | **I14**, and [`CONF` §6](ppcp-conformance.md#6-what-conformance-does-not-cover) confirms the suite does not test it |
| REQ-VER-1 | ✓ | [`CORE` §10.1a](ppcp-core.md#101-version-negotiation), [`MSG` §3.1–3.2](ppcp-messages.md#31-hello) |
| REQ-VER-2 | ✓ | **I13**, [`ENC` 4b](ppcp-encoding.md#4-primitive-types) — unknown keys skippable at every nesting level |
| REQ-VER-3 | ✓ | [`CORE` §10.1e](ppcp-core.md#101-version-negotiation) — settled at two MINOR back or twelve months |
| REQ-TRANS-1 | ✓ | [`CORE` §3](ppcp-core.md#3-transport-contract) T1–T4 |
| REQ-TRANS-2 | ◐ | Addressing and discovery are out of PPCP by T4 and specified in [`PPCP-RV`](ppcp-rv.md). The *pluggable locator interface* is a library API shape, not a wire concern |
| REQ-TRANS-3 | — | Library packaging. [`ENC` §9](ppcp-encoding.md#9-design-rationale) chooses CBOR partly to avoid heavy dependencies |

### Shot determination and detection

| Requirement | | Where |
|---|---|---|
| REQ-SHOT-1 | ✓ | [`CORE` §8.1](ppcp-core.md#81-nomination), [§8.2](ppcp-core.md#82-arbitration) |
| REQ-SHOT-2 | ✓ | [`CORE` §8.4](ppcp-core.md#84-orphan-capture-requests), [`MSG` §7.3](ppcp-messages.md#73-capture_request) |
| REQ-SHOT-3 | ✓ | **Mint** profile, [`CORE` §8.3](ppcp-core.md#83-the-zero-host-regime) |
| REQ-SHOT-4 | ✓ | `Candidate.at` + `confidence` ([`CORE` §5.12](ppcp-core.md#512-candidate)) |
| REQ-SHOT-5 | ✓ | `Candidate.basis` |
| REQ-SHOT-6 | ✓ | **I26** — narrowed to *live* nominators; a clockless record is associated by `ShotLink` ([`CORE` §8.1](ppcp-core.md#81-nomination)) |
| REQ-MIC-1 | — | Device audio configuration |
| REQ-MIC-2 | — | Device detector implementation |
| REQ-MIC-3 | ✓ | [`CORE` §8.1d](ppcp-core.md#81-nomination); the constant lives in the microphone Source's `Calibration` |
| REQ-MIC-4 | ✓ | Online estimation carried per-Candidate as `tof_correction` with mandatory sigma (**I29**), which is what makes convergence visible |
| REQ-MIC-5 | — | Classifier design, device-internal |
| REQ-MIC-6 | ✓ | `confidence` + basis-specific `classifier` on Candidate |
| REQ-NAV-1 | ✓ | `Annotation`, `provenance: device_advisory`, `kind: nav_anchor` ([`CORE` §5.18](ppcp-core.md#518-annotation)) |
| REQ-NAV-2 | ✓ | **I37** — never persisted or interpreted as phase data |
| REQ-NAV-3 | ✓ | The impact anchor is the acoustic Candidate's instant / `Shot.t0` |

### Session, state and resources

| Requirement | | Where |
|---|---|---|
| REQ-SESS-1 | ✓ | [`CORE` §5.10](ppcp-core.md#510-session) — roster, `contexts`, state, completeness |
| REQ-SESS-2 | ✓ | **I20**, [`CORE` §7.1](ppcp-core.md#71-roles), [§8.3](ppcp-core.md#83-the-zero-host-regime) |
| REQ-SESS-3 | ✓ | `transfer: pending \| in_flight \| present \| confirmed \| failed` ([`CORE` §5.14f](ppcp-core.md#514-capture)) |
| REQ-SESS-4 | ✓ | **I38**, and `capture_committed` is the only way `confirmed` is reached |
| REQ-SESS-5 | ✓ | [`CORE` §3.1](ppcp-core.md#31-why-two-channels-is-not-negotiable), [`MSG` §8.1](ppcp-messages.md#81-capture_announce) with a thumbnail ≤64 KiB |
| REQ-SESS-6 | ✓ | [`MSG` §8.3](ppcp-messages.md#83-the-payload_-family), T2 |
| REQ-STATE-1 | ✓ | [`CORE` §7.3a](ppcp-core.md#73-streams-and-capture-control) |
| REQ-STATE-2 | ✓ | Expressed as a measurement rather than a state — `Readiness.settled` / `estimated_ready_ms` |
| REQ-STATE-3 | ✓ | [`CORE` §7.4c](ppcp-core.md#74-liveness) |
| REQ-STATE-4 | ✓ | [`CORE` §7.3e](ppcp-core.md#73-streams-and-capture-control) |
| REQ-STATE-5 | ✓ | [`MSG` §5.3](ppcp-messages.md#53-interruption) `interruption` with its interval |
| REQ-STATE-6 | ✓ | **5.15a MUST NOT** plus [`MSG` 5.2b](ppcp-messages.md#52-arm--disarm--readiness) — stated twice deliberately |
| REQ-RES-1/2 *(resolution)* | ✓ | Any rate and resolution is declarable; acceptance is policy (**I14**) |
| REQ-RES-1/2 *(replay)* | — | Application resource policy |
| REQ-RES-3 | ✓ | `ThermalLevel` in `heartbeat_ack` ([`MSG` §5.4b](ppcp-messages.md#54-heartbeat--heartbeat_ack)) |
| REQ-RES-4 | ◐ | Battery is reported in `heartbeat_ack`; the *target* is a product verification |
| REQ-RES-5 | — | Product/thermal decision |

### Offline, privacy and the bundle

| Requirement | | Where |
|---|---|---|
| REQ-OFF-1 | ✓ | [`CORE` §9a](ppcp-core.md#9-offline-sessions-and-bundles), [`ENC` §7a](ppcp-encoding.md#7-bundle-container) — same bytes, one parser |
| REQ-OFF-2 | ◐ | `absent_reason: storage_full` is expressible; the *floor* is peer policy (**I14**) |
| REQ-OFF-3 | ✓ | [`CORE` §9b](ppcp-core.md#9-offline-sessions-and-bundles), [`MSG` §9.2a](ppcp-messages.md#92-session_manifest) |
| REQ-OFF-4 | ✓ | [`CORE` §9.1a](ppcp-core.md#91-clock-authority-inverts) |
| REQ-OFF-5 | ✓ | [`CORE` §9.1b](ppcp-core.md#91-clock-authority-inverts) + `evidence_stream_id` — **carriable only since revision 5**, via stream-anchored Captures |
| REQ-OFF-6 | ✓ | [`CORE` §9.1c](ppcp-core.md#91-clock-authority-inverts) |
| REQ-OFF-7 | ✓ | [`CORE` §9.1d](ppcp-core.md#91-clock-authority-inverts) |
| REQ-OFF-7a | ✓ | **I16**, [`CORE` §8.5d](ppcp-core.md#85-reconciliation) |
| REQ-OFF-8 | ✓ | **I15**, [`CORE` §6.5](ppcp-core.md#65-wall-clock) |
| REQ-OFF-9 | ✓ | [`ENC` §6](ppcp-encoding.md#6-bulk-transfer) — chunked, resumable, content-addressed |
| REQ-OFF-10 | ✓ | **I34**, [`CORE` §8.5c](ppcp-core.md#85-reconciliation) — keyed on `Capture.id`, not the digest |
| REQ-OFF-11 | ✓ | **I10**, [`CORE` §5.10d](ppcp-core.md#510-session) |
| REQ-OFF-12 | ✓ | **I9**, [`CORE` §8.5a–b](ppcp-core.md#85-reconciliation) — no merge operation exists to invoke |
| REQ-OFF-13 | ✓ | **I11**; revision 6 separates a `gaps` entry (loss) from an `absent` segment (nothing captured) |
| REQ-OFF-14 | ✓ | **I12** |
| REQ-OFF-15 | — | A measurement task, not a protocol obligation |
| REQ-OFF-16 | ✓ | `SessionLink`, **I25** — provisional at v1 |
| REQ-PRIV-1 | ✓ | [`RV` §7.1](ppcp-rv.md#71-threat-model), [§7.7](ppcp-rv.md#77-what-must-never-cross-an-unauthenticated-channel) |
| REQ-PRIV-2 | ◐ | The protocol makes retention expressible and its absence assertable; the *policy* is the application's ([`CORE` §13](ppcp-core.md#13-privacy-considerations)) |
| REQ-PRIV-3 | ✓ | [`CORE` §13d](ppcp-core.md#13-privacy-considerations) |
| REQ-PRIV-4 | ✓ | **I27** — a Capture anchored to a Candidate, which is what makes a rejected nomination keep its evidence |
| REQ-PRIV-5 | ✓ | [`CORE` §5.12.1a](ppcp-core.md#5121-candidate-evidence) — separate Stream, never muxed |
| REQ-PRIV-6 | ◐ | [`CORE` §13c](ppcp-core.md#13-privacy-considerations) states the count is unbounded by anything the user does; the arithmetic is the application's |
| REQ-PRIV-7 | — | Application retention format |
| REQ-CLIP-1 | ✓ | Every listed item, lens identity now included via `Source.optics` and 5.6d |
| REQ-CLIP-2 | ✓ | [`ENC` §7a](ppcp-encoding.md#7-bundle-container) |
| REQ-CLIP-3 | ✓ | Timing rides in the message stream, which is also the on-disk form |
| REQ-META-1 | ✓ | A `metadata` Stream carried by stream-anchored Captures — **the requirement B11 made unmeetable, and revision 5 fixed** |
| REQ-META-2 | ✓ | `ContextChange.kind: location` and `weather`, labels only ([`CORE` §5.10f](ppcp-core.md#510-session)) |
| REQ-STANDALONE-1 | ✓ | The zero-host regime is a regime, not a degraded mode |
| REQ-STANDALONE-2 | ✓ | [`ENC` §7a](ppcp-encoding.md#7-bundle-container) |
| REQ-STANDALONE-3 | ✓ | A bundle carries declaration, profiles and calibration alongside payload |
| REQ-STANDALONE-4 | ✓ | **Mint** |
| REQ-STANDALONE-5 | ✓ | [`CORE` §5.10](ppcp-core.md#510-session) |
| REQ-STANDALONE-6 | — | Application review mode |
| REQ-HOST-1 | — | Host integration. The host review found the named seam wrong and it is referred to the requirements owner |
| REQ-HOST-2 | ✓ | [`CORE` §9a](ppcp-core.md#9-offline-sessions-and-bundles) — one ingest path by construction |

### Rendezvous

| Requirement | | Where |
|---|---|---|
| REQ-DISC-1 | ✓ | [`RV` §3.5b](ppcp-rv.md#35-who-advertises-and-who-browses) — kept as a SHOULD with the querier-role reasoning; the mechanism is open to either peer |
| REQ-DISC-2 | ✓ | [`RV` §2](ppcp-rv.md#2-rendezvous-paths), [§4](ppcp-rv.md#4-rv-2--the-pairing-code) — the primary path |
| REQ-DISC-3 | ✓ | [`RV` §3.6](ppcp-rv.md#36-multicast-is-not-to-be-relied-on) |
| REQ-DISC-4 | ✓ | [`RV` §6](ppcp-rv.md#6-rv-4--network-join) |
| REQ-DISC-5 | ✓ | [`RV` §2](ppcp-rv.md#2-rendezvous-paths) direct path; [`CORE` §3.2](ppcp-core.md#32-transport-guidance) |
| REQ-DISC-6 | ✓ | [`RV` §8](ppcp-rv.md#8-operational-notes) |
| REQ-AUTH-1 | ✓ | [`RV` §5.2](ppcp-rv.md#52-tls-profile) — no PKI (5.2e). **Note**: forward secrecy is now best-effort by decision |
| REQ-AUTH-2 | ✓ | [`RV` §4](ppcp-rv.md#4-rv-2--the-pairing-code) single scan; [§7.7](ppcp-rv.md#77-what-must-never-cross-an-unauthenticated-channel) |

### Application, capture path, testing and licensing

Out of protocol scope by design, and listed so the exclusion is visible rather than assumed.

| Family | | Reason |
|---|---|---|
| REQ-OPT-1…7 | — | Device optical configuration. Two surface on the wire: `physical` (OPT-5) and calibration fixed for a Stream's lifetime (OPT-6, **I5**) |
| REQ-FPS-1…3 | — | Device capability enumeration; the *result* is a `CaptureProfile` |
| REQ-LIGHT-1, 2 | — | Device measurement; the result is `optical.noise_figure` and `achieved_exposure_ns` |
| REQ-BUF-1…4 | — | Ring-buffer implementation. BUF-1 bounds how far back an orphan request can reach ([`CORE` §8.4b](ppcp-core.md#84-orphan-capture-requests)) |
| REQ-ENC-1…4 | — | Encoder configuration |
| REQ-POSE-1…4 | — | Advisory pose, v2, device-local and never ingested |
| REQ-SETUP-1, 3 | — | Framing validation, application |
| REQ-SETUP-2 | ✓ | `Source.viewpoint { label, confidence, method }` ([`CORE` §5.6e](ppcp-core.md#56-source)) |
| REQ-REPLAY-1…4 | — | Application replay |
| REQ-MARK-1, 2 | ✓ | `Annotation` ([`CORE` §5.18](ppcp-core.md#518-annotation)) — lossless `body`, either direction |
| REQ-MARK-3 | — | Application UI |
| REQ-OBS-1…3 | ◐ | Application output; [`RV` 7.2b](ppcp-rv.md#72-handling-the-pairing-secret) constrains what it may contain, and `RT-9` tests it |
| REQ-OBS-4 | ✓ | Explicitly excluded from the protocol by **I14**, which is the requirement's own position |
| REQ-PORT-1…7, 9, 10, 13, 14 | — | Application portability |
| REQ-PORT-8 | ✓ | **I4** makes it structural: one id, or two ids and a relation. No third option, no silent default |
| REQ-PORT-11 | ✓ | Capability is `CaptureProfile`, never a platform type |
| REQ-PORT-12 | ✓ | [`ENC` §7a](ppcp-encoding.md#7-bundle-container) — one schema, no platform concepts |
| REQ-TEST-1, 2 | ✓ | [`CONF` §2d](ppcp-conformance.md#2-required-test-infrastructure) — the LED rig, and it measures `readout_ns` in the same experiment |
| REQ-TEST-3, 4, 5 | ✓ | [`CONF` §2a–c](ppcp-conformance.md#2-required-test-infrastructure) — fixture format *is* the bundle |
| REQ-TEST-6, 7 | — | Capture-path experiments |
| REQ-LIC-1…6 | — | Licensing and distribution |

---

## 2. What the audit confirms

Worth recording, because it is the larger part of the result.

- **Every MUST in the timebase, exposure, synchronisation, capability, versioning and offline families is met**, and most are met structurally rather than by prose — I1 makes an untimebased timestamp unwriteable, I4 makes a silent zero-offset unexpressible, I27 makes an unanchored Capture unconstructible.
- **REQ-META-1 and REQ-OFF-5 were unmeetable until revision 5.** Both depended on a `continuous` Stream carrying data, and nothing did. That is the strongest evidence this audit was worth running: two MUSTs had been unsatisfiable for six drafts and no review round caught it.
- **Requirements the protocol deliberately refuses are refused consistently.** REQ-CAP-5's "thresholds are host policy" is I14; REQ-OBS-4's own argument that emission thresholds do not belong in the protocol is honoured; REQ-STATE-6's device vocabulary is excluded twice over.

---

## 3. The findings

### G1 — no carriage for user-authored artefacts

**REQ-MARK-1 (MUST)** — *"Markup is a user artefact, not derived data: anchored to shot ID plus frame timestamp, round-trips losslessly."*
**REQ-MARK-2 (MUST)** — *"the protocol must permit host-originated and device-originated payloads in v1 even if only annotations flow back initially."*

MARK-2 is an explicit MUST **on the protocol**, and two things are missing.

**There is no entity for a user artefact.** Every payload in PPCP is a `Capture`, and a Capture realises a Shot, a Candidate, or an interval of a Stream — all of them *observations by a Source*. Markup is authored by a person; it has no Source, no timebase and no capture interval. The model's spine is that Sources observe and Captures realise, and a user artefact is neither.

**There is no host→device content path.** `capture_request` travels host→peer, but the payload still flows peer→host. Every `payload_*` message describes a Capture the sender owns. "Bidirectional for content, not only for commands" has no mechanism at all.

**Two candidate shapes**, and the choice is a design decision rather than a drafting one:

| | Shape | Cost |
|---|---|---|
| **A** | A new subordinate type — `Annotation { id, shot_id, at: Instant, author_peer_id, kind, payload }` — carried by its own message pair, flowing either direction | One entity, two messages. Keeps the observation model clean. |
| **B** | A `Source` of `kind: user`, `physical: false`, whose Stream carries markup as ordinary Captures | No new entity or message, and it reuses the whole announce-and-payload path — but it calls a person a capture source, which strains the model's central distinction |

Shape A is cleaner and costs more; shape B is cheaper and blurs the thing the model is most careful about. **Neither should be chosen without the implementation teams.**

### G2 — no commit acknowledgement, so `confirmed` is unobtainable

**REQ-SESS-3 (MUST)** — *"an independent store with per-shot sync state (local / sent / confirmed)"*
**REQ-SESS-4 (MUST)** — *"Nothing unconfirmed is evicted, regardless of retention policy."*

`Capture.transfer` is `pending | in_flight | present | failed`, and those are the **owner's** view. `payload_ack` acknowledges a chunk *arriving*. `payload_end` travels sender→receiver. **Nothing travels back to say the receiver has durably committed the Capture.**

So the third state of REQ-SESS-3 cannot be reached, and REQ-SESS-4 is satisfiable only by never evicting anything — which is safe and unbounded. At roughly 1 GB per session that is tolerable for a few sessions and not for a year of them.

**The shape is determined**, unlike G1:

> `Capture.transfer` gains **`confirmed`**, and a `capture_committed { capture_id, digest }` event travels **receiver → owner** when the receiver has durably committed the payload. A peer MUST NOT evict a Capture that is not `confirmed`.

One enum value and one message. It is offered rather than applied because it adds a message to a catalogue both teams have approved.

### G3 — navigation anchors have no home in the schema

**REQ-NAV-1 (MUST)** — the device may derive coarse scrub targets, *impact and top of backswing*.
**REQ-NAV-2 (MUST)** — *"These are named distinctly from host phase data in the schema and are never persisted as P1–P8 phases."*

NAV-2 says *in the schema*, and REQ-CLIP-2 makes the on-disk schema the wire schema — so this is a PPCP obligation. Impact is covered: it is the acoustic Candidate's instant, and `Shot.t0`. **Top of backswing has nowhere to go**, and nothing establishes the distinct naming NAV-2 requires.

`ContextChange.kind` is an open registry and would carry it, but no value is defined — so two implementations diverge, which is precisely the class of defect that produced the `Stream.kind` and launch-monitor findings.

> Define `ContextChange.kind: nav_anchor` with a `value` naming the anchor, and state that navigation anchors are never phase data.

### G4 — lens identity is not carried

**REQ-CLIP-1 (MUST)** lists the sidecar contents. Every item is carried — per-frame timestamps, intrinsics, attitude and gravity, exposure and ISO per frame, event times and confidences, the time-of-flight constant, the thermal timeline, clock-sync residuals, the capability triple, calibration state — **except lens identity**.

In practice a lens *is* a Source, since REQ-OPT-5 requires opening a physical device and two lenses are two devices. But nothing says so, `Source.label` is marked informational, and [`CORE` §5.2c](ppcp-core.md#52-peer) forbids inferring anything from `product`. The requirements' own implementation note records that one device offers the same profile on both the wide and the ultra-wide lens — so which one is in use is a real ambiguity that REQ-OPT-6 makes calibration-affecting.

> State that a distinct lens is a distinct `Source`, and that `Calibration` of `kind: intrinsics` identifies the lens it was solved for.

### G5 — a viewpoint classification is "reported" with nowhere to report it

**REQ-SETUP-2 (SHOULD)** — *"The device classifies its own viewpoint (\"DTL, right-handed\") and reports it, rather than asking the user to configure it."*

Reporting it means putting it on the wire. There is no field. `Calibration.kind: pose` is about geometry rather than classification, and a viewpoint label is a *conclusion* — which the model normally refuses to carry.

The honest resolution is that a self-classified viewpoint is a **declared observation with a confidence**, not a fact:

> `ContextChange.kind: viewpoint`, or a `Source`-level declaration carrying the label and a confidence, so a consumer can disagree with it.

### G6 — location and weather have no home

**REQ-META-2 (SHOULD)** — *"Time, location, and for outdoor sessions weather. Capture from v1 even if nothing consumes it yet."*

Time is covered by `Session.epoch`. Location and weather are not. `ContextChange` would carry them under an open registry, but no value is defined.

> Define `ContextChange.kind: location` and `kind: weather`, noting that both are labels and neither is used in any computation — the same footing `Session.epoch` already has under **I15**.

---

## 4. What was done, and what to disagree with

All six are closed in `PPCP-CORE` revision 7. **None has been reviewed**, and two of them took a design decision that the implementation teams should weigh.

**G1 — `Annotation` as a distinct type, not a `Source`.** The alternative was a `Source` of `kind: user`, which needs no new entity or message and reuses the whole announce-and-payload path. It was rejected because it puts a person in the position the model reserves for instruments. Every payload elsewhere in PPCP is a Capture; a Capture realises an observation; a `Source` has a clock, a calibration and an owning peer. A human being has none of those. **The cost of a separate type is one entity and one message. The cost of the alternative is the model's spine.** If the teams disagree, this is the place.

**G2 — a receiver-asserted `capture_committed`.** The shape was already determined and the only question was whether to add a message to an approved catalogue. It is one event on the control channel and one enum value, and it makes a stated obligation satisfiable that was not: a peer required to track *local / sent / confirmed* could not reach the third state, so "evict nothing unconfirmed" meant evicting nothing ever.

**G3–G6 are registry values and two optional fields**, of exactly the kind this specification has defined four times before. `Source.optics` and `Source.viewpoint` are new optional fields; `nav_anchor`, `location`, `weather` and `handedness` are values in registries that were already open.

Two things worth noting about the shape of the result:

- **`Annotation` answered two findings, not one.** A navigation anchor is a derived marker anchored to a shot and a frame instant — structurally identical to markup, differing only in provenance. `provenance: user | device_advisory` is the discriminator, and it is the same move the requirements make for advisory pose.
- **`Source.viewpoint` carries a confidence and a method** because a self-classified viewpoint is a *conclusion*, and this model carries measurements. A consumer may disagree with it.
