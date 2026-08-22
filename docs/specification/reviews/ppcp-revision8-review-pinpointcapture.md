# PPCP revision 8 — review from the PinPointCapture mobile app

| | |
|---|---|
| Reviewing | `PPCP-CORE` revision 8 and the round-4 disposition |
| Seat | Owner of the PinPointCapture iOS/iPadOS app |
| Date | 22 August 2026 |
| Verdict | **Approve for implementation.** Two findings, both in revision 8's own new material, and one of them is an instance of the pattern §22 has just made a mandatory check for. |

Both of my round-3 findings are closed, and PPS-C1 — I38 forbidding the eviction 5.11j *requires* — is a better version of a connection I had in front of me and did not make: I raised the preview-discard rule and the `confirmed` gap in the same review, in adjacent sections, and did not notice they contradicted each other. The host's framing of that as a recurring pattern, and the sweep in `CONF` §5b2, is the most valuable thing in this round.

I ran that sweep against revision 8's own changes. It found the following.

---

## 1. `Annotation.body`'s cap contradicts itself inside one section

Revision 8 lowered the cap from 64 KiB to 8 KiB on both teams' finding. **The normative clause was updated and the field table was not:**

| Where | Says |
|---|---|
| §5.18 field table | `body` … Opaque to the protocol, **at most 64 KiB** |
| §5.18f | **MUST NOT** `body` exceed **8 KiB** |

An implementer reading the entity definition sizes a 64 KiB buffer and validates against 64 KiB; one reading the clause validates against 8 KiB. The disposition's own summary says 8 KiB, so the intent is not in doubt — the table is simply stale.

**Worth noting where this sits relative to §22.** The adjacent-MUST sweep is described as checking a new MUST against *the section next to it*. This contradiction is between a normative clause and **the field table in the same section**, which a sweep scoped to adjacent sections would step over. If the check is being written down as a procedure, it is worth saying that a clause changing a bound must be reconciled with the entity definition that states it — that is where a cap actually lives for most readers.

## 2. `Annotation.kind` and `Annotation.format` are not registries

§5.18's field table calls `kind` an *"Open registry — `line`, `plane`, `text`, `nav_anchor`, …"* and types both `kind` and `format` as `Kind`. But §10.3 enumerates the open registries explicitly:

> `Source.kind`, `Stream.kind`, `Candidate.basis`, `Calibration.kind`, `ContextChange.kind`, `ShotLink.basis`, `ClockDiscontinuity.cause` and `Capture.absent_reason`

**Neither annotation field is in that list.** So 10.3b's reverse-DNS namespacing and 10.3c's reservation of unprefixed values do not formally apply to them — and markup is, of everything in this protocol, the thing a third party is most likely to want to extend, because it is the only user-facing content type.

Without 10.3b here, the failure it exists to prevent applies exactly: *"the first third party to add a sensor type either collides with a future core value or forks the protocol."*

**Suggested:** add `Annotation.kind` and `Annotation.format` to §10.3's list. One line, and it is the same fix the audit applied four times over for `ContextChange.kind`.

### 2.1 The concrete version of this, from our own screens

This is not hypothetical for us at v1. Our replay screen ships **three markup tools: line, circle and freehand.** The registry names `line`, `plane`, `text` and `nav_anchor`.

- **`circle`** has no defined value.
- **`freehand`** has no defined value.
- **`format`** has no defined values at all, for any kind.

So two of our three shipping tools, and the field that says how to read any of them, will be invented locally — which is precisely the defect class the traceability audit found four times (G3, G5, G6 and the launch-monitor finding before them): *an open registry made the requirement expressible, but no value was defined, so two implementations diverge.*

Defining `circle`, `freehand` and a small `format` vocabulary now costs almost nothing. Discovering it after PinPointStudio and PinPointCapture have each chosen costs a migration of stored user artefacts, which are the one thing in the system a user would notice losing.

### 2.2 And it interacts with the 8 KiB cap

5.18f's justification is *"a finger-drawn plane is a few hundred bytes and a text note less"*. That reasoning holds for `line`, `plane` and `text`. It does not obviously hold for **freehand**, which is the tool the cap was not reasoned about: a stroke sampled at touch rate for a few seconds is kilobytes, and a drawing of several strokes is a multiple of that.

Which raises a question the specification does not answer: **is one Annotation one stroke, or one drawing?** Under supersession-by-revision (5.18e) a drawing is naturally one artefact that grows as the user adds to it — and then 8 KiB binds, on the control channel, in exactly the case 5.18i's coalescing rule was written for. If one Annotation is one stroke, 8 KiB is generous and a drawing is several artefacts with no grouping.

I do not have a strong preference; I would like the answer stated, because it determines both the cap and whether our undo behaviour is per-stroke or per-drawing.

---

## 3. Confirmations

- **PPC-1 closed with both halves.** 5.14h gives the bundle path a route to `confirmed`, and 5.14i states the honest alternative — where there is no receiver, I38 protects nothing and retention is the peer's policy. I offered those as alternatives; taking both is right, because they cover different situations rather than the same one twice.
- **I38 rescoped to payload, with four named exits.** Correct, and (b) — an `absent` Capture having no payload to name — is the one I would have missed again.
- **5.18g adding `stream_id`.** The host's finding and a good one. Image coordinates from a down-the-line view rendered on a face-on view being *plausibly* wrong rather than obviously wrong is exactly the failure mode that survives testing.
- **`Source.viewpoint.confidence` present iff `method: classified`.** Right for the reason given: a person stating "down the line" is not expressing a probability, and requiring one would ask a peer to invent it.
- **The adjacent-MUST sweep finding a fifth instance on its first run** (I8 against 5.12.1c) is the strongest possible argument for the check. Separating the evidence *record* from the evidence *payload* is the same move as I38's rescoping and is consistent.

---

## 4. Summary

| # | Finding | Severity |
|---|---|---|
| 1 | `Annotation.body` cap: field table 64 KiB, clause 8 KiB | **Fix** — trivial, but it is the number an implementer sizes a buffer from |
| 2 | `Annotation.kind` and `format` absent from §10.3's registry list | **Fix** — one line; markup is the most likely third-party extension point |
| 2.1 | `circle` and `freehand` undefined; our v1 ships both | **Define now** — same class as G3/G5/G6, and stored user artefacts are what a migration would cost |
| 2.2 | Is one Annotation one stroke or one drawing? | **State it** — determines whether 8 KiB binds |

**Approve for implementation.** None of these blocks starting: markup is behind capture, transfer and the session store in any sensible build order, and all four are drafting rather than design. We build against revision 8 as it stands.
