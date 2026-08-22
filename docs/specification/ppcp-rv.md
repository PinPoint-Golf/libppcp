# PPCP-RV — Rendezvous

**How two PPCP peers find one another, prove they are meant to be talking, and hand PPCP an authenticated byte stream. Includes the security model PPCP itself does not have.**

| | |
|---|---|
| Document | `PPCP-RV` |
| Version | **1.0, Draft 5** |
| Payload version | `ppcp1` |
| Status | **Draft — unblocked. [§5.2](#52-tls-profile) resolved by a product decision on data sensitivity ([§5.4](#54-resolved-the-mechanism)); forward secrecy is now best-effort rather than required.** |
| Date | 22 August 2026 |
| Versioned | Independently of PPCP. Same repository. |
| Relates to | [`PPCP-CORE`](ppcp-core.md) §3 (transport contract), §5.2.1 (peer identity), §12 (security considerations) |
| Reviews | [`reviews/`](reviews/) — first-pass reviews from PinPointCapture and PinPointStudio, dispositioned in [`rv-review-disposition-2026-08-22.md`](rv-review-disposition-2026-08-22.md) |
| Conformance | **Implementing `PPCP-RV` is OPTIONAL.** Implementing PPCP is not. |

---

## 0. Status

This is the companion specification that [`PPCP-CORE` §12](ppcp-core.md#12-security-considerations) delegates its security model to. Until it is agreed, **PPCP's security model is not delegated — it is absent**, and no cross-implementation pairing is testable.

**Draft 2** carries the first-pass findings from both implementation teams. Draft 1 asked for [§4](#4-rv-2--the-pairing-code) to get the hardest reading and the least benefit of the doubt, and it needed it: the host reviewer recomputed the deterministic key ordering that §4.3 relies on and found that **`v` was not in fact the first key whenever a display name was present** — a defect invisible in the only worked example, in the one part of the document that cannot be corrected after a code is printed. That is [§4.3b](#43-payload), and it is the argument for putting test vectors in a specification and for exercising them with every optional field rather than none.

This is the companion specification that [`PPCP-CORE` §12](ppcp-core.md#12-security-considerations) delegates its security model to. Until it is agreed, **PPCP's security model is not delegated — it is absent**, and no cross-implementation pairing is testable.

**[§4](#4-rv-2--the-pairing-code) is the part that cannot be corrected later at all.** A pairing code carries an opaque fixed payload that both sides must parse with no chance to negotiate first, and printed codes outlive releases. It has now survived three review passes and three independent recomputations of its vectors — the first pass found a defect in it that was invisible in the worked example, which is why the vectors are in the document and why one of them exercises every optional field.

**[§5.2](#52-tls-profile) is the part still open.** A platform measurement ruled out the assumed mechanism and the owner relaxed forward secrecy to best-effort on the sensitivity of the data carried; [§5.4](#54-resolved-the-mechanism) records the measurement, the decision, what was given up and what both reviewers said about it.

**The change history is [Annex C](#annex-c--change-history)**, at the back.

---

## 1. Scope

### 1.1 What rendezvous is, and why it is not in PPCP

**Transport is a local choice.** TCP, a USB tunnel, QUIC — two conformant peers that pick different transports simply do not connect over that path, and no interoperability claim is harmed.

**Rendezvous is a mutual agreement problem.** If one implementation advertises `_ppcp._tcp` while another browses `_swingcap._tcp`, they never meet, and every invariant downstream is unreachable. A protocol whose peers cannot find one another is open in theory only. That is why this document exists, and why it is a specification rather than a deployment note.

It is separate from PPCP for two reasons: it will version faster — near-field pairing, deep links and low-energy pairing will all arrive without touching a single Timebase or Shot — and a peer connecting only over USB, or handed a socket by an embedding application, should not have to implement multicast discovery to be conformant.

### 1.2 What this document specifies

| | |
|---|---|
| **RV-1** | Service discovery: service type, instance naming, TXT record contents ([§3](#3-rv-1--service-discovery)) |
| **RV-2** | The pairing code: URI form, payload encoding, fields, version handling ([§4](#4-rv-2--the-pairing-code)) |
| **RV-3** | Key derivation and the TLS profile ([§5](#5-rv-3--key-derivation-and-tls)) |
| **RV-4** | Optional network join ([§6](#6-rv-4--network-join)) |
| **RV-5** | The security model ([§7](#7-rv-5--security-model)) |

### 1.3 Where it stops

- **(1.3a) MUST** Rendezvous ends when an authenticated, encrypted, bidirectional byte stream exists. Everything after that is PPCP.
- **(1.3b) MUST NOT** Anything in this document change the meaning of any field defined in [`PPCP-CORE`](ppcp-core.md).
- **(1.3c) MUST NOT** Any PPCP message be sent before the handshake of [§5](#5-rv-3--key-derivation-and-tls) completes. `hello` is the first byte of application data on an established, authenticated connection.

Out of scope, and deliberately: which transport is used, platform permission handling, user interface, and how a peer stores its own secrets at rest beyond the requirements in [§7.2](#72-handling-the-pairing-secret).

### 1.4 Requirement keywords

As in [`PPCP-CORE` §2.1](ppcp-core.md#21-requirement-keywords) — BCP 14, and only when in capitals.

---

## 2. Rendezvous paths

Three paths reach the same place. **They differ in which peer dials**, and getting that wrong is the "never meet" failure this document exists to prevent.

| Path | Publishes the endpoint | Dials | Listens | Status |
|---|---|---|---|---|
| **Pairing code** ([§4](#4-rv-2--the-pairing-code)) | the peer that **displays** the code, in the code | the peer that **scans** it | the displayer | **Primary.** REQUIRED of any RV implementation |
| **Service discovery** ([§3](#3-rv-1--service-discovery)) | the peer that **advertises**, via mDNS | the peer that **browses** | the advertiser | OPTIONAL. Reconnection convenience only |
| **Direct** | out of band — a tunnel, a cached endpoint, a socket handed in by an embedding application | either | either | OPTIONAL |

- **(2a) MUST** An RV implementation supports the pairing-code path.
- **(2b) MAY** An implementation support the discovery path, the direct path, both or neither in addition.
- **(2c) MUST** Whichever path is used, the resulting connection completes the handshake of [§5](#5-rv-3--key-derivation-and-tls) before any PPCP message crosses it. There is no unauthenticated path.

- **(2e) MUST** The table is written in terms of **what a peer does**, not what role it holds. Nothing here requires a host at either end. Two capture peers pairing directly — the multi-device case — is one displaying a code and the other scanning it, with no host involved.

**Why the two paths dial in opposite directions**, since it looks like an inconsistency:

A code can only carry the endpoint of the peer that displayed it, so on that path the scanner dials. In the ordinary deployment the host has the screen and the capture peer has the camera, which is why it usually reads as host-displays/device-dials — but that is the deployment, not the rule.

For discovery the constraint runs the other way. Whoever **browses** needs only the multicast querier role: it can send from an ephemeral port with the unicast-response bit set and never bind the multicast DNS port, which avoids conflicting with the responder that already owns that port on most desktop platforms and does not exist at all on some. Whoever **advertises** needs a responder. That is why [§3.5b](#35-who-advertises-and-who-browses) recommends the capture peer advertise and the host browse — a mobile platform supplies a responder, several desktop platforms make it awkward, and capture requires the foreground so the capture peer is the one reliably present.

The cost is that a peer supporting both paths implements both a listener and a connector. That is accepted, and it is why only the code path is required.

- **(2d)** Two things follow the dialling direction rather than any peer's role, and both differ between the paths: which peer is the TLS client ([§5.2g](#52-tls-profile)), and which peer sends `hello` rather than `hello_accept`. **The scanner dials and is therefore the initiator; the displayer listens and states its support window in `hello_accept.min_version`.** In the ordinary deployment the displayer is the host, which is the right way round for the old-application/new-host case. On the discovery path it is reversed, and in a peer-to-peer pairing neither is a host. Neither `PPCP-CORE` §10.1 nor this document assumes a direction; both are written in terms of initiator and responder for that reason.

---

## 3. RV-1 — Service discovery

*Optional. Reconnection convenience only — a first pairing always uses [§4](#4-rv-2--the-pairing-code).*

### 3.1 Service type

- **(3.1a) MUST** The DNS-SD service type is **`_ppcp._tcp`**.
- **(3.1b) MUST NOT** An implementation advertise or browse any other service type for PPCP rendezvous.

### 3.2 Instance name

- **(3.2a) MUST** The instance name is `PPCP-` followed by the first four bytes of `rid` ([§3.3](#33-txt-record)) in uppercase hexadecimal — for example `PPCP-9B1D2DF9`.
- **(3.2b) MUST NOT** The instance name contain a user-assigned device name, a person's name, a model identifier, or any other value that persists across pairings.

3.2b exists because platform advertising APIs commonly default the service name to the device name, which is frequently a person's name. Publishing that on a driving range's network is a privacy failure that no amount of transport encryption repairs, and it happens by default unless the name is set explicitly.

### 3.3 TXT record

- **(3.3a) MUST** The TXT record carries exactly these keys, and a receiver ignores any key it does not recognise.

| Key | Value | Notes |
|---|---|---|
| `txtvers` | `1` | Record format version. |
| `pv` | e.g. `1.0` or `1.0-1.2` | PPCP wire versions supported. A browser filters on MAJOR **before** connecting. |
| `role` | `host` \| `capture` \| `observer` | The role the peer intends to take. |
| `rn` | 16 hexadecimal characters | **Rotating nonce**, 8 bytes from a CSPRNG. |
| `rid` | 16 hexadecimal characters | **Resolvable identifier**, computed from `rn` — see [§3.4](#34-resolvable-identifiers). |

- **(3.3b) MUST NOT** The record carry `Peer.id`, a device or user name, a serial number, a session identifier, a count of stored sessions, or any capability detail. Capability is declared inside the authenticated channel, where it belongs.
- **(3.3c) SHOULD** The whole record stay under 200 bytes so it fits a single response.

### 3.4 Resolvable identifiers

A peer that advertised a stable identifier would be trackable across every venue it visits. A peer that advertised nothing identifying could not be recognised by a host it has already paired with. The resolution is an identifier that only a peer holding the pairing can resolve.

For a pairing whose key material is `K_id` ([§5.1](#51-key-derivation)):

```
rn  = 8 random bytes from a CSPRNG
rid = HMAC-SHA256(K_id, "ppcp1 rid" || rn)  truncated to the first 8 bytes
```

- **(3.4a) MUST** `rn` is regenerated on every service registration and at least every 15 minutes thereafter, and `rid` recomputed with it.
- **(3.4b) MUST** A browsing peer resolves a discovered `rid` by recomputing it with the `K_id` of each pairing it holds. A match identifies the pairing to offer in [§5.2](#52-tls-profile).
- **(3.4c) MUST NOT** A browsing peer connect to an instance whose `rid` it cannot resolve.
- **(3.4d)** A peer holding several pairings advertises the one it is offering to reconnect. Advertising several simultaneously is not specified — see [Annex B3](#annex-b--open-issues).

The construction is the same idea as a resolvable private address: unlinkable to a stranger, recognisable to a counterpart, and rotating so that observations in two venues cannot be correlated.

- **(3.4e)** **Residual exposure, stated rather than hidden.** Anyone on the link can see that a PPCP-capable peer is present, and anyone holding a pairing can test whether a given advertisement is that peer. Neither is fixable while the peer advertises at all, and both are why advertising is confined to the reconnection case.

### 3.5 Who advertises and who browses

- **(3.5a) MAY** Any peer advertise; any peer browse. `role` in the TXT record ([§3.3](#33-txt-record)) therefore legitimately carries any of its values, and a peer that discovers a counterpart dials it ([§2](#2-rendezvous-paths)).
- **(3.5b) SHOULD** A **capture peer advertises** and a **host browses**. Browsing needs only the querier role; advertising needs a responder, which a mobile platform supplies and several desktop platforms do not. Capture also requires the foreground, so the capture peer is the one reliably present to be found.
- **(3.5c)** A deployment that reverses this — a host advertising so a capture peer can browse and dial on reconnection — is conformant, and is the shape a "reconnect to a discovered host" interaction needs. The cost is that the host supplies its own responder, which is a platform question rather than a protocol one.

### 3.6 Multicast is not to be relied on

- **(3.6a) MUST NOT** An implementation treat discovery failure as an error state. Multicast is rate-limited or dropped by many consumer access points, blocked by client isolation on guest networks, and does not cross VLAN boundaries. **It will not work at a range.**
- **(3.6b) MUST** Failure to discover falls back to the pairing code or a cached endpoint, without user-visible failure.

---

## 4. RV-2 — The pairing code

*Primary path. Required. **The one part of this specification that cannot be changed after release.***

### 4.1 URI form

- **(4.1a) MUST** The pairing code encodes the URI:

```
ppcp:<base64url(payload)>
```

with no padding, where `payload` is the CBOR map of [§4.3](#43-payload).

- **(4.1b) MUST** The scheme is `ppcp` and does not change between payload versions. Versioning is inside the payload ([§4.2](#42-version-handling)).
- **(4.1c) MUST NOT** A pairing code use an `http` or `https` URL. It carries a secret; if the receiving application is not installed, the operating system opens such a URL in a browser, which sends the secret to a web server and writes it to browsing history. Use of a scheme no browser claims is what prevents that.
- **(4.1d) SHOULD** The code is rendered at error-correction level M or higher.

4.1b is the deliberate part. Putting the version in the scheme — `ppcp1:`, `ppcp2:` — would let a parser reject before decoding, but an application that has not registered the newer scheme never receives the code at all, and the user sees nothing happen. A stable scheme with the version inside means the application always gets the payload and can say what is wrong.

### 4.2 Version handling

- **(4.2a) MUST** The payload's first key is `v`, an unsigned integer. This draft defines `v = 1`. [§4.3b](#43-payload) is what makes this true by construction.
- **(4.2b) MUST** A peer that decodes a `v` it does not implement reports to its user that **the code requires a newer version of the application**, and does not report a generic failure.
- **(4.2c) MUST** A peer ignores payload keys it does not recognise, at every nesting level, and does not treat them as an error.
- **(4.2d) MUST NOT** A peer act on any other field of a payload whose `v` it does not implement.

4.2b is the whole point of the version marker, and it is a user-experience contract rather than a parsing one. A pairing code that fails with "could not pair" tells a user nothing they can act on; one that says "this code is newer than this app" tells them exactly what to do.

### 4.3 Payload

CBOR, as [`PPCP-ENC` §4](ppcp-encoding.md#4-primitive-types) — so an implementation needs no parser it does not already have.

- **(4.3a) MUST** The payload uses deterministic encoding (RFC 8949 §4.2.1). This makes a given pairing reproduce byte-identical codes.
- **(4.3b) MUST** Every key of the **top-level payload map** other than `v` is **at least two characters**. RFC 8949 §4.2.1 orders keys by the bytewise lexicographic order of their *encoded* forms, so a one-character key — encoded `0x61 XX` — sorts before every two-character key, encoded `0x62 XX YY`. Making `v` the only one-character key at the top level is what makes [4.2a](#42-version-handling) true **by construction**, and it holds for keys added in later payload versions.
- **(4.3b1)** **Nested maps are unconstrained.** The `ep` entries use `h` and `p`, and `wifi` uses `h`, `k` and `s`; those are one character each and correctly so. The rule exists only to fix the *first* key of the top-level map, and no nested map has a first key anyone depends on.

4.3b exists because Draft 1 claimed `v` sorted first and it did not. The optional display name was `n`, which encodes `61 6e`, and `v` encodes `61 76` — so `0x6e < 0x76` and **any code carrying a display name put `n` first**, contradicting 4.2a and failing RT-2. The worked example omitted the field, so the arithmetic looked right. The field is now `dn`.

This is worth more than the one-key fix. 4.2a is the clause the whole version story rests on: a peer that has not implemented a later `v` is expected to decode far enough to find it and tell the user the code is newer than the application ([4.2b](#42-version-handling)). A parser written to read the first key and stop — which 4.2a invites — would have read a display name instead.

| Key | Type | Card. | Meaning |
|---|---|---|---|
| `v` | uint | 1 | Payload version. `1`. |
| `ep` | array of `{ h: tstr, p: uint }` | 1..n | Endpoints the publisher is reachable at, **most preferred first**. `h` is a literal address or hostname; `p` a TCP port. |
| `mu` | uint | 0..1 | Maximum successful pairings this code may complete. Default `1`. |
| `psk` | bstr, 16 or 32 bytes | 1 | The pairing secret. |
| `sid` | bstr, 16 bytes | 1 | Session identifier, as `Session.id` in [`PPCP-CORE` §5.10](ppcp-core.md#510-session). |
| `exp` | uint | 0..1 | Expiry, seconds since the Unix epoch. |
| `wifi` | map | 0..1 | Network join — [§6](#6-rv-4--network-join). |
| `dn` | tstr | 0..1 | Display name for the publisher, **at most 64 bytes**. **Untrusted** — see 4.4d. |

- **(4.3g) MUST** `psk` is at least 16 bytes from a cryptographically secure random number generator ([§7.2](#72-handling-the-pairing-secret)).
- **(4.3c) MUST** A scanning peer tries `ep` entries in order and stops at the first that completes the handshake.
- **(4.3d) SHOULD** A publisher list every address it is reachable at — wired, wireless, and its hotspot address where it provides one. This is what makes the code work when discovery does not.
- **(4.3e) MUST** `sid` is the 16 raw bytes of a UUID. The `Session.id` used in PPCP ([`PPCP-CORE` §5.10](ppcp-core.md#510-session)) is its **canonical lowercase text form** — eight, four, four, four and twelve hexadecimal digits separated by hyphens. Peers MUST NOT use any other textual encoding of `sid`.
- **(4.3f) MUST** Where `wifi` is present and the peer is not already associated with that network, it joins ([§6](#6-rv-4--network-join)) **before** walking `ep`; where it is already associated, it walks `ep` directly. On total failure of every endpoint it MAY join and walk again.

4.3e closes an interoperability gap between two documents. `sid` is sixteen raw bytes here; `Id` in [`PPCP-CORE` §5.1](ppcp-core.md#51-notation-and-primitive-types) is an opaque UTF-8 **string**, encoded as a CBOR text string. Hexadecimal, canonical UUID text and base64url are all plausible conversions and all wrong if the other end chose differently — and [`PPCP-CORE` §8.5c](ppcp-core.md#85-reconciliation) keys idempotent re-import on `Session.id`. Two implementations choosing different textual forms would duplicate every Capture in a re-imported session, which is exactly the failure that rule exists to prevent, arriving through the rendezvous layer where nobody would look for it.

### 4.4 Handling a scanned code

- **(4.4a) MUST** A peer whose wall clock it has reason to trust refuses a code whose `exp` has passed, and reports it as expired rather than as a failure to connect.
- **(4.4a1) SHOULD** A peer with positive reason to distrust its wall clock — never synchronised since boot, or reading earlier than the software's own build date — **attempts the pairing anyway** and reports the code as *possibly* expired. The publisher holds the authoritative clock and enforces `exp` itself ([§7.3e](#73-single-use-and-expiry)); a device with a wrong clock at a range has no network to correct it and refusing a valid code leaves the user with no path at all.
- **(4.4b) MUST** A peer that cannot decode the payload reports an invalid code. It does not attempt any connection.
- **(4.4c) MUST NOT** A peer log a payload, include one in a diagnostic export, or retain one after the pairing it establishes has ended ([§7.2](#72-handling-the-pairing-secret)).
- **(4.4d) MUST** `dn` is treated as untrusted display text: it is shown before anything has been authenticated, so it is whatever was printed on the code. It is escaped for display, truncated to at most 64 bytes, and **MUST NOT** be used as an identifier, a trust signal, or a storage key.

### 4.5 Size

A code carrying one endpoint, a 16-byte secret and a session id is **75 bytes of CBOR, 100 base64url characters, 105 characters of URI** ([§10](#10-test-vectors)) — comfortably scannable from a screen at arm's length. Adding network credentials adds roughly the length of the network name plus its passphrase.

- **(4.5a) SHOULD** A payload stay under 400 bytes. Beyond that the code becomes dense enough that scanning distance and camera focus start to matter, and the primary pairing path is exactly where that must not be marginal.

---

## 5. RV-3 — Key derivation and TLS

### 5.1 Key derivation

The secret in the pairing code is **never used directly** as a protocol key. Both peers derive from it with HKDF-SHA256 (RFC 5869):

```
PRK   = HKDF-Extract(salt = sid, IKM = psk)
K_tls = HKDF-Expand(PRK, "ppcp1 tls-psk",        32)
K_id  = HKDF-Expand(PRK, "ppcp1 rendezvous-id",  32)
```

where `sid` and `psk` are the raw bytes of those payload fields, and the info strings are their ASCII bytes with no terminator.

- **(5.1a) MUST** `K_tls` is the TLS external pre-shared key, and is used for nothing else.
- **(5.1b) MUST** `K_id` keys the resolvable identifiers of [§3.4](#34-resolvable-identifiers) and [§5.3](#53-psk-identity), and is used for nothing else.
- **(5.1c) MUST** A peer that persists a pairing ([§7.4](#74-persistent-pairings)) persists `PRK` and derives from it, never the original `psk`.

Derivation rather than direct use is what keeps the two purposes independent: an identifier published in the clear on a multicast network is computed from a key that cannot be used to complete a handshake, and observing millions of `rid` values reveals nothing about `K_tls`.

### 5.2 TLS profile

- **(5.2a) MUST** TLS with an external pre-shared key. **TLS 1.3 (RFC 8446) is used wherever both peers can reach it**; TLS 1.2 with a PSK ciphersuite is permitted only where a peer's platform cannot ([§5.4](#54-resolved-the-mechanism)). Nothing below TLS 1.2 is ever negotiated.
- **(5.2b) MUST** At TLS 1.3 the key exchange mode is **`psk_dhe_ke`**; `psk_ke` MUST NOT be used. At TLS 1.2, `TLS_ECDHE_PSK_*` is used where the platform can name it, and a plain `TLS_PSK_*` suite only where it cannot.
- **(5.2b1) MUST** A peer offers the strongest option it has and accepts the strongest the counterpart offers. **A peer MUST NOT propose a weaker mode than its platform supports**, because the weakest end sets the outcome and there is no way for the other to tell a limitation from a choice.
- **(5.2c) MUST** The PSK's associated hash is SHA-256.
- **(5.2d) MUST** `TLS_AES_128_GCM_SHA256` is supported at TLS 1.3. Where TLS 1.2 is used, `TLS_PSK_WITH_AES_128_GCM_SHA256` (RFC 5487) is the interoperable floor; `TLS_ECDHE_PSK_WITH_AES_128_GCM_SHA256` (RFC 8442) SHOULD be preferred where available.
- **(5.2e) MUST NOT** Certificates, a public-key infrastructure, or a certificate authority be required. A peer MUST NOT reject a counterpart for presenting no certificate.
- **(5.2f) MUST NOT** An implementation fall back to an unencrypted connection under any circumstances, including a handshake failure, a timeout, or a user instruction. A failed handshake is a failed connection.
- **(5.2g) MUST** The peer that dialled is the TLS client; the peer that listened is the TLS server. This follows the dialling direction of [§2](#2-rendezvous-paths) and differs between the two paths.
- **(5.2h)** **The properties this profile exists to deliver are three:**

  | | Property | Status |
  |---|---|---|
  | 1 | **Mutual authentication** from a secret that only reaches the counterpart by being scanned | **Required.** Unchanged. |
  | 2 | **Forward secrecy** of captured traffic against later disclosure of that secret | **Best-effort.** Obtained wherever the peers can reach TLS 1.3 `psk_dhe_ke` or a TLS 1.2 ECDHE_PSK suite; not obtained otherwise. See [§5.4.3](#543-the-decision). |
  | 3 | **No value stable across connections crosses in the clear** ([5.3e](#53-psk-identity)) | **Required.** Unchanged, and it binds a **server-sent** field as much as a client-sent one: a `psk_identity_hint`, which exists in the TLS 1.2 PSK model and is sent in the clear, MUST be empty. |

  Property 2 was `Required` until Draft 4 and was relaxed by a product decision on the sensitivity of the data carried, **not** by a mechanism turning out to be inconvenient. Properties 1 and 3 are not negotiable, and neither is [5.2f](#52-tls-profile).
- **(5.2i)** A peer whose platform does not expose a mechanism this section constrains cannot assert the corresponding clause by construction. It demonstrates conformance by **observed handshake** — a wire capture, or a counterpart instrumented to refuse what the clause forbids — which is why RT-4's method is `injected` rather than `static`. This covers, at minimum:

  | Clause | What the platform may not expose |
  |---|---|
  | [5.2b](#52-tls-profile) | the key-exchange mode |
  | [5.2b1](#52-tls-profile) | which ciphersuites are offered — a peer that cannot *name* a PSK suite cannot choose to withhold one, and complies by having no way to do otherwise |
  | [5.2h](#52-tls-profile) property 3 | whether an omitted `psk_identity_hint` is sent empty or omitted entirely |

  Compliance by construction and compliance by accident look identical from outside, and a packet capture settles all three.

**What 5.2b buys where it is reachable.** An ephemeral Diffie-Hellman exchange runs alongside the PSK, so a later compromise of the pairing secret does not retroactively expose captured traffic. Without it, anyone who recorded a session and subsequently obtains that secret decrypts the recording. The cost is one round trip of elliptic-curve arithmetic, which is why 5.2b1 requires a peer to offer it whenever it can rather than settling for the floor.

**Downgrade is not a live attack here.** The negotiated version and ciphersuite are covered by the handshake transcript, and the transcript is authenticated by the PSK — so an attacker cannot force a weaker mode without the secret, and an attacker holding the secret has already won. What 5.2b1 guards against is not an attacker but an implementation that offers less than it has, which no peer on the other end can distinguish from a platform limitation.

TLS 1.3 with an external PSK provides **mutual authentication**: each end proves it holds `K_tls`, so no certificate is needed for either direction. That is exactly the property required — no unpaired peer may receive capture payload — with none of the infrastructure a certificate model would drag in.

### 5.3 PSK identity

The TLS client sends an identity so the server can select the right key.

- **(5.3a) MUST** The identity is the 17 octets:

```
0x01 || rn2 || tag

  rn2 = 8 random bytes from a CSPRNG, fresh per connection
  tag = HMAC-SHA256(K_id, "ppcp1 psk-id" || rn2)  truncated to the first 8 bytes
```

- **(5.3b) MUST** A server resolves an offered identity by recomputing `tag` with the `K_id` of each pairing it holds — outstanding codes and persisted pairings alike — and selecting the match.
- **(5.3c) MUST** A server that resolves no pairing aborts the handshake, with the **same alert** it would send for a resolved identity and a wrong key. Failing uniformly costs nothing and is required.
- **(5.3d) SHOULD** The two cases are also indistinguishable in **timing**. This is harder — a wrong key normally fails later, at Finished verification, than an unresolvable identity — and the usual technique is to proceed with a dummy key so both paths run to the same point.
- **(5.3e) MUST NOT** `sid`, `Peer.id`, or any other value stable across connections appear in the identity.

- **(5.3f)** The identity is **binary and need not be valid UTF-8.** RFC 4279 says a PSK identity "should" be UTF-8, and 5.3a mandates 17 raw octets that generally are not — which was the most likely second-order casualty of the TLS 1.2 floor. It has been measured: the [§10.2](#102-resolvable-identifiers) identity completes a handshake at TLS 1.2 with ciphersuite `0x00A8` unchanged. A peer MUST NOT transcode, validate as text, or truncate an identity.

**The identity rotates for the same reason the advertisement does.** It is sent in the clear in the `ClientHello`, so anything stable in it is a tracking beacon — and Draft 1 put `sid` there, then had a persisted pairing reuse that `sid` on every reconnection for the life of the pairing. A passive observer at two venues would have linked them by a fixed sixteen bytes. That is precisely what [§3.4](#34-resolvable-identifiers) and A7 were built to prevent, reintroduced one layer down and one connection earlier, so the cost of the rotating advertisement was being paid for nothing.

The construction is the one already in the document, keyed the same way and the same 17 octets. It also restores 5.3d's justification: an attacker cannot produce a resolvable identity without `K_id`, so there is no identity to probe the oracle with. Resolving costs one HMAC per held pairing, which A10 already accepted as cheap at this scale.

### 5.4 Resolved: the mechanism

**A measurement ruled out the assumed mechanism; a product decision resolved what to do about it.** Both are recorded here, because the second overrides the stated position of both implementation reviewers and should not be discoverable only from a commit message.

#### 5.4.1 What was measured

The check Draft 2 scheduled was run on the mobile platform's TLS interface: two loopback endpoints, the same 32-byte external PSK installed on both, minimum version forced, negotiated version and ciphersuite read back.

| Attempt | Result |
|---|---|
| Minimum TLS 1.3 — what [5.2a](#52-tls-profile) requires | **Handshake failed** |
| Minimum TLS 1.2 — what the platform does | Negotiated TLS 1.2, ciphersuite `0x00A8` = `TLS_PSK_WITH_AES_128_GCM_SHA256` (RFC 5487) — **plain PSK, no DHE, no forward secrecy** |
| TLS 1.2 with `TLS_ECDHE_PSK_WITH_AES_128_GCM_SHA256` (RFC 8442) requested | Request **silently ignored**; `0x00A8` negotiated |
| TLS 1.2 with `TLS_ECDHE_PSK_WITH_AES_128_CBC_SHA256` (RFC 5489) requested | Request **silently ignored**; `0x00A8` negotiated |

The cause is structural rather than incidental: the platform's ciphersuite enumeration **contains no PSK suites at all**, so the suite it actually negotiates cannot be named by the public interface — neither requested nor excluded.

Against [5.2h](#52-tls-profile)'s three properties: mutual authentication is achieved, **forward secrecy is not obtainable in any TLS version through this interface**, and property 3 is achievable either way.

- **(5.4a)** The check ran on the desktop variant of the same frameworks, which carry identical availability annotations and the same ciphersuite enumeration on both platforms. **Confirmation on the mobile device itself is outstanding** and is an afternoon's work. A decision of this size should not turn on a platform difference nobody expected.
- **(5.4b) MUST** The measurement is **repeated on the mobile device** before an implementation ships a pairing that relies on [§5.4.3](#543-the-decision). If TLS 1.3 with an external PSK proves reachable there, property 2 is obtained on that leg, [5.2b1](#52-tls-profile) already requires it to be used, and the relaxation simply never applies — **no clause changes on a favourable result.**

5.4b restores a gate Draft 4 removed. The confirmation was demoted to *"still worth having, but no longer gates anything"*, which was wrong: it gates the **premise**. Route D was chosen because forward secrecy is not obtainable through this interface *in any TLS version* — a finding from a proxy. Shared availability annotations and a shared enumeration are good evidence and they are not the measurement, and the asymmetry that made this dangerous in the first place still holds: the host library has supported external PSK for years, so nothing on that end will ever reveal a difference.

#### 5.4.2 What the routes were

Four, and they were weighed against [5.2h](#52-tls-profile)'s three properties rather than against convenience.

| | Route | Gets all three? | Principal cost |
|---|---|---|---|
| **A** | Embed a TLS library on the platform that cannot do it natively | Yes | Binary size; a changed export-compliance declaration; a patching obligation on an app-review-gated cadence |
| **B** | A Noise handshake over the raw socket | Yes | Neither end uses platform TLS; `§5` becomes a Noise profile, needing its own review pass |
| **C** | An application-layer ephemeral key over plain PSK | Payload only | A hybrid; control traffic stays retrospectively decryptable |
| **D** | Accept plain PSK where that is all the platform offers | No — property 2 is lost | None to the implementations. The cost is the property itself |

#### 5.4.3 The decision

**Route D. Taken by the protocol owner on 22 August 2026, on the grounds that the data carried is not highly sensitive.** Forward secrecy moves from *required* to *best-effort* ([5.2h](#52-tls-profile)).

**What is given up, stated plainly.** An attacker who records a session on a shared network — a range, a public venue — and **later** obtains that session's pairing secret can decrypt the recording retrospectively. They obtain it by photographing the code, or from a peer's storage. Single use ([7.3a](#73-single-use-and-expiry)) stops a photographed code being *used* to pair; it does not stop it decrypting a recording of the session it created.

**What is not given up.** The channel remains encrypted and mutually authenticated. No unpaired peer receives anything ([§7.1](#71-threat-model)). Nothing stable crosses in the clear ([5.3e](#53-psk-identity)). [5.2f](#52-tls-profile) — never fall back to an unencrypted connection, under any circumstances including a user instruction — is untouched. **Only property 2 changed.**

**The judgement, and its edge.** Swing video of a golfer is not sensitive material, and that is a reasonable basis for the trade. The part of the payload that carries a privacy dimension is not the video but the **candidate-attached audio windows**: they are short and centred on transients, but retention attaches to candidates rather than shots, so they capture events that were *not* shots — an adjacent player, a conversation — and [`PPCP-CORE` §13c](ppcp-core.md#13-privacy-considerations) says plainly that their count is not bounded by anything the user does. In the lesson use case that is a coach and a pupil talking. The decision is the owner's to make; this is the part of the payload it should be made about.

**Both reviewers had taken the opposite position**, and the record should show it rather than quietly lose it:

> *"this is the one clause in the document I would refuse to relax under schedule pressure"* — host review, on 5.2b
>
> *"Dropping forward secrecy is not available. §5.2h says so, A6 says why, and the reasoning is not weakened by the platform being awkward."* — mobile review, after running the check

Neither was overruled by schedule pressure, which is what both were guarding against. The relaxation rests on a judgement about the data — a judgement that is the product owner's and not a reviewer's — and it should go back to both teams as a decision taken rather than a question reopened.

**What now carries more weight, because it is what is left.**

- **(5.4f) MUST** A peer offers the strongest mode its platform supports ([5.2b1](#52-tls-profile)). Where both ends can reach TLS 1.3, forward secrecy is obtained and nothing has been lost; the relaxation applies only to the leg that cannot.
- **(5.4g) MUST** Single use and publisher-side expiry ([§7.3](#73-single-use-and-expiry)) are now the **primary** defence around the pairing secret rather than a secondary one, because the secret's later disclosure is no longer contained by the key exchange.
- **(5.4h) SHOULD** A peer erase a session's derived key material at session close unless the pairing is persisted ([7.2d](#72-handling-the-pairing-secret)), and a persisted pairing remains visible and revocable ([7.4b](#74-persistent-pairings)). Key material that no longer exists cannot be disclosed later.
- **(5.4i)** Where a deployment does regard its payload as sensitive — a different product on this protocol, or this one after a reassessment — the answer is Route A or B, not a variation of D. [§5.2h](#52-tls-profile) still states the property, and [§5.4.2](#542-what-the-routes-were) still names what obtains it.
- **(5.4j) SHOULD** A peer does not transfer **candidate-attached audio payload** over a connection that did not achieve forward secrecy, unless the user has been told and has agreed. The Capture is announced as normal — its metadata and its absence stay assertable (I10) — and its payload is withheld, or deferred to a connection that did. The rest of the session is unaffected.
- **(5.4k) MUST** A peer makes the **achieved TLS version and key-exchange mode** available to its application layer, and records both in its diagnostic export. [7.2b](#72-handling-the-pairing-secret) forbids that export carrying keys or payloads; the negotiated mode is neither.

**Why 5.4j exists, and how to remove it.** [§5.4.3](#543-the-decision) argues the relaxation on swing video and then identifies a different part of the payload as the one carrying a privacy dimension — the candidate-attached audio windows, whose count is *"not bounded by anything the user does"* ([`PPCP-CORE` §13c](ppcp-core.md#13-privacy-considerations)), and which in the lesson case is a coach and a pupil talking. The trade was argued on video; applied uniformly, it is paid for by audio.

5.4j applies the decision's own reasoning to the payload that reasoning named, and it costs a session nothing: the diagnostic value of candidate audio is entirely after the fact, so nothing in the live path needs it. It is a **SHOULD with an explicit escape**, not a prohibition.

**If the owner's judgement is that the audio is covered too, that is a legitimate answer and this clause should be deleted rather than worked around** — because [§5.4.3](#543-the-decision) would otherwise read as having named an exception and not taken it, and the next reader will assume it was an oversight.

5.4k is what 5.4j and 5.4i both need. Forward secrecy is now a **per-connection outcome** rather than a property of the protocol, and nothing previously required a peer to know which it got — so a deployment could not apply a policy to it, and a peer telling a user "this connection is encrypted" would be saying something true and not the whole of it.

**This decision is reversible and the reversal is bounded.** Only `§5` changes under any route; discovery, the pairing code including the part that cannot be corrected after printing, network join and the security model are all independent of the mechanism, and [§5.3](#53-psk-identity)'s resolvable identity survives as a pre-handshake selector under a non-TLS one.

---

## 6. RV-4 — Network join

*Optional. Not required for conformance, and the difference between working and not working at a range.*

A publisher that provides its own network may carry the credentials in the pairing code, so the user is not asked to leave the application, find a network name and type a passphrase.

| Key in `wifi` | Type | Card. | Meaning |
|---|---|---|---|
| `s` | tstr | 1 | Network name. |
| `k` | tstr | 0..1 | Passphrase. Absent means an open network. |
| `h` | bool | 0..1 | Hidden network. Default false. |

- **(6a) MUST** A peer joins only through a platform interface that obtains the user's consent for the specific network. It MUST NOT reconfigure networking silently.
- **(6b) MUST** A peer that joins for a pairing restores the prior network configuration when the session ends, **or** leaves the join in the user's control. It MUST NOT leave the device attached to a network the user did not choose to keep. On platforms where an application may remove its own network configuration but cannot reassociate a previously-used network — reassociation being the system's decision — only the second branch is available, and that is conformant. The disjunction is there for exactly that reason and the first branch is not the expected behaviour.
- **(6e) MUST** Where a code carries `wifi`, the join is attempted **before** the endpoint walk unless the peer is already associated with that network ([§4.3f](#43-payload)).
- **(6c) MUST** A code carrying `wifi` is treated as a network credential in every respect — the handling rules of [§4.4c](#44-handling-a-scanned-code) and [§7.2](#72-handling-the-pairing-secret) apply to the whole payload, not only to `psk`.
- **(6d) SHOULD** A publisher prefer a 5 GHz network on a channel it controls. Shared infrastructure at a public venue is heavy-tailed in latency, which directly degrades clock synchronisation ([`PPCP-CORE` §3.2](ppcp-core.md#32-transport-guidance)).

**A consequence worth stating.** Once network credentials are in the code, a photograph of the code is a photograph of the network passphrase. That raises the stakes on [§7.3](#73-single-use-and-expiry) considerably, and it is a good reason for a publisher to use a network provisioned for this purpose rather than the venue's own.

---

## 7. RV-5 — Security model

This section is what [`PPCP-CORE` §12](ppcp-core.md#12-security-considerations) delegates. Until it is agreed, that delegation points at nothing.

### 7.1 Threat model

**Defended against:**

| Threat | By |
|---|---|
| An unpaired peer on the same network receiving capture payload | Mutual authentication from a secret that only reaches the counterpart by being scanned ([§5.2](#52-tls-profile)) |
| Passive interception on a shared or hostile network | TLS 1.3 |
| Retrospective decryption after the pairing secret leaks | **Only where both peers reach TLS 1.3 or a TLS 1.2 ECDHE_PSK suite** ([§5.2b](#52-tls-profile)). Otherwise **not defended** — see below. |
| Tracking a device across venues by its advertisement, or by its first TLS flight | Rotating resolvable identifiers, in the advertisement ([§3.4](#34-resolvable-identifiers)) **and in the PSK identity** ([§5.3](#53-psk-identity)) — both halves are needed, and Draft 1 had only the first |
| A pairing code photographed and reused later | Single use and expiry ([§7.3](#73-single-use-and-expiry)) |
| A stale code reaching a newer peer and being half-understood | Version marker and its reporting obligation ([§4.2](#42-version-handling)) |

**Not defended against, and stated so nobody assumes otherwise:**

| Threat | Why not |
|---|---|
| Someone who can see the code at the moment it is displayed | It is a shared secret shown on a screen. This is the model. Physical control of the display is the control. |
| A compromised peer at either end | Out of scope for any rendezvous protocol. |
| Traffic analysis | Payload sizes and timing reveal that capture is happening and roughly when. Not addressed. |
| Denial of service | An attacker on the link can disrupt multicast or the transport. The fallbacks in [§3.6](#36-multicast-is-not-to-be-relied-on) reduce the impact; nothing prevents it. |
| **Impersonation between peers that scanned the same multi-use code** | They hold **identical key material** by construction ([§7.4f](#74-persistent-pairings)). `mu: 1` is the pairwise case; `mu > 1` is a group credential and must be read as one. |
| **Retrospective decryption of a recorded session, where the pairing secret is later obtained and the peers could not reach an ephemeral key exchange** | A deliberate trade, taken on the sensitivity of the payload ([§5.4.3](#543-the-decision)). Single use and publisher-side expiry ([§7.3](#73-single-use-and-expiry)) reduce the window in which a secret is obtainable; they do not close it. |
| Anything after the byte stream exists | PPCP's problem, and PPCP assumes the stream is authenticated ([§1.3c](#13-where-it-stops)). |

### 7.2 Handling the pairing secret

- **(7.2a) MUST** `psk` is at least 128 bits from a cryptographically secure random number generator. A predictable secret defeats the entire model, and it is the single easiest thing to get wrong.
- **(7.2b) MUST NOT** A pairing secret, a derived key, or a decoded payload appear in a log, a crash report, an analytics event, or a **diagnostic export**.
- **(7.2c) MUST** Secrets at rest are held in the platform's protected storage where one exists.
- **(7.2d) MUST** A peer erases a pairing's key material when the pairing is revoked or the session it belongs to closes, unless the pairing was persisted under [§7.4](#74-persistent-pairings).

7.2b names diagnostic export explicitly because a user-initiated diagnostic bundle is a first-class output of a PPCP implementation, it is attached to public issue trackers, and it is assembled by code whose author is thinking about clock residuals rather than about secrets.

### 7.3 Single use and expiry

- **(7.3a) MUST** A publisher invalidates a pairing code once `mu` handshakes have completed with it. The default is one.
- **(7.3b) MUST** A publisher invalidates the code when the session it belongs to closes, whether or not it was used.
- **(7.3c) SHOULD** A code carries `exp`, and a publisher chooses the shortest expiry the workflow tolerates.
- **(7.3d) MUST** A publisher generates fresh `psk` and `sid` for every code. A code is never regenerated with the same secret.
- **(7.3e) MUST** A publisher **refuses a handshake** for a code past its `exp`. Expiry is enforced by the party holding the authoritative clock, not by the party reading a printed number — which is what lets [4.4a1](#44-handling-a-scanned-code) permit a peer with an untrustworthy clock to attempt the pairing rather than be locked out.

7.3a and 7.3b are clock-free and are the primary defence; `exp` depends on two wall clocks agreeing and is therefore secondary rather than relied upon. `mu` exists because pairing several devices from one displayed code is a real workflow, and the alternative — a code that is silently reusable forever — is worse than one that says how many times it may be used.

### 7.4 Persistent pairings

- **(7.4a) MAY** Both peers persist `PRK` after a successful pairing, so a later session can be established without displaying a new code. This is what makes the discovery path of [§3](#3-rv-1--service-discovery) useful.
- **(7.4b) MUST** Persistence is opt-in, visible to the user, and individually revocable.
- **(7.4c) MUST** A persisted pairing is scoped to the counterpart peer identity learned inside the authenticated channel. It is not transferable.
- **(7.4d) MUST** Revocation on either side is honoured immediately by that side, and results in a failed handshake for the other.
- **(7.4e) MUST** A new session established from a persisted pairing derives a fresh `sid` inside the authenticated channel. **The original session's identifier is not reused for anything.** After the initial derivation of [§5.1](#51-key-derivation), `sid` survives only as the HKDF salt baked into `PRK`; it is never transmitted again, by either peer, on any connection.

7.4e's tail clause said the opposite until Draft 3 — that the original `sid` was reused *for the PSK identity* — which is what [5.3e](#53-psk-identity) now forbids, and its cross-reference pointed at a clause that had moved. It is the [§5.3](#53-psk-identity) fix not carried into the section an implementer is reading when they build persistence, and a security document that says transmit two sections after forbidding it is resolved by whichever section is read second.

- **(7.4f) MUST NOT** A peer persist `PRK` derived from a pairing code whose `mu` exceeded 1. **A pairing established from a multi-use code is session-scoped**, because its key material is held by every peer that scanned that code.
- **(7.4g)** Where a persistent pairing from a multi-use code is wanted, the peers derive a fresh **per-peer** secret inside the authenticated channel and persist that. Specifying that exchange is deferred; until it exists, multi-device pairing is per-session.

**Why `mu > 1` is a group credential and not three pairings.** Every peer that scans one code derives the same `PRK`, therefore the same `K_tls` and the same `K_id`, from the same `psk` and `sid`. With `mu: 3` the three devices hold **identical key material**: any one can complete a handshake indistinguishable from another's, and can present a different `Peer.id` in `hello` while doing it. 7.4c's scoping to a counterpart peer identity is a *policy* statement, not a cryptographic one, and nothing enforces it.

[§7.1](#71-threat-model) claims the model defends against an unpaired peer receiving capture payload, on the strength of a secret that only reaches the counterpart by being scanned. With `mu > 1` that secret reached three counterparts, so *paired* names a group and mutual authentication proves group membership rather than identity. 7.4f bounds the consequence to the session that created it; `mu` survives because displaying three codes is worse ergonomics for no gain over proper per-peer re-keying.

**The exposure of a persisted pairing is real and should be weighed rather than assumed away**: possession of the device's storage is possession of continuing access. That is why 7.4b requires it to be visible and revocable, and why it is optional rather than automatic.

### 7.5 Reconnecting within a session

[`PPCP-MSG` §4.3](ppcp-messages.md#43-session_resume) defines `session_resume` and says nothing about whether re-authentication is required. That silence was a hole. It is closed here:

- **(7.5a) MUST** A reconnecting peer completes a full handshake ([§5.2](#52-tls-profile)) on the new connection, using the same derived `K_tls`. It does not require a new pairing code.
- **(7.5b) MUST** `session_resume` is accepted only on a connection that completed that handshake, and only for the `sid` bound to it.
- **(7.5c) MUST NOT** A peer accept `session_resume` for a session whose pairing has been invalidated under [§7.3](#73-single-use-and-expiry).
- **(7.5d)** TLS session resumption tickets MAY be used to shorten the handshake. They do not replace it, and a peer MUST NOT accept application data on an early-data path.

7.5d matters because TLS 1.3 early data is replayable by design. A resumed connection that accepted `arm` — or a capture request — as early data would accept a replay of it.

### 7.6 Peer identity

[`PPCP-CORE` §5.2.1](ppcp-core.md#521-peer-identity) requires `Peer.id` to be a generated, persistent identifier that is not a platform device identifier, and leaves its exposure during rendezvous to this document.

- **(7.6a) MUST NOT** `Peer.id` appear in a TXT record, an instance name, a PSK identity, or anywhere else outside an authenticated channel.
- **(7.6b) MUST** `Peer.id` is first disclosed in `hello`, inside TLS.
- **(7.6c)** Consequence: a peer is recognised before connection only by a resolvable identifier ([§3.4](#34-resolvable-identifiers)), which requires a pairing to resolve. A stranger learns that a PPCP peer exists and nothing about which one.

### 7.7 What must never cross an unauthenticated channel

- **(7.7a) MUST NOT** Capture payload, a Candidate, a Shot, a declaration, or any other PPCP message be sent before the handshake completes ([§1.3c](#13-where-it-stops)).
- **(7.7b) MUST NOT** A peer disclose its Sources, profiles, calibration or stored sessions to an unauthenticated counterpart, by any means including an error message.
- **(7.7c) MUST** Rejection of an unauthenticated counterpart is uniform. A peer does not distinguish, in what it returns or in how long it takes, between an unknown identity and a wrong key.

---

## 8. Operational notes

*Not normative. Failure modes that are cheap to accommodate and expensive to diagnose.*

**Local network permission.** On platforms that gate access to the local network behind a user prompt, there is typically no public interface to read the current permission state, and a single refusal makes an application appear permanently broken with no obvious cause. Detect the symptom — discovery returning nothing and direct connection to a private address failing — and explain it. A direct connection from a pairing code degrades more gracefully than multicast browsing, which is a further reason the code path is primary.

**Peer-to-peer radios share the antenna.** On platforms where a peer-to-peer wireless technology shares the radio with infrastructure networking, using both degrades each. Prefer one.

**Power saving injects latency.** Wireless power-save can add tens of milliseconds, which lands directly in the clock-synchronisation estimator as a heavier left tail.

**The pre-shared-key interfaces in common toolkits are a trap, and both ends hit it.** Several widely-used TLS wrappers expose PSKs through a TLS 1.2-era interface — identity *hints*, RFC 4279 ciphersuite selection — which does not reach TLS 1.3 external PSKs at all. A desktop toolkit's socket-level PSK callback is one example; a mobile framework's PSK entry point sitting beside hint-based APIs is another ([Annex B8](#annex-b--open-issues)). An implementation generally has to use the underlying library's **external-PSK session callbacks** directly, installing the key as a synthetic session with the correct cipher and the hash of [5.2c](#52-tls-profile) bound to it. **Getting the hash wrong produces a handshake failure with no useful diagnostic**, indistinguishable from a key mismatch, so check it first when a handshake fails for no apparent reason.

**A wired tunnel is the best available option** where the peers are co-located: the latency floor is tighter and far more stable, which is what makes minimum-round-trip filtering converge quickly. It needs no rendezvous at all — it is the direct path of [§2](#2-rendezvous-paths).

---

## 9. Conformance

- **(9a)** Implementing `PPCP-RV` is OPTIONAL. A peer connecting only over a tunnel, or handed an established socket, is fully PPCP-conformant with no rendezvous implementation.
- **(9b) MUST** An implementation claiming `PPCP-RV` conformance implements the pairing-code path ([§4](#4-rv-2--the-pairing-code)), the key derivation and TLS profile ([§5](#5-rv-3--key-derivation-and-tls)), and [§7](#7-rv-5--security-model) in full.
- **(9c) MUST** It reproduces the test vectors of [§10](#10-test-vectors) exactly.
- **(9d)** Service discovery ([§3](#3-rv-1--service-discovery)) and network join ([§6](#6-rv-4--network-join)) are independently optional, and an implementation states which it provides.

Required tests, to be folded into [`PPCP-CONF`](ppcp-conformance.md) once this document is agreed. **Method** uses the vocabulary of [`PPCP-CONF` §1](ppcp-conformance.md#1-claiming-conformance), with one addition: **review** means the requirement is not observable from outside the implementation and is verified by reading the code.

| Test | Method | Asserts |
|---|---|---|
| **RT-1** | static | The derivation vectors of [§10.1](#101-key-derivation) reproduce byte-for-byte. |
| **RT-2** | static | Both pairing codes of [§10.3](#103-pairing-code) encode and decode byte-for-byte. **The all-fields payload — carrying `dn`, `mu`, `exp` and `wifi` — still encodes `v` as its first key**, which the minimal one cannot demonstrate. `Session.id` derives from `sid` as canonical lowercase UUID text ([4.3e](#43-payload)). |
| **RT-3** | injected | A `v` the implementation does not know produces a *version* report, not a generic failure (4.2b). |
| **RT-4** | injected | **A handshake negotiating a weaker mode than both peers can reach is refused** (5.2b1), and no handshake is unencrypted (5.2f). Where both peers reach TLS 1.3, assert `psk_ke` is refused (5.2b). Where one cannot, assert the negotiated result is the **strongest the pair can reach**, and that the outcome is surfaced (5.4k). Demonstrated against an instrumented counterpart or a wire capture, never by an API assertion ([5.2i](#52-tls-profile)). |
| **RT-5** | paired | A second handshake with a `mu: 1` code is refused (7.3a). |
| **RT-6** | injected | An expired code is reported as expired, with no connection attempted, **by a peer whose wall clock it has reason to trust** (4.4a). A peer exercising 4.4a1 is covered by RT-15 instead, and MUST NOT fail this one for attempting. |
| **RT-7** | paired | A TXT record contains no `Peer.id`, no device name and no session count; the instance name carries no persistent value (3.3b, 3.2b). |
| **RT-8** | paired | `rid` changes across re-registration and resolves under the correct `K_id` only (3.4a, 3.4b). |
| **RT-9** | paired | A diagnostic export produced immediately after a pairing contains no secret and no payload (7.2b, 4.4c). |
| **RT-10** | injected | `session_resume` is refused on a connection that did not complete the handshake (7.5b). |
| **RT-11** | injected | Rejection of an unresolvable identity and of a wrong key are indistinguishable in content, and in timing where [5.3d](#53-psk-identity) is met (7.7c). |
| **RT-12** | **review** | Secrets come from a platform CSPRNG at full width, are held in protected storage where one exists, and are erased on revocation or session close (7.2a, 7.2c, 7.2d). |
| **RT-13** | **review** | A network join obtains the user's consent for the specific network and does not leave the device attached to a network the user did not choose to keep (6a, 6b). |
| **RT-14** | static | The PSK identity of [§10.2](#102-resolvable-identifiers) reproduces byte-for-byte, **differs across connections**, resolves under the correct `K_id` only, and contains no `sid` (5.3a, 5.3e). **Where TLS 1.2 is negotiated, the server sends an *empty* `psk_identity_hint`** (5.2h property 3) — an obligation that exists only on the newly-permitted path, which is the path one implementation now always takes. |
| **RT-15** | paired | A publisher refuses a handshake for a code past its `exp` (7.3e), and a peer that cannot trust its clock attempts rather than refuses (4.4a1). |
| **RT-16** | **review** | No `PRK` derived from a code with `mu > 1` is persisted (7.4f). |
| **RT-17** | **review** | The peer offers **every** key-exchange mode and ciphersuite its platform exposes, and the offered set is derived from a platform capability query rather than from a constant (5.2b1, 5.4f). **Re-read whenever the TLS setup path is touched, and whenever a platform SDK is updated** — a mode that becomes available is a mode the peer must begin offering, and a platform that gains TLS 1.3 external PSK silently restores property 2 for an implementation that asks rather than assumes. |

**Three of these cannot be tested from outside**, and that is worth stating rather than leaving to be discovered. Entropy quality and storage protection produce no observable difference on the wire — a peer using a predictable secret completes exactly the same handshake as one using a good secret — so **RT-12 is the requirement on which the whole model rests and the one no test can catch.** It has to be read in the code, and it should be read again whenever the key-generation path is touched.

RT-17 joins RT-12 in that set for a specific reason: 5.2b1 is the clause the relaxation made load-bearing, and it states its own untestability — an observer cannot distinguish a peer that offered less than it had from one that had less to offer. The requirement now carrying property 2 is the one nothing external can check.

RT-9 and RT-11 are the two most likely to be skipped among those that *can* be tested, and the two least likely to surface in use.

---

## 10. Test vectors

All values hexadecimal unless stated. Info strings are ASCII with no terminator.

### 10.1 Key derivation

```
sid    3f2504e04f8941d39a0c0305e82c3301
       as PPCP Session.id:  3f2504e0-4f89-41d3-9a0c-0305e82c3301   (4.3e)
psk    000102030405060708090a0b0c0d0e0f

PRK    = HKDF-Extract(salt = sid, IKM = psk)
       d8a961d30def2e84bd930aa64fe8c9583286281ae0f61baa0116a8220bf6bcf9

K_tls  = HKDF-Expand(PRK, "ppcp1 tls-psk", 32)
       2b0c55242ac1075eef80f548a7b39976b1cc2b88fbb6d609e5f3cd20f36d7fd4

K_id   = HKDF-Expand(PRK, "ppcp1 rendezvous-id", 32)
       fd2d8fcfb1be76f83ca1d551e8d5ab34a2fbe3a76f048acb09c64c1d20646117

```

### 10.2 Resolvable identifiers

Both are keyed by `K_id` and both rotate. Neither carries `sid`.

```
advertisement (§3.4)
  rn     a1b2c3d4e5f60718
  rid    = HMAC-SHA256(K_id, "ppcp1 rid" || rn)[0..7]
         9b1d2df94b2cfa84

  TXT    txtvers=1  pv=1.0  role=capture
         rn=a1b2c3d4e5f60718  rid=9b1d2df94b2cfa84

  instance name   PPCP-9B1D2DF9

PSK identity (§5.3), fresh per connection
  rn2    0f1e2d3c4b5a6978
  tag    = HMAC-SHA256(K_id, "ppcp1 psk-id" || rn2)[0..7]
         b355ada60b4b5aa8

  identity = 0x01 || rn2 || tag
         010f1e2d3c4b5a6978b355ada60b4b5aa8              (17 octets)
```

### 10.3 Pairing code

Payload: `v = 1`, one endpoint `192.168.1.20:7788`, `mu = 1`, the `psk` and `sid` above.

```
CBOR, deterministic encoding, 75 octets:

a5                                      map(5)
  61 76                                 "v"
  01                                    1
  62 65 70                              "ep"
  81                                    array(1)
    a2                                  map(2)
      61 68                             "h"
      6c 31 39 32 2e 31 36 38 2e 31 2e 32 30    "192.168.1.20"
      61 70                             "p"
      19 1e 6c                          7788
  62 6d 75                              "mu"
  01                                    1
  63 70 73 6b                           "psk"
  50 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f
  63 73 69 64                           "sid"
  50 3f 25 04 e0 4f 89 41 d3 9a 0c 03 05 e8 2c 33 01
```

URI, 105 characters:

```
ppcp:pWF2AWJlcIGiYWhsMTkyLjE2OC4xLjIwYXAZHmxibXUBY3Bza1AAAQIDBAUGBwgJCgsMDQ4PY3NpZFA_JQTgT4lB05oMAwXoLDMB
```

Deterministic encoding orders these keys `v`, `ep`, `mu`, `psk`, `sid`.

#### Every optional field

The vector that matters for [4.3b](#43-payload), because the minimal one cannot demonstrate it. `dn = "Bay 3"`, `exp = 1787832000`, and a `wifi` block.

```
CBOR, deterministic encoding, 133 octets:

a8                                      map(8)
  61 76  01                             "v"    1
  62 64 6e  65 42 61 79 20 33           "dn"   "Bay 3"
  62 65 70  81 a2                       "ep"   [ {
    61 68 6c 31 39 32 2e 31 36 38 2e 31 2e 32 30    "h" "192.168.1.20"
    61 70 19 1e 6c                                  "p" 7788
                                              } ]
  62 6d 75  01                          "mu"   1
  63 65 78 70  1a 6a 90 26 c0           "exp"  1787832000
  63 70 73 6b  50 <16 bytes psk>        "psk"
  63 73 69 64  50 <16 bytes sid>        "sid"
  64 77 69 66 69  a3                    "wifi" {
    61 68 f4                                        "h" false
    61 6b 6c 63 6f 72 72 65 63 74 68 6f 72 73 65    "k" "correcthorse"
    61 73 6d 50 69 6e 50 6f 69 6e 74 2d 42 61 79 33 "s" "PinPoint-Bay3"
                                              }
```

URI, 183 characters:

```
ppcp:qGF2AWJkbmVCYXkgM2JlcIGiYWhsMTkyLjE2OC4xLjIwYXAZHmxibXUBY2V4cBpqkCbAY3Bza1AAAQIDBAUGBwgJCgsMDQ4PY3NpZFA_JQTgT4lB05oMAwXoLDMBZHdpZmmjYWj0YWtsY29ycmVjdGhvcnNlYXNtUGluUG9pbnQtQmF5Mw
```

The first four octets are `a8 61 76 01` — `map(8)`, `"v"`, `1`. **With `n` in place of `dn` they would have been `a8 61 6e …`**, and a parser reading the first key to find the version would have read a display name. Note also that this payload is 133 octets against the 400-byte guidance of [4.5a](#45-size), so a code carrying network credentials stays comfortably scannable.

---

## Annex A — Decisions and alternatives

*Non-normative. Every one of these is cheap to change now.*

| # | Decision | Alternative considered | Why |
|---|---|---|---|
| **A1** | **Ratify `_ppcp._tcp`** | Choose a fresh name now that the question is being asked properly | The name has already shipped in an application bundle. Changing it costs a release on one side and a silent no-discovery failure in between, and buys nothing: the string is unregistered, unambiguous and already correct. |
| **A2** | **A stable `ppcp:` scheme with the version inside the payload** | `ppcp1:`, `ppcp2:` — version in the scheme, so a parser rejects before decoding | A scheme an application has not registered is never delivered to it, so the user sees nothing happen rather than a message saying the code is too new. The version marker exists for the user, not the parser. |
| **A3** | **A custom scheme, not an `https` link** | An `https` universal link, which would also let an uninstalled application be found in a store | The payload carries a secret. If the application is absent the operating system opens the URL in a browser, sending the secret to a web server and into history. The store-discovery benefit is real and belongs beside the code as separate text. |
| **A4** | **CBOR payload** | A packed binary struct, or query parameters | The library already carries a CBOR codec for [`PPCP-ENC`](ppcp-encoding.md), so the code costs no new parser, and unknown-key tolerance matches the extension model PPCP already has. A packed struct would be ~20 bytes smaller and unextendable. |
| **A5** | **Derive `K_tls` and `K_id` rather than use `psk` directly** | Use the scanned secret as the TLS PSK, and again as the identifier key | Domain separation. The identifier is published in the clear on a multicast network; deriving it from a separate key means that publication reveals nothing about the key that completes a handshake. |
| **A6** | **`psk_dhe_ke` mandatory** | Permit `psk_ke`, which is simpler and one round trip cheaper | Forward secrecy. Without it, anyone who captured a session and later obtains the secret decrypts it retrospectively. This is the requirement most likely to be dropped for simplicity and the one most expensive to add back. **Upheld against the platform, then relaxed on the data.** Both reviewers independently said this was the one clause they would refuse to relax, and neither was overruled by schedule pressure — which is what they were guarding against. It was relaxed to *best-effort* by a product judgement that the payload is not highly sensitive ([§5.4.3](#543-the-decision)). The property, and what obtains it, are still stated: a deployment that judges differently reverses the decision without redesigning anything but `§5`. |
| **A7** | **Rotating resolvable identifiers in TXT** | Publish `Peer.id`, which is far simpler | A stable identifier broadcast on every network a golfer visits is a tracking beacon. The resolvable form costs one HMAC per known pairing per discovery. |
| **A8** | **Single use by default, with an explicit `mu`** | Codes reusable until the session ends | A code that is silently reusable forever is the failure a photograph exploits. `mu` keeps the multi-device workflow without making reuse the unstated default. |
| **A9** | **The scanner dials on the code path, the browser dials on the discovery path** | Force one direction, so only one peer needs a listener | A code can only carry the endpoint of the peer displaying it, and discovery is best served by putting the querier role on whichever peer can browse without binding a port that platform responders already own — a SHOULD, not a constraint ([§3.5b](#35-who-advertises-and-who-browses)). The asymmetry is inherent; only the code path is required, so a minimal implementation still needs one direction. |
| **A10** | **`Peer.id` disclosed only inside TLS** | Include it in the PSK identity, so a server can select a key without trying each | The identity is sent in the clear in the first flight. A stable identity there would undo the rotating identifier at the first connection. Trying each held pairing is cheap at the scale involved. |
| **A11** | **One PSK identity form, always resolvable** ([§5.3](#53-psk-identity)) | Keep `0x01 \|\| sid` for a first pairing and use the resolvable form only for a persisted one, which is marginally simpler on the first handshake | Two forms of the same length starting with the same byte need a discriminator, and the saving is one HMAC. One form is simpler than two plus a type rule. The leading `0x01` remains a format version byte for a future third form. |
| **A12** | **Every payload key but `v` is at least two characters** ([4.3b](#43-payload)) | Special-case `v` to be emitted first regardless of deterministic ordering | A special case is a rule an implementer can forget; a length constraint is one the encoder enforces for free, and it keeps working for keys added in later payload versions. |
| **A13** | **`mu > 1` is session-scoped and never persisted** ([7.4f](#74-persistent-pairings)) | Remove `mu` entirely, so every pairing is pairwise | Multi-device pairing is a real workflow and displaying three codes is worse ergonomics for no gain. Bounding the shared credential to one session keeps the workflow and removes the permanent exposure. |

---

## Annex B — Open issues

| # | Issue | Status |
|---|---|---|
| **B13** | **Whether the absence of forward secrecy should be user-visible.** A peer at the TLS 1.2 floor has *every* session without it, not some. 5.4k now makes the outcome readable, so a peer *can* say; nothing says whether it *should*. This is a product question for the implementation teams rather than a protocol one, and it is recorded so it is decided rather than omitted. | Open — not the protocol's to answer. |
| **B1** | **Draft 5 carries three review passes.** [§4](#4-rv-2--the-pairing-code), [§6](#6-rv-4--network-join) and [§7](#7-rv-5--security-model) are approved without reservation by both teams; §4 has survived three passes and three independent recomputations. Every remaining finding has been in **§5**, and every one of them was a consequence of the relaxation not carried into the clause next door. | Open — a fourth pass on [§5](#5-rv-3--key-derivation-and-tls) only. |
| **B2** | **`mu` greater than one has no revocation story**, and the peers that scanned it share key material. The sharing is now bounded — [7.4f](#74-persistent-pairings) forbids persisting such a pairing and [§7.1](#71-threat-model) names the impersonation exposure — but a publisher still cannot withdraw a live multi-use code from the second and third holder. **Per-peer re-keying inside the channel ([7.4g](#74-persistent-pairings)) is the fix and is unspecified.** | Open. Both publishers intend to emit `mu: 1` only until it exists. |
| **B3** | **A peer holding several persisted pairings advertises only one** ([§3.4d](#34-resolvable-identifiers)). Advertising several — as repeated keys, or as several service instances — leaks the count. Rotating through them delays reconnection. Neither is specified. | Open. |
| ~~**B4**~~ | ~~Expiry depends on two wall clocks.~~ | **Closed in Draft 2.** The publisher enforces `exp` ([7.3e](#73-single-use-and-expiry)) because it holds the authoritative clock, and a peer that cannot trust its own attempts rather than refuses ([4.4a1](#44-handling-a-scanned-code)). |
| **B5** | **No pairing-time transport negotiation.** The code carries endpoints and a port, so a publisher offering both a tunnel and a network connection must display a code per transport or list both as endpoints. Whether that is sufficient is untested. | Open. |
| ~~**B6**~~ | ~~The identity is `sid`-bound.~~ | **Closed in Draft 2**, and it was not an aesthetic issue: a persisted pairing broadcast a fixed sixteen bytes in the clear on every reconnection, undoing [§3.4](#34-resolvable-identifiers). The identity is now resolvable and rotates ([§5.3a](#53-psk-identity)). |
| **B8** | **TLS 1.3 external PSK is unreachable on the mobile platform** — measured ([§5.4.1](#541-what-was-measured)) and resolved by relaxing forward secrecy to best-effort ([§5.4.3](#543-the-decision)). **Re-opened narrowly in Draft 5**: the measurement was taken on the desktop variant of the same frameworks, and [5.4b](#541-what-was-measured) now requires it repeated on the device before an implementation ships a pairing that relies on the relaxation. It gates the premise, not the decision, and a favourable result changes no clause. | Open until the device measurement exists. |
| **B12** | **Forward secrecy is now best-effort, and nothing replaces it on the leg that lacks it.** A per-session **ratchet** — re-deriving `PRK` at each session close and erasing its predecessor — would restore the property for every session after the first, at the cost of persistent state that both ends must keep in step and recover from when they fall out of it. It is not specified, and it is the cheapest route back to property 2 without changing the transport. | Open — worth revisiting if the payload is ever reassessed, or if a peer wants it independently. |
| **B9** | **`role` in a TXT record is unverified before pairing.** A peer advertising `role: host` is taken at its word by a browser deciding whether to dial. It costs only a wasted connection — the handshake authenticates — but a browser should not treat it as more than a filter hint. | Open. |
| **B7** | **Interoperability is untestable until a second implementation exists.** Every test in [§9](#9-conformance) can pass against a single implementation's own assumptions, which is exactly the failure mode [`PPCP-CONF` §5c](ppcp-conformance.md#5-interoperability) records for PPCP itself. | Open — structural. |

---

# Annex C — Change history

*Non-normative. Newest first.*

**Draft 5** carries the third-pass findings. All six were in [§5](#5-rv-3--key-derivation-and-tls), and all six were consequences of Draft 4's relaxation that had not been carried into the clauses around it: a conformance test still refusing the newly-legal handshake, the clause that became load-bearing acquiring no test, the achieved outcome becoming a per-connection variable that nothing reported, and a server-sent field becoming reachable on a path nothing exercised. [§4](#4-rv-2--the-pairing-code), [§6](#6-rv-4--network-join) and [§7](#7-rv-5--security-model) are approved without reservation by both teams.

**Draft 4** resolved the one thing Draft 3 left blocked. The platform check was run and failed: TLS 1.3 with an external pre-shared key is not reachable through the mobile platform's interface, and neither is the TLS 1.2 ECDHE_PSK fallback — plain PSK is all it offers ([§5.4.1](#541-what-was-measured)).

The protocol owner has taken the decision, on the grounds that the data carried is not highly sensitive: **forward secrecy becomes best-effort rather than required.** [§5.4.3](#543-the-decision) records it in full, including what was given up, what both reviewers said about it, and what now carries more weight as a result.

**Only forward secrecy is relaxed.** The channel is still encrypted and still mutually authenticated; an unpaired peer still receives nothing; nothing stable still crosses in the clear. Two of the three properties of [5.2h](#52-tls-profile) are unchanged, and [5.2f](#52-tls-profile) — never fall back to an unencrypted connection, under any circumstances including a user instruction — is unchanged and unaffected.

**One item was already decided by shipping.** The mobile application declares `_ppcp._tcp` in its bundle, chosen before this document existed. [§3.1](#31-service-type) ratifies it rather than picking a different name; see [Annex A1](#annex-a--decisions-and-alternatives). Both reviewers endorsed that.
