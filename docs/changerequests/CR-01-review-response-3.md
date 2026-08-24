# CR-01 — response to the third review pass

| | |
|---|---|
| **Reviewing** | [PinPointStudio pass 3](../specification/reviews/CR-01-review-3-PinPointStudio.md) and [PinPointCapture pass 3](../specification/reviews/CR-01-review-pass3-PinPointCapture.md), both 24 August 2026, scoped as asked to the text no reviewer had seen |
| **Both verdicts** | **Ready to implement** |
| **Findings** | **Five. All accepted, all applied** — errata E43–E47 |
| **The one that had to be fixed** | **R-11** — verified here, not taken on report. It is the third pass running in which a defect arrived through a *rationale* rather than a clause |
| **Also settled** | **CR-01 question 3 is answered**, and answering it made a latent problem live (E47) |
| **Date** | 24 August 2026 |

---

## 1. R-11 — I asserted a property X25519 does not have, and it pointed at the load-bearing clause

Both teams raised this independently. I verified it rather than accepting it, because it is a cryptographic claim about my own text:

```
pk_a   675dd574ed7789310b3d2e7681f3790b466c773b1521fecf36577958371ea52f
pk_a'  87abc1e84c4c5572d2b1e63c69f5617a215518cf6261eb5a0e7db49ddad34208   ← a DIFFERENT key

X25519(sk_i, pk_a )  = 7c79d7b5f31b9aac367477f5f7c7a68b5c44cac28ed5c902a59ec48c02956a6a
X25519(sk_i, pk_a')  = 7c79d7b5f31b9aac367477f5f7c7a68b5c44cac28ed5c902a59ec48c02956a6a   ← identical, non-zero
```

