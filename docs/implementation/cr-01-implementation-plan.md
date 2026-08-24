# CR-01 implementation plan — RV-6, guided pairing

**The tracker for bringing `PPCP-RV` §11 into `libppcp`, PinPointStudio and PinPointCapture.**

| | |
|---|---|
| Status | **Session C1 open**, 24 August 2026 — `libppcp` L18/L19/L20, PinPointStudio H9, PinPointCapture D10 all in flight, three agents in parallel. Nothing was implemented in any repository before today |
| Date | 24 August 2026 |
| Against | `PPCP-RV` revision 9 as amended by **errata E30–E55** — [CR-01 closed](../changerequests/README.md), five review passes, no open specification items |
| Companion plan | [`implementation-plan.md`](implementation-plan.md) — the main programme, **complete and closed**. This is a separate document and does not amend it |
| Companion record | [`../conformance/matrix.md`](../conformance/matrix.md) — rows RT-18…RT-27 are what this plan fills in |
| Change request | [`../changerequests/CR-01-in-band-pairing.md`](../changerequests/CR-01-in-band-pairing.md), its [disposition](../changerequests/CR-01-disposition.md), five [review](../changerequests/CR-01-review-response.md) [responses](../changerequests/CR-01-review-response-5.md), and the [X25519 seam](../changerequests/CR-01-x25519-seam.md) |

---

## 0. What this plan delivers, and what it does not

**Delivers.** A first pairing between a host and a capture peer that have never met, with no code carried between two screens — six digits compared on both, affirmed at both ends.

