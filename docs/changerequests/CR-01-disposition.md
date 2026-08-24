# CR-01 — disposition

| | |
|---|---|
| **Request** | [CR-01 — an authenticated bootstrap for a first pairing](CR-01-in-band-pairing.md), PinPointCapture, 24 August 2026 |
| **Ruling** | **Granted in part.** The code goes. The operator does not. |
| **Specified in** | [`PPCP-RV`](../specification/ppcp-rv.md) revision 9 — **[§11, RV-6](../specification/ppcp-rv.md#11-rv-6--guided-pairing)**, [§3.7](../specification/ppcp-rv.md#37-the-bootstrap-window), [§10.4](../specification/ppcp-rv.md#104-guided-pairing), errata **E30–E33** |
| **Decided by** | Protocol owner, 24 August 2026 |
| ⚠ **Premise** | **CR-01's stated situation — *"a venue where a range operator sets up several bays"* — does not describe this deployment.** The host is never at the range. Confirmed by the protocol owner 24 August 2026, after four review passes none of which questioned it. The *requirement* still stands and RV-6 is kept; the **justification is narrower** than §3 below argues. See [E53](../specification/ppcp-core.md#errata-after-revision-9) and [§11's preamble](../specification/ppcp-rv.md#11-rv-6--guided-pairing) |
| **Status** | **Reviewed and accepted by both teams**, 24 August 2026 — [PinPointCapture](../specification/reviews/CR-01-review-PinPointCapture.md), [PinPointStudio](../specification/reviews/CR-01-review-PinPointStudio.md). Six findings, all applied as errata E34–E39; see the [response](CR-01-review-response.md). **§6 of this document is the ask-list they answered and is kept as written** |

---

## 1. The ruling

**Granted in part.** `PPCP-RV` gains a fourth path, **RV-6 — guided pairing**: two peers that have never met establish a pairing over a committed ephemeral exchange, authenticated by **six digits that appear on both screens and are affirmed at both ends**.

The half that is granted is the one the request is actually about — **the transfer**. Nothing is carried from one screen to the other, nothing is typed, no camera is involved, and the operator needs no line of sight between the two devices.

The half that is refused is **the operator**, and the refusal is not a judgement call.

> Authentication cannot be manufactured from nothing. Two peers meeting for the first time on a network an attacker may control share no secret, and nothing they say to each other distinguishes the intended counterpart from someone sitting between them relaying both halves. Every scheme that appears to escape this imports its trust from outside the channel: a printed code, a certificate authority, physical contact, or a person. There is no fourth kind.

The request's §5 says the same thing from the other side and is right to. Given that, the design question was never *how do we remove the human* — it was **how small can the human act be made, and how few chances does the attacker get at it**. Six compared digits, one attempt per operator action, is the answer.

**The request was raised correctly.** It reported no defect, stated a requirement rather than a mechanism, proposed no scheme, and implemented nothing ahead of the ruling. Its §7 is right that a plausible-looking scheme arriving with a change request is harder to discard than no scheme at all, and this disposition would have been worse work if it had arrived with one.

---

## 2. The three questions, answered

### Q1 — Is an authenticated first-contact bootstrap in scope for `PPCP-RV`, or deliberately out of it?

**In scope, and now in the document.** It was absent rather than excluded: revision 8 never considered it, which is why §3 says *"a first pairing always uses §4"* as a statement of fact rather than as a decision with reasoning behind it. There was no argument to overturn.

It is **OPTIONAL**, alongside service discovery and network join ([9d](../specification/ppcp-rv.md#9-conformance)), and it does **not** relieve a peer of 2a: **the pairing code stays REQUIRED** ([9f](../specification/ppcp-rv.md#9-conformance)). §3.6 is why — guided pairing is normally reached over multicast, at exactly the kind of venue where multicast does not work.

### Q2 — A fourth path in §2's table, or an extension of the pairing-code path?

**A fourth path**, and the deciding argument is 2c rather than tidiness.

An extension of §4 would have had to reach inside the code path's key schedule, where `PRK` descends from a printed `psk`. A fourth path does not: it produces a `PRK` by other means, closes its connection, and hands over to §5 unchanged. **2c is therefore unweakened rather than excepted.** There is still no unauthenticated rendezvous path, no new reading of §5, and — this is what made it worth the extra section — no second shape of `§5`, which is what an in-place TLS upgrade of the bootstrap connection would have cost ([A17](../specification/ppcp-rv.md#annex-a--decisions-and-alternatives)).

### Q3 — Is PinPointStudio going to advertise for reconnection?

**That remains PinPointStudio's to confirm, but it is no longer a question the specification leaves open on which peer *should*.** New clause [3.5e](../specification/ppcp-rv.md#35-who-advertises-and-who-browses):

> Where a peer's counterpart **cannot** advertise for reconnection under 3.5d, the peer that **can** advertises.

The request was right that this needed asking, and the gap was worse than it looked. 3.5d says only who must *not* advertise. Read together with 3.5b's recommendation that the capture peer does, a deployment could conclude that **neither** end advertises and satisfy every clause in the document while doing so. In that deployment §7.4's persisted pairing buys nothing: both peers hold valid key material, no path exists by which either finds the other, and the users see a protocol that remembers them and still asks for a code every session.

It is a SHOULD rather than a MUST only because §3 as a whole is optional and a host reachable at a cached endpoint has another way.

---

## 3. What the request did not spot, and it is the useful part

CR-01 §6 concludes:

> ⚠ **6.2 is the one that bounds the design space.** Any bootstrap in which the *capture peer* listens on Apple platforms inherits it.

**That is true of a bootstrap built on TLS-PSK. It does not bind one that carries no PSK.**

At first contact there is no pre-shared key, so the bootstrap is not a TLS-PSK connection, so there is no PSK identity for a listener to fail to resolve. `Network.framework`'s missing server-side resolver — the measurement behind E23 and 3.5d — has nothing to bite on. [11.2a](../specification/ppcp-rv.md#112-why-it-is-not-tls-and-what-that-unlocks) therefore leaves the **bootstrap's dialling direction free**: either peer may open the window, either may dial it, whatever its platform.

The consequence is the feature as it was actually asked for:

| | Who advertises | Who dials | Why |
|---|---|---|---|
| **First contact** (§11) | either — on this deployment, the **capture device** opens the window | either — on this deployment, **the host PC** | No PSK is involved, so 3.5d does not reach it ([11.2a](../specification/ppcp-rv.md#112-why-it-is-not-tls-and-what-that-unlocks)) |
| **Every session after** (§3, §5) | the **host** ([3.5c](../specification/ppcp-rv.md#35-who-advertises-and-who-browses), [3.5e](../specification/ppcp-rv.md#35-who-advertises-and-who-browses)) | the **capture device** | 3.5d leaves it no choice |

So **PinPointStudio can discover a phone and connect to it** — at first contact, which is where the operator is standing and where the request said the pain is. The peers then swap roles for the pairing that follows ([11.2b](../specification/ppcp-rv.md#112-why-it-is-not-tls-and-what-that-unlocks)), and the steady state stays exactly where CR-01 §2 correctly declines to reopen it.

> ⚠ **Corrected 24 August 2026, at PinPointCapture's request in its review.** The sentence above is looser than the table above it, and the review was right to say so. On this deployment the **capture device advertises the bootstrap window and the host dials it**. That is Studio *finding* the device, which is what the request wanted — but Studio is **not** the peer that listens, and a reader skimming for "PPS does discovery" could take it that way. The table is the precise statement; this note is here rather than a silent edit because the disposition is a document both teams have already read.

**CR-01 §2's ⚠ needs one correction on this point.** It reads: *"Both halves of a 'Studio finds the phone' product story therefore cannot be delivered on Apple platforms by a specification change alone."* The reconnection half indeed cannot. The **first-contact half can**, and now is.

---

## 4. The mechanism, in brief

Full text is [§11](../specification/ppcp-rv.md#11-rv-6--guided-pairing); vectors are [§10.4](../specification/ppcp-rv.md#104-guided-pairing).

```
  initiator                                        acceptor
      |  1.  bs_offer   { v, ct }                      |   ct = SHA-256("ppcp1 bs-commit" || pk_i)
      | ---------------------------------------------> |
      |  2.  bs_accept  { v, pk_a }                    |   acceptor reveals having seen only a hash
      | <--------------------------------------------- |
      |  3.  bs_reveal  { pk_i }                       |   acceptor verifies ct
      | ---------------------------------------------> |
      |      both derive; both DISPLAY six digits      |
      |      each waits for ITS OWN user               |
      |  4.  bs_confirm { mac } — both directions      |
      | <--------------------------------------------> |
```

Then `PRK = HKDF-Extract(salt = sid, IKM = Z)`, and **§5.1 takes over unchanged**.

**Why the commitment is the whole thing.** Without it, an interposed attacker picks its key after seeing the honest one and grinds 2²⁰ candidates until both legs show the same digits — seconds of work, and the comparison then proves nothing. With it, neither leg's digits can be steered, so the attacker's best play is to pick blind and hope both legs collide: **one chance in 1 000 000**, and a miss is a mismatch on two screens with an operator looking at both. *(Corrected 24 August 2026 by [E54](../specification/ppcp-core.md#errata-after-revision-9): this read 1 048 576, which is 2⁻²⁰ — the width of `sas_raw` rather than of the six digits an attacker must match.)*

**What bounds the retries matters more than the number.** [3.7a](../specification/ppcp-rv.md#37-the-bootstrap-window), [3.7b](../specification/ppcp-rv.md#37-the-bootstrap-window) and [11.3d](../specification/ppcp-rv.md#113-roles-and-the-connection) together make the attacker's expected work *one million operator confirmations* rather than one million packets: the window is user-opened, closes on the first abort or rejection, will not reopen without a further user action, and runs one attempt at a time. **Remove any one of those three and the attack becomes a loop.**

---

## 5. What this does not do, stated plainly

- **It does not remove the operator.** §1 above. If that was the requirement, the request is **declined**, and the pairing code is the answer.
- **It does not serve the fleet case.** CR-01's motivation is *"a range operator sets up several bays"*, and §11 still costs one confirmation per device per host. What collapses that to one per device is a venue-scoped enrolment credential — which is a **group credential**, which is what 7.4f forbids persisting and what [B2](../specification/ppcp-rv.md#annex-b--open-issues) says has no revocation story. Recorded as [B15](../specification/ppcp-rv.md#annex-b--open-issues) and deliberately not attempted: its prerequisite is B2's per-peer re-keying, not a fifth rendezvous path.
- **It does not change the steady state**, exactly as CR-01 §2 asked that it not be read to.
- **It does not restore forward secrecy.** The bootstrap exchange is itself ephemeral, but the `PRK` it produces persists and §5.4.3 binds every session keyed from it. It *does* remove the photographable secret — a guided pairing's key material is never displayed to a room — which is a consequence worth recording and **not** the reason the request was granted.
- **It adds a privacy cost.** An open window announces that a peer here will pair with a stranger right now, and `dl` puts an operator-chosen string on the venue's network against 3.3b's intent. Both are stated at [3.3g](../specification/ppcp-rv.md#33-txt-record) and [3.7g](../specification/ppcp-rv.md#37-the-bootstrap-window) rather than buried. `dl` is admitted because the several-bays workflow does not function without it.

---

## 6. What both teams are asked to check

§11 is the least-proved section in this document. Every other one has had at least three review passes; §4 has had three independent recomputations of its vectors. **This has had none.** In descending order of how badly it would hurt to get wrong:

| | Ask | Why |
|---|---|---|
| **1** | **Discharge [B14](../specification/ppcp-rv.md#annex-b--open-issues) before writing any code.** Run a raw X25519 agreement between two locally generated keypairs on your platform, through its public interface, with no TLS involved, and check the output against [RFC 7748 §6.1](https://www.rfc-editor.org/rfc/rfc7748#section-6.1). Report either way. | [A16](../specification/ppcp-rv.md#annex-a--decisions-and-alternatives) mandates X25519 on the *belief* that CryptoKit's `Curve25519.KeyAgreement` and the host's crypto library both expose it. **That belief is the same shape as the one §5.4 held about TLS 1.3 external PSK for four drafts before anyone ran it**, and this document has already paid once for reasoning from availability annotations instead of a measurement. A negative result reopens A16 only, not the section. |
| **2** | **Recompute [§10.4](../specification/ppcp-rv.md#104-guided-pairing) independently, both of you.** Every row, particularly `PRK`. | Independent recomputation is what caught the §4.3b defect that three readings had missed. The `PRK` row is where a disagreement surfaces: two peers that agree on all six digits and disagree on the `PRK` show the operator a successful comparison and then fail the TLS handshake with `PSK_IDENTITY_NOT_FOUND` — **a failure that looks exactly like the 3.5d platform limitation and will be diagnosed as one.** The three likely causes are all in the vector: `sid` salted before its version and variant bits are set, `Z` with transposed arguments, `sas_raw` info built as `pk_a \|\| pk_i`. |
| **3** | **Attack [§11.8](../specification/ppcp-rv.md#118-what-the-comparison-proves).** Specifically: is there any ordering of the five frames, or any way to reach the derivation, in which an attacker learns a public key before committing to its own? | That is the single property the path rests on. [11.5c](../specification/ppcp-rv.md#115-the-exchange) warns that sending `pk_a` only after receiving `pk_i` — a natural-looking reordering that saves a round trip — destroys the security entirely while changing nothing an external test can see. |
| **4** | **Rule on [11.7b](../specification/ppcp-rv.md#117-the-short-authentication-string): both peers must display six digits.** Does that hold for every peer either of you expects to ship? | It forbids headless guided pairing outright. If a screenless capture peer is in either roadmap, say so now — the answer is a short-code PAKE and [A14](../specification/ppcp-rv.md#annex-a--decisions-and-alternatives) reverses, which is a much cheaper decision before implementation than after. |
| **5** | **Confirm the UX obligations are buildable**: [11.7d](../specification/ppcp-rv.md#117-the-short-authentication-string) (acceptance not the default, prompt asks whether the numbers *match*) and [11.9c](../specification/ppcp-rv.md#119-aborting-and-the-one-attempt-rule) (a mismatch must not be reported in terms that invite a retry). | These are unusual clauses for a protocol document. They are there because a mismatch is the **one** signal this path produces that an attack is under way, and a dialogue whose reflex is *try again* converts the single-attempt bound into an unbounded one by way of the operator's muscle memory. If your platform's conventions make either unbuildable, that is a finding. |
| **6** | **PinPointStudio only — answer Q3 for the record.** Will you advertise `_ppcp._tcp` with `role: host` for reconnection? | [3.5e](../specification/ppcp-rv.md#35-who-advertises-and-who-browses) now says you should. §7.4's persistence delivers nothing on this deployment if neither end does. |
| **7** | **Both — is [RT-20](../specification/ppcp-rv.md#9-conformance) runnable between you?** | It needs a deliberate relay between two real implementations. No single-implementation harness can run it, and **until it runs, §11 is a design with vectors and not a demonstrated one.** No conformance claim should say otherwise. |

**One follow-up outside `PPCP-RV`.** [11.4a](../specification/ppcp-rv.md#114-frames) uses `PPCP-ENC` §3 framing with the reserved channel byte `255`, which `ENC` 2a reserves and nothing claims. That is deliberate — it makes a misdirected frame fail closed in both directions — but `ENC` 2a currently says only *"reserved"* and does not cross-reference the one thing now using it. Recommend an `ENC` erratum adding that cross-reference in the next revision. It is not urgent and it is exactly the "one idea spelled in two documents" shape that E25 had to clean up after, so it should not be left.
