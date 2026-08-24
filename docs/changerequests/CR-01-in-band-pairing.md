# PPCP-RV change request 01 — an authenticated bootstrap for a first pairing

| | |
|---|---|
| **Raised by** | PinPointCapture, 24 August 2026 |
| **Against** | `PPCP-RV` 1.0 (approved 22 August 2026), as amended by errata E21–E27 of 23 August |
| **Type** | **Change request.** Not a defect — the specification is self-consistent and does not serve a requirement the product has acquired |
| **Tracked** | PinPointCapture issue [#94](https://github.com/PinPoint-Golf/PinPointCapture/issues/94); recorded as `F-MVP-1` in that repository's `docs/conformance/ppcp-conformance.md` §4 |
| **Status** | Open. Awaiting the protocol team's ruling. Nothing has been implemented ahead of it |

---

## 1. The ask, in one sentence

**`PPCP-RV` has no path by which a host and a capture peer that have never met can establish a pairing without an out-of-band code, and the product now wants one.**

That is the whole request. §2 below is what is *not* being asked, because the specification already answers it and this request should not be read as reopening it.

---

## 2. ⛔ What is NOT being asked — the reconnection half is already settled

An earlier draft of this request treated "which peer is discovered" as open. It is not, and stating that here is the point of this section: a reader who conflates the two will answer a question that was closed last week.

For a capture peer on Apple platforms the specification already prescribes the shape:

> **(3.5d) MUST NOT** *Erratum E23.* A peer **advertise for reconnection where its platform cannot resolve a PSK identity server-side**, and 3.5b does not apply to it: the roles reverse under 3.5c, and **that reversal is the conformant shape for such a peer rather than a deviation from a SHOULD**.

> **(3.4d2) SHOULD** *Erratum E27.* A peer holding several pairings **browse as well as advertise**, and prefer what it discovers… This is 3.5c's reversal, and for a peer holding several pairings it is the better shape rather than merely a permitted one — **which for a peer that cannot advertise usefully anyway (3.5d) settles the question entirely.**

So: **the host advertises, the capture peer browses and dials.** PinPointCapture implements the browsing side already — `PpcpAdvertiser.browse(against identityKeys:)` resolves every held pairing per 3.4b and refuses unresolvable `rid`s per 3.4c — and its connect screen was designed around a discovered *host*. No change is sought here, and none is needed.

⚠ **The consequence for this request.** Granting it delivers *first contact* without a code. It does **not** change the steady state, which 3.5d/3.4d2 have already placed on the device-browses path. Both halves of a "Studio finds the phone" product story therefore cannot be delivered on Apple platforms by a specification change alone — and the half that can be is the one below.

---

## 3. The requirement

A driving-range session should be able to begin with a host and a capture device that have never been paired, and reach a working link, **without the operator transferring a code between two screens**.

The pairing-code path is not deficient. It works, it is REQUIRED by 2a, and PinPointCapture has measured it 30/30 two-sided against PinPointStudio's real listener. The requirement is about the first-run experience in a venue where a range operator sets up several bays.

---

## 4. Why the specification does not serve it

> **(2c) MUST** Whichever path is used, the resulting connection completes the handshake of §5 before any PPCP message crosses it. **There is no unauthenticated rendezvous path.**

All three paths in §2's table presuppose key material an earlier pairing established:

| Path | Presupposes |
|---|---|
| Pairing code (§4) | the code, carrying `PRK` |
| Service discovery (§3) | a pairing, whose `K_id` resolves `rid` (3.4b) and whose `K_tls` completes §5 |
| Direct | an endpoint reached out of band, still handshaking under §5 |

A host meeting a device for the first time holds nothing, so §5's handshake cannot complete, so 2c forbids the connection. There is no gap to exploit — 2c is doing exactly what it was written to do.

---

## 5. The constraint any answer must respect

⛔ **The bootstrap must be *authenticated*, not merely encrypted.** 2c's wording is deliberate, and the deployment makes it load-bearing: a driving range is a shared network, frequently an operator-run access point, on which an active man-in-the-middle is entirely practical. A scheme that establishes an anonymous encrypted channel and trusts what arrives on it would satisfy a casual reading of "secure" while deleting 2c in substance.

⚠ Whatever mechanism is chosen will also need to hold under the platform limits in §6, which are structural rather than incidental.

---

## 6. Platform evidence the answer will have to live within

All measured by PinPointCapture on **release** operating systems, not betas. Reproduced here so the design need not re-derive them.

**6.1 — Apple platforms cannot reach TLS 1.3 external PSK.** Measured 22 August 2026 on macOS 27.0 and an iPhone 16 running iOS 26.6, two `NWConnection` endpoints with the same 32-byte external PSK:

| Attempt | Result |
|---|---|
| minimum TLS 1.3 | handshake **fails**, `-9816` |
| minimum TLS 1.2 | TLS 1.2, ciphersuite `0x00A8` = `TLS_PSK_WITH_AES_128_GCM_SHA256` |
| append ECDHE_PSK `0xD001` or `0xC037` | **silently ignored**, still `0x00A8` |

Structural: `tls_ciphersuite_t` contains no PSK entry, so the negotiated suite can be neither requested nor withheld through the public API. This is what forced §5.4 to relax forward secrecy to best-effort.

**6.2 — Apple's TLS listener has no server-side PSK resolver.** `F-D1-1`, session S1 — the finding behind erratum E23. `sec_protocol_options_add_pre_shared_key` registers a (key, identity) pair up front; `sec_protocol_options_set_pre_shared_key_selection_block` is documented as client-side and has no server counterpart. A listener refuses an identity it did not register in advance with `PSK_IDENTITY_NOT_FOUND` → alert 115. Since 5.3a makes the identity fresh per connection, 5.3b's resolver has nowhere to live.

**6.3 — cross-platform PSK works, and is proved on the wire.** PinPointStudio's listener offers both TLS 1.2 PSK and TLS 1.3 (verified with `openssl s_client` against the real listener). The pair negotiated TLS 1.2 `TLS_PSK_WITH_AES_128_GCM_SHA256`, and both implementations independently surfaced the same suite and the same *"no forward secrecy"* — 5.4k paying off between two codebases sharing no TLS code.

**6.4 — binary PSK identities survive.** The 17-octet identity of 5.3a completes a handshake untranscoded despite RFC 4279 preferring UTF-8. E21's zero-octet rejection sampling is already accounted for.

⚠ **6.2 is the one that bounds the design space.** Any bootstrap in which the *capture peer* listens on Apple platforms inherits it. A bootstrap in which the capture peer **dials** does not — which is the shape the pairing-code path already has, and the reason 2a makes it the required one.

---

## 7. What this request deliberately does not propose

⛔ **No mechanism, no wire format, no ciphersuite.** A short-code PAKE is the obvious family and it would be easy to sketch one here. It is left out on purpose: the requirements document commits PPCP to being an open specification that is normative and *precedes* implementation, and a plausible-looking scheme arriving with a change request is harder to discard than no scheme at all. The design is the protocol team's.

⛔ **Nothing has been implemented.** PinPointCapture ships the pairing-code path unchanged and implements no rendezvous mechanism absent from `PPCP-RV`.

---

## 8. If the answer is "no"

That is a legitimate outcome and it costs the product little. The pairing-code path stays, the operator scans once per device per session, and 3.5d/3.4d2's browse-and-dial shape gives an automatic reconnection thereafter once the host advertises. The one thing needed for that — **PinPointStudio advertising `_ppcp._tcp` with `role: host`** — is a platform question rather than a protocol one, in 3.5c's own words, and is worth confirming as PinPointStudio's intent either way.

---

## 9. Questions this request would like answered explicitly

1. Is an authenticated first-contact bootstrap in scope for `PPCP-RV`, or deliberately out of it?
2. If in scope, does it become a fourth path in §2's table, or an extension of the pairing-code path?
3. Whichever way it is answered — **is PinPointStudio going to advertise for reconnection?** §2 above depends on it, and it is not currently confirmed.
