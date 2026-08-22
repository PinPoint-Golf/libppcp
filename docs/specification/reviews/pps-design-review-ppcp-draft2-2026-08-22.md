# Design review — PPCP Draft 2

**Reviewed as owner of PinPointStudio, the host implementation. Second round.**

| | |
|---|---|
| Documents reviewed | `libppcp/docs/specification/` Draft 2 — `PPCP-CORE`, `PPCP-MSG`, `PPCP-ENC`, `PPCP-CONF`, `PPCP-RV`, and the Round 2 review disposition |
| Reviewer | PinPointStudio maintainer |
| Prior round | `pps-design-review-ppcp-2026-08-22.md` (findings F1–F8) |
| Date | 22 August 2026 |
| Verdict | **Approve to implement.** Four things to fix first. Three are one clause each and I have written the clauses below; the fourth is a straight contradiction between two normative documents. None requires new machinery and none should cost more than an afternoon. |

---

## 0. Position

The disposition is the best I have seen on this project. Every finding was taken, two were resolved
better than I asked, and where the team departed from a reviewer's stated preference — D8, D9 — it
said so and gave the reason. §5.3's opening line, that this is round one's own principle arriving
early, is the right reading of F4.

So this is a delta review, and it is short. It looks only at what Draft 2 changed, because that is
where new defects live and because everything else I already checked.

