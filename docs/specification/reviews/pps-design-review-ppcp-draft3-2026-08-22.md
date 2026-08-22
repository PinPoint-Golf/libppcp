# Design review — PPCP Draft 3

**Reviewed as owner of PinPointStudio, the host implementation. Third and final round.**

| | |
|---|---|
| Documents reviewed | `libppcp/docs/specification/` Draft 3 — `PPCP-CORE`, `PPCP-MSG`, `PPCP-ENC`, `PPCP-CONF`, and the Round 3 disposition. **`PPCP-RV` not reviewed**, by agreement |
| Reviewer | PinPointStudio maintainer |
| Prior rounds | F1–F8 against Draft 1; R1–R4 and five consistency items against Draft 2 |
| Date | 22 August 2026 |
| Verdict | **Approve to implement.** Three findings, all in the new race-resolution machinery of §8.2i–l, all closable with wording supplied below. None changes an entity, adds a message or renumbers an invariant. After these I have no further findings and PinPointStudio builds against Draft 4 as it stands. |

---

## 0. Position

Every Round 2 finding is closed and closed properly. R1, R2, R3 and R4 are fixed, the five
consistency items are all done, `CT-S7` exists, and §11.1 — the rule that an invariant constrains
the shape of an output and never the choice an implementation makes — is better stated than I
proposed it, with both prior defects tabulated as the evidence.

Two things in Draft 3 are worth naming as good rather than merely accepted. The first is D14: the
device's Shot wins the residual race because it may already anchor an extracted Capture, and the
reasoning that the direction is *forced* rather than chosen is right — making the host win would
need a withdraw message, and a model that can withdraw a Shot can be talked into rewriting one. The
second is 5.10e, making the two arbitration parameters present if and only if the Session has a
host: that is I23 expressed structurally, in the register the whole document is written in, and it
came from the mobile side rather than from either of my rounds.

The three findings below are all in §8.2i–l. That is not a criticism of the fix — it is where the
new normative text is, and it is the most intricate machinery in the specification. All three are
of the same family: **the clauses describe what the two peers do, and do not quite describe what
the model has to permit for them to do it.**

---

## 1. Findings

### S1 — a peer told to mint may have no expressible `t0`, and the pairing that proves it is already in the suite {#s1}

**Severity: highest of the three. Reachable from a required interoperability pairing.**

§8.2i now permits a nominating peer to mint after the deadline, filtered by its own promotion
policy. §5.13c is unchanged: **`t0` is expressed in `Session.timebase_ref`** — the host's timebase
in any hosted session.

Now consider *why* a host stays silent. §8.2d names the case:

> A Candidate whose relation to `timebase_ref` is missing, `unrelated`, or too uncertain under host
> policy is **excluded from arbitration and retained**.

An excluded Candidate contributes to no Shot, so no `shot` ever references it, so the deadline
expires and 8.2i fires. But the reason it was excluded is precisely that the peer's relation to
`timebase_ref` is missing, `unrelated` or too wide — **so the peer cannot convert its own instant
into `Session.timebase_ref` either.** §5.4b and `MSG` §10c forbid it substituting a zero. It is
required to mint and unable to produce a conformant Shot.

This is not hypothetical. `CONF` §5 already requires the pairing:

> Reference host ↔ peer declaring `unrelated` timebases — *I3, and that an honest degraded peer is
> not silently mishandled.*

In that pairing **every** candidate from the device is excluded by 8.2d, and a device whose
promotion policy would have promoted a real swing — which it would — reaches 8.2i for each one. The
pairing written to prove that an honest degraded peer is handled honestly lands on an undefined
state.

**There is a second, related looseness in the same area.** §8.3f says a peer whose host link drops
*"enters this regime for the duration"* — but §8.3a scopes the regime to *"a Session with no
`host`"*, and a link-dropped session still has a declared host. So it is unstated whether I23 binds
during an outage, and §7.1's zero-host row (`timebase_ref` = a capturing peer's timebase) does not
apply, because `timebase_ref` is immutable under I16 and stays the host's. The behaviour everyone
intends is clear; the preconditions as written do not deliver it, and a conformance test reading
I23 literally would not apply it here.

