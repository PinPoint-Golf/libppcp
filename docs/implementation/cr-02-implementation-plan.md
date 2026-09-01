# CR-02 implementation plan — device status, actuator control, and statistics

**The tracker for bringing `PPCP-CORE` §5.19–5.21 and `PPCP-MSG` §5.5–5.6, §12 into `libppcp`, PinPointStudio and PinPointCapture.**

| | |
|---|---|
| Status | **Open.** Session C1 running |
| Date | 1 September 2026 |
| Against | `PPCP-CORE` revision 9 as amended by **errata E58–E65** — [CR-02 closed](../changerequests/README.md), two review rounds, no open specification items |
| Companion plans | [`implementation-plan.md`](implementation-plan.md) — the main programme, **complete and closed**. [`cr-01-implementation-plan.md`](cr-01-implementation-plan.md) — RV-6, **closed**. This is a third document and amends neither; it continues the L/H/D numbering from **L22, H11 and D12** so identifiers stay unique across all three |
| Companion record | [`../conformance/matrix.md`](../conformance/matrix.md) — row **CT-I39** is what this plan fills in |
| Change request | [`../changerequests/CR-02-device-status-and-control.md`](../changerequests/CR-02-device-status-and-control.md), its [disposition](../changerequests/CR-02-disposition.md), two review responses, six review files |

---

## 0. What this plan delivers, and what it does not

**Delivers.** The three things CR-02 was granted, and the two operator-facing features that motivated it:

- `libppcp` — the `Actuate` profile, the `Actuator` entity and `Peer.actuators`, the `actuator_command` / `actuator_command_ack` / `actuator_state` exchange, `DeviceStatus` / `device_status`, `BufferMargin` / `buffer_status`, `Session.opened_at`, and `CT-I39`.
- PinPointStudio — **a PinPointCapture statistics tab in the resource monitor**, and **a torch control in the toolbar's Cameras pill** for a PPCP camera.
- PinPointCapture — the torch declared as an `Actuator` and commanded from the host, plus per-Source `device_status` and per-Stream `buffer_status` emission.

**Does not deliver.** A retained Capture ledger in PinPointStudio, and therefore not the captures-committed or drop-total statistics CR-02 §3c named and the disposition declined as derivable. §2c's ⚠ correction records that this is real work in PinPointStudio, not a cheap read; it is deliberately out of scope here. Nor a `level`-controlled torch — the phone declares `control: on_off`, and `control` is an open registry precisely so a level actuator needs no protocol change later. Nor `device_status` **emission** from PinPointStudio for its own USB cameras: PinPointStudio consumes only, because nothing in a session consumes what it would send.

**What "done" looks like.** `CT-I39` passes in `libppcp` with a reproducible command; an operator toggles a phone's torch from the PinPointStudio toolbar and the control reflects the **ack**, not the click; and the resource monitor's PinPointCapture tab shows live per-Source availability and ring-buffer margin — a quantity PinPointStudio has **zero** visibility into today.

---

## 1. Ground rules

Unchanged from [`implementation-plan.md` §1](implementation-plan.md). Restated only where CR-02 sharpens one.

