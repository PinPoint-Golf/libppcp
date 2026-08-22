# PPCP Draft 1 — review from the PinPointCapture mobile app

| | |
|---|---|
| Reviewing | `libppcp/docs/specification` — `CORE`, `MSG`, `ENC`, `CONF`, `RV-SCOPE`, wire version `ppcp/1.0` Draft 1 |
| Seat | Owner of the PinPointCapture iOS/iPadOS app — the peer that will declare `Core + Capture + Detect + Mint + Offline` at v1 |
| Basis | A read of the specification set, plus a working iOS app: capability enumeration, the timebase and session model, permissions and the AVFoundation capture layer are built and running on an iPhone 16 |
| Date | 22 August 2026 |
| Verdict | **Approve to implement**, with two items I would want settled first and one conflict that will otherwise surface as stream churn in the field |

This review is deliberately narrow. It does not re-examine the model — the disposition of the 22 August review is complete and the five defects are properly closed. It asks one question throughout: **can the mobile side implement this, and what does it cost?** Findings are ordered by what they cost to fix later.

---

## 1. Blocking, or close to it

### 1.1 Online time-of-flight estimation conflicts with I5

This is the finding I am most confident about and the one most likely to bite in the field.

Three statements in the specification are individually right and cannot all hold:

| Where | Statement |
|---|---|
| `CORE` §5.9b | Acoustic time of flight lives in `Calibration`, `kind: position`, on the microphone Source. "It is not a separate concept." |
| `MSG` §6.2a | Per-shot residuals accumulated over a session "resolves the acoustic time-of-flight distance as a free parameter" |
| `CORE` §5.9a / I5, `MSG` §3.6a | A calibration change **closes every open Stream referencing that Source** and requires a new `stream_open` |

The device is required to estimate time of flight online, continuously, across a session — that is `REQ-MIC-4`, and it exists because the microphone-to-ball distance is user-chosen and cannot be measured by the golfer. The estimate improves with every shot. But the only place to express it closes and reopens a Stream each time it changes.

For a fifty-shot range session that is up to fifty `stream_close`/`stream_open` cycles on the audio Stream, fragmenting a session's audio into dozens of Streams whose only difference is a converging scalar. Nothing in the specification appears to intend that.

Note also that `Calibration.method` already offers `estimated_online` — a value that, under I5, can be set once and never updated. The enum anticipates the case the invariant forbids.

**Three ways out, in my order of preference:**

1. **Exempt `method: estimated_online` calibrations from I5**, on the stated grounds that I5 exists to stop *geometry* changing under a fixed set of frames — a lens swap, a knocked tripod — and a converging scalar estimate is not that. This keeps I5's purpose and removes the churn.
2. **Move the applied correction out of Calibration** and carry it per Candidate with its own uncertainty (see §1.2), leaving Calibration for the surveyed position where one exists.
3. **Require the device to freeze its estimate for a Stream's lifetime** and publish refinements only at Stream boundaries. Honest, but it discards the benefit for the session in which it was learned, which is the session that needed it.

I would not ship v1 against option 3 without saying so out loud, because the app's whole time-of-flight story is that the user never measures anything.

### 1.2 `tof_correction_ns` is the one estimate in the specification without a sigma

`CORE` §5.12 makes `Candidate.at` post-correction and carries `tof_correction_ns` as a bare `int64`, "so a host can undo it".

Every other estimated quantity in this specification carries mandatory uncertainty, and the reasoning is stated repeatedly and well: `TimebaseRelation` is malformed without both sigmas (I3); `SessionLink` carries both for the same reason (5.17b); `Calibration.uncertainty` is mandatory; "a point estimate with no dispersion is what silently corrupts fusion".

Time of flight is exactly such an estimate, and by §1.1 it is a *converging* one — which means its uncertainty is large early in a session and small late, and the difference is the whole point. At 343 m/s a 2 m distance is 5.8 ms, most of a frame at 150 fps. An early-session correction could plausibly be wrong by a frame, and the host has no way to know whether to weight it.

Undoing the correction is not a substitute: it recovers the raw timestamp but tells the host nothing about how much to trust the corrected one, and a host that undoes every correction has thrown away work the device was better placed to do.

**Suggested:** `tof_correction_ns` becomes `{ value_ns, sigma_ns }`, mandatory together, matching I3's shape. If §1.1 resolves toward option 2 this is the natural home for the whole quantity.

---

## 2. Costly to change later

### 2.1 `capture_announce` is called the small message and is not small

`MSG` §8.1 describes `capture_announce` as "The small, immediate message" on the control channel. It carries `Capture.achieved`, and `CORE` §5.8 makes `achieved` a set of **parallel per-frame arrays**.

For the profile the app actually ships:

| | 1080p150, 3 s | 1080p240, 3 s |
|---|---|---|
| frames | 450 | 720 |
| `frames.ns` | ~4 KB | ~6.5 KB |
| `exposure_ns` | ~2.3 KB | ~3.7 KB |
| `iso` | ~1.4 KB | ~2.2 KB |
| `intrinsics` (`per_frame`, 9 doubles/frame) | ~36.5 KB | ~58 KB |
| **total** | **~44 KB** | **~70 KB** |

