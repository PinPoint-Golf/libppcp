# CR-02 review — PinPointStudio, on device status, actuator control and statistics

| | |
|---|---|
| **Reviewing** | [`CR-02-device-status-and-control.md`](../Projects/libppcp/docs/changerequests/CR-02-device-status-and-control.md), raised 26 August 2026, against `PPCP-CORE`/`PPCP-MSG` 1.0 rev 9 as amended through E57 |
| **Reviewed by** | PinPointStudio, 26 August 2026 |
| **Position** | **Send it. No objection, nothing blocking.** All three parts are things this application can consume without restructuring — 3a and 3c are additive fields into a pipeline that already exists end to end; 3b needs a genuinely new primitive, which the request correctly identifies. |
| **Findings** | Three, all non-blocking. Two tighten claims the document already makes; one flags that 3c's ring-buffer half doesn't have the same shape as its session/device half, which matters for how the protocol team answers it. |

---

## 1. Can PinPointStudio actually support this? Yes — with one caveat worth stating plainly.

Before critiquing the request, the question it was sent here to answer: is any of this buildable on our side, or would granting it hand back a feature the app can't absorb?

**3a and 3c land in an existing pipe.** `PpcpHostPeer`/`PpcpLiveSession` already parse `heartbeat_ack` into a health struct and already accumulate a `Stats` block (heartbeat acks, link losses, relations published), and `PpcpHostService` already turns per-peer state into the `QVariantList` rows `PhonesPanel.qml` and the resource monitor read. New fields on `Peer`/`Session`/`Source`, or a new live-pushed status message, are additive rows in a table that already flows from wire to UI. Nothing about the architecture needs to change to carry them.

**3b needs a new primitive, and the codebase has nowhere to put one yet** — no outbound "command a peer" path exists anywhere in the current stack; everything today is declare/arm/heartbeat/payload, all either observational or capture-shaped. That is not a reason to decline 3b — `HostEngineConfig`'s callback-slot design (one slot per concern: `health`, `healthReport`, `syncTimebase`) accommodates a new slot the same way it accommodated the others — but it means 3b is the one part of this request where "can we support it" is answered by future work, not existing plumbing, exactly as §6's own severity ranking of the three asks implies.

**The caveat:** `heartbeat_ack`'s thermal/battery/storage fields — the data §2 of this request correctly says already crosses the wire — are parsed on our side and then go nowhere. `PpcpHostService::phones()` hardcodes `batteryPct: -1` rather than reading the health struct sitting one call away, and `PhonesPanel.qml` shows no battery, thermal or storage fact at all. This doesn't weaken the request — 3a and 3c ask for genuinely new wire content, not a re-ask of what §2 disclaims — but it means part of what would satisfy 3a's "operator sees device status live" is a display backlog item independent of anything the protocol team rules on. Worth knowing before this reads as blocked on Cupertino^H^H^H Kraków when part of it is blocked on us.

---

## 2. Findings

### R-1 — §4a understates what already reaches a non-host peer, and §2 leaves out the message that does it

§4a's second bullet: *"There is no per-`Source` status … and no path for a non-host peer, or an `observer`, to learn another peer's status at all."*

That is true of `heartbeat`/`heartbeat_ack` specifically — direction is fixed `host → peer` / `peer → host`, exactly as stated. But it is not true of the message set as a whole, and the request's own §2 already cites the message that says so without drawing the conclusion: `readiness` ([`PPCP-MSG` §5.2](../Projects/libppcp/docs/specification/ppcp-messages.md#52-arm--disarm--readiness)) is direction **capture peer → any** ([`PPCP-MSG` §5](../Projects/libppcp/docs/specification/ppcp-messages.md#5-streams-and-capture-control) table) — not host-only — and its `blocked_reason` already enumerates `storage_full`, `thermal_limit`, `permission_denied`, `no_source`. That vocabulary overlaps 3a's `unavailable` more than the request credits.

There is a second existing message in the same shape that §2 doesn't mention at all: `interruption` ([`PPCP-MSG` §5.3](../Projects/libppcp/docs/specification/ppcp-messages.md#53-interruption)) — an Event, **capture peer → any**, open-registry `kind`, exactly the "a device pushes its own status to whoever's listening" shape 3a is asking for, just scoped today to platform interruptions that cost capture rather than to availability generally.

None of this closes 3a — both are Live-profile, Stream-scoped, and only emitted while armed (`readiness`) or already capturing (`interruption`), so a phone sitting connected-but-unarmed still tells nobody anything, which is the actual gap. But stating the gap as "no path … at all" invites the protocol team's first reflex to be "broaden `blocked_reason` and start `readiness` earlier" — a genuine option that under-delivers (still nothing pre-arm, still nothing per-Source outside a capture Stream, still no `idle`-vs-`capture` distinction) but *looks* like it answers the request as currently worded. Naming both messages in §2 and narrowing §4a's claim to the actual residual gap forecloses that answer before it's offered rather than after.

**Fix:** add `interruption` to §2's list of what already exists; narrow §4a's second bullet from "no path … at all" to the specific residual — nothing before `arm`, nothing per-Source outside a Live-profile Stream, no `idle`/`capture` distinction.

### R-2 — 4b's "empty profile set" describes a Source the model already forbids, not merely one that's useless

§4b: *"Declaring a light as a `Source` with an empty profile set satisfies no invariant that gives a host anything to enumerate or command."*

