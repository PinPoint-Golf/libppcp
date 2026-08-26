# Review of CR-02 (device status, onboard actuator control, statistics visibility) — from PinPointCapture

Reviewed against the draft at `libppcp/docs/changerequests/CR-02-device-status-and-control.md` (uncommitted, working tree) and against what PinPointCapture (PPC) and PinPointStudio (PPS) actually do today. Purpose: is this something PPC can support, and is anything in the request worth tightening before it goes to the protocol team.

**Bottom line: yes, support all three asks — nothing here conflicts with PPC's architecture. But two of the three sections can be made sharper with evidence that already exists in the two codebases, and one carries a premise that CR-01's retrospective specifically says should be checked before it's reused.**

---

## What's already verified true

Before anything else: CR-02's claims about "nothing implemented" hold up.

- No `torch`/`flashlight` code exists anywhere in PPC — not even locally, on-device, unshared. `CaptureDevice` (`Sources/Platform/CaptureDevice.swift`) has no actuator method of any kind.
- No GitHub issue in PPC's tracker mentions torch, flashlight, or device status as a filed requirement. CR-02's "Tracked: Not yet filed" line is accurate.
- `RingStats` (`Sources/Platform/Capture/RingBufferRecorder.swift`) is real, computed, and entirely local — gated `#if DEBUG`, explicitly documented as *"an instrument, not a screen... deliberately not in the design handoff"* (`RingStatsOverlay.swift`). Nothing sends it to a peer. §4c's description of this gap is accurate.
- The "Product requirement (PinPointCapture / PinPointStudio)" joint attribution is earned, not presumed: PPS independently has its own local "device status" concept (the Readiness strip in `docs/user/pinpoint-ux-design.md`, showing *"connected devices, calibration ages, and any warnings"*) and its own ring-buffer UI (`CamerasPanel.qml`'s "RING BUFFER" bar, `resource_monitor_controller.cpp`'s `bufferState`). Both apps arrived at overlapping needs independently.

---

## 3a — Device status

**Support it — and the request can be argued more strongly than it currently is.**

§5 already flags the tension with 5.15a as a genuine open question. It's sharper than that framing suggests, in two ways worth adding:

1. **PPC already has a working, shipped template for exactly the shape 5.15a demands**, and CR-02 doesn't cite it. `DevicePeer.swift` implements `Readiness` with a comment that states the rule directly: *"5.15a — no device state name crosses the wire, so what a health callback answers is a measurement: settled, and what blocks it."* The mechanism is `PpcpReadiness.settled(blockedBy: reason)` / `.notSettled(readyInMilliseconds:)`, where `reason` is an open string registry (`thermal_limit`, `storage_full`, …) — not an enum. This is proof by construction that 3a's requirement is satisfiable without touching 5.15a: don't ask for a state name, ask for an `available: Bool` plus an open `unavailable_reason` registry and a "ready in ms" style measurement, mirroring what `Readiness` already does. Recommend citing this pattern in the request itself, so the protocol team has a concrete existing precedent to react to rather than an abstract tension.

2. **There are, right now, three different vocabularies for adjacent-but-not-identical concepts, and CR-02's proposed wording is a fourth**: PPC's own `CaptureState` is `cold/warm/armed` (`CaptureState.swift`); PPS's own `BufferState` is `idle/capturing/paused/stopping` (`resource_monitor_controller.cpp`); CR-02 §3a proposes `unavailable/ready/capture/idle`. None of the three matches either of the other two, despite all three describing roughly "is this thing doing something right now." That's concrete, present-tense evidence for exactly the risk 5.15a exists to prevent — a platform- or app-shaped name reaching the wire — and it's stronger ammunition than the request currently uses. Worth adding to §5 by name.

**Suggested edit:** reframe 3a away from a 4-value enumeration and toward "available (bool) + reason-if-not (open registry) + since-when," modelled explicitly on `Readiness`.

---

## 3b — Onboard actuator control

**Support it — with two gaps in the ask itself worth closing before it goes out, and one premise worth re-grounding rather than reusing silently.**

1. **Torch may not exist at all, and the request should say so.** iOS torch is a property of the physical rear wide-angle camera (`AVCaptureDevice.hasTorch`) — front-facing cameras never carry one. PPC's own `Viewpoint` model (`downTheLine / faceOn / behind`) already allows for setups where the active camera isn't the rear one. §3b asks the phone to "enumerate the onboard illumination the phone exposes" — good, that's the right shape — but the request should say explicitly that this may enumerate to *zero*, so the eventual actuator registry (§8 Q4, extending §10.3's pattern) doesn't get designed assuming every device has at least one actuator.

