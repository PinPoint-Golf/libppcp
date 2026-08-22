# PPCP-RV Draft 6 — review from the PinPointCapture mobile app

| | |
|---|---|
| Reviewing | `ppcp-rv.md` Draft 6 and the RV round-4 disposition |
| Seat | Owner of the PinPointCapture iOS/iPadOS app |
| Date | 22 August 2026 |
| Verdict | **Approve for implementation, without reservation.** No findings. One correction to the record that is mine to make, and a recommendation on the single item the owner still holds. |

## 1. §4.3b — I was wrong, and the disposition is too generous about why

The disposition records that I raised §4.3b three times against text that had been correct since Draft 3, and attributes it to *"a distribution problem rather than a specification one"* — a stale copy under review.

**That is not what happened, and I would rather correct it than accept the charitable reading**, because a distribution fix would be effort spent on a problem that does not exist.

I checked the history of the clause in the repository:

| Draft | `4.3b` reads |
|---|---|
| Draft 2 | *"Every payload key other than `v`"* — unqualified. **My first raising was correct.** |
| **Draft 3** (`5aa01c0`) | *"Every key of the **top-level payload map** other than `v`"* — **fixed** |
| Drafts 4, 5, 6 | unchanged |

I read the file directly from the repository each round. The copy was current every time. What I did was **restate the finding from my own previous review rather than re-reading the clause** — so I quoted Draft 2's wording twice more, against text that no longer said it, and then asked to be told whether it had been declined.

The lesson is mine and it is the same one this document set has been teaching all along: *a vector that exercises only the common case validates only the common case*, and a finding restated from memory validates nothing at all. I have no defence and no process suggestion for the team — the fix is that I re-read the clause, which I have now done. 4.3b1 naming `h`, `p`, `k` and `s` explicitly is a good addition and would have stopped me on a re-read even if I had been careless again.

## 2. N1 and the 5.4j scoping — endorsed, and it matters more to us than the host

The host's finding is right on all three counts, and the second and third are ours to live with:

- **The deferral escape hatch did not exist.** At the measured TLS 1.2 floor, *"a connection that did achieve forward secrecy"* is a connection that will never occur on our leg. An implementer — us — would have built a deferral queue that could never drain. That is precisely the kind of clause that reads as a route and is a dead end.
- **The I38 collision** would have been perverse: a clause added to *reduce* the exposure of candidate audio would have pinned every window of it on the device permanently. 5.4j2 making withheld payload evictable, and revision 8 making it one of I38's named exits, closes it.
- **5.4j1 putting bundles out of scope is the one that matters most to us.** UC-1 has no host at all and a range session exports later, so **the bundle is how this payload overwhelmingly travels for our primary use case.** Unscoped, 5.4j would have been either notional or would have removed candidate audio from the offline path entirely — taking with it the diagnostic purpose the whole candidate-attached retention design exists for. The reasoning given is also correct: what §5.4.3 gave up is confidentiality in transit against a passive recorder on an untrusted network, and a file on the device's own storage has no recorder on the wire.

## 3. On the open item — whether 5.4j stands or is deleted

This is the owner's call and both reviewers have said either answer is workable. Since it is the last thing open, here is a recommendation rather than a shrug.

**I would delete it**, on three grounds:

1. **Its remaining scope is narrow.** With bundles out (5.4j1), it applies only to live transfer over a network the peer does not control. Our two live cases are a studio with a host-provided hotspot or a cable (UC-2), and a range where the host is usually absent entirely (UC-1, UC-3). The uncontrolled-network case is real but it is the minority of the minority.
2. **It creates a user-facing decision we have not designed and would have to.** REQ-PRIV-2 requires audio retention policy to be explicit, user-visible and configurable, and honestly reflected in the privacy label. A per-connection "withhold candidate audio?" choice is a second, differently-shaped control on the same subject, and two controls over one thing is how a user ends up unsure what they have agreed to.
3. **§5.4.3's judgement was made on the sensitivity of the data carried**, and candidate audio was part of what was weighed. Carving it back out afterwards reopens a decision that was taken properly, and does so in the one place where the exposure is smallest.

**If it stands**, it is now implementable and I have no objection — 5.4j1 and 5.4j2 between them make it a real clause rather than a notional one. What I would not want is the third state: an exception named in §5.4.3 that nothing acts on, which Draft 6 already prevents by default.

## 4. Confirmations

- **§5.4b is discharged** and Draft 6 rests §5 on a measurement taken on the hardware it concerns — iPhone 16, iOS 26.6, matching the desktop result exactly. TLS 1.3 external PSK unreachable, ECDHE_PSK unselectable, plain `0x00A8` the only mode, and the 17-octet binary identity of §10.2 working unchanged. Nothing about that result has moved and nothing in Draft 6 depends on it having.
- **§4** has now survived four passes and three independent recomputations, one of them mine, all byte-exact. I have nothing further on the part of the document that cannot be changed later.
- **§6 and §7** — no findings, as at Drafts 4 and 5.

## 5. Summary

| # | Item | Status |
|---|---|---|
| 1 | §4.3b | **My error, corrected.** Fixed in Draft 3; the copy was never stale. No action for the team |
| 2 | N1 / 5.4j scoping | ✅ Endorsed; 5.4j1 is the clause that matters for UC-1 |
| 3 | Whether 5.4j stands | **Recommend deletion**, reasons above — owner's call |
| 4 | §5.4b measurement | ✅ Discharged on device |

**Approve for implementation.** I have no outstanding findings on `PPCP-RV`.
