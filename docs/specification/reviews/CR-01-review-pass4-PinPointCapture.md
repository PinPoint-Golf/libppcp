# `PPCP-RV` §11 and the X25519 seam — fourth-pass review from PinPointCapture

| | |
|---|---|
| **Scope** | E43–E48, the seam note, and E47 which landed unreviewed after the third pass |
| **Against** | `libppcp@19b6379` — `PPCP-RV` revision 9 as amended by E34–E48, and `CR-01-x25519-seam.md` |
| **From** | PinPointCapture, 24 August 2026 |
| **Verdict** | ⛔ **No objection to starting implementation.** One finding against the API surface, one platform question on 11.11h, and two corrections to our own third pass |

---

## 0. Everything numeric verified at `19b6379`

Independent implementation — X25519 from RFC 7748 §5, HKDF from RFC 5869, sharing no code with the specification or either product.

| Check | Result |
|---|---|
| §10.4's thirteen main rows | ✅ reproduce |
| Counter-vector, **cause 1** — `PRK` `9b779245…f64dc1ba` | ✅ and confirmed **silent**: digits `435948` and both MACs unchanged |
| Counter-vector, **cause 6** — `sid` `18dd04b1…bd2d0626` | ✅ and differs in the **first** octet, as stated |
| **R-11 witness** — `pk_a'` `87abc1e8…dad34208` | ✅ **`X25519(sk_i, pk_a')` is bit-identical to `Z`, and non-zero** |
| R-11's consequence — `BK`, `sid`, `PRK` identical under the substitution; only `sas_raw` separates | ✅ **435948 vs 485158**, exactly as published |
| E47's arithmetic — 15 min × 10 pairings | ✅ 2.5 hours; 20 s × 10 = 3.3 minutes |

⛔ **The R-11 witness is the most important thing in this pass and it holds.** One call confirms that X25519 is non-contributory here: a different public key produces the same shared secret, `11.6b` does not fire because the result is non-zero, and the *only* thing distinguishing the two peers is `sas_raw`'s explicit `pk_i ‖ pk_a`. Anyone inclined to think 11.6c2 over-cautious should run the third block, as §10.4 says.

---

## 1. Two corrections to our own third pass

**E43 escalates our precision point, and the escalation matters.** We wrote that *"`Z` already commits to both public keys by construction"* was **loose** — that X25519 is not a commitment scheme and the leg did no work. PinPointStudio's R-11 established it is **false**, with a witness, and worse than idle: the claim pointed directly at the one binding holding the property up, so a reader who believed it had been handed the argument for simplifying `sas_raw`'s key binding away. We identified the leg and not the reason. R-11 is the better finding and the clause is right to delete the claim outright rather than soften it.

**E44 corrects a sentence of ours that overreached.** Our third pass said *"the silent failure class has exactly two members, and 11.6c1 covers exactly those two."* The analysis under it was about **over-application sites** and is right within that scope; the sentence was not scoped and, read as written, is wrong — cause 1 (`sid` salted before its version and variant bits) has the identical signature and predates E34 entirely. Verified above: digits and both MACs correct, divergence only at `PRK`.

⚠ Same shape as our modulo-bias error: the computed part was checked and the sentence around it was not.

---

## 2. F-R9-5 — the API returns long-lived key material and offers no way to erase it ⚠ new

Against the seam note's §4, not against `PPCP-RV`.

`ppcp_rv_bootstrap` carries `k_c`, `mac_i`, `mac_a`, `sid`, **`prk`, `k_tls`, `k_id`**. So one call hands the caller the pairing's long-lived key material — and there is no `ppcp_rv_bootstrap_wipe()` in the surface.

Two obligations land on that struct and neither is served by the current shape:

⛔ **11.5g** — *"The pairing exists only when a peer has both affirmed at its own end and verified the counterpart's MAC. Until then it holds nothing."* The caller holds `PRK` from the moment of derivation, through the operator's affirmation, which 11.3e allows **60 seconds** for. Not a violation — computing is not persisting — but it means the window in which a peer holds key material it is not yet entitled to keep is a minute long, on a stack the caller manages.

⛔ **11.6f** — *"A peer erases its ephemeral private key, `Z`, `BK` and `K_c` when the handshake ends, whether it succeeded or failed."* The explicit list is four items and **`PRK` is not among them**. The closing sentence — *"what survives a failed one is nothing"* — does cover it, but an implementer working from a C struct will erase the four things named and leave `out` where it fell. On the abort paths that is a `PRK` for a pairing that does not exist, sitting in memory.

**Recommend adding to the surface:**

