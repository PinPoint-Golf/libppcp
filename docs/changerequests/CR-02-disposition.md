# CR-02 — disposition

| | |
|---|---|
| **Request** | [CR-02 — device status, onboard actuator control, and statistics visibility](CR-02-device-status-and-control.md), product requirement (PinPointCapture / PinPointStudio), 26 August 2026 |
| **Ruling** | **Granted in part, on all three sub-asks.** Each is narrower than the request's own illustration; §2 says exactly how and why for each. |
| **Specified in** | [`PPCP-CORE`](../specification/ppcp-core.md) — [§2.2](../specification/ppcp-core.md#22-conformance-profiles) (Actuate profile), [§5.19–5.21](../specification/ppcp-core.md#519-actuator) (`Actuator`, `DeviceStatus`, `BufferMargin`), [§5.10h](../specification/ppcp-core.md#510-session) (`Session.opened_at`), [12d](../specification/ppcp-core.md#12-security-considerations), [13e](../specification/ppcp-core.md#13-privacy-considerations), [I39](../specification/ppcp-core.md#11-invariants) — and [`PPCP-MSG`](../specification/ppcp-messages.md) [§5.5–5.6](../specification/ppcp-messages.md#55-device_status) (`device_status`, `buffer_status`), [§12](../specification/ppcp-messages.md#12-actuator-control) (`actuator_command`, `actuator_command_ack`, `actuator_state`). Errata **E58–E62** |
| **Decided by** | Protocol owner, 26 August 2026 |
| **Status** | **Closed. No open specification items.** Two review rounds run (of three allowed) — [round 1](../specification/reviews/CR-02-review-2-PinPointCapture.md) found the same defect independently in both reviews ([PPS](../specification/reviews/CR-02-review-2-PinPointStudio.md)), fixed as errata **E63–E64**, [response](CR-02-review-response-2.md); [round 2](../specification/reviews/CR-02-review-3-PinPointCapture.md) confirmed the fix cleanly on both sides ([PPS](../specification/reviews/CR-02-review-3-PinPointStudio.md)), [closure](CR-02-review-response-3.md). Not implemented in either application |

---

## 1. The ruling

**Granted in part on 3a, 3b and 3c alike** — not because any of the three was refused, but because each, read literally, asked for something either wider than 5.15a and 13d permit, or wider than the wire actually needs to carry. What was granted is the part of each ask that is a genuine protocol gap; what was declined is stated as declined, not silently narrowed.

| | Asked | Granted | Declined, and why |
|---|---|---|---|
| **3a** | A four-value `unavailable`/`ready`/`capture`/`idle` status, live, per device | `DeviceStatus` / `device_status`: per-Source `available` (bool) + `reason` (open registry) + `since` — `Readiness`'s own shape | The `capture`/`idle` half. It is a device state name (5.15a forbids it) **and** already derivable by any peer already watching `arm`/`stream_open`/`capture_announce` for that Source (§4a of the request, and this disposition's §2 below) |
| **3b** | A host commanding a phone's torch and, in general, an actuator on any peer | `Actuator` declaration, `actuator_command` / `actuator_command_ack` / `actuator_state` — **host-only origination** | A general peer-to-peer actuation primitive. The request's own justification (E53's confirmed deployment: operator at host) only needs host → device, and narrower is less to secure later |
| **3c** | Session/device aggregates (shots, captures committed, uptime, drop totals, link quality) and ring-buffer margin, all live | `Session.opened_at`, and `BufferMargin` / `buffer_status` for the ring buffer | Every other session/device aggregate named. Each is already fully derivable from data the wire already carries — this disposition's §2c is explicit about which existing field answers each one |

**The pattern across all three declines is the same one**, and it is worth naming once rather than three times: **this specification adds a field only where nothing already on the wire answers the question, and states plainly where something already does rather than adding a second way to say it.** That is not new discipline invented for CR-02 — it is 5.7d and I14's reasoning (no redundant, policy-shaped fields) applied to three fresh cases, and CR-02 §5's own "extensibility is load-bearing" and "13d is the one clause every part has to clear" constraints are answered the same way: narrower grants have less surface for either problem to reach.

---

## 2. Each sub-ask, and what actually happened to it

### 2a — Device status

CR-02 §4a made two arguments: 5.15a forbids the vocabulary as stated, and `heartbeat`/`heartbeat_ack` is peer-scoped and host-initiated so it cannot reach per-Source status at all. Both are accepted as stated.

**What is granted** answers 5.15a on its own terms rather than around it: `DeviceStatus` is a **measurement** — can this Source be opened or armed right now, and if not, why (open registry) and since when — pushed by the Source's own owner on every transition, to any peer, not solicited by the host. It is `Readiness`'s partner: `Readiness` asks whether the *next shot* would settle once armed; `DeviceStatus` asks whether the Source can be armed *at all*, and answers it **before** arm, which is exactly the residual gap PinPointStudio's review named once `readiness` and `interruption` were subtracted from the claim.

**What is declined** is the `capture`/`idle` distinction for an already-armed, already-open Source. Two independent reasons converge: it is a device state name in the sense 5.15a means, and it is not a gap — any peer receiving `stream_open`, `arm` and `capture_announce` for that Source (all `owner → any` or host-directed and already flowing to every party who would plausibly ask) can already tell whether it is capturing. Adding a field that restates observable message traffic is the redundant-field pattern this specification declines elsewhere (5.7d).

### 2b — Actuator control

CR-02 §4b found no existing shape at all — correctly. The grant is a **new profile**, `Actuate`, requiring only Core, conferring `Actuator` declaration and a three-message exchange: a request the host sends, an acknowledgement carrying the actuator's *actual* resulting state (not an echo), and an unsolicited event for state changes the host did not cause (a thermal cutoff, a local physical toggle).

Three of the request's own open questions (§3b) are answered directly:

1. **Enumeration may be empty.** `Peer.actuators` is `0..n`; a peer with no Actuators participates fully, stated identically to how a Source-less peer already does.
2. **On/off vs. level is per-Actuator, not per-protocol.** `Actuator.control` is an open registry read by both ends; `actuator_command` carries exactly the field the declared `control` calls for (I39), so the protocol does not force every actuator into one shape.
3. **Extensibility.** `Actuator.kind` and `Actuator.control` are both open registries, on the same terms as `Source.kind` — a future connected device's own indicator declares a new `kind` with no further change request, which is exactly what CR-02 §5's "extensibility is load-bearing" asked for.

**What is narrower than asked:** origination is **host-only** (`actuator_command`, `host → owner`), not the fully symmetric "any peer commands any peer" CR-02 §3b floated as the general shape. The request's own justification — the operator stands at the host — only needs this direction, and CR-02 §5's own ⚠ flagged that this is a new trust class for the protocol; granting the narrower shape now costs nothing the wider one would have bought and leaves less to secure. Widening it later, if a real second case shows up, is an additive change (loosening a direction constraint), not a breaking one.

**12d makes the trust question CR-02 §5 asked explicit**, rather than leaving 12b to be read as covering it by implication: an `actuator_command` is session control, full stop, on the same terms as a capture request or a declaration.

### 2c — Statistics

This is where the request's own illustration turned out to be the widest of the three, and where the grant is narrowest relative to it. Re-deriving each item CR-02 §3c named:

| Named in §3c | Status | Already answered by |
|---|---|---|
| Session duration so far | **Granted** | New `Session.opened_at` — genuinely missing; nothing else states it |
| Ring buffer margin, stall/gap diagnostics | **Granted** | New `BufferMargin` / `buffer_status`, matching PinPointStudio's review argument that this is `Readiness`-shaped, not field-shaped |
| Shots taken | Declined — already served | `len(Session.shots)` |
| Captures committed | Declined — already served | Walking `Capture.transfer == confirmed` across Captures already received via `capture_announce`/`capture_update`, both `owner → any` |
| Per-capture and per-session drop counts | Declined — already served | `Capture.achieved_summary` per capture, already `owner → any`; summed client-side |
| Link quality (offset, drift) | Declined — already served | `relation_update`, `method: estimated_online`, already on the wire |
| Link quality (throughput) | Declined — not the wire's to state | Directly observable by whichever peer is asking, from payload timing it already has |
| Device uptime | Declined — not the wire's to state | Directly observable by the host as elapsed time since it first heard from that peer; a peer already knows its own |

**This is not a smaller version of what was asked; it is a different claim about where the gap actually is.** The request described these as things "existing today only as unshared local counters" — true of the *implementations* (PinPointCapture's `TransferQueue`, PinPointStudio's `HostLink` telemetry, both cited in the request's own review), but not true of the *wire*: everything in the "declined" column above is already fully reconstructible by any peer from messages already flowing to it. Where an implementation has not wired its own already-received data to its own display — PinPointStudio's review named exactly this for `heartbeat_ack`'s battery field — that is a display backlog item, not a protocol gap, and adding a wire field to work around it would be carrying redundant content permanently to save one implementation a local computation once.

> ⚠ **Corrected, 26 August 2026, at PinPointStudio's round-1 review (finding R-2).** The paragraph above treats "captures committed / drop totals" as costing the same as "battery display backlog." Checked against real code, it does not: PinPointStudio currently discards `capture_announce` data after one use and does not parse `capture_update` at all, so walking `Capture.transfer`/`achieved_summary` needs a retained Capture ledger this codebase does not have — closer in size to the `capture_announce` handling already built than to reading one already-parsed scalar. **The decline itself still stands** — the wire content is sufficient in principle, which is what this disposition actually rules on — but a future reader sizing the eventual build should not read every item in the "declined" column as equally cheap. This note is added here rather than silently folded into the paragraph above because both teams have already read it, matching the convention `CR-01`'s disposition set.

13e answers CR-02 §5's explicit ask about 13d: these three additions travel only within a live session, between the peers already in it, answering a question one of them is asking to run the session — not telemetry leaving the session to a third party, and 13e says so normatively rather than leaving a future reader to infer it.

---

## 3. What was not touched

- **`heartbeat`/`heartbeat_ack`** is unchanged. Peer-level battery, thermal, storage and charging stay exactly as they were; nothing here duplicates them.
- **`Readiness`** is unchanged; `DeviceStatus` is additive beside it, not a replacement (5.20b).
- **No new error code.** Refusal shapes reuse the existing `verdict`/`reason` pattern (`stream_open_ack`) for `actuator_command_ack`, and the existing `not_declared`/`malformed` codes cover an undeclared or ill-formed actuator command.
- **No renumbering.** Every addition is a new subsection (`§5.19`–`5.21` in `CORE`, `§5.5`–`5.6` and a new `§12` in `MSG`) or a new table row. No existing section, clause or invariant number moved.

---

## 4. What both teams are asked to check

**The instruction governing this review is explicit and both teams should hold themselves and the protocol owner to it: at most three review rounds, starting now.** CR-01 took five and generalisation #7 of this folder's own `README.md` records what that cost. This request does not have that budget. A finding in round three that is genuinely blocking is still raised — but every finding should be written with the third round in mind: is this something a **fix** resolves, or something that needs a **fourth** look to know it was fixed? If the latter, say so explicitly and propose the most conservative fix available, because there will not be a round four to confirm it.

In descending order of how badly it would hurt to get wrong:

| | Ask | Why |
|---|---|---|
| **1** | **Is `DeviceStatus` actually distinguishable from `Readiness` in your own implementation, or would you end up computing both from the same underlying check?** | If a real implementation cannot tell "can this Source be opened" from "would the next shot settle" as two different questions with two different answers at two different times, 5.20b's claim that they are independent measurements is wrong, and that is worth knowing before code is written against it. |
| **2** | **Attack the host-only scoping of `actuator_command` (12a).** Is there a real deployment either of you ships or plans where a non-host peer legitimately needs to command an actuator — a second phone commanding the first's torch, for instance? | Widening a direction constraint later is additive; getting it wrong now and discovering a real case blocked by it is not free, even though it is not breaking. Better to find this in round one than to grant CR-03 for it in three months. |
| **3** | **Is §2c's "already served" table actually true against your own message handling?** Specifically: does your implementation actually retain every `capture_announce`/`capture_update` it receives such that walking `Capture.transfer` and `achieved_summary` is really possible after the fact, or does it discard what it does not immediately need? | The whole argument for declining most of 3c rests on this. If either implementation's real behaviour is "we don't keep that," the decline is wrong and the field should be added, not argued around. |
| **4** | **Is `BufferMargin`'s shape actually what your local ring-buffer instrumentation would map onto?** PinPointCapture's review cited `RingStats`' cause-broken-out drop counts and an 8-bucket gap histogram — `BufferMargin` carries only the single most recent discard, not a histogram. Is that too little? | This is a case where matching the existing local instrumentation exactly might have been the more useful answer, and the protocol owner chose the smaller version deliberately (a histogram is a UI concern more than a wire one, arguably). Worth a direct verdict either way rather than silent acceptance. |
| **5** | **Confirm `actuator_command_ack.state` echoing the achieved rather than requested value is buildable on your platform**, for a torch specifically. Does either platform's torch API ever silently clamp a requested level, and if so does your implementation currently have any way to observe the clamped value to put in `state`? | If the achieved value is unobservable on a given platform, 12.1c is unsatisfiable as written and needs a fallback (echo the request with a caveat, or drop the requirement). |
| **6** | **PinPointStudio: does `PpcpHostService::phones()`'s currently-hardcoded `batteryPct: -1` block anything in this grant**, or is it purely the pre-existing display backlog item your review already named? | Confirming it for the record, since §2c's whole "already served" argument leans on data already being received even where it is not yet displayed. |

**One item for the protocol owner, not the review teams.** Conformance test authoring beyond `CT-I39` (added in `PPCP-CONF` alongside this grant) is deferred to implementation, matching how RT-20's detail accreted across CR-01's own review passes rather than arriving complete with the grant. This is noted so neither team reads the absence of `CT`-rows for `device_status`/`buffer_status`/`Session.opened_at` as an oversight.