1. **Three repositories, three licences, no code crosses between them.** `libppcp` is the only shared artefact.
2. **Each team works only in its own repository.** It may read the other repos' *documentation* and `libppcp`'s *public headers and specification*. Agents never message each other; the orchestrator relays.
3. **The specification is the authority and changes first.** ⭐ CR-02 exercised this on day one: the granted text did not pass the [`PPCP-CONF` 5b2](../specification/ppcp-conformance.md#5-interoperability) audit E18 installed, and **L23 records erratum E65 before a line of code is written**.
4. **Conformance is evidence, not a claim.** Every package names its `CT-*` row. "Implemented" and "passing" are different states and the matrix records both.
5. **Commit to `main` in every repo, no branches, no PRs.**
6. ⛔ **Builds are bounded.** An explicit job count on **every** build command; the total across concurrent agents **at or below 8**; no build loops. **Ninja with no `-j` is itself unbounded** (cores + 2 = 12 on this machine), so a Ninja build needs its `-j` *more* than a Makefiles one, not less — and `jom -j` is the same trap. Three agents running concurrently use `-j2` each, or the orchestrator serialises so only one builds at a time. A bare `-j` crashed this machine on 22 August 2026 and cost forty minutes.
7. **The library owns no I/O.** Sans-I/O; the embedding supplies bytes, timestamps and storage.

---

## 2. Profile claims after this plan

| Implementation | Profiles | Change |
|---|---|---|
| `libppcp` reference | all **nine** — Core, Capture, Detect, Mint, Arbitrate, Live, Markup, Offline, **Actuate** | +Actuate |
| PinPointStudio | Core + Capture + Detect + Arbitrate + Live + Offline + Markup + **Actuate** | +Actuate (`CORE` §2.2.3, "commands one") |
| PinPointCapture | Core + Capture + Detect + Mint + Live + Offline + Markup + **Actuate** | +Actuate (`CORE` §2.2.3, "owns a torch") |

⚠ **The claim files and `matrix.md` are records of achieved state, not intentions.** They are updated in **L29/H16/D17**, when a command has produced the result — never in L23 alongside the specification.

---

## 3. Decisions this plan fixes

| # | Decision | Why |
|---|---|---|
| **CB1** | The phone's torch declares **`control: on_off`**. | CR-02 §3b left on/off-vs-level open as a product decision. A toggle is what the operator needs, and `Actuator.control` is an open registry so `level` is additive later. It also sidesteps the iOS nuance in CB4. |
| **CB2** | PinPointStudio **consumes only** — it originates `actuator_command` and handles the three inbound events, and does **not** emit `device_status` for its own Sources. | Nothing in a session consumes it. 5.5c permits emission by any Source owner; it does not require it. |
| **CB3** | The statistics tab shows the **granted wire fields plus data PinPointStudio already receives** — `buffer_status`, `device_status`, session duration, and `heartbeat_ack`'s battery/thermal/storage, which are parsed today and displayed nowhere. | The declined half of CR-02 §3c needs a retained Capture ledger (§2c's ⚠ correction). Out of scope, stated rather than silently dropped. |
| **CB4** | `actuator_state` is a **first-class expected input** on the host, not an edge case. | iOS's `setTorchModeOn(level:)` **throws** rather than clamping, so 12.1c's clamp-inside-the-ack case does not occur on the platform PinPointCapture ships on. The genuine achieved-differs-from-requested case arrives asynchronously as a thermal cutoff — `actuator_state` is the only channel that carries a torch's real drift there (PinPointCapture review round 1, Q5). |
| **CB5** | Any `device_status` consumption keys on the wire's **`source_id`**, never on `serialNumber` or a peer-id-derived `camIdent`. | PinPointStudio sets a PPCP camera's `serialNumber` to the owning **peer** id, so both of a phone's cameras already collapse onto one identity for folded stats — a known open defect. Keying new UI the same way repeats it in a second place (PinPointStudio review round 1, finding R-3). |
| **CB6** | The torch control lives in the Cameras pill's popup (`PpCameraPanel.qml`), not as a new toolbar pill. | The toolbar deliberately holds only *aggregates* over phones — *"a phone's only reason to be on this bar is that it is carrying a camera"* — and the panel's rows already carry `isPpcp`. |
| **CB7** | I39 is enforced **by shape**, not by a runtime check: separate constructors for an on/off command and a level command. | House style. I29 needs no check because the setter only accepts an `Estimate`; 5.15a needs none because there is no `ppcp_readiness_make(const char *state)`. `ppcp_readiness_settled` / `_not_settled` is the pattern to copy. |

---

## 4. The traps

Each is a mistake one of these codebases has already made, or a review explicitly warned about. A package that trips one passes its own tests and is still wrong.

