# Review of CR-02's E63/E64 fixes (round 2 of the three-round cap) — from PinPointCapture

Reviewed against `libppcp` main at commit `5e42268`: `PPCP-CORE` I39 (§11) and 5.20d (§5.20), including the E63/E64 errata table entries; `PPCP-MSG` 12.1 (schema, 12.1c1) and 12.2 (schema, 12.2a1); `PPCP-CONF` CT-I39. Cross-checked against `docs/changerequests/CR-02-review-response-2.md` and PinPointStudio's round-1 review for anything that bears on PinPointCapture (PPC) specifically, and against PPC's own code where a fresh claim needed grounding.

**Bottom line: both fixes close cleanly. No new finding. This closes from PinPointCapture's side.**

---

## 1. Does E63 actually close F1?

Yes. I39 now reads: *"An `actuator_command`, an `actuator_command_ack.state` (where present), and an `actuator_state.state` each carry `on` if and only if the named Actuator's `control` is `on_off`, and `level` if and only if it is `level` — never neither, never both, on all three."* That names all three messages explicitly, closing the exact gap F1 raised: the schema's `(optional)`/`(optional)` pair on `state.on`/`state.level` is now `(present iff Actuator.control == "on_off")` / `(present iff == "level")` on both `actuator_command_ack` and `actuator_state` (`PPCP-MSG` §12.1, §12.2), matching how `actuator_command`'s own `on`/`level` was already written. `state: {}` is no longer a schema-legal ack or event for `verdict: applied` — it now violates I39 the same way an `actuator_command` naming neither field already did.

**Checked the specific gap named in this round's brief** — whether there's a missing "a peer receiving one that violates this responds `error`/`malformed`" statement for the ack/event forms, since 12.1a states that explicitly for the request and 12.1c1/12.2a1 only state that the violation *is* `malformed`, not what the receiver does about it. This is not a gap: it follows the spec's existing pattern rather than breaking it. `5.4a` (`PPCP-CORE`) states `"A relation missing either sigma is malformed and MUST be rejected"` without separately restating a response action either — declaring a shape `malformed` is, spec-wide, already sufficient, because the general error-code table (`PPCP-MSG` §10: `malformed` — *"frame or message failed to decode, or a mandatory field was absent"*) and `10a` (*"`error` is a response where it answers a request, and an event where it does not"*) together already cover the case of a peer receiving a malformed non-request message: `10a` exists specifically so an `error` can be raised against something that isn't itself a request being answered. 12.1a's explicit restatement for the request is the outlier, not the baseline — natural there because a request already obligates *some* response, so spelling out which one costs nothing extra. No missing clause; CT-I39's extension (*"repeat both assertions against `actuator_command_ack.state` (`applied`) and `actuator_state.state`"*) is the correct and sufficient conformance hook.

**Verdict: E63 closes F1 with no residual gap.**

## 2. Does E64 cleanly fix F2?

Yes. §5.20's `reason` field table now reads *"`in_use`, `permission_denied`, `disconnected`, `thermal_limit`, `storage_full`, …"* — `no_source` is gone from the example list, and only from there; nothing else referenced it. 5.20d's new prose gives the reasoning F2 asked for: `device_status` is emitted only for an already-declared Source (`PPCP-MSG` 5.5c), so "there is no Source" cannot occur for an event that names one by construction, while the value remains legitimate on `Readiness` (reported at arm time, which can have nothing to arm). This is exactly F2's own argument, restated as normative text rather than left implicit. Open registry, so nothing else needed changing.

**Verdict: E64 is a clean, complete fix.**

## 3. Anything new, reading fresh against PinPointStudio's R-2/R-3/R-4 and their self-correction?

No finding. Checked each for a PPC-side analogue:

- **R-2** (PinPointStudio doesn't retain `capture_update`/`achieved_summary` the way §2c's decline assumed) is Studio-specific and doesn't touch PPC's answer to the same question (Q3, round 1): PPC's durable record *is* the bundle, written through the same `ppcp_peer_drain` → `ppcp_bundle_writer_append_frames` path a live peer would use, so PPC's "already served" verdict stands unchanged. The disposition's §2c correction note is a cost-framing fix for Studio's build, not a spec change, and doesn't alter anything PPC does.
- **R-3** (Studio's `serialNumber`/`camIdent` identity collision on `device_status` consumption) is a host-side UI-keying concern for a peer *consuming* `device_status` from multiple Sources. PPC is the `owner` of `device_status` (it emits it, per Q1's confirmation that PPC owns a torch and a camera), not a consumer keying a display off it — no analogue.
- **R-4** (`BufferMargin` doesn't map onto either of Studio's existing buffer UI concepts) is a host-side display-mapping question. PPC emits `buffer_status`/`BufferMargin`, it doesn't consume or display it — no analogue.
- **The `HostEngineConfig` self-correction** (Studio's prior review wrongly assumed a callback-slot config designed for *inbound* queries — `health`, `healthReport` — would also fit `actuator_command`, which is *host-originated*, i.e. outbound for Studio) is worth checking directly, since PPC has an analogous-looking config-with-callback-slots pattern (`DevicePeer.swift`'s `config.health`, `config.health_report`, `config.ingest_policy`). Checked `Packages/Core/Sources/CaptureCore/Live/PeerLinkPump.swift`: PPC does not dispatch inbound requests through per-concern callback slots the way Studio's `HostEngineConfig` does — `PeerLinkPump` already runs one generic event-poll loop that switches on every `PPCP_EVENT_*` C enum case (`PPCP_EVENT_ARM`, `PPCP_EVENT_STREAM_OPEN`, `PPCP_EVENT_CAPTURE_REQUEST`, etc.) into a single Swift `case`. A future `PPCP_EVENT_ACTUATOR_COMMAND` slots into that same switch as one more case, the same way `PPCP_EVENT_ARM` already does — it does not need a new `DevicePeer.swift` config slot the way Studio's mistaken read would have needed a new `HostEngineConfig` slot. PPC's architecture doesn't have the direction-mismatch trap Studio's did, because PPC's inbound-request dispatch was never shaped as one-slot-per-concern in the first place. No finding, no action.

Nothing else in the currently-specified text looks wrong on this pass. Round-1's Q1–Q5 verdicts stand unchanged.

---

## Summary

| # | Item | Verdict |
|---|---|---|
| E63 | Closes F1 | Confirmed — I39 binds all three messages, 12.1c1/12.2a1 state it locally, CT-I39 tests it; no missing "responds `error`/`malformed`" clause — general `10a`/malformed-code machinery already covers non-request messages, consistent with the spec's existing style (5.4a) |
| E64 | Closes F2 | Confirmed — `no_source` removed from §5.20's example list, 5.20d records the reasoning |
| Fresh pass | Anything new vs. PinPointStudio's R-2/R-3/R-4 and `HostEngineConfig` self-correction | No finding — R-2/R-3/R-4 are Studio-side consumption/display concerns with no PPC analogue (PPC is the emitting `owner`, not a consumer); PPC's event-dispatch architecture (`PeerLinkPump`'s generic `PPCP_EVENT_*` switch) doesn't have the direction-mismatch trap that caught out Studio's `HostEngineConfig` assumption |

**This closes from PinPointCapture's side.** No blocking finding, nothing deferred to round 3. If PinPointStudio's own round 2 turns up something new, PPC has no objection to a round 3 being run for that reason — but nothing here requires it.
