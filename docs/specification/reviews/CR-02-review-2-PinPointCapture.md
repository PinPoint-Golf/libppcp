# Review of CR-02's specified normative text (round 2, of the three-round cap) — from PinPointCapture

Reviewed against `libppcp` main at commit `28158b2`: `PPCP-CORE` §2.2, §5.2, §5.10h, §5.15, §5.19–5.21, §10.3, §11 (I39), §12 (12d), §13 (13e), and errata E58–E62; `PPCP-MSG` §5.5–5.6, §12, §11 message index; `PPCP-CONF` CT-I39. Grounded against PinPointCapture's (PPC's) actual codebase, not against the disposition's summary of it — per [`verify-before-restating-a-finding`], the live clauses were re-read rather than quoted from CR-02's own request text or from my first review.

**Bottom line: buildable, and nothing here is a scope conflict with how PPC actually works. One blocking finding: `actuator_command_ack.state` and `actuator_state.state` have a cardinality hole that lets a conformant response say nothing at all, which two independently-built implementations could resolve differently. Everything else is non-blocking — either the disposition's own reasoning checks out against real PPC code (worth recording so round 3 doesn't have to re-derive it), or a small polish item.**

---

## Findings

### F1 — BLOCKING. `actuator_command_ack.state` and `actuator_state.state` can be sent empty, contradicting their own prose

`PPCP-MSG` §12.1:

```
actuator_command_ack  { actuator_id: Id,
                         verdict: applied | refused,
                         reason: Kind (refused: 1),
                         state: { on: bool (optional), level: float (optional) } (applied: 1) }
```

`state` itself is mandatory when `verdict: applied` — but `on` and `level`, *inside* `state`, are each independently marked `(optional)`, not `(present iff Actuator.control == "on_off")` / `(present iff == "level")` the way the **request**'s own `on`/`level` pair is written two lines above it. The same pattern repeats verbatim in `actuator_state` (§12.2): `state: { on: bool (optional), level: float (optional) }`, with no clause anywhere binding it to the named Actuator's declared `control`.

I39 does not close this. It reads: *"An `actuator_command` carries `on` if and only if the named Actuator's `control` is `on_off`, and `level` if and only if it is `level`."* That sentence names `actuator_command` only. Nothing — not I39, not 12.1c, not 12.2a/b — makes the equivalent claim about `actuator_command_ack.state` or `actuator_state.state`.

The result: a peer can send `actuator_command_ack { verdict: applied, state: {} }` — `state` present as the schema requires, both its fields absent as the schema permits — and nothing in the document it violates. That satisfies 12.1c's cardinality table while contradicting 12.1c's own prose one line above it: *"`actuator_command_ack.state` reports what the Actuator is **actually** doing after the command is applied."* An empty object reports nothing.

