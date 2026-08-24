# CR-01 disposition — PinPointStudio's review

| | |
|---|---|
| **Reviewing** | [CR-01 disposition](../Projects/libppcp/docs/changerequests/CR-01-disposition.md), 24 August 2026, and `PPCP-RV` revision 9 §11 / §3.7 / §3.5e / §10.4 / §9 (RT-18 – RT-23), errata E30–E33 |
| **Reviewed by** | PinPointStudio (the host), 24 August 2026 |
| **Position** | **Accept the ruling.** The reasoning is sound, the fourth-path decision is the right one, and §3.3's correction of CR-01 §6.2 is correct and is the useful half. |
| **Blocking before implementation** | **R-01** and **R-02** below. Both are cheap now and expensive or impossible later; neither reopens the design. |
| **Nothing was changed in the `libppcp` repository.** | This document is the whole of PinPointStudio's response. |

---

## 1. The ruling, in one line

Granted in part is the right answer, and *"the code goes, the operator does not"* is the right place to draw the line. §11.1's argument — that authentication cannot be manufactured from nothing and every scheme that appears to escape it imports trust from outside the channel — is not a position PinPointStudio wishes to argue with, and the disposition is right that the remaining question was only how small the human act can be made.

**§3 of the disposition is the part that earns the revision.** CR-01 §6.2 concluded that Apple's missing server-side PSK resolver bounds any bootstrap in which the capture peer listens. It does not, because a bootstrap that carries no PSK gives that limitation nothing to bite on, and 11.2a follows. PinPointStudio did not spot that either, and it is what converts the answer from *"the code stays"* into a feature: at first contact the host browses and dials, which is the shape this application already has.

---

## 2. The seven asks, answered

### Ask 1 — Discharge B14: raw X25519 through the platform's public interface, no TLS

**Discharged for the host, with one structural caveat the disposition does not cover.**

Measured 24 August 2026 on the M4 Mac mini against **OpenSSL 3.6.3**, which is the library PinPointStudio's PPCP transport already links (`src/Ppcp/ppcp_transport.cpp` uses `EVP_*` directly, and `cmake/PinPointOpenSSL.cmake` locates it). Raw agreement through `EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, …)` + `EVP_PKEY_derive`, no `SSL_CTX` in the process:

```
RFC 7748 §6.1
  alice pub  8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a
  bob   pub  de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f
  K (a→b)    4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742
  K (b→a)    4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742   MATCH

RV §10.4
  pk_i       358072d6365880d1aeea329adf9121383851ed21a28e3b75e965d0d2cd166254   MATCH
  pk_a       675dd574ed7789310b3d2e7681f3790b466c773b1521fecf36577958371ea52f   MATCH
  Z          7c79d7b5f31b9aac367477f5f7c7a68b5c44cac28ed5c902a59ec48c02956a6a   MATCH
```

So A16's belief holds on this side, and it holds on a measurement rather than on an availability annotation. B14 may be closed for the host once PinPointCapture reports.

⚠ **The caveat, and it is not small.** A16 says *"the host's crypto library"*. **`libppcp` does not have one.** `include/ppcp/hash.h` states the position explicitly — *"No OpenSSL, no libsodium, no platform crypto: plan A1 says libppcp has no dependencies, and REQ-LIC-2/3/5 are why"* — and implements SHA-256, HMAC and HKDF in-library because all three are short enough to read. X25519 is not: it is constant-time field arithmetic over 2²⁵⁵−19, and it is the one primitive in §11 that a library with `libppcp`'s licence and dependency constraints should not hand-roll and cannot vendor without breaking A1.

A grep of `libppcp` confirms it: no `X25519`, no `Curve25519`, no curve arithmetic anywhere outside `docs/`.

