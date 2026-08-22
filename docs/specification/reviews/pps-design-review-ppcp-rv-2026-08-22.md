# Design review — PPCP-RV Draft 1

**Reviewed as owner of PinPointStudio, the host implementation.**

| | |
|---|---|
| Document reviewed | `libppcp/docs/specification/ppcp-rv.md` — `PPCP-RV` 1.0 Draft 1, payload version `ppcp1` |
| Reviewer | PinPointStudio maintainer |
| Method | Read in full, plus independent recomputation of every test vector in §10 and of the deterministic key ordering of §4.3a |
| Date | 22 August 2026 |
| Verdict | **Four findings, one of which is in §4 and is provably wrong.** The document asks for §4 to get the hardest reading and the least benefit of the doubt; it needed it. The cryptography is sound and every vector reproduces byte-for-byte. Approve to implement once V1–V4 are settled. |

---

## 0. Position

This is a good first draft and it is doing the difficult half of the job — the half `PPCP-CORE` §12
has been pointing at an empty document for three drafts. §7.1's two tables, defended and *not*
defended, are the right way to write a threat model, and the "not defended" side is honest in a way
these sections usually are not.

**I verified the cryptography rather than reading it.** Every value in §10 reproduces exactly:
`PRK`, `K_tls`, `K_id`, `rid`, the 75-octet CBOR payload, the 105-character URI. The HKDF
construction is correct — public salt, secret IKM, distinct info strings — and the domain
separation argument in A5 holds: publishing `rid` on a multicast network reveals nothing about
`K_tls`. That level of correctness in a first draft is not typical and is worth saying before I
list what is wrong.

What is wrong falls into two groups. **V1 is an outright defect in the payload format**, found by
recomputing the ordering rule the document relies on. **V2 and V3 are places where the document
states a principle and then does not apply it** — a stable identifier in the clear, and a shared
secret treated as a pairwise one. **V4 is an interoperability gap** between `sid` here and
`Session.id` in `PPCP-CORE`.

---

## 1. Findings

### V1 — the payload's first key is not `v` whenever a display name is present {#v1}

**Severity: highest. It is in §4, which the document itself says cannot be changed after the first
code is printed, and RT-2 asserts the property that fails.**

Three statements depend on each other:

- **4.2a MUST** — *"The payload's first key is `v`, an unsigned integer."*
- **4.3a MUST** — *"The payload uses deterministic encoding (RFC 8949 §4.2.1). ... it places `v`
  first without a special rule, since `v` sorts before every other key defined here."*
- **RT-2** — *"the pairing code encodes and decodes byte-for-byte, and `v` is the first key."*

RFC 8949 §4.2.1 orders map keys by the bytewise lexicographic order of their **encoded** forms.
`"v"` encodes as `61 76`; `"n"` — the optional display name of §4.3 — encodes as `61 6e`. And
`0x6e < 0x76`.

I sorted the full defined key set under that rule:

```
['n', 'v', 'ep', 'mu', 'exp', 'psk', 'sid', 'wifi']
```

**`n` sorts first.** So any pairing code carrying a display name violates 4.2a, contradicts 4.3a's
stated reason, and fails RT-2. The §10.3 vector happens to omit `n`, which is why the arithmetic
looks right — I confirmed that vector reproduces exactly, all 75 octets and all 105 URI characters.
The defect is invisible in the only example given.

This matters more than a one-key mistake normally would, because 4.2a is the clause the whole
version story rests on. A peer that has not implemented `v = 2` is supposed to decode far enough to
find `v` and tell the user the code is newer than the application (4.2b) — which the document
rightly calls a user-experience contract rather than a parsing one. A parser written to read the
first key and stop, on the strength of 4.2a, reads a display name instead.

#### Requested change

The cheap fix is to make the ordering rule survive future keys rather than to patch this one.

> **(4.3b) MUST** Every payload key other than `v` is **at least two characters**. Under the
> deterministic ordering of RFC 8949 §4.2.1 a one-character key sorts before every two-character
> key, so this is what makes 4.2a true by construction rather than by coincidence, and it holds for
> keys added in later payload versions. The display name is therefore **`dn`**, not `n`.

Renaming `n` → `dn` costs one row in §4.3, one line in 4.4d, and nothing else — the field is
optional and no code has been printed. Doing it now costs nothing; doing it after the first release
is not possible, because the whole point of §4 is that printed codes outlive the software.

I would also add an assertion to RT-2: *"a payload carrying every optional field — `mu`, `exp`,
`dn`, `wifi` — still encodes `v` as its first key."* The current vector cannot catch this because it
carries none of them.

---

### V2 — the PSK identity is a stable, cleartext pairing identifier, which undoes §3.4 for every persisted pairing {#v2}

**Severity: high. The document states this exact principle and then does the thing it forbids.**

