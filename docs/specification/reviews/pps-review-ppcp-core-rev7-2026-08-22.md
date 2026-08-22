# Design review — PPCP-CORE revision 7

**The requirements-traceability gaps. Reviewed as owner of PinPointStudio, the host implementation.**

| | |
|---|---|
| Documents reviewed | `PPCP-CORE` 1.0 revision 7, `PPCP-MSG` and `PPCP-CONF` as amended, and `requirements-traceability.md` |
| Scope | Revision 7 only — `Annotation` and the **Markup** profile (§5.18, I37), `transfer: confirmed` and `capture_committed` (§5.14f–g, I38), `Source.optics`, `Source.viewpoint`, and the new `ContextChange` values. Revision 6's disposition of my S1–S6 checked in passing |
| Reviewer | PinPointStudio maintainer |
| Date | 22 August 2026 |
| **Verdict** | **Not yet approved for implementation.** Three findings, one of which is a direct MUST-versus-MUST-NOT contradiction between revision 6 and revision 7, and one of which makes a stated convergence property false. All three are closable with wording below; none changes an entity or adds a message. |

---

## 0. Position

**Revision 6 closed S1–S6 properly**, and two of them better than I asked. Separating a deliberately-shed
interval from a failed one — `gaps` mean loss, an `absent` segment means nothing was captured — is a
cleaner distinction than the one I proposed, and it is the one that makes the coverage rule honest.
Scoping I36 to a `complete` Session with a hole-versus-tail distinction is exactly right. And 5.11k–l
handling the derived-preview problem in `achieved`, where every other realised-versus-claimed question
already lives, is better than adding a field.

**Revision 7 is the right kind of work.** An audit that cross-checks 172 numbered requirements against
the specification and finds two MUSTs with no carriage at all is worth more than another review pass,
and `requirements-traceability.md` offering G1's two shapes with their costs rather than picking one
quietly is how that should be done. **Shape A was the right choice.** Modelling a person as a `Source`
would have put a human in the position the model reserves for instruments, and §5.18's closing
sentence — *"the cost of a separate type is one entity and one message; the cost of the alternative is
the model's spine"* — is the correct trade.

I am withholding approval on three things, and only one of them is about revision 7's design. The other
two are seams: a new MUST that contradicts a MUST from the previous revision, and a convergence claim
that does not hold under the rule stated beside it.

---

## 1. Findings

### C1 — I38 forbids eviction the specification requires elsewhere, in four separate places {#c1}

**Severity: highest. A direct contradiction with revision 6, and the mechanism is unreachable for a
whole class of Captures.**

> **(5.14g) MUST NOT** A peer evict a Capture whose `transfer` is not `confirmed`, **whatever its
> retention policy** (I38).

`confirmed` is set only on receipt of `capture_committed { capture_id, digest }`, which only a receiver
sends and which carries a digest. Four cases cannot reach it, and the first is a flat contradiction:

**(a) Preview.** Revision 6 added, one section earlier:

> **(5.11j) MUST** A preview Capture is **live-only**. A peer that cannot deliver one promptly
> **discards** it rather than queueing it…

Discarding an undelivered preview Capture *is* evicting one that is not `confirmed`. 5.11j requires it;
I38 forbids it, "whatever its retention policy". Both were added in the last two revisions, and an
implementer will resolve it by whichever section they read second. This is the clause I asked for in
S3 and the clause added by G2, meeting.

**(b) Any `absent` Capture.** It has no payload and no digest, so `capture_committed` cannot name it and
`confirmed` is unreachable. Under I38 those records are retained for ever. They are small, so the cost
is bounded — but it is a state that cannot be reached, which is exactly the defect G2 itself identified
about the old `local / sent / confirmed` triple. **And 5.11j's own remedy produces them**: a discarded
preview segment is announced as `absent` with `not_retained`, so the fix for (a) generates instances of
(b).