The specification's own worked example puts `sync_probe` at 95 bytes. The "small, immediate" message is roughly 460 times that, and it is the message whose immediacy the entire two-channel design exists to protect. On the degraded link the app has a designed state for — 4.1 Mbit/s — 44 KB is about 86 ms before anything else on control moves.

It is well within the 1 MiB control limit, so nothing is *broken*. But the split between "correlate the shot now" and "here is everything about the frames" is exactly the split `REQ-SESS-5` describes, and per-frame arrays are on the wrong side of it: they are only interpretable alongside the frames they describe, which arrive on bulk.

Worth noting how redundant the payload is under the app's own constraints: `REQ-OPT-3` locks exposure, so all 450 `exposure_ns` values are identical, and locked focus means the 450 intrinsics matrices are too. The protocol is right not to assume the lock held — but it currently has no way to say "constant across this capture" either.

**Suggested:** split `AchievedCapability` into a summary carried by `capture_announce` (`dropped_frames`, thermal timeline, exposure/iso min–max–median, frame count) and the per-frame series carried with the payload or in a following `capture_update`. Alternatively permit a scalar form for any per-frame array that is constant across the Capture. Either keeps the announce genuinely small; the second is the smaller change.

### 2.2 A declared `frame_start_to_exposure_offset_ns` of zero cannot be distinguished from an unmeasured one

I22 requires the offset to be declared explicitly even when zero, and the reasoning given — "a declared zero is a checkable claim; an omitted field is not" — is right as far as it goes.

But no public iOS API exposes this quantity. Like `readout_ns`, it comes from the LED timecode rig, per device model, and **no model has been through the rig yet**. So every iOS peer shipping before the rig exists will declare `0`, and a host cannot tell that from a measured zero.

This is precisely the reasoning behind I28, applied to a different field: without `MeasuredCapability.method`, a cold three-second sample and a forty-minute sustained figure are the same number. Without an equivalent marker here, an unmeasured offset and a measured one are the same number — and this one silently biases every cross-source comparison rather than merely overstating a frame rate.

**Suggested:** give the offset the same treatment as `measured` — either `{ value_ns, method: declared | measured }`, or make it absent-means-unmeasured with I22 requiring presence only once the model is calibrated. The first is more consistent with I22's existing "declare it explicitly" instinct.

I would say the same about `geometry.readout_ns`, which has the same provenance and the same problem.

---

## 3. Implementability notes — not defects

Things the specification requires that the mobile side can do, but which are worth the protocol team knowing cost something.

**Per-frame exposure may not be available per sample buffer on iOS.** 5.8d and 6.1c require `exposure_ns` per frame, parallel to `frames.ns`. Per-frame *intrinsics* arrive as a sample-buffer attachment and are straightforward. Per-frame *exposure* I am not aware of an equivalent for — `AVCaptureDevice.exposureDuration` is a device-level property that must be sampled on the capture queue, which is exact while exposure is locked (`REQ-OPT-3`) and approximate otherwise. **This needs verifying on our side before we claim conformance**, and I flag it because 6.1c explicitly says the protocol must not assume the lock held: if the lock is off, we may not be able to supply a truly per-frame value at all, only a per-frame sample of a device property. If that is right, the honest conformance position for an unlocked iOS source is a question the specification should answer rather than one we answer locally.

**The per-frame arrays constrain the 150 fps path.** Collecting four parallel series at 6.7 ms per frame means preallocated storage sized to the window, not appended containers — a reallocation mid-swing is a dropped frame, and `REQ-CAP-3` exists to detect exactly that. This is our problem, not the protocol's, but it is a direct consequence of `achieved`'s shape and worth stating so it is not discovered late.

**Whole-payload digest before announce.** `ENC` 6c makes `payload_begin.digest` the hash of every chunk concatenated, and `Capture.digest` carries it. So a ~25 MB clip must be fully written and hashed before the announce that is meant to be immediate. On current hardware that is tens of milliseconds and fine — noted only because it means the announce cannot precede the clip being complete, which rules out announcing a capture while it is still being extracted from the ring buffer.

**Two channels are comfortable on iOS.** Two `NWConnection`s, or QUIC streams via `NWMultiplexGroup`, satisfy T2/T5 without difficulty, and the same holds over a USB tunnel. No concern here; recorded because T2 is the requirement most expensive to discover late and I want it on record that the mobile side has no objection to it.

---

## 4. Answers to the seven questions

**Q1 — CBOR with text keys.** **Agree, without reservation.** Control traffic is negligible on mobile and bulk is unaffected because chunks are opaque. The 40% saving is real and irrelevant; being able to read a frame in a hex dump during a field diagnosis is worth more, and we will be doing that diagnosis on a phone at a driving range with no debugger attached.

**Q2 — `SessionLink` defined now.** **Accept.** I suggested deferring; the counter-argument is better than my original point. The shape is determined by constraints already in the model, and a determined-but-unwritten shape is how two implementations invent divergent forms. It is OPTIONAL within Offline, and the v1 device does not implement it, so it costs the mobile side nothing. Keep it provisional as marked.

