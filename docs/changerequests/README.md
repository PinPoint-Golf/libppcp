# Change requests

A **change request** asks the protocol set to serve a requirement it does not serve. It is not a defect report: the specification is self-consistent and does exactly what it says, and the request is that it say something more.

That distinction decides where a finding goes.

| | Raised as | Recorded in | Numbering |
|---|---|---|---|
| The specification contradicts itself, or an implementation cannot satisfy two clauses at once | **Finding** | the errata table in [`PPCP-CORE`](../specification/ppcp-core.md#errata-after-revision-9), and the affected document's own table | `E<n>`, one sequence across the whole set |
| The specification is correct and does not do something the product needs | **Change request** | this folder, and — where granted — the same errata sequence, so one table remains the authoritative list of what changed | `CR-<nn>` here, `E<n>` in the errata table |

Errata numbering is deliberately shared. A reader asking *"what changed since revision 8, and why"* should not have to consult two sequences, and a granted change request is a change to the document like any other.

## Process

1. **Raise.** The requesting team writes the request against a named revision and its errata, states the requirement rather than a mechanism, and says what it has already implemented (usually nothing).
2. **Store.** The request is committed here verbatim, as received. It is not edited afterwards — a disposition that disagrees with it says so in its own file.
3. **Rule.** The protocol owner grants it, grants it in part, or declines it, and records the reasoning in a disposition alongside. A decline is a legitimate outcome and is written up as fully as a grant.
4. **Specify.** Where granted, the specification is changed and the errata table gains a row per clause group.
5. **Review.** Both implementation teams review the disposition and the specification change together. Their responses land in `../specification/reviews/`, and the protocol owner's reply to each pass lands here.
6. **Review the fix.** The errata a review produces are **themselves reviewed**, by both teams, against the amended text. This is not ceremony: CR-01's second pass found a defect that its first pass's own fix had introduced ([E40](../specification/ppcp-core.md#errata-after-revision-9)), in the direction that produces the failure the document names as its worst.
7. **Notify on derivation-affecting errata.** An erratum that touches a wire format, a frame, or a derivation is **told to both teams when it lands**, rather than left to be found on a re-read. Asked for by PinPointCapture, whose [RT-20](../specification/ppcp-rv.md#9-conformance) relay must reproduce both legs of an exchange to test either — so a relay quietly built against a superseded vector produces exactly the false green that test exists to prevent.

**A request should not arrive with a mechanism attached.** CR-01 says why, and it is right: a plausible-looking scheme in a change request is harder to discard than no scheme at all, and this protocol set is committed to being normative ahead of its implementations rather than behind them.

## Index

| # | Title | Raised by | Date | Status |
|---|---|---|---|---|
| [**CR-01**](CR-01-in-band-pairing.md) | An authenticated bootstrap for a first pairing | PinPointCapture | 24 Aug 2026 | **Granted in part, reviewed twice, closed.** [Disposition](CR-01-disposition.md) → `PPCP-RV` §11 (RV-6), §3.7, errata E30–E33. **Pass 1** ([PPC](../specification/reviews/CR-01-review-PinPointCapture.md), [PPS](../specification/reviews/CR-01-review-PinPointStudio.md)): six findings, two blocking → E34–E39, [response](CR-01-review-response.md). **Pass 2** ([PPC](../specification/reviews/CR-01-review-addendum-PinPointCapture.md), [PPS](../specification/reviews/CR-01-review-2-PinPointStudio.md)): four findings → E40–E42, [response](CR-01-review-response-2.md). **Pass 3** ([PPC](../specification/reviews/CR-01-review-pass3-PinPointCapture.md), [PPS](../specification/reviews/CR-01-review-3-PinPointStudio.md)): five findings → E43–E47, [response](CR-01-review-response-3.md). Question 3 answered; [B17 closed](CR-01-x25519-seam.md) as E48. **Pass 4** ([PPC](../specification/reviews/CR-01-review-pass4-PinPointCapture.md), [PPS](../specification/reviews/CR-01-review-4-PinPointStudio.md)): six findings → E49–E51, [response](CR-01-review-response-4.md) — **nothing in the normative clauses**. **Both teams: ready to implement, no objection to starting.** [RT-20 split](../specification/ppcp-rv.md#9-conformance) so two thirds of it runs before either app writes a line. **Unimplemented; [RT-20](../specification/ppcp-rv.md#9-conformance) still cannot run** |

## What CR-01 cost, and what it bought

Kept because the next request should know what the process is worth.

The grant was four errata. **The review pass that followed it was six more, two of them blocking**, and both of those were structural rather than editorial: a version field carried on the wire and bound into nothing, and a serialisation rule stated for one side of an exchange and not the other — where the *natural* implementation is the one that breaks it.

Neither was visible in the worked vectors. Neither would have been found by a test written against the section as it stood. Both would have been permanent after either team shipped. That is the same lesson [`PPCP-RV` §4.3b](../specification/ppcp-rv.md#43-payload) taught at the cost of finding out, arriving in the newest section of the document, in work by an author who had just finished writing up the earlier one.

**A second pass over the amendments found four more**, including [E40](../specification/ppcp-core.md#errata-after-revision-9) — a trap created by the first pass's own fix. **A third pass, over *that* fix, found five more**, including [E43](../specification/ppcp-core.md#errata-after-revision-9): E40's supporting rationale asserted a property X25519 does not have, and in doing so handed a reader the argument for deleting the one binding holding the whole construction up.

**Three passes, and each found a defect the previous one had introduced — every one of them in a *rationale* rather than in a clause.** The clauses have been reviewed more carefully than the sentences explaining them, and the explanations are what an implementer reads first.

**Six things generalised from it, for the next change request:**

1. **A granted change request is a new section with no review history, however carefully it was written.** It should be reviewed as a first draft and not as an amendment to an approved document, and its status line should say so where a reader will see it.
2. **The vectors are necessary and not sufficient.** Both teams reproduced every row byte for byte *and* found the two blocking defects, which were in clauses no vector exercises. Recomputation catches divergence; only reading catches an argument that was not carried from one clause to the one beside it.
3. **A generalisation offered to an implementer as a safety aid is a normative statement.** It reads with more authority than the clause it summarises, precisely because it was offered as the safer thing — so it needs the same scoping, and the same review, as a clause.
4. **Recomputation follows the table; nothing watches the paragraph.** Both teams recomputed fifteen vector rows twice each and neither recomputed the arithmetic in the prose beside them, where two wrong figures had been quoted onward from a review in good faith ([E42](../specification/ppcp-core.md#errata-after-revision-9)). Numbers in commentary are numbers — and E42 then got the *attribution* wrong too, which the blamed team accepted without checking and the guilty one corrected against itself a pass later.
5. **Review the errata, not only the text they amend.** Two of the three passes found their most serious item inside the previous pass's fix. A fix is new writing with no review history, exactly like the section it repairs.
6. **A vector catches divergence in values and is blind to divergence in rules.** §11 produced one *unstated rule* per pass — which roles a claim covers, which `v` is bound, whether unknown map keys are rejected — and no reproduction of §10.4 could ever have surfaced any of them. Two implementations agreeing on every published byte is not evidence they agree.
