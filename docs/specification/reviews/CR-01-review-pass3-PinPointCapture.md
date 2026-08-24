# `PPCP-RV` §11 — third-pass review from PinPointCapture

| | |
|---|---|
| **Scope** | **E40–E42 only**, plus the owner's own unreviewed additions — 11.6c1, 11.4h1, RT-24a, RT-18's erratum-level requirement, RT-20's role-slack note, §10.4's sixth cause. Not a third full re-read, as requested |
| **Against** | `PPCP-RV` at `4637191`, revision 9 as amended by E34–E42 |
| **From** | PinPointCapture, 24 August 2026 |
| **Verdict** | **Ready to implement.** One finding, one precision point, one design question endorsed, and one correction to E42 that is ours to make |

---

## 0. Vectors re-verified at `4637191`

**13/13.** E40–E42 do not touch §10.4, as stated — checked rather than taken. Same independent implementation, matched against the live section by string comparison.

⚠ My F-R9-3 correction landed and is right: `1 819 808 448 mod 10⁶ = 808448`.

I also computed the two values RT-24a is about, because they are more useful than prose:

```
correct    sid = 1cc4b886e8bd45e0a3b207ae783bc56b     ← §10.4
transcript-bound sid = 18dd04b1da8342a6b4248fb1bd2d0626     ← the R-09 trap
```

The over-applied `sid` differs in the **first octet**, so it is not a subtle divergence — anyone who prints it once sees it. That matters for §2 below.

---

## 1. 11.6c1 (E40) — the boundary is right, and correctly scoped

**Accepted.** The exclusion of `sid` and `PRK` from the transcript binding is correct, and the trap it closes is real: I reproduced it above.

**Is the scoped rule unambiguous everywhere else?** Yes, and for a reason worth stating rather than asserting. There are four other derivations in §11 an over-eager implementer could bind a transcript into — `ct`, `BK`, and the two MAC labels. Every one of them **fails loudly and early**:

| Over-application | Detected |
|---|---|
| into `ct` (11.5b) | acceptor's recomputation fails → `commitment_mismatch`, before any derivation |
| into `BK`'s salt (11.6c) | digits diverge → the operator sees a mismatch, before affirming |
| into either MAC label (11.5f) | MAC verification fails → `rejected`, after the digits but before a pairing exists |
| **into `sid` or `PRK`** | ⛔ **nothing** — matching digits, matching MACs, divergence at the TLS handshake |

So the silent failure class has exactly two members, and 11.6c1 covers exactly those two. The scoping is complete for the class that matters, and 11.6c's *"Both of these expansions"* now scopes the general rule at its source. No further clause needed.

### ⚠ One precision point on the rationale — not a defect

11.6c1's *"why the boundary falls where it does"* rests on three legs. The second is:

> `Z` already commits to both public keys by construction (11.6b having refused the small-order cases where it would not)

**X25519 is not a commitment scheme**, and this leg is doing no work. The property that actually holds the boundary is the first leg: by the time `PRK` is derived, the comparison and both MACs have verified, and *those* bind the transcript — so two peers reaching `PRK` provably agree on `v`, `pk_i` and `pk_a` already.

That argument is airtight and does not need leg two. Leg two, read strictly, claims a binding property the primitive does not have; read charitably, it says *agreement on `Z` implies agreement on the key pair with overwhelming probability*, which is true but is a probabilistic statement wearing "by construction".

⛔ Worth tightening precisely because E40 exists: R-09 was an over-generalised rationale, and a loosely-stated rationale leg inside the clause that fixes one is the same failure looking for a second host. Suggest deleting leg two or restating it as *"and the MACs have already proved that agreement"*.

---

## 2. F-R9-4 — RT-24a asserts something a `static` row cannot observe ⚠ new

> **RT-24a** | static | `sid` and `PRK` reproduce from `Z` **alone**, with no transcript bound (11.6c1) … **assert not merely that the value matches but that it was computed without the transcript.**