This is not hypothetical for PPC specifically: PPC is the Actuator **owner** on both message types (`actuator_command_ack` and `actuator_state` are both `owner → *`, and PPC's own profile row is *"owns a torch"*, `PPCP-CORE` §2.2.3). PPC is the peer that will be writing this response, and the schema as written does not tell it whether omitting `on` on a declared `on_off` torch is legal. An implementation built strictly to I39's letter (which binds only the request) would reasonably read the ack's independent-optional fields as "send whichever you happen to have" — and a host built by a different team reading the same document could reasonably assume the ack always echoes the control shape, the way the request does, and crash or silently mis-render on the first empty one it receives. That is exactly the two-implementations-can't-interoperate case this round is meant to catch.

**Proposed fix**, confirmable by re-reading the clause (no round 4 needed): extend I39 to cover the response and event forms, or add a sibling invariant with identical wording scoped to `actuator_command_ack.state` and `actuator_state.state`. Concretely, change the cardinality annotations from `(optional)` to `(present iff the named Actuator's control == "on_off")` / `(present iff == "level")` on both messages, matching how the request already reads. This is a text-only fix — no new field, no renumbering — and is exactly the kind of thing a reviewer should be able to confirm fixed just by reading the corrected clause.

### F2 — Non-blocking. `DeviceStatus.reason`'s example list carries a value the entity's own preconditions rule out

`PPCP-CORE` §5.20 gives `reason`'s open registry as *"`in_use`, `permission_denied`, `disconnected`, `thermal_limit`, `storage_full`, `no_source`, …"* — `no_source` is carried over verbatim from `Readiness.blocked_reason`'s vocabulary (§5.15).

But `device_status` is scoped to an already-declared Source: 5.5c requires *"the originating peer is the Source's owner… a peer MUST NOT emit `device_status` for a Source it does not own,"* and 5.19a's sibling rule for Actuators (a peer declares the entity before it can report on it) applies identically here — `Peer.sources` isn't retracted mid-session, and `declare`/`declare_ack` isn't repeated. A `device_status` event, by construction, always names a Source that exists and is declared. `no_source` — "there is no Source to speak of" — is the one reason in the list that cannot occur for an event that cannot be emitted unless the Source it names already does. It made sense on `Readiness`, which is reported at arm time and can legitimately have nothing to arm; it does not carry over to an entity that is inherently per-Source.

It's an open registry either way, so no implementation is blocked by this — PPC would simply never emit `no_source` here, which costs nothing. It's worth a line-item fix only because the errata table (E59) and the field notes are what an implementer skims, and a reason value with no reachable case is the kind of thing that gets cargo-culted into a switch statement's default arm. **Proposed fix:** drop `no_source` from §5.20's example list.

---

## Direct verdicts on the disposition's own §6 questions

### Q1 — Is `DeviceStatus` actually distinguishable from `Readiness`, or would PPC compute both from the same check?

**Distinguishable, confirmed by code — with one deliberate, and correct, overlap.**

PPC's `Readiness.settled` is not a platform-health read at all: `AppModel.reportReadiness()` (`Sources/App/AppModel.swift`) is driven by a poll loop that watches `self.device.ringStats.framesAppended` actually rising for two consecutive ticks before declaring `settled` — i.e. "is the camera really delivering frames," a signal `DeviceStatus.available` would never touch. `DeviceStatus.available` would instead need genuinely new computation PPC doesn't do today for any wire purpose: `PermissionsService.authorizationStatus(for: .video)` (`Sources/Platform/PermissionsService.swift`) for `permission_denied`, and AVFoundation's own in-use/disconnect notifications (`AVCaptureDeviceWasDisconnected`, `isInUseByAnotherApplication` polling — neither currently wired to anything) for `in_use`/`disconnected`. None of that exists in `Readiness`'s path.

The one place the two *do* read the same platform check is `thermal_limit`/`storage_full`: `DevicePeer.init`'s `health` closure (`Packages/Core/Sources/CaptureCore/Ppcp/DevicePeer.swift:452-469`) sets `Readiness.blocked = thermal_limit` from `DeviceHealthService.current().thermal`, and the identical `DeviceHealthService.current()` call already also feeds `heartbeat_ack.thermal` via the neighbouring `health_report` closure. A future `DeviceStatus` reason of `thermal_limit` would read the same `DeviceHealthService.current()`. That is not the conflation 5.20b warns against — it's two different questions (*can this be armed at all* vs. *would the next shot settle*) legitimately sharing one cause for one input each, at different scopes and different emission triggers (`device_status` is per-Source and unprompted by arm state; `readiness` only exists once a Stream is armed) — but it's worth naming explicitly so nobody reads "distinguishable" as "computed independently" when they build it: for the thermal/storage causes specifically, expect one platform read feeding three different wire fields (`heartbeat_ack.thermal`, `readiness.blocked_reason`, `device_status.reason`), which is by design, not a bug.

### Q2 — Is the host-only scoping of `actuator_command` (12a) right, or does PPC have a real case for commanding a non-host peer's actuator?

**Right, and more strongly than the disposition's own justification states.** PPC never instantiates a `DevicePeer` with `role: .host` anywhere in the app — grepping the whole codebase for `role: .host` / `Role.host` turns up only the enum case's *definition* (`DevicePeer.swift:524`, `DiscoveryAdvertisement.swift:40`), never a call site. PPC's own worked-example profile row is *"owns a torch"*; PinPointStudio's is *"commands one"* (`PPCP-CORE` §2.2.3). There is no code path — hostless or otherwise — in which PPC originates a session-control message toward another peer's hardware; it is structurally always the commanded side. Widening 12a to a symmetric peer-to-peer primitive would answer a case PPC does not have and currently has no design for (a genuinely hostless two-phone rig would leave each phone managing its own light locally, not commanding the other's over the wire). No objection to the narrower grant.

### Q3 — Is §2c's "already served" table actually true against how PPC retains and walks received capture traffic?

