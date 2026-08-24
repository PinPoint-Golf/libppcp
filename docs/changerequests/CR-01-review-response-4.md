# CR-01 — response to the fourth review pass

| | |
|---|---|
| **Reviewing** | [PinPointStudio pass 4](../specification/reviews/CR-01-review-4-PinPointStudio.md) and [PinPointCapture pass 4](../specification/reviews/CR-01-review-pass4-PinPointCapture.md), 24 August 2026 |
| **Both verdicts** | **Ready to implement. No objection to starting.** |
| **Findings** | **Six. Five accepted and applied** — errata E49–E52 and three amendments to the API note. **One declined**, on the facts |
| **The two answers demanded** | **[RT-20](#1-rt-20-the-test-was-wrong-and-that-is-the-answer)** — the row was wrong, and is now three rows. **[R-19](#2-r-19--the-objection-and-it-lands)** — accepted in full; E47 was mine and two of its claims were wrong |
| **Date** | 24 August 2026 |

---

## 1. RT-20: the test was wrong, and that is the answer

RT-20 has been reported as *"has not moved"* in **four consecutive review passes**, by both teams, and I have written it back four times. That is not a status; it is a symptom, and the diagnosis is that **the row was badly formed**.

RT-20 said *"no single-implementation harness can run it."* That was true of the row and **false of two thirds of its contents**, because the row bundled three claims of entirely different kinds:

| | The claim | What it actually needs |
|---|---|---|
| **a** | An interposer cannot make both legs show the same digits | **Arithmetic.** Four keypairs |
| **b** | A peer displays the right digits, declines correctly, and closes its window | **One** implementation, and a relay |
| **c** | *These two* implementations do that against each other | Both, and a relay |

Only **c** needs two implementations. Because all three were one row, the whole thing waited on the last of them — and was correctly reported as unrun, for months of development, while the part that carries the security argument was runnable from day one.

### What changes

**[RT-20a](../specification/ppcp-rv.md#9-conformance) — runnable before a line of §11 exists.** [11.11c](../specification/ppcp-rv.md#1111-where-x25519-comes-from) made the derivation a pure function of `Z, v, pk_i, pk_a`. So the interposition property is a computation: take an initiator, an acceptor and the two keypairs an interposer would hold, derive both legs, assert the digits differ.

⛔ **And over a large run of random quadruples it does something the document has never done: it *measures* [§11.8](../specification/ppcp-rv.md#118-what-the-comparison-proves)'s 2⁻²⁰ bound rather than asserting it.** That bound is the number the whole path's security is quoted in, and until now nothing checked it.

**[RT-20b](../specification/ppcp-rv.md#9-conformance) — runnable as soon as *either* peer exists.** One real implementation, the relay on the other side. It covers the behaviour, and — this is the part I had missed — **it covers [11.5c](../specification/ppcp-rv.md#115-the-exchange)'s ordering**, which [A15](../specification/ppcp-rv.md#annex-a--decisions-and-alternatives) and [E34](../specification/ppcp-core.md#errata-after-revision-9) both identify as the clause everything rests on. A relay observes it **directly**: withhold `bs_reveal`, and check the acceptor already sent `pk_a`. That is the single most important assertion in §11 and it needs one implementation, not two.

**[RT-20c](../specification/ppcp-rv.md#9-conformance) — the interop claim, and genuinely last.**

⛔ **This does not weaken anything, and the claim structure is explicit: RT-20c is the conformance claim to RV-6. RT-20a and RT-20b passing is not it.** [B7](../specification/ppcp-rv.md#annex-b--open-issues) stands. What changes is that a peer can state precisely what it has demonstrated, instead of reporting one row as unrun for the whole of development — and that the property §11 exists to deliver gets a real partial demonstration immediately rather than none for months.

### The split is not the whole answer, and on its own it makes one third of the objection worse

Put plainly, the objection has three parts. **The split addresses two of them and aggravates the first.**

| | The objection | Answered? |
|---|---|---|
| 1 | **The weight of accumulated green.** Volume of successful verification starts to feel like the feature is proven, when the one thing that would prove it has never run | ⛔ **Made worse by the split** — RT-20a and RT-20b add two more passing rows |
| 2 | **The ordering is backwards.** RT-20 runs last by construction, so a failure lands at maximum cost | ✅ RT-20b runs against **one** implementation |
| 3 | **No slack in the set.** Initiator-only and acceptor-only is the entire interoperable population | ✅ The relay is the third both-roles implementation, and is now needed *sooner* |

**Point 1 is right and the split alone would have made it worse.** So [9g](../specification/ppcp-rv.md#9-conformance):

> A conformance claim to §11 **names RT-20c explicitly and states its result**, and **MUST NOT report an aggregate pass** for RV-6 while it is unrun.

This is [RT-11](../specification/ppcp-rv.md#9-conformance)'s device — a harness records *not applicable* rather than quietly passing — applied where the stakes are higher. The framing is taken from the objection and is now in the document: **every other test in the list checks that two *honest* implementations compute the same numbers. RT-20 is the only one in which somebody is attacking.** Four vector reproductions, two implementations, four passes and twenty findings closed is a great deal of green, and none of it touches the security property. [11.1d](../specification/ppcp-rv.md#111-what-this-path-is-and-the-one-thing-it-cannot-be) already names the extreme — a peer that quietly compares the digits itself passes every static test in the document while authenticating nothing.

**And what each part does *not* prove is now tabulated beside it**, because the parts must not be read as the whole:

- **RT-20a touches no implementation at all.** A peer that auto-confirms passes it trivially — it proves the *derivation* separates an interposer's legs, not that any peer behaves.
- **RT-20c passing shows the protocol emits a mismatch signal. It does not show a tired operator at bay four notices it.** That is [11.7d](../specification/ppcp-rv.md#117-the-short-authentication-string) and [11.9c](../specification/ppcp-rv.md#119-aborting-and-the-one-attempt-rule), it is human factors, [§7.1](../specification/ppcp-rv.md#71-threat-model)'s *not defended* table already says an operator who affirms without comparing has authenticated the attacker, and **no protocol test reaches it.** That caveat is the objection's own and it is now the document's.

### "Worth building early" is now a consequence, not a preference

The objection's closing point is that *"worth building early" needs to become an actual position*, because the natural gravity of a test needing two implementations is to slide to the end. ⛔ **That is right, and a note in a response does not hold against gravity.**

I cannot put it in the implementation plan — that is a closed record of a finished programme and it is not mine to reopen. **So it is stated where it binds instead: in [§9](../specification/ppcp-rv.md#9-conformance), as a consequence of the test table itself.** RT-20b needs the relay; RT-20c needs the relay; so the relay is a prerequisite of **both** tests that touch the security property — and it is **the earliest thing in the list that can be built, because it needs no application to exist.** That is derivable from the rows rather than asserted, which is the form most resistant to being deprioritised later.

Building it first also produces the third implementation carrying both roles, which is the only slack the interoperable set has. **The item that has looked stuck for four passes turns out to be the first thing to build and the thing that unblocks the other two.**

---

## 2. R-19 — the objection, and it lands

**Accepted in full. [E47](../specification/ppcp-core.md#errata-after-revision-9) is mine, it landed after the third pass with nobody having reviewed it, and two of its claims were wrong.**

### (a) *"only multicast chatter"* — wrong, and wrong in the way that matters

[3.2a](../specification/ppcp-rv.md#32-instance-name) **MUST**s the instance name to be `PPCP-` plus the first four bytes of `rid`. So rotating `rid` **renames the service instance** — a deregister, probe and announce cycle, not a TXT update. At the fifteen-minute floor that is nothing. At the twenty-second rotation E47 asked for, it is continuous.

⛔ **On precisely the networks [3.6a](../specification/ppcp-rv.md#36-multicast-is-not-to-be-relied-on) says rate-limit and drop multicast.** PinPointStudio's phrasing is the right one and I am adopting it verbatim: **the mitigation for a discovery problem would trigger the condition that breaks discovery.** That is a sharper objection than the cost being merely unwelcome, and it would have been found at a range, in front of an operator, months from now.

### (b) *"strictly better for unlinkability"* — simply not true

Within one registration the `A`/`AAAA` record and the port do not change. An on-link observer links every rotation trivially: they are the same host by inspection. Rotation's value is the one [3.4e](../specification/ppcp-rv.md#34-resolvable-identifiers) actually claims — *observations in two venues cannot be correlated* — and that is untouched by rotating faster inside one venue. **Neutral, not better.** The claim is withdrawn rather than softened; a clause should not lean on a benefit it does not have.

### (c) The fix, taken as proposed

New **[3.2d](../specification/ppcp-rv.md#32-instance-name)**: a peer rotating faster than the floor may keep a **stable instance name for one registration** — eight hex characters of four CSPRNG bytes, drawn fresh per registration — and rotate only the TXT. One announcement instead of a full cycle. [3.2b](../specification/ppcp-rv.md#32-instance-name) is satisfied by construction: a per-registration value persists across nothing.

The reason it is free is worth recording: **a browser never reads the instance name.** [3.4b](../specification/ppcp-rv.md#34-resolvable-identifiers) resolves `rid` from the **TXT record**, so 3.2a's derivation is a convention rather than a mechanism.

### (d) Recorded so it is not raised as the obvious objection

Fast rotation does **not** leak the count of held pairings. [3.4a](../specification/ppcp-rv.md#34-resolvable-identifiers) forces rotation regardless, so an observer sees churning `rid`s either way and cannot distinguish one pairing from ten. [B3](../specification/ppcp-rv.md#annex-b--open-issues)'s property survives E47 intact. Both teams checked this independently and both concluded the same.

---

## 3. R-18 and F-R9-5 — both teams found the same thing in the struct

Raised independently, from different directions, and both right. The struct mixes **what must be erased** with **what may be kept**, and *the natural thing for a caller to do with it is wrong*: you keep it because it holds the `PRK`, and you keep `K_c` and the digits alive with it, against [11.6f](../specification/ppcp-rv.md#116-derivation) and [11.7f](../specification/ppcp-rv.md#117-the-short-authentication-string).

Applied to the API note: the struct is **split by lifetime with a rule between the halves**, and `ppcp_rv_bootstrap_wipe()` is added.

⛔ **PinPointCapture's half of this is a specification change, and it is [E51](../specification/ppcp-core.md#errata-after-revision-9).** [11.6f](../specification/ppcp-rv.md#116-derivation)'s prose said *"what survives a failed one is nothing"* — which covers `PRK` — while its **explicit list** named only four items, none of them `PRK`. **A list is what gets implemented from.** And the exposure is not theoretical: a peer computes the whole chain the moment it holds `Z`, and [11.3e](../specification/ppcp-rv.md#113-roles-and-the-connection) allows sixty seconds for a user to affirm, so **every abort path left a `PRK` in memory for a pairing that does not exist and never will.** The list now names `PRK`, `K_tls`, `K_id` and `sid` on the failure path.

⚠ Also taken: the two failure modes must be **distinguishable**. An all-zero `Z` is [11.6b](../specification/ppcp-rv.md#116-derivation)'s attack signal; a `v` outside 1..255 is a programming error. One code for both would report a caller's bug as an attack.

**R-16 accepted** — `bk` and `sas_raw` added to the struct. The point is exact: [RT-18](../specification/ppcp-rv.md#9-conformance) names `BK` and `sas_raw` as rows, and the component the seam nominates as RT-18's home could not assert either of them. That defeats the specific gain the design was argued on.

---

## 4. R-17 — declined, on the facts

> *"`PPCP_RV_BS_CT_BYTES` is defined in the proposed header and **nothing in the surface uses it**, which is its own hint … **Add `ppcp_rv_bs_commit()`.**"*

**It is already there, and it already uses that constant.** From the note as published:

```c
/* ct = SHA-256("ppcp1 bs-commit" || pk_i) — 11.5b.  No key agreement. */
PPCP_API void ppcp_rv_bs_commit(const uint8_t pk_i[PPCP_RV_BS_KEY_BYTES],
                                uint8_t ct[PPCP_RV_BS_CT_BYTES]);
```

No change is needed and none is made. ✅ **The reasoning is right and is worth keeping on the record even though the finding is not**: the commitment label would otherwise be the only `ppcp1` string spelled in the applications rather than in the library, twice, and a typo in it is invisible except through §10.4's `ct` row.

---

## 5. A correction back — and it matters before anyone writes code

PinPointStudio closes with:

> *"`ppcp_channel_validate()` still returns `PPCP_ERR_MALFORMED` for channel 255, so `libppcp` cannot emit a bootstrap frame today."*

**The observation is correct — I checked `src/ppcp_frame.c:43` — and the code is right and must stay right.**

That rejection **is** the fail-closed property [11.4a](../specification/ppcp-rv.md#114-frames) depends on. A bootstrap frame arriving on a PPCP link must be rejected, and channel 255 being invalid on a PPCP channel is exactly what rejects it. An implementer who meets this while adding §11, concludes the validator needs relaxing, and relaxes it would **delete the safety argument in the course of implementing the clause that relies on it** — and every test would still pass, because nothing exercises a misdirected frame.

⛔ What is needed is a **separate write path** for bootstrap frames that does not consult the PPCP channel rule, because a bootstrap connection is not a PPCP link ([1.3c1](../specification/ppcp-rv.md#13-where-it-stops)). Not a relaxation.

This is now stated in [11.4a](../specification/ppcp-rv.md#114-frames) itself, because it is a plausible first act and an expensive one.

---

## 6. Recorded rather than changed

**[11.11h1](../specification/ppcp-rv.md#1111-where-x25519-comes-from) — the erasure obligation is partly on a closed-source framework.** PinPointCapture reports that on CryptoKit the scalar and `Z` live in `Curve25519.KeyAgreement.PrivateKey` and `SharedSecret`, neither of which exposes a zeroise or documents one, while `SymmetricKey` does. They can guarantee they hold no copy; they cannot guarantee the framework holds none.

The clause stands — the alternative is worse and the mitigation is real. But this is **the same class as [§5.4](../specification/ppcp-rv.md#54-resolved-the-mechanism)'s PSK limits: not a defect, a bounded truth about what a MUST can mean on a given platform**, and this programme has now decided three times to write those down rather than assume them away. [RT-23](../specification/ppcp-rv.md#9-conformance) records the bound rather than reporting a pass it cannot justify.

**PinPointCapture's argument for the one-call shape is better than mine and is adopted**: it removes three of §10.4's six divergence causes outright. `sas` is pre-reduced so the little-endian misread is unreachable; one call means one transcript construction so the `sid` binding cannot be made by a caller at all; and named parameters are the best available defence against transposing `pk_i`/`pk_a`.

**Both teams corrected their own earlier work again.** PinPointCapture withdrew its third-pass sentence that *"the silent failure class has exactly two members"* as unscoped — cause 1 predates E34 and has the same signature — and confirmed R-11 was a stronger finding than the precision point it had itself raised. That is now the pattern of this review rather than an incident, and it is the reason the numbers in this document can be trusted.

---

## 7. Where §11 stands

**Four passes. Twenty findings. Errata E34–E51. Both teams: ready to implement.**

The trajectory closed: pass 1 found two blocking structural defects, pass 2 found one created by pass 1's fix, pass 3 found one created by pass 2's fix, **pass 4 found nothing in the normative clauses at all** — three of its five findings were against a document that says of itself *decision only, nothing implemented*, which is the right time and place to find them.

**Still open, and now with owners:**

| | | Owner |
|---|---|---|
| [B14](../specification/ppcp-rv.md#annex-b--open-issues) | The **iOS device** X25519 run — a ship gate, not a code gate | PinPointCapture |
| [RT-20c](../specification/ppcp-rv.md#9-conformance) | The interop demonstration | Both, after the relay |
| [B18](../specification/ppcp-rv.md#annex-b--open-issues) | Version negotiation | Deliberately unanswered |
| [B15](../specification/ppcp-rv.md#annex-b--open-issues) | The fleet case | Behind [B2](../specification/ppcp-rv.md#annex-b--open-issues) |
| ~~3.7h~~ | ~~Reaching a window without multicast~~ | ✅ **Closed by [E53](../specification/ppcp-core.md#errata-after-revision-9)** — it answered a question that does not arise. An ordinary MAY, no owner needed |

⛔ **Struck, 24 August 2026 — the premise was wrong and this paragraph was the loudest thing built on it.** It read that 3.7h decides whether the feature is worth building, because RV-6 over mDNS *"works in an office and not at a driving range, which is where CR-01 asked for it"*.

**The host is never at the range.** A capture device there works standalone and its session travels home as a bundle; the two peers meet on a home or coaching-studio network, which the user controls and where multicast behaves. **There is no venue at which RV-6 needs to work and does not**, [3.7h](../specification/ppcp-rv.md#37-the-bootstrap-window) is an ordinary MAY, and it has no owner because it needs none. See [E53](../specification/ppcp-core.md#errata-after-revision-9).

⚠ **Both teams escalated this and I escalated it twice more, over three passes, and none of us checked whether the venue existed.** That is the finding worth carrying out of CR-01 — larger than any of the twenty in the errata table, and the only one no review pass could have caught, because every reviewer had inherited the premise from the request itself.
