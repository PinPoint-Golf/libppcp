# PPCP-RV Draft 4 — review from the PinPointCapture mobile app

| | |
|---|---|
| Reviewing | `ppcp-rv.md` Draft 4 — B8 closed negative, forward secrecy relaxed to best-effort by decision |
| Seat | Owner of the PinPointCapture iOS/iPadOS app — the peer that scans the code, and the peer whose platform limitation caused the relaxation |
| Basis | The document, plus **a second empirical run** on the platform: the §10.2 PSK identity exercised end to end |
| Date | 22 August 2026 |
| Verdict | **Agree, including the relaxation.** One new finding: two clauses added in Draft 4 are unverifiable on the platform that forced the change, and 5.2i — the mechanism that already handles this class — does not cover them. |

## 1. On the relaxation itself

**I agree with it, and more importantly I agree with how it was reached.**

The sentence that matters is in 5.2h:

> Property 2 was `Required` until Draft 4 and was relaxed by a **product decision on the sensitivity of the data carried**, not by a mechanism turning out to be inconvenient.

That is the right distinction and it is the one I was most worried about when I reported B8. A finding that a mechanism is unavailable creates schedule pressure to weaken whatever it was protecting, and the honest response is to decide explicitly whether the property is still worth having by another route — not to let the platform decide the security posture by default. Draft 4 does that, says so, and leaves properties 1 and 3 non-negotiable.

Keeping 5.2f — no unencrypted fallback, ever, including on handshake failure or user instruction — while relaxing 2 is also right. Those are different questions and it would have been easy to relax both in one motion.

**A consequence someone should own, and it is not the protocol team's to own.** Because we are the peer at the TLS 1.2 floor, *every* session between PinPointCapture and any host has no forward secrecy, not some. A host can see this — it reads the negotiated ciphersuite — but nothing says whether a **user** should be told. Our own requirements are unusually explicit about telling users what is kept and where it goes (REQ-PRIV-2, and the audio-retention work in §11 of the companion requirements). I do not think "your swing video was sent without forward secrecy" belongs on a screen, but I would rather that were a decision than an omission. Raising it here so it is recorded; the answer belongs to us and PinPointStudio rather than to this document.

## 2. New finding: 5.2b1 and property 3 are unverifiable on the platform that caused the relaxation

Draft 4 adds two obligations that are correct in principle and that we cannot demonstrate:

**5.2b1** — *"A peer MUST NOT propose a weaker mode than its platform supports, because the weakest end sets the outcome and there is no way for the other to tell a limitation from a choice."*

**5.2h property 3** — *"a `psk_identity_hint`, which exists in the TLS 1.2 PSK model and is sent in the clear, MUST be empty."*

Neither is assertable through the platform interface, for the same reason the key-exchange mode was not:

- We cannot name a PSK ciphersuite at all — `tls_ciphersuite_t` contains **no PSK entries**, so we can neither offer nor withhold one. What we "propose" is whatever the stack proposes on our behalf. We comply with 5.2b1 by having no way to do otherwise, which is compliance by accident rather than by construction.
- We control the `psk_identity_hint` only by *not* calling `sec_protocol_options_set_tls_pre_shared_key_identity_hint`. Whether the stack then sends an empty hint or omits the field is not observable from the API, and property 3 is a MUST.

The specification already has the right answer to this shape of problem — **5.2i**, which permits demonstrating conformance by *observed handshake* where the platform does not expose the mode. But 5.2i names only 5.2b.

**Suggested:** widen 5.2i to cover any clause in §5.2 that a peer's platform does not expose, naming 5.2b, 5.2b1 and property 3 explicitly. That is a one-line change and it turns three unassertable MUSTs into three things a packet capture settles. It also matters for RT-4, whose method is already `injected` for exactly this reason.

## 3. Second empirical result: the §10.2 identity works, and this de-risks the fallback

When B8 came back negative I flagged that the TLS 1.2 floor could break something else: **RFC 4279 says a `psk_identity` "should" be UTF-8**, and 5.3a mandates a 17-octet *binary* identity — `0x01 || rn2 || tag` — which is not. A stack that validated or transcoded identities as text would have broken the resolvable-identity design on the very platform that forced us onto TLS 1.2.

I have tested it. Using the exact §10.2 vector as the identity:

```
identity  010f1e2d3c4b5a6978b355ada60b4b5aa8   (17 octets, not valid UTF-8)
result    handshake completes, TLS 1.2, ciphersuite 0x00A8
```

**So 5.3a survives the fallback.** The resolvable identity, the rotating `rn2`, and V2's fix all work at TLS 1.2 exactly as at 1.3. That is worth recording because it was the most likely second-order casualty of B8 and it did not happen.

Same caveat as before, unchanged and still true: this ran on macOS 27.0 against the macOS SDK. Same frameworks, same availability annotations, same enumeration on iOS. I regard it as strong and it is not the phone; an on-device confirmation is an afternoon whenever the team wants it, and I would suggest doing it once before `PPCP-RV` is declared agreed rather than never.

## 4. Confirmations

- **§8's new paragraph on PSK interfaces being a trap** is the most useful operational text in the document, and the detail that *"getting the hash wrong produces a handshake failure with no useful diagnostic, indistinguishable from a key mismatch"* is exactly the note that saves someone a day. Both teams hit this from opposite directions, which is why it is worth having in the specification rather than in a wiki.
- **5.2d's TLS 1.2 floor** — `TLS_PSK_WITH_AES_128_GCM_SHA256` as the interoperable floor with ECDHE_PSK preferred where nameable — matches what we measured exactly. `0x00A8` is what we negotiate and it is what the document now says to expect.
- **5.2b's split** — TLS 1.3 `psk_dhe_ke` where reachable, TLS 1.2 ECDHE_PSK where nameable, plain PSK only where not — describes the real landscape rather than an aspiration, and puts us in the third bucket honestly.
- **RT-2's extension** to assert `v` is first in the all-fields payload. This is the test that would have caught V1, and it now exists.

## 5. Outstanding from my previous review

**§4.3b is still unqualified**, and the specification's own vectors still contradict it:

> **(4.3b) MUST** Every payload key other than `v` is **at least two characters.**

The `ep` map uses `h` and `p`; the `wifi` map uses `h`, `k` and `s`. Read literally, RT-2 rejects the document's own normative test vectors. It is harmless functionally — nested ordering cannot affect where `v` sorts in the top-level map — but it is in §4, which cannot be corrected after a code is printed, and the fix is one word: *"Every key of the **top-level payload map** other than `v`…"*.

I raised this against Draft 2 and cannot find it dispositioned. It may simply have been missed among the larger items, which is why I am restating it rather than assuming it was declined.

## 6. Summary

| # | Item | Status |
|---|---|---|
| 1 | The relaxation, and the reasoning for it | **Agree.** Decided on the data's sensitivity, not on the mechanism being awkward — which is the right basis |
| 2 | 5.2b1 and property 3 are unassertable on our platform; 5.2i does not cover them | **Fix** — widen 5.2i, one line |
| 3 | §10.2's binary PSK identity verified working at TLS 1.2 | ✅ De-risks the fallback |
| 4 | §4.3b still contradicted by the document's own vectors | **Restated** — possibly missed rather than declined |
| 5 | Whether the absence of forward secrecy is user-visible | Ours to decide, not the protocol's — recorded so it is decided |

No blocking objection. With §2 and §4 addressed I would agree `PPCP-RV`, subject to the on-device confirmation in §3 being done once before it is declared final.
