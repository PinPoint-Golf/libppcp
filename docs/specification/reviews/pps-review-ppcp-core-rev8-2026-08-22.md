# Design review — PPCP-CORE revision 8

**Reviewed as owner of PinPointStudio, the host implementation.**

| | |
|---|---|
| Documents reviewed | `PPCP-CORE` 1.0 revision 8, `PPCP-MSG`, `PPCP-ENC` and `PPCP-CONF` as amended |
| Scope | Revision 8 only — the disposition of C1, C2, C3, the `body` cap, `viewpoint.confidence`, the accepted process change, and the fifth instance the sweep found |
| Reviewer | PinPointStudio maintainer |
| Date | 22 August 2026 |
| **Verdict** | **Approved for implementation**, with one clause to fix before any eviction code is written and four items to close before `ppcp/1.0` freezes. None is a design question; the model is settled. |

---

## 0. Position

All three findings carried, and the two that mattered most are fixed at the right level rather than
patched. **C1** is resolved by scoping I38 to *payload* and naming four exits — the diagnosis in the
changelog is the right one: *"the error was scope: I38 exists for shot payload a consumer has not
received, and was written as though it were about every Capture."* **C3** gains `stream_id` with 5.18g
fixing the timebase of `at` to the named Stream, which does more than I asked: it makes the frame anchor
exact instead of a clock conversion away from exact.

Two things beyond my findings are worth naming.

**The mobile team's PPC-1 is the better half of C1.** I found that `confirmed` was unreachable for four
classes of Capture; they found it was unreachable for the entry-level case *entirely* — hostless, and
across the bundle path — which the requirements call the normal one. 5.14h having a receiver send
`capture_committed` on its next connection for a Capture it obtained from a bundle, on the strength of
I34's identity and nothing new, is a good answer. 5.14i saying plainly that I38 protects nothing where
there is no receiver is better still: it stops a peer reading protection it does not have.

**The sweep was accepted and run in the same round, and found a fifth instance immediately** — I8
forbidding the discard of candidate evidence that 5.12.1c contemplates being evicted. Separating the
evidence *record*, never discarded, from the evidence *payload*, which may be shed with its absence
asserted, is exactly the right cut. That is the process change earning its keep on the day it was
adopted, which is the strongest argument for keeping it.

Five findings follow. One is substantive; the rest are the clause-and-its-test problem this round
formally accepted a check against, recurring inside the round that accepted it.

---

## 1. Findings

### D1 — exit 4 reintroduces "regardless of retention policy", which is the phrase REQ-SESS-4 forbids {#d1}

**Severity: high, and it is the only design-level finding. The fix for C1 over-corrected.**

> **(5.14g)** … A Capture is evictable when any of these holds: …
> | 4 | The protocol **or the peer's own declared retention policy** permits the owner to shed it —
> 5.11j for preview, 5.12.1b for candidate evidence, or a payload the owner chose to withhold |

The three named cases are all right, and I asked for them. The clause that carries them is not: **"or
the peer's own declared retention policy permits the owner to shed it"** is a general licence, and it
swallows the invariant.

The requirement G2 exists to satisfy reads:

> **REQ-SESS-4 (MUST)** — *"Nothing unconfirmed is evicted, **regardless of retention policy**."*

Revision 7's I38 said *"whatever its retention policy"*, matching it. Revision 8's exit 4 now says
retention policy may permit exactly that. A device under storage pressure declares a policy that sheds
shot payload older than N shots, sheds it, and the host never receives it — conformant under exit 4 and
a direct breach of the requirement. The hole G2 was written to close is open again, one revision later,
through the clause that closed it.

The three named cases do not need the general licence. Preview is permitted by 5.11j, candidate evidence
by 5.12.1b, and withheld payload by `PPCP-RV` 5.4j2 — all three are **the protocol permitting it**,
which the first half of exit 4 already covers.

#### Requested change

Delete the general licence and keep the enumeration:

> | 4 | **The protocol** permits the owner to shed it — [5.11j](#5112-preview-streams) for preview,
> [5.12.1b](#5121-candidate-evidence) for candidate evidence, or payload withheld under a rule that
> permits withholding, such as [`PPCP-RV` 5.4j](ppcp-rv.md#543-the-decision) | It was never going to be
> sent, so no receiver will ever confirm it |
>
> **(5.14g1) MUST NOT** A peer's own retention policy extend this list. **Shot-anchored payload is never
> sheddable by policy** while it holds payload no receiver has confirmed: that is the whole of I38 and
> of REQ-SESS-4, and every exit above is a case where the protocol itself says no receiver will ever
> confirm it.

That keeps all four exits working and stops the fourth from being a door onto the rule.

---

### D2 — the accepted process change was not written {#d2}

**Severity: medium, and pointed, because of what it is a change about.**

Revision 8's changelog records:

> **PPS §6** | C1 is the fourth instance of a new MUST contradicting one in an adjacent section. |
> **Accepted as a process change** — **`PPCP-CONF` §5b2** makes the adjacent-MUST sweep a required check
> before `ppcp/1.0` freezes.

`PPCP-CONF` §5 contains **5a, 5b, 5b1 and 5c**. There is no 5b2. 5b1 is the *profile-boundary* audit,
which is a different check and was accepted in an earlier round.

So the sweep was run — it found the fifth instance, which is recorded — but the requirement to run it
before the wire version freezes exists only as a cross-reference to a clause that does not exist. The
one thing that makes a process change durable is being written into the document that gates the freeze,
and that is the step that was missed.

> **(5b2) MUST** Before `ppcp/1.0` is declared stable, an **adjacent-MUST sweep** is run: every
> normative clause added or amended since the previous revision is read against every normative clause
> in the sections it touches, for contradiction. The same defect has now been found **five** times —
> I23 and the ball-into-screen transient; I32 and promotion; I36 and an honestly truncated bundle; I38
> and a preview segment 5.11j requires a peer to discard; and I8 against the evicted candidate window
> 5.12.1c contemplates. Four were found by a reviewer and the fifth by this sweep, which is the argument
> for running it rather than relying on the next review round.

---

### D3 — `CT-I37` and `CT-I38` were not updated for the fixes they test {#d3}

**Severity: medium. Two more instances of the failure this round accepted a check against, inside the
round that accepted it.**

**`CT-I38`** still reads: *"Withhold `capture_committed` … Then send it and assert `transfer` becomes
`confirmed` and the Capture becomes evictable."* That exercises **exit 1 only**. The whole substance of
C1 is exits 2, 3 and 4, and none is tested — so the contradiction that produced the finding would still
pass. I asked for this explicitly last round.

**`CT-I37`** still reads: *"… and that a **lower** `revision` for a known `id` is ignored."* That is
revision 7's rule. **The equal-revision case was the entire C2 defect** — two peers both producing
revision 2 and diverging permanently — and the tiebreak added to fix it is untested. Nothing tests 5.18g
(the timebase of `at`) or 5.18h (never render a view-specific annotation on another Stream) either.

> **`CT-I38`** … Assert each of the four exits of 5.14g independently: a `confirmed` Capture is
> evictable; an **`absent`** Capture is evictable with no commit possible; a Capture the receiver
> answered **`already_present`** is evictable; a **discarded preview** segment is permitted and is
> announced absent. Assert that a peer's own retention policy does **not** make shot-anchored payload
> evictable ([D1](#d1)).
>
> **`CT-I37`** … Assert an **equal** revision from a different `author_peer_id` resolves to the same
> annotation **whichever order the two arrive in**, at both ends. Assert `at` is in the named Stream's
> timebase where `stream_id` is present and in `Session.timebase_ref` where it is absent, and that a
> view-specific annotation is not rendered on another Stream.

---

### D4 — the `body` cap contradicts itself in the same section {#d4}

**Severity: medium. An implementer builds a validator from the field table.**

The changelog says the cap was lowered to 8 KiB. 5.18f says 8 KiB. `PPCP-MSG` 9.0e says 8 KiB. **The
field table in §5.18 still says `at most 64 KiB`.** Three places say one thing and the normative type
definition says another, in the same section as the clause that corrects it.

Related: `PPCP-ENC` §8's limits table — the decoder's pre-allocation enforcement point — has a
`Thumbnail bytes 64 KiB` row and **no annotation row**. Since annotations are on the control channel and
the cap now exists to protect it, the limit belongs where the decoder enforces limits.

Fix the table row to 8 KiB and add `Annotation body | 8 KiB | malformed` to `ENC` §8.

---

### D5 — `stream_id`'s presence rule cannot be checked, which is §11.1's own pattern {#d5}

**Severity: medium, and it is in the change I asked for, so I will own it.**

> | `stream_id` | `Id` | 0..1 | The Stream whose frame it is drawn on. **Present for any annotation
> whose `body` is interpreted in image coordinates.** |

`body` is opaque to the protocol and `format` is an open registry, so **no peer and no test can
determine whether a given annotation is interpreted in image coordinates.** A peer that emits a `line`
with no `stream_id` cannot be detected as non-conformant, and 5.18h — never render a view-specific
annotation on another Stream — cannot be enforced against it, because the receiver cannot tell that it
is view-specific either.

This is precisely what §11.1 now names: a rule that constrains a **judgement** an implementation makes
rather than the **shape** of its output. The wording I proposed carried the defect and the document
adopted it verbatim.

#### Requested change

Make it a property of `kind`, which is on the wire and checkable:

> **(5.18j) MUST** The `kind` registry marks each value **view-specific** or not. `line` and `plane`
> are view-specific; `text` and `nav_anchor` are not. **An annotation of a view-specific `kind` MUST
> carry `stream_id`, and one of a non-view-specific kind MUST NOT.** A consumer that does not recognise
> a `kind` treats it as view-specific if `stream_id` is present, which is the safe default: it renders
> it only on the Stream named, or not at all.

That makes presence statically checkable from `kind` alone, gives 5.18h something to bind to, and gives
an unknown vendor-namespaced kind a defined and conservative behaviour.

---

## 2. Smaller point

**5.14h's commit may arrive for a closed session.** A receiver that commits a Capture from a bundle
sends `capture_committed` *"on its next connection with the owning peer"* — which may be days later,
against a Session that has `state: closed`. `error / unknown_session` exists and a conformant peer might
answer with it, in which case the payload is never releasable and 5.14h achieves nothing. One sentence:
a `capture_committed` naming a closed Session is accepted, because releasing storage is the one
operation that is legitimate after a Session closes.

---

## 3. What I checked and think is right

- **The four exits, and the reasoning for each.** Exit 2's justification — *"there is no payload to
  evict, and no digest for `capture_committed` to name"* — is the one that shows the diagnosis was
  understood rather than the symptom patched.
- **5.14i.** *"A peer MUST NOT read I38 as protection it does not have."* That sentence is worth more
  than the clause it sits under, and it is the kind of thing that gets trimmed later as editorial.
- **The I8 split.** Record versus payload is the right cut, and it generalises: it is the same
  distinction `completeness` and `transfer` already make on Capture, applied to evidence.
- **5.18e's tiebreak.** A total order, stated as a total order, with the explicit note that it is a
  tiebreak and not a merge. I traced both delivery orders and both ends converge.
- **`viewpoint.confidence` present if and only if `classified`.** Correct — a user who states "DTL" is
  not expressing a probability.
- **8 KiB and 5.18i's coalescing SHOULD.** The right pair: the cap bounds the worst case and coalescing
  bounds the rate, which was the actual concern.

---

## 4. Host-side position

Unchanged from last round and reinforced by 5.14h. **PinPointStudio becomes the party that releases the
device's storage**, and now on the offline path too: a bundle we import obliges us to send
`capture_committed` on our next connection with that device. Two things I will hold us to — the commit
goes against a real flush rather than a successful read, and it goes for **every** Capture we accept,
because one we quietly drop is one the phone keeps for ever.

D1 matters to us specifically for the opposite reason. As the host we are the party that suffers if a
device sheds shot payload under its own policy before we receive it, and exit 4 as written permits
exactly that. I would rather the rule were strict and the device occasionally refused to arm on low
storage — which REQ-OFF-2 already requires — than that it quietly dropped swings.

---

## 5. Sign-off

**Approved for implementation.**

| | Fix | Cost | Before |
|---|---|---|---|
| **D1** | Delete exit 4's general retention-policy licence; add 5.14g1 | one phrase, one clause | **any eviction code** |
| **D2** | Write `PPCP-CONF` §5b2 | one clause | `ppcp/1.0` freeze |
| **D3** | Update `CT-I37` and `CT-I38` for what they now test | two test rows | the suite being written |
| **D4** | `body` table row to 8 KiB; add the limit to `ENC` §8 | one cell, one row | the decoder |
| **D5** | Make `stream_id` presence a property of `kind` | one clause | markup on either side |

**D1 is the only one that gates code**, and it gates one path: nothing about eviction should be built
until the licence is removed, because as written a conformant device may drop swings the host has not
received. D2–D5 are all before-freeze rather than before-start, and none affects the model.

With D1 carried, PinPointStudio builds against revision 8.

---

## 6. Closing

The design is finished. Every finding in this round is a seam rather than a shape: a licence too wide, a
clause referenced but not written, two tests that describe the rule they replaced, a table that
disagrees with the clause below it, and a MUST that names a judgement instead of a field.

That is worth saying because five of those six are the same failure, and this round both diagnosed it
and reproduced it. §5b2 was accepted as the guard and then not written; `CT-I37` and `CT-I38` were left
describing revision 7's rules in the revision that changed them; the `body` cap was lowered everywhere
except in the definition of the field. **The sweep needs to cover a clause's test and its own field
table, not only its neighbouring MUSTs** — and that is one line added to D2's wording, not a new
process.

The specification is in better shape than anything else I maintain, and the remaining work is a
morning's editing rather than a decision.
