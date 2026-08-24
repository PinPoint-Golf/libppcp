# CR-01 — response to the second review pass

| | |
|---|---|
| **Reviewing** | [PinPointStudio pass 2](../specification/reviews/CR-01-review-2-PinPointStudio.md) and [PinPointCapture's addendum](../specification/reviews/CR-01-review-addendum-PinPointCapture.md), both 24 August 2026, against `PPCP-RV` revision 9 as amended by E34–E39 |
| **Both verdicts** | **Accept and close.** Neither reopened anything |
| **Findings** | **Four. All accepted, all applied** — errata E40–E42 |
| **The one that matters** | **R-09** — a trap E34's own fix created, and the only one that had to land before either team writes code |
| **Wire, vectors, security argument** | **Unchanged by all four.** §10.4 reproduces on both sides, twice each, across the E34 boundary |
| **Date** | 24 August 2026 |

---

## 1. R-09 — E34 broke something by explaining itself

This is the finding worth the second pass, and it is uncomfortable in a useful way.

[E34](../specification/ppcp-rv.md#errata-after-revision-9--change-request-cr-01-and-its-review) summarised its binding as a general rule:

> **Everything that varies between two otherwise-identical exchanges is bound into everything derived from them**, which is one rule rather than three and is the shape an implementer is least likely to get partly right.

That sentence was written to *help*. It was offered explicitly as the thing to hold in mind **instead of** three separate info constructions, on the grounds that the abstraction was safer than the formulas. **It is untrue of the two clauses immediately beneath it** — [11.6d](../specification/ppcp-rv.md#116-derivation)'s `sid` and [11.6e](../specification/ppcp-rv.md#116-derivation)'s `PRK` are functions of `Z` alone, deliberately.

⛔ **And it is untrue in the worst available direction.** An implementer who trusts the rule over the formulas binds the transcript into `sid` too, and that implementation produces:

- **matching digits** — `sas_raw` is transcript-bound in both, so the operator sees a successful comparison;
- **matching MACs** — `K_c` is transcript-bound in both, so the confirmation passes;
- **a divergent `PRK`** — and the pairing dies at the TLS handshake with `PSK_IDENTITY_NOT_FOUND`.

That is precisely the failure [§10.4](../specification/ppcp-rv.md#104-guided-pairing) singles out as the one that matters and warns *"will be diagnosed as"* the [3.5d](../specification/ppcp-rv.md#35-who-advertises-and-who-browses) platform limitation. **The trap did not exist before E34**, because before E34 there was no general rule to over-apply.

**The lesson generalises past the fix, and it is the reason this response leads with it.** A generalisation offered to an implementer as a safety aid **is a normative statement**, and reads with more authority than the clause it summarises precisely because it was offered as the safer thing. This one was written in the same erratum that made it false, by an author who had just spent the erratum reasoning about what binds to what.

**Applied as [E40](../specification/ppcp-rv.md#errata-after-revision-9--change-request-cr-01-and-its-review)**, both halves as proposed:

- [11.6c](../specification/ppcp-rv.md#116-derivation)'s sentence is **scoped** to the two expansions it describes rather than deleted — the rule is genuinely useful about `sas_raw` and `K_c`, and deleting it would lose that.
- [11.6c1](../specification/ppcp-rv.md#116-derivation) is added as a **MUST NOT**, with the boundary and its reason: by the time `PRK` is derived the exchange has already been authenticated by the comparison and the MACs, `Z` already commits to both public keys by construction, the only element not implied by `Z` is `v` and `v` is agreed by [11.4h](../specification/ppcp-rv.md#114-frames) or the exchange has aborted — and [§5.1](../specification/ppcp-rv.md#51-key-derivation) is taken **verbatim**, so changing its inputs from this section would give §5.1 a second shape. **That is [A17](../specification/ppcp-rv.md#annex-a--decisions-and-alternatives)'s argument arriving one layer down**, which the review spotted and which is why the boundary is the right one rather than merely the documented one.
- [11.6d](../specification/ppcp-rv.md#116-derivation) and [11.6e](../specification/ppcp-rv.md#116-derivation) carry `no transcript — see 11.6c1` inline, because the formulas are what a hurried implementer reads.
- **[RT-24a](../specification/ppcp-rv.md#9-conformance)** added: assert not merely that `PRK` matches but that it was computed **without** the transcript. The review is right that RT-18's `PRK` row catches this — and right that catching it is an argument for stating the boundary, not for relying on the test.

---

## 2. R-10 — an unstated derivation input the vector cannot expose

Accepted, both halves, as [E41](../specification/ppcp-rv.md#errata-after-revision-9--change-request-cr-01-and-its-review).

**Width.** [11.4b](../specification/ppcp-rv.md#114-frames) made `v` a CBOR unsigned integer — reaching 2⁶⁴−1 — and [11.6c](../specification/ppcp-rv.md#116-derivation) encodes it into the transcript as one octet. `v` is `1` today so they never disagree; the construction becomes undefined the first time they do. Now **1..255**, and outside that `malformed`.

**Which `v`.** One clause further than the finding, because working it through surfaced a second unstated thing: **the document never said what an acceptor puts in `bs_accept.v`.** [11.4e](../specification/ppcp-rv.md#114-frames) says it aborts on a `v` it cannot implement, which implies an echo, but implication is what R-10 is a finding about. [11.4h1](../specification/ppcp-rv.md#114-frames) now states the echo explicitly, and then the binding question answers itself: exactly one `v` is in play on each side or the exchange has ended, so the initiator binds the `v` it sent (equal to what it received, by [11.4h](../specification/ppcp-rv.md#114-frames)) and the acceptor binds the `v` it received (equal to what it echoed).

⚠ **The review is right that this closes no hole**, and right that it is worth having anyway. Every consistent reading detects the both-directions rewrite — the analysis was checked and agrees. The reason to fix it is the one the review gives: **[§10.4](../specification/ppcp-rv.md#104-guided-pairing) carries a single value of `v`, so the vector agrees with every reading**, and [RT-18](../specification/ppcp-rv.md#9-conformance) would pass two implementations that had chosen differently. An unstated derivation input that the vector cannot expose is the exact shape of an interoperability failure, and it is [B7](../specification/ppcp-rv.md#annex-b--open-issues)'s point arriving inside a section written well after B7.

---

## 3. R-07 and R-08 / F-R9-3 — wrong numbers in notes about wrong numbers

Both accepted, applied as [E42](../specification/ppcp-rv.md#errata-after-revision-9--change-request-cr-01-and-its-review). Recomputed here rather than taken on report:

```
2^32 mod 10^6 = 967296                  -> 967296 residues have 4295 preimages
                                           32704 residues have 4294
excess over uniform, most probable      =  4295e6 / 2^32 - 1  =  7.614e-6
most-to-least ratio                     =  4295 / 4294        =  1.0002329

11e66a4c  little-endian = 0x4c6ae611 = 1 282 074 129  ->  074129     (pre-E34)
c012786c  little-endian = 0x6c7812c0 = 1 819 808 448  ->  808448     (current)
1281316113 = 0x4c5f5511 — reachable from no row of §10.4
```

**R-07.** The modulo-bias paragraph carried three errors: `295 967 296` is not a possible count when there are only 10⁶ residues; the favoured set is the **large** one (96.7%), which *"and the rest"* read backwards; and the excess was given as 2.3 × 10⁻⁷ against a true **7.6 × 10⁻⁶ — thirty-three times larger**. The `1.000 23` figure was right and attached to the wrong quantity. Replaced with the review's suggested sentence.

**R-08 / F-R9-3**, found independently by both teams. The little-endian example was wrong **twice over**: mis-reversed at source, and computed from the pre-E34 `sas_raw` three lines below the box that exists to warn against exactly that. PinPointCapture's diagnosis of the provenance is right — `0x4c5f5511` against the true `0x4c6ae611` differs in the middle two octets, which is the signature of a hand transcription rather than a computation. Corrected to `1 819 808 448 mod 10⁶ = 808448`.

⛔ **PinPointStudio reported two of the three against its own first-pass review, unprompted, with the working shown.** That is the right way to handle it and it is recorded plainly in [E42](../specification/ppcp-rv.md#errata-after-revision-9--change-request-cr-01-and-its-review) — a document that says *"both review teams found it independently"* carries more weight than it has earned if one of the teams got the figure wrong.

**The second-order lesson is the more useful half, and both teams identified it.** Both recomputed all fifteen vector rows, twice each, across the E34 boundary — and **neither recomputed the arithmetic in the prose sitting beside them.** Recomputation follows the table; nothing was watching the paragraph. Worth holding on to for the next set of vectors this project publishes.

---

## 4. Process changes both passes asked for

| | Ask | Applied |
|---|---|---|
| **Record the erratum level of a reproduction** | PinPointCapture: *"check the erratum level before trusting a reproduction"* should survive into the guidance | **[RT-18](../specification/ppcp-rv.md#9-conformance) now requires it.** Both teams' first re-run produced the superseded values, and a run reporting a mismatch without saying which text it read cannot be told from an implementation defect. The E34 warning box worked exactly as designed — it is what identified both teams' results as stale text rather than as arithmetic faults. |
| **Flag derivation-affecting errata explicitly** | PinPointCapture: the RT-20 relay must reproduce **both** legs, so it cannot be built against a moving target; a relay silently built on a stale §10.4 produces the false green RT-20 exists to prevent | **Agreed and added to the [change-request process](README.md).** An erratum touching §11's frames or derivation is notified to both teams on landing, not left to be discovered on a re-read. |
| **Two review passes, not one** | — | The [status line and §11's preamble](../specification/ppcp-rv.md#11-rv-6--guided-pairing) now say *two passes, not five*, and name E40 as a defect the first pass's own fix introduced. |

---

## 5. Two things recorded rather than fixed

**The interoperable set has no slack.** PinPointCapture will implement **acceptor**; PinPointStudio **initiator**. That is a working pair and it is the shape [11.2b](../specification/ppcp-rv.md#112-why-it-is-not-tls-and-what-that-unlocks) puts them in — and it is the *entire* set. There is no third implementation. **If either side descopes its role, RV-6 has no working pair at all, and [RT-20](../specification/ppcp-rv.md#9-conformance) cannot run either**, because a relay needs two real ends. This is a programme risk rather than a defect in [9e1](../specification/ppcp-rv.md#9-conformance) — 9e1 is what makes it visible — and it is now noted beside RT-20, which is where it would otherwise first be noticed, by then too late.

**What a "device run" means changed today.** PinPointCapture reports that its `make deploy` was found to be targeting a *simulator* by UDID — `devicectl` reports simulators as `tunnelState: connected` while a real phone reports `disconnected` — now fixed. Recorded because [B14](../specification/ppcp-rv.md#annex-b--open-issues)'s outstanding item is a **device** run, and any earlier claim of one from that repository should be re-read in that light. It does not change B14's status: still discharged for the host on hardware, discharged on the capture side for macOS and the simulator, **device run required before shipping** under [5.4b](../specification/ppcp-rv.md#54-resolved-the-mechanism)'s precedent.

---

## 6. Where §11 stands, and what has not moved

**Closed.** All ten findings across both passes are applied. Both teams accept and close. [B16](../specification/ppcp-rv.md#annex-b--open-issues) closed at the first pass and the second did not reopen it. §10.4 reproduces byte for byte on two implementations sharing no code, on both sides of the E34 boundary, checked against the committed file rather than a transcription of it.

**Open.** [B14](../specification/ppcp-rv.md#annex-b--open-issues) — the iOS device run, a ship gate not a code gate. [B17](../specification/ppcp-rv.md#annex-b--open-issues) — the X25519 seam, an API question for each implementation. [B15](../specification/ppcp-rv.md#annex-b--open-issues) — the fleet case, behind [B2](../specification/ppcp-rv.md#annex-b--open-issues). And **Q3 is still Mark's**: PinPointStudio's recommendation is unchanged — yes on macOS, where `DNSServiceRegister` needs no new responder and no 5353 bind; deferred on Windows, where there is no `dns_sd.h` and the browser is compiled out.

⛔ **[RT-20](../specification/ppcp-rv.md#9-conformance) has not moved, and two review passes should not be allowed to feel as though it has.** The section has now been attacked by two teams across two passes and survived with ten corrections; its vectors are reproduced by two independent implementations; and **none of that is the same as having demonstrated the property it exists to deliver.** It needs two real implementations either side of a deliberate relay, and neither team has written §11. Until it runs, no conformance claim to RV-6 should be made — which both teams have now said, unprompted, twice.