§5.3 is explicit about why the identity is not a peer identifier:

> The identity is deliberately not a peer identifier. **It is visible in the clear in the
> `ClientHello`**, so putting a stable peer identity there would undo [§3.4] at the first
> connection.

The identity is `0x01 || sid`. And §5.3c:

> A client offering a **persisted pairing** uses the `sid` of the session that pairing was
> established for.

So for a persisted pairing — the case §3 and §7.4 exist to serve — a **fixed 16-byte value is
broadcast in the clear on every reconnection, for the life of the pairing.** A passive observer at
two venues sees the same `sid` and links them. That is precisely the tracking beacon that §3.4, A7
and the whole rotating-identifier construction were built to eliminate, reintroduced one layer
down, at the first connection rather than at the advertisement.

The distinction between a stable *peer* identity and a stable *pairing* identity does no work here.
A golfer's pairing with their own studio host persists for months; for tracking purposes it is as
good as a device identifier, and better, because it also identifies which host they pair with.

Two consequences follow:

1. **§3.4's cost is paid for nothing.** An implementation does the HMAC-per-pairing work at
   discovery, keeps its advertisement unlinkable, and then discloses a permanent identifier in the
   first TLS flight of every session.
2. **§5.3b1's justification weakens.** The timing-oracle requirement is a SHOULD on the grounds
   that *"`sid` is 128 bits of randomness from a pairing code, so an attacker cannot produce an
   identity to probe with."* Once `sid` has been observed in a `ClientHello`, an attacker can
   produce exactly that identity, and can use the oracle to test whether a given host still holds
   that pairing.

#### Requested change

Make the identity resolvable, with the construction the document already has:

```
rn2      = 8 fresh random bytes per connection
identity = 0x01 || rn2 || HMAC-SHA256(K_id, "ppcp1 psk-id" || rn2)[0..7]     (17 octets)
```

A server recomputes the tag with the `K_id` of each pairing it holds and selects the match. That is
the same work A10 already accepted as cheap — *"trying each held pairing is cheap at the scale
involved"* — and it keeps the identity the same 17 octets it is now. `K_id`'s stated role in 5.1b
widens from "keys the resolvable identifier of §3.4" to "keys the resolvable identifiers", which is
one word.

**This also closes B6.** B6 records that the `sid`-bound identity is *"slightly odd"* and suggests a
pairing identifier independent of any session might be cleaner. It is not an aesthetic issue — it
is the finding above — and a resolvable identity resolves it without inventing a new identifier at
all.

For a **first** pairing the client can still offer `0x01 || sid` directly, since the server has just
generated that `sid` and holds exactly one candidate for it; only persisted pairings need the
resolvable form. Specifying both is two clauses and keeps the first handshake as simple as it is
now.

---

### V3 — `mu > 1` combined with §7.4a turns a shared pairing secret into a permanent group credential {#v3}

**Severity: high. Not stated anywhere, and it contradicts the property §7.1 claims.**

§7.3a permits a code to complete `mu` handshakes. Every peer that scans that code derives the
**same** `PRK`, therefore the same `K_tls` and the same `K_id`, from the same `psk` and `sid`.

So with `mu: 3`:

- The three devices hold **identical key material**. Any one of them can complete a handshake
  indistinguishable from any other's, and can impersonate another to the host.
- §7.4c's scoping — *"a persisted pairing is scoped to the counterpart peer identity learned inside
  the authenticated channel"* — is a **policy** statement, not a cryptographic one. Nothing stops a
  peer offering the shared key and declaring a different `Peer.id` in `hello`.
- §7.4a then permits **persisting `PRK`**, so the shared group key survives the session that
  created it, indefinitely, on three devices.

§7.1's first row claims the model defends against *"an unpaired peer on the same network receiving
capture payload"* by *"mutual authentication from a secret that only reaches the counterpart by
being scanned"*. With `mu > 1` that secret reached three counterparts, so "paired" names a group and
mutual authentication proves group membership, not identity.

B2 notices half of this — that there is no revocation story for the second and third device — but
not that the three share keys, which is the part that matters. A code used once and then
photographed is defended by 7.3a; a code deliberately used three times has no defence at all
between its three holders.

#### Requested change

I would not remove `mu`; the multi-device workflow is real and displaying three codes is worse
ergonomics for no security gain over a properly re-keyed group. Two clauses fix it:

> **(7.4f) MUST NOT** A peer persist `PRK` derived from a pairing code whose `mu` exceeded 1.
> A pairing established from a multi-use code is **session-scoped**, because its key material is
> held by every peer that scanned that code.
>
> **(7.4g)** Where a persistent pairing with a multi-use code is wanted, the peers derive a
> **fresh per-peer secret inside the authenticated channel** and persist that. Specifying that
> exchange is deferred; until it exists, multi-device pairing is per-session.