1. ⛔ **A reading is not a structural change.** In PinPointStudio, emitting `phonesChanged()` when a *reading* moves rebuilds every delegate — it destroyed the alias field an operator was typing into, once. Every new CR-02 per-phone reading emits **`phoneHealthChanged()`**.
2. ⛔ **Key on `source_id`** — CB5.
3. ⛔ **Queued is not achieved.** `ppcp_peer_*` returns success when a message is *queued*, before a byte leaves. This has been fixed twice already, for `arm` and for `stream_open`, whose comment reads *"we had the comment without the code."* **The torch control reflects the ack, never the click.**
4. ⛔ **Never read the C union by value from Swift.** `msg.pointee.body.x` is a get-modify-set of a 48 KB struct — enough to `SIGBUS` a test (F-D3-1). Use the existing pointer helper.
5. ⛔ **Harvest with the imported-aware entry point** (E28 / F-S5-3) — imported-session frames must not reach live handling.
6. ⛔ **`PpcpDeclaration` owns every buffer manually.** Give the actuators buffer a value before any `throw`; release only in `deinit`. The `catch` deliberately releases nothing — a double-free was fixed by that.
7. ⚠ **`discarded_since_open` has a different epoch from the existing counter.** PinPointCapture's `RingStats.fragmentsEvicted` resets per *arm*; the wire field is per *Stream open*. And 5.21a counts only what never became part of a Capture — `fragmentsEvicted` qualifies, encoder-busy frame drops do **not**, because they already flow to `achieved_summary` and would be counted twice.
8. ⚠ **Do not put the histogram on the wire.** Review Q4's verdict: `gapBuckets`/`largestGaps` are receiver-side aggregation, reconstructible from repeated `buffer_status`. Only the four `BufferMargin` fields cross.
9. ⚠ **`Session.opened_at` is mandatory**, so both constructors gain a parameter and roughly twenty call sites break across the fixtures tool and six tests. Its own commit.
10. ⚠ **`tests/no_thresholds.cmake` (CT-I14)** greps for quality thresholds. Validating a level within `[0.0, 1.0]` is domain validation and is safe; clamping to a device step is not the library's business — 12.1c puts the achieved value on the wire instead.
11. ⛔ **Keep PPCP headers out of PinPointStudio's `resource_monitor_controller.cpp`.** `HAVE_PPCP_TRANSPORT` is defined for some translation units and not others; the controller reaches phones through a duck-typed property read. Follow that seam.

---

## 5. Work packages — `libppcp`, team L

| # | Package | Unlocks |
|---|---|---|
| **L23** | ⭐ **The specification, alone in its commit.** Erratum **E65**: `PPCP-MSG` 5.5a reworded to name the message it requires; §11's `actuator_state` row **opt** → `12.2a`; 12.2a's incidental message names written out in prose; §11's tally fifteen → seventeen. The **E65 row** in `PPCP-CORE`'s errata annex. `PPCP-CONF` §3's header count, stale since I39. This plan file. **No code.** | `L16-profile-boundary`: 8 failures → 5 |
| **L24** | `PPCP_PROFILE_ACTUATE`; the `ppcp_actuator` entity; `ppcp_peer_desc.actuators` + `ppcp_peer_desc_set_actuators()`; the actuators list codec inside `declare`. 5.19b (Source and Actuator kinds disjoint) in `ppcp_peer_desc_validate`. | Everything below |
| **L25** | The five catalogue rows: type enum, `PPCP_MSG_COUNT` 45 → 50, five bodies, five union arms, five `msg_table` rows, and `test_ct_s6`'s count assertion. | ⭐ `L16-profile-boundary` **green** |
| **L26** | `DeviceStatus` and `BufferMargin` entities beside `Readiness`, and the `device_status` / `buffer_status` Events. One new sub-codec pair for `last_discard {since, duration}`. 5.5b as a `validate` clause. | `CT-S6` |
| **L27** | `Session.opened_at` — **its own commit**, because it is the only change that breaks existing call sites. | — |
| **L28** | The §12 trio: `peer_on_actuator_command` modelled on `peer_on_stream_open`, the 12a host-only check against the remote role, the `responder_profile` row, the event mappings, and the four `ppcp_peer_*` senders. Separate on/off and level constructors (CB7). | — |
| **L29** | `test_ct_i39.c` — **six** static assertions, not two, because E63 repeats both against `actuator_command_ack.state` and `actuator_state.state`. The paired half needs `actuators` parsing in `ppcp-sim`'s declaration reader, a scenario, and a `ppcp_pair_row()` entry. Then `matrix.md`'s CT-I39 row and profile line, `claim-libppcp.md`, and the `ppcp-conform --profiles` invocations. | ⭐ **CT-I39** |

