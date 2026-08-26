# CR-02 review 3 — PinPointStudio, confirming E63/E64 against round 1's findings

| | |
|---|---|
| **Reviewing** | `libppcp` main at `5e42268`: `PPCP-CORE` I39, §5.20d, errata E63–E64; `PPCP-MSG` §12.1 (12.1c1), §12.2 (12.2a1); `PPCP-CONF` CT-I39; `docs/changerequests/CR-02-review-response-2.md`; `CR-02-disposition.md` §2c's correction note |
| **Reviewed by** | PinPointStudio, 26 August 2026 — round 2 of the disposition's three-round cap |
| **Position** | **Closes from PinPointStudio's side.** E63 closes R-1 cleanly. The §2c correction note is an accurate restatement of R-2. Nothing new found. No round 3 needed on anything raised so far. |

---

## 1. Does E63 actually close R-1?

Yes. I39 now reads: *"An `actuator_command`, an `actuator_command_ack.state` (where present), and an `actuator_state.state` each carry `on` if and only if the named Actuator's `control` is `on_off`, and `level` if and only if it is `level` — never neither, never both, on all three."* That is exactly the fix R-1 proposed, applied to the invariant itself rather than left as a local restatement only. `PPCP-MSG` gained 12.1c1 and 12.2a1, each stating the same rule locally against the ack and the event respectively, matching the pattern 12.1a already used for the request. `state: {}` — the concrete counter-example R-1 gave — is now explicitly malformed under 12.1c1 for an `applied` ack, and `actuator_state.state` carrying neither field or both is malformed under 12.2a1. `CT-I39` was extended to assert both: *"Repeat both assertions against `actuator_command_ack.state` (`applied`) and `actuator_state.state`* — an ack or event carrying neither field, or both, on either `control` kind is malformed."* No gap remains in the cardinality rule itself.

**On the "what does a receiver do" question the task asked me to check specifically:** 12.1a spells out the receiver action for the request explicitly — *"responds `error` / `malformed`"* — while 12.1c1 and 12.2a1 only say the ack/event *"is malformed,"* without repeating the response mechanism. I checked whether this is a real gap or just terser phrasing, against how the rest of the specification handles the same pattern:

- `PPCP-ENC` **5d**: *"A receiver that cannot decode a payload responds `error` / `malformed` with `reply_to` where it could recover `msg_id`, and without it otherwise. It does not close the transport."* This is general — it does not gate on whether the malformed thing was a request, a response, or an event, and `10a` (`PPCP-MSG`) already establishes `error` is emitted as an event precisely when it is *not* answering a request, which is the ack/event case here.
- The same terse idiom — declare something "malformed" (or "rejected as malformed") without restating the receiver-response mechanism locally — is how I3, 5.3c, 5.4a and I29 are all written, and none of those needed a companion clause spelling out "responds `error`/`malformed`" either. `CT-I3` and `CT-I29` use the identical "rejected as malformed" phrasing to assert it.

So 12.1c1/12.2a1's phrasing is consistent with this document's own established convention, not a new omission introduced by E63. I don't think this needs a round-3 fix. I raise it only because the task asked the question directly: the obligation is implicit rather than restated, but it is implicit the same way it already is everywhere else "malformed" is used in this specification, and `CT-I39` was written by whoever fixed E63 with exactly that reading (it asserts rejection, not a specific wire-level receiver action, matching `CT-I3`/`CT-I29`'s house style).

## 2. Does the disposition's §2c correction note fairly restate R-2?

Yes, accurately, on both the substance and the scope. The note: (a) correctly isolates the change to cost-framing only — it does not touch the decline itself, matching R-2's own explicit framing ("This is not a reason to reopen E61"); (b) states the two concrete facts R-2 found in the code — `capture_announce` data discarded after one use, `capture_update` not parsed at all — without softening either; (c) uses R-2's own comparison ("closer in size to the `capture_announce` handling PinPointStudio already has than to reading one already-parsed scalar") close to verbatim rather than paraphrasing it into something weaker; (d) is explicit that the wire content is still sufficient in principle, which is the actual thing the disposition rules on, keeping the correction properly scoped to a build-cost estimate rather than a protocol judgment. Nothing in it undersells or overstates what R-2 found.