- `libppcp` — the derivation, the frames, the five-frame exchange as a sans-I/O engine, and **the relay**: a third implementation carrying both roles, which is the only thing that makes RT-20 runnable at all.
- PinPointStudio — the **initiator**: browse, dial, display, affirm. Plus advertising for reconnection ([3.5e](../specification/ppcp-rv.md#35-who-advertises-and-who-browses)), without which §7.4's persistence delivers nothing on this deployment.
- PinPointCapture — the **acceptor**: open a window, be found, accept, display, affirm, then dial the host under §5.

**Does not deliver.** Any change to the pairing-code path, which is REQUIRED ([2a](../specification/ppcp-rv.md#2-rendezvous-paths)), measured 30/30, and untouched. Nor a fleet or venue credential ([B15](../specification/ppcp-rv.md#annex-b--open-issues), behind B2). Nor version negotiation ([B18](../specification/ppcp-rv.md#annex-b--open-issues), decided against).

**What "done" looks like.** [RT-20c](../specification/ppcp-rv.md#9-conformance) passes: the two real implementations either side of the relay, both showing mismatched digits, both declining, neither pairing. ⛔ **Until that row runs, [9g](../specification/ppcp-rv.md#9-conformance) forbids anyone reporting an RV-6 pass** — and every other row in this plan passing is not it.

---

## 1. Ground rules

[The main plan's §1 applies unchanged](implementation-plan.md) — three repositories, three licences, nothing copied between them, each agent in its own repo, the specification changes first, commit to `main`, the library owns no I/O. **Five more are specific to this work, and the first is restated in full rather than referenced.**

9. ⛔ **Builds are bounded — the explicit numbers, not a reference.** Three agents in parallel means **`-j3` each and never a bare `-j`**; total jobs across all agents stays at or below 8. No agent loops a build waiting on another repo — poll `git log`, build once. No agent builds the PinPointStudio *application* target. An agent in PinPointStudio or PinPointCapture reads that repo's `CLAUDE.md` and memory index, and its build recipe, **before its first build**. This is restated here rather than left to [the main plan's rule 7](implementation-plan.md) because a bare `-j` crashed the machine once and cost forty minutes of a session, and the agent that did it had never read the notes that would have stopped it.
10. **The specification is closed and this plan does not reopen it.** CR-01 took five review passes and twenty-six findings, and the last two passes found nothing in the normative clauses. An agent that believes a clause is wrong **stops and reports**; it does not work around it and does not edit `PPCP-RV`. The bar for a twenty-seventh finding is a demonstration, not a reading.
11. **Nothing claims RV-6 conformance.** [9g](../specification/ppcp-rv.md#9-conformance) is a MUST: a claim names [RT-20c](../specification/ppcp-rv.md#9-conformance) and states its result, and **no aggregate pass** is reported while it is unrun. That binds the claim files from the first commit, not from the last.
12. **The traps of §4 are read before any code is written**, by every agent, in every repo. They are nine ways to produce an implementation that passes every test in this document and authenticates nothing. Five review passes found them; none of them is discoverable from the wire.
13. **X25519 never enters `libppcp`.** [§11.11](../specification/ppcp-rv.md#1111-where-x25519-comes-from) and [CA1](#3-decisions-this-plan-fixes). An agent that finds itself wanting a curve in the library has misread the seam and stops.

---

## 2. What is claimed — and what 9g forbids anyone claiming

| Implementation | Role under [9e1](../specification/ppcp-rv.md#9-conformance) | Claim file |
|---|---|---|
| `libppcp` reference | **both** — it holds the derivation, the engine and the relay | `libppcp/docs/conformance/claim-libppcp.md` |
| PinPointStudio | **initiator only** | `PinPointStudio/docs/ppcp-conformance.md` |
| PinPointCapture | **acceptor only** | `PinPointCapture/docs/ppcp-conformance.md` |

⚠ **Initiator-only and acceptor-only is a working pair and it is the entire interoperable population.** If either side descopes its role there is no pair at all, and RT-20c cannot run either, because a relay needs two real ends. [CA4](#3-decisions-this-plan-fixes) is what addresses that and it is the reason the relay comes first.

**Every claim file, from its first commit, carries a named `RT-20c` row reading `unrun` and no RV-6 aggregate.** That is [9g](../specification/ppcp-rv.md#9-conformance) and it is not a courtesy: four vector reproductions, two implementations and five review passes is a great deal of green, and none of it touches the property §11 exists to deliver.

---

## 3. Decisions this plan fixes

Decided here so three teams do not decide them three ways. The **CA** prefix keeps them distinct from the main plan's A1–A13 and from `PPCP-RV` Annex A. Reversing one is a plan change, recorded in [§10](#10-decisions-findings-and-errata-log).

| # | Decision | Why |
|---|---|---|
| **CA1** | **X25519 is a *parameter*, not a dependency and not a callback.** Each application computes its own `pk` and the shared secret `Z` with the crypto it already links — OpenSSL on the host, CryptoKit on the device — and passes 32 octets in. | [§11.11](../specification/ppcp-rv.md#1111-where-x25519-comes-from) / E48. A callback is right where the library needs something *during a loop it owns*; key agreement has no loop. Keeps [plan A1](implementation-plan.md)'s "no dependencies" intact, and makes the derivation a pure function so [§10.4](../specification/ppcp-rv.md#104-guided-pairing) is testable in the one component both apps share. API: [`CR-01-x25519-seam.md`](../changerequests/CR-01-x25519-seam.md). |
| **CA2** | **The five-frame exchange is a sans-I/O engine in `libppcp`**, not written twice in the applications. The apps supply bytes, the keypair, `Z`, timers, the window and the user's affirmation. | [Ground rule 1](implementation-plan.md): if two repositories need the same thing it belongs in `libppcp`. Four of the nine traps in [§4](#4-the-traps) live in the exchange, and one implementation of them is one place to get them right — and one place for [RT-19](../specification/ppcp-rv.md#9-conformance) and [RT-24a](../specification/ppcp-rv.md#9-conformance) to read. |
| **CA3** | **The relay lives in `libppcp/tools` and is built FIRST**, before either application implements its role. | Both remaining security-touching tests depend on it; it needs no application to exist; and building it produces the third both-roles implementation that [CA4](#3-decisions-this-plan-fixes) says the set otherwise lacks. **The natural gravity of a test needing two implementations is to slide to the end, where a failure costs most.** Agreed by both teams. |
| **CA4** | **PinPointStudio is initiator-only; PinPointCapture is acceptor-only.** | [11.2b](../specification/ppcp-rv.md#112-why-it-is-not-tls-and-what-that-unlocks) puts them there and [9e1](../specification/ppcp-rv.md#9-conformance) permits a single-role claim. A desktop host has no product reason to accept a guided pairing from a stranger. |
| **CA5** | **PinPointStudio advertises for reconnection on macOS; Windows is deferred** and recorded as a dependency decision, not a protocol one. | [3.5e](../specification/ppcp-rv.md#35-who-advertises-and-who-browses) / [A18](../specification/ppcp-rv.md#annex-a--decisions-and-alternatives). `DNSServiceRegister` does not bind UDP 5353 — it asks the responder over the IPC socket the existing browse path already uses, so it is additive. Windows has no `dns_sd.h` and would need Bonjour. **Until one end advertises, §7.4's persistence is dead weight.** |
| **CA6** | **The bootstrap write path is separate from the PPCP frame writer**, and `ppcp_channel_validate()` is **not touched**. | Trap 1. Its rejection of channel 255 *is* [11.4a](../specification/ppcp-rv.md#114-frames)'s fail-closed property. |
| **CA7** | **No RV-6 conformance claim until [RT-20c](../specification/ppcp-rv.md#9-conformance).** | [9g](../specification/ppcp-rv.md#9-conformance), and [ground rule 10](#1-ground-rules). |

---

## 4. The traps

⛔ **Read this before writing code. Every one of these produces an implementation that passes every static test in `PPCP-RV` and is wrong, and five review passes were needed to find them.** Each names the clause and what it costs.

| # | The trap | Why it is tempting | What it costs |
|---|---|---|---|
| **1** | **Relaxing `ppcp_channel_validate()` to let a bootstrap frame out.** `src/ppcp_frame.c:43` returns `PPCP_ERR_MALFORMED` for channel 255 and **must go on doing so**. | It is the first thing that blocks you, and it looks like an oversight. | That rejection **is** [11.4a](../specification/ppcp-rv.md#114-frames)'s fail-closed property — it is what stops a bootstrap frame being half-understood on a PPCP link. Relaxing it deletes the safety argument **while implementing the clause that relies on it**, and every test still passes. Use a separate write path ([CA6](#3-decisions-this-plan-fixes)). |
| **2** | **Sending `bs_accept` only after `pk_i` arrives.** | It saves a round trip and reads as an obvious optimisation. | ⛔ **It destroys the security of the entire path.** [11.5c](../specification/ppcp-rv.md#115-the-exchange). An interposer then chooses its key after seeing the honest one and grinds until both legs show the same digits — seconds of work. **Nothing on the wire changes and no static test can see it.** [RT-20b](../specification/ppcp-rv.md#9-conformance)(ii) is the only thing that catches it, and only via the relay. |
| **3** | **Dialling several discovered windows to show the operator a list of numbers.** | [3.3f](../specification/ppcp-rv.md#33-txt-record)'s `dl` exists so a browser seeing four windows can tell them apart — a list is the obvious interface. | [11.3d1](../specification/ppcp-rv.md#113-roles-and-the-connection). An attacker advertising N windows gets **N blind draws against one confirmation, with the operator finding the collision for them.** The user selects **before** the attempt begins. |
| **4** | **Binding the transcript into `sid` or `PRK` "for consistency".** | E34 bound `v ‖ pk_i ‖ pk_a` into `sas_raw` and `K_c`, and the rule reads as though it should apply to everything derived. | [11.6c1](../specification/ppcp-rv.md#116-derivation). Produces **matching digits, matching MACs and a divergent `PRK`** — a successful comparison, a successful confirmation, then `PSK_IDENTITY_NOT_FOUND`, which looks exactly like the [3.5d](../specification/ppcp-rv.md#35-who-advertises-and-who-browses) platform limitation and will be diagnosed as one. Counter-vector: `sid` `18dd04b1…`. |
| **5** | **Dropping `pk_i ‖ pk_a` from the `sas_raw` info because `Z` already depends on them.** | It is true that `Z` is computed from both keys, so naming them again looks redundant. | [11.6c2](../specification/ppcp-rv.md#116-derivation). **X25519 is not contributory**: a different public key yields a bit-identical, non-zero `Z` ([§10.4](../specification/ppcp-rv.md#104-guided-pairing)'s R-11 witness, `pk_a'` `87abc1e8…`). That binding is the **only** thing separating the two peers under substitution, and removing it is undetectable from outside. |
| **6** | **Keeping the `ppcp_rv_bootstrap` struct because it holds the `PRK`.** | It is the natural thing to do with a struct that carries what you need. | [11.6f](../specification/ppcp-rv.md#116-derivation), [11.7f](../specification/ppcp-rv.md#117-the-short-authentication-string). It keeps `K_c` and the digits alive against two MUSTs. Copy out `sid`/`prk`/`k_tls`/`k_id`, then wipe. **On every exit path** — a peer holds `PRK` from the moment it has `Z`, up to the 60 seconds [11.3e](../specification/ppcp-rv.md#113-roles-and-the-connection) allows, before the pairing exists at all ([11.5g](../specification/ppcp-rv.md#115-the-exchange)). |
| **7** | **Reporting a failed key agreement as a transport error, and retrying it.** | OpenSSL fails the call and CryptoKit throws; both land on the generic error path. | [11.6b](../specification/ppcp-rv.md#116-derivation), [11.11f](../specification/ppcp-rv.md#1111-where-x25519-comes-from). **A rejected key is an attack signal**, and a retry loop around it eats [3.7b](../specification/ppcp-rv.md#37-the-bootstrap-window)'s single-attempt bound — which is what [§11.8](../specification/ppcp-rv.md#118-what-the-comparison-proves)'s whole argument rests on. Note the split: the library sees only the zero, the application only the failure. |
| **8** | **Comparing the digits in software.** Matching them across a channel the peer also controls, or accepting the counterpart's assertion that they matched. | It removes the last tap, and the numbers *are* available on both ends. | [11.1d](../specification/ppcp-rv.md#111-what-this-path-is-and-the-one-thing-it-cannot-be). **It removes the entire security of the path while leaving every byte on the wire unchanged.** The comparison has value only because it crosses a channel the attacker is not on, and the only such channel is a person looking at two screens. A peer that does this **passes every static test in the document**. |
| **9** | **Reporting an aggregate pass for RV-6.** | Every row you can run will be green. | [9g](../specification/ppcp-rv.md#9-conformance). None of the rows you can run touches the security property. **A protocol can have flawless arithmetic and no security at all.** |

⚠ **Two more that are UX, not code, and are MUSTs anyway**: the affirmative control is **not** the default and not where a stray tap lands, and the prompt asks whether the numbers *match* ([11.7d](../specification/ppcp-rv.md#117-the-short-authentication-string)); and a mismatch or MAC failure is **not** reported in terms that invite a retry ([11.9c](../specification/ppcp-rv.md#119-aborting-and-the-one-attempt-rule)) — a mismatch is the one signal this path produces that an attack is under way, and a dialogue whose reflex is *try again* converts a one-shot bound into an unbounded one by way of muscle memory.

---

## 5. Work packages — `libppcp` (team L)

Numbered from L18, continuing the main plan's L0–L17 so the identifiers stay globally unique.

### L18 — Bootstrap derivation and the vectors

| | |
|---|---|
| Deliverable | `ppcp_rv_bs_commit()`, `ppcp_rv_bootstrap_derive()`, `ppcp_rv_bootstrap_wipe()`, `ppcp_rv_ct_equal()` as agreed in [the seam note](../changerequests/CR-01-x25519-seam.md). One call, not six — one transcript construction is one chance to get it wrong. The struct is **split by lifetime** with the wipe rule in the header (trap 6). Failures are **distinguishable**: all-zero `z` → `invalid_key` (an attack signal), `v` outside 1..255 → `malformed` (a caller's bug); mapping one to the other reports a programming error as an attack. **No X25519 anywhere in the library** ([CA1](#3-decisions-this-plan-fixes)). |
| Vectors | **Every row of [§10.4](../specification/ppcp-rv.md#104-guided-pairing)** — the thirteen main rows, the two derivation counter-vectors (`PRK` `9b779245…`, `sid` `18dd04b1…`), and the **interposer quadruple** (`849063` ≠ `576027`), which needs no key agreement and is what makes RT-20a runnable here. |
| Spec | `RV` [§11.6](../specification/ppcp-rv.md#116-derivation), [§11.11](../specification/ppcp-rv.md#1111-where-x25519-comes-from), [§10.4](../specification/ppcp-rv.md#104-guided-pairing) |
| Traps | 4, 5, 6, 7 (zero half) |
| Unlocks | RT-18, RT-20a(a), RT-24a, RT-24b, RT-27 (library half) |
| Status | ✅ **done** — C1, `4b47dee`. The [seam note](../changerequests/CR-01-x25519-seam.md) §4 surface implemented **verbatim**, one addition: `PPCP_ERR_RV_INVALID_KEY`, appended to `ppcp_result` so nothing renumbers. **Every row of [§10.4](../specification/ppcp-rv.md#104-guided-pairing) reproduces byte-for-byte against revision 9 as amended by E30–E55**, both counter-vectors and the interposer quadruple (`849063` ≠ `576027`). RT-18, RT-20a(a), RT-24b passing; orchestrator re-ran the suite at 53/53 |

### L19 — Bootstrap frames

| | |
|---|---|
| Deliverable | Encode and decode `bs_offer`, `bs_accept`, `bs_reveal`, `bs_confirm`, `bs_abort` — deterministic CBOR ([4.3a](../specification/ppcp-rv.md#43-payload)), `v` first by the two-character rule, the `PPCP-ENC` §3 header with channel **255**, written through a **separate path that does not consult `ppcp_channel_validate()`** ([CA6](#3-decisions-this-plan-fixes), trap 1). An **unrecognised map key is `malformed`** ([11.4c1](../specification/ppcp-rv.md#114-frames)) — the one closed vocabulary in the protocol set, against `3.3a`/`4.2c`/`A4` all pointing the other way. Out-of-order, unknown type, wrong field type or length, duplicate frame → `malformed`. The seven abort reason codes, with a user's refusal and a failed MAC **indistinguishable** as `rejected` ([11.4f](../specification/ppcp-rv.md#114-frames)). |
| Spec | `RV` [§11.4](../specification/ppcp-rv.md#114-frames), [`PPCP-ENC` §3](../specification/ppcp-encoding.md#3-framing) |
| Traps | 1 |
| Unlocks | the wire half of RT-19, RT-24 |
| Status | ✅ **done** — C1, `ad54ae0`, header fix in `bf9ee3f`. Five frames on a write path of their own; `ppcp_bs_frame_read()` carries its own inline header parse and **never calls `ppcp_frame_header_parse()` or `ppcp_channel_validate()`**, so [trap 1](#4-the-traps) holds in **both** directions — `src/ppcp_frame.c` is byte-identical to session start, verified by the orchestrator |

### L20 — The exchange as a sans-I/O engine

| | |
|---|---|
| Deliverable | The five frames of [§11.5](../specification/ppcp-rv.md#115-the-exchange) as a state machine: takes an inbound frame, emits an outbound one or an abort. Enforces the **order** (trap 2 — the engine emits `bs_accept` on receiving `bs_offer`, never later), the commitment check **in constant time** ([11.5d](../specification/ppcp-rv.md#115-the-exchange)), MAC verification in constant time with the reflection guard, `v` echo and the bind rule ([11.4h](../specification/ppcp-rv.md#114-frames), [11.4h1](../specification/ppcp-rv.md#114-frames)), and **[11.5g](../specification/ppcp-rv.md#115-the-exchange)'s gate**: nothing is held until both this side has affirmed and the counterpart's MAC verifies. Erases on **every** exit path including abort ([11.6f](../specification/ppcp-rv.md#116-derivation) as amended by E51 — `PRK`, `K_tls`, `K_id`, `sid` too, on a handshake that failed). Both roles, since [CA2](#3-decisions-this-plan-fixes) puts it here once and the relay needs both. The application supplies the keypair, `Z`, the socket, the timers and the user's affirmation; the library owns none of them ([main plan ground rule 8](implementation-plan.md)). |
| Spec | `RV` [§11.5](../specification/ppcp-rv.md#115-the-exchange), [§11.9](../specification/ppcp-rv.md#119-aborting-and-the-one-attempt-rule), [§11.10](../specification/ppcp-rv.md#1110-what-must-not-cross-a-bootstrap-connection) |
| Traps | 2, 6, 7 |
| Unlocks | RT-19, RT-21 (library half), RT-24 |
| Status | ✅ **done** — C1, `bf9ee3f`. Both roles, sans-I/O, 53/53 with ASan and UBSan clean. Two hazards caught **by its own tests, not by review**: `recv()` cleared the step before reading the inbound buffer, so the natural loop — one engine's `step.out` into the other's `recv()` — read a zeroed frame, **which is exactly how L21 will be written**; and a counterpart's `bs_confirm` can arrive before this side has been handed `Z`, since the application supplies `Z` asynchronously to the socket — in order *on the wire*, so the engine holds it until `K_c` exists. ⚠ A naive state machine rejects that as out-of-order; [CA2](#3-decisions-this-plan-fixes) means both applications get the right behaviour without writing it |

### L21 — The relay (`tools/ppcp-relay`) ⛔ **built before either application implements its role**

| | |
|---|---|
| Deliverable | A deliberate man-in-the-middle: **acceptor toward one peer, initiator toward the other**, with its own keypair per leg. Runs L20's engine twice. Asserts, per [RT-20b](../specification/ppcp-rv.md#9-conformance): the two legs' digits **differ**; a peer whose user declines **does not pair**, and its window closes and does not reopen without a further user action; the **commitment ordering** in whichever half the peer under test implements — withhold `bs_reveal` and check `pk_a` already arrived (acceptor), or simply do not reply and check no `pk_i` follows (initiator); and that **each of its own legs completes on demand**, or the harness is testing its own bug. Emits matrix rows in the format `ppcp-conform` already uses, and lives beside it so both teams run **the same relay** rather than two. |
| Why first | [CA3](#3-decisions-this-plan-fixes). Both remaining security-touching tests depend on it, it needs no application to exist, and **building it produces the third implementation carrying both roles** — the only slack the interoperable set has ([§2](#2-what-is-claimed--and-what-9g-forbids-anyone-claiming)). It is also what each application develops against from day one. |
| Spec | `RV` [RT-20b, RT-20c](../specification/ppcp-rv.md#9-conformance), [§11.8](../specification/ppcp-rv.md#118-what-the-comparison-proves) |
| Unlocks | RT-20b against either peer; RT-20c against both |
| Status | ☐ not started |

### L22 — Conformance rows, and the 9g claim shape

| | |
|---|---|
| Deliverable | RT-18…RT-27 into `ppcp-conform` and [`matrix.md`](../conformance/matrix.md). **RT-20a(a) deterministic** against the interposer quadruple — the required part, no curve. **RT-20a(b)** where key agreement is available: assert **no collision** over a stated run and **uniformity by χ²**; ⛔ **never assert the rate** — separating 10⁻⁶ from a 5% neighbour needs ~10⁹ trials for 1.5 σ, and a row nobody can run is a row that gets ticked. RT-24c (the R-11 witness) marked application-side, since it needs a curve. The `libppcp` claim file carries a named **RT-20c `unrun`** row and no RV-6 aggregate ([CA7](#3-decisions-this-plan-fixes)). |
| Spec | `RV` [§9](../specification/ppcp-rv.md#9-conformance), [`PPCP-CONF` §1](../specification/ppcp-conformance.md#1-claiming-conformance) |
| Traps | 9 |
| Unlocks | the matrix rows themselves |
| Status | ☐ not started |

---

## 6. Work packages — PinPointStudio (team H)

Numbered from H9.

### H9 — Advertise for reconnection ⚠ needs no bootstrap API — runnable in the first session

| | |
|---|---|
| Deliverable | `DNSServiceRegister` on macOS, additive to the existing browser in `src/Ppcp/ppcp_discovery.*` — **it does not bind UDP 5353**, it asks the same responder over the same IPC socket the browse path already uses, which is the objection that header records and which does not apply. TXT per [3.3a](../specification/ppcp-rv.md#33-txt-record): `txtvers`, `pv`, `role: host`, `rn`, `rid`, all of which `libppcp` already computes. **A stable per-registration instance name** ([3.2d](../specification/ppcp-rv.md#32-instance-name)) with only the TXT rotating — a rename is a deregister/probe/announce cycle and at seconds-scale that is the condition [3.6a](../specification/ppcp-rv.md#36-multicast-is-not-to-be-relied-on) says breaks discovery. **Rotation sized on pairings *held*, not devices present** ([3.4d3](../specification/ppcp-rv.md#34-resolvable-identifiers) as amended by E55) — [7.4a](../specification/ppcp-rv.md#74-persistent-pairings) gives a persisted pairing no expiry, so a studio host accumulates them indefinitely. **Windows deferred** ([CA5](#3-decisions-this-plan-fixes)), recorded not silently skipped. |
| Spec | `RV` [3.2d](../specification/ppcp-rv.md#32-instance-name), [3.3](../specification/ppcp-rv.md#33-txt-record), [3.4d1/d3](../specification/ppcp-rv.md#34-resolvable-identifiers), [3.5e](../specification/ppcp-rv.md#35-who-advertises-and-who-browses) |
| Unlocks | RT-7, RT-8 (host half); makes [§7.4](../specification/ppcp-rv.md#74-persistent-pairings) persistence useful at all |
| Status | ✅ **done** — C1, PinPointStudio `8ed4259`. `DNSServiceRegister` on macOS, stable per-registration instance name with only the TXT rotating, rotation sized on pairings *held* per E55, Windows deferred and recorded. RT-7 and RT-8 host halves passing, 14/14 in `ppcp_advertise_test`, tree 26/26. **Observed live**: `dns-sd -B` showed one instance name across three successive `rid` values — one registration, not three, which is the [3.6a](../specification/ppcp-rv.md#36-multicast-is-not-to-be-relied-on) failure E49 corrected the clause for. Left open: `pv` is the literal `"1.0"` rather than derived from `version.h` |

### H10 — The initiator

| | |
|---|---|
| Deliverable | Browse for `bs=1` instances ([3.3f](../specification/ppcp-rv.md#33-txt-record), [3.3g](../specification/ppcp-rv.md#33-txt-record) — an instance carrying both `bs` and `rid` is ignored), present `dl` as **untrusted display text** ([4.4d](../specification/ppcp-rv.md#44-handling-a-scanned-code)), **the user selects one before any attempt begins** and **one attempt at a time** (trap 3, [11.3d1](../specification/ppcp-rv.md#113-roles-and-the-connection)). Dial the instance's SRV endpoint ([3.7f](../specification/ppcp-rv.md#37-the-bootstrap-window)), drive L20's engine, supply `pk` and `Z` from OpenSSL `EVP_PKEY_*` ([CA1](#3-decisions-this-plan-fixes)) and map a **failed derive** to `invalid_key`, never to a transport error, never retried (trap 7). Display six digits grouped `435 948` in the existing QML pairing popup, affirmative control **not** the default ([11.7d](../specification/ppcp-rv.md#117-the-short-authentication-string)); [11.9c](../specification/ppcp-rv.md#119-aborting-and-the-one-attempt-rule) messaging with **no retry affordance** after a mismatch, and the code offered on the **first** `unsupported_version` ([11.9d1](../specification/ppcp-rv.md#119-aborting-and-the-one-attempt-rule)). Close the connection and connect under §5 as normal ([11.5h](../specification/ppcp-rv.md#115-the-exchange)). |
| Spec | `RV` [§11.2](../specification/ppcp-rv.md#112-why-it-is-not-tls-and-what-that-unlocks)–[§11.9](../specification/ppcp-rv.md#119-aborting-and-the-one-attempt-rule), [§3.7](../specification/ppcp-rv.md#37-the-bootstrap-window) |
| Traps | 3, 7, 8, and the two UX MUSTs |
| Unlocks | RT-20b (initiator mirror), RT-22 (browse half), RT-24c, RT-25, RT-26, RT-27 (host half) |
| Status | ☐ not started |

### H11 — Conformance claim

| | |
|---|---|
| Deliverable | The claim states **initiator only** ([9e1](../specification/ppcp-rv.md#9-conformance)), names every RT row with its command, and carries **RT-20c `unrun`** with **no RV-6 aggregate** until it runs ([CA7](#3-decisions-this-plan-fixes), trap 9). RT-25 and RT-26 are `review` rows with a named reviewer and a commit. |
| Traps | 9 |
| Status | ☐ not started |

---

## 7. Work packages — PinPointCapture (team D)

Numbered from D10.

### D10 — The bootstrap window ⚠ the advertising half needs no bootstrap API — runnable in the first session

| | |
|---|---|
| Deliverable | A window opened **only by an explicit user action** ([3.7a](../specification/ppcp-rv.md#37-the-bootstrap-window)), closing on the earliest of one completed pairing, one abort or rejection, a peer timeout of **at most 180 s**, or a further user action ([3.7b](../specification/ppcp-rv.md#37-the-bootstrap-window)) — and **not reopening without a further user action** ([11.9b](../specification/ppcp-rv.md#119-aborting-and-the-one-attempt-rule)). `bn` fresh per window, instance name `PPCP-` + 8 hex ([3.2c](../specification/ppcp-rv.md#32-instance-name)), TXT carrying `bs=1` and an optional operator-set `dl` and **no `rn`, no `rid`** ([3.3f](../specification/ppcp-rv.md#33-txt-record), [3.3g](../specification/ppcp-rv.md#33-txt-record) — `dl` never defaulted from a device or user name). A **listener on an endpoint distinct from the PPCP listener** ([3.7f](../specification/ppcp-rv.md#37-the-bootstrap-window)), refusing anything whose first frame is not a well-formed `bs_offer`, and **one attempt at a time** ([11.3d](../specification/ppcp-rv.md#113-roles-and-the-connection)). |
| Spec | `RV` [§3.7](../specification/ppcp-rv.md#37-the-bootstrap-window), [3.2c](../specification/ppcp-rv.md#32-instance-name), [3.3f/g](../specification/ppcp-rv.md#33-txt-record) |
| Unlocks | RT-22 |
| Status | ◐ **delivered, one half unrun** — C1, PinPointCapture `dbfc262`, `5230596`, `deaac7d`. The window is **its own type, not a flag on the advertiser** — a flag is what produces a record carrying both key sets, which [3.3g](../specification/ppcp-rv.md#33-txt-record) makes malformed; `tick()` can close a window and can never open one; the listener is ready *before* the window opens, so a bind failure does not burn the one window [11.9b](../specification/ppcp-rv.md#119-aborting-and-the-one-attempt-rule) will not reopen; no `bs_offer` parser — the 8-octet envelope is read and the judgement handed to a seam L19 fills in C2. **RT-22 two assertions of three**, 253/253 in `make test-core`. ⛔ The withdrawal half is written and **unrun**, blocked on [log row 4](#10-decisions-findings-and-errata-log) |

### D11 — The acceptor

| | |
|---|---|
| Deliverable | Drive L20's engine as acceptor. Supply `pk` and `Z` from `Curve25519.KeyAgreement` ([CA1](#3-decisions-this-plan-fixes)); **the throw half of [11.11f](../specification/ppcp-rv.md#1111-where-x25519-comes-from)** — `underlyingCoreCryptoError` is `invalid_key`, not a transport error, never retried (trap 7). ⛔ **`bs_accept` is emitted on receiving `bs_offer` and never later** (trap 2, [11.5c](../specification/ppcp-rv.md#115-the-exchange)) — the round trip it appears to save is the security of the path. Display six digits and require **this device's own user** to affirm ([11.7c](../specification/ppcp-rv.md#117-the-short-authentication-string)); the counterpart's `bs_confirm` never stands in for it. Erasure on every exit path, with [11.11h1](../specification/ppcp-rv.md#1111-where-x25519-comes-from)'s mitigation — derive into a `SymmetricKey`, which *is* documented to zero, and let `SharedSecret` go out of scope at once. Then **dial the host under §5** ([11.2b](../specification/ppcp-rv.md#112-why-it-is-not-tls-and-what-that-unlocks)) — the roles swap between the two connections. |
| Spec | `RV` [§11.5](../specification/ppcp-rv.md#115-the-exchange)–[§11.7](../specification/ppcp-rv.md#117-the-short-authentication-string), [§11.11](../specification/ppcp-rv.md#1111-where-x25519-comes-from) |
| Traps | 2, 6, 7, 8, and the two UX MUSTs |
| Unlocks | RT-20b (acceptor mirror), RT-21 (throw half), RT-23, RT-24c, RT-26, RT-27 (device half) |
| Status | ☐ not started |

### D12 — Conformance claim

| | |
|---|---|
| Deliverable | The claim states **acceptor only** ([9e1](../specification/ppcp-rv.md#9-conformance)). ⚠ This repository's claim reports aggregate pass counts at the top of the document; under [9g](../specification/ppcp-rv.md#9-conformance) that is **not permissible for RV-6** while RT-20c is unrun, so the aggregate is removed for RV-6 and a named **RT-20c `unrun`** row stands in its place. RT-23, RT-26 and RT-27 are `review` rows with a named reviewer and a commit. |
| Traps | 9 |
| Status | ☐ not started |

---

## 8. Sessions and gates

Three sessions, one agent per repo. **Team L runs one step ahead by construction** — and in C1 that is unusually easy, because H9 and D10's advertising half need no bootstrap API at all.

| Session | `libppcp` (L) | PinPointStudio (H) | PinPointCapture (D) | Gate to leave the session |
|---|---|---|---|---|
| **C1 — the library, and the discovery halves** | L18, L19, L20 | **H9** (advertising — no library dependency) | **D10** (the window and its advertisement — no library dependency) | `ctest` green; **every row of [§10.4](../specification/ppcp-rv.md#104-guided-pairing) reproduces byte-for-byte**, including both counter-vectors and the interposer quadruple; RT-18, RT-20a(a), RT-24b passing in `libppcp`; PinPointStudio advertises `role: host` on macOS and PinPointCapture browses and resolves it; PinPointCapture's window is discoverable and withdraws on close (RT-22) |
| **C2 — the relay, then the two roles** | **L21 first**, then L22 | H10 | D11 | ⛔ **The relay exists and both of its own legs complete**; each application completes a guided pairing **against the relay**, and RT-20b passes for each — including its own mirror of the commitment ordering, which is [CA4](#3-decisions-this-plan-fixes)'s point: each team only ever tests its own half |
| **C3 — the pair** | fixes from interop | H11 | D12 | **RT-20c passes**: the two real implementations either side of the relay, digits differing on both, both declining, neither pairing. Both claim files carry the [9g](../specification/ppcp-rv.md#9-conformance) shape. Every RT-18…RT-27 cell is *passing*, *review* with a named reviewer, or has a named blocker |

**Cross-team sync points inside a session** (the orchestrator relays; agents never message each other):
- When L18's header lands, H and D are told the package is consumable and code against it.
- ⛔ **When L21 lands, both teams are told immediately** — it is what they develop against, and a relay built against a stale [§10.4](../specification/ppcp-rv.md#104-guided-pairing) produces exactly the false green RT-20 exists to prevent.
- **Any erratum touching a frame or a derivation is notified to both teams on landing**, never left to be discovered on a re-read. That is the process item PinPointCapture asked for and [the change-request notes](../changerequests/README.md) record.
- A specification defect is reported, not fixed in place ([ground rule 9](#1-ground-rules)).

---

## 9. Compliance tracking

[`../conformance/matrix.md`](../conformance/matrix.md) gains rows **RT-18 … RT-27**, three columns as before, the same cell vocabulary. Two notes specific to this plan:

- **`review` rows are the majority here and that is not an accident.** RT-24a, RT-25, RT-26 and RT-27 are all `review`, because each catches something that produces byte-identical handshakes — a peer violating [11.3d1](../specification/ppcp-rv.md#113-roles-and-the-connection) is conformant on the wire, and one that compares the digits itself passes every static test in the document. They join [RT-12](../specification/ppcp-rv.md#9-conformance) and [RT-17](../specification/ppcp-rv.md#9-conformance) in the set nothing external can check, and each needs a **named reviewer and a commit**, re-read whenever the code it covers is touched.
- ⛔ **RT-20c gates the aggregate, not just its own cell** ([9g](../specification/ppcp-rv.md#9-conformance)).

---

## 10. Decisions, findings and errata log

*Appended to at the end of every session. Empty until C1 opens.*

| # | Date | Item |
|---|---|---|
| 1 | 2026-08-24 | **Session C1 opened.** L18/L19/L20, H9 and D10 briefed and launched in parallel; §4 carried verbatim in all three briefs. Orchestrator note on [ground rule 9](#1-ground-rules): three agents means `-j3` each would total 9, so `libppcp` builds at **`-j2`** this session and the stated ≤ 8 holds |
| 2 | 2026-08-24 | ⛔ **Finding, PinPointStudio (H9), and it needs the protocol owner: the SRV target name is unconstrained, and on macOS it defaults to the machine name.** A live `dns-sd -L` capture reads `PPCP-11121314._ppcp._tcp.local. can be reached at Marks-Mac-mini.local.:47788`. [3.2b](../specification/ppcp-rv.md#32-instance-name) keeps a person's name out of the **instance name** and [3.3b](../specification/ppcp-rv.md#33-txt-record) out of the **TXT record**, both for the same reason — platform APIs default the service name to the device name. The SRV target is a third record carrying exactly that string, and the only SRV clause in the document ([3.7f](../specification/ppcp-rv.md#37-the-bootstrap-window)) constrains a *bootstrap* instance's **port** and says nothing about the target. §10's worked example publishes no SRV target either, so the gap is invisible from the vectors. **The privacy property 3.2b and 3.3b exist to protect is defeated one record over, by default, on the platform [3.5e](../specification/ppcp-rv.md#35-who-advertises-and-who-browses) nominates as the advertiser.** Demonstrated in a packet log, not read off the page, so it clears [ground rule 10](#1-ground-rules)'s bar. ⚠ The mitigation is not free: `DNSServiceRegister`'s `host` parameter takes an explicit target, but supplying one makes the advertiser responsible for the A/AAAA records too — proxy registration, a materially larger change. **Not fixed, not worked around, `PPCP-RV` not edited.** ✅ **CLOSED, protocol owner, 24 August 2026 — accepted, no change.** The name stays on the local network, and [E53](../specification/ppcp-core.md#errata-after-revision-9)'s deployment is a home or studio network where the same host already publishes its machine name over mDNS for everything else it runs. **No proxy registration**, and PinPointStudio does not supply a `host` to `DNSServiceRegister`. |
| 3 | 2026-08-24 | **Finding, PinPointCapture (D10): [11.9a](../specification/ppcp-rv.md#119-aborting-and-the-one-attempt-rule) and [3.7b](../specification/ppcp-rv.md#37-the-bootstrap-window) together admit a denial of service on the whole path.** 11.9a lists "a malformed frame, a closed connection" among the aborts that close the window, and 3.7b closes on "one bootstrap attempt aborted or rejected". Read so that a *connection* becomes an *attempt* on arrival, **any device on the link closes the user's window by dialling once with junk** — and [11.9b](../specification/ppcp-rv.md#119-aborting-and-the-one-attempt-rule) then forbids reopening without a further user action. D10 took the line [11.3c](../specification/ppcp-rv.md#113-roles-and-the-connection) itself draws — an attempt begins at a well-formed `bs_offer` — so anything refused before that leaves the window untouched, and asserted it in a test. **Reported, not fixed.** The reading is defensible and matches 11.3c; what was open is whether the document should say so. ✅ **CLOSED, protocol owner, 24 August 2026 — risk accepted, no clause change.** A denial of service against this path needs an attacker already on the home or studio network, in the seconds a window is open, to achieve one re-tap. ⚠ **D10's stricter behaviour stands and is not to be relaxed** — an attempt begins at a well-formed `bs_offer`, anything refused before that leaves the window untouched, and `BootstrapAdvertiserTests` asserts it. It is already written, it costs nothing to keep, and it is the reading 11.3c gives. |
| 4 | 2026-08-24 | **Cross-team defect, D→L, blocking, one line.** `libppcp/include/ppcp/bootstrap.h` (`ad54ae0`, L19) uses `PPCP_FRAME_HEADER_BYTES` at line 66 but includes only `ppcp/rv.h`. It compiles inside `libppcp` only because `src/ppcp_bs_frame.c` includes `ppcp/frame.h` two lines later; **PinPointCapture consumes it as a Clang module, which is how every Swift consumer builds it, and a module has no such luck**. Reproduced by the orchestrator: `clang -fsyntax-only -I include -x c - <<< '#include "ppcp/bootstrap.h"'`. It broke `make test-core`, `make build` and `make test-app` in PinPointCapture and is what holds RT-22's withdrawal half unrun. Relayed to team L; fix is `#include "ppcp/frame.h"`. ✅ **The seam worked exactly as [CA1](#3-decisions-this-plan-fixes) and [CA2](#3-decisions-this-plan-fixes) intend — a header defect surfaced from the far side of a licence boundary on the first day, by a team that could not and did not edit it.** |
| 5 | 2026-08-24 | **[CA6](#3-decisions-this-plan-fixes)'s wording covers half of what it decides** (PinPointCapture, D10). It names the **write** path as separate from the PPCP frame writer, but `ppcp_frame_header_parse` rejects channel 255 through `ppcp_channel_validate()` as well, so the **read** path is equally blocked and needs its own path too. The behaviour is not in question — [trap 1](#4-the-traps) says that rejection stays and it does — but a reader following CA6 literally implements one direction. **Plan defect, recorded; CA6 to be reworded to name both directions.** |
| 6 | 2026-08-24 | ✅ **No twenty-seventh finding, and it was declined explicitly.** Team L reports: *"The bar is a demonstration and I do not have one; §11 implemented cleanly and §10.4 reproduced first time."* [Ground rule 10](#1-ground-rules) working as intended — five review passes, and the sixth pass is code. |
| 7 | 2026-08-24 | **Both ends observed on the wire by the orchestrator, not reported by the agent that wrote them.** With `8ed4259` holding a registration and PinPointCapture's window open at the same time, `dns-sd -L` reads `PPCP-11121314 … :47788  txtvers=1 pv=1.0 role=host rn=030405060708090a rid=9b95b9279f73bb93` and `PPCP-86A46030 … :58694  txtvers=1 pv=1.0 role=capture bs=1`. Five keys and no sixth on the host ([3.3a](../specification/ppcp-rv.md#33-txt-record)); `bs` present with **no `rn` and no `rid`** on the window, on a **distinct port** ([3.3f](../specification/ppcp-rv.md#33-txt-record), [3.7f](../specification/ppcp-rv.md#37-the-bootstrap-window)). `role` is in 3.3f's set, so the window's record is conformant. |
| 8 | 2026-08-24 | ⚠ **`docs/conformance/claim-libppcp.md` is stale and L22 must do two things, not one.** It is generated, still reads *"revision 8 + errata E3, E4"*, and claims RV only for §4/§5.1/§3.4/§5.3 — so it makes **no RV-6 claim and satisfies [9g](../specification/ppcp-rv.md#9-conformance) by silence rather than by a named row**. [Ground rule 11](#1-ground-rules) binds claims *"from the first commit"*, and there is now RV-6 code with no claim-side statement at all. **L22 regenerates it AND adds the explicit `RT-20c unrun` row.** |

---

## 11. How an orchestration session is run

As the main plan's §10, with three additions.

1. Read this file, [`matrix.md`](../conformance/matrix.md), and **[§4, the traps](#4-the-traps)**. Build the three agent briefs from the session row in [§8](#8-sessions-and-gates): the work package text verbatim, the ground rules of [§1](#1-ground-rules) **including the five CR-01 ones, and rule 9's job caps verbatim**, the decisions of [§3](#3-decisions-this-plan-fixes), **§4 in full**, and — for H and D — the path of `libppcp`'s public headers and that repo's own `CLAUDE.md` and memory index, with its build recipe pasted verbatim and the job cap.
2. ⛔ **Every brief carries §4 verbatim.** They are not background reading. Eight of the nine produce an implementation that passes every test an agent can run, and the ninth is the instruction not to claim a pass.
3. Launch the agents with working directories set to their own repositories, in parallel where the session row's packages are independent. In C2, **L21 lands before H10 and D11 start** — that is [CA3](#3-decisions-this-plan-fixes) and it is the ordering decision this plan exists to protect from slipping.
4. Each brief ends with the same reporting contract: *what landed (files, commits), which matrix rows moved and to what state with the reproducing command, what was found wrong in the specification or in another team's API, what is unfinished.*
5. When all three report: verify the gate, update the status boxes in [§5](#5-work-packages--libppcp-team-l)–[§7](#7-work-packages--pinpointcapture-team-d), update the matrix, append to [§10](#10-decisions-findings-and-errata-log), and commit each repo to `main`.
6. Record in memory where the session stopped.