**Recommendation.** Specify the seam rather than leave it to two implementations to invent. `libppcp` already has exactly the right precedent — `ppcp_rv_random_fn` (`include/ppcp/rv.h:123`), an injected callback, because RV 7.2a is the embedding's obligation and the library never generates a random byte. X25519 is the same shape: the library owns the framing, the commitment, the HKDF chain, the SAS and the MACs, and takes `ppcp_rv_x25519_fn` from the embedding. On this host that is thirty lines of OpenSSL; on PinPointCapture it is `Curve25519.KeyAgreement`. Without that seam stated somewhere, §11 either forces a dependency into a library that has none, or produces two incompatible integration shapes.

This is a `libppcp` API question rather than a `PPCP-RV` one, but A16 is where the assumption is written down, so it belongs in this reply.

---

### Ask 2 — Recompute §10.4 independently

**Every row reproduces byte for byte. RT-18 passes on this side.**

Recomputed from the specification text alone, with an implementation sharing no code with the author's: the RFC 7748 Montgomery ladder written from the RFC, and HKDF/HMAC over Python's `hashlib`. Cross-checked `pk_i`, `pk_a` and `Z` a second time against OpenSSL.

| Row | Value | |
|---|---|---|
| `pk_i` | `358072d6365880d1aeea329adf9121383851ed21a28e3b75e965d0d2cd166254` | ✔ |
| `pk_a` | `675dd574ed7789310b3d2e7681f3790b466c773b1521fecf36577958371ea52f` | ✔ |
| `ct` | `f32cd8e62f80f76adb4ba21971efbd10eb71aa6715d9e458f5422c1644357a3a` | ✔ |
| `Z` (both directions agree) | `7c79d7b5f31b9aac367477f5f7c7a68b5c44cac28ed5c902a59ec48c02956a6a` | ✔ |
| `BK` | `b9f16f38e5a45ec6c0563b4fd3b38b696dfbbf4e3491fe1b7941a62637099349` | ✔ |
| `sas_raw` | `11e66a4c` → `300313164` → **`313164`** → `313 164` | ✔ |
| `K_c` | `da18828cffc40cddfbf43ed2d1d3ff16d30fb9dc25989041b5eb71a26239e092` | ✔ |
| `mac_i` | `2785c15fc343b3edc5a98755bf7b69b0` | ✔ |
| `mac_a` | `7046dd47d329be70dfda16c59315a783` | ✔ |
| `expand` | `1cc4b886e8bd65e063b207ae783bc56b` | ✔ |
| `sid` | `1cc4b886e8bd45e0a3b207ae783bc56b` | ✔ |
| `Session.id` | `1cc4b886-e8bd-45e0-a3b2-07ae783bc56b` | ✔ |
| **`PRK`** | **`3e351aef1e5fe48411e969526b079830494d2cf13104d661694e897598ccf8c9`** | ✔ |
| `K_tls` | `240b513437501f3ab8602b06b45cd84577f10f126bdc497d3cf797c9559856b0` | ✔ |
| `K_id` | `9e8c8b155b89fcc9b70f4043ddaa607a7ff7acec20dc326f5c307661956a0bd9` | ✔ |

Three specifics the §10.4 commentary asks to be checked, all confirmed:

- **`sid` is salted after the version and variant bits are set.** Octet 6 `0x65 → 0x45`, octet 8 `0x63 → 0xa3`, zero-indexed, and salting the pre-bit-set `expand` gives a different `PRK` — so the vector does catch that mistake.
- **`Z` is symmetric.** `X25519(sk_i, pk_a)` and `X25519(sk_a, pk_i)` agree, so a transposed argument order is caught by the vector rather than at the TLS handshake.
- **`sas_raw` info is `pk_i || pk_a`, initiator first.** Reversing it changes the digits, so that too is caught.

Two notes, neither a defect:

- The document does not say `sas_raw` is read big-endian anywhere except 11.7a, and 11.7a is the only place it needs to. Confirmed: `0x11e66a4c` big-endian is `300313164`; little-endian would be `1281316113 mod 10⁶ = 316113`, which is a plausible-looking six digits and would be caught only by the vector. Worth keeping RT-18's *"six displayed digits"* wording, which does catch it.
- **Modulo bias on the SAS is negligible and is recorded so it is not re-raised.** 2³² is not a multiple of 10⁶, so values below `967 296` are very slightly favoured — a relative bias under 2.3 × 10⁻⁷. It cannot be steered by an attacker who commits blind, and it does not change the 2⁻²⁰ bound in any way worth writing down.

---

### Ask 3 — Attack §11.8: is there any ordering in which an attacker learns a public key before committing to its own?

**Not in the five-frame exchange as specified. But there is one outside it, in a place the specification permits, and it is worth three orders of magnitude — see R-02.**

The exchange itself holds. An interposed attacker M runs two legs: acceptor toward the initiator I, initiator toward the acceptor A. Both orderings were worked through:

- **Leg 1 first.** M receives `ct_i` (a hash — no information), and 11.5c makes it send `pk_M1` before `pk_i` is revealed. Leg-1 digits are then fixed and known to M. M now opens leg 2 knowing `pk_i` — which does not help, because leg-2 digits depend on `pk_a`, which arrives only after M has committed to `pk_M2`. **M commits blind on leg 2.**
- **Leg 2 first.** M commits `ct_M2` blind, receives `pk_a`, and can compute leg-2 digits in full. It then needs leg-1 digits to collide with a value it knows — but leg-1 digits depend on `pk_i`, and when M must send `pk_M1` it holds only `H(pk_i)`. **M commits blind on leg 1.**

Either way exactly one leg is unsteerable at the moment of commitment, which is all the argument needs. The reordering 11.5c warns about — sending `pk_a` only after receiving `pk_i` — collapses the first case immediately, and 11.5c is right to say so in the clause rather than in an annex.

Reflection is closed too: 11.5f's two labels stop a MAC being bounced back, and the "receives its own MAC value → abort" rule closes the case where a relay has nothing else to send.

The parallel variant against the acceptor is closed by **11.3d**. The parallel variant against the *initiator* is not closed by anything, and that is **R-02**.

---

### Ask 4 — Rule on 11.7b: both peers must display six digits

**Holds for every peer PinPointStudio expects to ship. A14 need not reverse on our account.**

The host is a desktop application with a window; there is no headless or screenless configuration of PinPointStudio that would ever establish a pairing. The offscreen `--probe` build exists for testing and does not pair.

One qualification so the answer is not read wider than it is: this is the *host's* roadmap only. If PinPointCapture has a screenless capture peer in view, that is their answer to give, and the disposition is right that a PAKE decided before implementation is much cheaper than one decided after.

---

### Ask 5 — Confirm the UX obligations are buildable

**Both buildable, both testable, and neither conflicts with this application's house rules.**

- **11.7d** — digits grouped `313 164` identically at both ends, the affirmative control not pre-selected and not where a stray click lands, and the prompt asking whether the numbers *match*. Buildable. Pairing already lives in an in-app QML `Popup` over `Overlay.overlay` (`src/Gui/home/PpcpPairDialog.qml`) rather than a native dialog, which is this application's standing rule, and a modal that asks a comparison question is the same component shape.
- **11.9c** — a mismatch or MAC failure reported in terms that do not invite a retry. Buildable, and it is the more valuable of the two clauses. Recording the intent here: on a mismatch the host will state that the numbers did not match and that the pairing was refused, offer the pairing code as the next step per 11.9d, and **not** render a *try again* control at all — 11.9b forbids one that reopens the window without a further user action, and the safest way to comply is to not draw the button.

**Both are testable offscreen**, which matters because it means they can be regression-guarded rather than asserted once. `src/Gui/tests/qml/tst_ppcp_pair_dialog.qml` already drives the pairing popup with an injected fake controller and no display; asserting *"the affirm control is not the default focus"* and *"no retry affordance is present after a mismatch"* is the same harness. Recommend the specification's RT set gain a `review`-method row for 11.7d/11.9c, since the disposition is right that these are unusual clauses and unusual clauses are the ones that get quietly dropped.