**Q3 — offset placement, `t + offset + d/2`.** **Agree.** Nominal frame start plus the offset is the actual exposure start; half the exposure past that is mid-exposure. The composition is right and worked example A is correct. My concern is not the placement but the provenance of the value — see §2.2.

**Q4 — coincidence window of 50 ms.** **Cannot settle without rig data, and I would not guess.** One datum from our side: acoustic time of flight at 2 m is 5.8 ms, and the spread between a device mic and a host mic in the same bay is of that order — so 50 ms is roughly an order of magnitude above the intra-bay spread, which sounds defensible. The case I would want measured is the adjacent bay: a shot 4 m away arrives ~12 ms later, and if two golfers in neighbouring bays hit within 40 ms the window merges two real shots into one. That is a failure that produces a *wrong* shot rather than a missing one, which is the worse direction. Worth the rig measuring inter-bay separation explicitly, not just intra-bay.

**Q5 — version support window.** A product decision, but the mobile constraint is specific and one-sided: App Store review latency plus users who never update means **old-app / new-host is the permanent normal case**, not an edge case. I would ask for two things beyond a number: that the *device* can state its own dialect and degrade gracefully facing an unknown host, not only the reverse; and that whatever N is, it is expressed in released versions rather than elapsed time, because our release cadence is not ours to control.

**Q6 — candidate audio retention is an application obligation.** **Confirmed, we own it.** The protocol's position is right: a retention cap is exactly the threshold I14 keeps out. Two caveats from our side. First, we cannot size the bound yet — it depends on the candidate-to-shot ratio in a real bay, which nobody has measured, and the requirements review already flags that our own arithmetic was computed on shots. Second, `CORE` §13's statement that the count is not bounded by anything the user does is the sentence that makes this reviewable; please keep it.

**Q7 — `PPCP-RV` does not exist.** **This is the highest-priority gap from where I sit**, ahead of anything in §1, and for a reason the scope document does not mention: **we have already shipped a guess.** The app's `Info.plist` declares `NSBonjourServices` as `_ppcp._tcp`, chosen by us with nothing to reference. That string goes to App Review as part of the bundle, and the QR payload format is parsed by the phone and cannot change after the first release without breaking every code already printed. So the deadline for `PPCP-RV`'s service type and QR payload is **our first App Store submission**, which is earlier than `ppcp/1.0` being declared stable. If those two items are fixed and nothing else, we can proceed.

---

## 5. What I checked and found sound

Recorded so the team knows what was exercised rather than skimmed.

- **The `Mint` profile** resolves the defect exactly as needed, and the Mint/Arbitrate comparison table in §2.2.1 makes the difference testable rather than merely stated. D5 — Arbitrate not depending on Mint — is the right call for the reason given.
- **§6.1 and its worked examples.** Example D, showing 0.87 ms of systematic error from skipping the conversion and observing that it *moves* with light, is the most useful paragraph in the specification for an implementer. We will reproduce all four as unit tests.
- **§6.2's rolling-shutter formula.** `r/(R−1)` with explicit endpoints and both directions, plus the `R == 1` case, removes the ambiguity the model had. That it is also what the rig measures directly is the right test.
- **I28 and `MeasuredCapability.method`.** This came from an implementation note and lands correctly on the wire. Our A7 screen currently displays "not measured yet" rather than a cold sample; `method` is what lets that be honest across implementations rather than a local choice.
- **`Capture.anchor` as exactly one of Shot or Candidate (I27).** Found while writing rather than in review, and it is the change that makes candidate-attached audio actually writable.
- **The absent-versus-`null` rule (`ENC` 4c) and absence having stated meaning.** Load-bearing for us in two places already — `measured` absent means not measured, `evidence_ref` absent means not retained.
- **`ENC` 8a/8b**, distinguishing fatal desynchronisation from a recoverable malformed message on the grounds that capture degrades last. That principle surviving into the encoding layer is a good sign for the document set as a whole.

---

## 6. Summary

| # | Finding | Severity |
|---|---|---|
| 1.1 | Online ToF estimation conflicts with I5; forces stream churn | **Blocking** — needs a decision before we implement audio |
| 1.2 | `tof_correction_ns` carries no uncertainty, unlike every other estimate | **High** — cheap now, wire change later |
| 2.1 | `capture_announce` carries per-frame arrays and is ~44–70 KB, not small | **High** — schema change later |
| 2.2 | A declared offset of `0` is indistinguishable from an unmeasured one | **Medium** — same reasoning as I28 |
| 3 | Per-frame exposure availability on iOS needs verifying against 5.8d | **Open question** — ours to answer, may become a spec question |
| Q7 | `PPCP-RV` deadline is our first App Store submission, not v1.0 stable | **Scheduling** |

Nothing here changes the verdict: **approve to implement**. §1.1 is the one I would want an answer on before we build the acoustic path, and §2.1 and §2.2 are both cheaper to change now than after the first bundle exists in the field.
