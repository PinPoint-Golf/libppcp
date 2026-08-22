# PPCP-RV Draft 1 — review from the PinPointCapture mobile app

| | |
|---|---|
| Reviewing | `libppcp/docs/specification/ppcp-rv.md`, Draft 1, payload version `ppcp1` |
| Seat | Owner of the PinPointCapture iOS/iPadOS app — the peer that scans the code, parses the payload, and is the TLS **client** on the primary path |
| Basis | The document read against Apple's iOS 27.0 SDK headers, and against our own designed pairing screens (B1, B4, B6) |
| Date | 22 August 2026 |
| Verdict | **One finding I would not proceed past without an empirical check**, because it may make an iOS peer unable to conform at all. Two smaller ones. The pairing code itself — the irreversible part — I have read hardest and have no objection to. |

The document asked for §4 to get the hardest reading and the least benefit of the doubt. I have given it that, and my objections are elsewhere.

---

## 1. TLS 1.3 external PSK may not exist on the platform we ship on

**This is the finding. It is unverified, it is cheap to verify, and if it goes the wrong way it is blocking rather than awkward.**

§5.2a requires TLS 1.3 with an external PSK and states that *"earlier versions MUST NOT be negotiated"*. §5.2f forbids any fallback under any circumstances, including handshake failure. RT-4 tests that a handshake negotiating TLS 1.2 is refused. Taken together there is no conformant path for a peer whose platform cannot do TLS 1.3 external PSK.

Apple's PSK API exists. Reading the iOS 27.0 SDK headers, what it appears to be *built on* is the concern:

| Evidence | Header |
|---|---|
| `sec_protocol_options_add_pre_shared_key(options, psk, psk_identity)` — available since iOS 12, **no TLS version constraint documented** | `SecProtocolOptions.h:373` |
| `sec_protocol_options_set_tls_pre_shared_key_identity_hint` — its doc comment cites **RFC 4279**, the TLS 1.2 PSK ciphersuite specification | `SecProtocolOptions.h:376` |
| `sec_protocol_pre_shared_key_selection_t` receives a **`psk_identity_hint` from the peer** — TLS 1.3 removed identity hints entirely; they exist only in the RFC 4279 model | `SecProtocolOptions.h:420` |
| `sec_protocol_options_set_tls_diffie_hellman_parameters` deprecated: *"DHE ciphersuites are no longer supported"* | `SecProtocolOptions.h:352` |

Two of those four are TLS 1.2-only concepts sitting immediately around the PSK entry point. That is not proof — the header nowhere states a version constraint, and Apple may well have wired `add_pre_shared_key` through to TLS 1.3 external PSK with the hint APIs left as legacy. But it is enough that **I would not agree §5.2a on the assumption it works.**

**Note the asymmetry.** The host side uses OpenSSL, which has had TLS 1.3 external PSK for years. So a reference host ↔ reference host pairing, and every test in §9, can pass with this risk entirely invisible — it surfaces only when someone attempts it on iOS. That is the same structural problem Annex B7 already records, arriving earlier than expected.

**If it turns out not to be available**, the options are all expensive and all worth knowing about now rather than in a month:

- **Relax §5.2a to permit TLS 1.2 with an ECDHE_PSK suite.** Forward secrecy survives if ECDHE is available; the document's own A6 reasoning is about forward secrecy rather than about the version number, so this may cost less than it looks. It would need §5.2a, §5.2b, §5.3 and RT-4 rewritten.
- **Embed a TLS library in the application.** This is the option people reach for and it is worse than it sounds for a mobile app: binary size, a materially different export-compliance answer at App Store submission because the app then ships its own cryptography, an ongoing patching obligation for a security-critical dependency, and it cuts directly against the reasoning used elsewhere in this document set for vendoring a small CBOR codec rather than taking a heavy dependency.
- **Something else** — but the two above are what the choice looks like.

**Proposed action, and I am volunteering for it.** A day's spike: two `NWConnection` endpoints on a loopback, `sec_protocol_options_add_pre_shared_key` on both, `sec_protocol_options_set_min_tls_protocol_version(.TLSv13)`, attempt the handshake, and read back `sec_protocol_metadata_get_negotiated_tls_protocol_version` and the negotiated ciphersuite. If it completes at 1.3, this finding evaporates and §5.2 stands as written. If it fails, we know before either team writes rendezvous code. **I would hold agreement on §5.2 until that result exists**, and nothing else in the document depends on the answer.

## 2. `psk_dhe_ke` is mandatory, and we can neither select it nor observe it

§5.2b makes `psk_dhe_ke` a MUST and forbids `psk_ke`. A6 explains why, and the reasoning is right — forward secrecy is exactly the property that matters a year after a pairing secret leaks.

But Apple's PSK interface offers **no control over the key-exchange mode**. There is no knob for `psk_dhe_ke` versus `psk_ke`, and `sec_protocol_metadata` exposes the negotiated protocol version and ciphersuite rather than the PSK key-exchange mode. So even if §1 resolves well, an iOS implementation is in the position of being obliged to guarantee something it can neither request nor assert.

This does not mean the requirement is wrong. It means **RT-4 cannot be satisfied by an API assertion on our side** and needs a wire-observing test — a packet capture of the `ClientHello`'s `psk_key_exchange_modes` extension, or a counterpart instrumented to reject `psk_ke`. Worth stating in §9 how an implementation is expected to demonstrate 5.2b, because the obvious reading is "assert it in code" and on at least one platform that is not possible.

