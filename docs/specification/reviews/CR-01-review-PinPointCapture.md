# CR-01 disposition and `PPCP-RV` §11 — review from PinPointCapture

| | |
|---|---|
| **Reviewing** | [CR-01 disposition](../libppcp/docs/changerequests/CR-01-disposition.md) and `PPCP-RV` revision 9 — §11 (RV-6), §3.5e, §3.7, §10.4, errata E30–E33 |
| **By** | PinPointCapture, 24 August 2026 |
| **Verdict** | **Accept.** No blocking finding. Two items to change, one of them measured; three notes; the seven asks answered |
| **Measurements** | Run on **macOS 27.0** and on the **iOS 18 simulator SDK**, arm64, identical results. Scripts reproduced in Annex A |

---

## 1. Verdict

The ruling is right and the section is good work. Granting the transfer and refusing the operator is the correct split, and §11.1's opening — *authentication cannot be manufactured from nothing* — is the sentence the request should have led with rather than reaching for in §5.

**The disposition's §3 correction is accepted in full and is the most valuable thing in it.** CR-01 §6 concluded that 6.2 bounds the design space because *"any bootstrap in which the capture peer listens on Apple platforms inherits it"*. That was reasoning from the measurement without re-examining its precondition: F-D1-1 is about a listener failing to **resolve a PSK identity**, and a bootstrap that carries no PSK presents no identity to resolve. The error was ours, it was the kind this project has an explicit habit against, and 11.2a is right.

**One correction back, in the same spirit:** the disposition's §3 says the first-contact half of "Studio finds the phone" *"can [be delivered], and now is"*. On the deployment described, first contact has **the capture device advertising and the host dialling** (11.2b). That is Studio *finding* the device, and it is what the request wanted — but it is not Studio being the one that listens, and a reader skimming for "PPS does discovery" could take it that way. The two-row table in §3 says it precisely; the sentence above it is looser than the table.

---

## 2. Findings

### F-R9-1 — 11.6b's trigger is not observable on Apple platforms ⚠ change requested

> **(11.6b) MUST** A peer whose key agreement produces an **all-zero** `Z` aborts with `invalid_key` and derives nothing.

**Measured.** `CryptoKit.Curve25519.KeyAgreement` does not return an all-zero `Z` for a small-order public key. It **throws** before producing one:

| Public key offered | `sharedSecretFromKeyAgreement` |
|---|---|
| all-zero `u` | `THREW CryptoKitError.underlyingCoreCryptoError(-7)` |
| order-8 point `e0eb7a…b800` | `THREW CryptoKitError.underlyingCoreCryptoError(-7)` |
| `p − 1` | `THREW CryptoKitError.underlyingCoreCryptoError(-7)` |

Identical on macOS 27.0 and the iOS 18 simulator SDK.

⛔ **The behaviour 11.6b wants is what happens; the condition it names never becomes observable.** A conformant PinPointCapture cannot test `Z == 0` because it never holds such a `Z`. As written, an implementer either writes an unreachable branch and believes it is defended, or concludes the clause does not apply and writes no handling at all.

This is E23's shape exactly: a clause phrased around an API behaviour one platform does not expose.

**Suggested wording** — the requirement is on the *outcome*, so state it there:

> **(11.6b) MUST** A peer aborts with `invalid_key` and derives nothing where the key agreement yields an all-zero `Z` **or where the platform's key agreement rejects the counterpart's public key outright**. Some implementations surface a small-order point as an error rather than as a zero output — CryptoKit throws and never returns the zero — and both are the same refusal. A peer MUST NOT treat such an error as a transport failure or retry it.

The last sentence matters more than it looks: a rejected key is an attack signal, and a retry loop around it interacts badly with 3.7b's single-attempt bound.

### F-R9-2 — 11.2c's claim is very slightly too strong ⚠ precision, not a defect

> **(11.2c)** … an observer who records the entire exchange **learns nothing that helps it**, because the secret is the Diffie-Hellman output and that is never sent.

11.2c invites the reviewer to *"name a value on this connection whose disclosure to a passive observer weakens the pairing"*. Taking that invitation seriously: **`mac_i` and `mac_a` are an offline verifier for `Z`.** Both descend from `Z` by public functions, so an observer holding the transcript can test any candidate `Z` — and therefore any candidate ephemeral private key — without further interaction.

Against a CSPRNG this is worth nothing; X25519 has no useful search space. It is worth stating because it changes what a **weak or backdoored RNG** costs. Without the transcript, a bad ephemeral key is exploitable only by an attacker present at the time. With it, a passive observer who recorded the exchange in March can recover the `PRK` in June and read every session keyed from it — and §11.6f erases `Z` on both peers, so neither end retains anything that would reveal it had happened.

That is the real force of 11.5a's MUST, and it is currently justified only by an impersonation argument. Suggest adding to §11.8's *what it does not prove*:

> - **It does not survive a weak ephemeral.** The confirmation MACs descend from `Z` by public functions, so a recorded transcript is an offline verifier for it. An attacker who can predict either peer's CSPRNG recovers the `PRK` from a transcript alone, at any later date, with nothing observable at either peer. This is why [11.5a](#115-the-exchange) is a MUST and why [11.6f](#116-derivation)'s erasure does not mitigate it.

Not a change to any mechanism.

---

## 3. Notes — checked, no change sought

**N1 — the modulo bias in 11.7a is real and immaterial.** `sas_raw` is 32 bits reduced mod 10⁶, and 2³² is not a multiple of 10⁶: 295 967 296 of the residues have 4 295 preimages and the rest 4 294. An attacker targeting the most probable string gains a factor of about 1.000 23 over 2⁻²⁰. Recorded so that a later reader who spots the bias does not raise it as a defect.

**N2 — `PPCP-ENC` 2a does reserve channel 255**, verified in revision 9: *"Channel `0` is the control channel. Channels `1` and above are bulk channels. Channel `255` is reserved."* The disposition's recommended `ENC` erratum adding the cross-reference is supported; PinPointCapture would rather it landed than not, for the reason E25 exists.

**N3 — 11.7e is well-defined for both roles**, which is worth recording because it reads asymmetrically. "Before it has completed 11.5d" means *having sent `bs_reveal`* for the initiator and *having verified `ct`* for the acceptor. The initiator can derive as soon as `bs_accept` arrives, so without 11.7e it could legitimately display a half-exchange. It does not. No change.

---

## 4. The seven asks

### Ask 1 — discharge B14 ✅ **discharged, positively**

Raw X25519 through CryptoKit's public interface, no TLS, against RFC 7748 §6.1:

```
alice public   8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a  ✅
bob   public   de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f  ✅
shared secret  4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742  ✅
```

All three properties A16 depends on hold, and each was in doubt:

1. **A private key can be constructed from raw octets** — `Curve25519.KeyAgreement.PrivateKey(rawRepresentation:)` accepts the RFC's fixed key. Had CryptoKit only generated keys, §10.4 would be untestable on this platform.
2. **The shared secret's raw bytes are readable** — `SharedSecret` is `ContiguousBytes`. Had it been KDF-only, 11.6c's `HKDF-Extract(salt, IKM = Z)` could not be expressed and A16 would have needed reopening.
3. **The result agrees with RFC 7748.**

⚠ **This is not the same shape as the TLS 1.3 PSK belief after all, and it is worth saying why**, because the analogy is what made the ask urgent. The PSK gap was structural — `tls_ciphersuite_t` contains no PSK entry, so the suite could be neither requested nor withheld through *any* public API. X25519 here is a first-class primitive with raw-bytes in and raw-bytes out. The belief happened to be true; the ask was still correct, and item 3 above found something.

### Ask 2 — recompute §10.4 independently ✅ **14/14 rows reproduce**

Recomputed **twice**, deliberately by different routes: once in Python with X25519 (RFC 7748 §5 Montgomery ladder) and HKDF (RFC 5869) implemented from the RFCs, once through CryptoKit. Neither shares code with the other or with the specification.

`pk_i` · `pk_a` · `ct` · `Z` (both directions agree) · `BK` · `sas_raw` · `K_c` · `mac_i` · `mac_a` · `expand` · `sid` · `PRK` · `K_tls` · `K_id` — **all reproduce byte for byte.** `sas_raw` = `11e66a4c` = 300 313 164, SAS = **313164**, `Session.id` = `1cc4b886-e8bd-45e0-a3b2-07ae783bc56b`.

⛔ **The `PRK` row specifically reproduces**, which is the one the disposition singles out. The three failure modes it names were each checked by construction: `sid` is salted *after* the version and variant bits are set (salting `1cc4b886e8bd65e0…` instead gives a different `PRK`); `Z` agrees computed from either side; and the `sas_raw` info is `pk_i || pk_a`, which is order-sensitive.

### Ask 3 — attack §11.8's ordering ✅ **no reordering found**

The question: is there an ordering, or a path to derivation, in which the attacker learns a public key before committing to its own?

With M interposed, M is acceptor to the initiator (leg 1) and initiator to the acceptor (leg 2). Four orderings were considered:

| Attempt | Why it fails |
|---|---|
| M runs leg 1 to completion, then leg 2 | On leg 2, M is initiator and must send `ct_M2` first — **before** `pk_a` exists to it. Knowing `pk_i` does not help: leg 2's digits depend on `pk_a` |
| M opens leg 2 first, learns `pk_a`, then chooses `pk_M1` adaptively | Legal, and it does not help. Leg 1's digits need `Z₁ = X25519(sk_i, pk_M1)`, and M holds only `ct_i` — a hash of `pk_i`. M cannot compute the digits it is trying to match |
| M interleaves so both legs are mid-flight | Same two constraints bind independently; neither is relaxed by the other's state |
| M is acceptor to both, or initiator to both | **Stronger** for the defence — both peers commit first, or M commits twice blind |

