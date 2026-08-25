# `ppcp/1.0` — freeze readiness

**Whether the protocol can be frozen, and what the freeze is conditional on.**

| | |
|---|---|
| Written | 23 August 2026, work package L17 wave 2 |
| Against | `PPCP-CORE` revision 9 + errata E1–E29; `PPCP-MSG`, `PPCP-ENC`, `PPCP-CONF` 1.0; `PPCP-RV` revision 8 + errata |
| Evidence | `libppcp` `4d0e04a` ([`claim-libppcp.md`](claim-libppcp.md), 49/49) · PinPointStudio `5f9d53c` (`docs/ppcp-conformance.md`, 23/23) · PinPointCapture `b83fdc7` (`docs/ppcp-conformance.md`, 174 core / 29 app) |
| Companion | [`matrix.md`](matrix.md) — the cell-by-cell record this report reads |
| Recommendation | **Freeze the documents. Do not declare `ppcp/1.0` stable yet.** See [§5](#5-recommendation) for the exact conditions. ⚠ *Updated 25 August 2026:* **both human-review conditions (3, RT-12 and 4, RT-17) are now met** as `maintainer-accepted` — an owner's risk acceptance, **not** an expert reading. **Conditions 1, 2 and 5 remain and all three need work rather than a signature**: a Capture carrying bytes between the two applications, a camera declaration meeting a foreign one on hardware, and the matrix's 48 `—` cells reclassified |

---

## 1. Specification status

`ppcp/1.0` is revision 9 as approved on 22 August 2026, plus **twenty-nine errata**, E1–E29, all normative. Every one was produced by building the protocol rather than by reviewing it: five review rounds and a requirements audit preceded approval and found none of them.

### Where the errata live

| Document | What its change history now carries |
|---|---|
| [`PPCP-CORE`](../specification/ppcp-core.md) | **The authoritative errata table**, "Errata after revision 9" — all twenty-nine, each with its clause, what changed, and the finding and session that produced it. Annex D still carries the pre-approval draft history. Twelve errata amend `CORE` itself (E10–E15, E29 and the invariant-text corrections to I8, I13 and I16) |
| [`PPCP-MSG`](../specification/ppcp-messages.md) | Erratum notes at 3.0 (E1), 6.1 (E2), 4.1a1 and 9.1b (E28), 8.1i1 (E16), 5.1e (E17), 1c and §11's **Required by** column (E18), 8.3h (E7) |
| [`PPCP-ENC`](../specification/ppcp-encoding.md) | §2.1 (E1), 5a1 (E6), §5.1 re-emitted (E5), 6g/6h (E7), 7d1 and 7h (E8, E9) |
| [`PPCP-CONF`](../specification/ppcp-conformance.md) | §3's preamble, CT-S1, CT-S4(1) and the new 5a1 (E19) |
| [`PPCP-RV`](../specification/ppcp-rv.md) | A new **"Errata after revision 8"** table in Annex C listing the ten that touch it (E3, E4, E20–E27), with erratum notes at 7.3 (E3), 2c1 (E4), 4.3a1, 5.3a1, 5.3c1, 3.5d, 4.4a2, 3.3d–e, 7.4h and 3.4d1–2 |
| [`README.md`](../specification/README.md) | Status line names E1–E29; the errata section groups all twenty-nine by kind |

Four of the twenty-nine are **decisions rather than clarifications** — E24–E27, taken by L17 because the implementations could not proceed without an answer. Each is marked reversible in its own clause and in the errata table.

### One defect in the specification set that is not an erratum

**Clause identifiers are not unique across documents, and nothing enforces that they are.** `ENC` §5.1 gained clauses `5.1a` and `5.1b` under erratum E5; `CORE` §5.1 and `MSG` §5.1 already had clauses of those names. The collision is visible in the S5 adjacent-MUST sweep, whose `--changed 5.1a,5.1b` emitted three unrelated sections.

Nothing has gone wrong yet: every citation in the set is qualified by document (`` `ENC` 5.1b ``, `` `CORE` 5.1a ``), the sweep generator over-emits rather than under-emits, and over-emission is the safe direction for a tool whose purpose is to make a human read more. But a cross-document citation that drops the qualifier now resolves to two clauses, and the freeze is the moment to decide whether identifiers become globally unique. **Recorded, not fixed** — renumbering after freeze is an erratum against every document at once, and doing it now would invalidate every citation in three repositories.

---

## 2. The matrix

[`matrix.md`](matrix.md) carries 63 rows across three columns — **189 cells**, updated in this wave from the two applications' own S5 claim files.

| State | Cells | Meaning |
|---|---|---|
| `pass` | **93** | Asserted by a named, re-runnable command |
| `—` | **48** | **Unmeasured.** See below — this is the largest single obstacle to the freeze, and it is mostly bookkeeping |
| `impl` | **23** | Code exists, the row does not yet pass |
| `n/a` | **16** | Profile not declared **and** the negative test ran and passed (`CONF` 1d) |
| `review` | **6** | RV review method; reviewer not yet named |
| `rig` | **3** | Needs the LED timecode rig |

### 2.1 The 48 `—` cells, and why they are not all the same thing

The plan's completion test is that *every row is `pass`, `n/a` by profile, or `blocked: rig`, in all three columns*. Forty-eight cells meet none of those. **They are not forty-eight pieces of missing work.** The matrix's vocabulary has no term for *"this row is not this implementation's to run"*, so a host column carries `—` for CT-I1 (an encoder refusing a `tb`-less `Instant`, which the host's engine is `libppcp`'s) exactly as it would for work nobody has started.

Most of the forty-eight are of the first kind. That is a defect in the **record**, not in the implementations, and it is cheap to fix: the matrix needs a sixth cell state — `n/a: not this peer's row` — distinguished from `n/a` (profile not declared, negative test passed). **Until that is done the matrix cannot answer the question the freeze asks of it**, and no amount of implementation work will change that.

### 2.2 Every cell that is not `pass` or `n/a`, by blocker class

**Rig** — needs the LED timecode rig, and cannot be closed by software.

| Cell | State | What it needs |
|---|---|---|
| CT-S2, all three columns | `rig` | `nominal_frame_start`'s offset measured per device model, and the residual shown independent of exposure duration |
| — | — | The two timing defaults of `CORE` Annex B8, `coincidence_window_ns` and `issue_hold_ns`, measured **per nominator class**; and B10, `exposure_provenance: sampled` accuracy |
| PinPointCapture, a measured time of flight | deferred | `tof_correction` is currently a user-entered distance with a sigma reflecting that it is an estimate |

**Phone** — needs capture hardware. Every one of these is PinPointCapture's, and they share one root: **the simulator enumerates no camera Source and the ring buffer is not wired to the capture path**, so no Capture the device announces carries bytes.

| Cell | State | What it needs |
|---|---|---|
| CT-S7 (4) | `blocked: a phone` | A converted instant against a peer declaring a **non-zero measured** offset — the assertion that catches a hardcoded zero |
| CT-S1 (1–5) | `impl (6 pass)` | The conversion against a peer declaring a different convention; assertion 6 (the scalar form) passes today |
| CT-I17 | `impl` | Same conversion, same blocker |
| CT-I19 consumer half (CT-S3) | `pass (own half)` | A camera declaration to consume |
| CT-I30's third assertion | `pass (own half)` | `capture_update` carrying `achieved_frames` only for `transfer: failed` |
| CT-I36a under induced contention | `pass (own half)` | A phone **and** a host |
| IOP-2's other half | row passes | A phone's camera declaration meeting a foreign one — `declared_camera: false` today |
| ~~RT-15's Keychain half~~ | *closed by [E56](../specification/ppcp-core.md#errata-after-revision-9)* | The gap was `kSecAttrAccessibleWhenUnlockedThisDeviceOnly` behaving differently on a simulator, with the property that mattered unobservable from a test. **PinPointCapture no longer uses the Keychain**, and a file-backed store **is** testable — so this stops being a hardware-blocked row and becomes ordinary test coverage |
| `NEHotspotConfiguration` | deferred | A phone **and an App ID capability** — an account decision, not only hardware |
| The microphone path end to end | deferred | A phone |
| `RingBufferRecorder`'s segment delivery | deferred | A phone, **and it is not wired**: `extractClip` answers `absent`/`outside_buffer` on every device today. This is unfinished product work, not merely untested work |

**Second implementation** — `CONF` 5c: *"at least one pairing uses an implementation not written by the reference team."*

| Cell | State | What it needs |
|---|---|---|
| `CONF` 5c | open | A third party. **Every pairing in this programme has `libppcp` at both ends**, including the real PinPointStudio-to-PinPointCapture pair: the foreignness is in the declaration, never in the implementation, so a defect shared by both ends is still invisible |
| PinPointStudio CT-I35 | `impl` | 8.2k/8.2l against a device that **mints**. `ppcp-sim` supplies it; a second real device would supply it better |
| PinPointCapture CT-I32 silent-host half, CT-S4 (7) live half | `impl` | A live host link, which wave 2 now provides — these are re-runnable rather than blocked |

**Human review** — `RV` §9's `review` method, which exists because the property is not observable on the wire.

| Cell | State | What it needs |
|---|---|---|
| ~~RT-12, PinPointStudio and PinPointCapture~~ | ✅ `maintainer-accepted` | **Discharged 25 August 2026.** `RV` §9 says plainly that RT-12 — CSPRNG width and erasure — is *"the requirement on which the whole model rests and the one no test can catch"*, and [E56](../specification/ppcp-core.md#errata-after-revision-9) dropped protected storage from it (`RV` 7.2c is now a SHOULD). The maintainer read both applications' generator and erasure paths. ⛔ Recorded as `maintainer-accepted` and **not** as `pass`: an author cannot discharge this row and the maintainer is not a cryptographic reviewer, so what is recorded is an owner accepting a risk, which is a weaker and more honest claim |
| ~~RT-17, both applications~~ | ✅ `maintainer-accepted` | **Discharged 25 August 2026.** A re-read whenever a platform SDK is updated remains the standing hygiene note — for PinPointCapture because a platform that gains TLS 1.3 external PSK silently restores forward secrecy for an implementation that asks rather than assumes, and for PinPointStudio because its candidate list is a constant. Neither is an obligation; the maintainer judged the exposure proportionate to the data at risk |
| RT-13, PinPointCapture | `review` | A named reviewer for the network-join consent path |
| RT-16, PinPointCapture | `review` | PinPointStudio raised this one to `pass` by making 7.4f's refusal observable; PinPointCapture has not |

**Product decision** — nobody is blocked; somebody has to choose.

| Item | Question |
|---|---|
| Host microphone distance | `CORE` 8.1d asks an acoustic nominator to correct for time of flight. PinPointCapture now has a user-entered distance; **PinPointStudio has none and sends no `tof_correction`**, which is honest and costs ~2.9 ms per metre of accuracy. Either the host gets a distance setting, or hosts nominate without ToF and that is stated |
| `RV` Annex B13 | Whether the absence of forward secrecy should be **user-visible**. Both applications can report it — the real pair reported it independently at both ends — and nothing says whether they should |
| `CORE` Annex B2 / `SessionLink` | Provisional and originated by nobody. Freeze it as provisional, or remove it |
| `CORE` Annex B15 | Whether requirements traceability is maintained or the citations return |

**Library** — `libppcp`'s own remaining `impl` cells.

| Cell | State | Note |
|---|---|---|
| CT-I5 | `impl` | Calibration change mid-session closes and reopens a Stream; the row is *paired* and has no socket form |
| CT-I16 | `impl` | The re-solve half needs an import path that re-solves |
| CT-I22, CT-I30, CT-I31 | `impl` | *static* rows against a declaration the library does not itself produce — they are the applications' to pass, and the library's cell should probably be the "not this peer's row" state §2.1 asks for |
| CT-I38 | `impl` | All four eviction exits and both refusals are asserted in `test_ct_i38`; the cell predates that and should be re-read |
| RT-6, RT-8 | `impl` | Library halves of rows whose other halves live in the applications |

**Nothing in the library blocks the freeze.** The five cells above are either rows that belong to another column or cells that have not been re-read since the test that closes them landed.

---

## 3. What the real pair proved, and what it did not

On 23 August 2026 PinPointCapture dialled PinPointStudio's real `Ppcp::Listener` over loopback and completed a session. This is the first time two independently-written applications, on two platforms, spoke PPCP to each other.

**The transport.** TLS 1.2, `TLS_PSK_WITH_AES_128_GCM_SHA256` — plain PSK, no forward secrecy. Both ends reported the same outcome independently: the device as `"TLS 1.2, TLS_PSK_WITH_AES_128_GCM_SHA256 — no forward secrecy"`, the host as `TLSv1.2 TLS_PSK_WITH_AES_128_GCM_SHA256 psk [no forward secrecy]`. The host offered both 1.2 and 1.3; the device could reach only 1.2, because Apple's `tls_ciphersuite_t` contains no PSK entry. That is `RV` 5.4b1's measured outcome meeting `RV` 5.2b1's obligation to offer everything the platform exposes, and it is the first time the two have been demonstrated against each other rather than each against itself. The PSK identity crossed as **17 octets of binary, not valid UTF-8**, unchanged — `RV` 5.3f, and the clause erratum E21 exists to make survivable.

**What ran.**

| Phase | Evidence |
|---|---|
| TLS link | `link_up: true`; `errors_fatal: 0`, `unknown_rx: 0`, `dropped_events: 0` |
| `hello` / `declare` | `declares_rx: 2`; the device declared seven profiles and two Source kinds and the host took it without complaint |
| Session open and join | Host `session_opened: true`; device carried the host's `timebase_ref: tb:host` |
| Streams | Two opened by the device, four seen by the host (two live, two replayed); `continuous_streams_rx: 2` |
| Clock synchronisation | 44 sync events, 22 probes, one relation published, `estimators_without_estimate: 0`; 31 heartbeats and 31 acks |
| Arm | Answered with a **Readiness measurement**, never a state name (5.15a) |
| Candidates and arbitration | Both directions. In the acoustic row a **single Shot referenced four Candidates from both peers** (`max_shot_candidates: 4`, `nominations_refused: 0`) — which is I8's whole point, demonstrated across two implementations for the first time |
| Honest clock reporting | The host published an offset for the one device clock it had measured and **`has_offset: false, offset_ns: 0`** for the two it had not. It did not substitute a zero (`CONF` 5b) |
| Offer and replay | `offers_tx`, `offers_accepted: accept`, `replay_completed: true`; host `offers_rx: 1` |

**What did not run, and why.**

| Not run | Reason |
|---|---|
| **A Capture carrying bytes** | `payload_frames: 0`. Every announced Capture is `completeness: absent` (8.4b, I10) because the device is a simulator with no camera and the ring buffer is not wired. **The entire bulk-transfer half of the protocol — `payload_begin`/`chunk`/`ack`/`end`, chunk and whole-payload digests, `already_present`, resume-from-last-acked, and `capture_committed` — has never crossed between the two applications.** It is exercised at both ends separately and against `ppcp-sim`, never between them |
| A camera declaration meeting a foreign one | `declared_camera: false`. This is IOP-2's named purpose and CT-S1's and CT-S7's; a simulator enumerates no camera Source |
| Preview streams | `preview_streams_rx: 0`. No `CONF` §5 row this device is party to needs one |
| `capture_request` | The host issued none. `ppcp-sim` can now originate them (F-S5-2, `requesting-host`); the wave-2 host cannot yet |
| The file-import direction of IOP-3 | The replayed Session went through the **live ingest** path, not the import ledger. The file direction is covered separately, by PinPointStudio reading PinPointCapture's two checked-in bundles |
| Any screen | Both harnesses open the Session the applications do not yet open. Every row is evidence about the protocol layer, not about a product |

**And it found the worst defect of the session.** The real pair is what surfaced **F-S5-3**: a Session offered and replayed over the live link silently rebound the *live* Session's `timebase_ref` to the exporting device's clock. Every subsequent `t0` was then expressed in a timebase the live Session had never declared — the device's own conversion became the identity — and the host's arbiter reported `candidates_foreign 4, adopted 4, groups 2` where the device had nominated two: **two Sessions arbitrated as one.** Nothing was malformed and neither end raised an error. It is now erratum E28, a library fix, and a regression guard verified red with the fix disabled.

That is the argument for `CONF` 5c in one paragraph. The defect had been in the library since L6, through two conformance tools, 49 library tests and two application suites, and it took two products on two platforms talking to each other to make it visible.

---

## 4. What the programme found in the specification

**Twenty-nine errata across five sessions**, against a document set that had passed five review rounds, a requirements-traceability audit and two implementation teams' sign-off at every round.

| Session | Errata | Found by |
|---|---|---|
| S1 | E1, E5, E10, E11, E20, E21, E22, E23 | Two implementations building the transport and the encoder |
| S2 | E6, E9, E15, E16 | Building the message layer and the bundle |
| S3 | E2, E12, E13, E14, E17, E19 | Building sync, arbitration and the capture path |
| S4 | E3, E4, E18, E24–E27 | Rendezvous, and the two freeze-gate audits |
| S5 | E7, E8, E28, E29 | The conformance tool, and the first real pair |

### The three that would have broken every deployment

**E1 — `link_bind`.** `CORE` §3 and `ENC` §2 said nothing about how a listener associates the several TCP connections of one peer, or which of them is channel 0. Both teams built the two-connection transport and both invented a rule: PinPointStudio grouped by the pairing the TLS PSK identity resolved to and ordered by the dialler's serialised handshakes; PinPointCapture used arrival order. Each is correct against itself, which is the definition of an interoperability failure, and **neither would have met the other**. Both implicit rules also fail on the `direct` path, where there is no PSK identity to group by. Found in session 1, before either product had a counterpart to fail against.

**E6 — the `session_id` collision.** `ENC` 5a reserves `session_id` as an envelope key; `ENC` 4d makes a duplicate key `malformed`; the envelope and the body share one flat CBOR map; and `PPCP-MSG` lists a `session_id` field in **eight** message bodies. An implementation obeying all four clauses **could not encode `session_open` at all** — the first message of every hosted Session. It survived five review rounds because no reviewer read the four clauses at once.

**E28 — the replay rebinding.** Described in §3. `CORE` 4.1a makes `timebase_ref` immutable and is written about *the same* `session_id`; read literally it guards nothing against a different one, so a `session_offer` accepted mid-session redefined the live Session's clock. Silent at both ends.

Each is a **composition** defect: two clauses, or two implementations, each correct in isolation. None was reachable by reading one document, and none was reachable by testing one implementation.

### What each application found that the other could not

**PinPointStudio found what only a peer that *listens* can find.** E21 — the PSK identity carrying a `0x00` octet. OpenSSL's TLS 1.2 PSK callbacks length the identity with `strlen`, so an embedded zero truncates it, the server resolves nothing, and the handshake fails **roughly one connection in sixteen**. An intermittent handshake failure at a driving range is diagnosed as a network fault, and the defect survived a session of manual testing before it was characterised. It is unreachable from the dialling side, where the identity is only ever written. PinPointStudio also found E3 (`mu` counting handshakes, so the default `mu: 1` was spent by the control channel and the bulk channel of the same link refused — **every conformant pairing died on its second channel**) and E7 (a payload with no declared container).

**PinPointCapture found what only a peer on a *constrained platform* can find.** E23 — `Network.framework`'s listener has no server-side PSK resolver, so `RV` 5.3b is unimplementable there and `RV` 3.5b's "the capture peer advertises" is unimplementable with it. A peer that advertised anyway would be discoverable and unable to complete the handshake it advertised for. It also found E4 (`RV` 2c forbidding the plaintext transport `CONF` §2c's own **required** test infrastructure runs over — jointly unsatisfiable for any peer that both claims RV and is testable), and the four F-D7 questions that became E24–E27. None is reachable from a desktop with OpenSSL.

**And erratum E25 caught a defect neither team could have caught before it existed.** `RV` 3.3a's `pv` carried `1.0-1.2` as an example of a range syntax that was **defined nowhere**, while `MSG` 3.1b spelled the same idea as a list and `CORE` 10.1f as "the sender's full supported range" — three documents, one concept, three spellings. E25 fixed one syntax; PinPointCapture then found (F-S5-5) that it had been advertising `pv=ppcp/1.0`, the wire *token*, which is not a range at all. **Every conformant browser was required by 3.3d to discard its advertisement**, so the device would have been invisible on the discovery path to anybody who implemented the clause. Nothing before E25 could have found it.

### What the two freeze-gate audits found

`CONF` 5b1 and 5b2 are required before freeze and both now run in `ctest`.

- **5b1, the profile boundary:** no MUST anywhere requires originating a message no profile confers. It also reported that **27 of the 45 catalogued messages were required by no normative clause at all**.
- **5b2, the adjacent-MUST sweep, run over those 27:** seven were **responses nothing obliged a peer to send** — a peer could receive `hello`, `declare`, `session_open`, `stream_open`, `heartbeat` or `sync_probe`, never reply, and violate nothing. That is erratum E18. Eight more had an obligation that did not name the message, so no audit and no implementer reading the catalogue would find it. Fifteen are deliberately optional and now say so, in a column the 5b1 audit asserts on every run.
- **The sweep over S5's own errata found five clauses that the new errata had put into disagreement** with old ones — `I13` and `CORE` 10.1d against E11's closed vocabularies, 5.11j against E16, I8 against E29, I16 against E28. All five reconciled. Two of them would have shipped.
- **And the gate itself had two defects, found by using it.** Its origination verbs are matched with surrounding spaces, so a bolded `**emits**` hid one — a gate a bold verb defeats is not a gate. And adding a column to `MSG` §11 silently switched *off* its clause comparison, which read a fixed field index: the gate did not fail, it stopped checking, and nothing said so.

---

## 5. Recommendation

**Freeze the four `PPCP` documents and `PPCP-RV` against anything but errata, now. Do not declare `ppcp/1.0` stable.**

The two are different acts and the specification set already separates them (`CORE` §0, Annex B0). The case for freezing the **text** is strong: twenty-nine errata over five sessions, the rate falling sharply after S2, the last four all found by tooling and by the first real pair rather than by building anything new, and the two required freeze-gate audits passing and automated. The case against declaring **stability** is that `CONF` §5's own completion test is not met, and one of the ways it is not met cannot be fixed by anybody in this programme.

### The freeze is conditional on exactly this list

**Blocking — a stable declaration is wrong until each is done.**

| # | Condition | Owner | Class |
|---|---|---|---|
| 1 | **A Capture carrying bytes crosses between the two applications.** The whole `payload_*` family, both digests, `already_present`, resume-from-last-acked and `capture_committed` have never been exercised between them. This is the largest untested surface in the protocol and it is where a wire-format defect would hide | PinPointCapture (wire the ring buffer to the capture path), then a re-run of IOP-1 | phone |
| 2 | **A camera declaration meets a foreign one on real hardware.** CT-S1 assertions 1–5, CT-S7 (4), CT-I17 and IOP-2's named half. CT-S7 (4) is the assertion that catches a hardcoded zero, and `CONF` §2c calls it the most dangerous site in the suite | PinPointCapture, on a phone | phone |
| ~~3~~ | ✅ **MET, 25 August 2026 — `maintainer-accepted`.** `RV` §9 calls RT-12 the requirement the whole security model rests on and the one no test can catch. The maintainer read the generator and erasure paths in both applications: one CSPRNG entry point each over the platform's audited source, **no fallback on any path**, `psk` at 32 bytes against a 16-byte floor, `OPENSSL_cleanse` on every C++ exit path. ⛔ **`maintainer-accepted`, NOT `pass`** — this project has no qualified cryptographic reviewer and the owner accepting a risk is not an expert having checked it. Two platform limits accepted rather than fixed, both unfixable in application code: Swift has no zeroise for `Data`/`SharedSecret` ([11.11h1](../specification/ppcp-rv.md#1111-where-x25519-comes-from)), and deleting a file row does not shred superseded disk blocks | Discharged | human review |
| ~~4~~ | ✅ **MET, 25 August 2026 — `maintainer-accepted`.** The maintainer read both applications' TLS setup and capability-query paths. PinPointCapture enumerates ciphersuite groups from the SDK and sets TLS 1.3 as a **maximum**, so it offers more the moment the platform does — and offers nothing extra today because iOS exposes no PSK suite at all, which is compliance **by construction** ([5.2i](../specification/ppcp-rv.md#52-tls-profile)). PinPointStudio probes OpenSSL per suite and verifies each landed. ⚠ **Gap accepted, not fixed:** Studio's candidate list is a constant, so it queries availability of a known set rather than the set itself — complete against today's RFCs, so no live defect. **Maintainer's reasoning:** *even if a new capability arrives later, the current code is robust enough given the data at risk.* ⛔ `maintainer-accepted`, **not** `pass` | Discharged | human review |
| 5 | **The matrix gains a cell state for "not this peer's row"** and its 48 `—` cells are re-classified. Until then the matrix cannot answer whether the suite is complete, and neither can this report | `libppcp` | bookkeeping |

**Non-blocking, but the freeze should record them as accepted rather than pending.**

| # | Item | Why it does not block |
|---|---|---|
| 6 | **`CONF` 5c — a second implementation** | No third party exists and none is expected. The foreignness in every pairing to date is in the *declaration*, never the implementation, and that limitation should be stated in the freeze rather than waited on. It is the reason F-S5-3 survived to session 5 and it will not stop being true |
| 7 | **An expert cryptographic review of `RT-24a` and `RT-27`** — *wanted, no longer blocking. Reclassified 24 August 2026.* | Both rows are **discharged by a named reviewer** ([`matrix.md` §5b](matrix.md#5b-rt-24a-and-rt-27--how-these-two-were-discharged-and-on-what)) on the evidence of [`rv6-machine-review-2026-08-24.md`](rv6-machine-review-2026-08-24.md), which reproduced every §10.4 vector under an independent implementation and found no security or interoperability defect. ⚠ **The reviewer is not a domain expert and the supporting review is machine-generated**, so an expert reading is still wanted — hand them the review's [§4](rv6-machine-review-2026-08-24.md#4-what-the-reviewer-could-not-determine--start-here) first: compiled-output constant-time, the far half of the §11.11 seam in the two applications, and `ppcp_cbor_validate`'s duplicate-key rejection, which nobody has read by eye. ✅ **[RT-12](../specification/ppcp-rv.md#9-conformance) and [RT-17](../specification/ppcp-rv.md#9-conformance) are both discharged as `maintainer-accepted`** (conditions 3 and 4, 25 August 2026), which is an owner's risk acceptance and **not** an expert reading — ⚠ the expert reading is wanted on those two exactly as it is on RT-24a and RT-27, and **no review row in this document has ever been read by a qualified cryptographic reviewer** |
| 7 | **CT-S2 and the rig** — timing constants, `CORE` B8's two defaults per nominator class, B10's `sampled` accuracy | Every unmeasured constant is declared `assumed` and the provenance is on the wire, which is what I31 exists for. A measurement changes data, not the protocol |
| 8 | **Host-side `tof_correction`** | A product decision. The host sends none today and that is honest; ~2.9 ms per metre is the cost |
| 9 | **`RV` B13** — whether the absence of forward secrecy is user-visible | A product decision. 5.4k makes it answerable either way and the real pair reported it at both ends |
| 10 | **`RV` B2** — per-peer re-keying for `mu > 1` codes | Both publishers emit `mu: 1` only |
| 11 | **`SessionLink` / `CORE` B2** | Provisional, originated by nobody. Freeze it as provisional |
| 12 | **The clause-id namespace collision** ([§1](#1-specification-status)) | Every citation in the set is document-qualified. Fixing it after freeze is an erratum against all five documents at once; the freeze is the moment to decide, not the moment to act |

### What is already met

- Both required freeze gates run in `ctest` and pass, and 5b1 now asserts the 5b2 sweep's own answer so it cannot go stale.
- All ten `CONF` §5a pairings pass, three of them between the two real applications.
- Every erratum is in the normative text with the finding that produced it, not only in a change history.
- Every `pass` cell in the matrix comes from a named command in one of the three claim files.