## 3. One more pass against PinPointCapture's round-1 findings

F1 is the same defect as R-1 and closes the same way (§1 above). F2 (`no_source` dropped from `DeviceStatus.reason`'s example list, now recorded as 5.20d) does not touch anything in my round-1 review — it is a `Readiness`-vocabulary carryover issue orthogonal to R-3's `source_id`/`camIdent` identity concern, and I confirmed 5.20d's reasoning (the event cannot fire without an already-declared Source, per `PPCP-MSG` 5.5c) does not conflict with anything PinPointStudio's code does.

PPC's Q5 (achieved-value ack buildable on iOS via `torchLevel`/`isTorchActive`, KVO-observable) is relevant to how PinPointStudio, as the peer that originates `actuator_command` and consumes the ack, should build its own receive-side handling — worth recording even though it asks for no spec change:

- PPC's own finding is that iOS's `setTorchModeOn(level:)` **throws** rather than silently clamping, so an out-of-range request becomes `verdict: refused` on iOS, not an `applied` ack with a clamped `state`. That means PinPointStudio's own validation of an `applied` ack's `state`, on an iOS-owned Actuator specifically, should not expect `state` to differ from what was requested in the success path — 12.1c's "clamping" language describes a case iOS's own API doesn't produce. The genuine achieved-differs-from-requested case on iOS arrives later, out of band, as `actuator_state` (12.2a) — a thermal cutoff mid-session, not a revised ack.
- This confirms something my own round-1 review already flagged as the right shape for the build (the `PeerHealth`/`setHealthCallback()`-pattern inbound path for `actuator_command_ack`/`actuator_state`, not a `HostEngineConfig` slot): PinPointStudio's own implementation needs to treat `actuator_state` as a first-class, expected input, not a rare edge case, since it is the channel that actually carries a torch's real drift on the platform PPC itself ships on. Nothing here is a defect in the currently-specified text — 12.2a already routes this correctly — it is only a build note, and it does not motivate reopening anything.

I checked the rest of PPC's round-1 material (Q1–Q4) against my own round-1 findings for any conflict and found none: Q1's thermal/storage platform-read overlap is the same shape of "expected overlap, not a design flaw" my own answer to §6 ask 1 gave for `Readiness`/`DeviceStatus`; Q3's confirmation that PPC's durable store *does* answer §2c's "already served" claim, unlike PinPointStudio's ephemeral state, is consistent with — and actually sharpens — R-2's point that the cost differs per implementation rather than being a shared "battery-sized" backlog item everywhere; Q4's verdict on `BufferMargin` (histogram omission fine, cause omission real but out of scope) does not touch R-4, which reached the same "keep the shape as specified" conclusion from a different angle (PinPointStudio's deployment doesn't need the histogram at all, let alone the cause breakdown).

---

## 4. Summary

| | Check | Result |
|---|---|---|
| **1** | E63 closes R-1 | **Yes.** I39 extended to all three messages; 12.1c1/12.2a1 restate it locally; CT-I39 asserts both. `state: {}` is now unambiguously malformed. Receiver-response obligation for the ack/event forms is implicit rather than restated, but that matches this document's own established convention (I3, 5.3c, 5.4a, I29) and `PPCP-ENC` 5d's general rule already covers it — not a new gap. |
| **2** | §2c correction note fairly restates R-2 | **Yes**, accurate and properly scoped — corrects cost-framing only, leaves the decline untouched, does not undersell or overstate. |
| **3** | Anything new against PPC's round-1 findings | **No new finding.** F2/5.20d doesn't conflict with anything in my review. Q5's iOS torch-throw behaviour is a useful build note for PinPointStudio's own ack/`actuator_state` handling, consistent with what my round-1 review already recommended — no spec change indicated. |

**This closes from PinPointStudio's side.** No finding in this round is blocking, and nothing here needs a round 3.
