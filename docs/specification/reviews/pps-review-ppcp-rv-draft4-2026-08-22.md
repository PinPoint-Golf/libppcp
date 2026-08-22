# Design review — PPCP-RV Draft 4

**Reviewed as owner of PinPointStudio, the host implementation. Third pass.**

| | |
|---|---|
| Document reviewed | `ppcp-rv.md` — `PPCP-RV` 1.0 Draft 4, payload version `ppcp1`, plus the RV review disposition |
| Reviewer | PinPointStudio maintainer |
| Prior passes | V1–V4 against Draft 1; W1–W3 and four consistency items against Draft 2 |
| Method | Re-verified the all-fields vector; read §5 in full against the measurement in §5.4.1 and the decision in §5.4.3 |
| Date | 22 August 2026 |
| Verdict | **W1–W3 are properly closed and §4 is stable.** The forward-secrecy relaxation is the product owner's call and I do not contest it. But **the conformance suite now forbids what §5.2a permits**, the decision rests on a measurement not yet taken on the platform it is about, and the two clauses left carrying property 2 have no test. Six findings, all in §5. |

---

## 0. Position

**On the decision itself.** §5.4.3 is the most honest piece of writing in the document set. It
records the measurement, names four routes, states what is given up in plain terms, quotes both
reviewers taking the opposite position — including me, verbatim — and says explicitly that the
relaxation rests on a judgement about data sensitivity rather than on a mechanism being
inconvenient. That is the distinction I was guarding against, and the document makes it in the right
place. **The call is the product owner's and I accept it.**

I also note what was *not* relaxed: 5.2f survives untouched, property 1 and property 3 are still
required, and 5.2b1 requires a peer to offer the strongest mode it has rather than settling at the
floor. The scope of the change is genuinely one property on one leg.

