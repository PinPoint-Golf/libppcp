# PPCP-RV Draft 2 — review from the PinPointCapture mobile app

| | |
|---|---|
| Reviewing | `libppcp/docs/specification/ppcp-rv.md`, Draft 2, payload version `ppcp1` |
| Seat | Owner of the PinPointCapture iOS/iPadOS app — the peer that scans the code and is the TLS client on the primary path |
| Basis | The document, the disposition, **an executed Annex B8 check**, and **every §10 vector recomputed** |
| Date | 22 August 2026 |
| Headline | **B8 is resolved, and the answer is no.** TLS 1.3 external PSK is not reachable through the platform interface, and neither is the ECDHE_PSK fallback the disposition was counting on. Evidence below. |

I owed this review two things I did not do last time: run the check I volunteered for, and recompute the vectors rather than read them. Both are here. The second matters because the host team found V1 by computing and I did not.

---

## 1. Annex B8 — executed, and it fails

**The one-day check is done.** Two `NWConnection` endpoints on loopback, the same 32-byte external PSK installed on both via `sec_protocol_options_add_pre_shared_key`, minimum TLS version forced, negotiated version and ciphersuite read back from `sec_protocol_metadata`.

```
min = TLS 1.3   (what §5.2a requires)  -> handshake FAILED
                                          -9816 errSSLClosedNoNotify
min = TLS 1.2   (what the platform does) -> negotiated TLS 1.2
                                          ciphersuite 0x00A8
```

`0x00A8` is **`TLS_PSK_WITH_AES_128_GCM_SHA256`** (RFC 5487) — plain PSK. No DHE, no ECDHE, **no forward secrecy**.

**The fallback was tested too, and it is also unavailable.** The disposition records that *"TLS 1.2 with an ECDHE_PSK suite would preserve both properties, which is why A6's reasoning survives the version number changing"*, and RV-D4 keeps §5.2a on that basis. I tried to force one:

```
min = TLS 1.2, append 0xD001  TLS_ECDHE_PSK_WITH_AES_128_GCM_SHA256 (RFC 8442)
                              -> negotiated TLS 1.2, ciphersuite 0x00A8
min = TLS 1.2, append 0xC037  TLS_ECDHE_PSK_WITH_AES_128_CBC_SHA256 (RFC 5489)
                              -> negotiated TLS 1.2, ciphersuite 0x00A8
```

The request is **silently ignored**. And the reason is structural rather than incidental: `tls_ciphersuite_t`, the enumeration `sec_protocol_options_append_tls_ciphersuite` takes, **contains no PSK ciphersuites at all**. It carries ECDHE_ECDSA and ECDHE_RSA certificate suites and nothing else. The suite actually negotiated, `0x00A8`, is not a value the public API can name — so it can be neither requested nor excluded.

**What this means for the document:**

| Clause | Status on the platform |
|---|---|
| §5.2a — TLS 1.3, earlier versions MUST NOT be negotiated | **Unachievable** via platform TLS |
| §5.2b — `psk_dhe_ke`, `psk_ke` MUST NOT be used | **Unachievable**; plain PSK is the only mode offered |
| §5.2h — mutual authentication from a scanned secret | ✅ Achieved — this is what PSK gives |
| §5.2h — forward secrecy against later disclosure | **Unachievable** via platform TLS, in any TLS version |
| RV-D4's fallback — TLS 1.2 with ECDHE_PSK | **Unavailable** |

**Caveat, stated plainly.** The spike ran on macOS 27.0 against the macOS SDK. `Network.framework` and `Security.framework` are the same APIs on iOS, the PSK entry point carries identical availability annotations on both, and `tls_ciphersuite_t` is the same enumeration in both SDKs — so I regard this as strong. It is not the same as having run it on the phone, and **I can do that in an afternoon if the team wants it before acting on a decision this size.** I would rather over-verify this one than have it turn on a platform difference nobody expected.

### 1.1 What is left

§5.2h is the clause that makes this decidable, and writing it was the right call — the choice is now against stated properties rather than against a version number. On that basis:

- **Dropping forward secrecy is not available.** §5.2h says so, A6 says why, and the reasoning — anyone who captures a session and later obtains the pairing secret decrypts it retrospectively — is not weakened by the platform being awkward.
- **Relaxing to TLS 1.2 is no longer a fallback**, because the property it was supposed to preserve is not obtainable at 1.2 either. RV-D4 should be closed as overtaken rather than exercised.

That leaves embedding a TLS library — BoringSSL via swift-nio-ssl, or OpenSSL — as the only route I can see to §5.2h's properties on this platform. I am not arguing against it, but the cost should be visible before it is chosen, because it is larger than it looks for a mobile app:

- Binary size, on an app whose users are being asked to install it to avoid buying cameras.
- **It changes the answer to App Store Connect's export-compliance questions.** The app would ship its own cryptography rather than relying solely on Apple's, which is a different declaration. I do not know the exact consequence and would want whoever handles submissions to confirm it before it is on the critical path.
- An ongoing patching obligation on a security-critical dependency, on a release cadence gated by app review.
- It cuts against the reasoning used elsewhere in this document set — A4 vendors a CBOR codec specifically to avoid a heavy dependency, and this is a much heavier one.

**My recommendation:** close B8 with *not reachable*, close RV-D4 as overtaken, and treat "embed a TLS library" as a decision with its own review rather than a consequence that follows automatically. If there is a fourth option nobody has thought of, this is the moment for it — and I do not have one.

---

## 2. §4.3b is contradicted by the specification's own vectors

The V1 fix is right in substance and over-stated in scope, in the section that cannot be corrected after a code is printed.

§4.3b reads, unqualified:

> **Every payload key other than `v` is at least two characters.**

The `ep` map in §10.3 uses `h` and `p`. The `wifi` map in the all-fields vector uses `h`, `k` and `s`. All five are one character, and all five are payload keys.

Read literally, **RT-2 and any validator written against 4.3b reject the specification's own normative test vectors.**

It is not functionally harmful — nested map ordering does not affect where `v` sorts in the top-level map, which is all 4.2a needs. But it is the clause the whole version story rests on, it is in §4, and it is the same shape as V1 itself: a rule stated more broadly than the vectors satisfy, invisible unless someone checks one against the other.

**Suggested:** scope it — *"Every key of the **top-level payload map** other than `v` is at least two characters."* One word, and it makes 4.3b true of the document as written. Worth also saying explicitly that nested maps are unconstrained, since a reader who has just internalised the rule will wonder why `ep` breaks it.

---

## 3. Vector verification

Recomputed independently — HKDF implemented from RFC 5869 rather than called from a library, HMAC-SHA256 from `hashlib`, CBOR assembled by hand from the octet listings, base64url without padding.

| Vector | Result |
|---|---|
| §10.1 `PRK` | ✅ matches |
| §10.1 `K_tls` | ✅ matches |
| §10.1 `K_id` | ✅ matches |
| §10.2 `rid`, and instance name `PPCP-9B1D2DF9` | ✅ matches |
| §10.2 PSK identity tag, and the 17-octet identity | ✅ matches |
| §10.3 minimal payload — 75 octets, 105 URI characters | ✅ matches |
| §10.3 all-fields payload — 133 octets, 183 URI characters | ✅ matches |
| §10.3 all-fields first four octets `a8 61 76 01` | ✅ matches |
| `exp` 0x6a9026c0 = 1787832000 | ✅ matches |

Deterministic ordering also checked directly: with every optional field present the key order is `v, dn, ep, mu, exp, psk, sid, wifi`. **`v` is first, which is what V1 was about, and the fix holds.**

---

## 4. Confirmations

- **§3.5 resolves my finding correctly.** Separating the mechanism — any peer may advertise or browse — from the recommendation — a capture peer advertises, a host browses — is better than either of the two answers I offered. Our B1 screen's "discovered host with a Connect action" is now a legitimate product decision, which is exactly what I asked for. We will decide it on our side and it needs nothing further from this document.
- **§5.2i on demonstrating `psk_dhe_ke` by observed handshake.** Correct, and now moot on our platform for the reason in §1 — but it is the right rule for any peer whose stack does not expose the mode, and should stay.
- **`mu: 1` only.** Confirmed. We will emit `mu: 1` and will not persist a `PRK` from any code with `mu > 1`, per 7.4f. The V3 finding is a good one and the group-credential framing is the part that makes it obvious.
- **4.3e fixing `sid` as canonical lowercase UUID text.** Endorsed. Two implementations choosing different textual forms would have duplicated every Capture on re-import, through the one layer nobody would have thought to look at.
- **7.3e making the publisher enforce `exp`.** Endorsed — it holds the authoritative clock, and 4.4a1 letting a peer with an untrustworthy clock attempt anyway is right for a device that has been at a range with no network for three hours.
- **§6b and 4.3f** both land as asked. Join first unless already associated, then walk `ep`; only the second branch of 6b is available on iOS and the document now says so.

---

## 5. Summary

| # | Item | Status |
|---|---|---|
| 1 | **B8 executed: TLS 1.3 external PSK unreachable; ECDHE_PSK fallback also unavailable; forward secrecy unobtainable via platform TLS** | **Blocking §5.2. Needs a decision, not a redraft.** |
| 2 | §4.3b contradicted by the document's own vectors | **Fix before agreement** — one word, in the irreversible section |
| 3 | All nine §10 vectors verified byte-for-byte | ✅ |
| 4 | §3.5, §5.2i, 4.3e, 7.3e, §6b, 4.3f | Confirmed, no further comment |

Apart from §5.2, this document is in good shape and I have no other objection. §1 is not a defect in the specification — the specification asked the right question and stated the property that decides it. The platform simply answered badly, and it is better to know now than after either team has written rendezvous code.