`Source.profiles` is cardinality `1..n` ([`PPCP-CORE` §5.6](../Projects/libppcp/docs/specification/ppcp-core.md#56-source)) — at least one `CaptureProfile` is mandatory, not optional. A Source with an *empty* profile set isn't a legal declaration to begin with, so the sentence describes something the spec already refuses rather than something it silently tolerates but gives no meaning to.

The underlying point survives — every field of the one profile you'd be forced to supply (`format`, `rate`, `optical`, `geometry`, `timing`, `intrinsics`, all camera-shaped) is meaningless for a torch — it's the framing that overstates. **Fix:** "a `Source` whose one mandatory `CaptureProfile` has every field meaningless for a torch" reads as the sharper, still-true version, and doesn't hand the protocol team an easy "well, just make `profiles` optional for non-capture kinds" non-answer that dodges the actual point about `arm`/`CaptureProfile` being capture-shaped.

### R-3 — 3c's ring-buffer half isn't the same shape as its session/device half, and §4c's closing line reads as though it is

§4c ends: *"3c does not obviously need a new entity, only new fields — which distinguishes it from 3b."* True for shots-taken, session duration, uptime, drop counts — all either monotonic counters or settled-at-close values, the same shape as fields `Session` and `Peer` already carry.

**The ring-buffer margin is not that shape**, and the spec's own model says so. `video` Streams are `shot_windowed`, and [`PPCP-CORE` §5.11](../Projects/libppcp/docs/specification/ppcp-core.md#511-stream)'s own note is explicit: *"the ring buffer discards everything else; the capture stream is never materialised continuously."* The buffer 3c wants visibility into sits **behind** the Stream abstraction, not inside it — it isn't a `continuous` Stream's `gaps`/`absent`-segment accounting either, because that machinery exists only for Streams declared `continuity: continuous`, and the ring buffer backs a `shot_windowed` one. There is, today, no entity in §5 that has this thing as a sub-part at all.

More importantly, it doesn't behave like a field. `Session.shots` and a device's uptime are settle-and-read; ring-buffer margin changes every frame a phone sits idle-but-armed, the same rhythm as `Readiness.settled` — which is why `Readiness` is a pushed, re-emitted-on-change *Event*, not a declared field on `Stream`. The ring-buffer third of 3c reads as needing the same treatment: a live, re-emitted measurement in `Readiness`'s family, not an additive field on `Session`/`Peer` the way shot counts and uptime are.

This doesn't change what 3c is asking for. It changes what "granted" would have to mean: if the protocol team reads §4c's closing sentence as licence to answer all of 3c with additive `Session`/`Peer` fields, the session and device counters land correctly and the ring-buffer number gets bolted on somewhere it doesn't fit — a live quantity crammed into a declare-once entity, which is exactly the kind of shape mismatch [5.10e](../Projects/libppcp/docs/specification/ppcp-core.md#510-session)'s reasoning about mandatory-but-meaningless fields already warns against elsewhere in this same document.

**Fix:** split §4c's closing line — say explicitly that the session/device aggregates are additive fields, and the ring-buffer margin is closer in kind to `Readiness` than to a counter, so the two are free to get different answers.

---

## 3. What's already right and doesn't need touching

- **§5's discipline (no wire format, no message names) is correct and should stay.** R-1's fix is "cite one more existing message and narrow one claim," not "here is the field list" — the request's own §6 reasoning about why a sketch is worse than no sketch applies equally to a review suggesting one.
- **§5's three ⚠ constraints (5.15a tension, 3b as a new trust class, extensibility) are the right three, and are stated as open questions rather than pre-answered** — this is the same posture that made CR-01's §6 hold up under five review passes.
- **§8's "if the answer is no" framing, especially asking the protocol team to distinguish "actuator control doesn't belong in PPCP" from "this case wasn't made,"** is worth keeping verbatim — it's the question CR-01 §3 forced on the pairing side and it's the right one to force here too, since a `PPCP-CORE` §1.3-style scope decline and a decline-for-now have different consequences for whether a CR-03 in this direction is worth raising.
- **The severity ranking in §6** ("3c is additive, 3b is new trust, 3a fights an existing clause") matches what this review independently found from the app side — 3a and 3c slot into existing plumbing, 3b doesn't. Nothing here argues that ranking should change.

---

## 4. Summary

| | Finding | Ask |
|---|---|---|
| **R-1** | §4a's "no path … at all" overstates the gap; §2 omits `interruption`, which is the closest existing precedent for a device pushing its own status | Add `interruption` to §2; narrow §4a to the actual residual (nothing pre-arm, nothing per-Source, no idle/capture distinction) |
| **R-2** | "Empty profile set" describes a Source the model's own cardinality (`1..n`) already forbids, not a legal-but-useless one | Reword to "a mandatory profile whose fields are all meaningless for a torch" |
| **R-3** | 3c's ring-buffer margin isn't fields-on-an-entity the way session/device counters are — it's a live, changing measurement with no current home, closer to `Readiness` than to `Session.shots` | Split §4c's closing line so the two halves of 3c are free to get different answers |

All three are wording and framing, in a document that says up front it isn't proposing wire format. **None of them is a reason to hold this before the protocol team sees it** — if anything, R-1 and R-3 exist so their first response engages the real gap instead of a narrower reading these paragraphs currently leave available.