**The additional assertion is not observable.** A static row sees values. If `PRK` matches §10.4, the transcript was not bound — any binding changes it, as §0 demonstrates. There is no state in which the value matches *and* the transcript was bound, so "not merely that the value matches" asks for something with no distinct evidence.

As written, RT-24a is **exactly RT-18's `PRK` row**, and an implementer will re-run RT-18, tick RT-24a, and have gained nothing — which is the failure mode a conformance row exists to prevent.

⚠ **The intent is good and worth keeping**; it is the row's *method* that does not match it. Two readings, and they want different rows:

1. **A negative test** — deliberately bind the transcript into `sid`, assert the result differs from §10.4. That is runnable, is genuinely distinct from RT-18, and tests that *your harness would catch the trap*. `18dd04b1…` is the expected wrong value and could be published beside it.
2. **A code read** — check the implementation does not bind it. That is `review`, alongside RT-25 and RT-26.

**Recommend (1)**, as a `static` row with the wrong value stated. It is the only version that is both observable and not already RT-18, and publishing the wrong value is what makes it self-checking — the same argument §10.4 makes for publishing the little-endian misread.

---

## 3. 11.4h1 (E41) — accepted, and the reasoning for adding it is right

The acceptor-echo rule is new normative text beyond what R-10 asked, and it belongs. Checked for completeness:

- `v` in **1..255** resolves a genuine contradiction — 11.4b's CBOR uint reaches 2⁶⁴−1, 11.6c encodes one octet. ✅
- `v = 0` is outside the range, therefore `malformed` under 11.4c. ✅
- Only `bs_offer` and `bs_accept` carry `v`; the acceptor learns it at frame 1 and echoes at frame 2, before any derivation. ✅
- **Which `v` each peer binds** is now stated, and the two are equal after 11.4h or the exchange aborted. ✅

⛔ **The justification for stating it is the important half and is correct.** Every consistent reading detects the both-directions rewrite, so no hole closes — but §10.4 carries **one** value of `v`, so the vector cannot distinguish two implementations that chose differently, and RT-18 would pass both. That is precisely the class of defect vectors cannot catch, and it is the second one found in two days.

---

## 4. The design question — version assertion, not negotiation. Real, and free to fix now

Not a finding against any clause; the clauses are self-consistent. Raised because the cost curve is the same as R-01's.

**The situation.** `bs_offer.v` is a single value. 11.4h requires a peer to offer the **highest** it implements and forbids proposing lower. So a v2 initiator meeting a v1 acceptor aborts under 11.4e, and 11.9b forbids retrying. **The two cannot complete a guided pairing even though both implement v1.**

⚠ **Severity is lower than it first looks, and the mitigation should be stated with the problem.** 11.9d already sends a peer that has aborted twice to the pairing code, which 2a makes REQUIRED of everyone. So the failure is *"RV-6 unavailable between these two peers"*, not *"these peers cannot pair"*. That is a real difference and it is why this is a design question rather than a defect.

**It is still worth deciding now.** `v` is 1, nobody has implemented, and the change is free today and a deployed-peer problem later — the argument R-01 already won.

⛔ **The naive fix reintroduces exactly what E34 closed.** Make `v` a list and an attacker strips the high entries; the acceptor picks v1 honestly, both peers bind v1, the digits match and the MACs verify. A downgrade that the transcript does not see, because the transcript binds only the *selection*.

**What would work** — and E34's machinery already supports it — is binding **everything offered together with what was selected**, not the selection alone:

```
transcript = <offered versions, canonically encoded> || selected_v || pk_i || pk_a
```

Both peers can compute it: the initiator knows what it offered and what came back; the acceptor knows what it received and what it chose. A stripped list gives the two peers different transcripts, so the digits differ **in front of the operator** — which is where §11.8 says the authentication lives, and the same argument that made E34 bind into `sas_raw` rather than only into `K_c`.

⚠ Two notes on shape, offered as constraints rather than as a design. `hello.versions` is an ordered list and E25 settled range-versus-list across the set, so §11 is currently the only place in the protocol family with neither — worth a deliberate answer either way. And whatever is chosen needs a canonical encoding of the offered set, because both ends must produce identical transcript octets from it; that is the part most likely to diverge, and it would want a §10.4 row.