and add a row to §7.1's *not defended against* table:

> **Impersonation between peers that scanned the same multi-use code** — they hold identical key
> material by construction. `mu: 1` is the pairwise case; `mu > 1` is a group credential and should
> be read as one.

**PinPointStudio's position as publisher:** we will emit `mu: 1` only, and will not implement
`mu > 1` until per-peer re-keying exists. I would rather the specification said the same thing than
have the field available with the consequence unstated.

---

### V4 — `sid` is a byte string here and a text `Id` in `PPCP-CORE`, with no stated conversion {#v4}

**Severity: medium. Silent divergence between two conformant implementations, landing on the one
rule that exists to prevent duplicates.**

§4.3 defines `sid` as *"bstr, 16 bytes — Session identifier, **as `Session.id`** in `PPCP-CORE`
§5.10"*.

But `PPCP-CORE` §5.1 defines `Id` as *"Opaque UTF-8 string, 1–64 bytes"*, `PPCP-ENC` §4 encodes it
as a CBOR **text** string, and §5.10 describes `Session.id` as a UUID. So sixteen raw bytes have to
become a text `Id`, and nothing says how. Three encodings are all plausible and all
wrong-if-the-other-end-chose-differently:

| | Result for the §10 vector |
|---|---|
| Lowercase hex | `3f2504e04f8941d39a0c0305e82c3301` |
| Canonical UUID text | `3f2504e0-4f89-41d3-9a0c-0305e82c3301` |
| base64url | `PyUE4E-JQdOaDAMF6CwzAQ` |

This is not cosmetic. `PPCP-CORE` §8.5c keys idempotent re-import on **`Session.id` plus the
minting `Peer.id`**, and I34 scopes Capture identity by `Session.id`. Two implementations that
choose different textual forms produce different `Session.id` for the same session, and re-import
duplicates every Capture in it — which is exactly the failure 8.5c exists to prevent, arriving
through the rendezvous layer rather than through the reconciliation logic anyone would look at.

#### Requested change

One sentence, and I would pick the UUID form because §5.10 already says `Session.id` is a UUID:

> **(4.3e) MUST** `sid` is the 16 raw bytes of a UUID. The `Session.id` used in PPCP is its
> **canonical lowercase text form** — eight, four, four, four and twelve hexadecimal digits
> separated by hyphens — so that a `Session.id` minted at rendezvous and one minted by a hostless
> peer are the same kind of string. Peers MUST NOT use any other textual encoding of `sid`.

Add the text form to the §10.1 vector alongside the bytes, so RT-2 covers it.

---

## 2. Smaller points

| | Item | Where |
|---|---|---|
| 1 | **§2's table binds the paths to roles, which forecloses device-to-device pairing.** It reads *"Publishes the endpoint: host / Dials: device"*, while 2d correctly says the direction follows dialling rather than role. Two capture peers pairing directly is a stated requirement — UC-6 stereo, and offline multi-device alignment — and neither is a host. Say *"the peer that displays the code"* and *"the peer that scans it"* in the table, and the asymmetry survives a peer-to-peer pairing without a rewrite. | §2 |
| 2 | **B4 has a remedy available and does not take it.** 4.4a is a MUST to refuse an expired code, and a device with a badly wrong wall clock therefore refuses valid codes with no way out — on a range, with no network to correct the clock, which is the environment this path is for. Since 7.3a and 7.3b are the primary defence and are clock-free, the honest rule is: *a peer whose wall clock it has no reason to trust SHOULD proceed and rely on the publisher's single-use enforcement, reporting the code as possibly expired rather than refusing to attempt it.* The publisher is the party that can actually decide, and it will. | §4.4a, B4 |
| 3 | **`n` / `dn` length limit is unstated.** 4.4d requires the display name be *"length-limited"* without a number, and it is the one field an attacker controls in a payload shown before anything is authenticated. Give it a bound — 64 bytes is generous — so two implementations truncate the same way. | §4.4d |
| 4 | **RT-12's honesty is correct and should survive editing.** *"The requirement on which the whole model rests and the one no test can catch."* That is true, it is the right thing to say out loud, and it is exactly the kind of sentence that gets trimmed as unhelpful later. Keep it. | §9 |

---

## 3. What I checked and think is right

- **The vectors.** All seven reproduce byte-for-byte. I recomputed HKDF-Extract and HKDF-Expand
  from RFC 5869 directly rather than trusting a library's HKDF, and the CBOR by hand from the
  annotated octets. The 75-octet payload, the base64url and the 105-character URI are all correct.
- **A3 — a custom scheme rather than an `https` link.** Right, and the reasoning is the strongest
  in the annex: an absent application means the operating system opens the URL in a browser, which
  sends the secret to a web server and writes it to history. This is a real failure that shipped
  products have had.
