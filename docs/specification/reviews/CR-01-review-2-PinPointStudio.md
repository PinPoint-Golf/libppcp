# CR-01 review pass 2 — PinPointStudio on the response and errata E34–E39

| | |
|---|---|
| **Reviewing** | [CR-01 review response](../Projects/libppcp/docs/changerequests/CR-01-review-response.md), 24 August 2026, and `PPCP-RV` revision 9 as amended by **E34–E39** (`a992af5` → `b5685d0`) |
| **Reviewed by** | PinPointStudio, 24 August 2026 |
| **Position** | **Accept, and close it.** All six findings are applied correctly, E34 goes further than R-01 asked and is right to, and nothing in the response reopens anything. |
| **Vectors** | **RT-18 re-verified under E34. All fifteen rows reproduce**, the four that changed change exactly as specified, and `PRK` / `K_tls` / `K_id` are byte-identical to pre-E34 as the response says they must be. |
| **Findings** | Four. **None touches the wire, the design or the security argument.** Two are arithmetic errors in published prose — and two of the three wrong numbers are mine, carried over from pass 1. One is a new trap the E34 prose created and should be fixed before anyone codes. One is a pair of missing sentences. |

**Nothing was changed in the `libppcp` repository.**

---

## 1. The vectors, recomputed under E34

Recomputed from the amended text with the same independent implementation as pass 1 — RFC 7748 Montgomery ladder written from the RFC, HKDF/HMAC over `hashlib`, sharing no code with the author's.

```
transcript = 01 || pk_i || pk_a                          65 octets   ✔ (spec says 65)

sas_raw  c012786c   ✔     = 3222435948 BE     SAS 435948   ✔
K_c      887bd19b77e6dd491886afb8cb8df9eeeadb3ead11a05cdf6e9d50b8cc00c90d   ✔
mac_i    b056a374ac4decba04f58bfd746746cd   ✔
mac_a    e0d3c748f738cf1cf54b08f7a819ff4d   ✔

UNCHANGED, and confirmed unchanged:
expand   1cc4b886e8bd65e063b207ae783bc56b   ✔
sid      1cc4b886e8bd45e0a3b207ae783bc56b   ✔
PRK      3e351aef1e5fe48411e969526b079830494d2cf13104d661694e897598ccf8c9   ✔
K_tls    240b513437501f3ab8602b06b45cd84577f10f126bdc497d3cf797c9559856b0   ✔
K_id     9e8c8b155b89fcc9b70f4043ddaa607a7ff7acec20dc326f5c307661956a0bd9   ✔
```

The warning box in §10.4 is doing its job: a recomputation against the pre-E34 text yields `11e66a4c` and SAS `313164`, which is exactly what this pass would have produced had it not re-read the section. Worth keeping.

**E34 is right to go further than R-01 asked.** R-01 proposed binding the transcript into `K_c`. Binding it into `sas_raw` as well moves the detection from the MAC to the digits — in front of the operator, *before* anyone affirms — rather than after a successful-looking comparison. That is the better place, for the reason §11.8 gives, and it was not in the finding.

---

## 2. Findings

### R-07 — the modulo-bias paragraph has two wrong numbers. **One of them is mine.**