## 6. Work packages — PinPointStudio, team H

| # | Package | Unlocks |
|---|---|---|
| **H12** | `PpcpLiveSession` parses all four inbound messages into structs beside `PeerHealth`/`PeerReadiness`, with callbacks in the established style; `setActuator()` beside `arm()`, carrying `arm()`'s own queued-is-not-achieved discipline. | — |
| **H13** | `PpcpHostService`: Qt adapters emitting **`phoneHealthChanged()`**, an `actuators` list on each `phones()` row, a `Q_INVOKABLE` actuator setter modelled on `armAll()`, and `ppcpStats()` extended with per-phone device status, buffer margin and session duration. `actuate` added to the declared profile set. | — |
| **H14** | ⭐ **The torch control** — a toggle on `isPpcp` rows of the Cameras pill's panel, cross-referenced to its phone the way the existing camera-count binding already does, guarded as everything PPCP on that bar already is. Lit state comes from the ack and from `actuator_state`. | Goal (b) |
| **H15** | ⭐ **The resource-monitor tab** — a fourth tab in the existing hand-rolled strip, gated the way the profiler tabs are, fed from `ppcpStats()`: a large already-built dataset whose only consumer today is the automated harness. | Goal (a) |
| **H17** | **`Session.opened_at` downstream.** L27 made the parameter mandatory and broke this repo: `src/Ppcp/ppcp_live_session.cpp:121` plus four call sites in `src/Ppcp/tests/ppcp_bundle_import_test.cpp`. The host **is** opening the session at `:121`, so "now" in `timebase_ref` is genuinely `opened_at` and not a fabricated instant. | build |
| **H16** | `ppcp_live_session_test`, `ppcp_host_service_test` (needs a `…ForTest` seam — the suite links stubs and never accepts a link), and the claim file's **three** profile statements, which must move together or drift. | claim |

## 7. Work packages — PinPointCapture, team D

| # | Package | Unlocks |
|---|---|---|
| **D13** | `Peer.actuators` in the declaration: a neutral actuator input type, a manually-owned storage buffer beside the timebase and source buffers, the setter call, and a view read **back out of the C structs** as `sources` already is. `actuate` into the device profile set. Not via `SourcePlan` — 5.19b. | — |
| **D14** | ⭐ **The torch on the platform seam** — Core-side capability/state/request types (no `AVCaptureDevice` may cross the port), a port method (*"adding a method here is a decision"*), the `lockForConfiguration` implementation, achieved-state readback, and enumeration at declaration time. **No libppcp dependency**, so it runs in C1. | Goal (b) |
| **D15** | Answer the command and emit the event: the four `DevicePeer` senders, the event translation, and a delegate verdict modelled on the existing stream-request verdict — **answering is a MUST** (1c). The existing ingest-policy gate already satisfies 12d before application code sees anything. | `CT-I39` paired half |
| **D16** | The two statistics emitters: `last_discard` from the eviction site where the timestamps are already to hand, a per-Stream `discarded_since_open` epoch (trap 7), `retained_from` and `retention_target` from values already public, and change-detected emission on the existing 1 Hz health tick — **on change, not per tick**. `in_use` and `disconnected` are genuinely new platform reads. | Goal (a) |
| **D18** | **`Session.opened_at` downstream.** Broke `Live/SessionResume.swift:89`, `:101`, `Ppcp/DevicePeer.swift:659` and `Tests/CaptureCoreTests/SessionBundleTests.swift:350`. ⚠ **Not mechanical**: `PpcpSessionRecord` carries `epochWallUtcNs`/`epochAtNs` but **no `openedAt`**, so the record gains a field and `SessionStore` must persist it. A resume carries the **original** instant, never the resume moment — inventing one is the fabrication 5.10h exists to prevent. | build |
| **D17** | Declaration, event-translation, loopback and ring-buffer tests; the claim file and the Makefile's profile list, which the Makefile warns must not drift. | claim |

