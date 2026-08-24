# CR-01 — response to the review pass

| | |
|---|---|
| **Reviewing** | [PinPointCapture](../specification/reviews/CR-01-review-PinPointCapture.md) and [PinPointStudio](../specification/reviews/CR-01-review-PinPointStudio.md) on the [CR-01 disposition](CR-01-disposition.md) and `PPCP-RV` revision 9 |
| **Both verdicts** | **Accept.** Neither team reopened the design |
| **Findings** | **Six. All six accepted, all six applied** — errata E34–E39 |
| **Blocking, and rightly** | **R-01** (`v` bound into nothing) and **R-02** (only the acceptor serialised) |
| **Outcome** | [B16](../specification/ppcp-rv.md#annex-b--open-issues) closed. [B14](../specification/ppcp-rv.md#annex-b--open-issues) discharged bar one run. [B17](../specification/ppcp-rv.md#annex-b--open-issues) opened |
| **Date** | 24 August 2026 |

---

## 1. The two blocking findings are the argument for having asked

Both were invisible in [§10.4](../specification/ppcp-rv.md#104-guided-pairing)'s vectors, both would have been permanent after either team shipped, and neither would have been found by any test written against the section as it stood.

### R-01 — `v` was an unprotected agility mechanism. Accepted, and taken one clause further.

[11.6g](../specification/ppcp-rv.md#116-derivation) argues that a first-contact handshake with cryptographic agility is a first-contact handshake with a downgrade attack. [11.4b](../specification/ppcp-rv.md#114-frames) then introduced a version field, checked it only against the reader's own capability, and bound it into nothing. `sas_raw` already bound `pk_i || pk_a` for exactly this reason — [11.6c](../specification/ppcp-rv.md#116-derivation)'s own note that *"`Z` alone would not say whose keys produced it"* — and **the argument was not carried across to the field sitting next to it.**

Both proposed clauses are taken, and PinPointStudio's analysis of why clause 1 alone is insufficient is correct: an attacker rewriting **both** directions passes the echo check while leaving the two peers deriving under different versions.

⚠ **One step further than asked, and here is the reasoning so it can be objected to.** R-01 proposes binding the transcript into `K_c`. [11.6c](../specification/ppcp-rv.md#116-derivation) now binds it into **`sas_raw` as well**:

```
transcript = v || pk_i || pk_a

sas_raw = HKDF-Expand(BK, "ppcp1 sas"        || transcript,  4)
K_c     = HKDF-Expand(BK, "ppcp1 bs-confirm" || transcript, 32)
```

The `K_c` binding alone catches a version rewrite **at the MAC — after the operator has compared matching digits and affirmed them.** That is a worse place to catch it: it lands the failure in [11.9c](../specification/ppcp-rv.md#119-aborting-and-the-one-attempt-rule)'s "mismatch or MAC failure" territory having already told the operator, by showing matching digits, that everything was fine. Binding into `sas_raw` puts the signal in front of the human **before** anyone confirms, which is where [§11.8](../specification/ppcp-rv.md#118-what-the-comparison-proves) says the authentication actually lives.

It also reduces the rule an implementer must hold to one sentence — *everything that varies is bound into everything derived* — rather than three expansions with three different info constructions, which is the shape most likely to be got partly right.

⛔ **Four rows of [§10.4](../specification/ppcp-rv.md#104-guided-pairing) change: `sas_raw`, the SAS, `K_c`, and both MACs.** `PRK`, `K_tls` and `K_id` do **not**. The SAS is now **435948**, not `313164`. Both teams reproduced the old values byte for byte, so a recomputation that still yields `11e66a4c` is not wrong about arithmetic — it is reading the pre-E34 text. That is flagged in §10.4 itself.

### R-02 — only the acceptor was serialised. Accepted verbatim.

[11.3d](../specification/ppcp-rv.md#113-roles-and-the-connection)'s rationale applies unchanged to the initiator and the document stated it once. **The finding's real force is that the natural implementation is the one that breaks it**: [3.3f](../specification/ppcp-rv.md#33-txt-record)'s `dl` was added so a browsing peer that sees four windows can tell them apart, the obvious host interface is a list of discovered windows, and a peer that dialled all four to show the operator a list of candidate numbers would have done nothing this document forbade — while handing an attacker N blind draws with **the operator actively finding the collision**.

[11.3d1](../specification/ppcp-rv.md#113-roles-and-the-connection) is added as proposed. The observation that this makes `dl` load-bearing rather than convenient is right and is now recorded under the clause — the selection must happen *before* the digits exist, so the operator needs something to select on, which strengthens the case [3.3g](../specification/ppcp-rv.md#33-txt-record) already makes for admitting the privacy trade.

[RT-25](../specification/ppcp-rv.md#9-conformance) is added as a **review**-method row, because a peer violating 11.3d1 completes handshakes that are byte-for-byte conformant.

---

## 2. The other four

### R-03 / F-R9-1 — `invalid_key` named an observable neither library produces. Accepted.

Both teams measured this independently, on different libraries, and got the same answer: OpenSSL 3.6.3 fails `EVP_PKEY_derive` for all five standard small-order u-coordinates; CryptoKit throws `underlyingCoreCryptoError(-7)`. **Neither ever returns the all-zero `Z` the clause told an implementer to check for.**

[11.6b](../specification/ppcp-rv.md#116-derivation) now requires the abort where the agreement **fails or** yields zeros. PinPointCapture's addition — *a peer MUST NOT treat such a failure as a transport error and MUST NOT retry it* — is taken and is the part with teeth: a rejected key is an attack signal, and a retry loop around it eats [3.7b](../specification/ppcp-rv.md#37-the-bootstrap-window)'s single-attempt bound, which is what [§11.8](../specification/ppcp-rv.md#118-what-the-comparison-proves)'s entire argument rests on. [RT-21](../specification/ppcp-rv.md#9-conformance) is reworded to assert the observable.

**PinPointCapture is right that this is E23's shape**, and it is worth recording that it recurred in a section written by someone who had just finished documenting E23. Knowing the failure mode by name was not sufficient to avoid it; the measurement was.

### R-04 — 11.4f's rationale was inverted. Accepted; the clause stands.

The correction is right. **An interposed attacker holds `Z` on both legs, therefore `K_c` on both, and forges both MACs correctly** — that is what winning the comparison *means*. A MAC failure is evidence that no such attacker is present, and is overwhelmingly an implementation disagreement of the `PRK`-divergence class [§10.4](../specification/ppcp-rv.md#104-guided-pairing) warns about one step earlier.

The clause's action is unchanged and the reasoning under it is replaced. The added sentence is the one that matters: **the MAC is not an authentication check.** The comparison is the authentication; the MAC is an agreement-and-liveness proof that both ends reached the same `Z` and that both users actually acted. The inverted version would have led an implementer to weigh the MAC as the security boundary and the digits as ceremony — the exact inversion [11.1d](../specification/ppcp-rv.md#111-what-this-path-is-and-the-one-thing-it-cannot-be) exists to prevent, arriving through the reasoning rather than through the clause.

[11.9c](../specification/ppcp-rv.md#119-aborting-and-the-one-attempt-rule) is unchanged, as the review says it should be: *do not retry until you know why* is the right message for a MAC failure whether the cause is a tamperer or a divergence.

### F-R9-2 — the transcript is an offline verifier for `Z`. Accepted.

[11.2c](../specification/ppcp-rv.md#112-why-it-is-not-tls-and-what-that-unlocks) invited a reviewer to *"name a value on this connection whose disclosure to a passive observer weakens the pairing"*, and PinPointCapture took the invitation properly. The confirmation MACs descend from `Z` by public functions, so a recorded transcript tests any candidate `Z` — and therefore any candidate ephemeral key — offline.

Against a CSPRNG this is worth nothing. What it changes is **the cost of a weak or backdoored RNG**: without the transcript a bad ephemeral is exploitable only by an attacker present at the time; with it, a passive observer who recorded the exchange in March recovers the `PRK` in June, and [11.6f](../specification/ppcp-rv.md#116-derivation)'s erasure means neither peer retains anything that would reveal it happened.

Added to [§11.8](../specification/ppcp-rv.md#118-what-the-comparison-proves)'s *what it does not prove*, with the review's own framing: this is the real force of [11.5a](../specification/ppcp-rv.md#115-the-exchange)'s MUST, which was justified only by an impersonation argument.

Two qualifications recorded with it. It is [7.2a](../specification/ppcp-rv.md#72-handling-the-pairing-secret)'s *"a predictable secret defeats the entire model"* arriving on this path rather than a property peculiar to it. And it is **not** a defect in 11.2c's plaintext decision, because encrypting the bootstrap connection would not remove it — the same MACs cross an encrypted channel to the same attacker who has compromised the RNG. 11.2c is amended to say so rather than left overclaiming.

### R-05 — role-partial conformance. Accepted.

[9e1](../specification/ppcp-rv.md#9-conformance) added. The case is immediate rather than theoretical: PinPointStudio will ship **initiator-only**, which [11.2b](../specification/ppcp-rv.md#112-why-it-is-not-tls-and-what-that-unlocks) puts it on anyway, and two initiator-only peers cannot pair. *"Implements §11"* described a narrower capability than a reader would take it for.

---

## 3. A16 — the reference library has no crypto library

**Accepted, and the finding is better than a wording correction.** [A16](../specification/ppcp-rv.md#annex-a--decisions-and-alternatives) said *"the host's crypto library"*, and `libppcp` has none by construction. Verified against the source: `include/ppcp/hash.h` states the position in its own header —

> *"No OpenSSL, no libsodium, no platform crypto: plan A1 says libppcp has no dependencies, and REQ-LIC-2/3/5 are why. These three primitives are what PPCP needs … and all three are short enough to read."*

— and there is no X25519 or curve arithmetic anywhere outside `docs/`.

**The point stands beyond the wording.** SHA-256, HMAC and HKDF are short enough that a dependency-free library implements them and a reader checks them. X25519 is constant-time field arithmetic over 2²⁵⁵−19, and it is the one primitive in §11 that such a library should neither hand-roll nor vendor without breaking the constraint that made it dependency-free.

**The proposed seam is right and the precedent named is the right one.** `ppcp_rv_random_fn` exists because [7.2a](../specification/ppcp-rv.md#72-handling-the-pairing-secret) makes entropy the embedding's obligation and the library never generates a random byte. Key agreement is the same shape: the implementation owns the framing, the commitment, the HKDF chain, the SAS and the MACs — all of which it already has — and takes the agreement from its embedding. Thirty lines of OpenSSL on one side, `Curve25519.KeyAgreement` on the other.

⛔ **No clause is added, deliberately.** `PPCP-RV` is a wire specification and an injected-callback API is neither wire nor language-neutral. It is recorded as [B17](../specification/ppcp-rv.md#annex-b--open-issues), and A16's false claim is corrected in place — because A16 is where the wrong assumption was written down, and leaving it there would have had the next reader inherit it.

The `libppcp` API work is an implementation item and belongs in that repository's plan, not here.

---

## 4. Corrections back

### To PinPointCapture: your correction to the disposition's §3 is accepted

The disposition wrote that the first-contact half of "Studio finds the phone" *"can be delivered, and now is"*, above a table that says precisely who advertises and who dials. The review is right that the sentence is looser than the table it introduces: on this deployment the **capture device advertises the window and the host dials it**. That is Studio *finding* the device, which is what the request wanted — but Studio is not the peer that listens, and a reader skimming for "PPS does discovery" could take it that way.

[The disposition is corrected in place](CR-01-disposition.md#3-what-the-request-did-not-spot-and-it-is-the-useful-part), with a note recording the change rather than a silent edit.

### To PinPointCapture: B14 is not fully discharged, and 5.4b is why

The measurement is accepted and the analysis is accepted — including the observation that the analogy to the TLS 1.3 PSK gap does not hold, because *that* one was structural (`tls_ciphersuite_t` contains no PSK entry, so the suite can be neither requested nor withheld through any public API) while this is a first-class primitive with raw bytes in and raw bytes out. That is the right correction and it is recorded in [B14](../specification/ppcp-rv.md#annex-b--open-issues).

⚠ **But the run was on the iOS simulator SDK, as the review says plainly and to its credit.** [5.4a and 5.4b](../specification/ppcp-rv.md#54-resolved-the-mechanism) exist because this document accepted a desktop proxy for a device measurement once, on reasoning of exactly this quality — shared availability annotations, shared enumerations, no expected difference — and 5.4b had to be restored after being demoted to *"still worth having, but no longer gates anything"*.

The reasoning here is better than it was there and the expected result is the same. **The rule still applies**: the device run is required before either implementation ships a guided pairing. It does not gate writing the code, because a negative result reopens [A16](../specification/ppcp-rv.md#annex-a--decisions-and-alternatives) only.

---

## 5. Decisions the reviews asked for

| | Question | Decision |
|---|---|---|
| **RT-20's relay — who hosts it?** | PinPointCapture offered; PinPointStudio proposed `libppcp/tools` beside `ppcp-conform` so both teams run the same relay | **`libppcp/tools`.** PinPointStudio's reasoning wins: two relays would be two harnesses each correct against its own author, which is [B7](../specification/ppcp-rv.md#annex-b--open-issues)'s failure mode reproduced inside the test for it. PinPointCapture's offer to build it stands — build it there. |
| **RT-20's assertions** | PinPointStudio asks for two beyond *"the digits differ"* | **Both taken**, and they are in [RT-20](../specification/ppcp-rv.md#9-conformance) already in substance: the declining peer does not pair **and** the window closes without reopening ([3.7b](../specification/ppcp-rv.md#37-the-bootstrap-window) is the property, the digits are only the signal); and the relay's own legs must each complete, or the harness is testing its own bug. [RT-24](../specification/ppcp-rv.md#9-conformance)'s second half — a `v` rewritten in both directions — needs the same relay and is noted as belonging with it. |
| **A `review` row for 11.7d/11.9c** | PinPointStudio asks for one; both teams report the clauses are offscreen-testable | **Added as [RT-26](../specification/ppcp-rv.md#9-conformance).** The reasoning is the review's own and is right: unusual clauses are the ones that get quietly dropped, and both teams have a harness that can assert them. |
| **11.7b — both peers display six digits** | Both teams confirm it holds for every peer they will ship | **[A14](../specification/ppcp-rv.md#annex-a--decisions-and-alternatives) does not reverse.** It stays recorded as the decision that reverses if a screenless capture peer ever appears — a PAKE decided before implementation being much cheaper than one decided after. |
| **Multi-device stereo** | PinPointCapture notes two or three phones on one host costs one confirmation each | **[B15](../specification/ppcp-rv.md#annex-b--open-issues) updated.** Its motivation is stronger than CR-01's "several bays" made it sound: the fleet problem arrives on a *single* bay. The answer is still B2's per-peer re-keying first. |
| **`PPCP-ENC` cross-reference for channel 255** | Both teams support raising it | **Agreed, and it should be raised as its own erratum against `PPCP-ENC` rather than folded in here.** `ENC` 2a reserves the byte and nothing in `ENC` says what claims it; that is the "one idea spelled in two documents" shape E25 had to clean up after. |

---

## 6. The one open question, and it is not the protocol team's

**PinPointStudio's answer to Q3 is: no advertising today, by explicit design**, with `src/Ppcp/ppcp_discovery.h` stating it in its own header — *"THE BROWSER HALF, AND ONLY THE BROWSER HALF"* — and the whole file guarded to Apple platforms, so the Windows host has no discovery in either direction.

The review accepts [3.5e](../specification/ppcp-rv.md#35-who-advertises-and-who-browses) as correct and names the consequence precisely: with the capture peer bound by [3.5d](../specification/ppcp-rv.md#35-who-advertises-and-who-browses) and the host advertising nothing, **[§7.4](../specification/ppcp-rv.md#74-persistent-pairings)'s persistence buys exactly nothing on this deployment** — both peers hold valid key material, neither can find the other, and the user sees an application that remembers the phone and still asks for a code every session. PinPointCapture confirms its half is already built and has nothing to find.

Its recommendation, offered for Mark's decision rather than the protocol team's:

- **macOS: commit to 3.5e.** The stated objection — binding UDP 5353 and conflicting with `mDNSResponder` — does not apply to `DNSServiceRegister`, which asks the same responder over the same IPC socket the existing browse path already uses. Advertising is additive to a mechanism already present in the process.
- **Windows: a dependency question, not a code one.** There is no `dns_sd.h`, the browser is compiled out entirely, and 3.5e is unimplementable there without taking on Bonjour or an equivalent responder.
- **The TXT record is not the obstacle** — `libppcp` already computes everything in it, and 3.4d1's rotation is a registration schedule.

⛔ **This is a product decision and it is not made here.** What the protocol team can say is what [3.5e](../specification/ppcp-rv.md#35-who-advertises-and-who-browses) already says: until one end advertises, §7.4 is dead weight on this deployment. *"Yes on macOS, deferred on Windows, and here is why"* is a better answer for the record than a bare yes, and the review is right about that too.

**PinPointStudio's R-06 belongs beside it.** The venue that motivated CR-01 is the venue where [3.6a](../specification/ppcp-rv.md#36-multicast-is-not-to-be-relied-on) says multicast *"will not work"*. That is why the code path stays REQUIRED under [9f](../specification/ppcp-rv.md#9-conformance), and the review's conclusion follows: the commercially load-bearing clause for the host is [3.7h](../specification/ppcp-rv.md#37-the-bootstrap-window) — reaching a window at an endpoint learned out of band — not §3. No clause changes. The work item is *how a window is reached without multicast*, and it is a host-side product question.

---

## 7. Where §11 now stands

**Closed.** [B16](../specification/ppcp-rv.md#annex-b--open-issues) — the section has had its review pass, both teams accept, six findings applied.

**Nearly closed.** [B14](../specification/ppcp-rv.md#annex-b--open-issues) — discharged on both sides; the iOS **device** run is outstanding and is required before shipping.

**Open.** [B17](../specification/ppcp-rv.md#annex-b--open-issues) — the X25519 seam. [B15](../specification/ppcp-rv.md#annex-b--open-issues) — the fleet case, behind B2. And **[RT-20](../specification/ppcp-rv.md#9-conformance) still cannot run**, because it needs two real implementations either side of a deliberate relay and neither has written §11 yet.

That last one has not moved and should not be allowed to feel as though it has. The vectors are now reproduced by two independent implementations, the design has been attacked by two teams and survived with six corrections, and **none of that is the same as having demonstrated the property the section exists to deliver.** Until RT-20 runs, no conformance claim to RV-6 should be made — which is what PinPointCapture said unprompted, and it is the right position.
