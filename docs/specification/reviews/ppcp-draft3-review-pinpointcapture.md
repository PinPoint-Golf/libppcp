# PPCP Draft 3 — review from the PinPointCapture mobile app

| | |
|---|---|
| Reviewing | `libppcp/docs/specification` at Draft 3, wire version `ppcp/1.0`. **`PPCP-RV` excluded from this review by agreement.** |
| Seat | Owner of the PinPointCapture iOS/iPadOS app — the peer declaring `Core + Capture + Detect + Mint + Offline` at v1 |
| Basis | The complete Draft 2 → Draft 3 diff across `CORE`, `MSG`, `ENC` and `CONF`, read against the disposition and a working iOS app |
| Date | 22 August 2026 |
| Verdict | **Approve to implement. No blocking findings, and none I would hold the library for.** Two minor points below, both one-sentence fixes. We will build against Draft 3 as it stands. |

I reviewed the diff rather than the change list, so this covers everything that moved, including the parts neither team raised. All three of my Draft 2 findings are closed, and I have verified each in the specification text rather than accepting the disposition's account.

This is a short review because the document has run out of the things I know how to find. That is the intended outcome of three rounds and I would rather say so than manufacture a fourth round's worth.

---

## 1. Verification of the Draft 2 findings

| Draft 2 finding | Fix | Verified |
|---|---|---|
| **1.1** The mint/issue race was narrowed, never closed | 8.2h bounds the host's window at the mint deadline so the two cannot overlap; 8.2j requires immediate `shot` on minting; 8.2k makes the host attach rather than compete (I35); 8.2l links a crossed pair by `shared_candidate` and withdraws neither | ✅ **Closed, and better than I proposed.** I suggested 8.2j and 8.2k. Bounding the host's window at both ends is the part that makes the race structurally impossible rather than merely resolved, and that came from the host review. The two halves together are what closes it. |
| **1.2** `Capture.digest` identity had a hole | Identity is `Capture.id` scoped by `Session.id` and owning `Peer.id`; digest demoted to a content check (I34) | ✅ Absent captures now have identity. I also traced the reinstall case — a bundle re-imported after the app is reinstalled carries the *original* owning `Peer.id`, so identity survives §3.1's new-peer-on-reinstall behaviour. |
| **1.3** Arbitration parameters mandatory in sessions that never arbitrate | Both present iff the Session has a host (5.10e, `MSG` 4.1d) | ✅ And `MSG` 4.1d's framing is better than mine: their *absence is the statement* that no arbitration occurs, which is I23 expressed structurally. |
| **§3** 5.8d unsatisfiable for an absent capture | Conditioned on the Capture having frames | ✅ |

**On the finding I missed.** Both teams found that Draft 2's issue-hold clause had reintroduced "every Candidate becomes a Shot" in the live regime. I did not — I traced the race where the host answers *late* and stopped, without following the branch where a host correctly declines and answers *never*. That is the more common path in a busy bay, and the host review found it. Worth recording that the two reviews caught different halves of the same clause.

**On D15 — the per-basis coincidence window.** The argument is right and I withdraw the suggestion. Only an arbitrating host consumes the field, so a per-`basis` override is additive rather than breaking, and there is never a second implementation applying it to diverge from. Adding a variant type now would buy nothing. The measurement design was the actionable half and Annex B8 now carries it, including the acoustic-to-acoustic floor I asked for.

---

## 2. Two minor points

Both are wording. Neither blocks anything.

### 2.1 §8.3 is entered by two different conditions, and I23 is written as though only one exists

The zero-host regime has two entry paths:

- **Roster absence** — 8.3a and I23: *"In a Session with no `host`"*. There is no host peer at all. This is UC-1.
- **Reachability** — 7.4c and 8.3f: a peer whose link drops *"enters this regime for the duration"*. The Session still has a host in its roster; it is merely unreachable.

I23 is a conformance test and reads as scoped to the first. An implementer following 8.3f into the regime during an outage could reasonably conclude I23 now binds them — that their minted Shots must carry exactly one Candidate permanently — and therefore refuse the host's attachment under 8.2k on reconnect.

**I traced it and it is not a live defect**: 8.2k requires a *shared* Candidate, which the host cannot have received while the link was down, so it never fires against an outage-minted Shot. But the two clauses use the same words for different conditions, and §11.1 was written precisely because this class of thing has now caused two defects.

**Suggested:** distinguish the terms — a *hostless session* (roster) from a *host-unreachable interval* (reachability) — and scope I23 to the former. One sentence in 8.3f noting that a Shot minted during an outage may later gain Candidates by the ordinary route would remove the ambiguity entirely.

### 2.2 The `intrinsics` scalar rule has no stated behaviour for an empty array

`ENC` 4.1d now disambiguates `intrinsics` by the **type of the first element** — a number means one constant `Matrix3`, an array means one per frame. That is the right fix and it resolves the case the major-type rule could not.

An empty array has no first element. It is degenerate — a Capture with frames always has at least one — but the rule as written has no answer for it, and a decoder branching on `first element type` will index out of bounds rather than reject cleanly. Suggest one clause: an empty `intrinsics` array is `malformed`, or is equivalent to the field being absent. Either is fine; the point is that a decoder should not have to invent it.

---

## 3. Recorded from our side, needing nothing from the specification

Stated so the protocol team has our positions on record before implementation, and so nothing here arrives later as a surprise.

- **`exposure_provenance`.** We will declare `locked_constant` under `REQ-OPT-3`'s exposure lock, which is what the product ships, and `sampled` for any unlocked source. We will not claim `per_frame` unless and until we verify the platform attaches exposure to the sample buffer. Annex B10 tracks the accuracy question; we have nothing to add to it yet.
- **Timing-constant provenance.** Every profile we declare will carry `assumed` for `frame_start_to_exposure_offset_ns` and `readout_ns` until the LED timecode rig exists. There is currently no iOS device with a `measured` value for either, and I31 is what stops that being silent.
- **The mint deadline is a user-visible latency for us.** With the defaults, a Candidate sent to a host has no Shot for up to `issue_hold_ns` + one heartbeat — 1.2 s — if the host declines to answer. Our capture screen shows the last shot immediately after impact, so it will show a candidate before it can show a shot ordinal. That is an application concern and we will handle it; recording it because it is a direct consequence of 8.2i and someone will otherwise ask why the number appears late.
- **Promotion policy (D12).** Confirmed content. Our detector decides, the unpromoted candidates survive in the bundle under I8, and a host can always re-derive.
- **Two channels.** Still no objection, and now implemented against — two `NWConnection`s satisfy T2/T5 without difficulty.

---

## 4. Sign-off

No blocking findings. §2.1 and §2.2 are one-sentence fixes that can land whenever convenient, including after implementation starts, since neither affects the wire.

**PinPointCapture will build against Draft 3 as it stands**, declaring `Core + Capture + Detect + Mint + Offline`.

Two things we are carrying rather than resolving, both already tracked:

- The LED timecode rig gates `measured` provenance on every timing constant, and gates the two Annex B8 defaults. It is the piece of test infrastructure with the longest lead time and nothing else can substitute for it.
- The synthetic peer simulator gates four of the seven silent-failure tests. `CONF` §2c is right that building it early is what makes them testable at all — a reference implementation tested against itself passes them by construction.

Neither is a specification problem. Both are on the critical path to declaring `ppcp/1.0` stable, and both are ours and the host team's to build rather than the protocol team's to specify.