**True for PPC, with one implementation-shape note worth flagging so the eventual build doesn't reach for the wrong local structure.** PPC's durable record of a session is the bundle itself — `SessionStore.swift`'s own header states it directly: *"the session store on the device **is** the bundle… there is no second on-disk schema."* Every `capture_announce`/`capture_update` PPC ever originates is written through the same `ppcp_peer_drain` → `ppcp_bundle_writer_append_frames` path that a live peer would have sent, so walking `Capture.transfer` and `achieved_summary` after the fact is genuinely possible from that durable store — the disposition's claim holds.

The note: PPC also has two **ephemeral** structures that look like they'd answer this and don't. The app-level `TransferQueue` (`Packages/Core/Sources/CaptureCore/Session.swift:168`) tracks only `pendingShotIDs` — an id leaves the list once transferred, so it cannot answer "how many committed this session," only "how many are left." `PayloadTransferQueue` (`Live/TransferQueue.swift`) explicitly evicts finished entries every pump (`queue.removeAll { $0.finished || $0.alreadyPresent }`, line 159) for the same reason — it exists to bound memory for in-flight transfer, not to be a ledger. Neither is a defect; both are correctly scoped to what they're for. But an implementer reaching for "session/device stats derivable from what we already have" should be pointed at the bundle/`SessionStore`, not at either queue, or they'll conclude the data isn't retained when it is, just not where they first looked.

### Q4 — Is `BufferMargin`'s shape (single most recent discard, no histogram) too little, now that PPC's own `RingStats` is on record as richer?

**Adequate as specified, with one direct gap worth a deliberate call rather than a silent pass.** `RingBufferRecorder.swift`'s `RingStats` keeps up to 8 timestamped, sized gaps (`largestGaps`, `Gap { sinceFirstNs, deltaNs }`, lines 114-178) — but that structure is itself built by *repeatedly observing* frame arrivals over the buffer's life (`observeArrival(atNs:)`), not read out of one message. `buffer_status`'s own re-emission discipline (5.6b: *"re-emits whenever `retained_from` moves discontinuously… MAY additionally re-emit at its own cadence"*) gives a receiver exactly the same raw material — a live peer that wants an 8-bucket histogram can accumulate one client-side from repeated `buffer_status` events the same way `RingStats` accumulates one from repeated frame arrivals. On that axis, the smaller wire shape is the right call: a histogram is a receiver-side aggregation, not a fact the wire needs to carry pre-summarised.

The gap that **isn't** reconstructible this way: `RingStats` breaks drops out **by cause** — `framesDroppedEncoderBusy`, `framesDroppedNotRetaining`, `fragmentsDroppedWriteFailed`, `fragmentsDroppedEmpty` — and `BufferMargin` carries none of that. `discarded_since_open` is one undifferentiated `uint64`; `last_discard` carries `since`/`duration` but no cause. No amount of re-emission recovers cause, because cause never crosses the wire at all. Whether that matters depends on what the grant is for: if `BufferMargin` is meant only to answer "how much margin do I have" (E60's own framing), the omission is fine. If it's meant to let a host distinguish "the buffer is shrinking because storage is full" from "because the encoder can't keep up" without falling back to `heartbeat_ack.storage_free_bytes` and a guess, it's a real gap — and unlike the histogram, it's not one a receiver can build for itself from what's already on the wire.

This isn't raised as blocking — CR-02 §3c's own ask named "how large and when," not "why," so the grant matches what was actually asked. It's flagged because the disposition explicitly asked for a verdict rather than silent acceptance: **verdict is that the histogram omission is fine as designed, but the cause omission is a real and separate gap from the histogram one, worth a one-field addition (`last_discard.cause: Kind`, open registry, additive) if a future round has budget for it — not worth spending this round's cap on.**

### Q5 — Is `actuator_command_ack.state` echoing the *achieved* value buildable for a torch on iOS?

**Buildable, with a nuance the clause's own illustrative example doesn't quite match.** `AVCaptureDevice.torchLevel` and `.isTorchActive` are both KVO-observable properties that reflect what the hardware is *actually* doing, independent of what was last requested — reading them back after a successful `setTorchModeOn(level:)` call gives PPC a genuine achieved-value readback, not an echo of the request. That satisfies 12.1c as written.

