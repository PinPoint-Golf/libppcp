# Design review — PPCP-RV Draft 2

**Reviewed as owner of PinPointStudio, the host implementation. Second pass.**

| | |
|---|---|
| Document reviewed | `libppcp/docs/specification/ppcp-rv.md` — `PPCP-RV` 1.0 Draft 2, payload version `ppcp1`, plus `rv-review-disposition-2026-08-22.md` |
| Reviewer | PinPointStudio maintainer |
| Prior pass | V1–V4 and four smaller points against Draft 1 |
| Method | Read in full; independently recomputed **all eleven** test vectors, including the new all-fields payload and the new PSK identity, and re-derived the deterministic key ordering against the amended key set |
| Date | 22 August 2026 |
| Verdict | **Three findings, two of which are contradictions the fixes introduced.** All four Draft 1 findings are properly closed. W1 and W2 should land before implementation; W3 is clerical but wants doing before clause numbers get quoted. |

---

## 0. Position

Every Draft 1 finding is closed, and two of them better than I asked.

**4.3b is a better fix than the one I would have accepted.** I proposed the two-character rule and would have settled for a special case making `v` first regardless of ordering; A12's reasoning for preferring the length constraint — *a special case is a rule an implementer can forget; a length constraint is one the encoder enforces for free* — is right, and it keeps working for keys added in later payload versions rather than only for `dn`.

**7.3e is better than what I proposed.** I suggested letting a peer with an untrustworthy clock proceed. Draft 2 does that *and* puts enforcement on the publisher, which holds the authoritative clock and was previously not required to check `exp` at all. That is the half I missed.

**I accept RV-D1.** I suggested keeping `0x01 || sid` for a first pairing and using the resolvable form only for persisted ones. Two equal-length forms starting with the same byte would need a discriminator, and the saving is one HMAC on a handshake already doing elliptic-curve arithmetic. One form is right.

**And 5.2h is the most valuable thing in Draft 2**, more than any of my findings. Stating the two *properties* the TLS profile exists to deliver, and naming the mechanism as a mechanism, is what makes B8 resolvable by evaluation rather than by negotiation under schedule pressure. That framing should survive whatever the platform check returns.

**I verified the arithmetic rather than reading it.** All eleven values reproduce byte-for-byte:
`PRK`, `K_tls`, `K_id`, `rid`, the PSK identity tag and the full 17-octet identity, the canonical
UUID text of `sid`, the `exp` encoding, the 75-octet minimal payload and its 105-character URI, and
the new **133-octet all-fields payload and its 183-character URI** — whose first four octets are
`a8 61 76 01`, which is precisely the demonstration V1 needed and the minimal vector could not give.

The three findings below are all in text Draft 2 added or amended. Two of them are the Draft 1
fixes not carried through to the clause next door.

---

## 1. Findings

### W1 — 7.4e contradicts 5.3e, and points at the wrong clause {#w1}

**Severity: highest. Two MUSTs in direct conflict, in the security section, and it is the V2 fix not
being carried into the clause that motivated it.**

§5.3e, new in Draft 2:

> **(5.3e) MUST NOT** `sid`, `Peer.id`, or any other value stable across connections appear in the
> identity.

§7.4e, unchanged from Draft 1:

> **(7.4e) MUST** A new session established from a persisted pairing derives a fresh `sid` inside
> the authenticated channel, and **does not reuse the original session's identifier for anything but
> the PSK identity** ([§5.3c](#53-psk-identity)).

7.4e states that the original `sid` *is* used in the PSK identity. 5.3e forbids exactly that. An
implementer reading §7 — which is where they will be when they build persistence — is told the
opposite of what §5.3 requires.

The cross-reference is stale too. In Draft 1, §5.3c was *"A client offering a persisted pairing uses
the `sid` of the session that pairing was established for"* — the clause 7.4e was pointing at. In
Draft 2, §5.3c is *"A server that resolves no pairing aborts the handshake, with the same alert…"*.
So the pointer now lands on an unrelated rule about uniform failure, which reads plausibly enough
that it may not be noticed.

#### Requested change

Delete the tail clause rather than repair it, because the truth is now stronger than the exception:

> **(7.4e) MUST** A new session established from a persisted pairing derives a fresh `sid` inside
> the authenticated channel. **The original session's identifier is not reused for anything.** After
> the initial derivation of [§5.1](#51-key-derivation) `sid` survives only as the HKDF salt baked
> into `PRK`; it is never transmitted again, by either peer, on any connection.

That last sentence is worth having explicitly. It is the property that makes B6 genuinely closed
rather than closed-by-renaming, and it is the one-line answer to *"where does `sid` go once the
pairing persists?"* — which is the question 7.4e was trying to answer and got wrong.

---

### W2 — 4.3b's scope is unstated, and the document's own vector violates it under the natural reading {#w2}

**Severity: high. A MUST that rejects a payload another MUST requires to be accepted.**

> **(4.3b) MUST** **Every payload key** other than `v` is at least two characters.

The all-fields vector in §10.3 — the one RT-2 requires to *"encode and decode byte-for-byte"* —
contains five one-character keys:

```
"ep" → [ { "h": "192.168.1.20", "p": 7788 } ]
"wifi" →  { "h": false, "k": "correcthorse", "s": "PinPoint-Bay3" }
```

`h`, `p`, `h`, `k`, `s`. All are keys inside the payload, so a validator written literally from
4.3b rejects the specification's own worked example, which RT-2 requires it to reproduce. I
confirmed the vector is otherwise exact — 133 octets, 183 URI characters, deterministic ordering
`v, dn, ep, mu, exp, psk, sid, wifi` — so the encoding is right and only the rule's scope is wrong.

The rule also does not *need* to reach the nested maps. It exists to guarantee 4.2a, and 4.2a is a
statement about the first key of the **top-level** map. Nested maps have no `v` and no ordering
obligation beyond deterministic encoding.

#### Requested change

One word:

> **(4.3b) MUST** Every **top-level** payload key other than `v` is at least two characters. …
> **The rule is confined to the top-level map, which is the only one whose first key is
> constrained; the nested `ep` entries and the `wifi` map use one-character keys and are
> unaffected.**

That sentence is worth adding rather than relying on the single word, because the nested keys are
right there in the vector and the next reader will ask.

---

### W3 — two different clauses are both numbered `4.3b` {#w3}

**Severity: medium. Clerical, but the conformance suite cites clause numbers.**

§4.3 now contains, in this order:

- **(4.3b) MUST** Every payload key other than `v` is at least two characters. *(new in Draft 2)*
- **(4.3b) MUST** `psk` is at least 16 bytes from a cryptographically secure random number
  generator. *(carried from Draft 1)*

The new rule was inserted with the label the entropy rule already held. Both are cited: §10.3 says
*"the vector that matters for [4.3b]"* and A12 says *"Every payload key but `v` is at least two
characters ([4.3b])"*, both meaning the first; §7.2a restates the entropy requirement that is the
second. Renumber the entropy rule — 4.3g is free — or renumber the key-length rule and update the
three citations. Whichever, it should happen before the numbers are quoted in an implementation
note or a test name, which is where they are about to go.

---

## 2. Consistency items

| | Item | Where |
|---|---|---|
| 1 | **2d reinstates the role language that 2e was added to remove**, one clause below it. The table now correctly says *the peer that displays* / *the peer that scans*, and 2e states that nothing requires a host at either end — then 2d says *"On the code path the **device** dials, so the device is the initiator and the **host** states its support window."* For two capture peers pairing directly, that sentence has no referent. Read it as *"the scanner dials and is therefore the initiator; in the ordinary deployment that is the capture peer, and the host states its support window."* **A9 has the same problem** and additionally says discovery *"must put the querier role on the host"*, which §3.5b downgraded to a SHOULD. | §2d, A9 |
| 2 | **B8's fallback needs one more constraint.** If TLS 1.2 with an ECDHE_PSK suite is taken, `psk_identity_hint` — the server-sent field in `ServerKeyExchange` — is exactly the hint-based mechanism the mobile team found in the platform headers, and it is sent in the clear. 5.3e's prohibition must apply to it: **the hint MUST be empty**. Worth stating in B8 now, while the fallback is hypothetical, rather than when someone is implementing it under time pressure. Relatedly, **5.2h enumerates two properties** — mutual authentication and forward secrecy — and Draft 2 added a third to the profile in 5.3e: nothing stable crosses in the clear. A relaxation should be evaluated against all three. | B8, §5.2h |
| 3 | **RT-6 does not carry 4.4a's new precondition.** It asserts *"An expired code is reported as expired, with no connection attempted (4.4a)"*, but 4.4a is now conditioned on *"a peer whose wall clock it has reason to trust"*, and RT-15 covers the other branch. RT-6 should name the precondition, or an implementation correctly applying 4.4a1 fails it. | RT-6 |
| 4 | **Two stale references in §7.1.** The denial-of-service row's link text says §3.5 (its anchor correctly points at §3.6, which multicast moved to); and the *"Tracking a device across venues"* row cites only §3.4, when §5.3 is now the other half of that same defence — which is the whole of V2. | §7.1 |

---

## 3. What I checked and confirmed

- **All eleven vectors.** Recomputed HKDF-Extract and HKDF-Expand from RFC 5869 directly rather
  than through a library, the HMAC tags likewise, and both CBOR payloads by hand from the annotated
  octets. Every value matches, including the two URI encodings and both octet counts.
- **The deterministic ordering under the amended key set** is `v, dn, ep, mu, exp, psk, sid, wifi`,
  which is what §10.3 shows. With `n` it would have been `n, v, …` — the vector's own note is
  correct.
- **4.3e closes V4 cleanly.** Canonical lowercase UUID text, stated as a MUST NOT for any other
  encoding, with the text form now in the §10.1 vector so RT-2 covers it.
- **7.4f and the §7.1 impersonation row close V3.** Bounding the shared credential to one session
  rather than removing `mu` is the right call, and RT-16 being `review` method is honest — nothing
  on the wire distinguishes a persisted group key from a persisted pairwise one.
- **§3.5's mechanism/recommendation split** resolves the host-advertises question without weakening
  the querier-role argument. 3.5c naming the reverse deployment as conformant is what makes the
  mobile team's reconnection screen a product decision rather than a conflict.
- **§8's PSK-interface paragraph** covers both ends' version of the same trap, including the
  detail that matters most in practice — a wrong hash fails indistinguishably from a key mismatch.
  That paragraph will save someone a day.
- **B8 is handled correctly.** Recording an empirical risk, scheduling the check, and refusing to
  relax a security requirement pre-emptively is the right order. The asymmetry note — that the host
  library has supported external PSK for years, so every test in §9 and any host-to-host pairing
  passes with the risk entirely invisible — is the part that makes it dangerous and it is stated.

---

## 4. Sign-off

**Approve, subject to W1–W3.** None changes a byte on the wire; W1 and W2 change what an
implementer is told to build, and W3 changes what a test can cite.

| | Fix | Cost |
|---|---|---|
| **W1** | Delete 7.4e's tail clause and state that `sid` is never transmitted after derivation | one clause |
| **W2** | Scope 4.3b to the top-level map, and say the nested keys are unaffected | one word plus a sentence |
| **W3** | Renumber one of the two `4.3b` clauses and fix the three citations | clerical |

Plus the four consistency items, one sentence each.

`PPCP-RV` remains gated on **B8**, which is the mobile team's check and not mine. Nothing in this
review depends on its outcome; if the fallback is taken, item 2 above is the clause that needs to
be written at the same time.

Once W1–W3 land I have no further findings on `PPCP-RV`. Combined with the three outstanding on
`PPCP-CORE` Draft 3, that is the whole of PinPointStudio's position on the specification set.

---

## 5. Closing

Draft 1's findings were in the model and the arithmetic. Draft 2's are in the **joins** — a clause
that was correct before the fix next door landed, a rule whose scope nobody stated because the
author knew what they meant, a label reused because the paragraph above it moved.

That is a good place for a document to be, and it is also the point at which reviewing gets less
useful than implementing. W1 is the only one I would insist on, because a security document that
tells you to transmit an identifier two sections after forbidding it will be resolved by whichever
section the implementer read second.

One thing to carry forward, since it has now happened three times across both documents: **the
worked example is where defects hide, and a vector that exercises only the common case validates
only the common case.** V1 was invisible because `n` was omitted; W2 is visible *only* because the
new vector includes the nested maps that the minimal one did not. The all-fields vector earned its
place the first time it was run.
