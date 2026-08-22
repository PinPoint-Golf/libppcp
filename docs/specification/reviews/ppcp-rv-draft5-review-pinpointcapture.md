# PPCP-RV Draft 5 — review from the PinPointCapture mobile app

| | |
|---|---|
| Reviewing | `ppcp-rv.md` Draft 5 |
| Seat | Owner of the PinPointCapture iOS/iPadOS app |
| Basis | The document, plus **the §5.4b on-device measurement, now executed** |
| Date | 22 August 2026 |
| Verdict | **Approve for implementation.** §5.4b is discharged — the result is unfavourable and confirms the relaxation. No blocking findings; one item from my previous review still appears undispositioned. |

## 1. §5.4b — the on-device measurement, executed

5.4b requires the platform measurement to be **repeated on the mobile device** before an implementation ships a pairing relying on §5.4.3. That is now done, on the target hardware rather than by inference from a desktop.

| | |
|---|---|
| Device | iPhone 16 (`iPhone17,3`), **iOS 26.6** — the release OS, not a beta |
| Method | Two `NWConnection` endpoints, same 32-byte external PSK via `sec_protocol_options_add_pre_shared_key`, minimum version forced, negotiated version and ciphersuite read from `sec_protocol_metadata` |
| Identity | The exact §10.2 vector, `010f1e2d3c4b5a6978b355ada60b4b5aa8` — 17 octets, not valid UTF-8 |

```
min TLS 1.3  (what 5.2a prefers)        ->  FAILED, -9816
min TLS 1.2                             ->  TLS 1.2, ciphersuite 0x00A8
min TLS 1.2 + ECDHE_PSK 0xD001 (RFC 8442) ->  TLS 1.2, ciphersuite 0x00A8
min TLS 1.2 + ECDHE_PSK 0xC037 (RFC 5489) ->  TLS 1.2, ciphersuite 0x00A8
```

**Identical to the desktop result in every respect.** The platform difference I could not rule out does not exist.

Three conclusions, and 5.4b's own wording anticipates all of them:

- **TLS 1.3 with an external PSK is not reachable** on the device. 5.4b says *"no clause changes on a favourable result"* — this is not one, so §5.4.3's relaxation stands as written and the best-effort framing of property 2 is the operative text.
- **ECDHE_PSK cannot be obtained.** Appending either suite is silently ignored, because `tls_ciphersuite_t` names no PSK ciphersuite at all — so the suite in use can be neither requested nor withheld. `0x00A8` is `TLS_PSK_WITH_AES_128_GCM_SHA256`, which is exactly the interoperable floor 5.2d names.
- **5.3a survives.** The 17-octet binary identity completes a handshake despite RFC 4279 saying identities "should" be UTF-8. The resolvable-identity design, and V2's fix, work unchanged at TLS 1.2. This was the most likely second-order casualty of the fallback and it did not happen.

**So PinPointCapture will be a plain-PSK peer**, and 5.2b1 obliges us to offer the strongest we have, which is this. Every session on our leg has mutual authentication and no forward secrecy. That is the decision §5.4.3 records, taken on the sensitivity of the data rather than on convenience, and I have nothing to add to it beyond confirming the measurement it rests on.

The probe was a throwaway build outside both repositories and has been removed from the device.

## 2. Everything I raised against Draft 4 has landed

| Draft 4 finding | Draft 5 | Verified |
|---|---|---|
| 5.2b1 and property 3 unassertable on our platform; 5.2i named only 5.2b | **5.2i widened** to *"a peer whose platform does not expose a mechanism this section constrains"*, with a table naming 5.2b1 explicitly | ✅ Generalised rather than patched — better than the one-line fix I suggested, because it covers clauses not yet written |
| The relaxation's basis | Unchanged, and still says it was a product decision on data sensitivity rather than a mechanism proving inconvenient | ✅ |

## 3. Still outstanding, and I think it was missed rather than declined

**§4.3b remains unqualified.** I raised this against Draft 2 and again against Draft 4, and I cannot find it in either disposition — which is why I read it as lost among the larger items rather than rejected.

> **(4.3b) MUST** Every payload key other than `v` is **at least two characters.**

The specification's own vectors contradict it: the `ep` map uses `h` and `p`, and the `wifi` map uses `h`, `k` and `s`. Read literally, **RT-2 rejects the document's own normative test vectors.**

It is harmless in effect — nested map ordering cannot affect where `v` sorts in the top-level map, which is all 4.2a needs — but it sits in §4, the part the document itself says cannot be corrected after a code is printed, and it is the clause the entire version-marker story rests on. The fix is one qualifier: *"Every key of the **top-level payload map** other than `v`…"*, with a sentence noting nested maps are unconstrained and why.

If it has been considered and declined I would rather know that than keep restating it.

## 4. Confirmations

- **§4 has now survived three passes and three independent recomputations**, and the all-fields vector exercising every optional field is what makes RT-2 meaningful. I verified all nine vectors myself against Draft 2 and they were exact; nothing in §4 has changed since.
- **§8's paragraph on PSK interfaces being a trap** is confirmed twice over by our measurement: the entry point sits beside RFC 4279 hint APIs, and it does exactly what that placement suggests. Keep it.
- **§6 and §7** — I have no findings and did not have any at Draft 4. The single-use and expiry model, the publisher-enforced `exp` with 4.4a1's allowance for an untrustworthy device clock, and the network-join handling all read correctly for our use.

## 5. Summary

| # | Item | Status |
|---|---|---|
| 1 | §5.4b on-device measurement | ✅ **Executed.** Unfavourable, identical to desktop; the relaxation stands and no clause changes |
| 2 | 5.3a's binary identity at TLS 1.2 | ✅ Verified working on device |
| 3 | Draft 4 findings | ✅ All landed |
| 4 | §4.3b contradicted by the document's own vectors | **Third time raised** — one qualifier, or tell me it is declined |

**Approve for implementation.** With §4.3b qualified I would have no outstanding comment on this document at all. It is the last thing I am holding, it costs one word, and it is in the section that cannot be fixed later — which is the only reason I am still raising it.
