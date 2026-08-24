# CR-01 — response to the fifth review pass, and closure

| | |
|---|---|
| **Reviewing** | [PinPointCapture pass 5](../specification/reviews/CR-01-review-pass5-PinPointCapture.md) (with [`RT20a.swift`](../specification/reviews/CR-01-review-pass5-RT20a.swift)) and PinPointStudio's four closing items, 24 August 2026 |
| **Both verdicts** | **No objection to implementation.** Four paper items, no new design |
| **Findings** | **Five. All accepted, all applied** — errata E54–E55 |
| **Closed** | [B14](../specification/ppcp-rv.md#annex-b--open-issues) on hardware, [B15](../specification/ppcp-rv.md#annex-b--open-issues) and [B18](../specification/ppcp-rv.md#annex-b--open-issues) as decided |
| **Status** | ⛔ **CR-01 has no open specification items.** What remains is evidence, and [9g](../specification/ppcp-rv.md#9-conformance) governs it |

---

## 1. RT-20a was run, and it found the number wrong

**This is the first thing in the whole change request that touched the security property**, and it happened the same afternoon [E50](../specification/ppcp-core.md#errata-after-revision-9) split the row — after four consecutive passes reporting RT-20 as unmoved.

```
trials                 200 000        (35 s, 5 700/s, CryptoKit on macOS 27)
legs colliding         0
uniformity χ²          933.6 over 999 dof     (expect 999 ± 45)
```

**The property held. The number describing it did not.**

### The bound is 1 in 1 000 000, not 1 in 1 048 576

Both teams found this independently — R-21 and F-R9-6 — and computed the same exact fraction. I recomputed it rather than taking it:

```
p(collision) = 144115188323 / 144115188075855872  =  1.000 000 001 7 × 10⁻⁶   =  1 in 999 999.998
2⁻²⁰                                              =  9.536 743     × 10⁻⁷    =  1 in 1 048 576
```

**2⁻²⁰ is the width of `sas_raw`. It is not the width of what anybody sees.** [11.7a](../specification/ppcp-rv.md#117-the-short-authentication-string) reduces mod 10⁶ before the digits are displayed, and the attacker has to match the **digits**. I carried the raw width into the security claim and it stood in [§7.1](../specification/ppcp-rv.md#71-threat-model) and [§11.8](../specification/ppcp-rv.md#118-what-the-comparison-proves) unchallenged through four review passes.

⚠ **It changes no argument.** One in a million and one in 1.05 million are the same claim in every way that matters, and [§11.8](../specification/ppcp-rv.md#118-what-the-comparison-proves)'s force was always the *single attempt* rather than the width — which is why the error survived so long: nothing downstream depended on it. **It changed a conformance row**, which had been instructing an implementer to check a measured rate against a figure 4.86% from the truth.

✅ **And it closes something [E42](../specification/ppcp-core.md#errata-after-revision-9) could not.** The modulo bias contributes `1.7 × 10⁻⁹` relative — it cancels to seven significant figures against uniform-over-10⁶, so **the bias does not reach the security bound at all.** That is exactly what [§11.7](../specification/ppcp-rv.md#117-the-short-authentication-string)'s paragraph asserted and had no way to demonstrate. An erratum spent correcting bias arithmetic is now retired by a computation that shows the bias never mattered.

### R-22 — and the row could not run where this document promised it could

PinPointStudio's second item is the sharper one. [11.11c](../specification/ppcp-rv.md#1111-where-x25519-comes-from) exists so the derivation is pure and testable without a curve — that was the argued gain of the whole seam decision, and [E50](../specification/ppcp-core.md#errata-after-revision-9) leaned on it to claim RT-20a *"needs no socket, no peer and no counterpart"*.

⛔ **It still needed four X25519 operations per trial.** The promise did not reach the one row it most needed to.

[§10.4](../specification/ppcp-rv.md#104-guided-pairing) now publishes an **interposer quadruple with both legs' shared secrets**, so the required assertion is deterministic: derive from the published `Z₁` and `Z₂`, assert `849063 ≠ 576027`. No key agreement, in the shared component, before any application exists.

### F-R9-7 — and the rate is not falsifiable anyway

E50 told an implementer to *"assert the collision rate matches within sampling error"*. PinPointCapture's table settles that it cannot be done: separating 10⁻⁶ from a 5% neighbour needs of order **10⁹ trials for 1.5 σ** — two days of continuous X25519 at the measured 5 700/s, and still inconclusive.

**A conformance row nobody can run is a row that gets ticked.** So the statistical half is restated as what a run *can* establish and did: **no collision over a stated run**, and **uniformity by χ²**, which is the property the 1-in-10⁶ figure is computed *from* and which 200 000 trials settle in half a minute.

⚠ **I over-claimed here and it is worth naming.** *"A run that measures the bound rather than restating it"* was the line E50 was argued on, and it was wrong twice over — the target was 4.86% off and the measurement is not achievable. **The intent survives; the claim does not.**

### R-23 — the initiator mirror

[RT-20b](../specification/ppcp-rv.md#9-conformance)(ii) asserted only that an *acceptor* sends `bs_accept` before receiving `pk_i`. [9e1](../specification/ppcp-rv.md#9-conformance) lets a peer claim one role, and **PinPointStudio claims initiator-only, so the assertion did not reach it at all.** Both mirrors are now stated: an initiator sends `bs_offer` carrying only `ct` and sends `pk_i` **only after** `bs_accept` arrives, observable by a relay that simply does not reply and checks no `pk_i` follows.

⛔ **This matters more than a missing row.** [11.5c](../specification/ppcp-rv.md#115-the-exchange)'s ordering is the clause the entire property rests on, and half the interoperable population had no test of its own half of it.

---

## 2. The fourth item — E53 had not been carried where it reached

[3.4d3](../specification/ppcp-rv.md#34-resolvable-identifiers)'s body still argued from *"a venue where devices move between bays"*, which [E53](../specification/ppcp-core.md#errata-after-revision-9) established does not exist. Carried through — **and the clause survives on a better reason than the one it was given.**

⛔ **[7.4a](../specification/ppcp-rv.md#74-persistent-pairings) gives a persisted pairing no expiry.** It ends on revocation ([7.4d](../specification/ppcp-rv.md#74-persistent-pairings)) and on nothing else. So a coaching-studio host **accumulates pairings indefinitely** — every device it has ever paired with, until somebody prunes them — and the count that sets the reconnection wait is that accumulated total, **not the two or three devices in the room today.** E53's *"a handful of pairings"* was measuring the wrong thing. Size the rotation on **pairings held**.

[7.4b](../specification/ppcp-rv.md#74-persistent-pairings)'s requirement that persistence be visible and individually revocable is the only pruning mechanism there is, and it is a user action rather than an automatic one. That is worth an implementation knowing.

---

## 3. Closed

| | | |
|---|---|---|
| [**B14**](../specification/ppcp-rv.md#annex-b--open-issues) | X25519 reachability | ✅ **Closed on hardware at both ends.** OpenSSL 3.6.3 on the host; CryptoKit on an **iPhone 16, iOS 26.6** — RFC 7748 §6.1 reproduces, §10.4's `Z` matches, and all three small-order keys are rejected by **throwing**, which is [11.11f](../specification/ppcp-rv.md#1111-where-x25519-comes-from)'s *throw half* confirmed on a device. ⛔ **[5.4b](../specification/ppcp-rv.md#54-resolved-the-mechanism)'s rule is satisfied rather than waived** — this document had already paid once for accepting a desktop stand-in for a device measurement, and did not do so again |
| [**B15**](../specification/ppcp-rv.md#annex-b--open-issues) | The fleet case | ✅ **Closed as decided.** Not served, deliberately: its prerequisite is [B2](../specification/ppcp-rv.md#annex-b--open-issues)'s per-peer re-keying, which stays open |
| [**B18**](../specification/ppcp-rv.md#annex-b--open-issues) | Version negotiation | ✅ **Closed as decided.** Not added; [11.9d1](../specification/ppcp-rv.md#119-aborting-and-the-one-attempt-rule) bounds it to *pairs by code, this once*; [11.4c1](../specification/ppcp-rv.md#114-frames) makes any future answer a v2 from scratch |

**CR-01 has no open specification items.**

---

## 4. PinPointCapture's admission, and what it is evidence of

The review records that PinPointCapture reviewed [E47](../specification/ppcp-core.md#errata-after-revision-9) and passed it, having verified the arithmetic and read past both qualitative claims — and that this is the third time today the same thing has happened to them.

**It is the third time it has happened to me too**, and the pattern is the same one every time: [E37](../specification/ppcp-core.md#errata-after-revision-9) (why a MAC fails), [E40](../specification/ppcp-core.md#errata-after-revision-9) (what binds to what), [E43](../specification/ppcp-core.md#errata-after-revision-9) (a property X25519 does not have), [E47](../specification/ppcp-core.md#errata-after-revision-9) (what rotation costs), and now [E54](../specification/ppcp-core.md#errata-after-revision-9) (2⁻²⁰ against 10⁻⁶). **Every one was a claim stated in prose beside numbers that were correct.**

Their conclusion is the right one and better than a process fix: *"two implementations reproducing thirteen vector rows four times is worth exactly nothing against a sentence."* It is [the change-request notes](README.md)' third generalisation, and five passes have now confirmed it was under-weighted rather than wrong.

⚠ **What is worth adding is that E54 is the first of these caught by *running* something rather than by reading it.** Four passes read §11.8's bound and none questioned it; a 35-second execution did. That is an argument for RT-20a existing at all, and it is the specific argument for [E50](../specification/ppcp-core.md#errata-after-revision-9)'s ordering decision — **build the relay first**, because the tests that execute find things the tests that are read do not.

---

## 5. Where CR-01 ends

**Five review passes. Twenty-six findings. Errata E34–E55. No open specification items.**

The trajectory: pass 1 found two blocking structural defects; passes 2 and 3 each found one created by the previous pass's fix; pass 4 found nothing in the clauses and diagnosed the test that had been stuck for four passes; **pass 5 executed that test and found the property sound and the number describing it wrong.**

**What is not demonstrated, and what governs it:**

- [**RT-20b**](../specification/ppcp-rv.md#9-conformance) — needs one implementation and the relay.
- [**RT-20c**](../specification/ppcp-rv.md#9-conformance) — needs both and the relay. **This is the conformance claim to RV-6.**
- [**9g**](../specification/ppcp-rv.md#9-conformance) forbids anyone reporting an aggregate pass for RV-6 while RT-20c is unrun, and PinPointCapture has confirmed its claim document will carry a named RT-20c row reading **unrun** with no RV-6 aggregate.

⛔ **The relay is the first artefact.** Both remaining tests depend on it, it needs no application to exist, and building it produces the third implementation carrying both roles — the only slack the interoperable set has. Both teams have adopted that ordering.

**And the honest summary of the whole change request:** RV-6 is specified, reviewed five times, corrected twenty-six times, granted on a premise that turned out to be false, kept on a narrower one that is true, and **not yet demonstrated to do the thing it exists for.** Every one of those clauses is in the document where a reader will find it.