---

### Ask 6 — PinPointStudio only: will you advertise `_ppcp._tcp` with `role: host` for reconnection?

**The honest answer today is: no, and by explicit design.** `src/Ppcp/ppcp_discovery.h` says so in its own header —

> *"THE BROWSER HALF, AND ONLY THE BROWSER HALF. … This host therefore registers NOTHING, answers NOTHING, and never binds UDP 5353."*

— and the whole file is guarded `#if defined(__APPLE__)`, so on the Windows host there is no discovery at all, in either direction.

3.5e is nevertheless **accepted as correct**, and the gap it names is real: with the capture peer bound by 3.5d and this host advertising nothing, §7.4's persisted pairing buys exactly nothing and the user sees an application that remembers the phone and still asks for a code. That is the current state of this deployment, and 3.5e is the first clause in the document that says whose problem it is.

Two facts that change the cost, and one that does not:

- **The stated objection does not block it on macOS.** The header's concern is binding UDP 5353 and conflicting with `mDNSResponder`. `DNSServiceRegister` does not bind 5353 either — it asks the same responder over the same IPC socket the existing `DNSServiceBrowse` path already uses (`ppcp_discovery.cpp:227`). Advertising on macOS is additive to a mechanism that is already there, not a new responder in this process.
- **On Windows it is a dependency question, not a code question.** There is no `dns_sd.h` on that platform and the whole browser is compiled out. 3.5e is unimplementable on the Windows host without taking on Bonjour or an equivalent responder, and that is a product decision rather than a protocol one.
- **The TXT record is not the obstacle.** It would carry `txtvers`, `pv`, `role: host`, `rn`, `rid` — all of which `libppcp` already computes (`ppcp_rv_rid`, `ppcp_rv_instance_name`), and 3.4d1's one-instance-with-rotation rule is a registration schedule.

**Recommendation, for Mark's decision rather than mine:** commit to 3.5e on macOS, where it is close to free and closes the persistence gap; record Windows as a separate item gated on the responder dependency; and note in the reply that until it ships, §7.4 persistence is dead weight on this deployment. Answering *"yes on macOS, deferred on Windows, and here is why"* is a better answer for the record than a bare yes.

---

### Ask 7 — Is RT-20 runnable between the two implementations?

**Yes, and this host is the natural place to put the relay.** It should be built, because the disposition is right that until it runs §11 is a design with vectors and not a demonstrated one.

The pieces already exist here: `Connector::connect()` and a listener in the same class (`src/Ppcp/ppcp_transport.h:359`, `:402`), the `libppcp` `PPCP-ENC` framing the bootstrap frames reuse under 11.4a, and a conformance tool in the sibling repo that already runs a two-process harness. A deliberate man-in-the-middle for RT-20 is a small program that accepts a bootstrap connection, dials the real acceptor, and runs its own key on each leg — the exchange is five frames and one channel byte.

Two things RT-20 must assert beyond "the digits differ", or it will pass while proving less than it claims:

1. That the peer whose user declines **does not pair** *and* that the window is closed and does not reopen without a further user action. The digits differing is only the signal; 3.7b is the property.
2. That the relay's *own* legs each complete successfully. A harness whose MITM simply fails is testing its own bug, not §11.

Suggest the harness live in `libppcp/tools` beside `ppcp-conform` so both teams run the same relay rather than two.

---

## 3. Findings

### R-01 — `v` is not echoed, not checked, and enters no derivation. **Blocking; free to fix now, impossible to fix later.**

11.4b puts the bootstrap format version `v` in `bs_offer` and `bs_accept`. 11.4e says a peer that decodes a `v` it does not implement aborts. **Nothing says the acceptor's `v` must equal the initiator's**, nothing says how a peer implementing v1 *and* v2 chooses between them, and `v` appears in no info string, no salt and no MAC.