```c
/* Erases every field. Call on EVERY exit path — 11.6f's "what survives a
 * failed one is nothing" covers prk/k_tls/k_id even though its explicit list
 * does not name them, and this struct is where that gap becomes a bug. */
PPCP_API void ppcp_rv_bootstrap_wipe(ppcp_rv_bootstrap *out);
```

⚠ And suggest 11.6f's list gain `PRK` explicitly for the failure case. The sentence carries it; the list is what gets implemented from.

---

## 3. §11.11 against this platform — and one question we cannot answer

The seam is right, and *parameter rather than callback* is the correct call for the reason given: key agreement has no loop. Checked against what we measured on 24 August:

| Clause | On CryptoKit |
|---|---|
| **11.11a/b/c** — only `pk` and `Z` need the curve | ✅ our side computes exactly those two |
| **11.11e** — the scalar is not clamped before it crosses | ✅ `PrivateKey(rawRepresentation:)` accepts RFC 7748's **unclamped** scalars and produces the RFC's public keys, so CryptoKit clamps internally. ⚠ Moot for us — the scalar never crosses; CryptoKit generates and holds it |
| **11.11f** — a reported failure and an all-zero `Z` map identically | ✅ we are the **throw** half, measured: `underlyingCoreCryptoError(-7)` for the zero point, an order-8 point and `p−1`. We never see zeros |

### ⚠ 11.11h is only partly satisfiable here, and the specification should know

> **(11.11h) MUST** The private scalar and `Z` are erased by whichever component holds them.

On this platform the component holding both is **CryptoKit**. `Curve25519.KeyAgreement.PrivateKey` generates and retains the scalar; `SharedSecret` holds `Z`. Neither exposes a zeroise, and Apple documents zero-on-release for `SymmetricKey` but **not** for these two types. We can guarantee we hold no copy of our own; we cannot guarantee the framework does not, and we cannot verify it from outside.

⛔ Not a request to change the clause — the obligation is right and the alternative is worse. It is a note that on at least one platform 11.11h is **partly an obligation on a closed-source framework**, which is the kind of thing this programme has decided twice now to write down rather than assume. It is the same class as 5.4's PSK limits: not a defect, a bounded truth about what a MUST can mean here.

⚠ We can reduce exposure by deriving into a `SymmetricKey` (which *is* documented to zero) and letting the `SharedSecret` go out of scope immediately. That is what we will do, and it is a mitigation rather than a guarantee.

---

## 4. E47 — accepted, and the inversion is correctly identified

Arithmetic verified. More importantly the argument is right, and it is a good catch that only became visible once question 3 was answered: **3.4d2's mitigation assumes the multi-pairing peer is the one that can browse, and this deployment inverts it.** The host accumulates pairings *and* must advertise; browsing gains it nothing because 3.5d silenced its counterparts.

⚠ Worth noting the asymmetry is genuinely one-sided: a **capture device** holding several host pairings is fine, because 3.4d2 lets it resolve all of them in one browsing pass. It is only the host that is stuck rotating. So E47 is a clause about PinPointStudio's registration schedule and costs this repository nothing.

---

## 5. What the API shape buys, worth recording

The one-call design does something the note undersells: **it removes three of §10.4's six divergence causes for anyone using it.**

- `sas` is a `uint32_t` in `0..999999`, so **cause 5** — the little-endian misread — is unreachable; the caller never sees `sas_raw`.
- One call, one transcript construction, so **cause 6** — transcript into `sid` — cannot be made by a caller at all.
- `pk_i` and `pk_a` are named parameters with the order documented at the call site, which is the best available defence against **cause 3**.

That is a better argument for the shape than tidiness, and it belongs beside the *"deliberately one call, not six"* paragraph.

---

## 6. From this side, before implementation starts

- ⛔ **B14's device run is still outstanding.** Our iPhone has been paired-but-not-connected all day. It is a **ship** gate not a code gate, per 5.4b, so it does not block anyone starting — but it is the one item on our list that has not moved and we are not treating it as discharged.
- ✅ **We build acceptor-only**, per 9e1, and the RT-20 relay in `libppcp/tools`.
- ⛔ **3.7h remains the difference between a demo and a product.** It is now MVP scope in this repository rather than a refinement, and it is useless unless PinPointStudio implements it too. §6 of the seam note lists it correctly as *not a specification gap*; we would only add that it is the one open item with no owner named against it.

⚠ **On starting: no objection.** §11 has had four passes, seventeen errata, and every number in it has now been reproduced independently by two implementations sharing no code. The two things it has not had are a device and a relay, and both of those are ours and PinPointStudio's to produce rather than the document's.