[11.7](../Projects/libppcp/docs/specification/ppcp-rv.md#117-the-short-authentication-string), the paragraph beginning *"The modulo bias is real, immaterial, and recorded here so it is not raised twice more"*:

> 2³² is not a multiple of 10⁶, so **295 967 296** of the residues have 4 295 preimages and the rest 4 294 — a **relative bias under 2.3 × 10⁻⁷**, worth a factor of about 1.000 23 to an attacker targeting the most probable string.

Three claims, computed:

| Claim | Correct | |
|---|---|---|
| `295 967 296` residues have 4 295 preimages | **967 296** — and the remaining **32 704** have 4 294 | Wrong, and not a possible count: there are only 10⁶ residues in total. The favoured set is also the **large** one (96.7%), which *"and the rest"* reads the wrong way round. |
| relative bias under `2.3 × 10⁻⁷` | **7.61 × 10⁻⁶** | **My error, from pass 1, taken in good faith.** Thirty times larger than stated. |
| a factor of about `1.000 23` | correct — but it is 4 295 / 4 294, the **most-probable to least-probable** ratio | Right number, different quantity from the one beside it. Against uniform the factor is 1.000 0076. |

```
2^32 mod 10^6 = 967296 residues with 4295 preimages; 32704 with 4294
p(most probable) / (1/10^6) - 1  =  4295e6 / 2^32 - 1  =  7.614e-6
p(most probable) / p(least probable) = 4295/4294       =  1.0002329
```

**The conclusion is untouched** — still immaterial, still unsteerable by an attacker who commits blind, still doesn't move the 2⁻²⁰ bound. Suggested replacement for the first sentence:

> 2³² is not a multiple of 10⁶, so 967 296 of the residues have 4 295 preimages and the remaining 32 704 have 4 294 — an excess over uniform of 7.6 × 10⁻⁶ for the most probable string, and a factor of 1.000 23 between the most and least probable.

I would rather this were right than quietly carried, because the paragraph's stated purpose is *"so that the third reader does not spend the same afternoon"* — and a third reader who checks the arithmetic will spend exactly that afternoon.

### R-08 — §10.4's little-endian example is wrong, and E34 has now made it stale as well. **Also mine.**

[§10.4](../Projects/libppcp/docs/specification/ppcp-rv.md#104-guided-pairing), last line of *"Read the `PRK` row as the one that matters"*:

> PinPointStudio adds a fifth its recomputation caught: **`sas_raw` read little-endian** gives `1281316113 mod 10⁶ = 316113`.

Both numbers are wrong, and they were wrong when I wrote them in pass 1:

```
pre-E34   11e66a4c  little-endian = 1 282 074 129   ->  074129     (not 1281316113 / 316113)
post-E34  c012786c  little-endian = 1 819 808 448   ->  808448     (the value that now applies)
```

Two problems compounding. The example is arithmetically wrong; and it is computed from the **pre-E34 `sas_raw`**, three lines below the warning box that exists to stop exactly that. An implementer whose byte order is wrong will print **808448** and will not find it in the list of likely causes — which is worse than the list not mentioning byte order at all, because the list reads as authoritative.

Suggested: `sas_raw` read little-endian gives `1 819 808 448 mod 10⁶ = 808448`.

The underlying point stands and is worth keeping: little-endian yields a perfectly plausible six digits that nothing but the vector distinguishes from the right answer.

### R-09 — 11.6c's one-sentence rule is contradicted two clauses later, in the direction that produces the failure §10.4 calls worst. **The one I would fix before anyone writes code.**

[11.6c](../Projects/libppcp/docs/specification/ppcp-rv.md#116-derivation) now states the binding as a general rule:

> **Everything that varies between two otherwise-identical exchanges is bound into everything derived from them**, which is one rule rather than three and is the shape an implementer is least likely to get partly right.

It is not true of the two clauses that follow it:

```
11.6d   sid = HKDF-Expand(BK, "ppcp1 bootstrap-sid", 16)      <- no transcript
11.6e   PRK = HKDF-Extract(salt = sid, IKM = Z)               <- no transcript, no public keys
```

`sid` and `PRK` are pure functions of `Z`. The rule says they are bound to `v || pk_i || pk_a`; they are not.

**Why this matters more than a wording slip.** The sentence is offered explicitly as the thing an implementer should hold in mind *instead of* three separate info constructions — it is presented as the safer abstraction. An implementer who follows the rule rather than the formulas binds the transcript into `sid` as well. That implementation then produces:

- **matching digits** — `sas_raw` is transcript-bound in both, so the operator sees a successful comparison;
- **matching MACs** — `K_c` is transcript-bound in both, so the confirmation passes;
- **a divergent `PRK`** — and the pairing fails at the TLS handshake with `PSK_IDENTITY_NOT_FOUND`.

That is precisely the failure §10.4 names as the one that matters and warns *"will be diagnosed as"* the 3.5d platform limitation. Before E34 the trap did not exist, because there was no general rule to over-apply. RT-18's `PRK` row catches it — which is an argument for stating the boundary, not for relying on the test.

**Two small changes close it.** Scope the sentence to what it describes: *"Both of these expansions bind everything that varies between two otherwise-identical exchanges"*. And add one line to 11.6d/11.6e saying the transcript is deliberately **not** bound there, with the reason — by the time `PRK` is derived the exchange has already been authenticated by the comparison and the MACs, and [§5.1](../Projects/libppcp/docs/specification/ppcp-rv.md#51-key-derivation) is taken verbatim, so its inputs cannot be changed by this section without giving §5.1 a second shape. That is [A17](../Projects/libppcp/docs/specification/ppcp-rv.md#annex-a--decisions-and-alternatives)'s argument again and it is a good one; it just needs saying, because the new rule now implies the opposite.

### R-10 — `v`'s width is unbounded in 11.4b and one octet in 11.6c; and whose `v` is bound is unstated. **A sentence each.**

**Width.** [11.4b](../Projects/libppcp/docs/specification/ppcp-rv.md#114-frames) defines `v` as *"an unsigned integer"* — a CBOR uint, so up to 2⁶⁴−1. [11.6c](../Projects/libppcp/docs/specification/ppcp-rv.md#116-derivation) encodes it as *"ONE octet"*. Today `v` is `1` and the question does not arise; the transcript construction becomes undefined the first time it does. Suggest: `v` is 1..255, and a `v` outside that range is `malformed` under [11.4c](../Projects/libppcp/docs/specification/ppcp-rv.md#114-frames).

**Which `v`.** The transcript is a normative derivation input, and the document does not say whether a peer binds the `v` it *sent* or the `v` it *received*. I worked the both-directions rewrite through every consistent reading and **all of them detect it**, so this is not a hole:

| Reading | initiator binds | acceptor binds | rewrite detected |
|---|---|---|---|
| each binds what it sent | 2 | 1 | ✔ digits differ |
| each binds what it received | 2 (rewritten echo) | 1 (rewritten offer) | ✔ digits differ |

But an unstated derivation input is exactly what RT-18 exists to catch and §10.4 cannot, because the vector carries one value of `v` and every reading agrees on it. One sentence: *the initiator binds the `v` it sent; the acceptor binds the `v` it received and echoed.*

---

## 3. Checked and sound — recorded so it is not re-checked

- **The sequential mirror of R-02 is not a second hole.** [11.3d1](../Projects/libppcp/docs/specification/ppcp-rv.md#113-roles-and-the-connection) closes the parallel case. An initiator dialling attacker-controlled windows *one after another* is different in kind: [3.7b](../Projects/libppcp/docs/specification/ppcp-rv.md#37-the-bootstrap-window) does not bind an attacker's own window, so it can reopen freely — but every attempt still costs **one operator comparison**, so §11.8's *"one million operator confirmations"* survives intact. R-02 was dangerous precisely because one operator action covered N draws. [11.9d](../Projects/libppcp/docs/specification/ppcp-rv.md#119-aborting-and-the-one-attempt-rule)'s SHOULD is adequate here and does not need to become a MUST.
- **11.4h and 11.4i together are sufficient.** The single-direction rewrite is caught by the echo; the both-directions rewrite by the transcript, twice over (digits first, MACs second). No third case found.
- **E38's offline-verifier statement is correct and correctly scoped.** The MACs descend from `Z` by public functions, so a recorded transcript tests any candidate `Z` — and since `sid` and `PRK` are pure functions of `Z`, it tests the `PRK` too, which the response says. The qualifications are right: it is [7.2a](../Projects/libppcp/docs/specification/ppcp-rv.md#72-handling-the-pairing-secret) arriving on this path rather than a property peculiar to it, and encrypting the bootstrap connection would not remove it.
- **`PRK`, `K_tls` and `K_id` are genuinely unchanged under E34** — reproduced above, not assumed.
- **9e1, 11.3d1, 11.4f's replaced reasoning, 11.6b's rewording, RT-21, RT-24, RT-25 and RT-26** are all accepted as written. RT-25 being a `review` row is right for the stated reason: a peer violating 11.3d1 completes handshakes that are byte-for-byte conformant.

---

## 4. Where this leaves PinPointStudio

Unchanged from pass 1 except where the response settles it:

- **B14 — discharged for the host.** OpenSSL 3.6.3, raw X25519 through `EVP_PKEY_derive` with no TLS, RFC 7748 §6.1 reproduced. Noted that PinPointCapture's run was on the simulator SDK and that the device run gates shipping, not coding — agreed, and it is not ours to run.
- **RT-18 — passes, now twice**, pre- and post-E34, on an implementation sharing no code with the author's.
- **B17 — accepted as the right home for the X25519 seam**, and agreed that a wire specification is the wrong place for an injected-callback API. The `libppcp` API work is ours and PinPointCapture's to schedule.
- **RT-20 in `libppcp/tools`** — agreed, and this host will still supply the relay side. The response is right that it has not moved and should not be allowed to feel as though it has.
- **Q3 — still Mark's to answer**, and the recommendation stands unchanged: yes on macOS, where `DNSServiceRegister` needs no new responder and no 5353 bind; deferred on Windows, where there is no `dns_sd.h` and the browser is compiled out entirely.
- **R-06 / 3.7h** — accepted as recorded. No clause change sought, and the host-side work item is *how a window is reached without multicast*.

**On R-07 and R-08.** Two of the three wrong numbers came from PinPointStudio's own pass-1 review and were taken in good faith. The arithmetic is corrected above with the working shown; the conclusions both numbers supported are unaffected. Recording it plainly because a specification that says *"both review teams found it independently"* has more weight than it should if one of the teams got the figure wrong.

---

## 5. Summary

| | Finding | Ask | Urgency |
|---|---|---|---|
| **R-07** | Modulo-bias paragraph: `295 967 296` impossible, `2.3 × 10⁻⁷` wrong by 30× | Replace the sentence; working shown above | Before the revision is circulated further |
| **R-08** | §10.4's little-endian example wrong, and stale since E34 | `1 819 808 448 mod 10⁶ = 808448` | Same — it sits in a diagnostic list |
| **R-09** | 11.6c's general rule contradicted by 11.6d/11.6e, toward the `PRK`-divergence failure | Scope the sentence; one line on why `sid`/`PRK` are deliberately unbound | **Before either team writes §11** |
| **R-10** | `v` unbounded in 11.4b, one octet in 11.6c; whose `v` is bound unstated | Bound `v` to 1..255; one sentence on which `v` each peer binds | With R-09 |

None of these changes the wire, the vectors, or the security argument. **From this side §11 is finished bar R-09's two sentences**, and the position on RT-20 is unchanged: until it runs between two real implementations across a deliberate relay, no conformance claim to RV-6 should be made.