---

## 8. Sessions and gates

Two sessions, one agent per repo. **Team L runs one step ahead**, but both applications have real work with no library dependency, so neither idles in C1.

| Session | L | H | D | Gate to leave |
|---|---|---|---|---|
| **C1** | **L23 first and alone**, then L24–L28 | H15 | D14 | `ctest --preset dev` green; ⛔ **`L16-profile-boundary` green**; the resource-monitor tab renders existing per-phone stats; the torch toggles on a real iPhone |
| **C2** | L29 | H12, H13, H14, H16 | D13, D15, D16, D17 | **CT-I39 passing** with a reproducible command; a torch toggled **from the PinPointStudio toolbar on a real iPhone**, reflecting the ack; `device_status` and `buffer_status` live in the tab; every claim file and `matrix.md` updated |

**Cross-team sync points** (the orchestrator relays; agents never message each other):
- ⭐ **L23 lands before anything else and both teams are told immediately.** A specification that changes under a half-built implementation is exactly the false green these ground rules exist to prevent.
- When **L24**'s header lands, H and D are told the package is consumable.
- When **L25** lands, both teams are told — it is what message handling binds to.
- Any further erratum is notified to both teams on landing, never left to be discovered on a re-read.
- A specification defect is **reported, not fixed in place**. L23 is what that rule looks like when it fires.

---

## 9. Compliance tracking

One new row, `CT-I39`, across three columns. It is `static` in `PPCP-CONF` §3 but its own text also requires two **paired** assertions — an undeclared `actuator_id` answered `error`/`not_declared` (12.1d), and a command from a non-host peer refused rather than acted on (12a). Neither is reachable from a single-process static test, so the row is not `pass` in any column until the simulator can declare actuators.

⚠ **A `pass` in `libppcp`'s column of a paired row is two `libppcp` engines through a byte buffer**, which `CONF` §2c warns is not an interoperability demonstration. The same caveat as every other paired row applies here and is not restated per row.

---

## 10. Decisions, findings and errata log

