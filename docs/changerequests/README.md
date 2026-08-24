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
5. **Review.** Both implementation teams review the disposition and the specification change together. Their responses land in this folder.

**A request should not arrive with a mechanism attached.** CR-01 says why, and it is right: a plausible-looking scheme in a change request is harder to discard than no scheme at all, and this protocol set is committed to being normative ahead of its implementations rather than behind them.

## Index

| # | Title | Raised by | Date | Status |
|---|---|---|---|---|
| [**CR-01**](CR-01-in-band-pairing.md) | An authenticated bootstrap for a first pairing | PinPointCapture | 24 Aug 2026 | **Granted in part** — [disposition](CR-01-disposition.md). `PPCP-RV` §11 (RV-6), §3.7, errata E30–E33. **Reviewed and accepted by both teams**; six findings applied as E34–E39 — [response](CR-01-review-response.md). Reviews: [PinPointCapture](../specification/reviews/CR-01-review-PinPointCapture.md), [PinPointStudio](../specification/reviews/CR-01-review-PinPointStudio.md) |

## What CR-01 cost, and what it bought

Kept because the next request should know what the process is worth.

The grant was four errata. **The review pass that followed it was six more, two of them blocking**, and both of those were structural rather than editorial: a version field carried on the wire and bound into nothing, and a serialisation rule stated for one side of an exchange and not the other — where the *natural* implementation is the one that breaks it.

Neither was visible in the worked vectors. Neither would have been found by a test written against the section as it stood. Both would have been permanent after either team shipped. That is the same lesson [`PPCP-RV` §4.3b](../specification/ppcp-rv.md#43-payload) taught at the cost of finding out, arriving in the newest section of the document, in work by an author who had just finished writing up the earlier one.

**Two things generalised from it, for the next change request:**

1. **A granted change request is a new section with no review history, however carefully it was written.** It should be reviewed as a first draft and not as an amendment to an approved document, and its status line should say so where a reader will see it.
2. **The vectors are necessary and not sufficient.** Both teams reproduced every row byte for byte *and* found the two blocking defects, which were in clauses no vector exercises. Recomputation catches divergence; only reading catches an argument that was not carried from one clause to the one beside it.
