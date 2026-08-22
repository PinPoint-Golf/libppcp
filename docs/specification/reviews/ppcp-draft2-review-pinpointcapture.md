# PPCP Draft 2 — review from the PinPointCapture mobile app

| | |
|---|---|
| Reviewing | `libppcp/docs/specification` at Draft 2, wire version `ppcp/1.0` |
| Seat | Owner of the PinPointCapture iOS/iPadOS app — the peer declaring `Core + Capture + Detect + Mint + Offline` at v1 |
| Basis | The Draft 2 document set and the disposition, read against a working iOS app; and against my Draft 1 review, whose four findings are all closed |
| Date | 22 August 2026 |
| Verdict | **Approve to implement.** Three findings, one of which I would fix before the first line of arbitration code, and one contribution to the open coincidence-window question that may change its shape. |

Draft 2 closes all four of my Draft 1 findings and the disposition is complete. I have not re-examined settled ground. Given this is the last review before change-request territory, I have gone looking specifically for **things that become expensive after implementation**: unresolved races, mandatory fields that will want to be optional, and identity rules with holes. All three findings below are of that kind.

I have also confirmed Draft 2's fixes actually work rather than accepting them on the disposition's word — §4.

---

## 1. Findings

### 1.1 The mint/issue race is narrowed by `issue_hold_ns` and never resolved

**This is the one I would fix first.** It is new in Draft 2, it arrived with a fix for a different problem, and it is the kind of defect that surfaces once a week in the field and never in a lab.

`8.2i` / I32 give a nominating peer a deadline: do not mint for a Candidate sent to a host until `issue_hold_ns` plus one `heartbeat_interval_ms` has elapsed with no `shot`. That is the right mechanism and it closes the disagreement the host review found.

But it narrows a race rather than closing it. Consider the ordinary sequence:

1. Device nominates a Candidate to the host
2. `issue_hold_ns` + one heartbeat elapses with no `shot`
3. Device mints, `authority: device`, and extracts a Capture anchored to its own `shot_id`
4. **The host's `shot` arrives at T+ε** — it was slow, or the control channel stalled briefly, or the heartbeat interval under-estimated the link

Nothing in the specification says what happens next. The outcome is two Shots for one swing:

- both immutable, because I7 forbids revising `t0` on either
- both referencing the same Candidate, which nothing forbids
- unmergeable, because I9 forbids rewriting on reconciliation
- with no `withdraw`, `supersede` or `retract` message in the catalogue

For the app the consequences are concrete and user-visible: two rows in the session library for one swing, a session count that is wrong by however often this fires, and a Capture already extracted and possibly sent against the device's `shot_id` while the host then issues `capture_request` against its own — the same bytes transferred twice. `8.5c` deduplicates on `Capture.digest` at the receiver, so the host does not store it twice; the device still sends it twice, over the bulk channel, on the link the app has a designed degraded state for.

**How likely?** Less likely than it was, which is the trouble — rare enough to survive testing, common enough to happen at a range. The margin is one heartbeat, and a heartbeat interval is chosen for liveness, not as an upper bound on host issue latency. Any host GC pause, disk stall or scheduling hiccup longer than the margin fires it.

**Suggested fix, which needs no new message and no new entity.** Make the rule ordering rather than withdrawal:

- A minting peer MUST send its `shot` immediately on minting, so the host learns of it.
- **A host that receives a device-minted `shot` for a Candidate it is still holding MUST NOT issue its own.** It attaches to the device's Shot, which `7.2c` already permits: re-send `shot` with an extended `candidates` list and the unchanged `t0`.

That makes the race benign in the direction it actually runs — the device's deadline has passed, so the device's Shot is the one that exists, and the host's arbitration attaches to it rather than competing with it. The alternative reading, that the host wins, would require withdrawing a Shot that Captures already anchor to, which the model has no room for and should not grow.

If the team believes the race is acceptable, I would still want it **stated** rather than unaddressed, because the first implementer to hit it will otherwise invent a resolution locally and it will not be the same one on both sides.

### 1.2 `Capture.digest` is the stated identity for idempotent re-import, and absent captures never have one

`8.5c` states: *"Re-import of a session already held is a no-op, never a duplicate. Identity is `Session.id` plus the minting `Peer.id`; Capture identity is `Capture.digest`."*