**And §5.4.3 identifies the edge correctly.** It says the sensitive part of the payload is not swing
video but the candidate-attached audio windows, cites `PPCP-CORE` §13c on their count being
unbounded by anything the user does, and names the lesson case where that is a coach and a pupil
talking. That reasoning is right — and then nothing acts on it. That is [R4](#r4), and it is the one
finding here where I am asking for something the decision's own argument implies.

**W1, W2 and W3 are closed cleanly.** 7.4e now says the original `sid` is not reused for anything and
survives only as the HKDF salt baked into `PRK`; 4.3b is scoped to the top-level map with 4.3b1
naming the nested keys explicitly; the duplicate label is resolved by renumbering the entropy rule to
4.3g. RT-6 carries its precondition. The all-fields vector still reproduces byte-for-byte — 133
octets, 183 URI characters, first four `a8 61 76 01`.

The six findings below are all consequences of the relaxation that were not carried through with it.

---

## 1. Findings

### R1 — RT-4 forbids exactly what 5.2a now permits {#r1}

**Severity: highest. The implementation the relaxation exists for fails its own conformance test.**

> **RT-4** | injected | A handshake negotiating `psk_ke`, **TLS 1.2**, or no encryption is refused
> (5.2a, 5.2b, 5.2f).

§5.2a now reads: *"TLS 1.3 is used wherever both peers can reach it; **TLS 1.2 with a PSK
ciphersuite is permitted** only where a peer's platform cannot."* And §5.2b permits a plain
`TLS_PSK_*` suite — which is `psk_ke` in all but name — *"only where it cannot"* name an ECDHE
variant. §5.4.1 measured that the mobile platform negotiates exactly `0x00A8`,
`TLS_PSK_WITH_AES_128_GCM_SHA256`.

So a mobile peer conforming to Draft 4 negotiates TLS 1.2 with a plain PSK suite, and RT-4 requires
that handshake to be **refused**. The test was written for the clause it tests, the clause changed,
and the test did not.

This is the fourth time across these documents that a conformance test has been left asserting the
behaviour a fix removed — I23 and `CT-S4` in Draft 1, I32 and `CT-I32` in Draft 2, and `CT-I36`'s
gaps in `PPCP-CORE` revision 5. `PPCP-CORE` §11.1 now carries the rule for writing an invariant;
the companion habit is that **a clause and its test are edited in the same pass**, and RT-4 is the
case for saying so.

#### Requested change

> **RT-4** | injected | **A handshake negotiating a weaker mode than both peers can reach is
> refused**, and no handshake is unencrypted (5.2b1, 5.2f). Where both peers reach TLS 1.3, assert
> `psk_ke` is refused (5.2b). Where one cannot, assert the negotiated result is the strongest the
> pair can reach and that the outcome is recorded ([R5](#r5)). Demonstrated against an instrumented
> counterpart or a wire capture of `psk_key_exchange_modes`, not by an API assertion (5.2i).

That tests 5.2b1, which is what the requirement has become, rather than a version number that is no
longer the rule.

---

### R2 — the decision rests on a measurement not taken on the platform it is about {#r2}

**Severity: high. Recoverable in an afternoon, and unrecoverable afterwards.**

§5.4a is admirably direct:

> The check ran on the **desktop variant** of the same frameworks, which carry identical
> availability annotations and the same ciphersuite enumeration on both platforms. **Confirmation on
> the mobile device itself is outstanding** and is an afternoon's work. *A decision of this size
> should not turn on a platform difference nobody expected.*

I agree with that sentence entirely, and then Draft 4 takes the decision anyway, and B8 demotes the
confirmation: *"still worth having, but **no longer gates anything**."*

It gates the premise. Route D was chosen because forward secrecy *"is not obtainable in any TLS
version through this interface"* — a finding from a proxy. Shared availability annotations and a
shared enumeration are good evidence and are not the measurement. If the device behaves differently,
Route D was unnecessary and a security property was given up for a platform limitation that does not
exist on the platform in question.

The asymmetry that made B8 dangerous in the first place still applies: the host side uses a library
that has supported external PSK for years, so nothing on our end will ever reveal this.

#### Requested change

Restore the gate, narrowly. Not a re-litigation of the decision — a confirmation of its premise:

> **(5.4b) MUST** The measurement of [§5.4.1](#) is repeated **on the mobile device** before an
> implementation ships a pairing that relies on [§5.4.3](#). If TLS 1.3 with an external PSK proves
> reachable there, property 2 is obtained on that leg and [5.2b1](#) already requires it to be used
> — no clause changes, and the relaxation simply never applies.

That costs an afternoon and is worded so a favourable result needs no redrafting at all.

---

### R3 — the two clauses now carrying property 2 have no test, and cannot have an external one {#r3}

**Severity: high.**

With forward secrecy best-effort, what preserves it wherever it *is* reachable is:

- **5.2b1 MUST** — *"A peer MUST NOT propose a weaker mode than its platform supports, because the
  weakest end sets the outcome and there is no way for the other to tell a limitation from a
  choice."*
- **5.4f MUST** — the same obligation restated as what now carries more weight.

Neither has an RT. And the clause states its own untestability: an observer cannot distinguish a peer
that offered less than it had from one that had less to offer. So the requirement that Draft 4 makes
load-bearing is the one nothing checks.

The document already has the right instrument. RT-12 is `review` method with the note that it is
*"the requirement on which the whole model rests and the one no test can catch"*, and RT-16 uses the
same method for persistence. This belongs in that set.

> **RT-17** | **review** | The peer offers every key-exchange mode and ciphersuite its platform
> exposes, and the offered set is derived from a platform capability query rather than from a
> constant (5.2b1, 5.4f). **Re-read whenever the TLS setup path is touched**, and whenever a
> platform SDK is updated, since a mode that becomes available is a mode the peer must begin
> offering.

The second sentence is the operative one. A platform gaining TLS 1.3 external PSK in a future
release silently restores property 2 — but only for an implementation that asks rather than assumes.

---

### R4 — the decision identifies candidate audio as the sensitive payload, and then does nothing about it {#r4}

**Severity: high. This is the one I am actually asking for, and it does not reopen the decision.**

§5.4.3, in its own words:

> Swing video of a golfer is not sensitive material, and that is a reasonable basis for the trade.
> The part of the payload that carries a privacy dimension is not the video but the
> **candidate-attached audio windows** … they capture events that were *not* shots — an adjacent
> player, a conversation … In the lesson use case that is a coach and a pupil talking. **The
> decision is the owner's to make; this is the part of the payload it should be made about.**

The relaxation is then applied uniformly to everything the channel carries. The trade was argued on
video and paid for by audio.

The exposure is concrete: an attacker records a session at a range on shared infrastructure and
later obtains the pairing secret — by photographing the code, which single use prevents from being
*used* but not from *decrypting*, or from a peer's storage. What decrypts is not only swing video but
every retained candidate window, including the ones attached to nominations that were rejected
because they were somebody talking.

Because retention attaches to Candidates and the count is unbounded by anything the user does
(`PPCP-CORE` §13c), the volume of that material is not something the user chose or can predict.
That is a materially different posture from video of their own swing, and §5.4.3 says so.

#### Requested change

A targeted rule that preserves the decision for the payload it was argued about:

> **(5.4j) SHOULD** A peer does not transfer candidate-attached **audio** payload over a connection
> that did not achieve forward secrecy, unless the user has been told and has agreed. The audio
> Capture is announced as normal — its metadata and its absence remain assertable (I10) — and its
> payload is withheld or deferred to a connection that did. The rest of the session is unaffected.
>
> This follows [§5.4.3](#)'s own reasoning: the relaxation was judged against swing video, and the
> audio windows are the part of the payload that judgement did not cover.

The machinery is already there. `completeness` and `transfer` are independent axes, `absent` is
assertable with a reason, and the host's diagnostic value in candidate audio is entirely
after-the-fact — nothing in the live path needs it. Withholding it costs a session nothing and is
the cheapest available answer to the exposure the decision itself named.

If the owner's judgement is that the audio is fine too, then that should be **stated** rather than
inherited, because §5.4.3 currently reads as though it identified an exception and then did not make
one.

---

### R5 — nothing surfaces what the connection actually achieved {#r5}

**Severity: medium, and it is what [R4](#r4) and 5.4i both need.**

Forward secrecy is now a per-connection outcome rather than a property of the protocol. Nothing
requires a peer to know which it got.

Three things depend on knowing:

- **5.4i** — *"where a deployment does regard its payload as sensitive … the answer is Route A or
  B"*. A deployment cannot apply a policy to an outcome it cannot read.
- **[R4](#r4)**, or any rule conditioned on the achieved mode.
- **An honest user-facing statement.** A peer that says "this connection is encrypted" without
  distinguishing the two cases is telling the truth and not the whole of it.

> **(5.4k) MUST** A peer makes the achieved TLS version and key-exchange mode available to its
> application layer, and records both in its diagnostic export ([7.2b](#) already forbids the export
> from carrying keys or payloads; the negotiated mode is neither).

The diagnostic bundle is the right home — user-initiated, no telemetry, and it is where every other
"what actually happened" figure in this project already lives.

---

### R6 — property 3's server-side obligation exists only on the newly-permitted path and has no test {#r6}

**Severity: medium.**

5.2h property 3 now binds a server-sent field, as I asked:

> it binds a **server-sent** field as much as a client-sent one: a `psk_identity_hint`, which exists
> in the TLS 1.2 PSK model and is sent in the clear, MUST be empty.

RT-14 covers the client identity — rotation, resolvability, no `sid`. Nothing covers the hint. And
the hint exists **only on the TLS 1.2 path**, which is precisely the path Draft 4 has just made
normal for one of the two implementations. A requirement that applies solely to the newly-permitted
mode, with no test, on a document whose §9 tests otherwise track every clause.

> Extend **RT-14**: … *and, where TLS 1.2 is negotiated, the server sends an **empty**
> `psk_identity_hint` (5.2h property 3).*

One clause on an existing test.

---

## 2. What I checked and confirmed

- **W1–W3 all closed correctly.** 7.4e's replacement is stronger than a repair — stating that `sid`
  survives only as the HKDF salt is the sentence that makes B6 genuinely closed. 4.3b1 naming the
  nested `h`/`p`/`k`/`s` keys explicitly is better than the single word I proposed, because it
  answers the question the vector raises.
- **The all-fields vector still reproduces**: 133 octets, 183 URI characters, `a8 61 76 01`.
- **5.2b1 is the right rule** even though it cannot be tested externally. *"The weakest end sets the
  outcome and there is no way for the other to tell a limitation from a choice"* is the correct
  framing, and it is why [R3](#r3) asks for a review-method test rather than doubting the clause.
- **The downgrade analysis is sound.** The negotiated version and suite are covered by the
  handshake transcript and the transcript is authenticated by the PSK, so an attacker cannot force a
  weaker mode without the secret, and one holding the secret has already won. Correct, and worth
  having written down.
- **B12 — the per-session ratchet** — is the right thing to have recorded. Re-deriving `PRK` at
  session close and erasing its predecessor restores property 2 for every session after the first
  without touching the transport, and the honest cost is persistent state both ends must keep in
  step. If R2's device check confirms the limitation, B12 is where I would look next.
- **§5.4.2's four routes were weighed against the three properties rather than against
  convenience**, and Route C — an application-layer ephemeral key over plain PSK — was correctly
  rejected as a hybrid that leaves control traffic retrospectively decryptable.

---

## 3. Sign-off

**Approve §4, §6 and §7 without reservation. §5 subject to R1–R6**, none of which reopens the
product decision.

| | Fix | Cost |
|---|---|---|
| **R1** | Rewrite RT-4 against 5.2b1 rather than against a version number | one test row |
| **R2** | Repeat the measurement on the device before shipping a pairing that relies on the relaxation | an afternoon, plus one clause |
| **R3** | RT-17, review method, for 5.2b1 and 5.4f | one test row |
| **R4** | Withhold candidate audio payload over a connection without forward secrecy — or state that the judgement covers it | one clause |
| **R5** | Surface the achieved version and mode to the application and the diagnostic export | one clause |
| **R6** | Extend RT-14 to the empty `psk_identity_hint` | one clause |

R1 first: the mobile implementation currently fails its own suite, and that is the sort of thing that
gets resolved by quietly not running the test.

R4 is the one I would press hardest on. It does not contest the decision; it applies the decision's
own stated reasoning to the payload that reasoning identified and then skipped. If the answer is that
the audio is acceptable too, that is a legitimate answer and I would like it written down, because
the next person to read §5.4.3 will otherwise find an exception that was named and not taken.

---

## 4. Closing

The decision was taken the right way. It was measured before it was argued, argued against stated
properties rather than convenience, and recorded with both dissenting reviews quoted rather than
summarised. I would rather be overruled like that than agreed with quietly.

What is left is the part that always follows a relaxation: the clauses around it were written when
the property was required, and they still read that way. RT-4 refuses the newly-legal handshake,
5.2b1 became load-bearing without acquiring a test, the achieved outcome became a per-connection
variable that nothing reports, and a server-sent field became reachable on a path nothing tests. Each
is a line or two, and each is invisible from the host side, which is where I would normally stop
looking — the mobile leg is the only one that ever exercises any of them.