- **A2 — the version inside the payload, not in the scheme.** Also right, and for the less obvious
  reason: a scheme the application has not registered is never delivered to it, so the user sees
  nothing at all rather than a message they can act on. The version marker is for the user.
- **A6 — `psk_dhe_ke` mandatory.** Correct, and correctly identified as the requirement most likely
  to be dropped for simplicity and most expensive to add back. I would go further: this is the one
  clause in the document I would refuse to relax under schedule pressure.
- **7.5d — no application data on an early-data path.** Easy to miss, and the consequence is
  concrete: TLS 1.3 early data is replayable by design, and a resumed connection accepting `arm` or
  a capture request as early data accepts a replay of it.
- **7.2b naming diagnostic export explicitly.** Right, and right for the stated reason — the bundle
  is assembled by code whose author is thinking about clock residuals. This is our code, and I will
  hold PinPointStudio to it.
- **3.2b — no user-assigned device name in the instance name.** The platform default really is the
  device name, which is frequently a person's name, and publishing it on a range's network is a
  privacy failure no amount of transport encryption repairs.
- **A1 — ratify `_ppcp._tcp`.** Agreed, as I said last round. The string already shipped, it is
  unregistered and unambiguous, and changing it buys a silent no-discovery window for nothing.

---

## 4. Host-side notes

Not specification findings. Recorded so the host cost is visible rather than discovered, as in the
previous rounds.

**PinPointStudio has no networking at all today.** No `QTcpServer`, no `QTcpSocket`, no
`QUdpSocket` anywhere in `src/`, no TLS library linked, and no QR generation. Under `PPCP-RV` the
host is the code publisher, the listener and the TLS **server** — every role that needs to exist
first — so this is a from-zero subsystem, not an extension of one.

**The Qt PSK path is a trap and I want it written down before someone finds it.** `QSslSocket`
exposes pre-shared keys through `preSharedKeyAuthenticationRequired`, which is a TLS 1.2-era
interface; it does not give access to TLS 1.3 external PSKs, and 5.2a forbids negotiating anything
below 1.3. The host will link OpenSSL 3.x directly and use the external-PSK session callbacks. That
is a fiddlier API than it looks — the PSK has to be installed as a synthetic session with the right
cipher and hash bound to it, and getting the hash wrong (5.2c) produces a handshake that fails with
no useful diagnostic. Worth one paragraph in §8's operational notes, since both implementations hit
it and the failure looks like a key mismatch rather than an API mistake.

**Consequence for sequencing.** This does not change my position that v1 should ship offline-only.
It strengthens it: the offline path needs no rendezvous, no TLS and no sockets, and `PPCP-RV` §9a
already says a peer handed an established socket is fully conformant with no rendezvous
implementation at all.

---

## 5. Sign-off

**Approve to implement once V1–V4 are settled.** V1 before anything else, because it is in the one
part of the document that cannot be corrected later and the fix is a rename that costs nothing
today.

| | Fix | Cost |
|---|---|---|
| **V1** | Require every payload key but `v` to be at least two characters; rename `n` → `dn`; extend RT-2 to a payload carrying every optional field | one rule, one rename |
| **V2** | Make the persisted-pairing PSK identity resolvable under `K_id`, as `rid` already is; closes B6 | one construction, two clauses |
| **V3** | Forbid persisting a `PRK` from a code with `mu > 1`; add the impersonation row to §7.1 | two clauses, one table row |
| **V4** | State that `Session.id` is the canonical lowercase UUID text form of `sid`; add it to the §10.1 vector | one clause |

None adds a message, a round trip or a dependency. V2 is the only one that changes bytes on the
wire, and it changes seventeen octets into a different seventeen octets.

Once these land I have no further findings on `PPCP-RV`, and combined with the three on `PPCP-CORE`
Draft 3, PinPointStudio is ready to build against the set.

---

## 6. One closing observation

The three `PPCP-CORE` rounds each found a different class of defect, and this document's findings
sort the same way. V4 is a **model** defect — two documents describing one identifier differently.
V2 and V3 are **fixes not carried through** — a principle stated and then not applied to the case
next to it. V1 is a **seam** defect: two correct rules, 4.2a and 4.3a, that do not compose once a
third key exists.

What is different here is that V1 was not findable by reading. The document asserts that `v` sorts
first, the assertion is plausible, the worked example confirms it, and it is false only for a field
the example omits. It took recomputing the ordering rule against the full key set.

That is the argument for the test vectors being in the document at all, and for RT-1 and RT-2
asserting byte-for-byte reproduction — and it is also the argument for extending RT-2 to a payload
that carries **every** optional field rather than the minimal one. A vector that exercises only the
common case validates only the common case. The same is true of `CT-S1` in `PPCP-CONF`, for the
same reason, and it was the same finding there.