2. **On/off vs. level is left unstated, and it isn't a free variable.** iOS actually exposes `setTorchModeOn(level:)`, a continuous 0.0–1.0 control, not just a switch. Given PPC already does real exposure/ISO measurement work (`LightAssessment`, REQ-LIGHT-1/2) that a controllable brightness would interact with, the request should say explicitly whether 3b means on/off only or brightness control too — that changes whether the eventual message needs a scalar parameter, and it's a product decision, not something inferable from the code as it stands today.

3. **The venue rationale reuses language CR-01's retrospective specifically flagged.** §3b's justification — *"a driving-range bay needs consistent lighting... the operator is standing at the host, not the phone"* — uses the same "range bay" framing that CR-01 was granted on and that `E53` later found wrong in its multi-bay form (*"the host is never at the range — it is in the studio"*). The specific structural claim 3b leans on — operator at the host, physically apart from the phone — is consistent with what E53 confirmed, not what it debunked, so the rationale itself is likely fine. But the CR-01 retrospective's lesson #7 is explicit: *"A change request states a requirement AND a situation, and only the requirement gets scrutinised... confirmed by whoever owns the deployment, in the disposition, before the mechanism is designed."* CR-02 should say, in its own words, that this is the single-bay/operator-in-studio deployment E53 already confirmed — not leave a reader to notice the echo and wonder whether it's the same unverified premise or a checked one.

**Suggested edit:** add a line to §3b or §5 stating the enumeration may be empty per device, stating on/off-vs-level explicitly as an open question for product (not the protocol team) to answer, and citing E53 by name as the deployment premise this rationale relies on.

---

## 3c — Statistics

**Support it — but the ring-buffer half of the ask is narrower than what PPC already found worth building, and the request would be stronger pointed at the real shape.**

The local `RingStats` PPC actually maintains is richer and differently taxonomized than "how far it reaches back / retention window remaining / segments discarded":

- Frame-drop causes broken out by reason (`framesDroppedEncoderBusy`, `framesDroppedNotRetaining`), fragment-drop causes likewise (`fragmentsDroppedWriteFailed`, `fragmentsDroppedEmpty`), a `monotonicityViolations` counter.
- An 8-bucket gap-duration histogram plus the 8 largest gaps, each carrying **when** it happened (`sinceFirstNs`) as well as how long (`deltaNs`) — added specifically post-incident (`83a2067`, "The gaps got timestamps, and the panic went away") because "how bad" without "where" wasn't enough to diagnose a stall.

Session/device counters that exist locally but aren't shared show a similar pattern: `TransferQueue`'s `pendingShotIDs`/`bytesRemaining`/`currentShotOrdinal`, `GapWindow.shotsInGap`, `HostLink`'s throughput/clock-offset/drift figures already sit in `HostPanelView`'s telemetry rows today (as the phone's own local read, not yet pushed to a host) — so "link quality" in 3c has a concrete local shape to point to (throughput, offset, drift, retry interval), not just a placeholder word.

The one item in §4c that's already precisely right and needs no change: "captures committed" is correctly identified as requiring a host to derive it by counting `Session.shots` and walking every `Capture.transfer` — that's accurate, and `PpcpTransferState` doesn't currently give a session-level aggregate either.

**Suggested edit:** broaden 3c's ring-buffer language from a retention-window countdown to include stall/gap diagnostics (when and how large, not just how much margin is left) — that's the half of the local instrumentation an operator-facing view would actually inherit value from, and the request as worded would undersell it to the protocol team.

---

## What needs no change

§6 (no mechanism proposed) and the constraint in §5 on 13d (telemetry) are both right as written and don't need PPC's input — 13d is a protocol-team call, not an implementation-shaped one, and nothing in PPC's local code bears on how to word it. Leave both as they are.

---

## Summary of suggested edits before this goes out

1. **3a** — cite `Readiness`'s `settled`/`blocked_reason` pattern as an existing precedent for "measurement, not name," and name the three-vocabulary mismatch (`cold/warm/armed` vs. `idle/capturing/paused/stopping` vs. the proposed `unavailable/ready/capture/idle`) as concrete evidence for the 5.15a tension already flagged in §5.
2. **3b** — state explicitly that per-device enumeration may be empty (front-camera setups); state explicitly whether on/off or brightness level is in scope; cite `E53` by name as the already-confirmed deployment premise the "operator at host, not at phone" rationale depends on.
3. **3c** — broaden the ring-buffer language to include stall/gap diagnostics (timestamped, sized) alongside retention margin, matching what PPC's own instrumentation was built to answer.

None of this changes what CR-02 is asking for. It's the same three requirements, made harder to argue with.
