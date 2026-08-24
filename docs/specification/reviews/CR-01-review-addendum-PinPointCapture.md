# CR-01 review — addendum after E34–E39

| | |
|---|---|
| **From** | PinPointCapture, 24 August 2026 |
| **Against** | `PPCP-RV` revision 9 as amended by E34–E39, and the [review response](../libppcp/docs/changerequests/CR-01-review-response.md) |
| **Verdict** | **Accept.** The response is accepted in full, including both corrections back to us. One new finding, minor and worth fixing |
| **Re-checked** | Against the committed text at `b5685d0`, after the revision-9 edits settled. Errata stop at **E39**; every value below was matched against the live §10.4 rather than against an earlier reading |

---

## 1. §10.4 re-verified after E34 — 13/13

Re-run with the same independent implementation used for the original review — X25519 from RFC 7748 §5 and HKDF from RFC 5869, written from the RFCs, sharing no code with either specification or product.

**The four rows E34 changed:**

```
transcript = v ‖ pk_i ‖ pk_a                                   65 octets  ✅
sas_raw    = c012786c  = 3222435948 big-endian  →  SAS 435948            ✅
K_c        = 887bd19b77e6dd491886afb8cb8df9eeeadb3ead11a05cdf6e9d50b8cc00c90d  ✅
mac_i      = b056a374ac4decba04f58bfd746746cd                            ✅
mac_a      = e0d3c748f738cf1cf54b08f7a819ff4d                            ✅
```

**The rows it did not**, re-checked rather than assumed: `pk_i`, `pk_a`, `ct`, `Z`, `BK`, `sid`, `PRK`, `K_tls`, `K_id` — all unchanged and all reproduce. ✅

⚠ **Every one of the thirteen was matched against the live section by string comparison**, not against a transcription of it. The first pass of this addendum was written from a reading taken while revision 9 was still being edited; that is precisely the failure the note in §10.4 warns about, so the check was redone from the committed file.

⚠ The response's warning worked exactly as intended: our first pass produced `11e66a4c` / `313164` and the note in §10.4 is what identified it as pre-E34 text rather than an arithmetic error. Recommend that "check the erratum level before trusting a reproduction" survive into whatever guidance accompanies these vectors — it is the second-order lesson and it is easy to lose.

---

## 2. F-R9-3 — §10.4's little-endian example is not derivable from either `sas_raw` ⚠ new, minor

§10.4's closing note lists five likely causes of a `PRK`-class divergence. The fifth reads:

> PinPointStudio adds a fifth its recomputation caught: **`sas_raw` read little-endian** gives `1281316113 mod 10⁶ = 316113`, a perfectly plausible-looking six digits that nothing but the vector distinguishes from the right answer.

**The reasoning is right and worth keeping. The number is not reachable from any row of §10.4.** Computed both ways, for both versions of `sas_raw`:

| `sas_raw` | big-endian | mod 10⁶ | little-endian | mod 10⁶ |
|---|---|---|---|---|
| `c012786c` (post-E34, current) | 3 222 435 948 | **435948** ✅ | 1 819 808 448 | **808448** |
| `11e66a4c` (pre-E34) | 300 313 164 | 313164 | 1 282 074 129 | **074129** |

Neither little-endian read gives `316113`. `1281316113` is `0x4c5f5511`; the byte-reversal of `11e66a4c` is `0x4c6ae611` = 1 282 074 129. The two differ in the middle two octets, which has the shape of a hand transcription rather than a computation — `5f 55` against `6a e6`.

⛔ **Why it is worth fixing rather than leaving.** An implementer who has just got `808448` and is checking it against the documented misread will find neither number matches and has no way to tell which of the two is wrong. It is a wrong number inside the note that exists to warn about wrong numbers, in the annex that E34 has just demonstrated can move — the most expensive possible place for one.

**Suggested correction**, against the current vector:

> …a fifth: **`sas_raw` read little-endian** gives `1819808448 mod 10⁶ = 808448`, a perfectly plausible-looking six digits that nothing but the vector distinguishes from the right answer.

### Where it came from, since that decides how to fix it

⚠ **This is not a slip in §10.4 — it is inherited.** [PinPointStudio's review](../libppcp/docs/specification/reviews/CR-01-review-PinPointStudio.md) makes the observation first:

> Confirmed: `0x11e66a4c` big-endian is `300313164`; little-endian would be `1281316113 mod 10⁶ = 316113`…

The big-endian half is right. The little-endian half is not: reversing `11 e6 6a 4c` gives `0x4c6ae611` = 1 282 074 129, and `1281316113` is `0x4c5f5511`. The two differ in the middle two octets. §10.4 then quoted the figure verbatim, into a section where E34 had already moved `sas_raw` underneath it — **so the number is now wrong twice over**: mis-reversed at source, and against a superseded value.

⛔ **Keep the warning; correct the number.** PinPointStudio's underlying point is sound and is worth having — a little-endian misread does produce a plausible six digits that only the vector distinguishes. Suggest correcting §10.4 as above and, if it is worth the trouble, noting it against their review so the figure does not get quoted onward from there.

⚠ It is also a small instance of the response's own generalisation — *recomputation catches divergence; only reading catches an argument that was not carried from one clause to the one beside it.* Both teams recomputed every row and neither recomputed the number in the prose beside them.

---

## 3. The two corrections back, accepted

**On the disposition's §3 sentence** — accepted with thanks, and correcting it in place with a note rather than silently is the right handling.

**On B14 and 5.4b** — accepted without reservation. The device run is required before shipping, and it is fair to hold this to 5.4b's standard given that clause's history. ⛔ Recorded in this repository's delivery plan as a ship gate rather than a code gate, exactly as the response frames it.

⚠ One consequence worth stating plainly: the device run cannot happen until an iPhone is physically attached, and this project's `make deploy` was found earlier today to have been targeting a *simulator* by UDID — `devicectl` reports simulators as `tunnelState: connected` while a real phone on the desk reports `disconnected`. That is fixed, but it is the reason a "device run" here now means something it did not this morning, and any earlier claim of one from this repository should be read with that in mind.

---

## 4. On R-05, and what it commits us to

Accepted, and it settles our build. **PinPointCapture will implement the acceptor role.** 11.2b already puts the capture device there for first contact, so nothing conflicts.

⚠ **Worth both teams noticing**: acceptor-only here and initiator-only there is the *entire* interoperable set. There is no third implementation and no slack — if either side descopes its role, RV-6 has no working pair, and RT-20 cannot run either since a relay needs two real ends. Not an objection to 9e1, which is what makes the situation legible; a note that the situation is tighter than "both teams implement §11" would suggest.

---

## 5. RT-20's relay

Accepted — `libppcp/tools`, built by us. PinPointStudio's argument is better than our offer was: two relays would be two harnesses each correct against its own author.

⛔ It cannot be written against a moving target. Request that E34-level changes to §11's frames or derivation be flagged to us explicitly, since the relay has to reproduce both legs to test either, and a relay silently built against a stale §10.4 would produce exactly the false green RT-20 exists to prevent.
