# CR-01 review pass 3 — PinPointStudio on E40–E42

| | |
|---|---|
| **Reviewing** | `PPCP-RV` revision 9 as amended by **E40–E42** (`b5685d0` → `4637191`) and the [second review response](../Projects/libppcp/docs/changerequests/CR-01-review-response-2.md) |
| **Scope** | Deliberately narrow, as asked: the text no reviewer has seen. **11.6c1, 11.4h1, RT-24a, RT-18's erratum-level requirement, the role-slack note by RT-20, and §10.4's sixth diagnostic cause.** Not a third full re-read — §1–§10 and the E34–E39 body are unchanged and were accepted twice. |
| **Reviewed by** | PinPointStudio, 24 August 2026 |
| **Position** | **E40's clause is right and its reasoning is not.** Three findings in the new text, one of which matters; plus an answer to the version question, which is a real problem with a cheaper answer than negotiation. |
| **Vectors** | Unaffected by E40–E42 and re-confirmed: all fifteen rows still reproduce. |

**Nothing was changed in the `libppcp` repository.**

The framing in the covering note is right, and worth repeating back: *§1–§10 yes unreservedly; §11 ready to implement but not finished* are different claims. Nothing below changes the second one into the first.

---

## 1. R-11 — *"`Z` already commits to both public keys by construction"* is false, and 11.6b does not repair it. **The one to fix.**

