# `PPCP-RV` §11 — fifth-pass review from PinPointCapture

| | |
|---|---|
| **Scope** | E49–E52, and **RT-20a run for the first time** |
| **Against** | `libppcp@eb28fcc` |
| **From** | PinPointCapture, 24 August 2026 |
| **Verdict** | **No objection to implementation.** Two findings, both on the same number, both from running RT-20a rather than reading it. One admission |

---

## 1. RT-20a is run. It is the first thing in this programme that touches the security property

E50's split was right, and the proof is that the arithmetic half ran the same afternoon it was written — after four passes reporting RT-20 as unmoved.

**200,000 random quadruples** — an initiator, an acceptor, and the two keys an interposer holds — both legs' digits derived through 11.6c and compared:

```
trials                 200 000        (35 s, 5 700/s, CryptoKit on macOS 27)
legs colliding         0
expected at 1.0e-6     0.200
uniformity chi²        933.6 over 999 dof     (expect 999 ± 45)
```

Zero collisions, and the displayed digits are uniform. ⛔ **11.5c's ordering is what makes this meaningful and RT-20a cannot see it** — the run assumes both legs were derived from keys chosen blind. That is RT-20b's, and RT-20b needs the relay.

---

## 2. F-R9-6 — the bound is 1 in 1 000 000, not 1 in 1 048 576 ⚠ new

> **§11.8** — *"pick both keys blind and hope the two legs collide — **one chance in 1 048 576**"*
> **§7.1** — *"One guess in 1 048 576, per operator confirmation"*

**The SAS is six decimal digits, so there are 10⁶ outcomes, not 2²⁰.** `sas_raw` is 32 bits, but 11.7a reduces it mod 1 000 000 before anyone sees it, and the attacker has to match what is *displayed*.

Exactly, including the modulo bias:

```
p(collision) = 144115188323 / 144115188075855872
             = 1.000 000 001 7 × 10⁻⁶      = 1 in 999 999.998
2⁻²⁰         = 9.536 743      × 10⁻⁷      = 1 in 1 048 576
```

⛔ **The real bound is 4.86% weaker than the one stated.** Immaterial to the security argument — one in a million and one in 1.05 million are the same claim in every way that matters, and §11.8's actual force is the single attempt, not the width.

**It is material to RT-20a**, which asks an implementer to *"assert the collision rate matches §11.8's 2⁻²⁰ within sampling error"*. That instructs a conformance test to check against a number 4.86% away from the truth.

⚠ **One good piece of news inside it.** The modulo bias contributes `1.7 × 10⁻⁹` relative — it cancels to seven significant figures against uniform-over-10⁶. E42 spent a whole erratum correcting the bias arithmetic; this says the bias does not reach the security bound at all, which is what that paragraph claimed and could not previously show.

**Suggested:** replace `1 048 576` with `1 000 000` at §7.1 and §11.8, and let the *"twenty bits"* framing in the six-digits paragraph stand as the design rationale it is — Bluetooth and ZRTP settled on twenty bits, this settled on six digits, and the two are close but not equal.

---

## 3. F-R9-7 — RT-20a's rate assertion is not falsifiable at any practical sample size ⚠ new

Even with the corrected target, *"assert the collision rate matches within sampling error"* cannot be run as written. Distinguishing 1.0 × 10⁻⁶ from 9.54 × 10⁻⁷ needs:

| trials | expected collisions | σ | separation |
|---|---|---|---|
| 10⁷ | 10 | 3.2 | 0.15 σ |
| 10⁸ | 100 | 10.0 | 0.46 σ |
| 10⁹ | 1 000 | 31.6 | **1.46 σ** |
| 4 × 10⁹ | 4 000 | 63.2 | **2.93 σ** |

At the 5 700 quadruples/second measured above, 10⁹ trials is **two days** of continuous X25519 and still under 1.5 σ. A conformance row nobody can run is a row that gets ticked.

⛔ **What the run can establish, and did:**

1. **The legs differ** across a large sample — the property, and the half worth asserting.
2. **The digits are uniform** — χ² 933.6 over 999 dof. This is what the 10⁻⁶ figure *rests on*, and unlike the rate it is measurable at 200 000 trials in half a minute.