**(c) `already_present`.** §8.3c has a receiver that already holds a payload with the announced digest
answer `payload_abort / already_present` rather than receive it again — the rule that makes re-import a
no-op, because *"users connect twice"*. The receiver demonstrably holds it durably, and sends an abort
rather than a commit. So a re-offered Capture never becomes `confirmed` and can never be evicted, on
precisely the path built to make reconnecting safe.

**(d) Candidate audio the model already expects to be evicted.** §5.12.1c reads: *"an **evicted** or
never-retained window is `completeness: absent` with a reason"*. The model contemplates evicting these
windows; I38 now forbids it unless each has been confirmed. That matters more than the others because
the candidate count *"is not bounded by anything the user does"* (§13c), so this is the one whose
unbounded growth the privacy label has to describe. `PPCP-RV` 5.4j compounds it — a peer that withholds
candidate audio payload for want of forward secrecy leaves those Captures permanently `pending`, and
therefore permanently unevictable.

**The underlying error is scope.** I38 exists for REQ-SESS-4 — *nothing unconfirmed is evicted* — which
is about **shot payload a host has not received yet**. It was never about preview frames, absence
records, data the owner deliberately shed, or payload the receiver already holds.

#### Requested change

> **(5.14g) MUST NOT** A peer evict a Capture **that holds payload the owner has not had confirmed**,
> whatever its retention policy (I38). A Capture is evictable when any of the following holds:
> its `transfer` is `confirmed`; its `completeness` is `absent`, so there is no payload to evict; the
> receiver answered `payload_abort` / `already_present`, which asserts it holds the payload durably and
> is equivalent to a commit for this purpose; or the protocol explicitly permits the owner to shed it,
> as [5.11j](#5112-preview-streams) does for preview.

and I38 restated to match — *"A Capture holding unconfirmed payload is never evicted"*. `CT-I38` should
assert all four exits, since the test as written exercises only the first.

---

### C2 — 5.18e's convergence claim is false for concurrent edits {#c2}

**Severity: high. The clause states a property it does not deliver, and markup is the one thing in this
protocol two people edit at once.**

> **(5.18e) MUST** Supersession is by `id` plus `revision`. A peer holding revision *n* and receiving
> *n* or lower **ignores** it; receiving higher replaces. **Two peers editing concurrently converge on
> the higher revision**, and the protocol does not merge — consistent with I9.

Take the case the clause is about. Both peers hold revision 1. The coach at the host edits, producing
its revision 2; the golfer at the device edits, producing its revision 2. Each sends. Each receives a
revision 2 while holding a revision 2, and *"an equal or lower revision is ignored"* — so the host keeps
its version and the device keeps its version, **permanently, silently, and both believing they
converged**. `MSG` 9.0c restates the same rule, so an implementer meets it twice.

This is not exotic. UC-5 is a coach at the host and a golfer at the phone looking at the same shot,
REQ-MARK-2 requires content to flow both ways, and REQ-MARK-3 makes finger-drawn lines the interaction
markup exists for. Two people drawing on one shot is the expected use, not a race.

The rule is a last-writer-wins with no tiebreaker, and it needs one. Not a merge — I9 rightly forbids
that, and §5.18's stance is correct.

> **(5.18e) MUST** Supersession is by `id`, then `revision`, then `author_peer_id`. A peer holding a
> revision and receiving a **higher** one replaces; receiving a **lower** one ignores it; receiving an
> **equal** one replaces if and only if the incoming `author_peer_id` sorts higher bytewise. The
> comparison is total and identical on both ends, so two peers editing concurrently converge on the
> same annotation without merging and without either having to know who acted first.

`author_peer_id` is already mandatory on the type, so this costs a comparison and no field. `CT-I37`
should gain the concurrent case: two equal revisions from different authors, delivered in both orders,
converge on the same one.

---

### C3 — an Annotation cannot say which view it was drawn on {#c3}

**Severity: high. A drawing is view-specific, and the type carries no reference to a view.**

`Annotation` anchors with `shot_id` plus `at`, which satisfies REQ-MARK-1's *"anchored to shot ID plus
frame timestamp"* as written. It is not sufficient in a session with more than one camera, and that is
the session this protocol exists for.

A Shot in a studio has Captures from a face-on FLIR, a DTL FLIR and a phone behind the golfer (UC-2).
An alignment line or a swing-plane line drawn on the DTL image is a set of image coordinates that mean
something **on that image and nowhere else**; rendered on the face-on view it is nonsense, and rendered
on the phone's behind-the-golfer view it is worse than nonsense because it will still look plausible.
REQ-MARK-3 makes finger-drawn plane and alignment lines the flagship interaction, so this is the common
case rather than an edge.

Two further consequences follow from the same omission:

- **The timebase of `at` is unstated.** It is an `Instant`, so it names one — but nothing says whether
  it should be `Session.timebase_ref` or the timebase of the stream whose frame it anchors to. A host
  and a device will choose differently, and REQ-MARK-1 requires the artefact to *"round-trip
  losslessly"*.
- **Frame identity survives a conversion only approximately.** An annotation authored against a host
  timebase and rendered on device frames converts through a relation with a non-zero sigma, so a line
  anchored to frame *N* can land on *N±1*. Naming the stream makes the anchor exact, because the
  instant is then in that stream's own timebase and matches a frame it actually contains.

#### Requested change

> | `stream_id` | `Id` | 0..1 | The Stream whose frame this annotation is drawn on. **Present for any
> annotation whose `body` is interpreted in image coordinates.** |
>
> **(5.18g) MUST** Where `stream_id` is present, `at` is expressed in **that Stream's timebase** and
> names a frame that Stream contains. Where it is absent, the annotation is not view-specific — a text
> note or a `nav_anchor` — and `at` is in `Session.timebase_ref`.
> **(5.18h) MUST NOT** A consumer render a view-specific annotation on any Stream other than the one it
> names.

That makes `nav_anchor` fall out correctly too: a scrub target is a time, not a place, so it carries no
`stream_id` and lives in the session timebase, which is where a consumer scrubbing a timeline wants it.

---

## 2. Smaller points

| | Item | Where |
|---|---|---|
| 1 | **`body` at 64 KiB on the control channel is the one unbounded-rate control message.** A thumbnail is capped the same and is on control, but there are ~50 of those per session; annotations are interactive, bidirectional, and 5.18e resends the **whole body on every revision** — so dragging a plane line emits a stream of them on the channel that carries shot events. 5.18f already observes that *"a finger-drawn plane is a few hundred bytes; anything approaching the cap is probably a different feature"*, which argues for a much smaller cap. At minimum add a SHOULD that a peer coalesces rapid revisions and sends the latest rather than every intermediate. | §5.18, `MSG` 9.0 |
| 2 | **`Source.viewpoint.confidence` has no stated meaning for `method: declared`.** A user or installer who states "DTL" is not expressing a probability. Either say confidence is omitted or `1.0` when `declared`, or make it conditional on `classified`. As written, a peer must emit a number for which it has no basis — which is the pattern I28 and I31 exist to prevent. | §5.6, 5.6e |
| 3 | **`CT-I38` tests one exit and one refusal.** With C1's four exits it needs the others, and they are cheap: an `absent` Capture, an `already_present` response, and a discarded preview segment. | `CONF` CT-I38 |

---

## 3. What I checked and think is right

- **Shape A for `Annotation`**, and the reasoning. `provenance: user | device_advisory` collapsing G1
  and G3 into one type is better than the `ContextChange.kind: nav_anchor` the audit proposed: an
  anchor belongs to a shot, not to a session, and `ContextChange` is explicitly *"a timestamped change,
  not a per-shot attribute"*.
- **I37** — an Annotation never contributes to a Shot, Candidate, calibration or computed quantity, and
  `nav_anchor` is never phase data. That is REQ-NAV-2 and REQ-POSE-2 made structural, and `CT-I37`
  asserting it *by API surface rather than behaviour* is the right method. This is the clause that stops
  the phone's advisory anchors leaking into P1–P8, which is the line PinPointStudio cares most about.
- **5.6d — a physically distinct lens is a distinct Source.** Correct, and it settles the ambiguity the
  requirements' own implementation note raised about one device offering the same profile on both a
  wide and an ultra-wide lens.
- **5.6e — a viewpoint is a self-report carrying its method and confidence, which a consumer may
  disagree with.** Exactly the right treatment of a conclusion in a model that carries measurements.
- **Handedness as a `ContextChange` rather than a Source property.** Right: which way a golfer swings is
  a property of the session, not of a camera.
- **Location and weather as labels never computed from**, on the same footing as `Session.epoch` under
  I15.
- **`capture_committed` on the control channel**, with the reasoning that it is what releases storage on
  the other end and must not queue behind the next clip. Correct, and it is the half of the
  event/payload split that was missing.

---

## 4. Host-side position

`transfer: confirmed` is the clause with the largest host consequence in revision 7, and it is one
PinPointStudio has to be careful about rather than one that costs us much.

**We become the party that releases the device's storage.** `capture_committed` is asserted by the
receiver *"when it holds a Capture's payload durably — written and flushed, not merely received"*, and
until we send it the phone may not evict. Two things follow that I will hold us to: we must send it
against a real flush rather than against a successful `payload_end`, and we must send it for every
Capture we accept, because a Capture we quietly drop is one the device keeps for ever. A host that is
lax here fills a golfer's phone.

**It also lands on the `swing.json` question already open.** Durably committed for us means the payload
is on disk and recorded in a session store that survives a crash — which is the same persistence
boundary as the recorded-versus-derived split and the analysis provenance stamp. Those should be
settled together rather than three times.

**Markup is the one part of revision 7 that reaches our UI.** `Annotation` flowing either direction is
what makes REQ-MARK-2 real, and C2 and C3 are both about it working correctly when a coach at the host
and a golfer at the phone are looking at the same shot — which is UC-5, and the case the feature exists
for.

---

## 5. Sign-off

**Not approved for implementation as it stands.** Three findings, all closable with the wording above,
none structural.

| | Fix | Cost | Blocks |
|---|---|---|---|
| **C1** | Scope I38 to Captures holding unconfirmed payload; name the four exits | one clause, one invariant restated | **`CT-I38`, preview, and any eviction path** |
| **C2** | Tiebreak equal revisions on `author_peer_id` | one clause, no new field | **`CT-I37`, and markup with two authors** |
| **C3** | Add `stream_id`, and state the timebase of `at` | one row, two clauses | **markup interpretation, and REQ-MARK-1's round-trip** |

C1 must land before any eviction code is written, because two MUSTs currently point in opposite
directions and the storage behaviour of the whole device depends on which one wins. C2 and C3 must
land before markup is implemented on either side; both are cheap now and both are wire-visible later.

With those three carried I approve revision 7 for implementation, and PinPointStudio will build against
it.

---

## 6. Closing

Worth noting what this round is and is not. C2 and C3 are ordinary first-review findings on new text —
a convergence rule that does not converge, an anchor that does not identify what it anchors to. They
are the expected yield of adding an entity.

**C1 is the fourth instance of a pattern this document set now names in two places.** `PPCP-CORE` §11.1
records that an invariant must constrain the shape of an output rather than the choice an
implementation makes; the RV disposition records the companion habit, that a clause and its test are
edited in the same pass. C1 is a third member of that family: **a new MUST added in one revision
contradicting a MUST added in the previous one, in an adjacent section.** I36 and truncation, I32 and
promotion, RT-4 and the TLS relaxation, and now I38 and preview.

The common cause is that each fix was written against the requirement it was closing rather than against
the section next to it. The cheapest guard is the one the traceability audit just demonstrated: it found
two MUSTs with no carriage by reading requirements against the specification. **The same sweep run the
other way — every MUST added in a revision, read against every MUST already in the adjacent section —
would have caught C1 in an afternoon**, and it is the check I would run before `ppcp/1.0` freezes rather
than another review pass.