| # | Date | Item |
|---|---|---|
| 1 | 1 Sep 2026 | **Erratum E65.** CR-02's granted text failed the `CONF` 5b1/5b2 audit E18 installed — `L16-profile-boundary` was red on three findings from the moment E58–E60 landed, and nobody ran it. Three text-only fixes; no obligation added or removed. Recorded before any code, per ground rule 3. |
| 2 | 1 Sep 2026 | **`PPCP-CONF` §3's header count was stale**, and had been since I39 was added: it said thirty-eight invariants while `PPCP-CORE` §11 said thirty-nine. Found by the same sweep. Folded into E65. |
| 9 | 1 Sep 2026 | ⛔ **The torch fired on commands the engine had already refused — a trust-boundary defect, found by D19 while wiring L30.** `ppcp_event.status` was never surfaced in PinPointCapture's Swift stack, so the delegate ran for **every** `PPCP_EVENT_ACTUATOR_COMMAND`, including 12.1d, 12.1a and **12a** refusals the engine had already answered. **A non-host peer's command would have physically lit the torch on its way to being told it was not allowed** — and 12a is a trust boundary, since `CORE` 12d makes an actuator command session control. The refusal was enforced on the wire while the hardware acted anyway. After L30 it would also have put a second, contradicting ack against one Request (1a). Fixed by threading the status through as `engineAnswered`, with an end-to-end test that needs a hand-built frame — no conformant peer can originate a non-host command, so the case is only reachable via `ppcp_peer_send`. **This is the second defect the 12.1c fix uncovered; neither was visible while the engine answered for the embedding.** |
| 8 | 1 Sep 2026 | ⛔ **The on-device gate items are OPEN and were never met.** C1's *"the torch toggles on a real iPhone"* and C2's *"a torch toggled from the PinPointStudio toolbar on a real iPhone"* have not been run. D14 was simulator-only; team D was build-capped and spent both on `make test-app`; team H had no phone reachable. The device left the site on 1 Sep. **The simulator has no camera, so no substitute run stands in for this** — an offscreen or simulated pass would prove the code path, not the light. To close: `make deploy`, launch with `-ppcpScreen D14`, warm the camera, toggle, and check the reported `state.on` against the actual light. Everything else in C1 and C2 is provable on the Mac and is not blocked by this. |
| 7 | 1 Sep 2026 | ⛔ **12.1c was unsatisfiable through the library, and both application teams hit it independently.** `peer_on_actuator_command` auto-acked `applied` with `state = b->setting` — a pure echo of the request — and queued it *before* `PPCP_EVENT_ACTUATOR_COMMAND` reached the embedding, so no hardware had been touched when the ack was written. **12.1c is a MUST**: `state` reports what the Actuator is *actually* doing, **not an echo**. The disposition's §6 ask 5 asked both teams to confirm achieved-state acks were buildable *on their platform*; both did, via `isTorchActive` — **nobody checked it was buildable through the library**, and it was not. This is trap 3 ("queued is not achieved") reproduced at the protocol layer: a host would light its torch control off its own request every time. Team D declined to fake a readback and recorded the behaviour as a test instead. Fixed in **L30**: the library keeps the refusals it can decide alone (12.1d `not_declared`, 12.1a `malformed`, 12a non-host) and hands a well-formed command to the embedding, which answers with `ppcp_peer_actuator_command_ack()` carrying the achieved value. **D19** then uses it. No erratum — the specification was right and the implementation was wrong. |
| 6 | 1 Sep 2026 | ⛔ **Erratum E66 — `declare` never said where `actuators` goes.** E58 put the field on the `Peer` entity and left `PPCP-MSG` 3.3's schema block untouched, so nesting it in the `peer` head and hoisting it beside `sources` were both defensible readings. **Two conformant peers would each declare Actuators the other could not see, nothing malformed, no error at either end** — E28's shape exactly. Pinned as a top-level sibling of `sources`, omitted where a peer owns none, so no fixture moves. Found by team L having to choose; reported, not fixed in place, and the erratum written by the orchestrator before H and D coded against it. |
| 4 | 1 Sep 2026 | **L27's mandatory `Session.opened_at` broke both applications**, not only `libppcp`'s own ~20 call sites. Five sites in PinPointStudio, four in PinPointCapture. Team D correctly **stopped rather than inventing instants** and reported it. Assigned as **H17** and **D18**; each repo fixes its own, per ground rule 1. Recorded because the plan sized L27's blast radius as library-only, and it was not. |
| 5 | 1 Sep 2026 | ⛔ **PinPointCapture's 1 Hz health tick runs only while armed** (`AppModel.startHealthPolling()`), and D16 emits `device_status` on it. Left as is, `device_status` could never fire before `arm` — **which rebuilds the exact gap CR-02 was granted to close**: §4a named *"nothing before `arm`"* as the residual gap, and 5.20b turns on `DeviceStatus` being reachable earlier than `Readiness`. **Widening the tick's lifetime to start at `warmUp` is therefore a requirement of D16, not an optional lifecycle change.** Found by team D while building D14. |
| 3 | 1 Sep 2026 | **`Session.opened_at` has no wire carrier.** E61 added the entity field, but `session_open`'s body (`MSG` §4.1) was not given one, so a non-host peer still cannot learn the session's start instant — the fabrication 5.10h says it exists to prevent. **Not blocking this plan**: PinPointStudio is the host and opens the session, so it knows the value locally, which is all the statistics tab needs. Recorded so it is not rediscovered as a defect. A CR-03 if it ever matters. |
