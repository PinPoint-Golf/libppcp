# PPCP-RV review disposition

**How each first-pass review finding was handled in Draft 2.**

| | |
|---|---|
| Status | Record of decisions. Non-normative. |
| Date | 22 August 2026 |
| Round covered | First pass on `PPCP-RV` Draft 1, by both implementation teams. Both reviews are in [`reviews/`](reviews/). |
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