**Suggested rewording:** assert the analytic bound rather than estimating it; assert zero collisions over a stated run; and assert uniformity by χ². Something like —

> Over a large run of random quadruples assert **no collision occurs**, and assert the displayed digits are **uniform** over 10⁶ by a χ² test — which is the property the 1-in-10⁶ bound is computed from. ⚠ The rate itself is not estimable: separating 10⁻⁶ from a 5% neighbour needs ~10⁹ trials.

That keeps E50's intent — a run that *measures* rather than restates — and points it at the quantity a run can actually reach.

---

## 4. ⛔ An admission: we reviewed E47 and passed it

E49 corrects E47 on two counts, and **we accepted E47 in the third pass.** Our review said the arithmetic was verified and the inversion argument correctly identified — both true, and both beside the point. We did not check either qualitative claim:

- *"costs only multicast chatter"* — false. 3.2a ties the instance name to `rid`, so each rotation **renames the service**: deregister, probe, announce. At seconds-scale on the networks 3.6a describes, **the mitigation would have triggered the condition it was mitigating.**
- *"strictly better for unlinkability"* — false. Address and port are stable within a registration, so an observer links the rotations by inspection. Faster rotation is neutral.

⚠ **This is the third time today.** The modulo-bias figure in our own N1 was wrong and got quoted into the specification; §10.4's little-endian example we caught only after E42 moved the value underneath it; and now E47. Each time we recomputed every number that was presented *as* a number and read past the claims stated as prose. Two implementations reproducing thirteen vector rows four times is worth exactly nothing against a sentence.

We have no process fix to offer beyond naming it. It is the same lesson the response generalised after R-09 and it has now caught us three times, which suggests the generalisation was right and under-weighted.

---

## 5. The rest — accepted

**E51 (our F-R9-5).** Accepted, and extended further than we asked: 11.6f's list now names `PRK`, `K_tls`, `K_id` and `sid` on the failure path. ⚠ Extending the list rather than relying on the closing sentence is the right call — the list is what gets implemented from, which was the whole finding.

**11.11h1.** The right disposition. Recording that on one platform the erasure MUST is partly an obligation on a closed-source framework is exactly the 5.4 treatment, and we would rather it be written down than assumed away.

**E52 / 9g.** ⛔ **The most important thing in this pass, and we endorse it without reservation.** *"A protocol can have flawless arithmetic and no security at all"* is the sentence this whole programme needed. Twenty findings and four vector reproductions is a great deal of green and none of it touched the property §11 exists to deliver — a fact that was invisible until 9g forced it to be stated.

⚠ **It binds us concretely.** This repository's conformance claim reports aggregate pass counts at the top of the document. Under 9g that is not permissible for RV-6 while RT-20c is unrun, so the claim will carry a named RT-20c row reading **unrun** and no RV-6 aggregate. We will not report a §11 pass before the relay exists.

**E50's ordering point — the relay is the first artefact, not the last.** Agreed and adopted. Both security-touching tests depend on it and it needs no application to exist, so we will build it in `libppcp/tools` before implementing our side rather than after.

**Channel 255 and `ppcp_channel_validate()`.** Endorsed strongly. The rejection *is* the fail-closed property, an implementer meeting it will be tempted to relax the validator, and every test would still pass. Stating it in 11.4a is right; a separate write path is the answer.

---

## 6. Where we are

⛔ **No objection to starting.** Five passes, twenty-two findings, and the first execution of the only test that touches the security property — which found nothing wrong with the property and two things wrong with the number describing it.

What remains untested is unchanged and correctly named by 9g: **RT-20b and RT-20c, both of which need the relay.** Everything else about §11 is now either measured or measurable.

⚠ From our side, `PPCP-RV` B14 is fully discharged: X25519 through CryptoKit ran **on an iPhone 16, iOS 26.6**, this afternoon. RFC 7748 §6.1 reproduces, §10.4's `Z` matches, and all three small-order public keys are rejected by **throwing** rather than by returning an all-zero `Z` — 11.11f's "throw half", confirmed on hardware rather than on a proxy.