But `digest` is `0..1` — "present once known" — and there are two ordinary cases where it is never known:

- **`completeness: absent`.** There is no payload, so there is no payload hash. These are exactly the Captures `REQ-OFF-11` and I10 exist to make assertable: the ones that say "I did not have this". They are the most important thing in a partial session and they have no identity.
- **`completeness: complete`, `transfer: pending`, and never sent.** Draft 2's `8.1e` now permits the digest to be absent from the announce specifically so the announce need not wait for the clip to be hashed — my own request. If such a Capture reaches a bundle before the digest is computed, it inherits the same hole.

So re-importing a bundle containing absent captures duplicates them, which is precisely what `8.5c` exists to prevent, and the specification's own justification for the rule is that *users connect twice*.

**Suggested:** identity is `Capture.id`, scoped by `Session.id` plus owning `Peer.id`, with `digest` as the **content** check where present rather than as the identifier. That keeps content-addressing doing what it is good at — detecting that two byte streams are the same — without asking it to identify things that have no bytes.

### 1.3 `coincidence_window_ns` and `issue_hold_ns` are mandatory on a Session that may have no arbitration

Both are `Card. 1` on `Session` (§5.10). Both are arbitration parameters. Neither has any meaning in a zero-host session, which is `UC-1` and therefore the **normal** case for this application.

Note that `heartbeat_interval_ms` in the same table is correctly conditional — `Live: 1`. These two were not given the same treatment.

Two costs, the second the one that matters:

- A capturing peer opening a hostless session must emit two numbers it will never consult, and every bundle from a range session carries them. A reader could reasonably infer arbitration was in play.
- **A mandatory field cannot be made optional after `1.0`.** Every decoder written against Draft 2 may assume presence. This is cheap today and a breaking change in six months.

**Suggested:** `Arbitrate: 1`, absent meaning no arbitration occurs in this Session — which is exactly what I23 already says about the zero-host regime, expressed structurally instead of by prose.

---

## 2. On Q4 — the two measurements may not admit a single value

This is a contribution rather than a finding, and it may change the shape of the field rather than its default.

Annex B8 now asks the rig for a floor and a ceiling. Putting the two constraints side by side:

| | Constraint | Source |
|---|---|---|
| **Ceiling** | Below the shortest realistic gap between two golfers in **adjacent bays**. The disposition's own figure: two golfers hitting within ~40 ms would merge into one Shot. | Draft 2, Annex B8 |
| **Floor** | Above the largest disagreement between two nominators of the **same** shot, after conversion. | §8.2b |

The floor is the part worth examining before the rig runs, because a live launch monitor is now a legitimate nominator (`Source kind`, Annex B4, settled). Our own B5 reconciliation screen is specified with *"largest disagreement — 41 ms"* between the device's shots and a launch monitor's records.

**That figure is illustrative design copy, not a measurement** — I want to be clear about its provenance. But if it is anywhere near representative, the two constraints are incompatible: a window wide enough to pair a launch monitor nomination with an acoustic one (> 41 ms) is also wide enough to merge two golfers in adjacent bays (> 40 ms). There is no single value.

If that holds, the resolution is probably that **tolerance is per-basis rather than per-session**: acoustic-to-acoustic nominations of the same event should agree to within a few milliseconds after time-of-flight correction, while an `external` nomination from a device with a coarse or loosely-related clock needs a much wider tolerance — and merging that wider tolerance into the acoustic case is what creates the adjacent-bay failure.

Since `coincidence_window_ns` is a mandatory scalar on `Session`, moving to a per-basis map later is a breaking change. If the team thinks this is plausible, the cheap insurance now is to allow the field to carry either a scalar or a per-basis map, defaulting to the scalar. **Worth deciding before the rig runs, not after**, because the measurement design differs: two nominator classes must be measured separately rather than pooled.

I would add one measurement to Annex B8's list: **acoustic-to-acoustic agreement between a device mic and a host mic on the same shot, after time-of-flight correction.** That is the true floor for the case the protocol most cares about, and it is the one the rig can measure most precisely.

---

## 3. Smaller points