Today that is latent, because `1` is the only value. The moment a `v: 2` exists — a longer SAS, a different curve, anything §11 might later want — an active attacker rewrites `bs_offer.v` to `1` on the way past. The v2 acceptor implements v1 and proceeds. The v2 initiator receives `bs_accept` with a `v` it *does* implement, so 11.4e does not fire, and 11.4c does not either because `v: 1` is a well-formed field of the right type. Both peers run v1, believing that is what the other could reach. **Neither MAC nor the SAS detects it**, because `K_c = HKDF-Expand(BK, "ppcp1 bs-confirm", 32)` is a function of `Z` alone.

11.6g argues correctly that a first-contact handshake with an agility mechanism is a first-contact handshake with a downgrade attack — and then 11.4b introduces a version field that is exactly such a mechanism, unprotected. 5.2b1 makes the same point on the TLS path and is enforced there.

**Two clauses, both one line:**

1. **An initiator MUST abort with `unsupported_version` if `bs_accept.v` differs from the `v` it sent.** (And, when later versions exist, a peer offers the highest it implements and never proposes a weaker one than its platform can reach — 5.2b1's shape.)
2. **Bind the transcript into the confirmation key**, so a rewrite that survives clause 1 still fails the MAC:

```
K_c = HKDF-Expand(BK, "ppcp1 bs-confirm" || v || pk_i || pk_a, 32)
```

where `v` is one octet. `sas_raw` already binds `pk_i || pk_a` for precisely this reason (11.6c's own note: *"Z alone would not say whose keys produced it"*); the same argument applies to the confirmation and was not carried across. Fixing it costs one info string, changes one row of §10.4, and there is no deployed implementation to break. After either team ships, it cannot be changed at all.

⚠ Clause 2 alone would be enough. Clause 1 alone would not — it protects the version and leaves the confirmation proving less than it should. Recommend both.

### R-02 — 11.3d serialises the acceptor. Nothing serialises the initiator, and CR-01's own workflow is the UI that breaks it. **Blocking.**

11.3d is exactly right and its rationale is exactly right: *"an acceptor that ran ten attempts in parallel would offer an attacker ten draws against one operator confirmation."* That reasoning applies unchanged to the initiator, and no clause states it there.

The concerning part is that the natural implementation invites it. CR-01's motivating scenario is *"a range operator sets up several bays"*; 3.3f adds `dl` specifically so a browsing peer *"that sees four open windows"* can tell them apart; and the obvious host UI is a list of discovered windows. An implementation that dials several to show the operator a list of candidate numbers has done nothing the specification forbids.

Cost: an attacker advertising N bootstrap windows gets N independent blind draws against a single honest confirmation, and — this is the part that hurts — **the operator does the selecting.** Shown a list of numbers, one of which matches the phone in their hand, an operator taps the match and reads it as success. The bound moves from 1 in 1 048 576 per confirmation to roughly N in 1 048 576, with a human actively finding the collision. At twenty bays that is a factor of twenty; at a thousand advertised windows it is a one-in-a-thousand attack, from a UI decision the document permits.

11.8 states the bound as *"one chance in 1 048 576"* and §11.7's *"six digits, and why not more"* rests the whole length argument on the attacker getting **one** draw. Both are true only for the acceptor as the document currently stands.

**Proposed clause**, mirroring 11.3d:

> **(11.3d1) MUST** An initiator runs at most one bootstrap attempt at a time, and MUST NOT display digits for more than one attempt. Where several bootstrap instances are discovered, the user selects one **before** the attempt begins ([3.3f](#33-txt-record)'s `dl` is what that selection is made on) and a second attempt requires the first to have ended.

Note that this makes `dl` load-bearing rather than merely convenient, which is worth saying out loud given 3.3g admits it as a privacy trade: the selection has to happen *before* the digits exist, so the operator needs something to select on. That strengthens the case 3.3g already makes.

### R-03 — 11.6b is written against a return value neither library produces. **Non-blocking, but it will be implemented wrongly.**

11.6b: *"A peer whose key agreement produces an all-zero `Z` aborts with `invalid_key`."*

Measured on OpenSSL 3.6.3, offering each of the five standard small-order u-coordinates (orders 1, 2, 4, 8 and the 2²⁵⁵−19−1 case) as the counterpart key:

```
point 0 (u=0)        derive FAILED (rejected by library)
point 1 (u=1)        derive FAILED (rejected by library)
point 2 (order 8)    derive FAILED (rejected by library)
point 3 (order 8)    derive FAILED (rejected by library)
point 4 (u=p-1)      derive FAILED (rejected by library)
```

**OpenSSL never returns an all-zero `Z`; it fails the call.** CryptoKit is documented to behave the same way. So an implementation that follows 11.6b literally writes a zero-check that can never fire, and the branch that *does* fire is the generic error path — which will be reported as a transport failure, not as `invalid_key`, and which on a careless implementation reads an uninitialised buffer.

The clause is right about what must happen; it names the wrong observable. Suggested wording:

> **(11.6b) MUST** A peer aborts with `invalid_key` and derives nothing where the key agreement **fails, or produces an all-zero output**. Most implementations of X25519 perform the [RFC 7748 §6.1](https://www.rfc-editor.org/rfc/rfc7748#section-6.1) check internally and report a failure rather than returning zeros; a peer MUST NOT treat that failure as a transport error.

**RT-21 needs the same treatment** — *"a shared secret of all zeros aborts with invalid_key"* is not observable on either platform. What is observable is that a small-order `pk` produces `bs_abort` / `invalid_key` and no derivation, which is what the row should assert.

### R-04 — 11.4f's rationale is the wrong way round. **Wording only.**

11.4f: *"A failed confirmation MAC after matching digits means an attacker forged one or an implementation is wrong."*

An interposed attacker holds `Z` on **both** legs, therefore `K_c` on both, and forges both MACs trivially and correctly. A MAC *failure* is evidence that no such attacker is present and something else is wrong — overwhelmingly an implementation disagreement, which is exactly the `PRK`-divergence class §10.4's own commentary warns about, one step earlier.

The clause's **action** is right and should stand: reporting a refusal and a MAC failure identically is correct, and for the reason given. It is the sentence explaining why that will mislead an implementer into treating the MAC as an authentication check. It is not — the comparison is the authentication, and the MAC is an agreement-and-liveness proof that both ends reached the same `Z` and that both users actually acted.

Knock-on: **11.9c** groups a MAC failure with a mismatch as *"either an implementation is wrong or someone is on the link"*. For a mismatch that is right. For a MAC failure the first cause is far more likely than the second, and the *"do not retry until you know why"* message is still the right one — so no clause needs to change, only the reasoning under it.

### R-05 — May a peer claim §11 while implementing one role only? **Needs one sentence.**

9e requires an implementation claiming §11 to implement it *"in full — both the commitment and the two-sided confirmation"*, and the reasoning is entirely about not skipping the security-critical halves of the exchange. It does not say whether **both roles** are required.

On CR-01's own deployment the answer matters immediately: this host will ship as **initiator only**. It browses, it dials, it never opens a bootstrap window — 11.2b puts it on that side, and there is no product reason for a desktop host to accept a guided pairing from a stranger. An initiator-only peer cannot pair with another initiator-only peer, so the conformance claim is materially narrower than "implements §11", and two implementations could each claim §11 and be unable to pair.

9d already requires an implementation to state which optional sections it provides. Suggest the same sentence extends to the role: *an implementation claiming §11 states which of the initiator and acceptor roles it provides, and 9e's "in full" binds each role it claims rather than requiring both.*

### R-06 — The venue that motivated the request is the venue where the delivery mechanism does not work. **Product observation; no clause change sought.**

Stated because it should be decided rather than discovered. CR-01's requirement is a driving range with several bays. 3.6a is unambiguous that multicast *"will not work at a range"*, and the disposition's own Q1 acknowledges it — *"guided pairing is normally reached over multicast, at exactly the kind of venue where multicast does not work"* — which is the stated reason the code stays REQUIRED under 9f.

That is honest and PinPointStudio agrees with it. The consequence for this host is that **3.7h is the clause that matters commercially**, not §3: reaching a bootstrap window at an endpoint entered or configured out of band. This application currently has no way to do that — there is no UI for entering an endpoint, deliberately, because PPCP sessions come from a connected device's offer list rather than from typed input, and there are no native dialogs.

No change to `PPCP-RV` is sought. The point for the record is that implementing §11 against mDNS alone would deliver the feature everywhere except the venue it was asked for, and the host-side work item is therefore *how a window is reached without multicast*, not *how a window is dialled*.

---

## 4. Agreed without reservation

Listed so the reply is not read as reservations only.

- **The fourth-path decision (Q2).** Producing a `PRK` by other means and handing to §5 unchanged, rather than reaching inside §4's key schedule, is right, and A17's refusal to upgrade the bootstrap connection in place is right for the reason given: one shape of §5 is worth more than one TCP setup.
- **11.2c, plaintext bootstrap.** The claim is checkable and it checks out — two ephemeral public keys, a hash of one, two MACs, and every one of them public in the construction's own argument. Naming a value whose disclosure weakens the pairing is the right test to offer a reviewer, and there isn't one.
- **11.1d.** The clause forbidding an automatic comparison is the one most likely to be optimised away and the one whose removal is invisible on the wire. Stating that it would pass every static test in the document is the right way to write it.
- **3.7a/3.7b/11.3d as a set.** The observation that removing any one of the three converts a one-shot attack into a loop is correct and is the load-bearing part of §11.8 — see R-02 for the fourth member the set is missing.
- **11.10b.** Forbidding any value from an existing pairing from crossing or influencing a bootstrap connection, so guided pairing cannot become an oracle for §3.4's identifiers, is a good catch that nothing in CR-01 asked for.
- **B15, the fleet case.** Declining to specify a venue credential before B2's per-peer re-keying exists is the right call, and the reasoning — that it would ship 7.4f's exposure permanently rather than for one session — is the right reasoning.
- **The `PPCP-ENC` follow-up.** Agreed: channel `255` being reserved by `ENC` 2a and claimed by 11.4a with no cross-reference is the *"one idea spelled in two documents"* shape E25 had to clean up after. An `ENC` erratum adding the cross-reference should be raised now rather than left.

---

## 5. Summary of what is asked of the protocol team

| | Finding | Ask | Urgency |
|---|---|---|---|
| **R-01** | `v` unechoed, unchecked, unbound | Two clauses: echo-or-abort, and `K_c` info gains `v \|\| pk_i \|\| pk_a` | **Before either team implements.** Free now, impossible after |
| **R-02** | Only the acceptor is serialised | A clause 11.3d1 mirroring 11.3d on the initiator | **Before either team implements.** Worth 3 orders of magnitude |
| **R-03** | 11.6b names an unobservable return value | Reword 11.6b and RT-21 to *"fails, or produces an all-zero output"* | Before implementation; measured, not hypothetical |
| **R-04** | 11.4f's rationale inverted | Wording under the clause; the clause itself stands | Next revision |
| **R-05** | Role-partial conformance undefined | One sentence extending 9d's statement obligation to the role | Next revision |
| **A16** | *"the host's crypto library"* — `libppcp` has none | Specify the X25519 seam, following `ppcp_rv_random_fn` | A `libppcp` API question, but A16 is where the assumption lives |

And from this side, for the record: **B14 is discharged for the host** (OpenSSL 3.6.3, measured), **RT-18 passes** (all fifteen rows, independent implementation), **11.7b holds** for every peer PinPointStudio will ship, **11.7d and 11.9c are buildable and offscreen-testable**, **RT-20 is runnable** and this host will host the relay, and **3.5e is accepted** with advertising to be committed on macOS and deferred on Windows pending a responder dependency.