[11.6c1](../Projects/libppcp/docs/specification/ppcp-rv.md#116-derivation), under *"Why the boundary falls where it does"*:

> `Z` already commits to both public keys by construction ([11.6b](../Projects/libppcp/docs/specification/ppcp-rv.md#116-derivation) having refused the small-order cases where it would not), so the only element not implied by `Z` is `v`.

**X25519 is not contributory, and refusing an all-zero `Z` does not make it so.** Clamping forces every scalar to a multiple of 8, so for any point `T` of order dividing 8, `k·(P + T) = k·P`. A counterpart can therefore present a *different* 32-byte public key that yields a **bit-identical, non-zero** shared secret.

Concrete witness, from §10.4's own keys:

```
pk_a    675dd574ed7789310b3d2e7681f3790b466c773b1521fecf36577958371ea52f
pk_a'   87abc1e84c4c5572d2b1e63c69f5617a215518cf6261eb5a0e7db49ddad34208   (= pk_a + T, order-2)

X25519(sk_i, pk_a )  = 7c79d7b5f31b9aac367477f5f7c7a68b5c44cac28ed5c902a59ec48c02956a6a
X25519(sk_i, pk_a')  = 7c79d7b5f31b9aac367477f5f7c7a68b5c44cac28ed5c902a59ec48c02956a6a   identical
```

Verified twice, on implementations sharing no code: the RFC 7748 ladder used for §10.4, and **OpenSSL 3.6.3, which accepted `pk_a'` without complaint and returned the same `Z`**. 11.6b does not fire — `Z` is non-zero and the agreement succeeds. 11.6b rejects only the case where the *entire* offered key is small-order; a legitimate key plus a small-order component is not that case.

### Why this matters, given the clause itself is still correct

The **conclusion** of E40 is right and the construction is safe. But it is safe for a different reason than the one written down, and the written reason points at the thing that is actually holding it up.

Follow the substitution through:

| | acceptor | initiator (given `pk_a'`) |
|---|---|---|
| `Z` | `7c79…6a6a` | `7c79…6a6a` — **same** |
| `BK` | same | same |
| `sid`, `PRK` | same | **same** |
| `sas_raw` | binds `pk_a` | binds `pk_a'` — **differs** |

Every value E40 says needs no transcript binding is identical across a substituted public key. **The only thing that separates the two peers is `sas_raw`'s explicit `pk_i || pk_a`** — the binding the sentence describes as redundant, on the grounds that `Z` already does it.

A reader who believes the claim has been handed the argument for simplifying that binding away: *if `Z` commits to both keys, why name them in the info string?* Doing so would produce identical digits, identical MACs, an identical `PRK`, and no signal anywhere. This is [11.5c](../Projects/libppcp/docs/specification/ppcp-rv.md#115-the-exchange) and [A15](../Projects/libppcp/docs/specification/ppcp-rv.md#annex-a--decisions-and-alternatives)'s shape exactly — a clause whose removal reads as a simplification and whose removal is fatal — arriving through the reasoning rather than the clause, which is precisely the failure mode [E37](../Projects/libppcp/docs/specification/ppcp-rv.md#errata-after-revision-9--change-request-cr-01-and-its-review) was raised for one pass ago.

⚠ **To be clear about severity: this is not an attack.** The substitution gives an interposer nothing — it cannot compute `Z` without a private key, and the digits diverge, so the operator catches it. The cofactor behaviour is inherent to X25519 and is **not** a defect to fix with more checks. The defect is the sentence.

### The fix

The claim is not needed for the conclusion. Delete it and let the first half of the argument carry, which it does on its own:

> By the time `PRK` is derived the exchange has already been authenticated — by the comparison and by the MACs — so binding the transcript again adds nothing: a transcript that differed was caught two steps earlier. The transcript is bound where it is *checked*, and `sid` and `PRK` are downstream of both checks.

And, because the explicit key binding is now known to be load-bearing rather than belt-and-braces, [11.6c](../Projects/libppcp/docs/specification/ppcp-rv.md#116-derivation) deserves the [11.5c](../Projects/libppcp/docs/specification/ppcp-rv.md#115-the-exchange) treatment — one sentence saying that `pk_i || pk_a` in the `sas_raw` info is **not** redundant with `Z`, that X25519 is not contributory, and that removing it is undetectable from outside.

---

## 2. R-12 — §10.4's sixth cause is not the only one that pairs successfully first. Cause **one** has the identical signature.

[§10.4](../Projects/libppcp/docs/specification/ppcp-rv.md#104-guided-pairing):

> ⛔ **And a sixth, which is now the most likely of all and is the only one that produces a *successful-looking* pairing first** … **Every other cause in this list breaks the digits or the MACs**, so the operator is told something is wrong before affirming anything.

Cause 1 in the same list — *`sid` salted before its version and variant bits were set* — does not break the digits or the MACs. `sid` feeds only `PRK`; `sas_raw` and `K_c` descend from `BK` and the transcript. Computed:

```
digits   435948   identical on both sides
MACs     identical on both sides
PRK  correct   3e351aef1e5fe48411e969526b079830494d2cf13104d661694e897598ccf8c9
PRK  cause 1   9b77924572627d0e6d1c51fc679a3596ccd1c4a7dff7943da2ef856ef64dc1ba
```

So the list contains **two** causes with the same signature and tells the reader there is one. That paragraph exists to triage a `PSK_IDENTITY_NOT_FOUND` — its whole job is to be read by someone whose pairing succeeded and then failed — and as written it routes them to the newer cause and away from the older, more familiar one. Cause 1 predates E34 and is the mistake the vector's two-row `expand`/`sid` presentation was built to catch in the first place.

**Fix:** *"Two of these produce a successful-looking pairing — the first and the sixth — and they are indistinguishable until the `PRK` is compared."*

---

## 3. R-13 — RT-24a as a `static` row asserts nothing RT-18 does not. **Minor.**

> **RT-24a** | static | … This is RT-18's `PRK` row read as a **negative**: assert not merely that the value matches but that it was computed without the transcript.

A static vector test cannot draw that distinction. If the transcript were bound into `sid`, the `PRK` would not match, and RT-18's `PRK` row fails — there is no observable difference between "matches" and "matches and was computed the right way".

Two honest options, either fine:

- Make it **`review`**, alongside RT-25, and have it read as: the code binds the transcript into `sas_raw` and `K_c` and into nothing else. That *is* checkable, and it is the assertion 11.6c1 actually wants.
- Keep it `static` and describe it as what it is — RT-18's `PRK` row named for its two most likely causes, which per R-12 is genuine diagnostic value and worth a row of its own.

**Accepted without change:** RT-18's requirement that a reproduction record the erratum level it was taken against. That is the right response to a vector that moved once, and it is cheap.

---

## 4. R-14 — the version question. Real, and the answer is not negotiation.

**Confirmed, and the asymmetry is the part that is not written down.**

[11.4h](../Projects/libppcp/docs/specification/ppcp-rv.md#114-frames) requires an initiator to offer the highest `v` it implements and forbids proposing lower. [11.4e](../Projects/libppcp/docs/specification/ppcp-rv.md#114-frames) makes an acceptor that does not implement it abort. [11.9b](../Projects/libppcp/docs/specification/ppcp-rv.md#119-aborting-and-the-one-attempt-rule) forbids reopening without a further user action. So a v2 initiator and a v1 acceptor cannot pair by RV-6, ever, though both implement v1.

But it runs one way only. [11.4h1](../Projects/libppcp/docs/specification/ppcp-rv.md#114-frames) has the acceptor **echo what it received**, so a *newer acceptor* pairs happily with an older initiator at the old version. Only the *newer initiator* is stuck.

⚠ **On this deployment the stuck direction is the normal one.** PinPointStudio is the initiator — a desktop application updated whenever the user likes. PinPointCapture is the acceptor — a phone application whose rollout lags. So "host updates first, phones catch up over the following weeks" is exactly the case that breaks, and it is the default way these two ship.

### I would not add negotiation, and here is the argument

The covering note's analysis of the naive fix is right: a list reintroduces the downgrade unless the transcript binds the entire offered set *and* the selection, which is TLS 1.3's transcript logic. E34's machinery would support it. That is not the question — the question is whether it is worth it, and I do not think it is:

1. **A version mismatch is not a pairing failure.** RV-6 is OPTIONAL; [§4](../Projects/libppcp/docs/specification/ppcp-rv.md#4-rv-2--the-pairing-code) is REQUIRED and unaffected. The consequence of the stuck case is that the operator scans a QR code once — the entire pre-CR-01 experience, which both teams measured at 30/30. That is a mild degradation with a working escape, not an outage.
2. **[11.6g](../Projects/libppcp/docs/specification/ppcp-rv.md#116-derivation) and [A16](../Projects/libppcp/docs/specification/ppcp-rv.md#annex-a--decisions-and-alternatives) both argue that agility on a first-contact handshake is a downgrade surface**, and they are the same argument that made R-01 blocking. Adding a negotiation mechanism to close a gap whose fallback is the required path would be trading a permanent attack surface for a temporary inconvenience.
3. **§11 has two review passes and no code.** Transcript-hash negotiation is real machinery, and it would be the largest thing in the section that nothing has ever run.

### Three things I would do instead, all cheap and all now

1. **Make the fallback explicit and immediate for this case.** [11.9d](../Projects/libppcp/docs/specification/ppcp-rv.md#119-aborting-and-the-one-attempt-rule) offers the pairing code after **two** aborts in a sitting. For `unsupported_version` the second attempt is guaranteed to fail identically, so the code should be offered on the **first**. One clause, and it converts "permanently unpairable" into "pairs by code, this once" — which is the honest description and a perfectly good product outcome.
2. **Record the ship order.** One sentence, and it prevents the entire failure: *where two implementations bump `v`, the acceptor side ships first.* Cheap, and nobody will work it out from 11.4h and 11.4h1 sitting next to each other.
3. **Record the negotiation question in Annex B**, with the constraint already established: the only safe shape binds the full offered set and the selection into the transcript, and it cannot be introduced compatibly *later* — see R-15 — so a v2 that wants it must be designed with it. Bounded by (1) being acceptable, which is why this is an Annex B item and not a change.

---

## 5. R-15 — an unknown *key* in a bootstrap frame is undefined, and that decides whether R-14 is ever fixable. **One sentence, but a deliberate one.**

[11.4c](../Projects/libppcp/docs/specification/ppcp-rv.md#114-frames) enumerates what a peer aborts on: a frame out of order, an unknown frame **type**, a field of the wrong type or length, a duplicate frame. It says nothing about an unrecognised **map key**. The rest of the set points both ways — [3.3a](../Projects/libppcp/docs/specification/ppcp-rv.md#33-txt-record) has a TXT receiver ignore keys it does not recognise and [A4](../Projects/libppcp/docs/specification/ppcp-rv.md#annex-a--decisions-and-alternatives) chose CBOR partly for *"unknown-key tolerance"*, while [11.10a](../Projects/libppcp/docs/specification/ppcp-rv.md#1110-what-must-not-cross-a-bootstrap-connection)'s *"the five frames of §11.4 are its entire vocabulary"* reads as a closed set.

Two implementations can differ here without either being wrong, which is the RT-18 category of divergence. And the choice is not cosmetic: **it is the difference between a future `v2` being able to extend a v1 frame and not.** If unknown keys are rejected, R-14's gap is permanent by construction; if they are ignored, a later version has somewhere to put an offered-set field.

Either answer is defensible. I would state *reject* — it matches 11.10a's intent and the fail-closed posture of channel 255 — and then R-14's Annex B note becomes the honest record that negotiation, if ever wanted, is a v2-designed-from-scratch matter. What should not happen is leaving it unstated.

---

## 6. Accepted without change

- **11.4h1's acceptor-echo rule.** Going past what R-10 asked was right: R-10 asked which `v` is bound and the answer was unstatable without first saying what an acceptor puts in `bs_accept.v`, which nothing did. The rule chosen — echo, or abort; never substitute — is the one that makes 11.6c's transcript unambiguous under every reading, and the `1..255` bound closes the CBOR-width gap cleanly.
- **The role-slack note by RT-20.** Correct, and correctly filed as a programme risk rather than a defect. **One constructive addition:** RT-20's relay must be an acceptor toward one peer and an initiator toward the other, so **building it necessarily produces a third implementation carrying both roles** — which is the slack the note says does not exist. That is an argument for building the relay early rather than last, and for it living in `libppcp/tools` where both teams can point at it, which is already the decision.
- **RT-18's erratum-level requirement**, and §10.4's E42 corrections — the modulo-bias figures and the little-endian example now read correctly against my own recomputation.
- **The E40 clause itself.** 11.6c1 as a MUST NOT is right, and stating the boundary rather than relying on RT-18 is right. Only the supporting sentence is wrong.

---

## 7. Summary

| | Finding | Ask | Urgency |
|---|---|---|---|
| **R-11** | *"`Z` already commits to both public keys"* is false — witness verified on OpenSSL 3.6.3 | Delete the claim; add one sentence to 11.6c saying the explicit `pk_i \|\| pk_a` binding is **not** redundant with `Z` | **Before implementation.** It licenses removing the one binding that holds the property |
| **R-12** | §10.4 says one cause pairs successfully first; cause 1 does too | *"Two of these — the first and the sixth"* | With R-11; it is a diagnostic others will rely on |
| **R-13** | RT-24a asserts nothing RT-18 does not, as a `static` row | Make it `review`, or describe it as RT-18's `PRK` row named for its causes | Minor |
| **R-14** | Newer initiator + older acceptor permanently unpairable, and that is this deployment's normal ship order | Offer the code on the **first** `unsupported_version`; record the ship order; record negotiation in Annex B | Cheap now; the fallback makes it non-blocking |
| **R-15** | Unknown map keys in bootstrap frames undefined | State it — I suggest reject | With R-14, since it decides R-14's future |

**None of these touches the wire, the vectors or the security argument**, and none reopens the design. R-11 is a paragraph, R-12 a clause, R-13 a table cell, R-14 three sentences and an Annex B row, R-15 a sentence.

From this side that is the last of it. **§1–§10: yes, unreservedly.** **§11: implement it** — after R-11, which is a paragraph's work and is the difference between the next reader defending the `sas_raw` binding and simplifying it away. And RT-20 has still not moved, which the document now says in three places and should keep saying.

### Two things outside the specification, restated because they decide whether this ships

- **B17 blocks `libppcp` specifically**, and nobody has scheduled it. The reference library cannot implement §11 at all until the X25519 seam exists — and it is the shared dependency both applications build against by sibling path, so it gates both. It is the first code change the programme needs and the ordering is forced: **libppcp first**.
- **The venue problem is real and should not stay a footnote.** CR-01 asked for a driving range; [3.6a](../Projects/libppcp/docs/specification/ppcp-rv.md#36-multicast-is-not-to-be-relied-on) says multicast *"will not work"* there. RV-6 over mDNS delivers the feature everywhere except where it was asked for. The answer is [3.7h](../Projects/libppcp/docs/specification/ppcp-rv.md#37-the-bootstrap-window) — an endpoint learned out of band — which neither application supports and about which the specification correctly says nothing. R-06 sought no clause change and that was right. It does mean **RV-6 can be fully conformant and still not solve the problem it was raised for**, and the host-side work item is *how a window is reached without multicast* — not *how a window is dialled*.