The nuance: 12.1c's own example — *"a torch driver rounding to a discrete step"* — describes a silent clamp inside a successful call. iOS's actual API doesn't do that: `setTorchModeOn(level:)` **throws** `AVError` for a level outside `(0, maxAvailableTorchLevel]` rather than silently rounding it, so an out-of-range request on iOS becomes `verdict: refused` (`reason: unsupported` or `thermal_limit` if `maxAvailableTorchLevel` has dropped under thermal load), not an `applied` ack with a clamped `state`. The genuine achieved-differs-from-requested case on iOS arrives **after** the ack, asynchronously — a thermal cutoff mid-session — and the spec already routes that correctly: `12.2a` sends it as `actuator_state`, not a revised ack. So the mechanism the clause needs exists on iOS; it's just reached by a different code path than the one 12.1c's own prose illustrates. No fix needed — noted so an implementer doesn't go looking for a clamp-inside-the-ack case that iOS's torch API doesn't produce.

---

## What's already right and doesn't need touching

- **§5.2's `actuators: [Actuator]` on `Peer`, cardinality `0..n`.** Matches PPC's own `Source`-declaration shape exactly (one `SourcePlan` per physical lens plus mic plus IMU, `Declaration.swift`), so `Actuator` slots into the same declaration machinery a `torch` Source-that-isn't-a-Source would otherwise have had to fight. 5.19b's "Source and Actuator are disjoint" is correctly reflected — nothing in PPC's `SourcePlan`/declaration code would need to grow a fake `CaptureProfile` for a light.
- **12d's "an `actuator_command` is session control."** Right, and it costs PPC nothing extra: `DevicePeer`'s existing `ingest_policy` gate (`DevicePeer.swift:438-451`) already refuses unauthenticated counterparts before any message reaches application code, so a command arriving from an unauthenticated peer never gets far enough to check 12a's host-only rule in the first place.
- **`Peer.actuators` participating fully at `0..n` (5.19c), same wording as `Source`.** No special-casing needed for the many PPC deployments where the active camera has no rear-facing torch reachable (front-camera setups) — an empty `actuators` list is already a legal, ordinary declaration by construction, not a case PPC has to test for separately from "no Sources."
- **13e's carve-out from 13d.** Correctly scoped to `DeviceStatus`/`BufferMargin` and silent on `Session.opened_at` in its closing "MUST NOT relay" sentence — and that's the right asymmetry, not an oversight: `opened_at` is an ordinary immutable `Session` field that legitimately travels in a bundle export (Offline profile) the way `Session.id` already does, while `DeviceStatus`/`BufferMargin` are live-only measurements with no bundle representation to carry them past the session that produced them.
- **I39's cardinality on the request side.** Exactly matches what PPC's `Actuator.control` registry needs to enforce on receipt — reject a `level` command against a declared `on_off` torch as `malformed` (12.1a) is a one-line switch on the already-declared `control` string, no new state to track.

---

## Summary

| # | Finding | Severity | Confirmable by re-read, or needs round 4? |
|---|---|---|---|
| F1 | `actuator_command_ack.state` / `actuator_state.state` can be sent with `on` and `level` both absent — I39 binds only the request, not the response or event, contradicting 12.1c's own prose | **Blocking** | Confirmable by re-read — text-only fix (extend I39 or add a sibling clause; change `(optional)` to `(present iff control == …)` on both messages) |
| F2 | `DeviceStatus.reason`'s example list includes `no_source`, a value that cannot occur given 5.5c/5.19a's precondition that the event always names an already-declared Source | Non-blocking (polish) | Confirmable by re-read — drop the value from the list |
| Q1 | `DeviceStatus` vs `Readiness` distinguishability | No finding — confirmed distinguishable in PPC's real code; thermal/storage causes intentionally share one platform read across three wire fields | — |
| Q2 | Host-only scoping of `actuator_command` | No finding — PPC never runs `role: .host` anywhere; the grant matches PPC's actual deployment exactly | — |
| Q3 | §2c "already served" table vs PPC's real retention | No finding — true against PPC's durable `SessionStore`/bundle; noted that PPC's two in-flight queues are the wrong place to look for it | — |
| Q4 | `BufferMargin` shape vs `RingStats` | No finding — histogram omission is fine (receiver-reconstructible from re-emission); discard-cause omission is real but was never asked for and isn't worth this round's cap | — |
| Q5 | `actuator_command_ack.state` buildability on iOS | No finding — buildable via KVO-observable `torchLevel`/`isTorchActive`; 12.1c's own clamp example doesn't occur on iOS but the mechanism it needs (`actuator_state` for async changes) already exists | — |

Two findings, one blocking. Everything else in this round is either a confirmation that the disposition's reasoning holds against PPC's real code (recorded so round 3 doesn't have to re-derive it) or an explicitly-requested direct verdict with no action attached.
