# Design review — PPCP-RV Draft 5

**Reviewed as owner of PinPointStudio, the host implementation. Fourth pass.**

| | |
|---|---|
| Document reviewed | `ppcp-rv.md` — `PPCP-RV` 1.0 Draft 5, payload version `ppcp1`, and the RV review disposition |
| Reviewer | PinPointStudio maintainer |
| Prior passes | V1–V4 (Draft 1), W1–W3 (Draft 2), R1–R6 (Draft 4) |
| Method | Re-verified every §10 vector for the fourth time; read §5 against the disposition of R1–R6 |
| Date | 22 August 2026 |
| **Verdict** | **Approved for implementation**, with one finding to close and two things that are not mine to close. §4, §6 and §7 I approve without reservation. |

---

## 0. Position

All six of R1–R6 are carried, and three of them beyond what I asked.

**R1** — RT-4 is rewritten against 5.2b1 rather than a version number, which is what the requirement
had become. **R2** — the gate is restored as 5.4b, worded so a favourable device result changes no
clause at all; that is better than what I proposed, because it removes any incentive to prefer one
outcome. **R3** — RT-17 exists with the operative instruction attached: re-read it whenever a platform
SDK updates, because a platform that gains TLS 1.3 external PSK silently restores property 2 for an
implementation that asks rather than assumes. **R5** and **R6** are done as asked.

**R4 was taken further than I expected and in the right way.** 5.4j is a SHOULD with an explicit
statement that it is deleted rather than worked around if the owner's judgement covers candidate audio.
Recording that the specification has gone slightly beyond the decision, and saying which way it should
be resolved, is the correct handling of a reviewer's point that reaches past the reviewer's authority.

**And the mobile team's second measurement is the most valuable thing in Draft 5**, more than any
finding of mine. RFC 4279 says a PSK identity "should" be UTF-8; 5.3a mandates 17 raw octets that are
not. That is the second-order casualty of the TLS 1.2 floor that nobody had looked for, and it was
tested with the actual §10.2 vector rather than reasoned about. 5.3f recording it as a requirement —
never transcode, validate as text, or truncate — is exactly right.

**Vectors re-verified, fourth pass**: `PRK`, `K_tls`, `K_id`, `rid`, the PSK identity tag and full
17-octet identity, the canonical UUID text, both payloads and both URIs. All reproduce byte-for-byte;
the all-fields payload is still 133 octets, 183 URI characters, first four `a8 61 76 01`.

One finding follows, and it is about where 5.4j does and does not reach.

---

## 1. Finding

### N1 — 5.4j protects the connection and leaves the bundle, which is how the payload it protects actually travels {#n1}

**Severity: high, and it is the last one. It exists only because 5.4j is new.**

> **(5.4j) SHOULD** A peer does not transfer candidate-attached audio payload **over a connection that
> did not achieve forward secrecy**, unless the user has been told and has agreed… its payload is
> withheld, or **deferred to a connection that did**.

Two problems, and the second is the one that matters.

**The escape hatch does not exist.** The disposition's own §15.7 records the mobile team's point: at the
TLS 1.2 floor, *every* session between their app and any host lacks forward secrecy — not some.
So "deferred to a connection that did" names a connection that will never occur. 5.4j therefore
reduces to *withhold permanently, unless the user agrees*, and the clause reads as though it offers a
route it does not. Worth saying plainly, because an implementer will build the deferral queue.

**And withholding it forever collides with `PPCP-CORE` I38.** Revision 7 added: *a peer MUST NOT evict a
Capture whose `transfer` is not `confirmed`, whatever its retention policy*. A withheld candidate-audio
Capture stays `pending`, never becomes `confirmed`, and therefore may never be deleted. Since the
candidate count *"is not bounded by anything the user does"* (`CORE` §13c), a clause added to reduce
the exposure of candidate audio has the effect of retaining every window of it on the device
indefinitely. **The privacy clause and the storage clause point in opposite directions**, and the
combination is worse for the user than either alone. I have raised the general form of this against
I38 separately; 5.4j is its sharpest instance.

**The gap that matters: a bundle is not a connection.** §9a of `PPCP-CORE` is emphatic that an exported
offline session *is* the recorded message stream, and the offline path is what v1 ships — UC-1 has no
host at all, and UC-4 is a golfer at a range exporting later. Candidate audio reaches a host
overwhelmingly **by bundle**, over USB or as a file, not over a TLS session at a range. 5.4j says
nothing about that, so:

- if bundles are **out of scope**, the protection is largely notional: the audio the clause is written
  about arrives at the host anyway, by the path it normally takes;
- if bundles are **in scope**, the offline path loses candidate audio entirely, and with it the whole
  diagnostic purpose REQ-PRIV-4 and REQ-OBS-4 exist for — explaining why detection fired, including
  when it fired wrongly.

Neither is what the clause intends, and the clause does not say which applies.

**The resolution is that 5.4j is about the wrong axis.** What §5.4.3 gave up is *confidentiality in
transit against a passive recorder on an untrusted network*. A USB tunnel, a file copied locally, or a
bundle written to the device's own storage has no passive recorder on the wire — that material is
protected by storage-at-rest (7.2c) and by physical control, which is a different mechanism and one the
relaxation never touched.