**If the answer is to leave it**, that is defensible on 11.9d's fallback — but it should be recorded as a decision in Annex A with the fallback named, not left as an unexamined property of a single-valued field.

---

## 5. The rest of the unreviewed set — all accepted

**RT-18's erratum-level requirement.** ✅ Strongly endorsed. It comes directly from both teams producing superseded values on their first pass, and it converts that from an anecdote into an obligation. ⛔ This repository will comply: every reproduction we report will carry the commit and erratum level it was taken against, as §0 above does.

**§10.4's sixth diagnostic cause.** ✅ Correct, correctly ranked, and correctly identified as the only one that produces a *successful-looking* pairing first. The `18dd04b1…` value in §0 is available if it would be useful beside it.

**RT-20's role-slack note.** ✅ Accurately records what we raised. Nothing to add beyond confirming the commitment: PinPointCapture builds **acceptor-only**, and the relay in `libppcp/tools`.

**E42.** ✅ Accepted — with one correction, below, which is ours.

---

## 6. E42's attribution is the wrong way round, and the error was ours

E42 records:

> Two of the three figures originated in PinPointStudio's pass-1 review and were quoted onward in good faith.

**That is not what happened, and the record should say so.** Checked against both pass-1 reviews:

| Wrong figure | Actually from |
|---|---|
| `295 967 296` residues | ⛔ **PinPointCapture** — verbatim from our N1 |
| the favoured set stated as *"the rest"* | ⛔ **PinPointCapture** — same sentence |
| `2.3 × 10⁻⁷` | PinPointStudio |

**PinPointStudio's pass-1 figure was `967 296`, which is correct.** Their sentence reads *"values below `967 296` are very slightly favoured"* — right count, right direction. Ours replaced it with a number that cannot be a count of residues when there are 10⁶ of them, and inverted which set was favoured.

So two of the three came from **us**, and the paragraph as published took our wrong count over their right one.

⚠ We should have caught it. Our own review offered N1 as *"recorded so that a later reader who spots the bias does not raise it as a defect"* — a paragraph whose entire purpose was to save a reader an afternoon, with the arithmetic unchecked. We ran two independent implementations against thirteen vector rows the same day and did not spend thirty seconds on the numbers in our own prose. That is the response's generalisation landing on us, and it is worth the record being accurate about whose it was.

**No clause is affected** — E42 is prose-only and the corrected paragraph is right. This is about provenance, which matters here for the same reason the little-endian figure did: a misattributed error is one the wrong team looks for next time.

---

## 7. On the two things that decide whether it ships

Neither is a specification defect and neither is ours to rule on. Both are recorded in PinPointCapture's own delivery plan.

**B17.** Noted. It gates `libppcp` and not this repository — our X25519 comes from `CryptoKit` behind whatever seam is agreed, and B14's measurement says it works. ⚠ It does gate **RT-20**, which needs two real ends, so it gates the point at which §11 stops being a design with vectors.

**The venue problem, which we agree should not stay a footnote.** 3.6a is unusually blunt — *"It will not work at a range"* — and CR-01 asked for RV-6 **because of** a range. 3.7h is a `MAY`, and neither application implements it.

⛔ **Stated plainly: RV-6 over mDNS is a feature that works in an office and not at the venue it was requested for.** That is not a criticism of the ruling — PinPointStudio raised it as R-06 and sought no clause change, which was right, since §11 constrains the handshake and not how the endpoint was learned. But it means *conformant* and *solves the problem* are different states here, and the gap belongs somewhere it will be seen rather than in an annex.

⚠ **For our part**: PinPointCapture's MVP plan currently assumes discovery works, and a demo that succeeds in the office and fails at the range would be the worst possible place to find this. We are recording 3.7h — an endpoint entered out of band — as MVP scope rather than as a later refinement, and would suggest PinPointStudio consider the same. If both applications implement it, the feature works where it was asked for; if neither does, it does not.
