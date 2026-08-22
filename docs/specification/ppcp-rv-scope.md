# PPCP-RV — Rendezvous: scope statement

**What the companion specification must fix, and why it is not this one.**

| | |
|---|---|
| Document | `PPCP-RV` |
| Version | **Scope statement only — the specification is unwritten** |
| Status | **Gap.** Tracked as [`PPCP-CORE` Annex B1](ppcp-core.md#annex-b--open-issues) |
| Date | 22 August 2026 |
| Versioned | Independently of PPCP. Same repository. |

---

## 1. Why this is a separate document

**Transport is genuinely a local choice.** TCP, a USB tunnel, QUIC — two conformant peers that pick different transports simply do not connect over that path, and no interoperability claim is harmed.

**Rendezvous is a mutual agreement problem.** If a third-party host advertises `_swingcap._tcp` while a capture device browses for `_ppcp._tcp`, the two never meet and every invariant downstream is unreachable. A protocol whose peers cannot find one another is open in theory only.

So rendezvous must be specified — but separately, for two reasons:

1. **It will version faster than the entity model.** NFC, deep links and BLE pairing will arrive without touching a single Timebase or Shot.
2. **A USB-only peer should not implement mDNS to be conformant.** [`PPCP-CORE`](ppcp-core.md) takes an already-established byte stream; a peer handed a socket by an embedding application is fully PPCP-conformant with no rendezvous implementation at all.

- **Implementing `PPCP-RV` is OPTIONAL.** Implementing PPCP is not.

---

## 2. What it must fix

Each of these is currently unspecified, and each independently blocks cross-implementation pairing.

| # | Item | Why it blocks |
|---|---|---|
| **RV-1** | **Service type and TXT record contents.** One agreed name, with enough in TXT — protocol version, peer id, role — that a host can filter *before* connecting rather than after. | Two implementations browsing different names never meet. |
| **RV-2** | **QR payload format.** The highest-priority item: QR is the **primary** pairing path, not a fallback. It is an opaque fixed blob both sides must parse with no chance to negotiate first, **so it needs a version marker in its first field or it can never change.** | A QR without a version marker is unfixable after the first release. |
| **RV-3** | **PSK derivation and TLS-PSK identity format.** | Without an agreed derivation the handshake cannot complete across implementations, even with a correctly-scanned QR. |
| **RV-4** | **Optional SSID and passphrase extension**, driving a hotspot join, so a host can remove the network problem rather than work around it. | Not blocking, but it is the difference between working and not working at a range. |
| **RV-5** | **The security model.** See [§3](#3-the-security-model-lives-here). | PPCP has none. |

---

## 3. The security model lives here

[`PPCP-CORE` §12](ppcp-core.md#12-security-considerations) records that PPCP defines no security model. **That is defensible only if this document exists, and it does not.** Until it is written, PPCP's security model is not delegated — it is absent.

What belongs here:

- **PSK strength and derivation**, and what a peer does when a PSK is reused.
- **Replay resistance** across sessions and across reconnects.
- **Whether a peer may rejoin a session after reconnecting without re-pairing.** This is a modelling question with a wire consequence — [`PPCP-MSG` §4.3](ppcp-messages.md#43-session_resume) defines `session_resume` and deliberately says nothing about whether re-authentication is required. That silence is a hole, not a design.
- **`Peer.id` exposure.** [`PPCP-CORE` §5.2.1](ppcp-core.md#521-peer-identity) makes `Peer.id` a persistent generated identifier that must not be a platform device id. Whether it may be broadcast in a TXT record before pairing is a privacy decision this document has to take.
- **What an unpaired peer may learn.** On a shared network, capture payload must never reach an unpaired host, and discovery must not leak more than it has to before authentication.

---

## 4. Constraints inherited from PPCP

Anything written here must hold these:

- **(RV-a)** Discovery, pairing and authentication complete in **a single user action**.
- **(RV-b)** **Assume multicast fails.** It is rate-limited or dropped on many consumer access points, blocked by client isolation on guest networks, and does not cross VLANs. It will not work at a range. This is why QR is primary.
- **(RV-c)** **The device advertises; the host browses.** The host then needs only the querier role and never binds the multicast DNS port, avoiding conflict with a platform responder. It also fits the topology: capture requires foreground, so the device is the party reliably present.
- **(RV-d)** Direct-IP connection from a QR degrades more gracefully than multicast browsing, and platform local-network permission denial must be detected and explained rather than presenting as a permanently broken application.
- **(RV-e)** Nothing here may change the meaning of any field in [`PPCP-CORE`](ppcp-core.md). Rendezvous hands PPCP a byte stream and stops.

---

## 5. Status

Not started. It is **not** blocking implementation — a device and a host built by the same team will pair over a private arrangement, and a USB peer needs none of this — but it **is** blocking any claim that the protocol is open, and it is blocking the third-party interoperability pairing required by [`PPCP-CONF` §5c](ppcp-conformance.md#5-interoperability).

Recommended sequencing: draft RV-2 (the QR payload, with its version marker) before the first release that ships a QR, because that is the one item that cannot be fixed later.