**5.8d is unsatisfiable for an absent capture.** It reads: *"On a Capture from a camera Source, `AchievedFrames.exposure_ns` is present."* A Capture with `completeness: absent` has no frames and, since `AchievedFrames` travels with the payload (I30), no `AchievedFrames` at all. A conformance test for 5.8d or I17 will fail on a correctly-formed absent capture. Suggest conditioning it on the Capture having frames.

**`exposure_provenance: sampled` is cardinality-bound to `exposure_ns`, but its accuracy is unquantified.** Annex B10 records this and that is the right place for it. From our seat: we will declare `sampled` for any unlocked source and `locked_constant` under `REQ-OPT-3`'s lock, and we will not claim `per_frame` until we have verified the platform attaches the value. That is the position the specification now makes expressible, and it is what I asked for.

**D12's consequence is acceptable to us.** Two conformant devices given identical audio may report different shot counts. Within one device this is invisible — shots are minted once and recorded — and the unpromoted candidates survive in the bundle (I8), so a host can always re-derive. We are content; recording it because the disposition asked to be sure.

---

## 4. Verification of Draft 2's fixes to my Draft 1 findings

Checked against the specification text rather than the disposition.

| Draft 1 finding | Fix | Verified |
|---|---|---|
| **1.1** ToF collided with I5 | Correction moved to `Candidate`; `Calibration` keeps surveyed position; **5.9c** forbids closing a Stream to publish a refined estimate | ✅ Stream churn is gone. I5 untouched. |
| **1.2** `tof_correction_ns` had no sigma | Now an `Estimate` `{ value_ns, sigma_ns }`, both mandatory, made structural in the encoding (I29) | ✅ And making it structural rather than a stated rule is better than I asked for — an encoder cannot emit one without the other, the same way `Instant` makes I1 unwriteable to violate. |
| **2.1** `capture_announce` was ~44–70 KB | Split into `AchievedSummary` (control) and `AchievedFrames` (payload), I30; plus a scalar form for constant series | ✅ Both taken. The announce is now genuinely small, and 5.8e correctly refuses a scalar form for `frames.ns` on I2 grounds — which is the trap I would have fallen into. |
| **2.2** A declared `0` offset was indistinguishable from unmeasured | `Provenance` on the offset, on `readout_ns`, and on per-frame exposure (I31), plus a fourth silent-failure test | ✅ Extended further than I asked. |
| **3** Per-frame exposure availability on iOS | `exposure_provenance`, three honest positions, `per_frame` forbidden where the platform does not supply it | ✅ Answered exactly as asked: the protocol carries the fact, the consumer judges. |

**On D8 — the departure from my stated preference.** I asked for option 1, exempting `estimated_online` calibrations from I5. The team took option 2 and gave the reason: option 1 makes `Stream.calibration_id` stop identifying a fixed value, so reproducing a past conversion would need a time-indexed calibration history, and the property I5 exists to provide would be gone.

**That argument is better than mine and I withdraw the preference.** Option 2 also has a property I did not appreciate when I wrote the review: it puts the correction where it is consumed and where its convergence is visible, which is what makes the sigma of §1.2 meaningful at all. The two changes fit together in a way my option 1 would have prevented.

---

## 5. Summary

| # | Finding | Severity | Cost if deferred |
|---|---|---|---|
| 1.1 | Mint/issue race has no resolution rule | **Fix before arbitration is written** | Two Shots per swing in the field; duplicate transfers; two implementations inventing different local fixes |
| 1.2 | `Capture.digest` identity has a hole for absent and unsent captures | **High** | Duplicate captures on re-import — the exact failure 8.5c prevents |
| 1.3 | Two arbitration parameters mandatory in sessions that never arbitrate | **Medium** | Mandatory → optional is a breaking change after 1.0 |
| 2 | The coincidence window's floor and ceiling may be incompatible | **Decide before the rig runs** | Scalar → per-basis is a breaking change; and the measurement design differs |
| 3 | 5.8d unsatisfiable for absent captures | **Low** | A conformance test that fails on correct data |

None of these changes the verdict. **Approve to implement.** 1.1 and 1.3 are both cheap now and awkward later, and 1.1 is the only one I would want settled before the arbitration path is built rather than alongside it.

For our part: we will build against Draft 2 as it stands, declare `Core + Capture + Detect + Mint + Offline`, and treat §1.1 as an open question we will not resolve locally — if we hit the race before it is settled, we will report it rather than pick a behaviour.
