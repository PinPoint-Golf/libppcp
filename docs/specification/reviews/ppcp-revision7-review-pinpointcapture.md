# PPCP revision 7 — review from the PinPointCapture mobile app

| | |
|---|---|
| Reviewing | `PPCP-CORE` revision 7 and the traceability audit — the six requirement gaps found after approval, and their closures |
| Seat | Owner of the PinPointCapture iOS/iPadOS app |
| Date | 22 August 2026 |
| Verdict | **Approve for implementation.** Two findings, one of which I would want closed before we build the session store — it leaves the gap G2 set out to fix intact for our primary use case. |

The audit is the right thing to have run, and finding six unmet requirements *after* approval is an argument for it rather than against the approval: none of the six changes anything already built, and all are additive. My review covers the closures, since none has been seen by either team.

---

## 1. `transfer: confirmed` is unreachable in the case the requirements call normal

**G2 is correctly diagnosed and the mechanism is right. It closes the gap for live sessions and leaves it open for standalone ones**, which is UC-1 — described in our requirements as *"the normal case rather than a fallback"*.

`confirmed` is set only on receipt of `capture_committed`, which travels receiver → owner over a live connection (8.4a). I38 then forbids evicting anything not `confirmed`. In a hostless session there is no receiver, so nothing is ever confirmed, so nothing is ever evictable — which is precisely the "safe, and unbounded" outcome 5.14f's own rationale sets out to end.

**The offline path does not rescue it.** REQ-OFF-1 makes an exported bundle a recorded PPCP stream, so a host importing one does durably commit those payloads. But the device is not connected at import, and nothing in the specification says the host emits `capture_committed` for captures obtained from a bundle when it next meets the owning peer. I searched for it; there is no clause linking `confirmed` to the bundle or import path at all.

Two consequences, and the second is ours to live with:

- **Storage grows without bound for a standalone golfer**, whose only backstop is REQ-OFF-2 — warn on low space and refuse to arm below a floor. Telling the UC-1 user, who by definition could not afford machine-vision cameras, that they must connect a computer or stop capturing is a poor answer to a problem the protocol now has the machinery to solve.
- **Our session library cannot render its designed state.** C3's `In Studio` chip means *"confirmed by the host, never merely uploaded"* — that phrasing is verbatim in our design handoff and it is the state the whole per-shot sync model exists to show. For a session exported as a bundle it could never light, however completely the host ingested it.

**Suggested, and it is small because the groundwork exists:** state that a receiver which durably commits a Capture obtained from a bundle sends `capture_committed` for it on its next connection with the owning peer. Identity is already sufficient — I34 made it `Capture.id` scoped by `Session.id` and the owning `Peer.id`, which is exactly what lets a host name a capture from a session it received as a file. No new message, no new field; one clause in §9 or beside 8.4a.

If the teams would rather not carry commits across a reconnect, the honest alternative is to say so, and to state that eviction in a hostless session is governed by application policy under REQ-OFF-2 rather than by I38 — so that an implementer knows I38 is not the rule that protects them there.

## 2. `annotation` carries up to 64 KiB on the control channel

`annotation` is `control` (`MSG` §11 index, §9.0), and `Annotation.body` is capped at 64 KiB (5.18f).

That cap is **larger than the payload this specification already judged too big for the control channel.** In Draft 2 I measured `capture_announce` at 44–70 KB carrying per-frame series, and the resolution was I30: per-frame series never travel on control, because *"`capture_announce` exists so a host can correlate and display a shot immediately"*. A single annotation may now be 64 KiB on that same channel.

In practice most annotations are tiny — 5.18f says so itself: *"a finger-drawn plane is a few hundred bytes; anything approaching the cap is probably a different feature."* So the typical case is fine and the **cap** is the problem: it permits, on the immediacy-critical channel, roughly what I30 was written to keep off it. A coach marking up several shots on a weak link — 4.1 Mbit/s is a state our host panel is explicitly designed for — would put shot correlation behind markup.

**Suggested:** either bring the cap down to match the document's own expectation of the artefact — a few KiB is ample for a plane, a line and a text note — or say that an annotation above some threshold travels on bulk. The first is simpler and I would prefer it; 5.18f already argues that anything larger is a different feature, so the cap may as well say so.

## 3. G1 — I agree with `Annotation` over the alternative, and there is a stronger argument than the one given

The traceability document offers shape B — a `Source` of `kind: user` — as the cheaper option, and rejects it on the grounds that it *"puts a human being in the position the model reserves for instruments"*. I agree, and there is a more concrete reason that settles it without appealing to the model's spine:

**Captures are immutable and content-addressed; annotations are edited and deleted.** A Capture's identity involves a `digest` of bytes that do not change, and there is no update path for one. But markup is a user artefact that a user redraws, moves and removes — which is why 5.18e carries `revision` and 5.18d carries `deleted`. Shape B would have required mutable Captures, and mutable Captures would have broken the idempotent re-import rule that I34 exists to provide.

So shape B is not merely inelegant; it is not implementable without damaging something already relied on. 5.18e's supersession-by-revision, and its refusal to merge concurrent edits in line with I9, are both right.

## 4. G3–G6, and one endorsement worth recording

All four are registry values and two optional fields, and I have no objection to any of them.

**G4 deserves a specific endorsement because we found the same thing empirically.** The audit states that a distinct lens is a distinct `Source`, and that `Calibration` of `kind: intrinsics` identifies the lens it was solved for. Running capability enumeration on an iPhone 16 we found the wide and the ultra-wide lens **both offering 1080p240** — and an early ranking bug in our own code selected the ultra-wide, which REQ-OPT-6 treats as the fallback a cramped studio forces and which carries heavy distortion. Without lens identity on the wire a host would have had no way to see which one produced a clip, on a device where the profiles are indistinguishable. This closes a real ambiguity rather than a theoretical one.

**G5's `Source.viewpoint` carrying a confidence and a method** is the right shape for the reason the audit gives — a self-classified viewpoint is a conclusion, and this model carries measurements, so a consumer must be able to disagree with it.

## 5. Summary

| # | Finding | Severity |
|---|---|---|
| 1 | `confirmed` unreachable hostless and across the bundle path; G2's gap survives for UC-1 | **Close before the session store is built** — one clause, groundwork already exists |
| 2 | `annotation` permits 64 KiB on the control channel, above what I30 keeps off it | **Medium** — lower the cap or route large bodies to bulk |
| 3 | G1's choice of `Annotation` over a `Source` | ✅ Agree, and mutability settles it |
| 4 | G3–G6 | ✅ No objection; G4 confirmed empirically on our hardware |

**Approve for implementation.** Neither finding blocks starting: finding 1 affects the retention and sync-state layer, which is behind the capture path in any sensible build order, and finding 2 is a constant. We will build against revision 7 as it stands.