**The headline finding is uncomfortable and I want it stated plainly:** the fix for my own F2
reintroduced the F1 defect in the live regime, and `CT-I32` now certifies it — the same pattern as
Draft 1, in a new place, arriving through the change that closed it. That is [R1](#r1). It is the
one item I would hold implementation on.

The other three are cheap: an undefined conversion input that has been in the document since Draft
1 and that I missed last round ([R3](#r3)), a normative contradiction created by the announce split
([R2](#r2)), and an encoding ambiguity in the scalar form ([R4](#r4)).

---

## 1. Findings

### R1 — the issue-hold fix reintroduces "every Candidate becomes a Shot", in the live regime {#r1}

**Severity: highest. Holds implementation of Mint. `CT-I32` currently asserts the defect as a pass.**

Draft 2 closed F2 with `issue_hold_ns` and I32. The device-side half reads:

> **(8.2i) MUST** A nominating peer MUST NOT mint locally for a Candidate it has sent to a host
> until `issue_hold_ns` has elapsed since that Candidate's instant, plus a margin of at least one
> `heartbeat_interval_ms` to cover the link (I32). **After that, with no `shot` referencing it, the
> peer MAY mint.**

Nothing anywhere obliges a host to respond to a Candidate it declines. A host arbiter that rejects
a nomination — a dropped club, a club-on-mat, a shot in the next bay, anything that failed
cross-modal agreement — issues no `shot` at all. There is no message that says "considered and
rejected". So the branch that fires is the silent one.

**The consequence is exactly the defect Draft 2 just removed.** In the hostless regime a Mint peer
now promotes a subset of its candidates (8.3b), and `CT-S4` was rewritten so the suite tests the
shape and not the choice. In the live regime, promotion is bypassed entirely: after 1.2 seconds of
host silence the device mints a Shot for *every* candidate the host declined, including the ones
its own detector never believed. A dropped club that the host correctly rejected becomes a Shot
with `authority: device`, in a session that has a host.

Three things follow, and the third is the one that will be found in the field rather than in
review:

1. **It contradicts §7.1.** The roles table still maps *1 host* → `Shot.authority: host`, and 8.3a
   scopes `authority: device` to a session with no host. `MSG` 7.2b says the same. 8.2i and 7.2d
   permit a third state the model does not describe.
2. **`CT-I32` requires the wrong behaviour.** *"A host that receives a Candidate and never issues a
   `shot`. Assert the peer does not mint before `issue_hold_ns + heartbeat_interval_ms`, and **does
   mint after**."* A device that sensibly declined to promote a transient it never believed in
   fails conformance. This is `CT-S4` assertion 2 all over again.
3. **There is nothing to reconcile it with.** 8.3f handles the link-drop case by reconciling minted
   Shots through `ShotLink` on reconnect. Here the link never dropped, so there is no reconnect and
   no link. The host simply receives `shot` messages for events it rejected and has no stated
   response. For PinPointStudio that means the phone injecting shots into a live session for
   transients our arbiter turned down — product-visible, and indistinguishable from a detector
   fault.

**The asymmetry is what makes it reachable.** The host's obligation is `SHOULD NOT` issue early
(8.2h); the device's is `MUST NOT` mint before the deadline (8.2i). Nothing bounds how *late* a
host may issue, so a slow-but-conformant host and a prompt-but-conformant device produce a
duplicate Shot for the same swing, with no defect on either side.

#### Requested change — two clauses, no new message

I looked for a fix that needs a new `candidate_declined` event and I do not think one is required.
Conditioning the mint on the peer's own promotion policy does the work, and closing the host's
deadline makes the two windows non-overlapping.

**(a) Amend 8.2i** (and `MSG` 7.2d, and I32) — added text in bold:

> **(8.2i) MUST** A nominating peer MUST NOT mint locally for a Candidate it has sent to a host
> until `issue_hold_ns` has elapsed since that Candidate's instant, plus a margin of at least one
> `heartbeat_interval_ms` to cover the link (I32). After that, with no `shot` referencing it, the
> peer MAY mint — **but only for a Candidate its own promotion policy would have promoted in a
> hostless session ([§8.3b](#83-the-zero-host-regime)). Host silence does not promote a Candidate
> the peer did not believe. The resulting Shot carries `authority: device` and is reconciled to the
> host's Shots through `ShotLink`, as [§8.3f](#83-the-zero-host-regime) requires of a Shot minted
> during a link outage.**

**(b) Raise 8.2h from SHOULD NOT to a bounded MUST**, so the host's issue window and the device's
mint window cannot overlap:

> **(8.2h) MUST** A host issues a Shot no earlier than `issue_hold_ns` after the earliest
> contributing Candidate, and no later. Issuing early locks `t0` to whichever modality happened to
> be fastest, which is not the same as whichever is most accurate, and I7 forbids correcting it
> afterwards. Issuing late overlaps the window in which a nominating peer is entitled to mint
> ([§8.2i](#82-arbitration)), and produces two Shots for one event with no defect on either side.

**(c) Add the third row to §7.1**, so the state is described rather than merely permitted:

| Hosts in session | `Session.timebase_ref` | `Shot.authority` | Arbitration |
|---|---|---|---|
| 0 | a capturing peer's timebase | `device` | **none occurs** |
| 1 | the host's timebase | `host` | the host arbitrates |
| 1, for a Candidate the host did not use | the host's timebase | `device` | none — minted under §8.2i, linked by `ShotLink` |

**(d) Rewrite `CT-I32`'s second assertion** to match: *"...and does mint after, **if and only if
its promotion policy would have promoted that Candidate hostless. Replay the same Candidate below
the peer's promotion threshold and assert no Shot is minted.**"* Without the negative half the test
certifies the defect, in the same way `CT-S4` assertion 2 did.

That is four edits, no new message type, and it reuses the promotion concept Draft 2 already added.

---

### R2 — `AchievedFrames` on the control channel: `MSG` 8.2b permits what I30 and 5.8g forbid {#r2}

**Severity: high. A straight contradiction between two normative documents, and the suite will not
catch it.**

`PPCP-CORE` I30:

> `capture_announce` carries summary capability only. Per-frame series travel with the payload they
> describe and **never on the control channel**.

`PPCP-CORE` 5.8g:

> **MUST NOT** `AchievedFrames` be carried on the control channel (I30).

`PPCP-MSG` 8.2b:

> **MAY** `capture_update` carry `achieved_frames` for a Capture whose payload will not transfer —
> a `complete` + `failed` clip whose frame timing is still worth having. **It is the only route by
> which the series reach a consumer on the control channel**, and it is a fallback, not the normal
> path (I30).

`capture_update` is on the control channel (`MSG` §8 table). So 8.2b is a MAY that a MUST NOT
forbids, and it cites the invariant it violates as its authority. An implementer coding to `CORE`
will refuse to emit what `MSG` permits; one coding to `MSG` will emit what `CORE` rejects as
non-conformant. `CT-I30` tests only `capture_announce`, so neither is caught.

**The exception is right and the invariant is what needs narrowing.** A `complete` + `failed`
capture is precisely a range session where the WiFi died: PinPointStudio still wants the frame
timeline to know what it lost, and the frames are never coming. I30's real intent is *the immediate
correlation message stays small*, not *control never carries a series*.

> **I30** — `capture_announce` carries summary capability only. Per-frame series travel with the
> payload they describe, **with one exception: `capture_update` MAY carry `AchievedFrames` for a
> Capture whose payload will not transfer ([`PPCP-MSG` §8.2b](ppcp-messages.md#82-capture_update)),
> because the series would otherwise be lost with the payload.**

and 5.8g reworded to match. `CT-I30` should gain: *"assert `capture_update` carries
`achieved_frames` only for a Capture whose `transfer` is `failed`."*

---

### R3 — `Candidate.at` has no stated convention, and 8.2a tells the host to convert it with an input it does not have {#r3}

**Severity: high. Present since Draft 1; I missed it last round. It is an I17-class silent failure —
both ends applying half a conversion — in the one place the suite does not look.**

§8.2a:

> **MUST** The host converts every Candidate into `Session.timebase_ref` using the current
> `TimebaseRelation` set **and** the canonical-instant conversion of §6.1, before comparing them.

and `MSG` Annex A.3 step 10 reinforces it: *"Conversion needs the relation **and** the
canonical-instant conversion. Either alone gives a wrong answer."*

But §6.1's conversion requires `d`, the exposure duration of **that frame**, taken from
`AchievedFrames.exposure_ns` (6.1c). **A Candidate carries no frame reference and no exposure.**
`Candidate.evidence_ref` points at a Capture, and for an acoustic candidate that Capture is the
*audio* window. There is no route from a Candidate to the exposure duration of the frame it came
from.

For an acoustic candidate this is harmless by accident: a microphone Source's profile has no
`format`, so 6.1d fixes `convention: mid` and the canonical instant is `t`. For a **motion**
candidate it is not. `basis: motion` is FLIR-side detection — one of the three nominators
REQ-SHOT-1 names, drawn in sequence A.4, and exactly what PinPointStudio's `ArbSource::Ball` is
today. Its Source is a camera whose profile declares `start` or `nominal_frame_start`, and the host
is instructed to apply `t + offset + d/2` with no `d` in reach.

So the specification is currently ambiguous between two readings, and they differ by `d/2` plus the
offset:

- the nominator emits a **raw** instant and the host converts — impossible, the host lacks `d`;
- the nominator emits a **canonical** instant and the host must not convert again — in which case
  8.2a is wrong and A.3's note is wrong.

At a 6.67 ms frame period with a 2 ms exposure the discrepancy is about 1 ms. That is comfortably
inside the 40 ms coincidence tolerance, so **arbitration still succeeds** and nothing looks broken —
and then `t0` carries a systematic 1 ms error, every capture interval is extracted against it, and
`sync_residual` (6.3h) measures the acoustic fiducial against it. The one number the phone's
microphone exists to produce is biased by the ambiguity, and it moves with exposure, which is the
signature §6.1 spends a page warning about.

#### Requested change — one clause, and it follows the pattern already in the document

The nominator is the only party that holds the frame and its exposure. It already applies the
time-of-flight correction before emitting (8.1d) and reports it so a consumer can undo it. The
canonical conversion is the same shape.

> **(5.12e) MUST** `Candidate.at` is the **canonical instant** of the observation
> ([§6.1](#61-canonical-instant)), converted by the nominating peer before emission. The nominator
> is the only party holding the frame and its exposure duration, so it is the only party that can
> convert; a consumer applying §6.1 to a Candidate a second time would double the correction. This
> is the same division as acoustic time of flight ([§8.1d](#81-nomination)): the observer corrects,
> and the correction is visible.

Then amend **8.2a** to drop the §6.1 clause — *"...using the current `TimebaseRelation` set. The
canonical-instant conversion has already been applied by the nominator ([§5.12e](#512-candidate))"*
— and fix the A.3 step 10 note to match. I would also add it to `CT-I26` or a new assertion on
`CT-S1`: *"a motion-basis Candidate from a camera Source declaring `nominal_frame_start` is emitted
canonical, and a consumer that converts it again is detected by a `d/2 + offset` discrepancy."*

---

### R4 — the scalar form is ambiguous for `intrinsics`, the one field it was added for {#r4}

**Severity: medium. Encoding-level, and it will be found by a decoder bug rather than by review.**

`PPCP-ENC` 4.1d:

> **MUST** A per-frame field in `AchievedFrames` is encoded **either** as an array of that length
> **or** as a single value of the element type... **A decoder distinguishes the two by CBOR major
> type, not by length**: a one-frame Capture still encodes an array of one.

That rule works for `exposure_ns` (major 0/1 scalar versus major 4 array) and for `iso`. It does
not work for `intrinsics`, whose element type is `Matrix3` — itself *"`[f64 × 9]`, row-major"*, a
CBOR array. Both forms are major type 4:

- scalar form: `[f64 × 9]` — one matrix, constant across the Capture;
- parallel form: `[[f64 × 9], [f64 × 9], …]` — one per frame.

A decoder following 4.1d literally cannot tell them apart, and `intrinsics` is the field most likely
to *be* constant, because REQ-OPT-2 locks focus for the whole session. The scalar form's headline
case — *"a locked exposure and locked focus collapse three of the four series to one value each"* —
is the case the rule mis-specifies.

> **(4.1d)** ...A decoder distinguishes the two by CBOR major type, not by length: a one-frame
> Capture still encodes an array of one. **`intrinsics` is the exception, because its element type
> is itself an array: there the forms are distinguished by the type of the first element — a number
> means one `Matrix3` constant across the Capture, an array means one `Matrix3` per frame.**
> `frames.ns` has no scalar form (I2).

`CT-I30`'s scalar assertion should exercise `intrinsics` specifically, since it is the only field
where the disambiguation is non-trivial.

---

## 2. Consistency items

Not defects; the kind of thing that costs an implementer an hour and a maintainer an argument.

| | Item | Where |
|---|---|---|
| 1 | **The silent-failure count disagrees in three places.** `README` says "eight silent-failure tests" and "four of the eight"; `CONF` §4 opens "Six places... three of the six"; the `README` document table still says "the six places it will silently fail". `CONF` §2c names I31 as one of the four the simulator makes testable — **but there is no `CT-S7` for it**, and §2c calls it *"most dangerously I31, where an unmeasured offset declared as `0` is correct relative to another implementation that also declared `0`."* The site the document calls the most dangerous has a matrix test and no silent-failure test. Either write `CT-S7`, or correct the count in both directions. | `CONF` §4, §2c; `README` |
| 2 | **`CT-S1` was not updated for the announce split.** Its setup still says *"Exposure deliberately varies frame to frame in `achieved.exposure_ns`"* — a field that no longer exists; it is `AchievedFrames.exposure_ns` now. More substantively, `CT-S1` tests only the varying-exposure path, and the shipping application runs with exposure **locked** (REQ-OPT-3), which is the scalar path. The conversion test does not exercise the form the product uses. Add an assertion: the scalar form and an equivalent constant array produce identical canonical instants. | `CONF` §4.1 |
| 3 | **§6.1's preamble and I17 still name `Capture.achieved`.** 6.1c was correctly updated to `AchievedFrames.exposure_ns`; the paragraph above it and I17 were not. This is the most-quoted passage in the specification and `CT-S1` asserts against it. | `CORE` §6.1, I17 |
| 4 | **10.1e states the support window as a fixed MUST rather than a floor.** *"The window is two MINOR versions back or twelve months, whichever is longer"* makes a third-party host that chooses to support five versions back non-conformant. Read "**at least** two MINOR versions back or twelve months, whichever is longer". | `CORE` §10.1e |
| 5 | **10.1e says "a peer states its support window in `hello_accept.min_version`",** but an initiator sends `hello`, which has no `min_version`. Functionally fine — the initiator's enumerated `versions` list *is* its window, and the device is the initiator (RV-c), so the window that matters is stated by the host, which is the right way round for old-app/new-host. The sentence just needs to say "a responder". | `CORE` §10.1e |

---

## 3. On the two calls I would not reverse, and one I would sharpen

**D8 — time of flight on the Candidate rather than exempting `estimated_online` from I5.** The
Capture team's first preference was the wrong one and the reasoning for declining it is right: an
exempted calibration makes `Stream.calibration_id` stop identifying a fixed value, and reproducing
a past conversion would then need a time-indexed calibration history. From the host seat this is
also the answer that costs us nothing, because a converging correction with its own sigma is
directly consumable and a stream fragmented fifty ways is not.

**D12 — promotion policy left entirely to the peer**, with the noted consequence that two
conformant devices given identical audio may report different shot counts. I am content. Two devices
with different detectors already report different *candidates*; promotion is the same class of
decision and I14 is the right home for it. The alternative — a protocol-level promotion hint — is a
threshold, and the moment it exists someone will tune the product by changing the protocol.

**D10 / B9 — `arrival_pairing` asserted without confirmation.** The call is right and I am not
asking to reverse it: there genuinely is no later moment at which arrival-order evidence improves,
and requiring a user to confirm every launch monitor row would make the feature unusable. But B9
understates the problem by one step, and the fix is nearly free.

`confirmed: bool` now carries **two different epistemic states**. Before Draft 2 it meant "a human
agreed". Now, for `arrival_pairing`, it also means "a machine asserted this live and no human will
ever be asked". A consumer reading `confirmed: true` cannot tell which it got — and that is exactly
the conflation this specification refuses everywhere else: `claimed`/`measured`/`achieved`,
`cold_sample`/`sustained`, `assumed`/`vendor`/`measured`. One boolean now does what three enums do
elsewhere.

The mis-pair is real and it is ours: PinPointStudio arms a slot on shot detection and lets the next
arriving reading claim it, displacing whatever was armed. A reading that arrives after a second
swing has displaced the slot pairs to the wrong swing, silently, and would land as
`confirmed: true`.

Suggested, in the register of the rest of the document:

> `ShotLink.confirmed_by` — `observer` \| `user`. Present when `confirmed` is true. `observer` is a
> live assertion by the peer that saw the arrival; `user` is a human decision. A consumer MUST NOT
> treat them as equivalent, and a retrospective basis ([§5.16b](#516-shotlink)) may only be
> `user`.

One optional field, and it turns B9 from "confirm the semantics against a real mis-pair" into
something a consumer can act on before the mis-pair happens.

---

## 4. Closed from my side

Recorded so nothing is left ambiguous going into implementation.

| Prior finding | Status |
|---|---|
| **F1** — I23 turned every Candidate into a Shot | **Closed.** Fixed as asked, and better: the Mint/Arbitrate contrast table gaining a *Selection* row is the part that will stop it recurring. Superseded in the live regime by [R1](#r1). |
| **F2** — no model of when a host may issue | **Closed in substance**, `issue_hold_ns` and the tolerance/deadline split are exactly right. The device-side half needs [R1](#r1). |
| **F4** — launch monitor | **Closed.** The third shape was the right call over my own fallback, and 8.1f — that a hostless session has nothing to reconcile and no product flow may promise otherwise — is the sentence I most wanted written down. `B4` reopened as asked. |
| **F5** — host capability claims | **Closed on the protocol side.** §6.1's rationale no longer depends on any estimator existing, which is stronger than what I asked for. The remaining items are the requirements document's and I accept the referral; I will carry them there. The confirmation that a peer's *internal* representation need not carry timebase identity is what I needed on record. |
| **F8** — identifiers | **Closed.** 5.1a is the minimum and the right minimum. The host mapping is ours. |
| **F3, F6, F7** | **Correctly referred.** `CT-I8`'s new same-`basis`-different-peer assertion and the new interoperability pairing are more than I asked for, and they turn our arbiter defect into something a test catches rather than a field report. |
| **Q1, Q3, Q5, Q6** | Closed, agreed. |
| **Q2** | Keep. My reservation was contingent on F2 being unfixed; it is fixed. Withdrawn. |
| **Q4** | Correctly left open. B8 asking for a floor *and* a ceiling — with the adjacent-bay case from the mobile side — is the right shape. I will contribute the intra-bay measurement from our rig when it exists. |
| **Q7** | Agreed, and the deadline moving earlier is the correct reading. `_ppcp._tcp` shipping by guess is an argument for ratifying it, not for changing it. |

**OPEN-6.** Noted that the requirements owner still has this. My position is unchanged and I will
restate it there: ship v1 offline-only rather than tethered-only. Draft 2 strengthens the case — the
zero-host path is now the one with a correct promotion model and a rewritten conformance test, while
the live path has [R1](#r1) open.

---

## 5. Sign-off

**Approved to implement**, subject to:

| | Fix | Cost | Blocks |
|---|---|---|---|
| **R1** | Condition the post-deadline mint on the peer's own promotion policy; bound the host's issue window; add the §7.1 row; rewrite `CT-I32`'s second assertion | four edits, no new message | **Mint, and `CT-I32`** |
| **R3** | State that `Candidate.at` is canonical; amend 8.2a and the A.3 note | one clause, two edits | **arbitration and `t0` correctness** |
| **R2** | Narrow I30 and 5.8g to admit the `capture_update` exception | two edits | `capture_announce` / `capture_update` |
| **R4** | Add the `intrinsics` disambiguation to `ENC` 4.1d | one sentence | the CBOR decoder |

R1 and R3 should be settled before the Mint and Arbitrate code is written. R2 and R4 can land
alongside the encoder and the capture messages, but both are cheaper now than after a fixture
corpus exists — a fixture written against the wrong `intrinsics` encoding has to be regenerated,
and fixtures are what the whole regression story rests on.

Nothing here changes an entity, adds a message, or moves an invariant number. If Draft 3 carries
these four and the five consistency items, I have no further findings and PinPointStudio will build
against it as it stands.

---

## 6. One thing worth saying

Two rounds have now found the same failure mode twice: an invariant that constrains a *choice*
rather than a *shape*, with a conformance test written to match, so the suite certifies the defect
rather than catching it. I23 did it in Draft 1. I32 does it in Draft 2, in the clause that fixed
I23's sibling.

The tell is the same both times — a MUST that names what an implementation should *decide* rather
than what its output should *look like*. `CONF` §6 already carries the antidote as a table row:
*"Which candidates a Mint peer promotes — detector tuning, and therefore I14 territory. The suite
tests the shape of the result, never the choice."* That row is the rule. It is worth promoting out
of §6 and into the section on writing invariants, because both defects would have been caught by
reading a new MUST against it before the test was written.
