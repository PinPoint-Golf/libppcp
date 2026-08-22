# Design review — continuous streams during capture

**`PPCP-CORE` 1.0 revision 5. Reviewed as owner of PinPointStudio, the host implementation.**

| | |
|---|---|
| Scope | Revision 5 only: `Capture.anchor` gaining `{ stream: true }` ([§5.11.1](#), [§5.14](#)), **I36**, I27 as amended, and `preview` Streams ([§5.11.2](#)). Plus `PPCP-MSG` 8.1c/8.1f/8.1g and `CT-I27`/`CT-I36` |
| Reviewer | PinPointStudio maintainer |
| Date | 22 August 2026 |
| Verdict | **The design is right and I want it.** Six findings, all in the new machinery. Two are in the coverage rule and would misreport honest data as a defect; one has a storage and bandwidth consequence the offline path cannot absorb. None needs a new message or a new entity. |

---

## 0. Position

This closes the gap I should have found two rounds ago and did not. `continuity: continuous` has been
in the model since Draft 1, three separate obligations depended on it — continuous attitude and
gravity, the raw sensor-arrival evidence a bundle must carry, `imu`/`wrist` while armed — and every
payload message was keyed on a `capture_id` belonging to a Shot or a Candidate. The flag had nothing
behind it. Adding a third anchor form rather than a fourth message is the right shape, and
`{ stream: true }` reusing the whole announce-and-payload path means the host gains no second ingest
route.

**`preview` matters more to PinPointStudio than the rest of revision 5 combined**, and the reasoning
in §5.11.2 is exactly right: heartbeat proves the link is up, only frames prove it reflects what the
user is doing. Framing validation (REQ-SETUP-1) is the case where a golfer places the device badly
and does not know it, and a coach at the host cannot fix that from a thermal state and a settled
flag. Making it an ordinary Stream from an existing Source, with no new message and no new
machinery, is what makes it cheap enough to actually ship.

Three provisions I would defend if they come under pressure later: **5.11g** — preview is never used
for measurement, pose, arbitration or any quantity that reaches a result; **5.11i** — preview
degrades before transfer, which degrades before capture; and **I36**'s central claim, that time
accounted for by neither a Capture nor a declared gap is a defect rather than a dropout. That last
one is the whole value of the coverage rule and it is also where two of my findings are.

---

## 1. Findings

### S1 — there is no way to assert that a continuous Stream recorded nothing {#s1}

**Severity: highest. It is the exact case I36 exists to distinguish, and the model cannot express it.**

Two clauses conflict:

- **§5.14, `interval` row** — *"In the Stream's timebase. **Absent when `completeness: absent`**."*
- **§5.14d MUST** — *"`{ stream: true }` is permitted only on a Stream whose `continuity` is
  `continuous`, and **`interval` is then mandatory** — a segment with no interval says nothing about
  what it covers."*

A stream-anchored Capture with `completeness: absent` must therefore both carry and not carry an
interval. That is not a drafting nuisance; it is the only route to the statement the coverage rule
most needs.

**The concrete case.** A phone is armed at a range with a continuous `imu` Stream. Storage fills, or
the BLE link drops, and nothing is recorded for four minutes. Then it resumes. Under §5.11c the
whole open interval must be accounted for by Captures and declared gaps — but:

- there is **no Capture covering those four minutes**, so there is nothing to hang a `gaps` entry
  on. `gaps` is a field *of a Capture*, and gaps are naturally within that Capture's interval;
- an **`absent` Capture spanning them** is the obvious answer, and 5.14d and the `interval` row
  disagree about whether it is constructible.

So the peer knows exactly what happened, wants to assert it, and I36 turns its honest silence into
*"a defect, not a dropout"* — reporting an implementation error where there was a storage limit.
This is precisely inverted from I10's principle, which the same clause cites: absence is asserted,
never inferred.

#### Requested change

Resolve in favour of 5.14d for the stream anchor, and say so where the contradiction is:

> **`interval`** — In the Stream's timebase. Absent when `completeness: absent`, **except on a
> stream-anchored Capture, where it is always mandatory (5.14d): for a segment the interval *is* the
> claim, and an `absent` segment with an interval is how a peer asserts that a stated span of a
> `continuous` Stream was not recorded, with `absent_reason` saying why.**

That gives a continuous Stream two ways to account for lost time and makes the difference
meaningful: a **gap** is a dropout inside a segment that otherwise exists, an **`absent` segment**
is a span where nothing was captured at all. `CT-I36` should assert both, and assert that an
`absent` segment satisfies the coverage rule rather than breaching it.

---

### S2 — I36 reads a legitimately truncated bundle as a defect {#s2}

**Severity: high. The two rules were written in different documents and meet on the offline path,
which is the path v1 ships.**

§5.11c requires accounting *"from `opened_at` to `closed_at`, or to the present"*, and I36 makes
unaccounted time a defect. §5.11d correctly scopes this to **announced** Captures rather than
arrived payload, which handles a lagging transfer.

It does not handle a **truncated bundle**, and `PPCP-ENC` §7d contemplates one explicitly:

> **(7d) MUST** A truncated final frame means the bundle is incomplete. The reader treats the
> Session as `completeness: partial` only if the bundle itself did not assert otherwise, and never
> upgrades a partial Session to complete.

A bundle truncated mid-write — a phone that ran out of battery, a transfer interrupted at a range —
loses the announces at its tail along with everything else. The Stream has an `opened_at` and no
`closed_at`, the last announced segment ends well before the session did, and the unaccounted
remainder is the truncation. I36 calls that a defect. The host is then required to report an
implementation error for a session that is honestly and explicitly `partial`.

This matters for us specifically because **the bundle is the v1 path** and because
`Session.completeness` is asserted, not inferred (I10, 5.10d) — the machinery to distinguish the two
cases already exists and I36 simply does not consult it.

#### Requested change

Scope the coverage obligation to what the owner claimed:

> **(5.11c) MUST** … account for the whole of the Stream's open interval. **The obligation binds a
> Session asserted `completeness: complete`. In a `partial` or `unknown` Session, time after the
> last announced Capture is the incompleteness the Session already declares, not a defect; time
> unaccounted for *between* announced Captures is a defect in either case.**

The between-versus-after distinction is what keeps the rule sharp: a hole in the middle is still a
defect, because nothing truncates a bundle in the middle. `CT-I36` should gain a truncated-bundle
case, since it is a `fixture` test and a truncated fixture costs nothing to produce.

---

### S3 — preview Captures will be queued and persisted, which is the opposite of what §5.11i intends {#s3}

**Severity: high, and it is the one with a product consequence rather than only a specification one.**

§5.11i and `MSG` 8.1g are clear about priority: preview degrades first, and *"a preview frame that
arrives late is worth nothing; a clip that arrives late is worth everything."* Nothing, however,
says what becomes of a preview Capture that could not be sent.

Every announced Capture has a `transfer` state, and `pending` is where one starts. A conformant
implementation that announces a preview segment and cannot deliver it will therefore **queue it**,
because that is what `pending` means and because `REQ-SESS-6` requires bulk transfer to be queued,
resumable and backpressure-aware. Two consequences follow, and the second is worse:

1. **The queue fills with the cheapest thing in the session.** A preview backlog competes for the
   same bulk capacity as shot payload, which is exactly the inversion 5.11i exists to prevent —
   arriving through the transfer queue rather than through the channel.
2. **It reaches the bundle.** §9a is emphatic that an exported session *is* the recorded message
   stream, so segments that were announced and never delivered are part of it. A preview running for
   a 90-minute range session at even a modest rate is a substantial fraction of the ~1 GB that
   §16.2 budgets for **shot video** — spent on frames 5.11g forbids anyone from using for anything.
   `REQ-OFF-2` refuses to arm below a storage floor; this is the one stream that could push a
   session under it while contributing nothing.

The link-drop case (§8.3f) makes it concrete: the peer continues, queues Captures as
`transfer: pending`, and under the rules as written a preview opened before the drop keeps
producing them.

#### Requested change

> **(5.11j) MUST** A preview Capture is **live-only**. A peer that cannot deliver it promptly
> discards it rather than queueing it, and MUST NOT retain it for later transfer or write it to a
> bundle. A discarded preview segment is a declared gap on the preview Stream, which is the honest
> account and costs one message.
> A consumer therefore never sees `transfer: pending` on a preview Capture.

This is 5.11i's intent stated where an implementer will act on it, and it also makes the coverage
rule work on a preview Stream instead of making it expensive.

---

### S4 — 5.8d requires per-frame exposure on preview Captures, whose use 5.11g forbids {#s4}

**Severity: medium.**

> **(5.8d) MUST** On a Capture from a camera Source, `AchievedFrames.exposure_ns` is present, in
> parallel or scalar form. Without it the canonical-instant conversion is impossible (I17).

> **(5.11g) MUST NOT** A `preview` Stream be used for measurement, pose, arbitration, or any
> quantity that reaches a result.

A preview Stream is a camera Source, so 5.8d binds it. So every preview segment carries per-frame
exposure durations for a conversion that another MUST forbids performing. The justification given —
*"without it the canonical-instant conversion is impossible"* — is precisely the reason it is not
needed here.

The scalar form limits the cost where exposure is locked, but a preview may reasonably run its own
exposure while the capture profile stays locked, and then it is a parallel array per segment on the
stream that is supposed to be the cheapest thing in the session.

> **(5.8d)** … **`AchievedFrames` is OPTIONAL on a `preview` Stream, which by 5.11g never reaches a
> conversion.**

---

### S5 — a concurrent preview makes `MeasuredCapability` optimistic, and nothing says so {#s5}

**Severity: medium. It is I28's own reasoning, applied to a concurrency revision 5 has just made
reachable.**

`MeasuredCapability` is a self-test result attached per capture profile, and I28 exists so that a
figure cannot be presented as something it is not — a cold three-second sample must declare
`method: cold_sample` rather than pass as sustained. 5.8b puts it plainly: *"a consumer MUST NOT
treat it as a sustained figure."*

Every such measurement is taken with **that profile running alone**. A preview Stream is a second
concurrent encode on the same hardware encoder, thermal budget and bulk path. The moment a consumer
opens one, the device is doing measurably more work than when it measured — and `sustained_rate_mhz`
still reads as though it were not.

The order of events makes it reachable rather than theoretical: a host reads `measured`, accepts the
device under its ingest policy, and *then* opens a preview. The acceptance decision was made against
a figure the subsequent action invalidated. `AchievedCapability` reports the truth per shot, so it
surfaces eventually — as dropped frames on real swings.

> **(5.8j)** A `MeasuredCapability` describes its profile **running alone** unless the peer states
> otherwise. A peer SHOULD re-measure, or qualify the figure, where it expects to run a `preview` or
> any second Stream from the same Source concurrently. This is 5.8b's principle applied to
> concurrency rather than to thermal state: the number is honest only about the conditions it was
> taken under.

---

### S6 — a preview profile is a sibling in the model and a derivative in the hardware {#s6}

**Severity: medium. A declaration that is true alone and false alongside.**

§5.11f describes a preview Stream as *"a second Stream from an existing Source, with its own
`profile_id` — typically a low rate and a small frame"*, activated from that Source's declared
profile set like any other.

No camera runs two configurations at once. A phone capturing 1080p150 cannot simultaneously operate
a 640×360@10 mode; the preview is produced by **decimating and downscaling the active capture
stream**. So while a capture Stream is open, the preview profile is not a mode the Source operates
in — it is a derivative of the one it is already in. `CaptureProfile` is defined as *"a mode a Source
can operate in"*, and `measured` attaches per profile, so a preview profile carries a
`MeasuredCapability` that has no independent meaning while capture is running.

The nuance that makes it worth stating rather than forbidding: a preview **alone** — during setup and
framing, before arming — genuinely is an independent mode, and that is its main use. So the same
declared profile is independently activatable in one situation and derived in the other, and nothing
tells a host which it is getting.

The practical question a host will ask and cannot currently answer: *if I request 1080p150 capture
and a 640×360@10 preview, do I get the rate I asked for on the preview, or the nearest decimation of
150?*

> **(5.11k)** Where a `preview` Stream is open alongside a capture Stream from the same Source, its
> realised rate and format are **derived** from the active capture profile — a decimation, a
> downscale, or both — and its declared profile is a request rather than an independent mode. The
> `AchievedSummary` on each segment reports what was actually produced. Where a `preview` Stream is
> the only Stream open on that Source, its profile is activated normally.

That keeps the declaration honest in both situations without adding a field, and it puts the answer
in `achieved`, which is where every other realised-versus-claimed question in this specification is
already answered.

---

## 2. Consistency items

| | Item | Where |
|---|---|---|
| 1 | **`preview` is missing from the `Stream.kind` enumeration.** The field row reads `video \| audio \| imu \| wrist \| event \| metadata \| …`, and `preview` appears only in the continuity table below it and in 5.11f–k. It is an open registry so it is legal, but it is now normatively referenced in five clauses and a message rule, and an implementer building the enum from the field row will not have it. | §5.11 |
| 2 | **Two different `evidence_ref` fields mean different things.** `TimebaseRelation.evidence_ref` is *"Stream carrying raw evidence"*; `Candidate.evidence_ref` is *"A Capture id"*. Same name, different entity, and revision 5 makes the first one actually usable for the first time — so it will now be implemented, by someone reading the other one's definition. Worth renaming one, or at minimum saying in both rows which entity is referenced. | §5.4, §5.12 |
| 3 | **`CT-I36` does not exercise an `absent` segment or a truncated bundle**, which are [S1](#s1) and [S2](#s2). It is a `fixture` test, so both cases are cheap to add and neither needs a device. | `CONF` CT-I36 |

---

## 3. Host-side position

Recorded as in previous rounds, so the cost is visible rather than discovered.

**This is the change that makes the phone useful to PinPointStudio before analysis exists.** A
device that can only deliver clips retrospectively is invisible during a session; one that can carry
a preview and continuous attitude is something a coach can set up against. I would take revision 5
ahead of several things ranked above it.

**The good news is that the seam already exists.** A stream-anchored Capture arriving as a rolling
sequence of segments is the same shape `deferred_sources_design.md` already solved for a wrist
sensor: `RamPayloadSource` holds an id-keyed sample vector, `CompositePayloadSource` routes by
`SourceId`, and `SwingDiskLoader` already synthesises index rows from timestamps and sorts them.
Slicing continuous segments to a swing window is that machinery pointed at a different producer. The
piece still unbuilt is the one that design note names — `EventBuffer::reserveSourceId()` for a
source that was **never live**, which it explicitly anticipates for *"a phone-hosted sensor"*.

**Preview does not enter the buffer at all**, and should not. It is a display surface, it is barred
from measurement by 5.11g, and PinPointStudio should route it nowhere near `SwingWindow`. I want
that stated on our side before someone notices there is a video stream arriving and wires it in.

**One thing I will hold us to.** 5.11i puts preview last in the degradation order, and our own
priority rule (`REQ-RES-1/2`, capture degrades last) says the same. The temptation when a preview
stutters will be to give it more of the bulk path. The answer is that a stuttering preview is the
system working correctly.

---

## 4. Sign-off

**Approve.** Six findings, none structural.

| | Fix | Cost | Blocks |
|---|---|---|---|
| **S1** | Make `interval` mandatory on a stream-anchored Capture including when `absent`; say an `absent` segment satisfies coverage | one row, one clause | **I36 and any honest dropout report** |
| **S2** | Scope the coverage obligation to a `complete` Session; distinguish holes from truncation | one clause | **the bundle path, which is v1** |
| **S3** | Preview Captures are live-only: never queued, never bundled | one clause | **storage and the transfer queue** |
| **S4** | `AchievedFrames` OPTIONAL on a preview Stream | one sentence | — |
| **S5** | `MeasuredCapability` describes its profile running alone | one clause | host ingest policy |
| **S6** | A preview alongside capture is derived, and `achieved` reports what was produced | one clause | — |

S1 and S2 should land before `CT-I36` is written, for the reason that has now recurred four times
across these documents: a test written from a rule that does not cover the honest case certifies the
wrong behaviour. S3 should land before any implementation opens a preview stream, because the
default state of an announced Capture is `pending` and the queue will do the wrong thing without
being told not to.

Once these land I have no further findings on revision 5.
