# PPCP revision 5 — continuous streams and preview during capture

**Review from the PinPointCapture mobile app**

| | |
|---|---|
| Reviewing | `PPCP-CORE` revision 5 — B11's fix (`Capture.anchor: { stream: true }`, I27 amended, I36 added) and `preview` ([§5.11.1](.), [§5.11.2](.)), with the `PPCP-MSG` 8.1c/f/g changes |
| Seat | Owner of the PinPointCapture iOS/iPadOS app — the peer that would *produce* both continuous streams and preview |
| Date | 22 August 2026 |
| Verdict | **Support the change.** B11 was a real defect and the fix is the right shape. Four findings, one of which I think matters: preview is the one stream that is deliberately dropped, and the model currently has no way to say so without claiming a dropout. |

## 0. Why this matters from here, stated first

B11 was blocking a requirement we hold, not a nice-to-have. **REQ-META-1 is a MUST**: capture device attitude and gravity *continuously*, on the grounds that it constrains extrinsics substantially and costs nothing. Before revision 5 a `metadata` Stream — which §5.11 calls *always* continuous — had no way to carry a single sample, so the requirement was unimplementable and we would have had to defer it and explain why.

The same is true of the sensor-arrival evidence §9.1b requires a bundle to carry, which is the requirement our own doc calls "the most expensive to retrofit" because the evidence exists only at capture time.

So the fix unblocks two things we were going to have to raise ourselves. Recording that up front because the rest of this review is findings.

---

## 1. Preview is deliberately dropped, and a dropped interval is currently indistinguishable from a failed one

**This is the finding.**

§5.11c accounts for a continuous Stream's whole open interval using *"those Captures and their declared `gaps`"*. But §5.11's own table, and I11, give a gap one specific meaning:

> `continuous` — absence between shots means **a dropout**, recorded as an explicit gap

And §5.11i requires the opposite behaviour of preview:

> Under contention, preview degrades **before** transfer, which degrades before capture … a preview frame is the cheapest thing in the session to drop.

So the one stream the specification tells a peer to drop on purpose is carried by a mechanism in which absence means something failed. A host looking at a bundle sees a gap on the preview stream and cannot tell a deliberately-shed second of video from a camera that stalled — which is exactly the conflation the continuity flag was introduced to prevent.

It is *expressible* today: a stream-anchored Capture with `completeness: absent` and `absent_reason: not_retained` is a Capture, so it satisfies I36's accounting, and `absent_reason` is where the distinction lives. But nothing says to use it, and I36 treats a gap and an absent Capture as interchangeable for accounting purposes. Two conformant peers will choose differently, and the one that chooses `gaps` will be reporting dropouts it did not have.

**Suggested:** state in §5.11.1 that deliberate non-retention is an **absent Capture with a reason**, and that `gaps` are reserved for loss. That preserves I11's meaning, needs no new field, and makes the preview degradation of 5.11i honestly recordable. A sentence in 5.11c would do it.

This matters more for us than it looks, because our thermal budget means we will be shedding preview frames routinely rather than exceptionally.

## 2. A preview `CaptureProfile` is not a mode the Source can operate in

§5.7 defines `CaptureProfile` as *"A mode a Source can operate in"*. §5.11f defines a preview Stream as *"a second Stream from an existing Source, with its own `profile_id` — typically a low rate and a small frame"*.

On iOS those cannot both be true. An `AVCaptureDevice` has exactly one `activeFormat` at a time. There is no way to run 1080p150 for capture and 640×360 at 5 fps for preview *as two device modes of one camera*. The only viable implementation is decimating and scaling in software from the capture format — so the preview profile describes a **derived view**, not a mode the Source can enter.

Two consequences worth deciding rather than discovering:

- **A host reading our profile list has no way to tell.** It could reasonably select the preview profile for a capture Stream, and we would have to refuse a profile we ourselves advertised. Nothing marks a profile as preview-only.
- **The per-profile declarations mean something different.** `measured` is `0..1` so absence is honest. `intrinsics: none` is available and correct, since 5.11g forbids measurement — and it must be `none`, because scaling changes the intrinsic matrix and declaring the capture profile's would be false. `geometry` is the sensor's readout and is unchanged by decimation, so declaring it is honest.

So it works, but only because each escape happens to exist. **Suggested:** say so explicitly — that a preview profile MAY describe a derived view rather than a native mode, and is valid only on a Stream of kind `preview`. One clause in 5.11f, and it stops the semantics of "profile" widening silently.