Suggested: RT-4's method should be **injected against an instrumented counterpart** rather than static, and §5.2b should note that a peer whose platform does not expose the mode demonstrates conformance by observed handshake rather than by construction.

## 3. §2's dialling table and §3.3's `role` field disagree, and our reconnection screen sits on the crack

§2 and A9 are unambiguous: on the discovery path the **device advertises and the host dials**. The reasoning is good — the querier role belongs on the host so it never binds the multicast port a platform responder already owns, and capture requires the foreground so the capture peer is the one reliably present.

But §3.3's TXT record defines `role` as `host | capture | observer`. If only devices advertise, `role: host` is a value that can never legitimately appear. Either hosts may advertise — in which case a device-dials-host reconnection path exists and is unspecified — or the enum should not admit it.

**This is not academic for us.** Our B1 pairing screen's first row is a *discovered host*: `Bay 3 — Mac Studio / On this network · paired yesterday / [Connect]`. That is the device having found a host and the user dialling it. Under §2 that flow does not exist: on reconnection the host finds us and dials, and there is nothing for the device to list or for the user to press.

Three ways this resolves, and they are genuinely different products:

1. **The spec is right and our screen is wrong.** B1's row becomes passive — "Bay 3 will reconnect automatically" — with no Connect action. We would want to make that change before building the screen rather than after.
2. **Hosts may advertise too**, and §2/§3.3 should say so, with the device browsing on reconnection. This costs the host a listener and gives the mobile side a reconnection path it can initiate.
3. **The row is a cached endpoint**, not a discovery result — the direct path of §2. That works, but the copy "On this network" is a claim the device can only make by probing, and a cached endpoint goes stale on any DHCP change, which at a range is routine.

I do not have a strong preference between them and it is partly our design decision. What I want is for §2 and §3.3 to agree, because as written an implementer cannot tell which of the three is intended.

One platform note that bears on the choice: on iOS, **advertising and browsing both trigger the Local Network permission prompt**, and `NSBonjourServices` in the bundle must list the service type. We already declare `_ppcp._tcp`, which is correct either way — but whether we advertise, browse, or both changes B6's copy and the failure we have to explain to a user who refuses.

## 4. Smaller points

**§6b's first branch is not implementable on iOS.** It requires a peer that joined a network for a pairing to *"restore the prior network configuration when the session ends, or leave the join in the user's control"*. An app can remove its own `NEHotspotConfiguration`, which detaches; it cannot restore a previously-associated network — reassociation is the system's decision. The clause is already a disjunction so we are conformant via the second branch, and the trailing MUST NOT is satisfiable because removing our configuration detaches the device. Worth a note that on some platforms only the second branch is available, so nobody reads the first as the expected behaviour.

**§3.2b is a good catch and I want to endorse it explicitly.** Requiring the instance name to carry no user-assigned device name is exactly right, and the reason given — that platform APIs default it to the device name, which is frequently a person's name — is precisely what happens on iOS if you do not set it. This is the kind of thing that ships by accident.

**§7.2b naming diagnostic export explicitly.** Endorsed, and taken. Our diagnostic bundle is a designed, user-initiated, issue-tracker-attached artefact assembled by capture code, and it is exactly where a payload would end up by accident.

---

## 5. On §4, the part that cannot be changed

Read hardest, per §0. No objection, and three choices I want to record agreement with rather than leave silent:

- **A3, a custom scheme rather than an `https` link.** Correct, and the reason is the one that matters: an `https` code for an app that is not installed puts the pairing secret into a browser and into history. The store-discovery benefit is real and belongs beside the code as text, exactly as stated.
- **A2, version inside the payload rather than in the scheme.** Correct for the reason given — an unregistered scheme is never delivered, so the user sees nothing at all rather than a message they can act on. §4.2b's obligation to say *"this code needs a newer app"* is a user-experience contract and we will implement it as one.
- **§4.5's size discipline.** 105 characters of URI for the base case is comfortably scannable at arm's length on a phone camera, and the 400-byte SHOULD is the right place to stop. Our B1 screen has the camera live on entry, so scanning distance is the first thing a user experiences.

One question rather than an objection: **`ep` is `1..n` and §4.3c has the scanner try entries in order until one completes the handshake.** With a `wifi` join in the same code (§6), the reachable endpoint may only become reachable *after* the join. Is the intended order: join first, then walk `ep`? Or walk `ep`, and on total failure join and walk again? Both work; they differ in how long a user waits before anything happens, which on our B4 sheet is user-visible. Worth one sentence.

---

## 6. Summary

| # | Finding | Severity |
|---|---|---|
| 1 | TLS 1.3 external PSK may be unavailable on Apple platforms; §5.2a/f admit no fallback | **Blocking risk — verify before agreeing §5.2.** Spike offered. |
| 2 | `psk_dhe_ke` is mandatory but unselectable and unobservable through the platform API | **High** — RT-4 needs a wire-observing method |
| 3 | §2 and §3.3 disagree on whether a host may advertise; our reconnection screen depends on it | **High** — cheap to settle, and it is a product decision as much as a protocol one |
| 4 | §6b's "restore prior network configuration" is not implementable on iOS | Low — the disjunction saves it |
| 5 | Ordering of network join versus endpoint walk is unstated | Low |

Everything else I read I agree with, including the whole of §4. If §1 resolves in the platform's favour, I have no blocking objection to this document.
