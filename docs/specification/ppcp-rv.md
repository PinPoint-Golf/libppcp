# PPCP-RV — Rendezvous

**How two PPCP peers find one another, prove they are meant to be talking, and hand PPCP an authenticated byte stream. Includes the security model PPCP itself does not have.**

| | |
|---|---|
| Document | `PPCP-RV` |
| Version | **1.0** |
| Payload version | `ppcp1` |
| Status | **APPROVED for implementation**, 22 August 2026. **Revision 9 adds [RV-6](#11-rv-6--guided-pairing) under [CR-01](../changerequests/CR-01-in-band-pairing.md); both teams reviewed it twice and both accept and close.** Ten findings over two passes, applied as errata E34–E42; **no open findings**. What is not yet demonstrated is [RT-20](#9-conformance), and that has not moved. |
| Date | 22 August 2026 |
| Versioned | Independently of PPCP. Same repository. |
| Relates to | [`PPCP-CORE`](ppcp-core.md) §3 (transport contract), §5.2.1 (peer identity), §12 (security considerations) |
| Reviews | [`reviews/`](reviews/) — first-pass reviews from PinPointCapture and PinPointStudio, dispositioned in [`rv-review-disposition-2026-08-22.md`](rv-review-disposition-2026-08-22.md) |
| Revision | 9 — adds RV-6 (guided pairing), plus errata E34–E42 from two review passes over it. Revision 8 was final; [CR-01](../changerequests/CR-01-in-band-pairing.md) reopened it. |
| Change requests | [`../changerequests/`](../changerequests/) — [CR-01](../changerequests/CR-01-in-band-pairing.md), its [disposition](../changerequests/CR-01-disposition.md), and the responses to the [first](../changerequests/CR-01-review-response.md) and [second](../changerequests/CR-01-review-response-2.md) review passes |
| Conformance | **Implementing `PPCP-RV` is OPTIONAL.** Implementing PPCP is not. |

---

## 0. Status

This is the companion specification that [`PPCP-CORE` §12](ppcp-core.md#12-security-considerations) delegates its security model to. Until it is agreed, **PPCP's security model is not delegated — it is absent**, and no cross-implementation pairing is testable.

**Draft 2** carries the first-pass findings from both implementation teams. Draft 1 asked for [§4](#4-rv-2--the-pairing-code) to get the hardest reading and the least benefit of the doubt, and it needed it: the host reviewer recomputed the deterministic key ordering that §4.3 relies on and found that **`v` was not in fact the first key whenever a display name was present** — a defect invisible in the only worked example, in the one part of the document that cannot be corrected after a code is printed. That is [§4.3b](#43-payload), and it is the argument for putting test vectors in a specification and for exercising them with every optional field rather than none.

This is the companion specification that [`PPCP-CORE` §12](ppcp-core.md#12-security-considerations) delegates its security model to. Until it is agreed, **PPCP's security model is not delegated — it is absent**, and no cross-implementation pairing is testable.

**[§4](#4-rv-2--the-pairing-code) is the part that cannot be corrected later at all.** A pairing code carries an opaque fixed payload that both sides must parse with no chance to negotiate first, and printed codes outlive releases. It has now survived three review passes and three independent recomputations of its vectors — the first pass found a defect in it that was invisible in the worked example, which is why the vectors are in the document and why one of them exercises every optional field.

**[§5.2](#52-tls-profile) is the part still open.** A platform measurement ruled out the assumed mechanism and the owner relaxed forward secrecy to best-effort on the sensitivity of the data carried; [§5.4](#54-resolved-the-mechanism) records the measurement, the decision, what was given up and what both reviewers said about it.

**[§11](#11-rv-6--guided-pairing) is the part that is new.** Revision 8 closed with no open findings and was final. [CR-01](../changerequests/CR-01-in-band-pairing.md) then asked for something the document had never been asked for and does not serve: a first pairing between peers that have never met, without an operator carrying a code between two screens. It is granted in part — the transfer goes, the human does not — and [§11](#11-rv-6--guided-pairing) is the answer.

**It has now had two review passes and it needed both.** Both teams accept the ruling and neither reopened the design. The first pass found six things, two of them blocking and both structural — a version field carried but bound into nothing ([E34](#errata-after-revision-9--change-request-cr-01-and-its-review)), and a rule that serialised one side of the exchange and not the other, where the natural implementation is the one that breaks it ([E35](#errata-after-revision-9--change-request-cr-01-and-its-review)). Neither was visible in the worked vectors and neither would have been fixable after either team shipped.

**The second pass found a trap the first pass's own fix had created.** E34 summarised its binding as a general rule and offered it as the safer thing to hold in mind instead of the formulas — and the rule was untrue of the two clauses directly beneath it, in the direction that yields matching digits, matching MACs and a divergent `PRK` ([E40](#errata-after-revision-9--change-request-cr-01-and-its-review)). **A generalisation offered as a safety aid is a normative statement**, and that one was written in the same erratum that made it false.

**[§4.3b](#43-payload) is why this document has vectors; E34 and E35 are why it has review passes; E40 is why a fix gets reviewed too.**

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
| **RV-6** | Guided pairing: a first pairing with no code, authenticated by a compared short string ([§11](#11-rv-6--guided-pairing)) |

### 1.3 Where it stops

- **(1.3a) MUST** Rendezvous ends when an authenticated, encrypted, bidirectional byte stream exists. Everything after that is PPCP.
- **(1.3b) MUST NOT** Anything in this document change the meaning of any field defined in [`PPCP-CORE`](ppcp-core.md).
- **(1.3c) MUST NOT** Any PPCP message be sent before the handshake of [§5](#5-rv-3--key-derivation-and-tls) completes. `hello` is the first byte of application data on an established, authenticated connection.

- **(1.3c1) MUST** *Erratum E30, 24 August 2026 — CR-01.* The **bootstrap frames of [§11](#11-rv-6--guided-pairing) are not PPCP messages**, and 1.3c does not reach them. They cross a connection of their own that carries nothing else, ends before any pairing exists, and is torn down before [§5](#5-rv-3--key-derivation-and-tls)'s handshake is attempted. A peer MUST NOT send a PPCP message on a bootstrap connection and MUST NOT send a bootstrap frame on a PPCP link ([11.4c](#114-frames)).

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
| **Guided pairing** ([§11](#11-rv-6--guided-pairing)) | the peer that **opens a bootstrap window**, via mDNS or out of band | the peer that **dials the window** | the window opener | OPTIONAL. **First pairing only**, and the only path that establishes one without a code |
| **Direct** | out of band — a tunnel, a cached endpoint, a socket handed in by an embedding application | either | either | OPTIONAL |

- **(2a) MUST** An RV implementation supports the pairing-code path.
- **(2b) MAY** An implementation support the discovery path, the guided-pairing path, the direct path, any combination or none in addition.
- **(2c) MUST** Whichever path is used, the resulting connection completes the handshake of [§5](#5-rv-3--key-derivation-and-tls) before any PPCP message crosses it. There is no unauthenticated **rendezvous** path.
- **(2c1) MUST** *Erratum E4, 23 August 2026.* 2c binds connections this document establishes — the three paths above. It does not reach a connection an embedding established by other means and handed to the PPCP engine, which [9a](#9-conformance) already declares fully PPCP-conformant and which is what [`PPCP-CONF` §2c](ppcp-conformance.md#2-required-test-infrastructure)'s **required** test infrastructure runs over: a simulator that spoke TLS would be testing a TLS stack rather than PPCP. What binds a peer claiming `PPCP-RV` on such a connection is this: no pairing-code key material, no persisted `PRK`, no `PRK`-derived key and no resolvable identifier ([§7.7](#77-what-must-never-cross-an-unauthenticated-channel)) ever crosses it; the peer does not present it to the user as a paired connection; and a **shipping configuration does not offer one** — a harness path is a build-time facility, not a runtime setting. Read without this, 2c and 9a are jointly unsatisfiable for any peer that both claims RV and is testable (F-D9-1).

- **(2f) MUST** *Erratum E30, 24 August 2026 — CR-01.* Guided pairing **establishes** a pairing; it does not carry PPCP. Its bootstrap connection produces a `PRK` and closes, and the peers then connect under [§5](#5-rv-3--key-derivation-and-tls) exactly as they would from a scanned code — which is why [2c](#2-rendezvous-paths) is unweakened rather than excepted, and why the fourth row's *Dials* column describes the **bootstrap** connection only. The [§5](#5-rv-3--key-derivation-and-tls) connection that follows it may run in the **opposite** direction, and on the deployment CR-01 describes it does ([11.2](#112-why-it-is-not-tls-and-what-that-unlocks)).
- **(2e) MUST** The table is written in terms of **what a peer does**, not what role it holds. Nothing here requires a host at either end. Two capture peers pairing directly — the multi-device case — is one displaying a code and the other scanning it, with no host involved.

**Why the two paths dial in opposite directions**, since it looks like an inconsistency:

A code can only carry the endpoint of the peer that displayed it, so on that path the scanner dials. In the ordinary deployment the host has the screen and the capture peer has the camera, which is why it usually reads as host-displays/device-dials — but that is the deployment, not the rule.

For discovery the constraint runs the other way. Whoever **browses** needs only the multicast querier role: it can send from an ephemeral port with the unicast-response bit set and never bind the multicast DNS port, which avoids conflicting with the responder that already owns that port on most desktop platforms and does not exist at all on some. Whoever **advertises** needs a responder. That is why [§3.5b](#35-who-advertises-and-who-browses) recommends the capture peer advertise and the host browse — a mobile platform supplies a responder, several desktop platforms make it awkward, and capture requires the foreground so the capture peer is the one reliably present.

The cost is that a peer supporting both paths implements both a listener and a connector. That is accepted, and it is why only the code path is required.

- **(2d)** Two things follow the dialling direction rather than any peer's role, and both differ between the paths: which peer is the TLS client ([§5.2g](#52-tls-profile)), and which peer sends `hello` rather than `hello_accept`. **The scanner dials and is therefore the initiator; the displayer listens and states its support window in `hello_accept.min_version`.** In the ordinary deployment the displayer is the host, which is the right way round for the old-application/new-host case. On the discovery path it is reversed, and in a peer-to-peer pairing neither is a host. Neither `PPCP-CORE` §10.1 nor this document assumes a direction; both are written in terms of initiator and responder for that reason.

---

## 3. RV-1 — Service discovery

*Optional. Reconnection convenience, and — since [§3.7](#37-the-bootstrap-window) — the way a peer offering a first pairing is found. A first pairing uses [§4](#4-rv-2--the-pairing-code) or [§11](#11-rv-6--guided-pairing); discovery never establishes one by itself.*

### 3.1 Service type

- **(3.1a) MUST** The DNS-SD service type is **`_ppcp._tcp`**.
- **(3.1b) MUST NOT** An implementation advertise or browse any other service type for PPCP rendezvous.

### 3.2 Instance name

- **(3.2a) MUST** The instance name is `PPCP-` followed by the first four bytes of `rid` ([§3.3](#33-txt-record)) in uppercase hexadecimal — for example `PPCP-9B1D2DF9`.
- **(3.2b) MUST NOT** The instance name contain a user-assigned device name, a person's name, a model identifier, or any other value that persists across pairings.

- **(3.2c) MUST** *Erratum E31, 24 August 2026 — CR-01.* A **bootstrap instance** ([§3.7](#37-the-bootstrap-window)) has no `rid` to name itself from. Its instance name is `PPCP-` followed by the eight uppercase hexadecimal characters of `bn` — the 4-byte window identifier of [3.7c](#37-the-bootstrap-window) — and it is therefore indistinguishable in **form** from a reconnection instance, deliberately. [3.2b](#32-instance-name) is unchanged and binds it identically: `bn` is drawn fresh for every window and persists across nothing.

3.2b exists because platform advertising APIs commonly default the service name to the device name, which is frequently a person's name. Publishing that on a driving range's network is a privacy failure that no amount of transport encryption repairs, and it happens by default unless the name is set explicitly.

### 3.3 TXT record

- **(3.3a) MUST** The TXT record carries exactly these keys, and a receiver ignores any key it does not recognise.

| Key | Value | Notes |
|---|---|---|
| `txtvers` | `1` | Record format version. |
| `pv` | e.g. `1.0` or `1.0-1.2` | PPCP wire versions supported, as the **version range** of [3.3d](#33-txt-record). A browser filters on MAJOR **before** connecting. |
| `role` | `host` \| `capture` \| `observer` | The role the peer intends to take. |
| `rn` | 16 hexadecimal characters | **Rotating nonce**, 8 bytes from a CSPRNG. |
| `rid` | 16 hexadecimal characters | **Resolvable identifier**, computed from `rn` — see [§3.4](#34-resolvable-identifiers). |

- **(3.3b) MUST NOT** The record carry `Peer.id`, a device or user name, a serial number, a session identifier, a count of stored sessions, or any capability detail. Capability is declared inside the authenticated channel, where it belongs.
- **(3.3c) SHOULD** The whole record stay under 200 bytes so it fits a single response.
- **(3.3d) MUST** *Erratum E25, 23 August 2026 — a decision, reversible.* A **version range** is written `LOW` or `LOW-HIGH`, where each endpoint is `MAJOR.MINOR` as [`PPCP-CORE` 10.1b](ppcp-core.md#101-version-negotiation) defines it. Both endpoints are **inclusive**, they share a MAJOR, and the range denotes every MINOR between them: `1.0-1.2` is `1.0`, `1.1`, `1.2`. A bare `LOW` is the range `LOW-LOW`. Support across two MAJORs is written as **several ranges separated by a comma**, most preferred first — `2.0-2.1,1.4-1.6`. A reader that cannot parse a range ignores that advertisement rather than guessing.
- **(3.3e) MUST** The same range syntax is used **everywhere this protocol set states a supported range**: `pv` here, and `detail.supported` on `error` / `unsupported_version` ([`PPCP-CORE` 10.1f](ppcp-core.md#101-version-negotiation)). It is **not** used for `hello.versions` ([`PPCP-MSG` 3.1b](ppcp-messages.md#31-hello)), which is an ordered **list** of the exact versions an initiator offers, most preferred first — a different thing, deliberately, because the initiator is choosing rather than describing and the message is not size-constrained the way a TXT record is.

- **(3.3f) MUST** *Erratum E31, 24 August 2026 — CR-01.* A **bootstrap instance** ([§3.7](#37-the-bootstrap-window)) carries a different set, and the two forms are told apart by the presence of `bs`:

| Key | Value | Notes |
|---|---|---|
| `txtvers` | `1` | As above. |
| `pv` | as above | As above, and filtered before connecting for the same reason. |
| `role` | as above | As above, and [B9](#annex-b--open-issues) applies to it identically. |
| `bs` | `1` | **A bootstrap window is open.** Its presence is what identifies the instance as one. |
| `dl` | tstr, at most 32 bytes | **Optional bootstrap label.** Operator-set, **untrusted**, present only while the window is open — see [3.3g](#33-txt-record). |

- **(3.3g) MUST** A bootstrap instance carries **no `rn` and no `rid`**: it names no pairing, because it holds none. [3.3b](#33-txt-record) binds it unchanged in every other respect, with `dl` as the single scoped exception, and `dl` is subject to the rules of [4.4d](#44-handling-a-scanned-code) in full — escaped for display, truncated, and **never** an identifier, a trust signal or a storage key. A peer MUST NOT default `dl` from a device name, a user name or a host name; it is either set by the operator for this window or absent. A receiver that sees both `bs` and `rid` on one instance treats the instance as malformed and ignores it.

`dl` is a privacy trade and is stated as one. [3.2b](#32-instance-name) and [3.3b](#33-txt-record) exist to keep a persistent human-readable string off a venue's network, and `dl` puts one there. It is admitted because the workflow CR-01 was raised for — *"a range operator sets up several bays"* — does not function without it: a browsing peer that sees four open windows and cannot tell which is the bay the operator is standing in has no basis to choose, and choosing wrong is the case [§11.8](#118-what-the-comparison-proves) then has to catch rather than avoid. What bounds the trade is that the string lives only for the window ([3.7d](#37-the-bootstrap-window)), is typed for the venue rather than inherited from the device, and is never any part of what the pairing is keyed on.

3.3d exists because `1.0-1.2` appeared in the `pv` row as an example and was defined nowhere, while [`PPCP-MSG` 3.1b](ppcp-messages.md#31-hello) spelled the same idea as an ordered list and 10.1f called for "the sender's full supported range" without saying how to write one — three expressions of one concept across three documents, each of which an implementer would have invented independently. There are now **two** forms and the boundary between them is stated: a range where a peer *describes* what it supports, a list where it *offers* in preference order (finding F-D7-2, PinPointCapture, session S4; decided by L17).

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
- **(3.4c1) MUST** *Erratum E31, 24 August 2026 — CR-01.* [3.4c](#34-resolvable-identifiers) binds the **reconnection** path — an instance that carries an `rid`. A **bootstrap instance** carries none ([3.3g](#33-txt-record)), so there is nothing to resolve and 3.4c does not reach it. A browsing peer MAY dial such an instance **only** while its own user has asked it to pair, and **only** to run [§11](#11-rv-6--guided-pairing). It MUST NOT dial one opportunistically, in the background, or to enumerate what is present; and a bootstrap connection establishes nothing until [§11.7](#117-the-short-authentication-string)'s comparison has been affirmed at both ends, so an unattended peer that dialled one anyway would obtain a pairing with nobody.
- **(3.4d)** A peer holding several pairings advertises the one it is offering to reconnect. Advertising several simultaneously is not specified — see [Annex B3](#annex-b--open-issues).
- **(3.4d1) MUST** *Erratum E27, 23 August 2026 — a decision, reversible.* A peer holding several pairings advertises **exactly one service instance at a time**, and **rotates which pairing it advertises on the `rn` rotation of [3.4a](#34-resolvable-identifiers)** — a fresh registration at least every 15 minutes, selecting the next pairing in a stable order. One instance is what keeps the count of held pairings unobservable, which is the property 3.4e is about; rotation is what bounds the wait for the *right* counterpart to recognise it, to the rotation period times the number of pairings held. A peer SHOULD advertise a **recently used** pairing first, because the counterpart a user is standing in front of is usually the one they used last.
- **(3.4d2) SHOULD** A peer holding several pairings **browse as well as advertise**, and prefer what it discovers. Browsing resolves *every* held pairing against every advertisement it sees ([3.4b](#34-resolvable-identifiers)) in one pass, with no rotation and no waiting, so a peer that can browse should not be waiting on its own advertisement at all. This is [3.5c](#35-who-advertises-and-who-browses)'s reversal, and for a peer holding several pairings it is the better shape rather than merely a permitted one — which for a peer that cannot advertise usefully anyway ([3.5d](#35-who-advertises-and-who-browses)) settles the question entirely.

3.4d said what a peer advertises and Annex B3 said the alternatives were unspecified, which left a device holding a season of pairings with no conformant way to be found by any of them but one. The decision costs nothing structural — it is a registration schedule, not a wire change — and it is deliberately the conservative half of B3: repeated `rid` keys in one TXT record, or several service instances, would each leak the count and are still unspecified (finding F-D7-4, PinPointCapture, session S4; decided by L17).

The construction is the same idea as a resolvable private address: unlinkable to a stranger, recognisable to a counterpart, and rotating so that observations in two venues cannot be correlated.

- **(3.4e)** **Residual exposure, stated rather than hidden.** Anyone on the link can see that a PPCP-capable peer is present, and anyone holding a pairing can test whether a given advertisement is that peer. Neither is fixable while the peer advertises at all, and both are why advertising is confined to the reconnection case.

### 3.5 Who advertises and who browses

- **(3.5a) MAY** Any peer advertise; any peer browse. `role` in the TXT record ([§3.3](#33-txt-record)) therefore legitimately carries any of its values, and a peer that discovers a counterpart dials it ([§2](#2-rendezvous-paths)).
- **(3.5b) SHOULD** A **capture peer advertises** and a **host browses**. Browsing needs only the querier role; advertising needs a responder, which a mobile platform supplies and several desktop platforms do not. Capture also requires the foreground, so the capture peer is the one reliably present to be found.
- **(3.5c)** A deployment that reverses this — a host advertising so a capture peer can browse and dial on reconnection — is conformant, and is the shape a "reconnect to a discovered host" interaction needs. The cost is that the host supplies its own responder, which is a platform question rather than a protocol one.
- **(3.5d) MUST NOT** *Erratum E23, 23 August 2026.* A peer **advertise for reconnection where its platform cannot resolve a PSK identity server-side**, and 3.5b does not apply to it: the roles reverse under 3.5c, and that reversal is **the conformant shape for such a peer rather than a deviation from a SHOULD**. [5.3b](#53-psk-identity) requires the accepting side to recompute `tag` with the `K_id` of *each pairing it holds* and select the match — a per-connection resolver hook. A platform whose listener offers no such hook refuses a rotating identity outright, so a peer that advertised anyway would be discoverable and unable to complete the handshake it advertised for, which is worse than not advertising. This is measured, not hypothetical: `Network.framework`'s listener has no server-side PSK resolver and answers `PSK_IDENTITY_NOT_FOUND`. The **required** pairing-code path is unaffected — there the device dials (finding F-D1-1, PinPointCapture, session S1).

- **(3.5e) SHOULD** *Erratum E32, 24 August 2026 — CR-01.* Where a peer's counterpart **cannot** advertise for reconnection under [3.5d](#35-who-advertises-and-who-browses), the peer that **can** advertises. This is the reversal of [3.5c](#35-who-advertises-and-who-browses) stated as an obligation rather than a permission, and it is here because 3.5d only says who must *not* advertise: read together with [3.5b](#35-who-advertises-and-who-browses)'s recommendation that the capture peer does, a deployment could conclude that **neither** end advertises and satisfy every clause while doing so. In that deployment [§7.4](#74-persistent-pairings)'s persisted pairing buys nothing at all — both peers hold valid key material and no path exists by which either finds the other — and the users see a protocol that remembers them and still asks for a code every session.

  CR-01 §9 question 3 asks whether the host will advertise. 3.5e is the answer this document can give: on a deployment whose capture peer is bound by 3.5d, **the host advertising is what makes persistence work**, and it is a SHOULD rather than a MUST only because [§3](#3-rv-1--service-discovery) as a whole is optional and a host reachable at a cached endpoint has another way. Whether PinPointStudio does it remains PinPointStudio's to confirm, and 3.5c is still right that supplying a responder is a platform question — but it is no longer a question the specification leaves open on which peer *should*.

### 3.6 Multicast is not to be relied on

- **(3.6a) MUST NOT** An implementation treat discovery failure as an error state. Multicast is rate-limited or dropped by many consumer access points, blocked by client isolation on guest networks, and does not cross VLAN boundaries. **It will not work at a range.**
- **(3.6b) MUST** Failure to discover falls back to the pairing code or a cached endpoint, without user-visible failure.

### 3.7 The bootstrap window

*Erratum E31, 24 August 2026 — CR-01. How a peer offering a first pairing under [§11](#11-rv-6--guided-pairing) is found. The handshake itself is [§11](#11-rv-6--guided-pairing); this is only its advertisement.*

A **bootstrap window** is a bounded interval during which a peer will accept one guided pairing from a peer it has never met. It is advertised as a service instance of its own, alongside — not instead of — whatever the peer advertises for reconnection.

- **(3.7a) MUST** A bootstrap window opens **only** on an explicit user action at the peer that opens it. It MUST NOT open at launch, on a schedule, on discovery of a counterpart, or in response to anything arriving on the network.
- **(3.7b) MUST** The window closes on the earliest of: one guided pairing **completed**; one bootstrap attempt **aborted or rejected** ([§11.9](#119-aborting-and-the-one-attempt-rule)); the peer's own timeout; or a further user action closing it. The timeout is the peer's own policy and **MUST NOT exceed 180 seconds**. On close the peer withdraws the service instance.
- **(3.7c) MUST** `bn` is 4 bytes from a CSPRNG, drawn fresh for each window, used for the instance name of [3.2c](#32-instance-name) and for nothing else. It is not a key, not an identifier of the peer, and is never persisted.
- **(3.7d) MUST** A peer advertises **at most one** bootstrap instance at a time, and the instance exists only while the window is open.
- **(3.7e) MAY** A peer advertise a bootstrap instance **and** a reconnection instance ([3.4d1](#34-resolvable-identifiers)) simultaneously. This does not breach 3.4d1's one-instance rule, which exists to keep the *count of held pairings* unobservable: a bootstrap instance carries no `rid` and therefore contributes nothing to that count.
- **(3.7f) MUST** The bootstrap instance's SRV record names the endpoint the bootstrap connection is made to. That endpoint MUST NOT be the peer's PPCP listener: a bootstrap connection and a PPCP link are different protocols with different first frames, and separating them at the port is what keeps either from having to guess which it received ([11.3c](#113-roles-and-the-connection)).
- **(3.7g)** **Residual exposure, stated rather than hidden.** An open window announces to everyone on the link that a peer here will pair with a stranger **right now**, and — where `dl` is present — under a name the operator chose. This is strictly more than [3.4e](#34-resolvable-identifiers) discloses, and it is why the window is user-opened ([3.7a](#37-the-bootstrap-window)), single-attempt ([3.7b](#37-the-bootstrap-window)) and short. It is not reducible further while the peer is discoverable for pairing at all: a window nobody can see is a window nobody can pair with.
- **(3.7h) MAY** A guided pairing be reached **without** discovery, at an endpoint entered or configured out of band. [§11](#11-rv-6--guided-pairing) constrains the handshake and not how the endpoint was learned, and [3.6a](#36-multicast-is-not-to-be-relied-on) applies here with more force than anywhere else in this document — multicast is least reliable at exactly the venue this path was asked for.

**Why a window and not a mode.** A peer permanently willing to pair with a stranger is a peer any passer-by may attempt to pair with, and the only thing standing between them and success is an operator who declines. [§11.8](#118-what-the-comparison-proves) bounds an active attacker to one guess in a million *per operator confirmation*, and that bound is worth exactly what the number of confirmations is: an always-open peer converts a one-shot attack into a grinding one. The window is what makes the count small, and 3.7a and 3.7b are the two clauses that keep it small.

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
- **(4.3a1) MUST** *Erratum E20, 23 August 2026.* An encoder emits every optional field whose value the publisher has decided, **including one equal to this document's default** — both vectors of [§10.3](#103-pairing-code) emit `mu: 1`. Absence and a defaulted presence carry the same *meaning* and different *bytes*, so 4.3a's promise that a given pairing reproduces byte-identical codes is only true if one pairing has one encoding. A **decoder** still accepts absence and reads the default ([4.2c](#42-version-handling)); this binds encoders only, and 4.3a says nothing about it as first written (finding by `libppcp`, session S1).
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
- **(4.4a1) SHOULD** A peer with positive reason to distrust its wall clock **attempts the pairing anyway** and reports the code as *possibly* expired. The publisher holds the authoritative clock and enforces `exp` itself ([§7.3e](#73-single-use-and-expiry)); a device with a wrong clock at a range has no network to correct it and refusing a valid code leaves the user with no path at all.
- **(4.4a2)** *Erratum E24, 23 August 2026 — a decision, reversible.* **"Positive reason to distrust" is any one of the following, and a peer that can evaluate only the first is conformant:**

  | | Test | Availability |
  |---|---|---|
  | 1 | The clock reads **earlier than the software's own build date**, which is baked into the binary. | Universal. A peer MUST implement this one. |
  | 2 | The clock has **never been synchronised since boot**. | Where the platform exposes it. |
  | 3 | The clock has **stepped since boot** by more than the peer's own tolerance — an observed `ClockDiscontinuity` on a `wall` timebase ([`PPCP-CORE` §5.5](ppcp-core.md#55-clockdiscontinuity)). | Where the peer watches its own clock, which a PPCP peer already does. |

  4.4a1 as first written named test 2 first, and **iOS does not expose it**: an application cannot read whether the system clock has ever been set from a time source. Only test 1 is implementable there, so a peer restricted to it would either have looked non-conformant or would have skipped 4.4a1 and refused valid codes. Test 1 alone is sufficient because it catches the case the clause exists for — a device whose clock reset to the epoch, or to a manufacture date, on a flat battery — and because the cost of a false negative is bounded by 7.3e: the publisher refuses an expired code regardless, so a peer that wrongly trusts its clock and attempts anyway loses one round trip (finding F-D7-1, PinPointCapture, session S4; decided by L17).
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

- **(5.3a1) MUST** *Erratum E21, 23 August 2026.* **No octet of the identity is `0x00`.** A client draws `rn2`, computes `tag`, and **draws again** if any byte of either is zero; the leading `0x01` never is. Rejection sampling costs 1.07 HMACs on average, leaves `rn2` with more than 63 bits of entropy, and changes nothing at the server — 5.3b recomputes `tag` from the `rn2` it received exactly as before. It is required because several widely-used TLS stacks carry a PSK identity as a C string and take its length with `strlen`: an embedded zero truncates the identity, the server resolves nothing, and the handshake fails **intermittently** — measured at roughly one connection in sixteen, which is 17 octets each with a 1-in-256 chance of being zero. An intermittent handshake failure at a driving range is diagnosed as a network fault, not as a specification defect. [5.3f](#53-psk-identity) already required the identity to be carried untranscoded and untruncated; this makes it survivable by a stack that does not (finding by PinPointStudio, session S1).
- **(5.3b) MUST** A server resolves an offered identity by recomputing `tag` with the `K_id` of each pairing it holds — outstanding codes and persisted pairings alike — and selecting the match.
- **(5.3c) MUST** A server that resolves no pairing aborts the handshake, with the **same alert** it would send for a resolved identity and a wrong key. Failing uniformly costs nothing and is required.
- **(5.3d) SHOULD** The two cases are also indistinguishable in **timing**. This is harder — a wrong key normally fails later, at Finished verification, than an unresolvable identity — and the usual technique is to proceed with a dummy key so both paths run to the same point.
- **(5.3c1)** *Erratum E22, 23 August 2026 — scope, recorded rather than changed.* The case 5.3c and 5.3d equalise — **a resolved identity with a wrong key** — is **unreachable under this document's key schedule**, and an implementation that cannot make the two paths identical in timing should know why before it spends effort on it. `K_tls` and `K_id` are derived from one `PRK` ([§5.1](#51-key-derivation)), so a peer holding the wrong secret holds the wrong `K_id` too and produces an identity the server cannot resolve: the wrong-key branch is not reachable by a scanned code, by a persisted pairing, or by an attacker without `PRK`. 5.3c and 5.3d stand as written because they cost nothing and because a future schedule that separates the two keys would make the branch reachable — at which point they are the clauses that were already there. Until then a conformance harness may record [RT-11](#9-conformance) as **not applicable** on a code path where the two keys share a `PRK`, and MUST say so rather than reporting a pass (finding F-D1-2, PinPointCapture, session S1, narrowed on re-test).
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
- **(5.4b1)** **Discharged.** The measurement was repeated on the target hardware — an iPhone 16 on the release operating system, not a beta — and is **identical to the desktop result in every respect**: TLS 1.3 with an external PSK fails, both ECDHE_PSK suites are silently ignored, and `0x00A8` is negotiated. The platform difference that could not be ruled out does not exist. The result is unfavourable, so §5.4.3's relaxation stands as written and no clause changes.
- **(5.4b2)** The same run confirmed [5.3f](#53-psk-identity) on the device: the 17-octet binary identity of [§10.2](#102-resolvable-identifiers) completes a handshake at TLS 1.2 unchanged.

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
- **(5.4k) MUST** A peer makes the **achieved TLS version and key-exchange mode** available to its application layer, and records both in its diagnostic export. [7.2b](#72-handling-the-pairing-secret) forbids that export carrying keys or payloads; the negotiated mode is neither.

**The judgement covers candidate audio too — recorded, because it was asked and answered.** [§5.4.3](#543-the-decision) argues the relaxation on swing video and then identifies a different part of the payload as the one carrying a privacy dimension: the candidate-attached audio windows, whose count is *"not bounded by anything the user does"* ([`PPCP-CORE` §13c](ppcp-core.md#13-privacy-considerations)), and which in the lesson case is a coach and a pupil talking.

Draft 4 carried a clause withholding that audio over a connection without forward secrecy. **The protocol owner has since answered the question it existed to force: the sensitivity judgement covers the audio as well, and the clause is deleted.** Both reviewers had said either answer was acceptable and that the one unacceptable outcome was §5.4.3 naming an exception that nothing acted on — so the reasoning above stays, with the answer recorded against it rather than the paragraph quietly removed.

What follows from that, and is not softened: **every session on a plain-PSK leg carries candidate audio with no forward secrecy**, and an attacker who records one and later obtains its pairing secret decrypts that audio along with the video. That is the accepted consequence, not an oversight, and [§7.1](#71-threat-model)'s *not defended* table says so.

5.4k is what [5.4i](#543-the-decision) needs, and what makes [B13](#annex-b--open-issues) answerable either way. Forward secrecy is now a **per-connection outcome** rather than a property of the protocol, and nothing previously required a peer to know which it got — so a deployment could not apply a policy to it, and a peer telling a user "this connection is encrypted" would be saying something true and not the whole of it.

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
| **An active man-in-the-middle on a first pairing established without a code** | The compared short authentication string of [§11.7](#117-the-short-authentication-string), which an attacker must match on **both** legs at once and cannot bias ([§11.8](#118-what-the-comparison-proves)). One guess in 1 048 576, per operator confirmation, non-repeatable ([3.7b](#37-the-bootstrap-window)) |
| **A pairing secret photographed, where the pairing was established under [§11](#11-rv-6--guided-pairing)** | **Structurally.** A guided pairing's `PRK` is derived from an ephemeral exchange and is never displayed, printed, or rendered as a code. There is nothing to photograph — see below |
| A stale code reaching a newer peer and being half-understood | Version marker and its reporting obligation ([§4.2](#42-version-handling)) |

**Not defended against, and stated so nobody assumes otherwise:**

| Threat | Why not |
|---|---|
| Someone who can see the code at the moment it is displayed | It is a shared secret shown on a screen. This is the model. Physical control of the display is the control. |
| A compromised peer at either end | Out of scope for any rendezvous protocol. |
| Traffic analysis | Payload sizes and timing reveal that capture is happening and roughly when. Not addressed. |
| Denial of service | An attacker on the link can disrupt multicast or the transport. The fallbacks in [§3.6](#36-multicast-is-not-to-be-relied-on) reduce the impact; nothing prevents it. |
| **Impersonation between peers that scanned the same multi-use code** | They hold **identical key material** by construction ([§7.4f](#74-persistent-pairings)). `mu: 1` is the pairwise case; `mu > 1` is a group credential and must be read as one. |
| **Retrospective decryption of a recorded session, where the pairing secret is later obtained and the peers could not reach an ephemeral key exchange** | A deliberate trade, taken on the sensitivity of the payload ([§5.4.3](#543-the-decision)) — **including the candidate-attached audio**, which the owner's judgement was asked about specifically and covers. Single use and publisher-side expiry ([§7.3](#73-single-use-and-expiry)) reduce the window in which a secret is obtainable; they do not close it. |
| **An operator who affirms [§11.7](#117-the-short-authentication-string)'s digits without comparing them** | The comparison **is** the authentication on that path. A user who confirms without looking has authenticated the attacker, and no clause can reach that. [11.7d](#117-the-short-authentication-string) constrains how the digits are presented for exactly this reason; it cannot constrain whether they are read. |
| **A one-in-a-million guess by a man-in-the-middle on a guided pairing** | Bounded, not eliminated. [§11.8](#118-what-the-comparison-proves) states the number and what keeps the attacker to one draw of it. A successful guess is an undetected MITM for the life of the pairing, which is why [7.4b](#74-persistent-pairings)'s revocability matters as much on this path as on any. |
| **Denial of a bootstrap window by an attacker who dials it first** | An attacker on the link can consume the single attempt of [3.7b](#37-the-bootstrap-window) before the operator's device reaches it. The operator sees digits that do not match, declines, and opens the window again; the attacker can repeat this indefinitely. It costs the attacker nothing and gains it nothing but the operator's patience, and it is the [§3.6](#36-multicast-is-not-to-be-relied-on) fallback — the pairing code — that ends it. |
| Anything after the byte stream exists | PPCP's problem, and PPCP assumes the stream is authenticated ([§1.3c](#13-where-it-stops)). |

**The one place [§11](#11-rv-6--guided-pairing) is stronger than [§4](#4-rv-2--the-pairing-code), and it is worth naming.** [§5.4.3](#543-the-decision) accepted retrospective decryption on the grounds that the payload is not highly sensitive, and named the route by which the secret leaks: *"they obtain it by photographing the code, or from a peer's storage."* A guided pairing removes the first of those two routes entirely. Its `PRK` descends from an X25519 exchange whose private halves never leave the two devices and are erased at the end of the handshake ([11.6f](#116-derivation)); no value from which it can be recovered is ever displayed to a room. Storage remains — [7.4b](#74-persistent-pairings) and [7.2c](#72-handling-the-pairing-secret) are what address that, unchanged — but the camera does not. This was not the reason CR-01 was raised and it is not why the request was granted; it is a consequence, and a document that records what a decision costs should record what one buys.

### 7.2 Handling the pairing secret

- **(7.2a) MUST** `psk` is at least 128 bits from a cryptographically secure random number generator. A predictable secret defeats the entire model, and it is the single easiest thing to get wrong.
- **(7.2b) MUST NOT** A pairing secret, a derived key, or a decoded payload appear in a log, a crash report, an analytics event, or a **diagnostic export**.
- **(7.2c) MUST** Secrets at rest are held in the platform's protected storage where one exists.
- **(7.2d) MUST** A peer erases a pairing's key material when the pairing is revoked or the session it belongs to closes, unless the pairing was persisted under [§7.4](#74-persistent-pairings).

- **(7.2e) MUST NOT** *Erratum E33, 24 August 2026 — CR-01.* A guided pairing's **ephemeral private key**, its shared secret `Z`, `BK`, or `K_c` ([§11.6](#116-derivation)) appear in a log, a crash report, an analytics event or a diagnostic export, and each is erased at the end of the handshake whether it succeeded or failed ([11.6f](#116-derivation)). [7.2b](#72-handling-the-pairing-secret) already binds the `PRK` that results; these are the values it descends from, and a document that named only the destination would be read as permitting the sources. The **short authentication string** is not on this list: it is displayed on two screens by design, reveals nothing about `Z`, and is worthless once the window has closed.

7.2b names diagnostic export explicitly because a user-initiated diagnostic bundle is a first-class output of a PPCP implementation, it is attached to public issue trackers, and it is assembled by code whose author is thinking about clock residuals rather than about secrets.

### 7.3 Single use and expiry

*Erratum E3, 23 August 2026 — 7.3a reworded and 7.3f added, after the third implementation session. 7.3a counted **handshakes**, and a PPCP link is two (optionally three) TCP connections each carrying its own TLS session keyed by the same `K_tls` ([`PPCP-CORE` §3.1](ppcp-core.md#31-why-two-channels-is-not-negotiable), [`PPCP-ENC` §2.1](ppcp-encoding.md#21-binding-streams-to-a-link)). So the default `mu: 1` — the pairwise case the whole model is built around — was spent by the control channel's handshake and the bulk channel **of the same link** was then refused: every conformant pairing died on its second channel (finding F-H6-1, PinPointStudio, session S4).*

- **(7.3a) MUST** A publisher invalidates a pairing code once `mu` **pairings** have been established with it. The default is one. A pairing is one derived `K_tls` and therefore one link; a link is **several** TLS handshakes, one per channel, and they count once between them.
- **(7.3b) MUST** A publisher invalidates the code when the session it belongs to closes, whether or not it was used.
- **(7.3c) SHOULD** A code carries `exp`, and a publisher chooses the shortest expiry the workflow tolerates.
- **(7.3d) MUST** A publisher generates fresh `psk` and `sid` for every code. A code is never regenerated with the same secret.
- **(7.3f) MUST** `mu` and [7.3b](#73-single-use-and-expiry) invalidate the **code**, not the pairings already established from it. A pairing outlives the code that created it: it ends when its session closes, when either side revokes it ([7.4d](#74-persistent-pairings)), or — for a code whose `mu` exceeded 1 — with the session it was scoped to ([7.4f](#74-persistent-pairings)). Reconnection within a session ([§7.5](#75-reconnecting-within-a-session)) is therefore available from a `mu: 1` code, which the previous reading of 7.3a made impossible: one link and no reconnection made §7.5 dead letter in the default case (F-H6-1a).
- **(7.3e) MUST** A publisher **refuses a handshake** for a code past its `exp`. Expiry is enforced by the party holding the authoritative clock, not by the party reading a printed number — which is what lets [4.4a1](#44-handling-a-scanned-code) permit a peer with an untrustworthy clock to attempt the pairing rather than be locked out.

7.3a and 7.3b are clock-free and are the primary defence; `exp` depends on two wall clocks agreeing and is therefore secondary rather than relied upon. `mu` exists because pairing several devices from one displayed code is a real workflow, and the alternative — a code that is silently reusable forever — is worse than one that says how many times it may be used.

### 7.4 Persistent pairings

- **(7.4a) MAY** Both peers persist `PRK` after a successful pairing, so a later session can be established without displaying a new code. This is what makes the discovery path of [§3](#3-rv-1--service-discovery) useful.
- **(7.4b) MUST** Persistence is opt-in, visible to the user, and individually revocable.
- **(7.4c) MUST** A persisted pairing is scoped to the counterpart peer identity learned inside the authenticated channel. It is not transferable.
- **(7.4d) MUST** Revocation on either side is honoured immediately by that side, and results in a failed handshake for the other.
- **(7.4h) MAY** *Erratum E26, 23 August 2026 — a decision, reversible.* A peer persisting a pairing under 7.4a **also persist the network name `wifi.s`** from the code that created it, and **only** that field, as a hint for rejoining. It **MUST NOT** persist `wifi.k`, and [4.4c](#44-handling-a-scanned-code)'s prohibition on retaining a decoded payload is scoped by this clause to that one field. A network name is not a secret — it is broadcast by the access point — and a passphrase is, which is why the disjunction falls where it does. The peer offers the name to the **user**, who joins or does not; it MUST NOT rejoin silently ([6a](#6-rv-4--network-join) is unchanged, and a peer holding no passphrase could not anyway).

  Without this, §7.4's whole workflow failed at exactly the venue it was written for. A publisher that provides its own network puts `wifi` in the code; 4.4c discards the payload once the pairing is established; §7.4 persists `PRK` and nothing else; [6b](#6-rv-4--network-join) requires the peer to leave the join in the user's control or restore the prior configuration. So on the next visit both peers hold a valid pairing, the device is on some other network or none, and the persisted pairing — whose entire purpose is to avoid displaying a new code — could not reach the publisher and could not say why. Displaying a fresh code works and is the fallback; carrying the name means it is not the *only* path (finding F-D7-3, PinPointCapture, session S4; decided by L17).
- **(7.4e) MUST** A new session established from a persisted pairing derives a fresh `sid` inside the authenticated channel. **The original session's identifier is not reused for anything.** After the initial derivation of [§5.1](#51-key-derivation), `sid` survives only as the HKDF salt baked into `PRK`; it is never transmitted again, by either peer, on any connection.

7.4e's tail clause said the opposite until Draft 3 — that the original `sid` was reused *for the PSK identity* — which is what [5.3e](#53-psk-identity) now forbids, and its cross-reference pointed at a clause that had moved. It is the [§5.3](#53-psk-identity) fix not carried into the section an implementer is reading when they build persistence, and a security document that says transmit two sections after forbidding it is resolved by whichever section is read second.

- **(7.4i) MAY** *Erratum E33, 24 August 2026 — CR-01.* A pairing established under [§11](#11-rv-6--guided-pairing) **is persisted under 7.4a like any other**, and [7.4f](#74-persistent-pairings) does not reach it. A guided pairing is pairwise by construction — one bootstrap connection, two ephemeral keys, one counterpart, and a window that closes on the first completed attempt ([3.7b](#37-the-bootstrap-window)) — so its key material reached exactly two peers and the group-credential reasoning below does not apply. There is no `mu` on this path and none is defined for it; a second device pairs by opening a second window.
- **(7.4f) MUST NOT** A peer persist `PRK` derived from a pairing code whose `mu` exceeded 1. **A pairing established from a multi-use code is session-scoped**, because its key material is held by every peer that scanned that code.
- **(7.4g)** Where a persistent pairing from a multi-use code is wanted, the peers derive a fresh **per-peer** secret inside the authenticated channel and persist that. Specifying that exchange is deferred; until it exists, multi-device pairing is per-session.

**Why `mu > 1` is a group credential and not three pairings.** Every peer that scans one code derives the same `PRK`, therefore the same `K_tls` and the same `K_id`, from the same `psk` and `sid`. With `mu: 3` the three devices hold **identical key material**: any one can complete a handshake indistinguishable from another's, and can present a different `Peer.id` in `hello` while doing it. 7.4c's scoping to a counterpart peer identity is a *policy* statement, not a cryptographic one, and nothing enforces it.

[§7.1](#71-threat-model) claims the model defends against an unpaired peer receiving capture payload, on the strength of a secret that only reaches the counterpart by being scanned. With `mu > 1` that secret reached three counterparts, so *paired* names a group and mutual authentication proves group membership rather than identity. 7.4f bounds the consequence to the session that created it; `mu` survives because displaying three codes is worse ergonomics for no gain over proper per-peer re-keying.

**The exposure of a persisted pairing is real and should be weighed rather than assumed away**: possession of the device's storage is possession of continuing access. That is why 7.4b requires it to be visible and revocable, and why it is optional rather than automatic.

### 7.5 Reconnecting within a session

[`PPCP-MSG` §4.3](ppcp-messages.md#43-session_resume) defines `session_resume` and says nothing about whether re-authentication is required. That silence was a hole. It is closed here:

- **(7.5a) MUST** A reconnecting peer completes a full handshake ([§5.2](#52-tls-profile)) on the new connection, using the same derived `K_tls`. It does not require a new pairing code.
- **(7.5b) MUST** `session_resume` is accepted only on a connection that completed that handshake, and only for the `sid` bound to it.
- **(7.5c) MUST NOT** A peer accept `session_resume` for a session whose **pairing** has ended — revoked under [7.4d](#74-persistent-pairings), or closed with its session under [7.3b](#73-single-use-and-expiry). A code that has spent its `mu` is invalid for establishing further pairings and says nothing about the pairing this connection already holds ([7.3f](#73-single-use-and-expiry)).
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
- **(9d)** Service discovery ([§3](#3-rv-1--service-discovery)), network join ([§6](#6-rv-4--network-join)) and guided pairing ([§11](#11-rv-6--guided-pairing)) are independently optional, and an implementation states which it provides.
- **(9e) MUST** *Erratum E30, 24 August 2026 — CR-01.* An implementation claiming [§11](#11-rv-6--guided-pairing) implements it **in full** — both the commitment and the two-sided confirmation. Guided pairing has no useful subset: an implementation that skipped the commitment ([11.5c](#115-the-exchange)) or confirmed at one end only ([11.7c](#117-the-short-authentication-string)) would complete handshakes indistinguishable from a conformant one and would authenticate nothing, and its counterpart cannot tell. This is [RT-12](#9-conformance)'s problem arriving on a second path, and [RT-20](#9-conformance) is where it is caught.
- **(9e1) MUST** *Erratum E39, 24 August 2026 — finding R-05, PinPointStudio.* An implementation claiming [§11](#11-rv-6--guided-pairing) **states which of the initiator and acceptor roles it provides**, and [9e](#9-conformance)'s *in full* binds each role it claims rather than requiring both. 9e was written about not skipping the security-critical halves of the exchange and said nothing about the roles, and the distinction is not academic: **on CR-01's own deployment one peer will ship initiator-only** — a desktop host has no product reason to accept a guided pairing from a stranger, and [11.2b](#112-why-it-is-not-tls-and-what-that-unlocks) puts it on that side anyway. Two initiator-only peers cannot pair with each other, so *"implements §11"* on its own describes a narrower capability than a reader would take it for. This is [9d](#9-conformance)'s statement obligation extended one level down, for [9d](#9-conformance)'s reason.
- **(9f)** Claiming [§11](#11-rv-6--guided-pairing) does not relieve a peer of [2a](#2-rendezvous-paths). The pairing code stays REQUIRED, and [§3.6](#36-multicast-is-not-to-be-relied-on) is why: guided pairing is reached over multicast on a network where multicast frequently does not work.

Required tests, to be folded into [`PPCP-CONF`](ppcp-conformance.md) once this document is agreed. **Method** uses the vocabulary of [`PPCP-CONF` §1](ppcp-conformance.md#1-claiming-conformance), with one addition: **review** means the requirement is not observable from outside the implementation and is verified by reading the code.

| Test | Method | Asserts |
|---|---|---|
| **RT-1** | static | The derivation vectors of [§10.1](#101-key-derivation) reproduce byte-for-byte. |
| **RT-2** | static | Both pairing codes of [§10.3](#103-pairing-code) encode and decode byte-for-byte. **The all-fields payload — carrying `dn`, `mu`, `exp` and `wifi` — still encodes `v` as its first key**, which the minimal one cannot demonstrate. `Session.id` derives from `sid` as canonical lowercase UUID text ([4.3e](#43-payload)). |
| **RT-3** | injected | A `v` the implementation does not know produces a *version* report, not a generic failure (4.2b). |
| **RT-4** | injected | **A handshake negotiating a weaker mode than both peers can reach is refused** (5.2b1), and no handshake is unencrypted (5.2f). Where both peers reach TLS 1.3, assert `psk_ke` is refused (5.2b). Where one cannot, assert the negotiated result is the **strongest the pair can reach**, and that the outcome is surfaced (5.4k). Demonstrated against an instrumented counterpart or a wire capture, never by an API assertion ([5.2i](#52-tls-profile)). |
| **RT-5** | paired | A second **pairing** with a `mu: 1` code is refused (7.3a), and — the half erratum E3 added — the second and third **channels of the first pairing** are not: a link is several handshakes over one `K_tls`, and they count once between them (7.3f). A harness that asserts only the refusal passes the wrong reading. |
| **RT-6** | injected | An expired code is reported as expired, with no connection attempted, **by a peer whose wall clock it has reason to trust** (4.4a). A peer exercising 4.4a1 is covered by RT-15 instead, and MUST NOT fail this one for attempting. |
| **RT-7** | paired | A TXT record contains no `Peer.id`, no device name and no session count; the instance name carries no persistent value (3.3b, 3.2b). |
| **RT-8** | paired | `rid` changes across re-registration and resolves under the correct `K_id` only (3.4a, 3.4b). |
| **RT-9** | paired | A diagnostic export produced immediately after a pairing contains no secret and no payload (7.2b, 4.4c). |
| **RT-10** | injected | `session_resume` is refused on a connection that did not complete the handshake (7.5b). |
| **RT-11** | injected | Rejection of an unresolvable identity and of a wrong key are indistinguishable in content, and in timing where [5.3d](#53-psk-identity) is met (7.7c). **Where `K_tls` and `K_id` are derived from one `PRK`, as [§5.1](#51-key-derivation) does, the wrong-key case is unreachable and this row is `n/a` on that code path** ([5.3c1](#53-psk-identity)) — a harness records that rather than a pass. |
| **RT-12** | **review** | Secrets come from a platform CSPRNG at full width, are held in protected storage where one exists, and are erased on revocation or session close (7.2a, 7.2c, 7.2d). |
| **RT-13** | **review** | A network join obtains the user's consent for the specific network and does not leave the device attached to a network the user did not choose to keep (6a, 6b). |
| **RT-14** | static | The PSK identity of [§10.2](#102-resolvable-identifiers) reproduces byte-for-byte, **differs across connections**, resolves under the correct `K_id` only, contains no `sid` (5.3a, 5.3e), and **carries no `0x00` octet over a run of draws large enough for one to have appeared** ([5.3a1](#53-psk-identity)) — a single draw passes 94% of the time whether or not the peer implements it, so one is not evidence. **Where TLS 1.2 is negotiated, the server sends an *empty* `psk_identity_hint`** (5.2h property 3) — an obligation that exists only on the newly-permitted path, which is the path one implementation now always takes. |
| **RT-15** | paired | A publisher refuses a handshake for a code past its `exp` (7.3e), and a peer that cannot trust its clock attempts rather than refuses (4.4a1). |
| **RT-16** | **review** | No `PRK` derived from a code with `mu > 1` is persisted (7.4f). |
| **RT-17** | **review** | The peer offers **every** key-exchange mode and ciphersuite its platform exposes, and the offered set is derived from a platform capability query rather than from a constant (5.2b1, 5.4f). **Re-read whenever the TLS setup path is touched, and whenever a platform SDK is updated** — a mode that becomes available is a mode the peer must begin offering, and a platform that gains TLS 1.3 external PSK silently restores property 2 for an implementation that asks rather than assumes. |

| **RT-18** | static | The guided-pairing vectors of [§10.4](#104-guided-pairing) reproduce byte-for-byte: `pk`, the commitment, `Z`, `BK`, the **six displayed digits**, both confirmation MACs, `sid` in canonical UUID text, and the `PRK`, `K_tls` and `K_id` that descend from it (11.5, [11.6](#116-derivation)). The `PRK` row is the one that matters most — it is where this path rejoins [§5.1](#51-key-derivation), and an implementation that agrees on the digits and disagrees on the `PRK` fails at the TLS handshake with no diagnostic. **A reproduction MUST record the erratum level it was taken against**: E34 moved four of these rows, both teams' first pass produced the superseded values, and a run that reports a mismatch without saying which text it read cannot be told from an implementation defect. |
| **RT-19** | injected | An acceptor sent a `bs_reveal` whose `pk` does not match the committed hash **aborts** with `commitment_mismatch` and does not derive (11.5d). Injected by a counterpart that commits to one key and reveals another. |
| **RT-20** | paired | The digits differ on the two legs of an interposed connection, and a peer whose user declines **does not pair** — with the window closed and not reopened without a further user action (11.7, [3.7b](#37-the-bootstrap-window)). Run against a deliberate man-in-the-middle that relays both legs; this is the test the whole path exists to pass, and no single-implementation harness can run it. |
| **RT-21** | injected | A small-order `pk` produces `bs_abort` / `invalid_key` and **no derivation** (11.6b) — asserted on the *observable*, because neither implementation's library returns an all-zero `Z` to check for, and the row said so until erratum E36. Injected by offering each of the five standard small-order u-coordinates. Assert also that the failure is **not** retried ([3.7b](#37-the-bootstrap-window)). |
| **RT-22** | paired | A bootstrap instance carries `bs`, no `rn` and no `rid`; an instance carrying both `bs` and `rid` is ignored; the instance is withdrawn when the window closes (3.3f, [3.3g](#33-txt-record), [3.7b](#37-the-bootstrap-window)). |
| **RT-23** | **review** | The ephemeral private key, `Z`, `BK` and `K_c` are erased on completion **and on abort**, and appear in no export (7.2e, [11.6f](#116-derivation)); the keypair is drawn fresh per attempt and never reused (11.5a). |
| **RT-24** | injected | A `bs_accept.v` differing from the `v` sent **aborts** (11.4h); and a `v` rewritten in **both** directions, so the echo check passes, produces **different digits and failing MACs** at the two peers rather than a completed pairing (11.4i). The second half is the one that matters and cannot be tested without an interposed relay — it belongs with [RT-20](#9-conformance). |
| **RT-24a** | static | `sid` and `PRK` reproduce from `Z` **alone**, with no transcript bound (11.6c1) — the over-application [11.6c1](#116-derivation) exists to forbid, which produces matching digits, matching MACs and a divergent `PRK`. This is [RT-18](#9-conformance)'s `PRK` row read as a **negative**: assert not merely that the value matches but that it was computed without the transcript. |
| **RT-25** | **review** | An initiator runs **one** attempt at a time and displays digits for one (11.3d1). Read in the code, because a peer that dials a list of discovered windows to show the operator a list of numbers passes every wire test in this document while multiplying the [§11.8](#118-what-the-comparison-proves) bound by the length of the list. |
| **RT-26** | **review** | The affirmative control is not the default and not where a stray tap lands, and the prompt asks whether the numbers **match** (11.7d); a mismatch or MAC failure is reported without an affordance that invites a retry (11.9c). Added at PinPointStudio's request — unusual clauses are the ones that get quietly dropped, and both teams report these are offscreen-testable rather than assert-once. |

**Three of these cannot be tested from outside**, and that is worth stating rather than leaving to be discovered. Entropy quality and storage protection produce no observable difference on the wire — a peer using a predictable secret completes exactly the same handshake as one using a good secret — so **RT-12 is the requirement on which the whole model rests and the one no test can catch.** It has to be read in the code, and it should be read again whenever the key-generation path is touched.

RT-17 joins RT-12 in that set for a specific reason: 5.2b1 is the clause the relaxation made load-bearing, and it states its own untestability — an observer cannot distinguish a peer that offered less than it had from one that had less to offer. The requirement now carrying property 2 is the one nothing external can check.

RT-9 and RT-11 are the two most likely to be skipped among those that *can* be tested, and the two least likely to surface in use.

**RT-25 and RT-26 join [RT-12](#9-conformance) and [RT-17](#9-conformance) in the set nothing external can check**, and RT-25 is the more dangerous of the two: a peer violating it completes handshakes that are byte-for-byte conformant.

**RT-20 is the one that cannot be run yet, and it is the important one.** [B7](#annex-b--open-issues) already records that interoperability is untestable until a second implementation exists; guided pairing sharpens that from a general caution into a specific gap, because the property [§11](#11-rv-6--guided-pairing) exists to deliver is *resistance to an interposed third party*, and a harness cannot interpose itself between an implementation and itself in any way that means anything. Until RT-20 runs against a real counterpart with a real relay in between, [§11](#11-rv-6--guided-pairing) is a design with vectors and not a demonstrated one, and no conformance claim should say otherwise.

⚠ **[9e1](#9-conformance) has a consequence worth stating where RT-20 is described.** On this deployment the two implementations have declared **complementary single roles** — one acceptor-only, one initiator-only — which is a working pair and is the shape [11.2b](#112-why-it-is-not-tls-and-what-that-unlocks) puts them in. It is also the **entire** interoperable set: there is no third implementation and no slack, so if either side descopes its role RV-6 has no working pair at all, and RT-20 cannot run either, because a relay needs two real ends. That is a programme risk rather than a defect in 9e1 — 9e1 is what makes it visible — and it is recorded here because RT-20 is where it would first be noticed, by then too late.

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

### 10.4 Guided pairing

*Added 24 August 2026 — [CR-01](../changerequests/CR-01-in-band-pairing.md). The chain of [§11.6](#116-derivation), end to end, from two ephemeral keys to the `PRK` that [§5.1](#51-key-derivation) takes over.*

**The two private keys below are fixed so the vector reproduces. They are not how a key is chosen.** [11.5a](#115-the-exchange) requires a fresh CSPRNG draw per attempt, and a peer that shipped either of these values would be trivially impersonable by anyone reading this document.

```
sk_i   202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f
sk_a   606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f

pk_i = X25519(sk_i, 9)
       358072d6365880d1aeea329adf9121383851ed21a28e3b75e965d0d2cd166254
pk_a = X25519(sk_a, 9)
       675dd574ed7789310b3d2e7681f3790b466c773b1521fecf36577958371ea52f
```

**The commitment** ([11.5b](#115-the-exchange)) — `bs_offer.ct`:

```
ct = SHA-256("ppcp1 bs-commit" || pk_i)
     f32cd8e62f80f76adb4ba21971efbd10eb71aa6715d9e458f5422c1644357a3a
```

**The shared secret** ([11.6a](#116-derivation)). Both peers reach it, and this is the assertion that catches a peer which has mixed up the argument order of its key-agreement call:

```
Z = X25519(sk_i, pk_a) = X25519(sk_a, pk_i)
    7c79d7b5f31b9aac367477f5f7c7a68b5c44cac28ed5c902a59ec48c02956a6a
```

**The bound transcript, the bootstrap key, and what hangs off it** ([11.6c](#116-derivation)):

```
transcript = v || pk_i || pk_a                  1 + 32 + 32 = 65 octets
             01
             358072d6365880d1aeea329adf9121383851ed21a28e3b75e965d0d2cd166254
             675dd574ed7789310b3d2e7681f3790b466c773b1521fecf36577958371ea52f

BK      = HKDF-Extract(salt = "ppcp1 bootstrap", IKM = Z)
          b9f16f38e5a45ec6c0563b4fd3b38b696dfbbf4e3491fe1b7941a62637099349

sas_raw = HKDF-Expand(BK, "ppcp1 sas" || transcript, 4)
          c012786c                    = 3222435948 as a big-endian uint32

SAS     = 3222435948 mod 1000000      = 435948
                                      → displayed as  435 948
```

**The confirmation MACs** ([11.5f](#115-the-exchange)). Two labels, one per direction, so neither can be reflected:

```
K_c   = HKDF-Expand(BK, "ppcp1 bs-confirm" || transcript, 32)
        887bd19b77e6dd491886afb8cb8df9eeeadb3ead11a05cdf6e9d50b8cc00c90d

mac_i = HMAC-SHA256(K_c, "ppcp1 bs-confirm-i")  first 16 bytes
        b056a374ac4decba04f58bfd746746cd
mac_a = HMAC-SHA256(K_c, "ppcp1 bs-confirm-a")  first 16 bytes
        e0d3c748f738cf1cf54b08f7a819ff4d
```

⚠ **Four rows changed on 24 August 2026 under erratum E34** — `sas_raw`, the SAS, `K_c` and both MACs — because [11.6c](#116-derivation) now binds `v || pk_i || pk_a` into both expansions. Every other row below and above is untouched, including `PRK`. **Both teams reproduced the previous values byte for byte**, so a recomputation that still yields `11e66a4c` / SAS `313164` is not wrong about arithmetic: it is reading revision 9 as first published rather than as amended by E34, and a stale vector reaching an implementation is exactly the divergence RT-18 is for.

**The session identifier** ([11.6d](#116-derivation)). Note the two rows: the expanded bytes, and the bytes **after** the version and variant fields are set. The second is `sid`, and it is the second that salts the `PRK`:

```
expand  = HKDF-Expand(BK, "ppcp1 bootstrap-sid", 16)
          1cc4b886e8bd65e063b207ae783bc56b
                        ^^      ^^
sid     = expand with octet 6 = (0x65 & 0x0f) | 0x40 = 0x45
                    octet 8 = (0x63 & 0x3f) | 0x80 = 0xa3
          1cc4b886e8bd45e0a3b207ae783bc56b

Session.id (4.3e canonical text):
          1cc4b886-e8bd-45e0-a3b2-07ae783bc56b
```

**Where it rejoins [§5.1](#51-key-derivation)** ([11.6e](#116-derivation)). From here nothing in this document is new — these are §5.1's own expansions, over a `PRK` that came from an exchange instead of from a code:

```
PRK   = HKDF-Extract(salt = sid, IKM = Z)
        3e351aef1e5fe48411e969526b079830494d2cf13104d661694e897598ccf8c9
K_tls = HKDF-Expand(PRK, "ppcp1 tls-psk",       32)
        240b513437501f3ab8602b06b45cd84577f10f126bdc497d3cf797c9559856b0
K_id  = HKDF-Expand(PRK, "ppcp1 rendezvous-id", 32)
        9e8c8b155b89fcc9b70f4043ddaa607a7ff7acec20dc326f5c307661956a0bd9
```

**Read the `PRK` row as the one that matters.** Two implementations that agree on all six digits and disagree on the `PRK` show the operator a successful comparison and then fail the TLS handshake with `PSK_IDENTITY_NOT_FOUND` — a failure that looks exactly like the platform limitation of [3.5d](#35-who-advertises-and-who-browses) and will be diagnosed as one. The four most likely causes are all in this vector: `sid` salted before its version and variant bits were set, `Z` computed with the arguments transposed, the `SAS` info string built as `pk_a || pk_i`, and — since E34 — the transcript omitted from one of the two expansions but not the other. PinPointStudio adds a fifth its recomputation caught: **`sas_raw` read little-endian** gives `1 819 808 448 mod 10⁶ = 808448`, a perfectly plausible-looking six digits that nothing but the vector distinguishes from the right answer.

⛔ **And a sixth, which is now the most likely of all and is the only one that produces a *successful-looking* pairing first:** the transcript bound into `sid` or `PRK` as well, against [11.6c1](#116-derivation). Every other cause in this list breaks the digits or the MACs, so the operator is told something is wrong before affirming anything. That one does not — it agrees on all six digits, agrees on both MACs, and diverges only at the `PRK`, so the operator affirms a comparison that succeeded and the failure surfaces one connection later as a TLS error. [RT-24a](#9-conformance) exists for it.

*Erratum E42, 24 August 2026 — finding R-08 (PinPointStudio) and F-R9-3 (PinPointCapture), reported independently.* That example read `1281316113 mod 10⁶ = 316113` until now, and it was wrong twice over: the byte reversal of `11e66a4c` is `4c6ae611` = 1 282 074 129, not `4c5f5511` = 1 281 316 113; and it was computed from the **pre-E34** `sas_raw`, three lines below the box warning against exactly that. An implementer whose byte order is wrong prints `808448` and would have found neither number in the list — **worse than the list omitting byte order, because the list reads as authoritative.** It is a wrong number inside the note that exists to warn about wrong numbers, in the annex E34 had just moved. Both teams recomputed every row of the vector and neither recomputed the number in the prose beside it, which is the second-order lesson and is the more useful half of the finding.

---

## 11. RV-6 — Guided pairing

*Optional. Added 24 August 2026 by [CR-01](../changerequests/CR-01-in-band-pairing.md), reviewed and accepted by both teams the same day, and amended by errata E34–E39 as a result. A first pairing between peers that have never met, with no code carried between two screens.*

⚠ **Two review passes, not five.** [§4](#4-rv-2--the-pairing-code) survived three passes and three independent recomputations of its vectors; this has had two of each, and the second pass found a defect the first pass's own fix introduced ([E40](#errata-after-revision-9--change-request-cr-01-and-its-review)). Its vectors now reproduce byte for byte on two implementations sharing no code, on both sides of the E34 boundary. **[RT-20](#9-conformance) — the test the path exists to pass — still cannot run**, because it needs two real implementations either side of a deliberate relay and neither has written this section yet. Nothing in two review passes has changed that, and it should not be allowed to feel as though it has.

### 11.1 What this path is, and the one thing it cannot be

**Authentication cannot be manufactured from nothing.** Two peers meeting for the first time on a network an attacker may control share no secret, and nothing they say to each other distinguishes the intended counterpart from someone sitting between them relaying both halves. Every scheme that appears to escape this imports its authentication from somewhere outside the channel: a printed code, a certificate authority, physical contact, or a person. There is no fourth kind.

CR-01 asks for a first pairing without the code. **It cannot also be without the person**, and this section does not pretend otherwise — [§5](../changerequests/CR-01-in-band-pairing.md) of that request is right that a scheme establishing an anonymous encrypted channel and trusting what arrives on it would satisfy a casual reading of *secure* while deleting [2c](#2-rendezvous-paths) in substance.

What guided pairing removes is the **transfer**. Nothing is carried from one screen to the other, nothing is typed, and no camera is involved. What it keeps is a **comparison**: six digits appear on both screens and the operator affirms at each end that they match. That is a materially smaller act — it needs no focus, no lighting and no line of sight between the two devices, and it works when one of the two screens is a desktop monitor that cannot be pointed at anything.

- **(11.1a) MUST** A guided pairing produces a `PRK`, and from that point the pairing is **indistinguishable** from one established by a scanned code. [§5](#5-rv-3--key-derivation-and-tls), [§7.4](#74-persistent-pairings) and [§7.5](#75-reconnecting-within-a-session) apply to it verbatim and are unchanged by this section.
- **(11.1b) MUST NOT** A guided pairing complete without an affirmative act by a user at **both** peers ([11.7c](#117-the-short-authentication-string)).
- **(11.1c) MUST NOT** A peer establish a pairing by accepting an unauthenticated channel and trusting what arrives on it, at any point, under any user instruction, on any path in this document. Trust on first use is not a permitted reading of this section, and [2c](#2-rendezvous-paths) stands unamended.
- **(11.1d) MUST NOT** A peer substitute an automatic comparison for the human one — matching the digits itself across a channel it also controls, or accepting a counterpart's assertion that they matched. The comparison has value **only** because it crosses a channel the attacker is not on, and the only such channel here is a person looking at two screens.

11.1d is the clause most likely to be optimised away by someone trying to remove the last tap, and removing it removes the entire security of the path while leaving every byte on the wire unchanged. A peer that did so would pass [RT-18](#9-conformance) and every other static test in this document.

### 11.2 Why it is not TLS, and what that unlocks

The bootstrap handshake carries **no pre-shared key**, because at first contact there is not one to carry. It is therefore not a TLS-PSK connection, and the platform limitation that shapes the rest of this document does not reach it.

That is not a detail. [CR-01 §6.2](../changerequests/CR-01-in-band-pairing.md) measured that Apple's TLS listener has no server-side PSK resolver, which is what [3.5d](#35-who-advertises-and-who-browses) is built on, and CR-01 §6 concludes: *"Any bootstrap in which the capture peer **listens** on Apple platforms inherits it."* **That is true of a bootstrap built on TLS-PSK and it does not bind this one.** There is no PSK identity to resolve, so there is nothing for the listener to fail to resolve.

- **(11.2a) MUST** The dialling direction of the **bootstrap** connection is unconstrained. Either peer may open the window and either may dial it, whatever its platform, and [3.5b](#35-who-advertises-and-who-browses), [3.5c](#35-who-advertises-and-who-browses) and [3.5d](#35-who-advertises-and-who-browses) do not reach it — they are about advertising a **pairing**, and a bootstrap window advertises none.
- **(11.2b) MUST** The [§5](#5-rv-3--key-derivation-and-tls) connection that **follows** a guided pairing is constrained by [§3.5](#35-who-advertises-and-who-browses) exactly as any other, and the peers therefore **may swap roles between the two connections**. On the deployment CR-01 describes they do: the capture peer opens the window and the host dials it, then the pairing exists and the capture peer dials the host under [§5](#5-rv-3--key-derivation-and-tls) because [3.5d](#35-who-advertises-and-who-browses) leaves it no choice.

**This is what makes "the host PC finds the device and connects to it" reachable**, and it is reachable only at first contact. A range operator standing at a bay opens a window on the capture device, and PinPointStudio — browsing, dialling, with a screen large enough to show six digits at a glance — finds it and connects. Once the pairing exists, [3.5d](#35-who-advertises-and-who-browses) and [3.4d2](#34-resolvable-identifiers) put the steady state back on the device-browses-and-dials shape that CR-01 §2 correctly declines to reopen. Both directions are available, on different connections, for different reasons, and neither contradicts the other.

- **(11.2c)** **The bootstrap connection is plaintext, and that is not a relaxation of [5.2f](#52-tls-profile).** Nothing confidential crosses it: two ephemeral public keys, a hash of one of them, and two MACs. Every one of those is a public value in the construction's own security argument — an observer who records the entire exchange learns nothing that helps it, because the secret is the Diffie-Hellman output and that is never sent. [5.2f](#52-tls-profile) forbids a **PPCP** connection falling back to plaintext, no PPCP message crosses this one ([1.3c1](#13-where-it-stops)), and encrypting it would add a key-agreement step to protect values that are already public.

11.2c will be read by a reviewer as a loophole, so it is stated as a claim that can be checked rather than as an assurance. The check is: name a value on this connection whose disclosure to a passive observer weakens the pairing. **PinPointCapture took the invitation and found the one qualification**: the confirmation MACs are an *offline verifier* for `Z`, so the transcript is worth something to an observer against a peer whose CSPRNG is weak. That does not weaken the pairing of a peer whose CSPRNG is sound — there is no useful search space — and it is recorded at [§11.8](#118-what-the-comparison-proves) rather than treated as a defect in 11.2c, because encrypting the connection would not remove it either: the same MACs cross an encrypted channel to the same attacker who has compromised the RNG. What an **active** attacker can do to this connection is a different question entirely, and it is [§11.8](#118-what-the-comparison-proves) — which does not depend on the connection being encrypted, and could not be repaired by encrypting it.

### 11.3 Roles and the connection

- **(11.3a)** The peer that opens the window and accepts the connection is the **acceptor**. The peer that dials it is the **initiator**. Neither term implies a role: a host, a capture peer or an observer may be either ([2e](#2-rendezvous-paths)).
- **(11.3b) MUST** The bootstrap runs over one reliable, ordered byte stream. Where that stream is TCP it is a connection of its own, to the endpoint of [3.7f](#37-the-bootstrap-window), and it carries one bootstrap attempt and nothing else.
- **(11.3c) MUST** An acceptor closes the connection **without reply** if its first frame is not a well-formed `bs_offer`. This is the mirror of [`PPCP-ENC` 2.1c](ppcp-encoding.md#21-binding-streams-to-a-link) for the other protocol, and it is what makes the separation at the port of [3.7f](#37-the-bootstrap-window) reliable rather than merely tidy. Where the first frame **is** a well-formed `bs_offer` and no window is open, the acceptor replies `bs_abort` / `window_closed` and closes: the line between the two is whether the counterpart has already demonstrated it speaks this protocol. Something that has not gets nothing to learn from; something that has is far more likely a peer racing a window that has just closed than an attacker, and it is owed a diagnostic its user can act on.
- **(11.3d) MUST** An acceptor runs **at most one** bootstrap attempt at a time and refuses a concurrent one with `bs_abort` / `window_closed`. Serialising is what makes the single-attempt bound of [3.7b](#37-the-bootstrap-window) mean what [§11.8](#118-what-the-comparison-proves) says it means; an acceptor that ran ten attempts in parallel would offer an attacker ten draws against one operator confirmation.
- **(11.3d1) MUST** *Erratum E35, 24 August 2026 — finding R-02, PinPointStudio.* An **initiator** runs at most one bootstrap attempt at a time, and MUST NOT display digits for more than one attempt. Where several bootstrap instances are discovered, the user selects one **before** the attempt begins — [3.3f](#33-txt-record)'s `dl` is what that selection is made on — and a second attempt requires the first to have ended.

  [11.3d](#113-roles-and-the-connection)'s reasoning applies unchanged to the initiator and was stated only for the acceptor, which left the more dangerous half open. **The natural implementation is the one that breaks it**: CR-01's motivating scenario is a range operator with several bays, [3.3f](#33-txt-record) added `dl` precisely so a browsing peer that sees four open windows can tell them apart, and the obvious host interface is a list of discovered windows — so a peer that dialled several to show the operator a list of candidate numbers would have done nothing this document forbade. The cost is that an attacker advertising N windows gets **N independent blind draws against one honest confirmation**, and worse, **the operator does the selecting**: shown a list of numbers one of which matches the phone in their hand, an operator taps the match and reads it as success. At twenty bays that is a factor of twenty; at a thousand advertised windows it is a one-in-a-thousand attack arising from a user-interface decision.

  This makes `dl` **load-bearing rather than merely convenient**, which is worth saying out loud given [3.3g](#33-txt-record) admits it as a privacy trade: the selection has to happen *before* the digits exist, so the operator needs something to select on. That strengthens the case 3.3g already makes.
- **(11.3e) SHOULD** An attempt that has not reached [11.5f](#115-the-exchange) within 30 seconds is aborted, and one awaiting a user's affirmation is aborted after 60. The window's own bound is [3.7b](#37-the-bootstrap-window)'s 180 seconds and it binds regardless.

### 11.4 Frames

- **(11.4a) MUST** A bootstrap frame is framed exactly as [`PPCP-ENC` §3](ppcp-encoding.md#3-framing) — the 8-byte header, then a deterministically encoded CBOR map ([4.3a](#43-payload)) — with the header's `channel` byte set to **`255`**, which [`PPCP-ENC` 2a](ppcp-encoding.md#2-channels) reserves and no PPCP channel may use.

  The reuse is deliberate and is [A4](#annex-a--decisions-and-alternatives)'s reasoning applied a second time: a peer implementing this document already has this parser and this encoder, and needs no second one. The reserved channel byte is what makes a misdirected frame fail closed rather than be half-understood — a bootstrap frame arriving on a PPCP link is rejected by [`PPCP-ENC` 2c](ppcp-encoding.md#2-channels), and a PPCP frame arriving on a bootstrap connection is rejected by [11.3c](#113-roles-and-the-connection) or [11.4c](#114-frames).

- **(11.4b) MUST** Every frame carries `ty`, an unsigned integer naming its type. `bs_offer` and `bs_accept` also carry `v`, the **bootstrap format version**, which this document defines as `1` and which is unrelated to the PPCP wire version — that is negotiated in `hello`, inside TLS, after the pairing exists.

| `ty` | Frame | Sent by | Fields |
|---|---|---|---|
| `1` | `bs_offer` | initiator | `v` uint, `ct` bstr(32) |
| `2` | `bs_accept` | acceptor | `v` uint, `pk` bstr(32) |
| `3` | `bs_reveal` | initiator | `pk` bstr(32) |
| `4` | `bs_confirm` | either | `mac` bstr(16) |
| `5` | `bs_abort` | either | `rc` uint |

- **(11.4c) MUST** A peer that receives a frame out of the order of [§11.5](#115-the-exchange), a frame type it does not know, a field of the wrong type or length, or a second frame of a type already received, aborts with `malformed` and closes. It does not attempt recovery: there is one exchange, it is five frames long, and a peer that has lost track of where it is in it has nothing to resynchronise to.
- **(11.4d) MUST** `v` is the **first key** of `bs_offer` and `bs_accept`. This is [4.3b](#43-payload)'s construction rather than a second rule — under RFC 8949 §4.2.1 a one-character key sorts before every two-character key, and `ty`, `ct`, `pk`, `mac` and `rc` are all two — and it is here for [4.2a](#42-version-handling)'s reason: a peer that does not implement a later bootstrap version decodes far enough to say so.
- **(11.4e) MUST** A peer that decodes a `v` it does not implement aborts with `unsupported_version` and reports to its **user** that the counterpart requires a newer version of the application, not a generic failure. This is [4.2b](#42-version-handling) on the other path, and for the same reason: the operator is standing there and can act on it.
- **(11.4h) MUST** *Erratum E34, 24 August 2026 — finding R-01, PinPointStudio.* An initiator **aborts with `unsupported_version` if `bs_accept.v` differs from the `v` it sent**. Where later versions exist, a peer offers the **highest** it implements and MUST NOT propose a lower one than it can reach — [5.2b1](#52-tls-profile)'s rule, for [5.2b1](#52-tls-profile)'s reason, on this path.
- **(11.4h1) MUST** *Erratum E41, 24 August 2026 — finding R-10, PinPointStudio.* `v` is in the range **1..255**, and a `v` outside it is `malformed` under [11.4c](#114-frames). An **acceptor echoes the `v` it received** in `bs_accept`, or aborts with `unsupported_version` under [11.4e](#114-frames) if it does not implement it; it never substitutes a different one. So exactly one `v` is in play on each side of the connection or the exchange has ended, and [11.6c](#116-derivation)'s transcript is unambiguous: **the initiator binds the `v` it sent — equal to the one it received, by [11.4h](#114-frames) — and the acceptor binds the `v` it received, equal to the one it echoed.**

  [11.4b](#114-frames) defines `v` as a CBOR unsigned integer, which reaches 2⁶⁴−1, and [11.6c](#116-derivation) encodes it into the transcript as **one octet**. Today `v` is `1` and the two never disagree; the transcript construction becomes undefined the first time they do. **And which `v` a peer binds was a normative derivation input that the document did not state.** Every consistent reading of it detects the both-directions rewrite [11.4i](#114-frames) exists for, so this closes no hole — but [§10.4](#104-guided-pairing) carries one value of `v` and every reading agrees on it, so the vector cannot catch a disagreement and [RT-18](#9-conformance) would pass two implementations that had chosen differently.
- **(11.4i) MUST** *Erratum E34, 24 August 2026 — finding R-01, PinPointStudio.* `v` is **bound into the derivation** ([11.6c](#116-derivation)) along with both public keys, so that a rewrite surviving [11.4h](#114-frames) changes the digits and fails the MACs rather than passing unnoticed.

**Why `v` needs both clauses, and why this was a real hole.** As first written, `v` was carried, checked against the reader's own capability by [11.4e](#114-frames), and bound to nothing. `1` is the only value today, so it was latent — but the moment a `v: 2` exists, an active attacker rewrites `bs_offer.v` to `1` in flight, the v2 acceptor implements v1 and proceeds, and 11.4e never fires at the initiator because the `v` coming back is one it does implement. Both peers run v1 believing that is all the other could reach, and **neither the SAS nor the MACs detect it**, because as first written both descended from `Z` alone.

[11.4h](#114-frames) alone is not sufficient: an attacker rewriting **both** directions — `v: 2` down to `1` outbound, `1` back up to `2` inbound — passes the echo check while leaving the two peers deriving under different versions. [11.4i](#114-frames) is what closes that, and it closes it twice over: the digits differ, so the operator sees it before anyone confirms, and the MACs differ, so the peers see it if the operator does not.

[11.6g](#116-derivation) argues that a first-contact handshake with an agility mechanism is a first-contact handshake with a downgrade attack, and [11.4b](#114-frames) then introduced exactly such a mechanism and left it unprotected. `sas_raw` already bound `pk_i || pk_a` for this reason — [11.6c](#116-derivation)'s own note that *"`Z` alone would not say whose keys produced it"* — and the argument was not carried across to `v` or to the confirmation. It is now carried to both.

**Abort reason codes.** `rc` is one of:

| `rc` | Meaning |
|---|---|
| `1` | `unsupported_version` — `v` not implemented ([11.4e](#114-frames)) |
| `2` | `commitment_mismatch` — the revealed `pk` does not hash to `ct` ([11.5d](#115-the-exchange)) |
| `3` | `invalid_key` — the key agreement produced an all-zero output ([11.6b](#116-derivation)) |
| `4` | `rejected` — the user declined, or a confirmation MAC did not verify |
| `5` | `timeout` — [11.3e](#113-roles-and-the-connection) |
| `6` | `window_closed` — no window open, or one attempt already running ([11.3d](#113-roles-and-the-connection)) |
| `7` | `malformed` — [11.4c](#114-frames) |

- **(11.4f) MUST** A user's refusal and a failed confirmation MAC are reported with the **same** code, `rejected`, and are indistinguishable to the counterpart. This is [7.7c](#77-what-must-never-cross-an-unauthenticated-channel)'s principle on this path, narrowed to the one pair of cases where it bites — the other codes describe the peer's own state before any secret exists and reveal nothing.

  *Erratum E37, 24 August 2026 — finding R-04, PinPointStudio. The clause stands; the reasoning under it was inverted and is replaced.* It read that a failed MAC means an attacker forged one or an implementation is wrong. **An interposed attacker cannot fail this MAC.** It holds `Z` on both legs, therefore `K_c` on both, and forges both MACs correctly and trivially — that is what it means to have won the comparison. A MAC failure is therefore *evidence that no such attacker is present* and that something else is wrong: overwhelmingly an implementation disagreement, which is the `PRK`-divergence class [§10.4](#104-guided-pairing)'s own commentary warns about one step earlier.

  **The MAC is not an authentication check.** The comparison is the authentication; the MAC is an agreement-and-liveness proof — that both ends reached the same `Z`, and that both users actually acted. Getting that backwards would lead an implementer to weigh the MAC as the security boundary and the digits as ceremony, which is the exact inversion [11.1d](#111-what-this-path-is-and-the-one-thing-it-cannot-be) exists to prevent.
- **(11.4g) MUST NOT** `bs_abort` carry any detail beyond `rc` — no message, no diagnostic string, no peer name.

### 11.5 The exchange

Five frames. Read the order as load-bearing: **the acceptor reveals its key having seen only a commitment to the initiator's**, and that is the whole of what stops an attacker choosing the digits.

```
  initiator                                        acceptor
      |                                                |
      |  1.  bs_offer   { v: 1, ct }                   |     ct = SHA-256("ppcp1 bs-commit" || pk_i)
      | ---------------------------------------------> |
      |                                                |
      |  2.  bs_accept  { v: 1, pk: pk_a }             |     acceptor reveals first
      | <--------------------------------------------- |
      |                                                |
      |  3.  bs_reveal  { pk: pk_i }                   |     acceptor checks ct
      | ---------------------------------------------> |
      |                                                |
      |        both derive Z, BK, SAS, K_c  (11.6)     |
      |        both DISPLAY the six digits             |
      |        each waits for its own user  (11.7)     |
      |                                                |
      |  4.  bs_confirm { mac: mac_i }                 |
      | ---------------------------------------------> |
      |  4.  bs_confirm { mac: mac_a }                 |
      | <--------------------------------------------- |
      |                                                |
      |        both verify; pairing exists (11.6e)     |
```

- **(11.5a) MUST** Each peer draws a **fresh X25519 keypair** ([RFC 7748](https://www.rfc-editor.org/rfc/rfc7748)) from a CSPRNG for **every attempt**, uses it for that attempt only, and never reuses or persists it. A reused ephemeral is not ephemeral, and on this path it would let an attacker who obtained one private key impersonate that peer at every future first pairing it ever attempts.
- **(11.5b) MUST** The initiator sends `bs_offer` carrying `ct = SHA-256("ppcp1 bs-commit" || pk_i)`, and does **not** send `pk_i` in it.
- **(11.5c) MUST** The acceptor replies `bs_accept` carrying `pk_a`. It MUST NOT have seen `pk_i` at this point, and an implementation that sends `pk_a` only after receiving `pk_i` — a natural-looking reordering that saves a round trip — **destroys the security of this path entirely** ([§11.8](#118-what-the-comparison-proves)).
- **(11.5d) MUST** The initiator sends `bs_reveal` carrying `pk_i`. The acceptor recomputes `SHA-256("ppcp1 bs-commit" || pk_i)`, compares it to the `ct` it received **in constant time**, and aborts with `commitment_mismatch` on any difference. It MUST NOT derive anything from a `pk_i` that failed this check.
- **(11.5e) MUST** Both peers then derive ([§11.6](#116-derivation)), display the six digits, and each waits for **its own** user ([§11.7](#117-the-short-authentication-string)). Neither sends `bs_confirm` before its own user has affirmed.
- **(11.5f) MUST** Each peer sends `bs_confirm` carrying its own MAC and verifies the counterpart's **in constant time**:

```
mac_i = HMAC-SHA256(K_c, "ppcp1 bs-confirm-i")   first 16 bytes   (sent by the initiator)
mac_a = HMAC-SHA256(K_c, "ppcp1 bs-confirm-a")   first 16 bytes   (sent by the acceptor)
```

  The two labels differ so that neither MAC can be reflected back at its own sender by a relay that has nothing else to send. A peer that receives its own MAC value aborts with `rejected`.

- **(11.5g) MUST** The pairing exists only when a peer has **both** affirmed at its own end and verified the counterpart's MAC. Until then it holds nothing and MUST NOT persist, advertise, or offer anything derived from the exchange.
- **(11.5h) MUST** The bootstrap connection is closed once both MACs have verified. It is not reused, not upgraded in place, and not held open — the peers reconnect under [§5](#5-rv-3--key-derivation-and-tls), in whichever direction [11.2b](#112-why-it-is-not-tls-and-what-that-unlocks) puts them.

**Why not upgrade the connection in place**, since it is already open and both ends now hold a key. Because [§5](#5-rv-3--key-derivation-and-tls) would then have two shapes — one where TLS is negotiated on a fresh connection and one where it is layered onto a live plaintext stream — and the second is a new attack surface, a second code path in every implementation, and a second thing [2c](#2-rendezvous-paths) has to be read against. The cost of not doing it is one TCP connection setup, once, at pairing time, while an operator is watching a screen. It is not a cost worth a second shape of `§5`.

### 11.6 Derivation

- **(11.6a) MUST** `Z = X25519(own private key, counterpart public key)`, 32 octets, as [RFC 7748 §5](https://www.rfc-editor.org/rfc/rfc7748#section-5).
- **(11.6b) MUST** *Amended by erratum E36, 24 August 2026 — findings R-03 (PinPointStudio) and F-R9-1 (PinPointCapture), independently measured.* A peer aborts with `invalid_key` and derives nothing where the key agreement **fails, or produces an all-zero `Z`**. This is [RFC 7748 §6.1](https://www.rfc-editor.org/rfc/rfc7748#section-6.1)'s check, and it is what makes a small-order public key a failed handshake rather than a shared secret an attacker chose. **A peer MUST NOT treat such a failure as a transport error and MUST NOT retry it.**

  The clause first named only the zero output, and **neither implementation's library ever produces one**. Both teams measured it, on different libraries, and got the same answer: OpenSSL 3.6.3 fails `EVP_PKEY_derive` for each of the five standard small-order u-coordinates, and CryptoKit's `sharedSecretFromKeyAgreement` throws `underlyingCoreCryptoError(-7)` rather than returning zeros. Most X25519 implementations perform RFC 7748 §6.1's check internally and report a failure. So an implementer following the clause literally writes a zero-check that can never fire, believes the case is defended, and leaves the branch that **does** fire on the generic error path — where it is reported as a network fault and, on a careless implementation, reads an uninitialised buffer.

  The clause was right about what must happen and named the wrong observable. **This is [E23](#errata-after-revision-8)'s shape exactly** — a clause phrased around an API behaviour a platform does not expose — arriving in a section written by someone who had just finished documenting E23. The retry prohibition is PinPointCapture's addition and is the part with teeth: a rejected key is an attack signal, and a retry loop around it interacts badly with [3.7b](#37-the-bootstrap-window)'s single-attempt bound, which is what [§11.8](#118-what-the-comparison-proves)'s whole argument rests on.
- **(11.6c) MUST** From `Z`:

```
transcript = v || pk_i || pk_a          v as ONE octet; each pk 32 raw octets, initiator first

BK      = HKDF-Extract(salt = "ppcp1 bootstrap", IKM = Z)
sas_raw = HKDF-Expand(BK, "ppcp1 sas"        || transcript,  4)
K_c     = HKDF-Expand(BK, "ppcp1 bs-confirm" || transcript, 32)
```

  where the info strings are the ASCII bytes of the quoted label with no terminator, followed by the transcript octets. **Both of these expansions bind everything that varies between two otherwise-identical exchanges** — one construction rather than two, which is the shape an implementer is least likely to get partly right. `Z` alone would say neither **whose** keys produced it nor **under which version**, and both bindings are what [§11.8](#118-what-the-comparison-proves) rests on.

- **(11.6c1) MUST NOT** *Erratum E40, 24 August 2026 — finding R-09, PinPointStudio.* The transcript be bound into [11.6d](#116-derivation)'s `sid` or [11.6e](#116-derivation)'s `PRK`. **Those two are functions of `Z` alone, deliberately**, and an implementation that binds the transcript into either does not interoperate with one that does not.

  The clause is here because the sentence above it, as first written, generalised to *everything derived* and [11.6d](#116-derivation) and [11.6e](#116-derivation) are the two clauses it was not true of. An implementer following the rule instead of the formulas — which is exactly what the sentence invited, since it was offered as the safer thing to hold in mind — produces **matching digits**, **matching MACs**, and a **divergent `PRK`**: a successful comparison, a successful confirmation, and then a TLS handshake that fails with `PSK_IDENTITY_NOT_FOUND`. That is the failure [§10.4](#104-guided-pairing) names as the one that matters and warns *will be diagnosed as* the [3.5d](#35-who-advertises-and-who-browses) platform limitation. [RT-18](#9-conformance)'s `PRK` row catches it, which is an argument for stating the boundary rather than for relying on the test.

  **Why the boundary falls where it does.** By the time `PRK` is derived the exchange has already been authenticated — by the comparison, and by the MACs — so binding the transcript again adds nothing: a transcript that differed was caught two steps earlier. `Z` already commits to both public keys by construction ([11.6b](#116-derivation) having refused the small-order cases where it would not), so the only element not implied by `Z` is `v`, and `v` is agreed by [11.4h](#114-frames) or the exchange has aborted. And [§5.1](#51-key-derivation) is taken **verbatim** by [11.6e](#116-derivation): changing its inputs from this section would give §5.1 a second shape, which is [A17](#annex-a--decisions-and-alternatives)'s argument for tearing the bootstrap connection down rather than upgrading it, arriving one layer lower.

  *Erratum E34 — finding R-01, PinPointStudio.* As first written, `sas_raw` bound the two public keys and `K_c` bound nothing; `v` was bound nowhere. See [11.4h and 11.4i](#114-frames) for the attack that closed, and note that binding `v` into `sas_raw` as well as into `K_c` goes one clause further than R-01 asked: R-01's second clause alone would have caught a version rewrite at the MAC, **after** the operator had compared matching digits and affirmed them. Catching it in the digits puts the signal in front of the human, where [§11.8](#118-what-the-comparison-proves) says the authentication actually lives.

- **(11.6d) MUST** The session identifier is derived rather than exchanged:

```
sid = HKDF-Expand(BK, "ppcp1 bootstrap-sid", 16)          no transcript — see 11.6c1
```

  with octet 6 then set to `(octet6 & 0x0f) | 0x40` and octet 8 to `(octet8 & 0x3f) | 0x80`, so that `sid` is a well-formed version 4 UUID as [4.3e](#43-payload) requires. **The bits are set before `sid` is used for anything**, including as the salt of [11.6e](#116-derivation). Deriving it costs no round trip and puts no value on the plaintext connection that a later observation could be correlated against.

- **(11.6e) MUST** The pairing key material is then [§5.1](#51-key-derivation)'s, unchanged:

```
PRK   = HKDF-Extract(salt = sid, IKM = Z)                 no transcript — see 11.6c1
K_tls = HKDF-Expand(PRK, "ppcp1 tls-psk",        32)
K_id  = HKDF-Expand(PRK, "ppcp1 rendezvous-id",  32)
```

  [§5.1](#51-key-derivation) is not amended by this section and does not need to be: it takes an input keying material and a salt, and this path supplies `Z` and a derived `sid` where the code path supplies `psk` and a printed one. [5.1c](#51-key-derivation)'s rule that a persisting peer persists `PRK` and never the original secret is satisfied here by construction — there is no original secret to persist, only an ephemeral one that [11.6f](#116-derivation) erases.

- **(11.6f) MUST** A peer erases its ephemeral private key, `Z`, `BK` and `K_c` when the handshake ends, **whether it succeeded or failed**, and they appear in no log or export ([7.2e](#72-handling-the-pairing-secret)). What survives a successful handshake is `PRK` and what [§5.1](#51-key-derivation) derives from it; what survives a failed one is nothing.
- **(11.6g) MUST** An implementation MUST NOT substitute a different curve, a different KDF, or a different label. There is no negotiation on this path and none is wanted: a first-contact handshake with a cryptographic-agility mechanism is a first-contact handshake with a downgrade attack, and the values here are fixed for the same reason [§4](#4-rv-2--the-pairing-code)'s are.

### 11.7 The short authentication string

- **(11.7a) MUST** The displayed value is `sas_raw` read as a **big-endian unsigned 32-bit integer, modulo 1 000 000**, rendered as exactly **six decimal digits with leading zeros**. `000042` is a valid string and MUST be shown as six characters.
- **(11.7b) MUST** Both peers display it. A peer that cannot display six digits to a user MUST NOT implement this path — there is no headless guided pairing, because the comparison is the authentication and a peer with no screen has no way to be compared.
- **(11.7c) MUST** Each peer obtains an affirmative act from **its own** user before sending `bs_confirm`. A single affirmation at one end does not establish a pairing at the other ([11.1b](#111-what-this-path-is-and-the-one-thing-it-cannot-be)), and a peer MUST NOT treat the arrival of the counterpart's `bs_confirm` as standing in for its own user's.
- **(11.7d) SHOULD** The digits are presented so that comparison is the obvious act and acceptance is not the default: both peers group them identically — `313 164` — the affirmative control is not pre-selected and not the one a stray tap reaches, and the prompt asks whether the numbers **match** rather than whether to trust or continue. A dialogue whose default is *Continue* is a dialogue that authenticates whatever is on the other end.
- **(11.7e) MUST NOT** A peer display any part of the digits, or any control that affirms them, before it has completed [11.5d](#115-the-exchange). There is nothing to compare before then, and a progressive display would leak the value to whichever side an attacker reached first.
- **(11.7f) MUST NOT** The digits be reused, cached, or shown again after the attempt ends. They are a function of two ephemeral keys and are meaningless outside the attempt that produced them.

**The modulo bias is real, immaterial, and recorded here so it is not raised twice more.** 2³² is not a multiple of 10⁶, so **967 296** of the residues have 4 295 preimages and the remaining **32 704** have 4 294 — an excess over uniform of **7.6 × 10⁻⁶** for the most probable string, and a factor of **1.000 23** between the most and the least probable. It cannot be steered by an attacker who commits blind ([§11.8](#118-what-the-comparison-proves)) and it does not move the 2⁻²⁰ bound by anything worth writing down.

*Erratum E42, 24 August 2026 — finding R-07, PinPointStudio.* This paragraph carried three wrong numbers until now, and its stated purpose is that *the third reader does not spend the same afternoon*: `295 967 296` is not a possible count of residues when there are only 10⁶ of them, the favoured set is the **large** one rather than *"the rest"*, and the excess was given as 2.3 × 10⁻⁷ — **thirty-three times smaller than it is**. The 1.000 23 figure was right and was attached to the wrong quantity: it is the most-to-least ratio, not the excess over uniform. The conclusion is unchanged in every respect, which is why it took a second pass to notice. PinPointStudio reported the error against its own pass-1 figure, which is where two of the three came from.

**Six digits, and why not more.** Twenty bits is what Bluetooth numeric comparison and [ZRTP](https://www.rfc-editor.org/rfc/rfc6189) both settle on, and the reasoning is not that 2⁻²⁰ is negligible in the abstract — it is that the attacker gets **one** draw and a failed draw is seen by a human. Lengthening the string buys little against an attacker already limited to one attempt and costs a great deal against the operator, who is the component most likely to fail: a person asked to compare ten digits at a range, in daylight, forty times a day, stops comparing. [3.7b](#37-the-bootstrap-window)'s single attempt is doing more work here than a longer string would.

### 11.8 What the comparison proves

**The property.** After a successful comparison, both peers hold the same `PRK`, and that `PRK` is shared with **the peer at the other end of the channel the operator's two screens belong to** and with nobody else.

**How the commitment produces it.** An attacker interposed on the bootstrap connection must run two exchanges — one with the initiator, one with the acceptor — and must make both display the same six digits, because the operator will compare them. The digits on each leg are `HKDF(Z, "ppcp1 sas" || v || pk_i || pk_a)` for that leg's key pair. To force a collision the attacker would need to choose one of its two keys **after** seeing the honest key it is paired against, and grind candidates until the two legs agree; 2²⁰ trial keys is seconds of work.

[11.5b](#115-the-exchange) and [11.5c](#115-the-exchange) are what deny it that. The initiator commits to `pk_i` before seeing anything; the acceptor reveals `pk_a` having seen only a hash. So on the acceptor's leg the attacker must choose its key before learning `pk_a`, and on the initiator's leg it is bound by a commitment it made before learning `pk_i`. Neither leg's digits can be steered. The attacker's best strategy is to pick both keys blind and hope the two legs collide — **one chance in 1 048 576** — and a miss is a mismatch on two screens with an operator looking at both.

**What bounds the retries, which is the part that matters more than the number.** A miss must cost the attacker the attempt. [3.7b](#37-the-bootstrap-window) closes the window on an abort or a rejection, [3.7a](#37-the-bootstrap-window) requires a fresh user action to reopen it, and [11.3d](#113-roles-and-the-connection) forbids concurrent attempts. Together those make the attacker's expected work *one million operator confirmations*, not one million packets. Remove any one of the three and the attack becomes a loop.

**What it does not prove**, stated so it is not assumed:

- **It does not prove which device is at the other end** — only that the channel has two ends and the operator saw both. An operator who compares the bay's screen against the wrong bay's phone has authenticated a channel to the wrong bay, correctly. `dl` ([3.3f](#33-txt-record)) exists to make that unlikely; the comparison is what makes it *survivable*, because the wrong bay's digits will not match the one the operator is reading.
- **It does not survive an operator who does not compare.** [§7.1](#71-threat-model) says so in its *not defended* table, and [11.7d](#117-the-short-authentication-string) is the most that a specification can do about it.
- **It does not survive a weak ephemeral.** *Erratum E38, 24 August 2026 — finding F-R9-2, PinPointCapture.* The confirmation MACs descend from `Z` by public functions, so **a recorded transcript is an offline verifier for `Z`** — an observer can test any candidate shared secret, and therefore any candidate ephemeral private key, without further interaction. Against a CSPRNG that is worth nothing; X25519 has no useful search space. What it changes is the cost of a **weak or backdoored RNG**: without the transcript a bad ephemeral is exploitable only by an attacker present at the time, and with it a passive observer who recorded the exchange in March recovers the `PRK` in June and reads every session keyed from it. [11.6f](#116-derivation)'s erasure does not mitigate this and neither peer retains anything that would reveal it had happened. This is [7.2a](#72-handling-the-pairing-secret)'s *"a predictable secret defeats the entire model, and it is the single easiest thing to get wrong"* arriving on this path, and it is the real force of [11.5a](#115-the-exchange)'s MUST — which until now was justified only by an impersonation argument.
- **It confers no forward secrecy on what follows.** The bootstrap exchange is itself ephemeral, but the `PRK` it produces persists, and every session keyed from it is bound by [§5.4.3](#543-the-decision) exactly as a code-established pairing is. A guided pairing removes the photographable secret ([§7.1](#71-threat-model)); it does not remove the stored one.

### 11.9 Aborting, and the one-attempt rule

- **(11.9a) MUST** Any abort — a mismatch, a user's refusal, a failed MAC, a timeout, a malformed frame, a closed connection — ends the attempt, closes the window ([3.7b](#37-the-bootstrap-window)), and leaves **no** pairing at either peer.
- **(11.9b) MUST NOT** A peer reopen the window without a further explicit user action ([3.7a](#37-the-bootstrap-window)). It MUST NOT retry automatically, offer a *try again* control that reopens without that action, or keep the window open across a failure.
- **(11.9c) MUST NOT** A peer report an abort to its user in terms that invite a retry as the obvious next step where the cause was a **mismatch or a MAC failure**. Those two mean either an implementation is wrong or someone is on the link, and *"the numbers did not match — do not retry until you know why"* is the honest message. A timeout or a closed connection carries no such implication and may be reported as the ordinary failure it is.
- **(11.9d) SHOULD** A peer that has aborted twice in one sitting offers the pairing code ([§4](#4-rv-2--the-pairing-code)) instead. The code path is required of every implementation ([2a](#2-rendezvous-paths)), it does not depend on multicast, and it is the answer to both of the plausible causes.

11.9c is unusual for a specification to state and it is here because the alternative is worse. Every other failure in this document is a network problem, and users learn from those that retrying is what one does. A mismatch is the **one** signal this path produces that an attack is under way, and a peer whose dialogue makes retrying the reflex has converted its single-attempt bound into an unbounded one by way of the operator's muscle memory.

### 11.10 What must not cross a bootstrap connection

- **(11.10a) MUST NOT** A PPCP message, a `Peer.id`, a device or user name, a Source list, a capability, a session identifier, a stored-session count, or any value that persists across attempts cross a bootstrap connection. The five frames of [§11.4](#114-frames) are its entire vocabulary. [7.6a](#76-peer-identity) and [7.7b](#77-what-must-never-cross-an-unauthenticated-channel) bind it unchanged, and `Peer.id` is still first disclosed in `hello`, inside TLS ([7.6b](#76-peer-identity)), after the pairing exists.
- **(11.10b) MUST NOT** Any value from a pairing the peer already holds — a `PRK`, a `K_id`, an `rid`, a `psk`, a `sid` — cross a bootstrap connection or influence any value on it. A guided pairing knows nothing about what either peer has paired with before, and that is what keeps it from becoming an oracle for [§3.4](#34-resolvable-identifiers)'s identifiers.
- **(11.10c) MUST** A peer treats everything it received on a bootstrap connection as spent when the connection closes. Nothing from it is persisted except the `PRK` of [11.6e](#116-derivation), and only after [11.5g](#115-the-exchange).

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
| **A14** | **A compared six digits, not a transferred short code** ([§11.7](#117-the-short-authentication-string)) | A short-code PAKE — SPAKE2 or CPace over four or six digits the acceptor displays and the initiator's user types | A PAKE is the stronger primitive and it solves a problem CR-01 did not raise: it still transfers a value between two screens, which is the exact act the request asked to remove. Comparison needs no keyboard, no camera and no line of sight, and against an attacker bounded to one attempt the two reach the same place. A PAKE also brings a password-guessing surface and a hash-to-curve dependency; the comparison brings neither. **If a future deployment wants pairing with only one screen — a headless capture peer — a PAKE is the answer and this decision reverses**, because [11.7b](#117-the-short-authentication-string) forbids that case outright. |
| **A15** | **Commit, then reveal** ([11.5b](#115-the-exchange), [11.5c](#115-the-exchange)) | A plain two-message ephemeral exchange, one round trip shorter | Without the commitment an interposed attacker chooses its key after seeing the honest one and grinds 2²⁰ candidates until both legs display the same digits — seconds of work, and the comparison then proves nothing. The commitment is the entire security of the path and it costs one frame. It is also the clause most likely to be "optimised" back out by an implementer counting round trips, which is why [11.5c](#115-the-exchange) says what removing it does. |
| **A16** | **X25519, fixed, with no negotiation** ([11.6g](#116-derivation)) | Offer a curve list, or use P-256, which more platforms expose through a general-purpose crypto interface | A first-contact handshake with an agility mechanism is a first-contact handshake with a downgrade attack, and there is no PSK here to authenticate the transcript with — the whole point of the path. One curve, stated. X25519 over P-256 because it has no invalid-curve class of failure and both platforms in this deployment expose it. **[B14](#annex-b--open-issues) gated that on a measurement rather than an assumption**, which is what [5.4b](#54-resolved-the-mechanism) had to learn the hard way, and both teams have now discharged it. ⚠ **The clause as first written said *"the host's crypto library"*, and one of the two implementations has none**: `libppcp` carries SHA-256, HMAC and HKDF in-library precisely so it depends on nothing, and X25519 — constant-time field arithmetic over 2²⁵⁵−19 — is the one primitive in [§11](#11-rv-6--guided-pairing) that such a library should neither hand-roll nor vendor. See [B17](#annex-b--open-issues): the primitive is expected to be **supplied by the embedding**, which is a shape this protocol set already has for the CSPRNG that [7.2a](#72-handling-the-pairing-secret) makes the embedding's obligation. |
| **A17** | **A bootstrap connection of its own, torn down before [§5](#5-rv-3--key-derivation-and-tls)** ([11.5h](#115-the-exchange)) | Upgrade the live plaintext connection to TLS-PSK in place once both ends hold the key | The upgrade saves one TCP setup, once, at pairing time, and costs `§5` a second shape — one where TLS is negotiated on a fresh connection and one where it is layered onto a stream that has already carried plaintext. That is a second code path in both implementations, a second thing to read [2c](#2-rendezvous-paths) against, and a new place for a downgrade to hide. Tearing down also makes [11.2b](#112-why-it-is-not-tls-and-what-that-unlocks)'s role swap natural rather than awkward. |
| **A13** | **`mu > 1` is session-scoped and never persisted** ([7.4f](#74-persistent-pairings)) | Remove `mu` entirely, so every pairing is pairwise | Multi-device pairing is a real workflow and displaying three codes is worse ergonomics for no gain. Bounding the shared credential to one session keeps the workflow and removes the permanent exposure. |

---

## Annex B — Open issues

| # | Issue | Status |
|---|---|---|
| **B13** | **Whether the absence of forward secrecy should be user-visible.** A peer at the TLS 1.2 floor has *every* session without it, not some. 5.4k now makes the outcome readable, so a peer *can* say; nothing says whether it *should*. This is a product question for the implementation teams rather than a protocol one, and it is recorded so it is decided rather than omitted. | Open — not the protocol's to answer. |
| **B1** | **Draft 5 carries three review passes.** [§4](#4-rv-2--the-pairing-code), [§6](#6-rv-4--network-join) and [§7](#7-rv-5--security-model) are approved without reservation by both teams; §4 has survived three passes and three independent recomputations. Every remaining finding has been in **§5**, and every one of them was a consequence of the relaxation not carried into the clause next door. | Open — a fourth pass on [§5](#5-rv-3--key-derivation-and-tls) only. |
| **B2** | **`mu` greater than one has no revocation story**, and the peers that scanned it share key material. The sharing is now bounded — [7.4f](#74-persistent-pairings) forbids persisting such a pairing and [§7.1](#71-threat-model) names the impersonation exposure — but a publisher still cannot withdraw a live multi-use code from the second and third holder. **Per-peer re-keying inside the channel ([7.4g](#74-persistent-pairings)) is the fix and is unspecified.** | Open. Both publishers intend to emit `mu: 1` only until it exists. |
| **B3** | **A peer holding several persisted pairings advertises only one** ([§3.4d](#34-resolvable-identifiers)). Advertising several — as repeated keys, or as several service instances — leaks the count. Rotating through them delays reconnection. | **Narrowed by erratum E27**: one instance at a time, rotating on the 15-minute `rn` rotation, and a multi-pairing peer browses as well ([3.4d1](#34-resolvable-identifiers), [3.4d2](#34-resolvable-identifiers)). The rejected halves — repeated keys, several instances — stay unspecified and stay open, because both leak the count. |
| ~~**B4**~~ | ~~Expiry depends on two wall clocks.~~ | **Closed in Draft 2.** The publisher enforces `exp` ([7.3e](#73-single-use-and-expiry)) because it holds the authoritative clock, and a peer that cannot trust its own attempts rather than refuses ([4.4a1](#44-handling-a-scanned-code)). |
| **B5** | **No pairing-time transport negotiation.** The code carries endpoints and a port, so a publisher offering both a tunnel and a network connection must display a code per transport or list both as endpoints. Whether that is sufficient is untested. | Open. |
| ~~**B6**~~ | ~~The identity is `sid`-bound.~~ | **Closed in Draft 2**, and it was not an aesthetic issue: a persisted pairing broadcast a fixed sixteen bytes in the clear on every reconnection, undoing [§3.4](#34-resolvable-identifiers). The identity is now resolvable and rotates ([§5.3a](#53-psk-identity)). |
| ~~**B8**~~ | ~~TLS 1.3 external PSK unreachable; device confirmation outstanding.~~ | **Closed in Draft 6.** The measurement was repeated on an iPhone 16 on the release OS and is identical to the desktop result ([5.4b1](#541-what-was-measured)). The premise is confirmed, the relaxation stands, and no clause changed — which is how 5.4b was worded. |
| **B12** | **Forward secrecy is now best-effort, and nothing replaces it on the leg that lacks it.** A per-session **ratchet** — re-deriving `PRK` at each session close and erasing its predecessor — would restore the property for every session after the first, at the cost of persistent state that both ends must keep in step and recover from when they fall out of it. It is not specified, and it is the cheapest route back to property 2 without changing the transport. | Open — worth revisiting if the payload is ever reassessed, or if a peer wants it independently. |
| **B9** | **`role` in a TXT record is unverified before pairing.** A peer advertising `role: host` is taken at its word by a browser deciding whether to dial. It costs only a wasted connection — the handshake authenticates — but a browser should not treat it as more than a filter hint. | Open. |
| **B17** | **[§11](#11-rv-6--guided-pairing) needs a primitive the protocol set has never needed before, and an implementation may have nowhere to put it.** SHA-256, HMAC and HKDF are short enough that a dependency-free library implements them and a reader checks them. X25519 is not. An implementation whose licence or dependency constraints forbid a crypto library — `libppcp`'s do — can neither vendor it nor safely write it. | **Open. The seam, not the primitive, is what needs specifying.** The expected shape is the one already used for the CSPRNG: the implementation owns the framing, the commitment, the HKDF chain, the SAS and the MACs, and **takes key agreement from its embedding** — thirty lines of OpenSSL on one side, `Curve25519.KeyAgreement` on the other. That is an API question for each implementation rather than a wire question, so no clause here constrains it; it is recorded because [A16](#annex-a--decisions-and-alternatives) is where the wrong assumption was written down (PinPointStudio, CR-01 review). |
| **B14** | **X25519 reachability through both platforms' public interfaces is assumed, not measured.** [11.6a](#116-derivation) mandates it and [A16](#annex-a--decisions-and-alternatives) argues for it on the belief that CryptoKit's `Curve25519.KeyAgreement` and the host's crypto library both expose raw X25519 with no TLS involved. That belief is the same *shape* as the one [§5.4](#54-resolved-the-mechanism) held about TLS 1.3 external PSK for four drafts before anyone ran it. | **Discharged on both sides, with one run outstanding.** PinPointStudio measured it against OpenSSL 3.6.3 on hardware; PinPointCapture measured it against CryptoKit on macOS 27.0 and the **iOS simulator SDK**, and reports — unprompted — that the device run has not been done and that this review does not claim it has. **That run is required before either implementation ships a guided pairing**, which is [5.4b](#54-resolved-the-mechanism)'s rule and [5.4b](#54-resolved-the-mechanism)'s reason: this document has already paid once for accepting a desktop proxy for a device measurement. It does not gate writing the code, because a negative result reopens [A16](#annex-a--decisions-and-alternatives) only. PinPointCapture's finding that the analogy to the TLS 1.3 PSK gap does not hold — that one was structural, this is a first-class primitive with raw bytes in and out — is accepted and is the right correction. |
| **B15** | **The fleet case is not served, and its motivation is stronger than CR-01 made it sound.** CR-01's is *"a range operator sets up several bays"*, and [§11](#11-rv-6--guided-pairing) still costs one operator confirmation per device per host. PinPointCapture adds a case the disposition did not anticipate: **multi-device stereo puts two or three phones on one host**, each with a screen — so [11.7b](#117-the-short-authentication-string) is satisfied and the fleet problem arrives anyway, through a different door and on a single bay. What would collapse that to one per device is a venue-scoped enrolment credential — pair once with the venue, connect to any bay in it — and that is a **group credential**, which is exactly what [7.4f](#74-persistent-pairings) forbids persisting and what [B2](#annex-b--open-issues) says has no revocation story. | **Open, and deliberately not attempted here.** Its prerequisite is [B2](#annex-b--open-issues)'s per-peer re-keying inside the authenticated channel, not a fourth rendezvous path. Specifying a venue credential before B2 exists would ship the multi-device exposure [7.4f](#74-persistent-pairings) was written to bound, permanently rather than for one session. |
| ~~**B16**~~ | ~~[§11](#11-rv-6--guided-pairing) has had no implementation review pass.~~ | **Closed 24 August 2026.** Both teams reviewed it and both accept the ruling. Six findings, two of them blocking and both structural — an unbound version field ([E34](#errata-after-revision-9--change-request-cr-01-and-its-review)) and an unserialised initiator ([E35](#errata-after-revision-9--change-request-cr-01-and-its-review)) — and the vectors reproduced byte for byte on both sides by implementations sharing no code, which is what the ask was for. **The pass did exactly what [§4.3b](#43-payload)'s did**: the defects it found were invisible in the worked example and would have been permanent after either team shipped. **A second pass over the amendments then found [E40](#errata-after-revision-9--change-request-cr-01-and-its-review)** — a trap created by the first pass's own fix — which is the argument for reviewing errata and not only the text they amend. |
| **B7** | **Interoperability is untestable until a second implementation exists.** Every test in [§9](#9-conformance) can pass against a single implementation's own assumptions, which is exactly the failure mode [`PPCP-CONF` §5c](ppcp-conformance.md#5-interoperability) records for PPCP itself. | Open — structural. |

---

# Annex C — Change history

*Non-normative. Newest first.*

## The CR-01 second review pass — errata E40–E42

Both teams re-read revision 9 as amended and both **accept and close**. Four findings, none touching the wire, the vectors or the security argument — and one of them is the interesting kind.

**[R-09](#errata-after-revision-9--change-request-cr-01-and-its-review) is a trap [E34](#errata-after-revision-9--change-request-cr-01-and-its-review) created.** E34's binding was summarised as a general rule — *everything that varies between two otherwise-identical exchanges is bound into everything derived from them* — offered explicitly as the thing to hold in mind **instead of** the individual formulas, on the grounds that it was the safer abstraction. It is not true of the two clauses immediately below it. An implementer who trusted the rule would bind the transcript into `sid`, and would then see matching digits, matching MACs, and a `PRK` that diverges — the exact failure [§10.4](#104-guided-pairing) singles out as the one that will be misdiagnosed as a platform limitation.

That is worth more than the fix. **A generalisation offered as a safety aid is a normative statement**, and this one was written in the same erratum that made it false. The correction is not to delete the sentence but to scope it and say where the boundary falls and why — which is [11.6c1](#116-derivation), and which is [A17](#annex-a--decisions-and-alternatives)'s *"one shape of §5 is worth more than one TCP setup"* arriving one layer down.

**[R-10](#errata-after-revision-9--change-request-cr-01-and-its-review) closes no hole and is worth having anyway.** `v` was a CBOR uint in one clause and one octet in another, and *which* `v` a peer binds was never stated. PinPointStudio worked every consistent reading through and all of them detect the attack — but [§10.4](#104-guided-pairing) carries one value of `v`, so the vector agrees with every reading and [RT-18](#9-conformance) would pass two implementations that had chosen differently. **An unstated derivation input that the vector cannot expose is the precise shape of an interoperability failure**, and it is [B7](#annex-b--open-issues)'s point arriving inside a section written after B7.

**[R-07 and R-08](#errata-after-revision-9--change-request-cr-01-and-its-review) are wrong numbers in notes about wrong numbers**, and the way they were found says more than the errors do. Both teams recomputed all fifteen vector rows, twice each, across the E34 boundary — and neither recomputed the arithmetic in the prose sitting beside them, where two figures had been quoted onward from a review in good faith. **Recomputation follows the table; nothing was watching the paragraph.** The E34 warning box did work exactly as designed: both teams' first re-run produced the superseded values and the box is what identified them as stale text rather than as an arithmetic fault — so [RT-18](#9-conformance) now requires a reproduction to record the erratum level it was taken against.

## The CR-01 review pass — errata E34–E39

**Both teams accept the ruling and neither reopened the design.** Six findings, and the two blocking ones are the argument for having asked.

**[R-01](#errata-after-revision-9--change-request-cr-01-and-its-review) — `v` was an unprotected agility mechanism.** [11.6g](#116-derivation) argues, correctly, that a first-contact handshake with cryptographic agility is a first-contact handshake with a downgrade attack — and [11.4b](#114-frames) then introduced a version field, checked it only against the reader's own capability, and bound it into nothing. `sas_raw` already bound both public keys for exactly this reason and the argument was not carried across. Latent while `1` is the only value; permanent after either team ships. Now bound into the digits as well as the MACs, which is one clause further than the finding asked and puts the signal in front of the operator rather than behind the confirmation.

**[R-02](#errata-after-revision-9--change-request-cr-01-and-its-review) — only the acceptor was serialised.** [11.3d](#113-roles-and-the-connection)'s reasoning — *"an acceptor that ran ten attempts in parallel would offer an attacker ten draws against one operator confirmation"* — applies unchanged to the initiator, and the document stated it only once. **The natural implementation is the one that breaks it**: [3.3f](#33-txt-record)'s `dl` was added so a browsing peer that sees four windows can tell them apart, and a host that dials all four to show a list of candidate numbers does nothing the document forbade — while handing an attacker N blind draws with the operator actively finding the collision. Three orders of magnitude at a large venue, from a user-interface decision.

**Both were invisible in [§10.4](#104-guided-pairing) and would have been unfixable after either team shipped**, which is what a review pass is for and is what [§4.3b](#43-payload) established at the cost of finding out.

**Four more, all accepted.** [11.6b](#116-derivation) named an all-zero shared secret that neither OpenSSL nor CryptoKit ever returns — [E23](#errata-after-revision-8)'s shape again, measured on both platforms independently ([R-03](#errata-after-revision-9--change-request-cr-01-and-its-review)/F-R9-1). [11.4f](#114-frames)'s reasoning was inverted: an interposed attacker holds `Z` on both legs and forges both MACs correctly, so a MAC failure means no such attacker is present ([R-04](#errata-after-revision-9--change-request-cr-01-and-its-review)). The plaintext connection's transcript is an offline verifier for `Z`, which changes what a weak RNG costs ([E38](#errata-after-revision-9--change-request-cr-01-and-its-review)). And an implementation claiming §11 must say which **roles** it provides, because one peer here ships initiator-only ([R-05](#errata-after-revision-9--change-request-cr-01-and-its-review)).

**[A16](#annex-a--decisions-and-alternatives) said *"the host's crypto library"* and one implementation has none.** `libppcp` is dependency-free by construction and carries SHA-256, HMAC and HKDF in-library because all three are short enough to read; X25519 is not, and is the one primitive in §11 such a library should neither hand-roll nor vendor. [B17](#annex-b--open-issues) records the seam — key agreement supplied by the embedding, as the CSPRNG already is — rather than inventing an API in a wire specification.

**[B16](#annex-b--open-issues) is closed and [B14](#annex-b--open-issues) is nearly.** Both teams reproduced every §10.4 row byte for byte from implementations sharing no code, and both discharged the X25519 measurement — PinPointStudio on hardware against OpenSSL 3.6.3, PinPointCapture against CryptoKit on macOS and the iOS **simulator**. The device run is outstanding and is required before shipping: [5.4b](#54-resolved-the-mechanism) exists because this document already paid once for accepting a desktop proxy for a device measurement.

**What is still not demonstrated is unchanged.** [RT-20](#9-conformance) needs two real implementations either side of a deliberate relay and neither has written §11 yet. Until it runs, §11 remains a design with vectors — now well-reviewed vectors — and not a demonstrated one.

## Revision 9 — CR-01, guided pairing

**Revision 8 was final and closed with no open findings.** [CR-01](../changerequests/CR-01-in-band-pairing.md) reopened it, correctly: it reported no defect, and asked for a requirement the document had never been asked to serve — a host and a capture peer that have never met reaching a working link without an operator carrying a code between two screens.

**Granted in part.** The transfer goes; the person does not. [§11.1](#111-what-this-path-is-and-the-one-thing-it-cannot-be) states why in one paragraph and it is the only interesting sentence in the ruling: authentication cannot be manufactured from nothing, so a first contact on a hostile network imports its trust from outside the channel or has none. CR-01 §5 says the same thing from the other side and is right to. What [§11](#11-rv-6--guided-pairing) does is make the human act as small as it can be — six digits compared, not carried — and then spend the rest of the section keeping the attacker to a single guess at them.

**[2c](#2-rendezvous-paths) is untouched.** This was the constraint the answer had to respect and it is met structurally rather than by exception: the bootstrap produces a `PRK` and closes, and the peers then connect under [§5](#5-rv-3--key-derivation-and-tls) exactly as they would from a scanned code. There is still no unauthenticated rendezvous path.

**One thing CR-01 did not spot, and it is the useful part.** The request's §6 concludes that any bootstrap in which the capture peer listens inherits the platform's missing server-side PSK resolver. That is true of a bootstrap built on TLS-PSK. This one carries no PSK — there is not one yet — so nothing has to be resolved, and [11.2a](#112-why-it-is-not-tls-and-what-that-unlocks) leaves the bootstrap's dialling direction free. **The host PC can find the capture device and connect to it**, at first contact, which is what the feature was asked for. The steady state stays where [3.5d](#35-who-advertises-and-who-browses) and [3.4d2](#34-resolvable-identifiers) put it, and CR-01 §2 was right not to reopen that.

**Question 3 is answered as a clause rather than left to goodwill.** [3.5e](#35-who-advertises-and-who-browses): where the counterpart cannot advertise, the peer that can should. Without it a deployment could have neither end advertising and satisfy every clause, which makes persistence buy nothing.

**What is new is also what is least proved.** [§11](#11-rv-6--guided-pairing) has had no implementation review pass, [§10.4](#104-guided-pairing)'s vectors have been computed once by one implementation, [RT-20](#9-conformance) — the test the path exists to pass — cannot run until two implementations can be put either side of a relay, and [B14](#annex-b--open-issues) gates the whole section on a platform measurement neither team has run. Every other section of this document earned its confidence over three review passes. This one has earned none yet, and the revision says so where a reader will see it rather than in an annex.

**Draft 5** carries the third-pass findings. All six were in [§5](#5-rv-3--key-derivation-and-tls), and all six were consequences of Draft 4's relaxation that had not been carried into the clauses around it: a conformance test still refusing the newly-legal handshake, the clause that became load-bearing acquiring no test, the achieved outcome becoming a per-connection variable that nothing reported, and a server-sent field becoming reachable on a path nothing exercised. [§4](#4-rv-2--the-pairing-code), [§6](#6-rv-4--network-join) and [§7](#7-rv-5--security-model) are approved without reservation by both teams.

**Draft 4** resolved the one thing Draft 3 left blocked. The platform check was run and failed: TLS 1.3 with an external pre-shared key is not reachable through the mobile platform's interface, and neither is the TLS 1.2 ECDHE_PSK fallback — plain PSK is all it offers ([§5.4.1](#541-what-was-measured)).

The protocol owner has taken the decision, on the grounds that the data carried is not highly sensitive: **forward secrecy becomes best-effort rather than required.** [§5.4.3](#543-the-decision) records it in full, including what was given up, what both reviewers said about it, and what now carries more weight as a result.

**Only forward secrecy is relaxed.** The channel is still encrypted and still mutually authenticated; an unpaired peer still receives nothing; nothing stable still crosses in the clear. Two of the three properties of [5.2h](#52-tls-profile) are unchanged, and [5.2f](#52-tls-profile) — never fall back to an unencrypted connection, under any circumstances including a user instruction — is unchanged and unaffected.

**One item was already decided by shipping.** The mobile application declares `_ppcp._tcp` in its bundle, chosen before this document existed. [§3.1](#31-service-type) ratifies it rather than picking a different name; see [Annex A1](#annex-a--decisions-and-alternatives). Both reviewers endorsed that.

## Draft 7

The last open item, and it was a decision rather than a finding.

**5.4j is deleted.** The protocol owner has answered the question Draft 4 raised and Draft 6 kept open: the sensitivity judgement of [§5.4.3](#543-the-decision) covers candidate-attached audio as well as swing video. The clause withholding that audio over a connection without forward secrecy goes with it, and so do 5.4j1 and 5.4j2, which existed only to scope it.

**The reasoning that identified the audio stays.** Both reviewers said either answer was acceptable and that the unacceptable outcome was §5.4.3 naming an exception nothing acted on — so §5.4.3 still records that candidate audio is the part of the payload with a privacy dimension, and now records that the judgement was put to the owner and covers it. What follows is stated rather than softened: every session on a plain-PSK leg carries that audio with no forward secrecy, and [§7.1](#71-threat-model)'s *not defended* table names it.

**5.4k stays.** It was asked for on its own merits — a per-connection outcome that nothing reported left [5.4i](#543-the-decision) unable to apply a policy and a peer unable to tell a user the whole truth — and [B13](#annex-b--open-issues) still needs it.

`PPCP-RV` has no open findings. What remains is [B13](#annex-b--open-issues), a product question, and [B2](#annex-b--open-issues), per-peer re-keying, which both publishers avoid by emitting `mu: 1` only.

## Errata after revision 8

*Changes made during implementation, each normative and each naming the finding that produced it. The authoritative list for the whole protocol set is the errata table in [`PPCP-CORE`](ppcp-core.md#errata-after-revision-9).*

| # | Clause | Change |
|---|---|---|
| **E3** | [7.3a, 7.3f, 7.5c](#73-single-use-and-expiry) | `mu` counts **pairings**, not handshakes, and invalidates the **code** rather than the pairings made from it (F-H6-1, F-H6-1a, PinPointStudio, S4). |
| **E4** | [2c1](#2-rendezvous-paths), [RT-5](#9-conformance) | 2c is scoped to the three rendezvous paths, so a handed-in socket that 9a already declares conformant is not forbidden — and what still binds a claiming peer on one is stated (F-D9-1, PinPointCapture, S4). |
| **E20** | [4.3a1](#43-payload) | An encoder emits a field whose value equals the default, as both §10.3 vectors do. 4.3a's byte-identity promise needs one pairing to have one encoding (`libppcp`, S1). |
| **E21** | [5.3a1](#53-psk-identity) | **No octet of the PSK identity is `0x00`**; the client draws again. A `strlen`-lengthed PSK interface truncated the identity and failed roughly one handshake in sixteen (PinPointStudio, S1). |
| **E22** | [5.3c1](#53-psk-identity) | Scope: the wrong-key branch 5.3c and 5.3d equalise is unreachable while both keys come from one `PRK`. RT-11 is `n/a` on such a path and says so (F-D1-2, PinPointCapture, S1). |
| **E23** | [3.5d](#35-who-advertises-and-who-browses) | A peer whose platform cannot resolve a PSK identity server-side does not advertise for reconnection; the roles reverse under 3.5c and that is the conformant shape (F-D1-1, PinPointCapture, S1). |
| **E24** | [4.4a2](#44-handling-a-scanned-code) | **Decided by L17, reversible.** Three tests for an untrustworthy clock, of which only the build-date one is required — the boot-synchronisation test is not readable on iOS (F-D7-1, S4). |
| **E25** | [3.3d, 3.3e](#33-txt-record) | **Decided by L17, reversible.** One **range** syntax for `pv` and `detail.supported`; `hello.versions` stays an ordered list. Three documents had spelled one idea three ways (F-D7-2, S4). |
| **E26** | [7.4h](#74-persistent-pairings) | **Decided by L17, reversible.** A persisted pairing may keep the network **name** and never the passphrase, so §7.4's workflow reaches a venue with its own network (F-D7-3, S4). |
| **E27** | [3.4d1, 3.4d2](#34-resolvable-identifiers) | **Decided by L17, reversible.** One advertised instance at a time, rotating on the `rn` rotation, recently-used first; a multi-pairing peer browses as well. Annex B3 narrowed (F-D7-4, S4). |

## Errata after revision 9 — change request CR-01 and its review

*[CR-01](../changerequests/CR-01-in-band-pairing.md) asked for something revision 8 did not serve rather than reporting a defect in what it did. It is **granted in part**: the code goes, the operator does not. The [disposition](../changerequests/CR-01-disposition.md) carries the ruling and the answers to the request's three questions. **E30–E33 are the grant; E34–E39 are the review pass both teams then ran over it**, and the two blocking findings among them are the argument for having asked.*

| # | Clause | Change |
|---|---|---|
| **E30** | [§11](#11-rv-6--guided-pairing), [1.3c1](#13-where-it-stops), [2f](#2-rendezvous-paths), [9e, 9f](#9-conformance) | **Added, 24 August 2026 — CR-01.** **RV-6, guided pairing**: a first pairing between peers that have never met, from a committed X25519 exchange authenticated by six digits compared on both screens. It produces a `PRK` and closes; [§5](#5-rv-3--key-derivation-and-tls) and [§7](#7-rv-5--security-model) then apply verbatim, so [2c](#2-rendezvous-paths) is unweakened rather than excepted. Optional; the pairing code stays REQUIRED ([9f](#9-conformance)). |
| **E31** | [3.2c](#32-instance-name), [3.3f, 3.3g](#33-txt-record), [3.4c1](#34-resolvable-identifiers), [§3.7](#37-the-bootstrap-window) | **Added, 24 August 2026 — CR-01.** The **bootstrap window**: a user-opened, single-attempt, ≤180-second service instance carrying `bs` and an optional operator label `dl`, and carrying **no `rid`** — so [3.4c](#34-resolvable-identifiers)'s refusal to dial an unresolvable instance is scoped to the reconnection path, which is what made a first pairing over discovery impossible. `dl` is a stated privacy trade, bounded by the window. |
| **E32** | [3.5e](#35-who-advertises-and-who-browses) | **Added, 24 August 2026 — CR-01 question 3.** Where a counterpart cannot advertise under [3.5d](#35-who-advertises-and-who-browses), the peer that can **SHOULD**. 3.5d says only who must not, and read with [3.5b](#35-who-advertises-and-who-browses) a deployment could have **neither** end advertising while satisfying every clause — leaving [§7.4](#74-persistent-pairings)'s persisted pairing with no path by which either peer finds the other. |
| **E34** | [11.4h, 11.4i](#114-frames), [11.6c](#116-derivation), [§10.4](#104-guided-pairing), [RT-24](#9-conformance) | **Amended, 24 August 2026 — finding R-01, PinPointStudio, blocking.** `v` was carried, checked against the reader's own capability, and **bound to nothing** — an unprotected agility mechanism in a handshake whose own [11.6g](#116-derivation) argues against exactly that. An initiator now aborts if `bs_accept.v` differs from what it sent, and `v \|\| pk_i \|\| pk_a` is bound into **both** `sas_raw` and `K_c`, so a rewrite surviving the echo check changes the digits in front of the operator as well as failing the MACs. Four §10.4 rows change; `PRK` does not. Latent while `1` is the only value, permanent after either team ships. |
| **E35** | [11.3d1](#113-roles-and-the-connection) | **Added, 24 August 2026 — finding R-02, PinPointStudio, blocking.** [11.3d](#113-roles-and-the-connection) serialised the acceptor and its reasoning applied unchanged to the initiator, which nothing stated — and the natural implementation is the one that breaks it: a host that dials several discovered windows to show the operator a list of candidate numbers gives an attacker advertising N windows **N blind draws against one confirmation, with the operator finding the collision**. Worth three orders of magnitude at a large venue, from a user-interface decision the document permitted. |
| **E36** | [11.6b](#116-derivation), [RT-21](#9-conformance) | **Amended, 24 August 2026 — findings R-03 (PinPointStudio) and F-R9-1 (PinPointCapture), independently measured.** 11.6b named an all-zero `Z` that **neither implementation's library ever produces**: OpenSSL 3.6.3 fails the derive call for every small-order point and CryptoKit throws. The requirement moves to the outcome — abort on a key agreement that **fails or** yields zeros, and never treat that as a transport error or retry it. [E23](#errata-after-revision-8)'s shape, in a section written by someone who had just finished documenting E23. |
| **E37** | [11.4f](#114-frames) | **Reasoning replaced, 24 August 2026 — finding R-04, PinPointStudio. The clause stands.** It read that a failed confirmation MAC means an attacker forged one. **An interposed attacker holds `Z` on both legs and forges both MACs correctly** — a MAC failure is evidence that no such attacker is present, overwhelmingly an implementation disagreement. The MAC is an agreement-and-liveness proof, not an authentication check; the comparison is the authentication, and the inverted reasoning would have led an implementer to weigh them the wrong way round. |
| **E38** | [§11.8](#118-what-the-comparison-proves), [11.2c](#112-why-it-is-not-tls-and-what-that-unlocks) | **Added, 24 August 2026 — finding F-R9-2, PinPointCapture.** 11.2c invited a reviewer to name a value on the plaintext connection whose disclosure weakens the pairing; the review took the invitation. The confirmation MACs descend from `Z` by public functions, so **a recorded transcript is an offline verifier for it** — worth nothing against a CSPRNG, and the difference between *exploitable by an attacker present at the time* and *exploitable by anyone who recorded it, at any later date* against a weak one. This is the real force of [11.5a](#115-the-exchange)'s MUST, which was justified only by an impersonation argument. |
| **E39** | [9e1](#9-conformance) | **Added, 24 August 2026 — finding R-05, PinPointStudio.** An implementation claiming [§11](#11-rv-6--guided-pairing) states **which roles** it provides. 9e's *in full* was about not skipping halves of the exchange and said nothing about initiator versus acceptor — and one peer on this very deployment ships initiator-only, so two implementations could each claim §11 and be unable to pair. |
| **E40** | [11.6c1](#116-derivation), [11.6c](#116-derivation), [RT-24a](#9-conformance) | **Added, 24 August 2026 — finding R-09, PinPointStudio.** [E34](#errata-after-revision-9--change-request-cr-01-and-its-review) stated its binding as a general rule — *everything that varies is bound into everything derived* — and [11.6d](#116-derivation) and [11.6e](#116-derivation) are the two clauses it was not true of: `sid` and `PRK` are functions of `Z` alone, deliberately. An implementer following the rule instead of the formulas gets **matching digits, matching MACs and a divergent `PRK`** — a successful comparison, a successful confirmation, then `PSK_IDENTITY_NOT_FOUND`, which [§10.4](#104-guided-pairing) warns will be diagnosed as the [3.5d](#35-who-advertises-and-who-browses) platform limitation. **The trap did not exist before E34**, because there was no general rule to over-apply. The rule is scoped and the boundary is now stated with its reason. |
| **E41** | [11.4h1](#114-frames) | **Added, 24 August 2026 — finding R-10, PinPointStudio.** `v` is **1..255** — [11.4b](#114-frames) made it a CBOR uint reaching 2⁶⁴−1 while [11.6c](#116-derivation) encodes it as one octet — an acceptor **echoes** the `v` it received rather than substituting one, and the document now says **which `v` each peer binds**. That last was a normative derivation input left unstated: every consistent reading detects the rewrite [11.4i](#114-frames) exists for, so no hole closes, but [§10.4](#104-guided-pairing) carries one value of `v` and cannot catch two implementations that chose differently. |
| **E42** | [§11.7](#117-the-short-authentication-string), [§10.4](#104-guided-pairing) | **Corrected, 24 August 2026 — findings R-07 and R-08 (PinPointStudio) and F-R9-3 (PinPointCapture). Prose only; no clause, vector or security claim changes.** Two arithmetic errors in published commentary, both in notes whose purpose is to stop a reader making an arithmetic error. The modulo-bias paragraph claimed `295 967 296` residues out of 10⁶, put the favoured set the wrong way round, and understated the excess over uniform by **33×**. [§10.4](#104-guided-pairing)'s little-endian example was mis-reversed at source *and* computed from the pre-E34 `sas_raw`, three lines below the box warning against exactly that. Two of the three figures originated in PinPointStudio's pass-1 review and were quoted onward in good faith; **both teams recomputed every row of the vector and neither recomputed the numbers in the prose beside them.** |
| **E33** | [7.1](#71-threat-model), [7.2e](#72-handling-the-pairing-secret), [7.4i](#74-persistent-pairings) | **Added, 24 August 2026 — CR-01.** The security model carried into [§11](#11-rv-6--guided-pairing): what the comparison defends and what it does not, erasure of the ephemeral material, and that a guided pairing is **pairwise** so [7.4f](#74-persistent-pairings) does not reach it. Records that this path removes the photographable secret [§5.4.3](#543-the-decision) named — a consequence, not the reason it was granted. |

## Revision 8 — final

**Approved by both teams.** The fifth-pass review raised one clarification, E1, asking how a peer determines that a network is one it does not control. That question existed only to scope 5.4j, which [Draft 7](#draft-7) deleted, so **E1 is overtaken rather than answered** — there is no longer a clause whose behaviour turns on the distinction.

The document has no open findings. Two items remain and neither is drafting: [B13](#annex-b--open-issues), whether the absence of forward secrecy should be user-visible, which is the implementation teams' product question and which [5.4k](#543-the-decision) makes answerable either way; and [B2](#annex-b--open-issues), per-peer re-keying for multi-use codes, which both publishers avoid by emitting `mu: 1` only.