The construction holds because each leg is bound by a *different* mechanism: leg 1 by the initiator's commitment, leg 2 by the acceptor revealing first. An attacker needs both steerable and can never have more than neither.

Reflection is closed by 11.5f's distinct labels plus the own-value abort. Running one leg only is closed by 11.1b — the other screen never shows digits, and the operator is comparing two.

⛔ **11.5c is the load-bearing clause and its warning is correct.** Sending `pk_a` only after receiving `pk_i` saves a round trip, looks like an obvious optimisation, and hands the attacker adaptive choice on the leg that currently constrains it — while changing nothing any external test observes. Recommend this be called out in the implementation guidance as well as in the clause, because it is the sort of thing found during a latency pass six months later.

### Ask 4 — rule on 11.7b, both peers display six digits ✅ **holds for PinPointCapture**

No screenless capture peer exists in this roadmap or is planned. Every device that would implement RV-6 here is an iPhone or iPad; the Android port (E25, v3) is also a screened device.

⚠ **One adjacent case worth the protocol team's awareness, which is not a screen problem.** Multi-device stereo (UC-6, E22, v3) puts two or three phones on one host. Each has a screen, so 11.7b is satisfied — but pairing a bay costs one operator confirmation *per device*, which is B15's fleet case arriving through a different door than the one the disposition anticipated. Not a request to change §11; a note that B15's motivation is stronger than CR-01's "several bays" made it sound.

### Ask 5 — are 11.7d and 11.9c buildable ✅ **yes, on this platform**

**11.7d** — a SwiftUI `.alert` with the affirmative control given a non-default role and the *"do not match"* control given `.cancel` makes refusal the emphasised button and the one a Return keypress or stray tap reaches. Grouping as `313 164` is trivial. Phrasing the prompt as *do these numbers match?* is copy.

**11.9c** — copy, and buildable. ⚠ Worth flagging that it cuts against the platform's grain: iOS convention for a failed network operation is a *Try Again* button, and it is what a reviewer will expect to see. That it is a MUST NOT is the right call and the reason should stay in the document, because it will be questioned by someone who does not know why.

### Ask 6 — PinPointStudio only

Not ours. ⚠ PinPointCapture's side of 3.5e is already built: `browse(against identityKeys:)` resolves every held pairing per 3.4b and refuses an unresolvable `rid` per 3.4c, and the connect screen was designed around a discovered *host*. If PinPointStudio does not advertise, that code has nothing to find and §7.4's persistence delivers nothing on this deployment — as the disposition says.

### Ask 7 — is RT-20 runnable between us ✅ **yes, and PinPointCapture will host it**

It needs a deliberate relay between two real implementations, which this project already has the shape for — `tools/` carries three interop harnesses that drive the shipping path against `ppcp-sim` and against PinPointStudio's real listener, including a two-sided one that reads the host's own log rather than trusting the client's verdict. A relaying proxy on the bootstrap port is a fourth of the same kind.

⛔ **Two conditions.** It cannot run until PinPointStudio implements §11 — a relay needs two real ends, and a harness standing in for one would be testing itself. And the disposition is right that **until RT-20 runs, §11 is a design with vectors and not a demonstrated one**; PinPointCapture will not claim conformance to RV-6 before then, whatever its unit tests say.

---

## 5. What PinPointCapture will do next

- **Nothing implemented against §11** until B14 is formally discharged and this review is answered, per the disposition's status line. B14's measurement is above; the discharge is the protocol owner's to record.
- The two measurements will be **landed as permanent tests** in this repository — the §10.4 vectors and the RFC 7748 agreement — so a CryptoKit or toolchain change that breaks either is caught by CI rather than by a failed pairing at a range.
- F-R9-1's wording change is wanted **before** implementation, since it decides whether the `invalid_key` path is written at all.

---

## Annex A — reproducing the measurements

Both scripts are standalone and depend on nothing in either repository.

**A.1 — the vectors, independent of Apple.** Python, X25519 from RFC 7748 §5 and HKDF from RFC 5869, implemented in the file. Prints the RFC 7748 §6.1 self-check first, then every §10.4 row against the specification's value.

**A.2 — the platform, B14.** Swift against CryptoKit. Run on macOS with `swift b14.swift`; for the iOS SDK, `swiftc -sdk $(xcrun --sdk iphonesimulator --show-sdk-path) -target arm64-apple-ios18.0-simulator b14.swift -o b14-ios` and `xcrun simctl spawn <booted-udid> ./b14-ios`. Section 3 of its output is F-R9-1's measurement.

⚠ Both were run on the iOS **simulator** SDK, not on hardware. CryptoKit is not a hardware-dependent framework and the two agreed with macOS exactly, so a device run is not expected to differ — but it has not been done, and this review does not claim it has.