The alternative implementation — a second physical camera as a second Source — is worse and I would not want anyone assuming it: it needs `AVCaptureMultiCamSession`, is unavailable on some devices, and spends exactly the thermal budget §7.4d exists to protect.

## 3. A peer needs to close a preview it has already opened

§5.11i puts preview first in the degradation order, and `stream_open_ack` lets us refuse one at open time with a reason. Both good.

What is not clear is whether the **producing** peer may originate `stream_close`. Under sustained thermal load forty minutes into a session — the case REQ-ENC-4 says to design for and the one where published throughput figures stop applying — the honest action is to close the preview Stream, not to keep it nominally open while announcing absent Captures for the rest of the session.

If only the opening peer may close, a device under thermal pressure satisfies I36 by announcing absence forever, which is conformant and silly. **Suggested:** confirm `stream_close` may be originated by either peer, and that `reason` can carry `thermal_limit` — the vocabulary `Readiness.blocked_reason` already uses.

## 4. The control channel changes character, and window length has only one voice

Before revision 5, control traffic was proportional to **shot count** — roughly fifty events in a session. After it, a continuous Stream announces a Capture per window for the whole session.

For a session with `metadata` (always continuous), an `imu` stream running while armed, and a preview, at a one-second window that is three announces per second — of order **16,000 `capture_announce` messages in a ninety-minute session**, against about fifty shot events. Each is small; the aggregate is not nothing, and it lands on the channel whose immediacy the entire two-channel design exists to protect.

§5.11e leaves window length to the producing peer, and that is the right default. But it is the *consumer* that experiences preview latency: a host wanting quarter-second preview implicitly wants a quarter-second window, and has no way to ask for one. Nor is there a way for it to say "I want preview at all, but I do not need the metadata stream announced every second".

**Suggested:** one sentence in 5.11e noting that window length is the producer's and that a consumer needing lower latency asks for a different **profile** rather than a shorter window — or, if influencing it is intended, say how. Either is fine; the silence is what will produce two different assumptions.

## 5. Smaller points

**`preview` is missing from the `kind` enumeration.** §5.11's field table lists `video | audio | imu | wrist | event | metadata | …`, and the continuity table directly beneath it has a `preview` row. The registry is open so `preview` is legal, but two tables in one section disagree, and `preview` is not an extension — it is defined normatively in 5.11.2.

**8.1g's reasoning is worth keeping verbatim.** *"A preview frame that arrives late is worth nothing; a clip that arrives late is worth everything."* That is the whole priority rule in one line and it is the sentence an implementer will remember when deciding what to drop.

**I36's "a defect, not a dropout" is the right severity.** Unaccounted time being a *defect* rather than an inferred dropout is consistent with I10 throughout, and it is testable, which the alternative would not have been.

---

## 6. What we will do

Recorded so the protocol team knows the producer's side.

- **`metadata` continuous, from v1.** Attitude and gravity, stream-anchored, satisfying REQ-META-1. This is the change's immediate value to us.
- **Preview not offered while armed, initially.** We will declare no preview profile until we have measured the thermal cost of decimate-scale-encode alongside 1080p150 sustained for forty minutes. §5.11f's "a peer that does not offer a suitable profile simply refuses" is what makes that a conformant position rather than a gap, and I want to be explicit that we intend to use it.
- **Window length is ours and we will start long** — of order one second — and shorten only where a consumer demonstrates it needs to.
- Our own C1 screen's full-bleed local preview is a **different thing** from a protocol preview Stream and costs nothing on the wire. Nobody should read our having one as evidence that a protocol preview is cheap for us.

## 7. Summary

| # | Finding | Severity |
|---|---|---|
| 1 | A deliberately-dropped preview interval is indistinguishable from a dropout | **Fix before implementation** — one clause, and it is what preview's degradation rule depends on |
| 2 | A preview `CaptureProfile` is a derived view, not a Source mode | **Medium** — works only because each escape happens to exist; say so |
| 3 | Unclear whether a producer may close a preview it has open | **Medium** — matters under thermal load, which is the designed case |
| 4 | Control traffic now scales with session length; window length has one voice | **Low** — size it, and say whether a consumer may influence it |
| 5 | `preview` absent from the `kind` enumeration | Trivial |

The change itself is right and I would not want it deferred. B11 was a defect that made a documented obligation unmeetable, and the fix is additive, needs no new message, and lands while `1.0` is still unfrozen — which is the correct moment for it.
