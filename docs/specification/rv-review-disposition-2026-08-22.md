# PPCP-RV review disposition

**How each first-pass review finding was handled in Draft 2.**

| | |
|---|---|
| Status | Record of decisions. Non-normative. |
| Date | 22 August 2026 |
| Rounds covered | **First pass** on Draft 1 (V1–V4), which produced Draft 2. **Second pass** on Draft 2 (W1–W3, four consistency items, and the executed platform check), which produced Drafts 3 and 4. **Third pass** on Draft 4 (R1–R6 and two from the mobile side), which produced Draft 5. All six reviews are in [`reviews/`](reviews/). |
| Separate from | [`review-disposition-2026-08-22.md`](review-disposition-2026-08-22.md), which covers PPCP itself. `PPCP-RV` has its own review cycle and is not covered by the PPCP approval. |

Both teams read the document hardest where it asked to be read hardest, and one of them found something there. Every finding is dispositioned; the ones **not** actioned are listed with reasons.

---

## 1. The finding that justified the exercise

### 1.1 V1 — `v` was not the first key whenever a display name was present

**Accepted in full. This is a defect in [§4](ppcp-rv.md#4-rv-2--the-pairing-code), the one part of the document that cannot be corrected after a code is printed.**

Three clauses depended on each other and the third was false:

- 4.2a: *the payload's first key is `v`*
- 4.3a: deterministic encoding *"places `v` first without a special rule, since `v` sorts before every other key defined here"*
- RT-2 asserted the property

RFC 8949 §4.2.1 orders map keys by the bytewise lexicographic order of their **encoded** forms. `"v"` encodes as `61 76`; the optional display name `"n"` encoded as `61 6e`; `0x6e < 0x76`. **Any code carrying a display name put `n` first.** Recomputed against the full key set, the order was `n, v, ep, mu, exp, psk, sid, wifi`.

The §10.3 worked example omitted the field, so the arithmetic looked right — the reviewer confirmed all 75 octets and all 105 URI characters reproduce exactly. The defect was invisible in the only example given.

**Why it mattered more than a one-key mistake.** 4.2a is the clause the version story rests on: a peer that has not implemented a later `v` decodes far enough to find it and tells the user the code is newer than the application ([4.2b](ppcp-rv.md#42-version-handling)) — a user-experience contract, not a parsing one. A parser written to read the first key and stop, which 4.2a invites, would have read a display name.

| Change | Where |
|---|---|
| **[4.3b](ppcp-rv.md#43-payload)**: every payload key other than `v` is at least two characters, so a one-character key (`0x61 XX`) always sorts before a two-character one (`0x62 XX YY`) and 4.2a is true **by construction** — including for keys added in later payload versions | `RV` §4.3 |
| `n` renamed **`dn`**, with the 64-byte limit [§2.4](#24-smaller-points) asked for | `RV` §4.3, 4.4d |
| A second worked vector carrying **every** optional field — `dn`, `mu`, `exp`, `wifi` — 133 octets, 183 URI characters, first four octets `a8 61 76 01` | `RV` §10.3 |
| RT-2 extended to assert `v` is first in the all-fields payload | `RV` §9 |

The reviewer's closing observation is the durable lesson and is worth repeating: *"A vector that exercises only the common case validates only the common case."* The same finding was made against `CT-S1` in `PPCP-CONF` two rounds earlier, for the same reason.

---

## 2. PinPointStudio — host review

### 2.1 V2 — the PSK identity was a stable cleartext pairing identifier

**Accepted in full.** The document stated the principle and then did the thing it forbids.

§5.3 said the identity is deliberately not a peer identifier *"because it is visible in the clear in the `ClientHello`, so putting a stable peer identity there would undo §3.4 at the first connection"* — and then 5.3c had a persisted pairing reuse its original `sid` on every reconnection for the life of the pairing. A passive observer at two venues links them by a fixed sixteen bytes. The rotating advertisement of §3.4 was being paid for and then given away one layer down, one connection earlier.

The distinction between a stable *peer* identity and a stable *pairing* identity does no work: a pairing with one's own studio host persists for months, and it additionally reveals which host.

The identity is now resolvable, with the construction already in the document:

```
identity = 0x01 || rn2 || HMAC-SHA256(K_id, "ppcp1 psk-id" || rn2)[0..7]
```

Same 17 octets, keyed by the same `K_id`, fresh `rn2` per connection. A server resolves by recomputing the tag against each pairing it holds — the cost A10 already accepted as cheap. **This also closes B6**, which had recorded the `sid`-bound identity as "slightly odd"; it was not an aesthetic issue.

**One departure.** The reviewer suggested keeping `0x01 || sid` for a *first* pairing and using the resolvable form only for persisted ones. Draft 2 uses **one form always** ([A11](ppcp-rv.md#annex-a--decisions-and-alternatives)): two forms of equal length starting with the same byte would need a discriminator, and the saving is one HMAC on a handshake that already does elliptic-curve arithmetic. The leading `0x01` stays a format version byte so a future third form remains possible.

The change also restores 5.3d's justification, which V2 had weakened: with a resolvable identity an attacker cannot produce one to probe the timing oracle with, because that needs `K_id`.

### 2.2 V3 — `mu > 1` is a group credential

**Accepted in full, including the reviewer's judgement not to remove `mu`.**

Every peer scanning one code derives the same `PRK`, hence the same `K_tls` and `K_id`. With `mu: 3` the three devices hold **identical key material**: any one can complete a handshake indistinguishable from another's, and can declare a different `Peer.id` in `hello` while doing it. 7.4c's scoping to a counterpart peer identity is a *policy* statement with nothing enforcing it — and 7.4a then permitted persisting that shared key indefinitely.

§7.1 claimed the model defends against an unpaired peer receiving capture payload, on the strength of a secret that only reaches the counterpart by being scanned. With `mu > 1` it reached three, so *paired* named a group.

| Change | Where |
|---|---|
| **7.4f**: no `PRK` from a code with `mu > 1` is persisted — such a pairing is **session-scoped** | `RV` §7.4 |
| **7.4g**: per-peer re-keying inside the channel is the fix for persistent multi-device pairing, and is deferred | `RV` §7.4 |
| An impersonation row added to the *not defended against* table | `RV` §7.1 |
| **RT-16** (review method) asserts no such `PRK` is persisted | `RV` §9 |
| B2 rewritten: it had noticed the missing revocation story but not the shared keys, which is the part that matters | `RV` Annex B |

`mu` survives because displaying three codes is worse ergonomics for no gain over proper re-keying. Both publishers state they will emit `mu: 1` only until 7.4g exists.

### 2.3 V4 — `sid` bytes versus `Session.id` text

**Accepted, wording adopted.** `sid` is sixteen raw bytes in the pairing code; `Id` in `PPCP-CORE` is an opaque UTF-8 **string**. Hexadecimal, canonical UUID text and base64url are all plausible and all wrong if the other end chose differently — and `PPCP-CORE` §8.5c keys idempotent re-import on `Session.id`. Two implementations choosing different forms would duplicate every Capture in a re-imported session: the exact failure that rule exists to prevent, arriving through the rendezvous layer where nobody would look.

[4.3e](ppcp-rv.md#43-payload) fixes the canonical lowercase UUID text form, and §10.1's vector now carries it.

### 2.4 Smaller points

| | Item | Disposition |
|---|---|---|
| 1 | §2's table bound the paths to roles, foreclosing device-to-device pairing — UC-6 stereo, offline multi-device, neither of which has a host | **Accepted.** The table is now written as *the peer that displays the code* / *the peer that scans it* and *advertises* / *browses*, with **2e** stating that nothing requires a host at either end. |
| 2 | B4's expiry problem had a remedy available and did not take it | **Accepted, and taken further.** [7.3e](ppcp-rv.md#73-single-use-and-expiry) makes the **publisher** enforce `exp` — it holds the authoritative clock — which was missing entirely; [4.4a1](ppcp-rv.md#44-handling-a-scanned-code) then lets a peer that cannot trust its clock attempt rather than be locked out at a range with no network to correct it. **B4 closes.** |
| 3 | `dn` had no length limit, and it is the one attacker-controlled field shown before anything is authenticated | **Accepted.** 64 bytes. |
| 4 | Keep RT-12's sentence about the requirement no test can catch | **Kept**, and it is now also in the PPCP disposition's record so it survives editing. |

### 2.5 Host-side notes

No specification change; recorded because the reviewer asked for the cost to be visible.

PinPointStudio has **no networking at all today** — no sockets, no TLS, no QR generation — and under `PPCP-RV` the host is the code publisher, the listener and the TLS server. That is a from-zero subsystem.

One note **was** actioned: the reviewer warned that the desktop toolkit's PSK interface is a TLS 1.2-era API that does not reach TLS 1.3 external PSKs, and that installing the key with the wrong hash fails with no useful diagnostic. That is the same trap the mobile team hit from the other side, so [§8](ppcp-rv.md#8-operational-notes) now carries one paragraph covering both.

---

## 3. PinPointCapture — mobile review

### 3.1 The platform risk on §5.2a

**Not resolved, and deliberately not papered over.** This is the one finding Draft 2 does not close, because it is empirical.

§5.2a requires TLS 1.3 with an external PSK and §5.2f forbids any fallback. The reviewer read the platform SDK headers and found the PSK entry point sitting beside identity-hint and RFC 4279 APIs — both TLS 1.2-only concepts — with no stated version constraint either way. That is not proof, and they were careful to say so. It is enough not to agree §5.2a on the assumption it works.

**The asymmetry is what makes it dangerous**: the host uses a library that has supported external PSK for years, so a host-to-host pairing and every test in §9 pass with the risk entirely invisible. It surfaces only on the mobile platform.

Draft 2 does three things and resolves none of them prematurely:

- **[Annex B8](ppcp-rv.md#annex-b--open-issues)** records the risk, the one-day check that settles it, and both fallback options with their costs. The mobile team volunteered the check.
- **[5.2h](ppcp-rv.md#52-tls-profile)** states the **properties** the profile exists to deliver — mutual authentication from a scanned secret, and forward secrecy against later disclosure — and says TLS 1.3 with `psk_dhe_ke` is the *mechanism*. A relaxation preserving both is a different mechanism; one dropping forward secrecy is not available. This is the important half: it means the fallback, if needed, is evaluated against a stated property rather than negotiated under schedule pressure.
- The document's status says §5.2 is provisional pending that check.

TLS 1.2 with an ECDHE_PSK suite would preserve both properties, which is why A6's reasoning survives the version number changing. That is the fallback to reach for first if the check goes the wrong way.

### 3.2 `psk_dhe_ke` is mandatory and unobservable through the platform API

**Accepted.** The requirement is right and stays; what was wrong was the implied way of demonstrating it. A peer that cannot select or read back the key-exchange mode cannot assert 5.2b by construction.

[5.2i](ppcp-rv.md#52-tls-profile) states that such a peer demonstrates conformance by **observed handshake** — a capture of the `ClientHello`'s `psk_key_exchange_modes` extension, or an instrumented counterpart that refuses `psk_ke` — and RT-4's method is `injected` rather than `static` for exactly that reason.

### 3.3 §2 and §3.3 disagreed on whether a host may advertise

**Accepted**, and it resolves together with the host's smaller point 1.

§2 said the device advertises and the host dials; §3.3's `role` admitted `host`, a value that could then never legitimately appear. The mobile team's reconnection screen assumes the opposite — a discovered host the user taps to connect — and they correctly noted that three different products were compatible with the text.

Draft 2 separates the **mechanism** from the **recommendation**: [3.5a](ppcp-rv.md#35-who-advertises-and-who-browses) makes advertising and browsing available to any peer, so `role` carries any of its values legitimately; [3.5b](ppcp-rv.md#35-who-advertises-and-who-browses) keeps the SHOULD that a capture peer advertises and a host browses, with the querier-role reasoning intact; [3.5c](ppcp-rv.md#35-who-advertises-and-who-browses) states that the reverse is conformant and is the shape a "reconnect to a discovered host" interaction needs, at the cost of the host supplying its own responder.

That makes the mobile team's screen a legitimate product decision rather than a conflict with the specification, which is what they asked for.

### 3.4 Smaller points

| | Item | Disposition |
|---|---|---|
| §4 | 6b's *"restore the prior network configuration"* is not implementable where reassociation is the system's decision | **Accepted.** 6b now says so, and that only the second branch is available on such platforms — which is conformant, and the disjunction exists for that reason. |
| §5 | Ordering of network join versus endpoint walk was unstated | **Accepted.** [4.3f](ppcp-rv.md#43-payload): join first unless already associated, then walk `ep`; on total failure, MAY join and walk again. |
| §5 | With `wifi` present, is the reachable endpoint reachable before the join? | Answered by the same clause. |

### 3.5 Endorsements recorded

Both teams explicitly endorsed **A1** (ratify `_ppcp._tcp`), **A2** (version inside the payload, not the scheme), **A3** (custom scheme rather than an `https` link), **3.2b** (no user-assigned device name in the instance name) and **7.2b** (naming diagnostic export explicitly). The mobile team read §4 hardest per the document's request and had no objection to it beyond V1, which the host found.

The host reviewer independently recomputed every §10 vector, including HKDF from RFC 5869 directly rather than through a library, and confirmed all seven reproduce byte-for-byte.

---

## 4. Decisions a reviewer may wish to reverse

| # | Decision | Alternative | Where |
|---|---|---|---|
| **RV-D1** | **One PSK identity form, always resolvable** | Keep `0x01 \| sid` for a first pairing; the reviewer's suggestion. Rejected because two equal-length forms starting with the same byte need a discriminator, for a saving of one HMAC | [A11](ppcp-rv.md#annex-a--decisions-and-alternatives) |
| **RV-D2** | **A length rule rather than a special case for `v`** | State that `v` is emitted first regardless of ordering. Rejected because a special case is a rule an implementer forgets, while a length constraint the encoder enforces for free keeps working for future keys | [A12](ppcp-rv.md#annex-a--decisions-and-alternatives) |
| **RV-D3** | **`mu` retained, bounded to session scope** | Remove `mu` entirely so every pairing is pairwise | [A13](ppcp-rv.md#annex-a--decisions-and-alternatives) |
| **RV-D4** | **§5.2a left as written, marked provisional** | Relax it pre-emptively to admit TLS 1.2 with ECDHE_PSK. Rejected: the check is a day's work, and relaxing a security requirement against a risk that may not exist is the wrong order | [B8](ppcp-rv.md#annex-b--open-issues) |

---

## 5. Status

`PPCP-RV` is **Draft 2, unagreed**. Both first-pass reviews are carried; neither team has re-reviewed. It is not covered by the PPCP approval and has its own cycle.

One item gates agreement: **[B8](ppcp-rv.md#annex-b--open-issues), whether TLS 1.3 external PSK is reachable on the mobile platform.** Nothing else in the document depends on the answer, and both teams agreed the check comes before the clause.


---

# Second pass — Draft 2

Both teams recomputed every vector independently this time, and both found the same scope defect. One of them ran the platform check they had volunteered for, and it failed.

## 6. The result that changes the shape of the document

**B8 is resolved, and the answer is no.** The mobile team executed the check Draft 2 scheduled:

| Attempt | Result |
|---|---|
| Minimum TLS 1.3 — what 5.2a requires | **Handshake failed** |
| Minimum TLS 1.2 | `TLS_PSK_WITH_AES_128_GCM_SHA256` — plain PSK, **no forward secrecy** |
| ECDHE_PSK requested, two different suites | Request **silently ignored** |

The cause is structural: the platform's ciphersuite enumeration contains **no PSK suites at all**, so the suite it negotiates cannot be named — neither requested nor excluded.

**The fallback the disposition was counting on is also gone.** RV-D4 kept §5.2a on the basis that TLS 1.2 with an ECDHE_PSK suite would preserve forward secrecy. It would have, and it is unreachable, so RV-D4 is **closed as overtaken rather than exercised**.

### 6.1 What Draft 3 does about it

It does not weaken the requirement, and it does not choose the mechanism.

- **[§5.2a](ppcp-rv.md#52-tls-profile) is marked BLOCKED** and retained as written. Weakening a security clause to match a platform limitation, before anyone has decided what to do, is the failure mode this whole document set exists to avoid.
- **[§5.4](ppcp-rv.md#54-resolved-the-mechanism)** records the measurement, states what follows, and lays out four routes with their costs — embed a TLS library; a Noise handshake over the raw socket; an application-layer ephemeral key over plain PSK; or accept no forward secrecy. The last is named **so that it is visibly excluded rather than silently reached for under schedule pressure.**
- **[5.2h](ppcp-rv.md#52-tls-profile) gained a third property** on the host reviewer's point, and it is what makes the decision tractable: the choice is between mechanisms measured against stated properties, not a negotiation about a version number. The host reviewer called 5.2h *"the most valuable thing in Draft 2, more than any of my findings"*, and this is why.
- **[5.4d](ppcp-rv.md#542-what-the-routes-were) bounds the blast radius**: only §5 changes under any route. Discovery, the pairing code — including the irreversible part — network join and the security model are all independent of it, and the resolvable identity of §5.3 survives a mechanism change as a pre-handshake selector.

**A recommendation is recorded, not a decision**: Route B, a Noise handshake over the raw socket, subject to two confirmations worth an afternoon between them — the check re-run on the device rather than the desktop variant, and the export-compliance position for an application using only platform-supplied primitives. Route A, embedding a TLS library, is the conservative answer and the document says plainly that nobody should be argued out of it cheaply.

The mobile reviewer's framing is the right one and is worth preserving: *"§1 is not a defect in the specification — the specification asked the right question and stated the property that decides it. The platform simply answered badly, and it is better to know now than after either team has written rendezvous code."*

## 7. Findings both teams made

### 7.1 The scope of 4.3b — W2, and the mobile team's §2

**Accepted, and both proposed the same one-word fix.** The V1 rule read *"every payload key other than `v` is at least two characters"*, unqualified. The `ep` entries use `h` and `p`; the `wifi` map uses `h`, `k` and `s`. All five are payload keys, so **a validator written literally from 4.3b rejects the specification's own normative vectors**, which RT-2 requires it to reproduce.

[4.3b](ppcp-rv.md#43-payload) is now scoped to the **top-level** map, with [4.3b1](ppcp-rv.md#43-payload) saying explicitly that nested maps are unconstrained and naming the five keys — because they are right there in the vector and the next reader will ask.

The host reviewer's observation about how it was found is the durable one: **W2 is visible only because the all-fields vector includes the nested maps the minimal one did not.** The vector added to catch V1 caught the fix for V1 two hours later.

## 8. PinPointStudio — second pass

### 8.1 W1 — 7.4e contradicted 5.3e

**Accepted, and the reviewer's stronger replacement taken.** [5.3e](ppcp-rv.md#53-psk-identity), new in Draft 2, forbids any value stable across connections appearing in the identity. 7.4e, unchanged from Draft 1, said the original `sid` *is* reused **for the PSK identity** — the exact thing forbidden — and its cross-reference pointed at a clause that had moved, landing on an unrelated rule about uniform failure that reads plausibly enough to go unnoticed.

This is the V2 fix not carried into the section an implementer is reading when they build persistence. As the reviewer put it: *a security document that tells you to transmit an identifier two sections after forbidding it will be resolved by whichever section the implementer read second.*

The tail clause is deleted rather than repaired, because the truth is now stronger than the exception: **after the initial derivation, `sid` survives only as the HKDF salt baked into `PRK`, and is never transmitted again by either peer on any connection.** That sentence is the one-line answer to *"where does `sid` go once the pairing persists?"* and it is what makes B6 closed rather than closed-by-renaming.

### 8.2 W3 — two clauses numbered 4.3b

**Accepted.** The V1 rule was inserted with the label the entropy requirement already held, and both are cited. The entropy rule is renumbered **4.3g**; the three citations of the key-length rule are unaffected. Clerical, and worth doing before the numbers reach a test name.

### 8.3 Consistency items

| | Item | Fix |
|---|---|---|
| 1 | **2d reinstated the role language 2e was added to remove**, one clause below it, and A9 had the same problem plus a stale MUST about the querier role | Both rewritten in terms of scanner and displayer, browser and advertiser. A9 now reflects that §3.5b is a SHOULD |
| 2 | B8's fallback needed one more constraint — a TLS 1.2 `psk_identity_hint` is server-sent, in the clear, and is exactly the hint mechanism 5.3e forbids; and 5.2h enumerated two properties when Draft 2 had added a third | **5.2h now states all three**, with the hint named explicitly as bound by property 3. Moot for the fallback, which is overtaken, but the principle binds any mechanism |
| 3 | RT-6 did not carry 4.4a's new precondition, so an implementation correctly applying 4.4a1 would fail it | RT-6 names the precondition and points at RT-15 for the other branch |
| 4 | Two stale references in §7.1 | Both fixed; the tracking row now cites **both halves** of that defence, §3.4 and §5.3, which is the whole of V2 |

## 9. Confirmed by both, no change needed

- **All eleven vectors** recomputed independently by both teams — HKDF from RFC 5869 rather than through a library, HMAC tags likewise, both CBOR payloads by hand. Every value matches, including both octet counts and both URI lengths, and the all-fields payload's first four octets `a8 61 76 01` are the demonstration V1 needed.
- **The deterministic order under the amended key set** is `v, dn, ep, mu, exp, psk, sid, wifi`. With `n` it would have been `n, v, …`. The fix holds.
- **RV-D1 accepted** by the reviewer who proposed the alternative: one identity form is right, and two equal-length forms starting with the same byte would need a discriminator for a saving of one HMAC.
- **A12's reasoning preferred** over the reviewer's own suggestion — *a special case is a rule an implementer can forget; a length constraint is one the encoder enforces for free* — and it keeps working for keys added later.
- **7.3e better than what was proposed**: the reviewer suggested letting a peer with an untrustworthy clock proceed; Draft 2 does that *and* puts enforcement on the publisher, which holds the authoritative clock and was previously not required to check `exp` at all.
- **§3.5's mechanism/recommendation split**, **4.3e**, **7.4f**, **§6b**, **4.3f** and **§8's PSK-interface paragraph** all confirmed by both teams.
- **`mu: 1` only**, from both publishers, until per-peer re-keying exists.

## 10. Status after the second pass

`PPCP-RV` is **Draft 3**. W1–W3 and all six consistency items are carried, and both teams state they have no further findings on the document as it stands.

**One thing is open and it is not a specification question:** which mechanism delivers 5.2h's three properties, now that the assumed one has been measured and cannot. That needs both teams and, on the evidence, its own review round for whichever §5 replaces the current one.

The host reviewer's closing note on where the defects have been found across two passes is worth carrying into that round:

> Draft 1's findings were in the model and the arithmetic. Draft 2's are in the **joins** — a clause that was correct before the fix next door landed, a rule whose scope nobody stated because the author knew what they meant, a label reused because the paragraph above it moved.


---

# The mechanism decision — 22 August 2026

Recorded separately from the review rounds, because it is not a review finding: it is a product decision that **overrides the stated position of both reviewers**, and it should be legible as such.

## 11. What was decided

**Route D.** Forward secrecy moves from *required* to *best-effort* ([`RV` 5.2h](ppcp-rv.md#52-tls-profile)), on the protocol owner's judgement that the data carried is not highly sensitive.

The measurement that forced the choice stands: TLS 1.3 with an external PSK does not complete on the mobile platform, and the TLS 1.2 ECDHE_PSK fallback is unreachable because the platform's ciphersuite enumeration contains no PSK suites at all. That was not in dispute.

What was in dispute was what to do about it, and there were four answers ([`RV` §5.4.2](ppcp-rv.md#542-what-the-routes-were)). The specification recommended Route B — a Noise handshake over the raw socket — which obtains all three properties. The decision taken is Route D, which does not.

## 12. What that costs, precisely

**Only property 2.** An attacker who records a session and **later** obtains its pairing secret can decrypt the recording retrospectively. Single use stops a photographed code being used to pair; it does not stop it decrypting the session it created.

**Not relaxed**, and worth stating because "relax the encryption" is a broader phrase than the change:

- the channel is still encrypted and still mutually authenticated;
- no unpaired peer receives anything;
- nothing stable crosses in the clear;
- [5.2f](ppcp-rv.md#52-tls-profile) — never fall back to an unencrypted connection, under any circumstances including a user instruction — is untouched;
- and where **both** peers can reach TLS 1.3, forward secrecy is still obtained. The relaxation applies to the leg that cannot, which today is the mobile one.

**The edge worth naming.** Swing video is a reasonable thing to judge non-sensitive. The part of the payload with a privacy dimension is the candidate-attached audio: retention attaches to candidates rather than shots, so it keeps windows for events that were *not* shots — an adjacent player, a conversation — and `PPCP-CORE` §13c states that their count is not bounded by anything the user does. In the lesson use case that is a coach and a pupil talking. That is the part of the payload this judgement is really about, and it is the owner's to make.

## 13. Both reviewers had said no

Neither was overruled by schedule pressure, which is the thing both were explicitly guarding against:

> *"this is the one clause in the document I would refuse to relax under schedule pressure"* — host review of Draft 1, on 5.2b
>
> *"Dropping forward secrecy is not available. §5.2h says so, A6 says why, and the reasoning is not weakened by the platform being awkward."* — mobile review of Draft 2, after running the check

The relaxation rests on a judgement about the **data**, which is a product owner's call and not a reviewer's. It should go back to both teams as a decision taken rather than a question reopened — and if either team's assessment of the payload differs from the owner's, that is the conversation to have, rather than a re-argument about mechanisms.

## 14. What the document does with it

It does not quietly restate the requirement. [5.2h](ppcp-rv.md#52-tls-profile) still names all three properties and marks property 2 **best-effort**, with the reason; [§5.4.2](ppcp-rv.md#542-what-the-routes-were) still names what would obtain it; [§7.1](ppcp-rv.md#71-threat-model) moves retrospective decryption into the *not defended* table with the trade named; and [A6](ppcp-rv.md#annex-a--decisions-and-alternatives) records that the clause was upheld against the platform and then relaxed on the data.

Four things now carry more weight because they are what is left ([`RV` §5.4.3](ppcp-rv.md#543-the-decision)):

- **5.2b1** — a peer offers the strongest mode its platform supports, and MUST NOT propose a weaker one than it has. Where both ends reach TLS 1.3, nothing was lost.
- **5.4g** — single use and publisher-side expiry become the *primary* defence around the pairing secret rather than a secondary one.
- **5.4h** — erase derived key material at session close unless the pairing is persisted. Key material that no longer exists cannot be disclosed later.
- **5.4i** — a deployment that judges its payload differently reverses this by choosing Route A or B, and only `§5` changes.

**B12** is recorded as the cheapest route back to property 2 without changing the transport: a per-session ratchet, re-deriving `PRK` at each close and erasing its predecessor, which restores forward secrecy for every session after the first. It is not specified and needs no decision now.


---

# Third pass — Draft 4

Both teams accepted the relaxation, and the host reviewer's framing of *how* it was taken is worth keeping:

> The decision was taken the right way. It was measured before it was argued, argued against stated properties rather than convenience, and recorded with both dissenting reviews quoted rather than summarised. **I would rather be overruled like that than agreed with quietly.**

Every finding in this pass was in **§5**, and every one was the same shape: a clause written while the property was required, still reading that way after it stopped being. As the host review put it — *"the clauses around it were written when the property was required, and they still read that way."*

## 15. The findings

### 15.1 R1 — the conformance suite forbade what 5.2a now permits

**Accepted, highest.** RT-4 required a handshake negotiating TLS 1.2 to be **refused**. Draft 4's 5.2a permits exactly that where a platform cannot do better, and §5.4.1 measured that the mobile platform negotiates precisely `TLS_PSK_WITH_AES_128_GCM_SHA256` at TLS 1.2. **The implementation the relaxation exists for failed its own suite.**

RT-4 is rewritten against **5.2b1** — refuse anything weaker than both peers can reach — which is what the requirement has become, rather than against a version number that is no longer the rule.

The reviewer's count is right and worth carrying: this is the **fourth** time a conformance test has been left asserting the behaviour a fix removed — I23/`CT-S4`, I32/`CT-I32`, `CT-I36`'s gaps, and now RT-4. [`PPCP-CORE` §11.1](ppcp-core.md#111-the-rule-for-writing-an-invariant) carries the rule for writing an invariant; the companion habit is that **a clause and its test are edited in the same pass.**

### 15.2 R2 — the decision rested on a measurement not taken on the platform it was about

**Accepted, and the gate restored.** Draft 4 demoted the device confirmation to *"still worth having, but no longer gates anything."* That was wrong, and the reviewer identified why precisely: **it gates the premise.** Route D was chosen because forward secrecy is unobtainable *in any TLS version through this interface* — a finding from the desktop variant. Shared availability annotations and a shared ciphersuite enumeration are good evidence; they are not the measurement.

[5.4b](ppcp-rv.md#541-what-was-measured) now requires it repeated on the device before an implementation ships a pairing that relies on the relaxation, worded so that **a favourable result changes no clause**: 5.2b1 already requires the strongest reachable mode, so if TLS 1.3 works on the phone the relaxation simply never applies. B8 is re-opened narrowly for this. The mobile team asked for the same confirmation independently.

### 15.3 R4 — the decision named an exception and did not take it

**Accepted as a SHOULD with an explicit escape, and it needs the owner's word to finish.**

§5.4.3 argues the relaxation on swing video and then states that the part of the payload carrying a privacy dimension is not the video but the **candidate-attached audio** — whose count is unbounded by anything the user does, and which in the lesson case is a coach and a pupil talking. The reviewer's summary is exact: **the trade was argued on video and paid for by audio.**

[5.4j](ppcp-rv.md#543-the-decision) applies the decision's own reasoning to the payload that reasoning named: a peer SHOULD not transfer candidate-attached audio payload over a connection without forward secrecy, unless the user has been told and agreed. The Capture is announced as normal, so its metadata and its absence stay assertable; only the payload waits. It costs a session nothing, because the diagnostic value of candidate audio is entirely after the fact.

**This is the one place where the specification has gone slightly beyond what the owner decided**, and it is deliberately reversible: if the judgement covers the audio too, **the clause is deleted rather than worked around**, and the text says so. What should not happen is §5.4.3 continuing to name an exception that nothing acts on, because the next reader will take it for an oversight.

### 15.4 R3, R5, R6 and the mobile team's finding

| | Finding | Disposition |
|---|---|---|
| **R3** | The two clauses now carrying property 2 — 5.2b1 and 5.4f — had no test, and cannot have an external one: an observer cannot distinguish a peer that offered less than it had from one that had less to offer | **RT-17**, `review` method, with the operative instruction to **re-read it whenever the TLS path is touched or a platform SDK updates** — because a platform that gains TLS 1.3 external PSK silently restores property 2, but only for an implementation that asks rather than assumes |
| **R5** | Forward secrecy became a per-connection outcome and nothing reported it, so 5.4i's own policy hook could not be applied and a peer could tell a user "encrypted" without telling the whole of it | **5.4k**: the achieved version and mode are available to the application layer and recorded in the diagnostic export, which is where every other "what actually happened" figure in this project already lives |
| **R6** | Property 3's server-side obligation — an empty `psk_identity_hint` — exists **only** on the newly-permitted TLS 1.2 path, and had no test | **RT-14 extended.** A requirement that applies solely to the mode Draft 4 made normal for one implementation |
| **PPC §2** | 5.2b1 and property 3 are unassertable on the platform that caused the relaxation, and 5.2i named only 5.2b | **5.2i widened** to any mechanism §5.2 constrains that a platform does not expose, with the three cases tabulated. *"Compliance by construction and compliance by accident look identical from outside"* |

### 15.5 The mobile team's second measurement

Recorded because it de-risks something that could have gone badly. RFC 4279 says a PSK identity "should" be UTF-8, and [5.3a](ppcp-rv.md#53-psk-identity) mandates 17 raw octets that generally are not — **the most likely second-order casualty of the TLS 1.2 floor.** It was tested with the §10.2 vector and the handshake completes unchanged.

[5.3f](ppcp-rv.md#53-psk-identity) now records it as a requirement: the identity is binary, and a peer MUST NOT transcode, validate as text, or truncate one.

### 15.6 Restated, and already addressed

The mobile team restated §4.3b as unqualified, noting they could not find it dispositioned and were *"restating it rather than assuming it was declined."* It **was** taken, in Draft 3: 4.3b is scoped to *"every key of the **top-level payload map**"*, with 4.3b1 naming `h`, `p`, `k` and `s` explicitly, and it is dispositioned at [§7.1](#71-the-scope-of-43b--w2-and-the-mobile-teams-2) of this document. The host reviewer confirmed the fix in the same round. It appears to be a stale copy rather than a missed finding — but the restatement was the right call, and the correct thing for a reviewer to do when a finding seems to have vanished.

### 15.7 Recorded, not the protocol's to answer

The mobile team raise that because they sit at the TLS 1.2 floor, **every** session between their app and any host has no forward secrecy, not some — and ask whether a user should be told. They do not think it belongs on a screen, and would rather it were a decision than an omission. That is exactly right, and it is a product question. [B13](ppcp-rv.md#annex-b--open-issues) records it; 5.4k makes it answerable either way by making the outcome readable.

## 16. Status after the third pass

`PPCP-RV` is **Draft 5**. §4, §6 and §7 are approved without reservation by both teams, and §4 — the part that cannot be corrected after a code is printed — has now survived three passes and three independent recomputations of its vectors.

Two things remain, and neither is a drafting question:

- **The device measurement** ([5.4b](ppcp-rv.md#541-what-was-measured)). An afternoon. A favourable result changes no clause and quietly restores the property.
- **Whether 5.4j stands or is deleted** — the owner's word on whether the sensitivity judgement covers candidate audio.
