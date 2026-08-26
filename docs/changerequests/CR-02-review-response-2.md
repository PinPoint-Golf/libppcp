# CR-02 — response to review round 1

| | |
|---|---|
| **Responding to** | [PinPointCapture](../specification/reviews/CR-02-review-2-PinPointCapture.md) and [PinPointStudio](../specification/reviews/CR-02-review-2-PinPointStudio.md), round 1 of the disposition's three-round cap, 26 August 2026 |
| **Applied as** | Errata **E63–E64** |
| **Disposition corrected** | §2c, one paragraph — see the ⚠ note added there |
| **Rounds remaining** | Two, by the cap in [the disposition](CR-02-disposition.md#6-what-both-teams-are-asked-to-check). One more (round 2) is being run anyway, to review these fixes rather than only the text they amend — see below |

---

## What both teams found, independently, on the same clause

Both reviews' single blocking finding is the same defect, found by two implementations reading the same specification text with no coordination between them: **I39 constrained `actuator_command`'s `on`/`level` cardinality but said nothing about the identical `{ on, level }` shape carried by `actuator_command_ack.state` and `actuator_state.state`.** As first specified, both fields inside `state` were independently marked optional, so a conformant peer could send `state: {}` — satisfying the schema while contradicting 12.1c's own prose that `state` reports what the Actuator is actually doing, not an echo. PinPointCapture (F1) and PinPointStudio (R-1) each read this from the clause itself, not from each other's review — PinPointStudio's review was written and submitted without visibility into PinPointCapture's.

**That is exactly the category of finding this review round exists to catch**, and exactly the reason the disposition's process note (README generalisation #2, restated in CR-02 §5's own ⚠) holds here as it did for CR-01: two implementations independently reading a clause and reaching the same conclusion about its cardinality gap is worth more than either alone.

**Fixed as E63.** I39 now binds `actuator_command`, `actuator_command_ack.state` (where present) and `actuator_state.state` identically — `on` iff the named Actuator's `control` is `on_off`, `level` iff `level`, never neither, never both, on all three. `PPCP-MSG` 12.1 and 12.2 each gained a clause (12.1c1, 12.2a1) stating this locally, and `CT-I39` now asserts it against the ack and event forms as well as the request. Text-only: no field added, nothing renumbered, both reviews' own proposed fix taken essentially verbatim.

## The one other spec change

**PinPointCapture's F2 (non-blocking, polish), fixed as E64.** `DeviceStatus.reason`'s example list carried `no_source` over from `Readiness.blocked_reason`'s vocabulary, but `device_status` is emitted only for an already-declared Source — the value names a case the event's own precondition rules out. Dropped from the list; 5.20d records why, matching the finding's own reasoning about an unreachable value getting cargo-culted into a `switch`.

## What was not a spec change, and why

**PinPointStudio's R-2, R-3 and R-4 asked for verdicts, not fixes, and none of the three asked to reopen a decline or add a field.** Each is recorded rather than silently accepted:

- **R-2** corrected this disposition's own cost-framing in §2c — the "already served" decline for shots/captures/drop-totals is still right (the wire content is sufficient in principle), but likening the eventual PinPointStudio build to the `heartbeat_ack` battery field undersold it: PinPointStudio doesn't parse `capture_update` at all today and discards `capture_announce` data after one use, so the real cost is closer to `capture_announce` handling PinPointStudio already built than to reading an already-parsed scalar. **The disposition is amended with a correction note in place**, following `CR-01`'s own precedent for a review-sourced correction to already-read text — not a silent rewrite.
- **R-3** is an implementation note for PinPointStudio's own future build (key `device_status` on the wire's `source_id`, not on the `serialNumber`/`camIdent` identity the project's own memory already records as colliding for two cameras on one phone). No spec ambiguity — `Source.id` is already unambiguous. Filed for whoever eventually builds it, not for this document.
- **R-4** confirmed `BufferMargin`'s shape (single `last_discard`, no histogram) is right-sized against PinPointStudio's actual deployment, and that it maps onto neither of the two existing "buffer" UI concepts in that codebase — it is new UI, not a rewire. No spec change asked for or made.

**PinPointCapture's direct verdicts on the disposition's §6 questions (Q1–Q5) all returned "no finding"** — `DeviceStatus`/`Readiness` are genuinely distinguishable in real code with one expected, correct platform-read overlap on thermal/storage; the host-only actuator scoping matches PinPointCapture's actual deployment exactly (it never runs `role: host` anywhere); the §2c "already served" argument holds against PinPointCapture's durable bundle store; `BufferMargin`'s shape is adequate (its one real gap — discard *cause* — was never asked for by CR-02 §3c and is recorded as a possible future addition, not made now); and the achieved-value ack is buildable on iOS via KVO-observable torch properties.

**One correction PinPointStudio made against its own prior review, not against this disposition**, worth carrying forward rather than losing: `HostEngineConfig`'s callback-slot design does not accommodate `actuator_command` the way the prior review claimed — that config only answers inbound queries, and `actuator_command` is host-originated. The real precedent in PinPointStudio's codebase is a direct method calling libppcp's C API, the way `PpcpLiveSession::arm()`/`disarm()` already do. This changes nothing in the specification; it is recorded here so it is not lost between rounds, since neither the specification nor this response is the right home for an implementation-pattern correction and PinPointStudio's own review is not read again by anyone once this closes.

## Why there is a round 2 at all, given both reviews called their own fix "confirmable by re-read, no round 4 needed"

Because the fix is new writing with no review history of its own — exactly the shape `README.md`'s generalisation #6 names: *"Review the errata, not only the text they amend."* Both reviews already told the protocol owner this specific class of fix doesn't need a **fourth** round to confirm; that is a statement about round 2 closing this, not about skipping round 2 altogether. Round 2 is scoped narrowly: confirm E63/E64 read correctly against the same codebases, and give one more pass over anything else in the grant neither review has already covered twice. If round 2 returns nothing new, this closes there, one round under the cap.
