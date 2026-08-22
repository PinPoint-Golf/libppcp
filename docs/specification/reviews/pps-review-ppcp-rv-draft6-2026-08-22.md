# Design review — PPCP-RV Draft 6

**Reviewed as owner of PinPointStudio, the host implementation. Fifth pass.**

| | |
|---|---|
| Document reviewed | `ppcp-rv.md` — `PPCP-RV` 1.0 Draft 6, payload version `ppcp1`, and the RV review disposition |
| Reviewer | PinPointStudio maintainer |
| Prior passes | V1–V4 (Draft 1), W1–W3 (Draft 2), R1–R6 (Draft 4), N1 (Draft 5) |
| Method | Re-verified every §10 vector for the fifth time; read §5.4.1 and §5.4.3 against the on-device result |
| Date | 22 August 2026 |
| **Verdict** | **Approved for implementation.** No blocking findings. One clarification worth a sentence, and one open item that is the owner's rather than mine. |

---

## 0. Position

N1 is fully carried, in all three parts, and the measurement that has been outstanding since Draft 2 is
in.

**5.4b1 discharges the gate, and the result is unfavourable.** The check was repeated on an iPhone 16 on
the release operating system — not a beta — and is *"identical to the desktop result in every
respect"*: TLS 1.3 with an external PSK fails, both ECDHE_PSK suites are silently ignored, and `0x00A8`
is negotiated. The platform difference that could not be ruled out does not exist.

That closes the objection I have carried through three passes. My concern was never Route D itself — it
was that a security property had been given up on the strength of a proxy measurement, with the
confirmation demoted to *"no longer gates anything"*. It gated the premise, the premise is now
confirmed on the hardware, and **no clause changed**, which is exactly how 5.4b was worded so that
neither outcome would require a redraft. That is the cleanest possible resolution of a disagreement
about evidence, and it is worth recording that the answer went against me on measurement rather than on
argument.

**5.4b2 is a bonus worth noting**: the same run confirmed 5.3f on the device — the 17-octet binary
identity completes a handshake at TLS 1.2 unchanged. The RFC 4279 UTF-8 question was the most likely
second-order casualty of the floor, and it is now measured rather than reasoned about, on the target
hardware.

**N1's three parts are all resolved and one is resolved better than I asked.** 5.4j scoped to a network
the peer does not control; 5.4j1 putting bundles out of scope, with the right reason — what §5.4.3 gave
up is confidentiality in transit against a passive recorder, and a file on the device's own storage has
no recorder on the wire; and 5.4j2 as a **MUST** rather than the permission I proposed. Making it a MUST
is better: withholding must not become unbounded retention of the very material the clause protects, and
a SHOULD there would have been read as optional by the implementation under storage pressure.

**Vectors re-verified, fifth pass.** All eleven reproduce byte-for-byte; the all-fields payload is 133
octets, 183 URI characters, first four `a8 61 76 01`. §4 has now survived four passes and four
independent recomputations, and I have nothing further on it.

---

## 1. Clarification

### E1 — how a peer determines that it does not control the network {#e1}

**Severity: low. Not blocking. One sentence, and worth having because the two defaults behave in
opposite directions.**

> **(5.4j) SHOULD** A peer does not transfer candidate-attached audio payload over a connection that did
> not achieve forward secrecy **and is carried over a network the peer does not control** — shared or
> public infrastructure, rather than a wired tunnel or a publisher-provided hotspot…

The scoping is right, and it is the right axis. But nothing says how a peer decides which side of it a
given connection falls on, and it is not self-evident: a peer sees an interface and an address, not a
provenance.

It is cheaply determinable from things the peer already knows, and the determination should be stated
rather than inferred, because a peer that cannot tell will default one way or the other and the two
defaults give opposite behaviour on the same network.

> **(5.4j3)** A peer treats a network as **controlled** where it joined through the pairing code's
> `wifi` block ([§6](#6-rv-4--network-join)), or where the transport is a direct one — a wired tunnel,
> or a socket handed in by an embedding application ([§2](#2-rendezvous-paths)). **Every other network
> is uncontrolled**, including one the user selected manually and one the peer was already associated
> with when the code was scanned. The default is uncontrolled, because a peer that cannot establish
> provenance has not established it.

The default matters more than the rule. A range's guest WiFi and a studio's own access point are
indistinguishable to the peer, and only one of them is the case 5.4j is about — so the conservative
default is the correct one, and it costs nothing because a studio deployment can supply the `wifi` block
or use a tunnel and thereby say so.

---

## 2. Not mine to close

**Whether 5.4j stands or is deleted.** The owner's word on whether §5.4.3's sensitivity judgement covers
candidate audio. My position is unchanged and I have no further argument to make: the reasoning in
§5.4.3 names the audio as the part carrying a privacy dimension, and 5.4j applies that reasoning to it.
If the owner's judgement covers the audio too, deleting the clause is a legitimate answer and the text
already says that is how it should be removed.

What Draft 6 has done correctly is make either answer cheap. The clause is a SHOULD with an explicit
escape, its removal is described, and §5.4.3 no longer names an exception that nothing acts on. That was
the outcome I actually wanted, rather than a particular answer.

**B13 — whether the absence of forward secrecy should be user-visible.** Still recorded as a product
question, still correct that it is one. From the host seat, unchanged: record it in the diagnostic export
as 5.4k requires, and keep it out of the ordinary session UI. A permanent warning a user cannot act on
teaches them to ignore warnings.

---

## 3. What I checked and confirmed

- **The on-device measurement.** Same four attempts, same results, release OS, target hardware. The
  cause stated as structural rather than incidental — *"the platform's ciphersuite enumeration contains
  no PSK suites at all, so the suite it actually negotiates cannot be named by the public interface"* —
  is the finding that makes Route A or B the only alternatives, and it is now confirmed twice.
- **5.4j2 as a MUST.** The cross-reference into `PPCP-CORE` 5.14g's exits is correct, and the two
  documents now agree: withheld payload is one of I38's stated exits, so a peer may shed it. That was a
  cross-document collision when I raised it and it is closed on both sides.
- **5.4j1's reasoning.** Protected at rest and by physical control, *"which the relaxation never
  touched"*. Right, and the distinction is the one that makes the scoping principled rather than
  convenient.
- **§4 unchanged and stable.** Four passes, four recomputations.
- **B8 closed properly** — with the note that no clause changed, which is how 5.4b was written.
- **§8's PSK-interface paragraph** still carries the detail that will actually save time: a wrong hash
  fails indistinguishably from a key mismatch, so check it first.

---

## 4. Sign-off

**`PPCP-RV` Draft 6 is approved for implementation.**

§4, §6 and §7 without reservation. §5 I now also approve without reservation: the mechanism rests on a
measurement that has been taken on the hardware it concerns, twice, with the same result, and the
document says plainly what was given up and on whose authority.

E1 is a clarification rather than a finding and blocks nothing; it should land whenever §5.4 is next
touched.

One open item remains and it is not a drafting question: the owner's word on 5.4j.

---

## 5. Closing

Five passes. The first found the arithmetic wrong in the one section that cannot be corrected after
release; the last finds a sentence missing about how to classify a network. That distance is the whole
value of the exercise.

The thing I will remember from this document is not a finding. It is that a disagreement between two
implementation teams and a product owner, over a real security property, was resolved by someone
scheduling a measurement, running it on the actual hardware, and writing the result down whichever way
it came out — with both dissenting positions quoted rather than summarised. **I was overruled and then
proved wrong by data, in that order, and both steps are legible in the document.** That is a better
outcome than being agreed with would have been, and it is the reason I have no reservations left about
a clause I argued against twice.