**Confirmed.** [11.6b](../specification/ppcp-rv.md#116-derivation) does not fire — the agreement succeeds and `Z` is not zero. Clamping forces every scalar to a multiple of 8, so for any `T` of order dividing 8, `k·(P + T) = k·P`. 11.6b rejects a key that is *wholly* small-order; a legitimate key plus a small-order component is not that case.

⛔ **And the consequence is the part that matters.** Under that substitution:

| | acceptor | initiator, given `pk_a'` |
|---|---|---|
| `Z`, `BK`, `sid`, `PRK` | — | **all identical** |
| `sas_raw` → digits | **435948** | **485158** — differs |

**Every value [11.6c1](../specification/ppcp-rv.md#116-derivation) says needs no transcript is identical across a substituted key. The only thing separating the two peers is `sas_raw`'s explicit `pk_i || pk_a`** — the binding my sentence described as redundant with `Z`.

So a reader who believed the claim had been handed a clean argument for deleting it: *if `Z` already commits to both keys, why name them in the info string?* Doing so yields identical digits, identical MACs, an identical `PRK`, and no signal anywhere. That is [11.5c](../specification/ppcp-rv.md#115-the-exchange) and [A15](../specification/ppcp-rv.md#annex-a--decisions-and-alternatives)'s shape exactly — a removal that reads as a tidy-up and is fatal.

**Applied as [E43](../specification/ppcp-rv.md#errata-after-revision-9--change-request-cr-01-and-its-review)**, taking PinPointStudio's fix and PinPointCapture's framing of it:

- The claim is **deleted**, not repaired. Both teams independently said the first leg carries the argument alone, and it does: by the time `PRK` is derived, the comparison and both MACs have verified, and *those* bind the transcript — so two peers reaching `PRK` have already proved they agree. **The transcript is bound where it is checked, and `sid` and `PRK` are downstream of both checks.**
- **[11.6c2](../specification/ppcp-rv.md#116-derivation)** is added, giving the key binding the [11.5c](../specification/ppcp-rv.md#115-the-exchange) treatment PinPointStudio asked for: omitting `pk_i || pk_a` is forbidden, X25519 is not contributory, and **removing it is undetectable from outside.**
- **The witness is published** in §10.4 so nobody has to take it on trust. It is one call to any X25519 implementation.

⚠ Both reviews were careful to say this is **not an attack** and wants **no new check** — an interposer cannot compute `Z` without a private key, and the digits diverge so the operator catches it. The cofactor behaviour is inherent. **The defect was the sentence.** That distinction is preserved in the clause.

### The pattern, which is now three for three

PinPointCapture put it best: *"a loosely-stated rationale inside the clause that fixes one is the same failure looking for a second host."*

| | Erratum | What was wrong |
|---|---|---|
| Pass 1 | [E37](../specification/ppcp-rv.md#errata-after-revision-9--change-request-cr-01-and-its-review) | The reason a MAC fails — inverted |
| Pass 2 | [E40](../specification/ppcp-rv.md#errata-after-revision-9--change-request-cr-01-and-its-review) | A generalisation of what binds to what — over-broad |
| Pass 3 | [E43](../specification/ppcp-rv.md#errata-after-revision-9--change-request-cr-01-and-its-review) | A property claimed of a primitive — false |

**Every clause in §11 has now been reviewed more carefully than the sentences explaining them**, and the explanations are what an implementer reads first. Each of these three was written to *help*, and each read with more authority than the clause it accompanied precisely because it was offered as the safer thing to hold in mind.

---

## 2. R-12 and R-13 — the diagnostics, which matter more than their size

**R-12.** §10.4 said the sixth divergence cause was the only one producing a successful-looking pairing. **Cause 1 has the identical signature** — verified: `sid` feeds only `PRK`, so a `sid` salted before its version and variant bits are set gives digits `435948` and both MACs correct, diverging only at `PRK` (`9b779245…` against `3e351aef…`).

That paragraph exists to triage a `PSK_IDENTITY_NOT_FOUND` — **its whole readership is people whose pairing succeeded and then failed** — and it routed them to the newest cause and away from the older one that the vector's two-row `expand`/`sid` presentation was built to catch in the first place. Corrected to name both.

**R-13 / F-R9-4**, raised independently by both teams and both are right: **[RT-24a](../specification/ppcp-rv.md#9-conformance) as a `static` row asserted nothing RT-18 did not.** If a transcript were bound into `sid`, `PRK` would not match and RT-18's `PRK` row already fails; there is no state where the value matches *and* the transcript was bound. An implementer would have re-run RT-18, ticked RT-24a and gained nothing.

The two teams proposed different fixes, and both were right about different things, so both are taken:

- **RT-24a → `review`** (PinPointStudio's first option): the code binds the transcript into `sas_raw` and `K_c` and into nothing else, and does not omit the key binding. That is the assertion 11.6c1 and 11.6c2 actually want, and it is checkable.
- **[RT-24b](../specification/ppcp-rv.md#9-conformance) → `static` counter-vectors** (PinPointCapture's option 1): §10.4 now publishes **what a wrong implementation produces** — both silent-failure values and the R-11 witness. That is genuinely distinct from RT-18, it is self-checking rather than merely matching, and it is the same argument that put the little-endian misread in the diagnostic list.

PinPointCapture's `18dd04b1da8342a6b4248fb1bd2d0626` is published with its observation that it differs in the **first octet**, so one printed line settles it.

---

## 3. R-14 — the version gap: fallback made immediate, negotiation declined

Both teams confirmed the gap and **both argued against closing it with negotiation.** I agree, and the reasoning is now in [B18](../specification/ppcp-rv.md#annex-b--open-issues) rather than in a commit message:

- The fallback is [§4](../specification/ppcp-rv.md#4-rv-2--the-pairing-code), which [2a](../specification/ppcp-rv.md#2-rendezvous-paths) makes REQUIRED of everyone and which both teams measured 30/30. The consequence is *the operator scans a code once*, not an outage.
- [11.6g](../specification/ppcp-rv.md#116-derivation) and [A16](../specification/ppcp-rv.md#annex-a--decisions-and-alternatives) both hold agility on a first-contact handshake to be a downgrade surface — the argument that made R-01 blocking. **Trading a permanent attack surface for a temporary inconvenience is the wrong way round.**
- §11 has three review passes and no code. Transcript-hash negotiation would be the largest thing in the section that nothing has ever run.

⚠ **PinPointStudio's asymmetry point is the one I had not seen, and it sharpens the problem.** It runs one way only: [11.4h1](../specification/ppcp-rv.md#114-frames)'s echo means a newer *acceptor* pairs happily with an older initiator. Only the newer *initiator* is stuck — **and on this deployment the host is the initiator and the faster-moving side.** *"Host updates first, phones catch up over the following weeks"* is precisely the broken case and precisely how these two ship.

Three cheap things, all applied:

1. **[11.9d1](../specification/ppcp-rv.md#119-aborting-and-the-one-attempt-rule)** — offer the code on the **first** `unsupported_version`, not the second. A retry is guaranteed to fail identically, so 11.9d spent an operator's attempt on a certainty.
2. **The ship order is written down**: where two implementations bump `v`, **the acceptor side ships first.** Nobody would deduce that from 11.4h and 11.4h1 sitting next to each other, and it prevents the whole failure.
3. **[B18](../specification/ppcp-rv.md#annex-b--open-issues)** records the question *with the constraint on any future answer* — a bare list reintroduces the downgrade; only binding the entire offered set **plus** the selection is safe; and it needs a canonical encoding, which is the part most likely to diverge.

---

## 4. R-15 — unknown map keys, and the third instance of a class

Accepted as [E46](../specification/ppcp-rv.md#errata-after-revision-9--change-request-cr-01-and-its-review), with PinPointStudio's recommended answer: **reject.**

[11.4c](../specification/ppcp-rv.md#114-frames) enumerated unknown frame *types* and said nothing about keys, while [3.3a](../specification/ppcp-rv.md#33-txt-record), [4.2c](../specification/ppcp-rv.md#42-version-handling) and [A4](../specification/ppcp-rv.md#annex-a--decisions-and-alternatives) all point the other way — so two implementations could have resolved it differently with neither being wrong, and §10.4 could never have shown it.

⛔ **That is the third instance in three passes of the same class**: [E39](../specification/ppcp-rv.md#errata-after-revision-9--change-request-cr-01-and-its-review) (which roles a claim covers), [E41](../specification/ppcp-rv.md#errata-after-revision-9--change-request-cr-01-and-its-review) (which `v` is bound), now E46. Vectors catch divergence in *values*; they are structurally blind to divergence in *rules*, and §11 has produced one per pass. Worth carrying into whatever gets specified next.

The consequence PinPointStudio names is accepted deliberately and stated in the clause: rejecting means a future `v2` **cannot** extend a v1 frame, so B18 — if ever answered *yes* — is a v2 designed with negotiation from the start.

---

## 5. E42's attribution was wrong, and PinPointCapture corrected it against itself

Checked against both pass-1 reviews:

| Wrong figure | Actually from |
|---|---|
| `295 967 296` residues | **PinPointCapture** — verbatim from its N1 |
| the favoured set as *"the rest"* | **PinPointCapture** — same sentence |
| `2.3 × 10⁻⁷` | PinPointStudio |

**PinPointStudio's pass-1 figure was `967 296` — the right count, in the right direction.** The published paragraph took PinPointCapture's wrong count over PinPointStudio's right one. PinPointStudio then accepted the blame in pass 2 without checking, I recorded it in E42, and **PinPointCapture caught it in pass 3 and corrected the record against itself.**

Corrected in both places. An erratum about miscopied numbers had miscopied whose they were, which is funny and is also the reason it was worth fixing: **a misattributed error is one the wrong team looks for next time.**

---

## 6. Question 3 is answered — and answering it made a latent problem live

**The protocol owner has committed the deployment to host-advertises / device-dials.** [3.5e](../specification/ppcp-rv.md#35-who-advertises-and-who-browses) is satisfied and [A18](../specification/ppcp-rv.md#annex-a--decisions-and-alternatives) records it as a **decision rather than a concession**, because the reasoning is stronger than *3.5d leaves no choice*:

- A **foregrounded** application dials reliably; a **backgrounded** one cannot reliably listen — and capture requires the foreground anyway, which is [3.5b](../specification/ppcp-rv.md#35-who-advertises-and-who-browses)'s own argument.
- It matches the workflow: the host is set up first and already advertising; the device joins when it comes online, rather than the host hunting for a device that may not be listening.
- **The platform constraint and the ergonomics agree.** That is the comfortable case and is rarer than it should be.

First contact under [§11](../specification/ppcp-rv.md#11-rv-6--guided-pairing) still runs the other way — no PSK, so no constraint — which is where *"the host finds the device"* is genuinely available.

### E47 — what the answer made live

With the host committed to advertising, it becomes **the peer that accumulates pairings** — one per device that has used that bay — *and* the peer that must advertise, because [3.5d](../specification/ppcp-rv.md#35-who-advertises-and-who-browses) silences its counterparts.

[3.4d1](../specification/ppcp-rv.md#34-resolvable-identifiers) bounds the reconnection wait to *rotation period × pairings held*. ⛔ **[E27](../specification/ppcp-rv.md#errata-after-revision-8)'s mitigation for that — [3.4d2](../specification/ppcp-rv.md#34-resolvable-identifiers)'s "browse as well as advertise" — assumes the multi-pairing peer is the one that can browse, and this deployment inverts it.** Browsing gains the host nothing: there is nothing to find. So the one mitigation E27 provided is exactly the one unavailable, leaving only *recently-used-first*, which is weak where devices move between bays.

At the 15-minute floor with ten pairings that is a wait of **up to two and a half hours**. [3.4a](../specification/ppcp-rv.md#34-resolvable-identifiers) sets a floor on *frequency*, not a ceiling — so [3.4d3](../specification/ppcp-rv.md#34-resolvable-identifiers) now requires substantially faster rotation in this configuration. At twenty seconds the wait is under four minutes. It costs multicast chatter and is strictly **better** for unlinkability.

⚠ **Neither review found this** — it was not in their scope, and it only became reachable once question 3 was answered. It is flagged here because it is the one change in this round that **neither team has reviewed**, and by the standard this section has established that means it should be read before it is built on.

---

## 7. The venue problem is now in the body, not an annex

Both teams asked for this, in almost the same words, and both are right. It is now stated in [§11's preamble](../specification/ppcp-rv.md#11-rv-6--guided-pairing) where a reader will meet it:

> CR-01 was raised for a driving range. [3.6a](../specification/ppcp-rv.md#36-multicast-is-not-to-be-relied-on) says of multicast, without hedging, that **it will not work at a range.** So RV-6 reached over mDNS is a feature that works in an office and not at the venue it was requested for.

No clause changes — [3.7h](../specification/ppcp-rv.md#37-the-bootstrap-window) already permits an endpoint learned out of band, and §11 correctly constrains the handshake rather than how the endpoint was found. **What changes is that *conformant* and *solves the problem* are now visibly different states.** PinPointCapture is moving 3.7h into MVP scope and suggests PinPointStudio do the same; that is the right call, and if both do it the feature works where it was asked for. If neither does, it does not.

---

## 8. Where this leaves it

**From both teams: ready to implement.** Fifteen findings across three passes, all applied. §10.4 reproduces on two independent implementations, now with counter-vectors as well.

**Still open, and none of it is drafting:**

- **[B17](../specification/ppcp-rv.md#annex-b--open-issues) is the first code change the programme needs**, and PinPointStudio is right that the ordering is forced: `libppcp` has no crypto dependency and cannot implement §11 until the X25519 seam exists, and both applications build against it. **libppcp first.**
- **[B14](../specification/ppcp-rv.md#annex-b--open-issues)** — the iOS **device** run, a ship gate.
- **[B18](../specification/ppcp-rv.md#annex-b--open-issues)** — negotiation, deliberately unanswered.
- **[E47](../specification/ppcp-rv.md#errata-after-revision-9--change-request-cr-01-and-its-review) is unreviewed**, per §6 above.

⛔ **[RT-20](../specification/ppcp-rv.md#9-conformance) has not moved, and three passes should not be allowed to feel as though it has.** There is one encouraging thing, which PinPointStudio found: the relay must be an **acceptor** toward one peer and an **initiator** toward the other, so **building it necessarily produces the third implementation carrying both roles** — the slack the role-note says does not exist. That is an argument for building it early rather than last. Until it runs, no conformance claim to RV-6 should be made, which both teams have now said, unprompted, three times.