#### Requested change — two clauses

**(a) Add to 8.2i**, after the promotion condition:

> **A peer that cannot express `t0` in `Session.timebase_ref` — because it holds no `affine`
> relation to that timebase, or its relation is `unrelated`, or it exceeds the peer's own policy —
> MUST NOT mint. The Candidate is retained with no Shot referencing it, which is already the legal
> and honest result under [§8.3b](#83-the-zero-host-regime) and [I8](#11-invariants). A peer MUST
> NOT substitute a zero offset to make a Shot expressible** ([§5.4b](#54-timebaserelation)).

That is the honest degraded behaviour the `unrelated` pairing exists to demonstrate, and it costs
nothing: retaining a Candidate with no Shot is a state the model already has.

**(b) Add to 8.3f**, so the regime's precondition matches its invocation:

> **A Session with an unreachable host is not a Session with no host. `Session.peers`,
> `Session.timebase_ref` and `Session.coincidence_window_ns` are unchanged (I16, 5.10e); what
> changes is that no arbitration occurs and the peer mints under 8.3a–c. For the purposes of I23, a
> host unreachable for three consecutive heartbeat intervals ([§7.4c](#74-liveness)) is treated as
> absent.**

`CT-I32` should gain the negative: *"a peer declaring `unrelated` timebases against a silent host
mints nothing, and retains every Candidate."*

---

### S2 — Mint and Arbitrate now carry MUSTs that require originating a message only Offline confers {#s2}

**Severity: high. A direct C2 / I24 contradiction, and the third occurrence of the family that
created the Mint profile in the first place.**

Three normative clauses added or amended in Draft 3 discharge their obligation through `ShotLink`:

| Clause | Profile it binds | What it requires |
|---|---|---|
| §8.2i | **Mint** | the minted Shot *"is reconciled to the host's Shots through `ShotLink`"* |
| §8.2l, I35, `MSG` 7.2f | **Arbitrate** | *"the host links them with `shot_link`, `basis: shared_candidate`"* |
| §8.3f | **Mint** | *"reconciles the minted Shots on reconnect through `ShotLink`"* |

But `shot_link` is conferred by **Offline** — `MSG` §9 table and §11 index both say so — and the
profile table's dependencies are:

- **Mint** requires Core, Detect. Not Offline.
- **Arbitrate** requires Core. Not Offline.

And C2 is unambiguous: *"MUST NOT a peer originate a message whose profile it has not declared."*

So `Core + Arbitrate + Live` — a legal, constructible profile set — **cannot satisfy I35 without
violating C2**, and `Core + Capture + Detect + Mint + Live` cannot satisfy 8.2i or 8.3f. Every
worked example in §2.2.3 happens to declare Offline, which is exactly why this will not surface in
testing: the reference implementations pass by accident, in the manner `CONF` §2c warns about.

**This is the same defect that produced the Mint profile.** Draft 1's disposition records it as:
*"the v1 PinPointCapture device therefore performed an operation none of its declared profiles
granted."* Third time, new place.

#### Requested change — move the type, not the dependency

The blunt fix is to add Offline to the Requires column of Mint and Arbitrate. I would not do that:
Offline confers bundle read and write and carries I15, I16, I25 and I34, so a live-only third-party
host would have to implement a file container to resolve a race that happens on a socket. That is
disproportionate and it re-creates the coupling §2.2 exists to avoid.

**`ShotLink` is no longer an offline concept, and the profile table should say so.** Of its six
bases, `arrival_pairing` and `shared_candidate` are asserted live at capture time and `manual` may
be; only `interval_alignment`, `acoustic_correlation` and `sequence_alignment` are retrospective.
Suggested:

| Profile | Confers the ability to originate | Requires |
|---|---|---|
| **Core** | Peer, Timebase, TimebaseRelation, ClockDiscontinuity declaration; version and extension negotiation; **`ShotLink` and `shot_link`** | — |
| **Offline** | Bundle read and write, ~~ShotLink,~~ SessionLink, reconciliation | Core |

with **I9 moved to Core** (it constrains anyone who originates a link, and its text — *reconciliation
creates links; no entity is rewritten or merged* — is a Core-shaped prohibition, not an offline
one). `MSG` §9.3 and the §11 index change their Profile column from Offline to Core; nothing else
moves.

If the team prefers the minimal edit over the correct one, adding Offline to Mint's and Arbitrate's
Requires does close the hole — but it should then be recorded in the disposition as a call, because
it makes a live-only host implement a bundle parser.

---

### S3 — §8.2k has one peer amend another peer's Shot, and no rule says who may {#s3}

**Severity: medium. Not a contradiction — an unstated rule that our two implementations will
otherwise guess at differently, which is the definition of the thing this document exists to
prevent.**

§8.2k:

> It attaches its own Candidates to the device's Shot by **re-sending `shot` with an extended
> `candidates` list** and the unchanged `t0`, exactly as 8.2e requires of a late Candidate (I35).

Three things follow that the model does not currently state:

1. **The direction column is now wrong.** `MSG` §7 declares `shot` as `issuer → any`. Under 8.2k
   the host sends `shot` for a Shot whose `issued_by` is the device. It is not the issuer.
2. **Nothing says who may amend a Shot, or which fields are the issuer's.** Before Draft 3 the
   question could not arise: exactly one peer ever sent `shot` for a given `shot.id`. Now two do.
   I7 protects `t0` and 7.2c repeats it, but `authority`, `issued_by` and `id` have no stated
   owner, and `candidates` has no stated amender.
3. **The receiving device has no stated obligation.** It gets a `shot` carrying its own `shot.id`
   and its own `issued_by`, with a `candidates` list it did not author. Must it accept the
   extension? Must it merge? May it ignore it? PinPointStudio and PinPointCapture will each pick an
   answer, and the two answers will differ.

This is cheap because the intended behaviour is obvious — it just is not written down.

#### Requested change — one clause and one table cell

Add to §5.13 (and reference it from 8.2k):

> **(5.13d) MUST** A Shot's `id`, `t0`, `authority` and `issued_by` are set by the issuer and are
> **never changed by another peer**. Its `candidates` list MAY be extended by any peer holding a
> Candidate that belongs to that Shot, by re-sending `shot` with the extended list and every other
> field unchanged ([§8.2e](#82-arbitration), [§8.2k](#82-arbitration)). A peer receiving an
> extension to a Shot it issued **MUST** adopt the extended list; extension is additive and
> order-independent, so the two peers converge on the same set regardless of arrival order.

and change `MSG` §7's direction cell for `shot` from `issuer → any` to
**`issuer or attaching peer → any`**, with a footnote pointing at 8.2k.

The convergence sentence is the part worth having: it makes the resolution commutative, so neither
end has to reason about who saw what first.

---

## 2. Consistency items

Two, both one line.

| | Item | Where |
|---|---|---|
| 1 | **`confirmed_by: observer` is defined in arrival-pairing language and does not describe `shared_candidate`.** 5.16e reads *"`observer` is a live assertion by the peer that **saw the arrival**"*. A `shared_candidate` link is asserted by a host that observed a *collision*, not an arrival, and `confirmed` is mandatory — so the new basis has no correct value for a new MUST. Broaden to *"a live assertion by the peer that observed the association"*, and add to 5.16g that `shared_candidate` is `confirmed_by: observer`. | `CORE` §5.16e, 5.16g |
| 2 | **`ENC` 4.1d's `intrinsics` exception has no rule for an empty array.** *"Distinguished by the type of the first element"* is undecidable when there is no first element. It only arises for a zero-frame Capture, which 5.8d now correctly excludes from carrying `AchievedFrames` at all — so the honest fix is half a sentence saying an empty array MUST NOT be emitted, rather than another rule. | `ENC` §4.1d |

---

## 3. One thing to leave exactly as it is

`canonical_correction_ns` is a bare `int64` while `tof_correction` beside it is an `Estimate` with
a mandatory sigma. That asymmetry looks like an oversight and is not, and I want the reason on
record so it is not "fixed" during implementation.

Time of flight is a **converging estimate** whose dispersion changes shot to shot — wide early in a
session, tight late — and the sigma is the only way a consumer knows where in that convergence a
given shot sits. The canonical correction is **arithmetic over declared values**: the profile's
convention, its `frame_start_to_exposure_offset_ns`, and that frame's measured exposure. Its
trustworthiness is not a per-shot quantity; it is `frame_start_to_exposure_offset_provenance`, which
already lives on the profile under I31 and is reachable from any Candidate in one hop through
`source_id`.

So the honest uncertainty is already carried, in the right place, once per profile rather than once
per candidate. Adding an `Estimate` here would duplicate it and invite a peer to compute a
per-candidate sigma it does not have. **Leave it.**

---

## 4. Closed from my side

| Prior finding | Status |
|---|---|
| **R1** — the issue-hold fix reintroduced the F1 defect | **Closed.** Both halves fixed, and the mobile team's race finding folded in. 8.2j–l are more than I asked for and the D14 reasoning is sound. Residual: [S1](#s1), [S2](#s2), [S3](#s3) are all in this machinery. |
| **R2** — I30 vs `MSG` 8.2b | **Closed.** Invariant narrowed, `CT-I30` asserts the exception applies only where `transfer` is `failed`. |
| **R3** — `Candidate.at` convention | **Closed**, and `canonical_correction_ns` goes usefully beyond it. `CT-I33`'s discrepancy assertion is the right shape. |
| **R4** — scalar form for `intrinsics` | **Closed**, wording adopted, and `CT-I30` exercises it specifically. |
| **Five consistency items** | **All closed.** `CT-S7` exists and its assertion 4 — differing by exactly the offset against a peer that measured — is the one that catches a hardcoded zero. |
| **§3 — `confirmed` carried two states** | **Closed** by `confirmed_by`, and B9 closes with it. One wording residual, item 1 above. |
| **§6 — the same failure mode twice** | **Closed and promoted.** §11.1 is stated better than I proposed it. |
| **D8, D10, D12, D13, D14, D15, D16** | All read, none I would reverse. D15 in particular: the argument that only an arbitrating host consumes `coincidence_window_ns`, so a per-basis override is additive rather than breaking, is correct and I withdraw any concern about deferring it. |
| **Q4** | Correctly open. B8's floor-per-class design is right, and the acoustic-to-acoustic case between a device mic and a host mic after time-of-flight correction is the measurement I will contribute from our rig. |

---

## 5. Sign-off

**Approved to implement.**

| | Fix | Cost | Blocks |
|---|---|---|---|
| **S1** | A peer that cannot express `t0` in `timebase_ref` MUST NOT mint; state that an unreachable host is not an absent one | two clauses | **Mint, and the `unrelated` interop pairing** |
| **S2** | Move `ShotLink` / `shot_link` origination from Offline to Core, and I9 with it | four table cells | **conformance for any live-only Mint or Arbitrate peer** |
| **S3** | State who owns which Shot fields and who may extend `candidates`; correct the `shot` direction cell | one clause, one cell | **`shot` handling on both ends** |

S1 and S2 should land before the Mint and Arbitrate code is written; both are in the same section
and the same edit pass. S3 blocks nothing structurally but should land before either team writes
`shot` receive handling, because it is the one place where two reasonable implementations diverge
silently and only meet at integration.

The two consistency items and §3 need no action beyond a sentence each.

---

## 6. Closing

Three rounds, and the pattern in what each one found is worth recording, because it is the argument
for having done it this way.

Round 1 found defects in the **model** — things the entity graph could not express. Round 2 found
defects in the **fixes** — a new invariant that constrained a choice, a contradiction between two
documents. Round 3 finds defects in the **seams around the fixes** — a peer obliged to emit a value
it cannot compute, an obligation discharged through a message its profile does not confer, an
amendment with no stated owner. Each round's findings are smaller and further from the centre than
the last, which is what convergence looks like.

The specification is in better shape than PinPointStudio's own internal contracts, and several
provisions in it — symmetric declaration, absence being assertable, provenance on any quantity a
peer cannot guarantee, and §11.1 — are things I intend to apply on the host side whether or not a
phone ever connects to it. That is the strongest thing I can say about a document I have now spent
three rounds trying to break.