#### Requested change

> **(5.4j) SHOULD** A peer does not transfer candidate-attached audio payload over a connection that
> did not achieve forward secrecy **and is carried over a network the peer does not control** — shared
> or public infrastructure, rather than a wired tunnel or a publisher-provided hotspot. The Capture is
> announced as normal, so its metadata and its absence stay assertable (I10); only the payload waits.
>
> **(5.4j1)** A **bundle is not a connection** and is out of scope here. Candidate audio in a bundle is
> protected at rest ([7.2c](#72-handling-the-pairing-secret)) and by physical control of the medium, not
> by the key exchange, and the relaxation of [§5.4.3](#543-the-decision) did not change that.
>
> **(5.4j2)** A peer that withholds payload under 5.4j **may still evict it under its own retention
> policy**; `PPCP-CORE` I38 binds payload a receiver has not confirmed, and withheld payload is payload
> the owner chose not to send. Withholding must not become unbounded retention of the material this
> clause exists to protect.

5.4j2 is the important one. Without it the clause makes the exposure worse in the one dimension —
volume held on the device — that the user has no control over.

---

## 2. Not mine to close

Neither is a finding. Both are recorded so the sign-off is not read as covering them.

**The device measurement (5.4b).** Still outstanding, correctly gated, and an afternoon. My position is
unchanged: it gates the premise of Route D, not merely a detail of it. Draft 5 words it so a favourable
result changes nothing, which is the right way to hold it open — but it is held open, and my approval
below is of the document rather than of the mechanism the measurement will confirm or overturn.

**Whether 5.4j stands or is deleted.** The owner's word on whether the sensitivity judgement covers
candidate audio. If it does, 5.4j is deleted and [N1](#n1) goes with it. If it does not, N1 needs
answering. Either outcome is fine; leaving §5.4.3 naming an exception that nothing acts on is the one
outcome that is not, and Draft 5 has correctly stopped that happening by default.

**B13 — whether the absence of forward secrecy should be user-visible.** Recorded as a product question
rather than a protocol one, which is right, and 5.4k makes it answerable either way. From the host
seat: PinPointStudio should record the achieved version and mode in its diagnostic export as 5.4k
requires, and should not surface it in the ordinary session UI. A permanent warning that a user cannot
act on trains them to ignore warnings.

---

## 3. What I checked and confirmed

- **RT-4's rewrite is correct and tests the right thing.** Refusing anything weaker than both peers can
  reach, asserting `psk_ke` is refused where both reach 1.3, and asserting the strongest reachable
  result plus its surfacing where one cannot. That is 5.2b1, which is what the requirement became.
- **5.2i widened to any mechanism §5.2 constrains that a platform does not expose**, with the cases
  tabulated. The mobile team's line — *"compliance by construction and compliance by accident look
  identical from outside"* — is the right generalisation of what 5.2i was doing for one clause.
- **RT-17 in the review-method set beside RT-12 and RT-16.** 5.2b1 states its own untestability and is
  now the clause carrying property 2 wherever it survives; putting it where RT-12 lives, with the
  re-read trigger, is the only honest treatment.
- **5.3f.** Measured rather than assumed, and the requirement stated as a prohibition on transcoding
  rather than as a hope.
- **§4 is stable.** Three passes, three independent recomputations, and the all-fields vector that
  caught what the minimal one could not. I have nothing further on it.
- **The disposition's own count** — that RT-4 was the fourth conformance test left asserting the
  behaviour a fix removed — and the companion habit it draws from it. That habit is the right one, and
  I have raised a fourth instance of its sibling against `PPCP-CORE` revision 7 in the same round.

---

## 4. Sign-off

**Approved for implementation**, subject to N1 and with the two items in §2 held open as noted.

| | Fix | Cost |
|---|---|---|
| **N1** | Scope 5.4j to an uncontrolled network; state that a bundle is out of scope; state that withheld payload remains evictable | three clauses, no new field |

N1 should land before candidate-audio transfer is implemented on either side, because the withhold
decision and the eviction decision are the same code path and they currently disagree.

`PPCP-RV` §4, §6 and §7 I approve without reservation. §5 I approve as a document; the mechanism it
describes rests on 5.4b, which is not yet measured on the platform it concerns, and I would want that
result before the first pairing ships rather than before the first line is written.

---

## 5. Closing

Four passes, and the yield has gone from *the arithmetic is wrong* through *the fixes contradict each
other* to *this clause is about the wrong axis*. Each round has been smaller and further from the
centre than the last, which is what a document converging looks like.

What is left is not a drafting problem. It is a measurement somebody has to take and a judgement
somebody has to make, and Draft 5 has done the only useful thing a specification can do with either:
named them, said which clause turns on which, and worded the text so that neither answer requires a
redraft.

I would not have written §5.4.3, and I said so twice. It is the best-documented decision in the set,
and the fact that I can point at the exact clause I disagree with, read the reasoning, and find my own
objection quoted back accurately is why disagreeing with it costs nothing.
