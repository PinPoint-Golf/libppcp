# PPCP specification — Draft 1

**PinPoint Capture Protocol. An open protocol for time-synchronised capture devices.**

| | |
|---|---|
| Wire version | `ppcp/1.0` |
| Status | **Draft 1 — for approval to implement** |
| Date | 22 August 2026 |
| Supersedes | `ppcp-protocol-overview.md` model draft 4, retained at [`docs/background/`](../background/ppcp-protocol-overview-draft4.md) |
| Reference implementation | `libppcp`, MIT, this repository |

---

## What this is

Model draft 4 of the protocol overview established the entity model and was reviewed on 22 August 2026. This is the **formal specification** that follows: normative field tables, a fixed message catalogue, a wire encoding, and a conformance suite — with the five defects from that review resolved and the message names that were marked provisional now settled.

It is published to get **approval to implement**. Implementation can begin against it; `ppcp/1.0` is not stable until both first-party implementations pass the conformance suite and the open issues close.

## The documents

| Read | Document | Authority | What it settles |
|---|---|---|---|
| 1st | [**PPCP-CORE**](ppcp-core.md) | Normative | Entities, timing contract, session and shot semantics, conformance profiles, twenty-eight invariants |
| 2nd | [**PPCP-MSG**](ppcp-messages.md) | Normative | Forty-two messages, channel semantics, error codes. Annex A holds the nine interaction sequences, now with real message names |
| 3rd | [**PPCP-ENC**](ppcp-encoding.md) | Normative | Framing, CBOR encoding, bulk transfer, the bundle container |
| 4th | [**PPCP-CONF**](ppcp-conformance.md) | Normative | What an implementation must demonstrate, and the six places it will silently fail |
| — | [**PPCP-RV**](ppcp-rv-scope.md) | Scope only | Rendezvous, pairing, security. **Unwritten.** What it must fix, and why it is separate |
| — | [**Review disposition**](review-disposition-2026-08-22.md) | Record | Every review comment, what was done with it, and the calls a reviewer may want to reverse |

If you have an hour, read `PPCP-CORE` §2 (profiles), §5 (the model) and §6.1 (the canonical instant), then `PPCP-MSG` Annex A. If you have twenty minutes, read the review disposition and `PPCP-CORE` §6.1.

## What changed since model draft 4

Five review defects resolved, three found while writing, and the whole thing made normative.

| | Change |
|---|---|
| **1** | `timing.frame_start_to_exposure_offset_ns` added. The obligation on `nominal_frame_start` — the default path for the entire mobile side — was previously unsatisfiable. I17 amended, I22 added. |
| **2** | **New `Mint` profile.** Issuing a Shot and arbitrating between Candidates are different operations; the v1 offline device does the first and must not do the second. I6 reassigned, I23 added. |
| **3** | Profiles gate **origination, not comprehension**. Every peer parses everything; a profile confers the right to emit. I24 added. |
| **4** | `SessionLink` added for cross-session alignment. I25 added. **Provisional** — this one goes further than the review asked. |
| **5** | Twenty-eight invariants, counted once. I1–I21 keep their numbers. |
| **6** | A filesystem-imported launch monitor record is **not** a Candidate. It reconciles through `ShotLink`. I26 added. |
| **7** | `MeasuredCapability.method` is mandatory, so a cold onboarding sample cannot be presented as a sustained figure. I28 added. |
| **8** | Found while writing: `Capture.anchor` and `Candidate.id` (I27), the rolling-shutter row formula, `Stream.calibration_id`. |

## What we are asking for

**Approval to implement**, plus a decision on each of the following. These are the places the specification took a position that the implementation partners may want changed — and each is cheap now and expensive later.

| | Question | Where |
|---|---|---|
| **Q1** | **CBOR with text keys** as the wire encoding. Integer keys would be ~40% smaller on control messages and unreadable in a hex dump. | [`ENC` §9](ppcp-encoding.md#9-design-rationale) |
| **Q2** | **`SessionLink` defined now** rather than deferred. If the instinct to defer is right, deleting it costs one type and one invariant. | [disposition §1.4](review-disposition-2026-08-22.md#14-cross-session-time-has-no-home) |
| **Q3** | **Where the `nominal_frame_start` offset goes** in the conversion: `t + offset + d/2`. If anyone intended different placement, this is the line to argue with. | [`CORE` §6.1](ppcp-core.md#61-canonical-instant) |
| **Q4** | **The coincidence window default of 50 ms** is a proposal carried from the model, not a measurement. | [`CORE` Annex B8](ppcp-core.md#annex-b--open-issues) |
| **Q5** | **The version support window** needs a number and a deprecation path. Old-app/new-host is the permanent normal case. | [`CORE` Annex B6](ppcp-core.md#annex-b--open-issues) |
| **Q6** | **Candidate audio retention is unbounded by the protocol**, deliberately. The two teams should confirm they are content that the bound is an application obligation. | [`CORE` Annex B7](ppcp-core.md#annex-b--open-issues) |
| **Q7** | **`PPCP-RV` does not exist.** Not blocking implementation; blocking any claim that the protocol is open. The QR payload format is the one item that cannot be fixed after the first release. | [`PPCP-RV`](ppcp-rv-scope.md) |

## Two things that are easy to miss

**The transport must supply two independently flow-controlled channels.** Not one connection with interleaving — two. A 25 MB capture in flight on a single stream head-of-line blocks the next shot's event. This is expensive to retrofit and invisible in every sequence diagram, because both channels are drawn as one lifeline. [`CORE` §3.1](ppcp-core.md#31-why-two-channels-is-not-negotiable)

**Three of the six silent-failure tests pass by accident when an implementation is tested only against itself.** Host-side declaration, the zero-host path, and comprehension-versus-origination all require a synthetic peer that declares something the reference implementation would not. Building that simulator early is what makes them testable at all. [`CONF` §2c](ppcp-conformance.md#2-required-test-infrastructure)

## Changing this specification

The invariants are the conformance surface, and their identifiers are stable — I1–I21 keep the numbers model draft 4 and its review used, even where the text was amended. New invariants append.

If implementation shows something here to be wrong, that is the expected outcome rather than a failure of the specification — but **the change belongs in the specification first and the code second**, or the document stops describing the system.
