# PPCP implementation plan — three teams, one protocol

**The tracker for bringing PPCP 1.0 and PPCP-RV 1.0 into `libppcp`, PinPointStudio and PinPointCapture.**

| | |
|---|---|
| Status | **Active tracker.** Updated at the end of every orchestration session |
| Date | 22 August 2026 |
| Against | `PPCP-CORE` revision 9, `PPCP-MSG`, `PPCP-ENC`, `PPCP-CONF` 1.0; `PPCP-RV` revision 8 — all approved for implementation |
| Companion | [`../conformance/matrix.md`](../conformance/matrix.md) — the compliance record this plan exists to fill in |
| Requirements | `PinPointCapture/docs/capture-companion-requirements.md` (21 August 2026) |

---

## 0. What this programme delivers, and what it does not

**Delivers.** The PPCP and PPCP-RV elements in place across all three projects, so each project can continue development locally against a real protocol layer:

- `libppcp` — the MIT reference implementation, the conformance suite, the fixture format, the synthetic peer, and the tooling that demonstrates compliance.
- PinPointStudio — a host peer behind the existing camera abstraction, with the bundle import path, the live path, rendezvous, and a conformance claim for the host profile set.
- PinPointCapture — a device peer in the Core layer, with declaration from the real capture stack, the bundle writer, the live path, rendezvous, and a conformance claim for the device profile set.

**Does not deliver.** A working companion app. Detection tuning, replay, markup UI, guided setup, framing validation, the LED rig, measured timing constants, and every threshold the protocol refuses to carry (I14) are product work that follows this programme in each repo.

**What "done" looks like.** Every row of the conformance matrix is either *passing*, *n/a by profile*, or *blocked: rig* — for all three columns — and the interoperability pairings of `PPCP-CONF` §5 that do not need a rig have been run. At that point `ppcp/1.0` is eligible to freeze.

---

## 1. Ground rules

These are fixed. An agent that finds one of them wrong stops and reports rather than working around it.

1. **Three repositories, three licences, no code crosses between them.** `libppcp` is MIT. PinPointStudio is GPL-2.0. PinPointCapture is MIT (decided 22 August 2026 — GPL and App Store distribution conflict, so the app is MIT like the library; requirements OPEN-4 is closed). The **only** shared artefact is `libppcp`, consumed as a dependency by the other two. Nothing is copied from one repo into another — not a type, not a test, not a fixture. If two repos need the same thing, it belongs in `libppcp`.
2. **Each team works only in its own repository.** An orchestration session runs up to three agents in parallel, one per repo. An agent never edits, and never reads for the purpose of copying, another repo's source. It may read the other repos' *documentation* and `libppcp`'s *public headers and specification*.
3. **The specification is the authority and changes first.** An implementation that finds the specification wrong records an erratum in `libppcp/docs/specification/` (`CORE` Annex A.4) and then changes the code. The change history grows; the code never leads it.
4. **Conformance is evidence, not a claim.** Every work package names the `CT-*`, `RT-*` or interop row it unlocks. A package is not done until the row it names is *passing* in the matrix, with a reproducible command. "Implemented" and "passing" are different states and the matrix records both.
5. **Build order follows `CORE` Annex A.1**, which is stricter than it looks because later stages need earlier ones as *test infrastructure*. In particular: injectable clock before anything timed; the synthetic peer before the silent-failure tests; the bundle path before the live path.
6. **Commit to `main` in every repo, no branches, no PRs.** Small commits with messages that say what was found, not what was typed.
7. **Builds are bounded, and an agent reads its repo's build notes before its first build.** On 22 August 2026 a full PinPointStudio app build launched with a bare `-j` (unlimited jobs), concurrent with a `swift build` polling loop in PinPointCapture and a `libppcp` build, exhausted memory and crashed the machine; forty minutes of session S2 went with it. So: every build command carries an explicit job count (`-j3` per agent when three run in parallel; never bare `-j`); no agent loops a build command waiting on another repo — poll `git log`, build once; no agent builds the PinPointStudio *application* target (the `ppcp-tests` tree is the unit of verification; the app build is the user's); and an agent working in PinPointStudio or PinPointCapture reads that repo's `CLAUDE.md` and `~/.claude/projects/<repo>/memory/MEMORY.md` (and the build notes it indexes) before it builds anything — those notes already said Ninja `-j8`, and the agent that crashed the machine had never seen them.
8. **The library owns no I/O.** `libppcp` is sans-I/O, modelled on `libwrist`: it owns no socket, thread, timer, clock or file. The embedding application supplies bytes, timestamps and storage and drains the library. That is what makes one implementation serve both ends (`CORE` A.3), what keeps LGPL transport dependencies out of the MIT library (REQ-TRANS-3), and what makes the fixture replay and the simulator trivial.

---

## 2. Profile sets and conformance claims

| Implementation | Profiles | Role | Claim file |
|---|---|---|---|
| `libppcp` reference | **all eight** — Core, Capture, Detect, Mint, Arbitrate, Live, Markup, Offline | any | `libppcp/docs/conformance/claim-libppcp.md` |
| PinPointStudio | Core + Capture + Detect + **Arbitrate** + Live + Offline + Markup (`CORE` §2.2.3) | `host` | `PinPointStudio/docs/ppcp-conformance.md` |
| PinPointCapture | Core + Capture + Detect + **Mint** + Live + Offline + Markup (`CORE` §2.2.3, "full mobile capture device") | `capture` | `PinPointCapture/docs/ppcp-conformance.md` |
| Synthetic peer (`ppcp-sim`) | configurable — any subset, any declaration | any | n/a — it is test infrastructure |

Both applications also claim `PPCP-RV` conformance: pairing-code path (required), plus service discovery and network join (optional, both intended).

**Negative conformance matters as much** (`CONF` §1d): PinPointStudio must parse `shot` with `authority: device` and never originate one; PinPointCapture must parse `capture_request` and never originate it; neither originates `session_link` until `SessionLink` leaves provisional.

---

## 3. Architecture decisions this plan fixes

Each of these was open in the requirements or implied by the specification. They are decided here so three teams do not decide them three ways. Reversing one is a plan change, recorded in §9.

| # | Decision | Why |
|---|---|---|
| **A1** | **`libppcp` is C11, sans-I/O, CMake, no dependencies**, laid out like `libwrist` (`include/ppcp/`, `src/`, `tests/` with `purity.cmake`, `tools/`). | REQ-PORT-6; `CORE` A.3; the proven embedding pattern PinPointStudio already uses for `libwrist`. |
| **A2** | **The CBOR codec is written in the library**, not vendored. Deterministic encoding always (`ENC` 4e, `RV` 4.3a). | A conformant encoder/decoder is a few hundred lines (`ENC` §9); the limits of `ENC` §8 must be enforced before allocation, which a general-purpose library will not do the way we need. |
| **A3** | **The public surface is one umbrella header `ppcp/ppcp.h`** over per-area headers, and it is the *port surface* for both applications. Nothing else is public. | REQ-PORT-2: the port surface is a documented artefact. Both apps bind to the same set of symbols. |
| **A4** | **PinPointStudio embeds `libppcp` exactly as it embeds `libwrist`**: `FetchContent` from `github.com/PinPoint-Golf/libppcp`, with a sibling `../libppcp` checkout winning (`PP_LIBPPCP_LOCAL`), provenance in the About box. | Existing pattern; nothing new to learn or maintain. |
| **A5** | **PinPointCapture consumes `libppcp` as a SwiftPM package.** `libppcp` carries a `Package.swift` exposing a C target `CPPCP` (headers + sources, no module map hacks); the app's existing `Packages/Core` (`CaptureCore`) gains a dependency on it and wraps it in Swift. Local path `../libppcp` during co-development, git URL once tagged. | The `CaptureCore` README already says it exists to be substituted by `libppcp`; a package dependency is the substitution. The layer-purity test still holds: `CPPCP` is not a platform framework. |
| **A6** | **Transport is two TCP connections per peer pair**, channel 0 and channel 1 (+ an optional channel 2 for preview), each a separate TLS-PSK session over the same derived `K_tls`. | `CORE` §3.1 — the acceptable table's first row. No multiplexer to write, per-channel flow control for free, and `ENC` 2c's header-matches-stream rule is checkable. |
| **A7** | **TLS lives in the applications, never in the library.** PinPointStudio: OpenSSL external-PSK session callbacks (`RV` §8). PinPointCapture: `Network.framework` `NWProtocolTLS` with the PSK, accepting the measured outcome of TLS 1.2 `0x00A8` (`RV` 5.4b1), offering everything the platform exposes (RT-17). The library provides `K_tls`, `K_id`, the PSK identity and its resolver, and nothing that touches a socket. | Ground rule 7; `RV` 5.2i — compliance on the device is demonstrated by observed handshake, not API. |
| **A8** | **Discovery lives in the applications.** The library computes `rn`/`rid` and resolves them; the app registers/browses `_ppcp._tcp`. | Same split as A7. |
| **A9** | **The session store on the device *is* the bundle.** Each Session on disk is a `PPCPBNDL` file (control frames first, payload frames appended as clips land), plus the clip files it references. No second on-disk schema. | `CORE` §9, `ENC` §7, REQ-STANDALONE-2, REQ-CLIP-2. One parser, one format, and every range session is a regression fixture (REQ-TEST-3). |
| **A10** | **PinPointStudio's bundle import is a file transport for the same peer engine**, landing in the same ingest path as a live socket. There is no importer. | `CORE` §9 9a; REQ-HOST-2. |
| **A11** | **The conformance suite has two halves.** (i) C tests inside `libppcp` for every *static*, *fixture* and *injected* method; (ii) `ppcp-conform`, a tool that drives any peer over two sockets using the synthetic peer, for every *paired* method — so PinPointStudio and PinPointCapture are tested by the same tool, from outside, through their real transports. | `CONF` §1e, §2c. An application tested only by its own unit tests is the single-implementation trap `CONF` §2c describes. |
| **A12** | **Every timing constant nobody has measured is declared `assumed`.** `frame_start_to_exposure_offset_ns`, `readout_ns` on every AVFoundation profile; `start` convention with `global` geometry on FLIR. No exception until the rig exists. | I31, CT-S7. Both teams ship the honest value. |
| **A13** | **Device profile data is a JSON file keyed by model**, loaded by the app, validated by the library's provenance rules. PinPointCapture already has `Sources/Platform/DeviceProfiles.json`; it gains the PPCP timing and geometry fields. | REQ-PORT-10; CT-S2 assertion 3. |

---

## 4. Work packages — `libppcp` (team L)

Prefix **L**. The reference implementation, the suite, the tooling. Everything else depends on this, so it runs one session ahead.

### L0 — Repository scaffold

| | |
|---|---|
| Deliverable | `CMakeLists.txt` with presets (`dev`, `san`, `cov`, `rel`, `release`), `include/ppcp/`, `src/`, `tests/` with `purity.cmake` asserting the sans-I/O rule (no `<sys/socket.h>`, `<pthread.h>`, `<time.h>` clock calls, `<stdio.h>` file I/O in `src/`), `tools/`, `Package.swift` with a `CPPCP` C target, `include/ppcp/version.h`, `README.md` updated from "Empty" to the layout. CI-free but `ctest` runs clean. |
| Spec | `CORE` A.3; REQ-LIC-2/3/5, REQ-PORT-6 |
| Unlocks | Everything. |
| Status | ☑ done — S1 |

### L1 — CBOR codec, framing, limits, envelope

| | |
|---|---|
| Deliverable | Deterministic CBOR encoder and a decoder that enforces `ENC` §8 limits *before allocating*, rejects integer keys, duplicate keys, `null`-as-absent, tags and indefinite lengths, accepts half/single floats, ignores unknown keys at every depth (I13). Frame header read/write (`ENC` §3), channel rules (`ENC` §2), message envelope (`ENC` §5) with per-sender `msg_id` and `reply_to`. The worked example of `ENC` §5.1 reproduces byte-for-byte. Bundle magic header (`ENC` §7) read/write. |
| Spec | `ENC` §2–5, §7, §8 |
| Unlocks | CT-I1 (encoding half), CT-I13, the fixture format (`CONF` 2b) |
| Status | ☑ done — S1 |

### L2 — Injectable clock and timebases

| | |
|---|---|
| Deliverable | `ppcp_clock` — the embedding supplies `now(tb)`; tests supply a simulated clock with offset, skew and injected discontinuities. `Timebase`, `TimebaseRelation` (affine requires both sigmas — unconstructible otherwise), `ClockDiscontinuity`, `Instant`, `Series`, `Interval`, `Estimate` as C structs with encode/decode and structural validation. No composition API exists (I18). |
| Spec | `CORE` §5.1, §5.3–5.5, §6.4, §6.5 |
| Unlocks | CT-I1, CT-I3, CT-I4, CT-I18 (static half), `CONF` 2a |
| Status | ☑ done — S1 |

### L3 — Canonical instant and rolling shutter

| | |
|---|---|
| Deliverable | `ppcp_canonical_instant(profile_timing, t, d)` and its inverse; `ppcp_row_instant(geometry, canonical_first, r)` for both directions and `R == 1`. Scalar-or-array `AchievedFrames` accessor so the scalar path is the one the product uses. Worked examples A–D of `CORE` §6.1.1 reproduce to the nanosecond. |
| Spec | `CORE` §6.1, §6.2, §5.8 |
| Unlocks | **CT-S1 all six assertions**, CT-I17 |
| Status | ☑ done — S1 |

### L4 — Type vocabulary and validation

| | |
|---|---|
| Deliverable | Every entity of `CORE` §5 as a C struct with encode, decode and validate: Peer, Source, CaptureProfile (+Timing, Provenance), MeasuredCapability, AchievedSummary, AchievedFrames (with the `intrinsics` first-element rule), ThermalLevel, Calibration, Session, ContextChange, Stream, Candidate, Shot, Capture (Anchor exactly-one-key), Readiness, ShotLink, SessionLink, Annotation. Validation makes the structural invariants unconstructible: I22 iff, 5.10e iff, I27, I29, I28 (no synthesised `measured`), 5.6e `confidence` iff `classified`, 5.16e/f `confirmed_by` rules, 5.18j `stream_id` follows `kind`. Open registries are strings; unknown values pass. |
| Spec | `CORE` §5 entire, §10.3 |
| Unlocks | CT-I1, CT-I3, CT-I22, CT-I26 (static), CT-I27, CT-I28, CT-I29, CT-I31 (static), CT-I6 (static), CT-I9 (API surface — there is no merge function) |
| Status | ☑ code — S2 (commit `7d56e79`). The rows it unlocks are still `impl`: the vocabulary exists and the tests stated over it are L15's. |

### L5 — Message catalogue

| | |
|---|---|
| Deliverable | All forty-five messages of `MSG` §11 encode/decode — including `link_bind` (erratum E1) — with class (request/response/event), channel and originating-profile metadata in a table the profile-boundary audit (L16) reads. `error` with every code of `MSG` §10 and the fatal/non-fatal distinction. `unsupported_version` carries `detail.supported`. |
| Spec | `MSG` §3–§11 |
| Unlocks | CT-S6 assertion 4 (every message decodes on a Core-only peer) |
| Status | ☑ done — S2. `ctest --preset dev -R test_ct_s6` (and `--preset san`): all forty-five encode and decode, the channel rules refuse both violations of MSG §2, the seventeen error codes carry the fatal split, and `unsupported_version` is refused without `detail.supported`. |

### L6 — Peer engine: connection, declaration, session, streams

| | |
|---|---|
| Deliverable | `ppcp_peer` — the sans-I/O state machine. `hello`/`hello_accept` with version selection, support window (`CORE` 10.1e), `role_conflict`; `declare` as a complete snapshot by generation, `declare_ack` with per-profile notes (ingest policy is a **callback** the embedding supplies — no threshold in the library, I14); `session_open`/`joined`/`resume`/`state`/`close`; `stream_open`/`ack`/`close` by either peer; `arm`/`disarm`/`readiness`/`interruption`; C1–C3: parse everything, originate only what declared profiles confer, `profile_not_supported` never closes. Per-channel input (`ppcp_peer_feed(ch, bytes)`) and output queues (`ppcp_peer_drain(ch)`). **Link binding** (`ENC` §2.1): a dialler-side `link_bind` emitter and a listener-side binder that groups streams by `link_id`, takes the channel from the header, and rejects the three cases of 2.1c. |
| Spec | `CORE` §2.2.2, §7, §10; `MSG` §3–5 |
| Unlocks | CT-I5, CT-I12, CT-I14 (the grep has something to grep), CT-I20, CT-I24/CT-S6 |
| Status | ☑ done — S2. `ctest --preset dev -R test_ct_i24` and `ctest --preset dev -R CT-I14`. CT-I14 and CT-I20 pass; CT-I5 and CT-I24/CT-S6 are `impl` — CT-I5 needs the calibration/Capture half (L7) and CT-S6 assertion 1's "and arbitrates over the result" needs L10. |

### L7 — Captures, bulk transfer, I38

| | |
|---|---|
| Deliverable | `capture_announce` / `capture_update` / `capture_committed`; the `payload_*` family with chunking, SHA-256 per chunk and whole, `already_present`, `payload_resume` from last acked index, `achieved_frames` on `payload_begin` only (I30, with the `capture_update` failed-transfer exception). Transfer state per Capture as the owner's view; `confirmed` settable only by receipt of `capture_committed`. The eviction predicate implementing **each of 5.14g's four exits** and refusing policy as a fifth (5.14g1). Stream-anchored coverage accounting (I36) with the four cases of CT-I36; preview live-only rule (5.11j, CT-I36a). SHA-256 implemented in the library (no dependency). |
| Spec | `CORE` §5.8, §5.11.1–2, §5.14; `MSG` §8; `ENC` §6 |
| Unlocks | CT-I10, CT-I11, CT-I30, CT-I34, CT-I36, CT-I36a, CT-I38 |
| Status | ☑ done — S2. `ctest --preset dev -R test_ct_i38`. CT-I30, CT-I36, CT-I36a and CT-I38 move to `impl`: each is stated as *paired* or *fixture* against a real capture device, and what exists is the library half — the four eviction exits and both refusals, the coverage rule's four cases, the payload codec with both digests, and preview refused a `pending` announce and refused entry to a bundle. |

### L8 — Bundle reader and writer

| | |
|---|---|
| Deliverable | `ppcp_bundle_writer` — appends frames in the order they would have been sent, manifest before any payload (`ENC` 7c); a hostless peer records `session_open` without the two arbitration parameters and **no `arm`/`disarm`** (`CORE` 7.3b). `ppcp_bundle_reader` — streams a bundle through the same `ppcp_peer_feed` path as a socket; truncated final frame → `partial` unless asserted otherwise (`ENC` 7d), never upgraded; unknown `minor` tolerated. Re-import idempotent on `Capture.id` scoped by session and peer (I34). The fixture format **is** this. |
| Spec | `CORE` §9, §8.5c; `ENC` §7; `MSG` §9.1–9.2 |
| Unlocks | CT-I12, CT-I15, CT-I16, CT-I34, CT-I36 (c)(d), `CONF` 2b, interop row "bundle A→B" |
| Status | ☑ done — S2. `ctest --preset dev -R test_ct_i12`. CT-I12 and CT-I34 pass and `CONF` 2b's fixture format exists; CT-I16 and CT-I36 are `impl` (the re-solve half is L9, the coverage half is L7); CT-I15 not started. |

### L9 — Clock synchronisation and liveness

| | |
|---|---|
| Deliverable | `sync_probe`/`sync_reply` per timebase (I21 — one sequence per declared timebase, no composition); an offset-and-skew estimator with minimum-RTT filtering, published via `relation_update` with both sigmas, **filtered never stepped**; burst of 10–20 on connect / network change / thermal event, driven by the embedding's events; maintenance cadence independent of heartbeat; `sync_residual`. `heartbeat`/`heartbeat_ack` with thermal/storage/battery from an embedding callback; three missed → link lost → zero-host regime entry (8.3g) with roster and `timebase_ref` unchanged. |
| Spec | `CORE` §5.4.1, §6.3, §7.4, §8.3g; `MSG` §5.4, §6 |
| Unlocks | CT-I18, CT-I21, CT-S5, CT-S4 assertion 7 |
| Status | ☐ |

### L10 — Detect, Mint, Arbitrate

| | |
|---|---|
| Deliverable | **Detect**: `candidate` emission requires a declared Source with a declared Timebase (I26); `Candidate.at` is canonical, converted by the library from the nominator's raw instant + profile + exposure (I33), `tof_correction` both-or-neither (I29), every candidate emitted (7.1d). **Mint**: promotion is a **callback** (I14); no window; one Candidate per Shot, `authority: device`; the 8.2i deadline (`issue_hold_ns + heartbeat_interval_ms`) with the promotion condition (I32); 8.2i1 — no affine relation to `timebase_ref` → no Shot; `shot` sent immediately. **Arbitrate** (role host only, I20): convert candidates via current relations (no second canonical conversion), exclude-and-retain on missing/unrelated/uncertain (policy callback), coincidence grouping within `coincidence_window_ns`, issue no earlier than `issue_hold_ns` and no later than the mint deadline (8.2h), late candidates attach without moving `t0` (I7), two same-basis candidates from different peers both retained (I8), 8.2k attach to a device-minted Shot, 8.2l `shared_candidate` link. Shot extension merge that is additive and order-independent (5.13d–e). `ShotLink` with `confirmed_by` rules. `capture_request` served with `outside_buffer` as a result not an error (8.4). |
| Spec | `CORE` §5.12, §5.13, §5.16, §8 |
| Unlocks | CT-I6, CT-I7, CT-I8, CT-I9, CT-I23/CT-S4 (1–6), CT-I26, CT-I29, CT-I32, CT-I33, CT-I35 |
| Status | ☐ |

### L11 — Markup

| | |
|---|---|
| Deliverable | `annotation` either direction; opaque `body` ≤ 8 KiB round-tripped byte-identical; supersession by `id`, `revision`, then bytewise `author_peer_id` — total order, converges in both delivery orders; `at` timebase per 5.18g; no API path from an Annotation to any computed quantity (I37, by surface). |
| Spec | `CORE` §5.18; `MSG` §9.0 |
| Unlocks | CT-I37 |
| Status | ☐ |

### L12 — PPCP-RV: payload, derivation, identities

| | |
|---|---|
| Deliverable | Pairing-code payload encode/decode (`v` first by the two-character rule, every optional field, `sid` → canonical lowercase UUID text), `ppcp:` URI form, `v`-unknown → *version* report (4.2b); HKDF-SHA256 (implemented in the library — HMAC-SHA256 over the SHA-256 from L7) producing `PRK`, `K_tls`, `K_id`; `rn`/`rid` and the 17-octet PSK identity with CSPRNG bytes **supplied by the embedding**; resolver over a set of held pairings; expiry decision helper honouring 4.4a/4.4a1. **All vectors of `RV` §10 reproduce byte-for-byte, including the all-fields code.** Nothing here touches TLS, a socket, storage or a random source. |
| Spec | `RV` §3.4, §4, §5.1, §5.3, §10 |
| Unlocks | RT-1, RT-2, RT-3, RT-14 (static half), RT-8 (resolver half) |
| Status | ☑ done — S1 |

### L13 — Synthetic peer (`tools/ppcp-sim`)

| | |
|---|---|
| Deliverable | A command-line peer over two TCP sockets (plaintext — it is test infrastructure, and the `direct` path of `RV` §2 is conformant without RV) that can present **any declaration** from a JSON description: different `timing.convention`, `geometry`, `provenance: measured` with a non-zero offset, `unrelated` timebases, a foreign profile set (Core-only observer; `Core + Arbitrate + Live + Offline` with no Detect), a host that never answers a Candidate, a host delayed past the mint deadline, three timebases; **a TLS-PSK mode that offers `psk_ke` only**, so the hosts' refusal can be demonstrated rather than asserted (RT-4, requested by H in S1). Scriptable scenarios for each interop row. This is the `CONF` 2c requirement and it is what makes CT-S3, S4, S6, S7 writable at all. |
| Spec | `CONF` §2c, §4, §5 |
| Unlocks | CT-S3, CT-S4, CT-S6, CT-S7, every *paired* test, every interop row |
| Status | ☐ |

### L14 — Conformance tool (`tools/ppcp-conform`)

| | |
|---|---|
| Deliverable | Drives a peer-under-test over its two sockets (or a bundle file) through the *paired* and *injected* scenarios, asserting on the wire, and emits a machine-readable result (JSON) plus a Markdown fragment in the exact row format of [`matrix.md`](../conformance/matrix.md). Takes the claimed profile set as input and applies `CONF` §1b–d: positive tests for declared profiles, negative tests for undeclared ones. |
| Spec | `CONF` §1, §3, §4 |
| Unlocks | The *passing* state for the two application columns of the matrix |
| Status | ☐ |

### L15 — Reference conformance run

| | |
|---|---|
| Deliverable | Every CT row with method static/fixture/injected implemented as a C test; every paired row run `ppcp-sim ↔ libppcp` through `ppcp-conform`; `claim-libppcp.md` generated. Fixture bundles under `tests/fixtures/` for CT-I2, I11, I12, I13, I15, I34, I36. |
| Spec | `CONF` §3, §4 |
| Unlocks | The `libppcp` column of the matrix |
| Status | ☐ |

### L16 — Specification audits as tooling

| | |
|---|---|
| Deliverable | `tools/audit-profile-boundary` — reads the L5 message table and the normative clause list, asserts every clause that requires originating a message is bound to a profile that confers it (`CONF` 5b1). `tools/audit-adjacent-must` — a checklist generator for the sweep of `CONF` 5b2, keyed by revision diff. Both run in `ctest`. |
| Spec | `CONF` §5b1, §5b2 |
| Unlocks | Freeze readiness |
| Status | ☐ |

### L17 — Errata and freeze report

| | |
|---|---|
| Deliverable | Every specification defect found during L1–L16 and by teams H and D recorded in `docs/specification/` change history as errata with the clause amended; `README.md` status line updated; a freeze-readiness report listing the matrix rows still open and why (rig, product decision, second implementation). |
| Spec | `CORE` §0, Annex A.4, Annex B0 |
| Status | ☐ |

---

## 5. Work packages — PinPointStudio (team H)

Prefix **H**. The host. GPL. Consumes `libppcp` by `FetchContent`. Every package lands behind the existing abstract-base-and-factory rule (REQ-HOST-1).

### H0 — Embed `libppcp`

| | |
|---|---|
| Deliverable | `CMakeLists.txt` block mirroring the `libwrist` block exactly: `FetchContent` from `PinPoint-Golf/libppcp`, `PP_LIBPPCP_LOCAL` sibling-checkout override, static link, version/commit provenance into the About box. `LICENSEDEPS.md` gains the MIT entry. Compiles with nothing calling it yet. |
| Depends | L0 |
| Status | ☑ done — S1 (GitHub fetch defaults OFF until the repo is public: `PP_LIBPPCP_FETCH`) |

### H1 — Transport: two TCP channels, TLS-PSK via OpenSSL

| | |
|---|---|
| Deliverable | `src/Ppcp/ppcp_transport.{h,cpp}` — a listener and a connector, each producing **two** ordered byte streams (channel 0, channel 1; optional channel 2 for preview), each a TLS session keyed by `K_tls` through OpenSSL's **external-PSK session callbacks** (not the RFC 4279 hint interface — `RV` §8), identity resolved through the L12 resolver, uniform failure for unknown identity and wrong key (5.3c, 7.7c), empty `psk_identity_hint` where TLS 1.2 is negotiated (5.2h property 3), **no plaintext fallback** (5.2f), achieved version and KEX mode surfaced (5.4k). Offered suites derived from a capability query, not a constant (RT-17). Unit-tested loopback with a known `K_tls`; no `libppcp` peer logic yet. Built and tested on macOS and Linux; Windows builds. |
| Spec | `CORE` §3; `RV` §5.2, §5.3, §7.7, §8 |
| Unlocks | RT-4 (host half), RT-10, RT-11, RT-14 (wire half), RT-17 (review) |
| Depends | Nothing in `libppcp` except the L12 derivation API, which can be stubbed with the §10 vectors until L12 lands |
| Status | ☑ done — S1 (RT-4 `psk_ke` refusal awaits a `ppcp-sim` mode that offers `psk_ke` only — added to L13) |

### H2 — Host peer adapter and own-Source declaration

| | |
|---|---|
| Deliverable | `src/Ppcp/ppcp_host_peer.{h,cpp}` — wraps `ppcp_peer` in `role: host` with the Studio profile set, pumps the two channels from H1, supplies the clock (the host's monotonic clock as `tb:host`), the ingest-policy callback (the existing 120 fps floor lives **here**, never in the library — CT-I14), thermal/storage callbacks. **Declares the host's own Sources** from the existing `VideoInputBase`/`CameraCapabilities`: FLIR/Aravis cameras as `convention: start`, `geometry: global`, `intrinsics: fixed`, with `calibration` where the rig has it; the host microphone as a `microphone` Source (`convention: mid`) so it can nominate. A host with no cameras sends `declare` with an empty `sources` list (`MSG` 3.3d). **H1's listener is reworked to bind streams by `link_bind` (`ENC` §2.1) and the pairing-grouping rule removed.** |
| Spec | `CORE` §5.6.1, §7.2; `MSG` §3 |
| Unlocks | CT-S3 assertions 1 and 3 (host side), CT-I19 |
| Depends | L6 |
| Status | ☐ |

### H3 — Bundle import as a file transport

| | |
|---|---|
| Deliverable | A file transport that streams a `PPCPBNDL` through the same `ppcp_host_peer` feed as a socket would, producing Sessions, Shots, Captures and clip files in the same ingest path the live link uses — landing alongside the existing per-swing export (`swing.json`) rather than replacing it. Idempotent re-import (I34); `completeness` honoured as asserted (I10, `ENC` 7d); `capture_committed` queued for the owning peer on its next connection (5.14h). ~~A "Import session…" entry in the UI that does nothing more than pick a file.~~ **No UI** (§9, 22 Aug): the host chooses from sessions a connected device offers; H3 is the engine only. |
| Spec | `CORE` §9, §8.5c, §5.14h; `ENC` §7 |
| Unlocks | CT-I12, CT-I15, CT-I16, CT-I34 (host column), interop row "device, no host → bundle → host import" |
| Depends | L8 |
| Status | ☑ done — S2 (`f752c96`, `5fafe6f`). `ctest --test-dir build/ppcp-tests` 5/5. CT-I12 and CT-I34 pass (host); CT-I15/I16 `impl` (interval half is H7, re-solve half is L9). Import lands clips under `PPCP Imports/<peer>/<session>/`; the join to `swing.json` waits on Session/Shot identity (host item 2). App-side wiring (menu, QML) compiled standalone only — app target unverified; menu entry is macOS-only |

### H4 — `VideoInputPpcp` behind the camera factory

| | |
|---|---|
| Deliverable | `src/Video/VideoInputPpcp.{h,cpp}` registered with `video_input_factory`, presenting a connected capture peer's camera Source as a `VideoInputBase`: `queryCapabilities()` from the declared profiles, `start()` opening a Stream, clips arriving as Captures delivered into the EventBuffer/SwingWindow path with the **canonical instant** applied from `achieved_frames` (L3) before any timestamp is compared with a host camera's. A `preview` Stream, where the device offers one, feeding the live tile on its own bulk channel. |
| Spec | `CORE` §5.11, §5.11.2, §6.1; REQ-HOST-1 |
| Unlocks | CT-S1 on the host path, CT-I36a (host as consumer) |
| Depends | L6, L7 |
| Status | ☐ |

### H5 — Live session: sync, heartbeat, arm, arbitration bridge

| | |
|---|---|
| Deliverable | Session open with `tb:host` as `timebase_ref` and both arbitration parameters; sync prober per device timebase and **per host timebase** (I21 — the host with several cameras on independent clocks runs it per clock); heartbeat at the session interval reporting degradation to the UI; `arm`/`disarm` wired to the existing SHOT/armed flow. **Arbitration bridge**: the existing acoustic and IMU shot detectors nominate as Candidates from host-owned Sources into the library's Arbitrate engine; the existing arbiter is replaced by, not layered over, `ppcp` arbitration for any session containing a PPCP peer; `capture_request` for a `t0` the device never nominated. The GCQuad CSV row becomes a `ShotLink` with `basis: arrival_pairing`, `confirmed_by: observer` — never a Candidate (8.1). |
| Spec | `CORE` §6.3, §7, §8.1–8.2, §8.4, §8.5f |
| Unlocks | CT-I7, CT-I8, CT-I18, CT-I20, CT-I21, CT-I35, CT-S5 (host), interop rows 1, 5, 6, 7, 8 |
| Depends | L9, L10 |
| Status | ☐ |

### H6 — Rendezvous, host side

| | |
|---|---|
| Deliverable | Publish a pairing code: fresh `psk`/`sid` per code from a CSPRNG, every reachable address in `ep` (wired, wireless, hotspot), `mu: 1`, `exp` short, optional `wifi`; rendered as a QR at ECC ≥ M in a "Pair a device" panel. Outstanding-code table with single-use and close-invalidation (7.3a, b, e). mDNS **browse only** (querier role, never binding 5353): resolve `rid` against persisted pairings (`PRK` in the OS keychain/secret store, opt-in and revocable, 7.4b), dial on match, never dial an unresolved `rid` (3.4c). Discovery failure is silent fallback (3.6). Diagnostic export provably free of secrets and payloads (RT-9). |
| Spec | `RV` §3, §4, §5, §7 |
| Unlocks | RT-5, RT-6, RT-7 (browser half), RT-8, RT-9, RT-12 (review), RT-15, RT-16 (review) |
| Depends | L12, H1 |
| Status | ☐ |

### H7 — Markup and annotations

| | |
|---|---|
| Deliverable | Receive `annotation` from a device and persist losslessly against the Shot; send host-authored lines/planes back with `stream_id` naming the view; supersession by the library; nothing in Analysis reads an Annotation (I37 — asserted by the absence of an include). |
| Depends | L11 |
| Unlocks | CT-I37 (host) |
| Status | ☐ |

### H8 — Conformance claim

| | |
|---|---|
| Deliverable | `docs/ppcp-conformance.md` stating the profile set, the `ppcp-conform` command that reproduces it, and the results pasted in matrix row format. A `ctest` target that runs `ppcp-conform` against a headless `ppcp_host_peer` over loopback. |
| Depends | L14, H1–H7 |
| Unlocks | The PinPointStudio column of the matrix |
| Status | ☐ |

---

## 6. Work packages — PinPointCapture (team D)

Prefix **D**. The device. Consumes `libppcp` by SwiftPM. All protocol state lives in `CaptureCore`; all platform types stay in `Sources/Platform` (REQ-PORT-3, held by `LayerPurityTests`).

### D0 — Consume `libppcp`

| | |
|---|---|
| Deliverable | `Packages/Core/Package.swift` gains `.package(path: "../../../libppcp")` (co-development) with the git URL recorded for later; `CaptureCore` depends on `CPPCP`. `LayerPurityTests` updated to permit `CPPCP` and still forbid every platform framework. `swift test` green on the host. |
| Depends | L0 |
| Status | ☑ done — S1 (SwiftPM identity is the directory name `libppcp`, product `CPPCP`) |

### D1 — Transport: two `NWConnection`s, TLS-PSK

| | |
|---|---|
| Deliverable | `Sources/Platform/Network/PpcpTransport.swift` — a connector (scanner dials) and a listener (for the discovery path), each producing **two** ordered byte streams, each an `NWConnection` with `NWProtocolTLS` configured with `K_tls` and the 17-octet binary PSK identity (5.3f — no transcoding), minimum TLS 1.2, **offering every mode the platform exposes** and recording the negotiated version and suite (5.4k, RT-17). No plaintext fallback. A neutral `ByteChannel` protocol in `CaptureCore` is what the peer adapter sees (REQ-PORT-3). Loopback test against itself with the `RV` §10 vectors. |
| Spec | `CORE` §3; `RV` §5.2, §5.3, §5.4b1–b2 |
| Unlocks | RT-4 (device half), RT-10, RT-14 (wire half), RT-17 (review) |
| Depends | L12 API (stub with §10 vectors until it lands) |
| Status | ☑ done — S1 |

### D2 — Device peer adapter and declaration from AVFoundation

| | |
|---|---|
| Deliverable | `CaptureCore/Ppcp/DevicePeer.swift` wrapping `ppcp_peer` in `role: capture` with the device profile set; clock from `mach_continuous_time` as `tb:hosttime` (`kind: continuous`, `epoch_stable: true`, `resolution_ns` measured), with a `wall` timebase declared for `Session.epoch` only; discontinuity detection on wake. **Declaration** generated from the existing `DeviceCapability`/`AVFoundationCaptureDevice` enumeration: one Source per **physical lens** (`optics`, 5.6d), each `CaptureProfile` with `convention: nominal_frame_start`, `frame_start_to_exposure_offset_ns: 0` **`provenance: assumed`**, `rolling_shutter { readout_ns from DeviceProfiles.json, readout_provenance: assumed, direction, rows }`, `intrinsics: per_frame`; the microphone as a Source on the **same** `tb:hosttime` (I4); IMU as a Source. `measured` only from a real self-test, `cold_sample` at onboarding (I28). `viewpoint` with `method` and conditional `confidence`. The existing Swift capability types become views over the library's structs. **D1's listener is reworked to bind streams by `link_bind` (`ENC` §2.1) and arrival-order assembly removed; the connector sends `link_bind` first on every stream.** |
| Spec | `CORE` §5.2–5.8, §5.6.1, §6.4; A12, A13 |
| Unlocks | CT-I4, CT-I19, CT-I22, CT-I28, CT-I31, CT-S7 assertions 1–3 |
| Depends | L4, L6 |
| Status | ☑ done — S2 (`ebfbc48`). `make test-core` 89/89. CT-I4/I19/I22/I28 pass (device); CT-S7 (1–2) pass, (3–4) D4/L13. Listener still assembles links in its own actor, not `ppcp_link_binder` (see §9 finding). `tb:hosttime` declared `monotonic` (mach_absolute_time), `tb:continuous` separately — F-D2-1, orchestrator question |

### D3 — Session store as bundle writer

| | |
|---|---|
| Deliverable | `CaptureCore/Store/SessionStore.swift` — each Session is a `PPCPBNDL` written through `ppcp_bundle_writer`: `session_open` with the device's timebase and **no** arbitration parameters, `stream_open` for each Stream, `readiness`, `candidate`, `shot`, `capture_announce`, manifest before payload, payload frames referencing clip files as they land. **No `arm`/`disarm` in a hostless bundle** (7.3b). Per-shot sync state (`onDevice`/`sending`/`inStudio`) is now the library's `transfer` axis; **`inStudio` is set only by `capture_committed`**. Export = hand the file to a transport (AirDrop/Files/USB) — the existing `SessionLibraryScreen` reads its list from here. |
| Spec | `CORE` §9, §7.3b, §5.14; `ENC` §7; A9 |
| Unlocks | CT-S4 assertion 1 (the bundle half), CT-I12, CT-I34, interop row "device, no host → bundle" |
| Depends | L8 |
| Status | ☑ done — S2 (`ebfbc48`). CT-I12/I34 pass (device); CT-S4 (1) bundle half passes. Nothing in `Sources/` composes a `DevicePeer`/`SessionBundleWriter` yet (D4/D6/D8); `SessionLibraryScreen` still takes one `Session`. `make test-app` hang fixed in `5ffa2ad` (uncancellable continuations in the listener — §9) |

### D4 — Capture path into Captures

| | |
|---|---|
| Deliverable | The existing fragment ring buffer (`REQ-BUF-1`) extracts a clip around `t0` into a shot-anchored Capture with `interval`, `completeness`, `achieved_summary` (frame count, drops, realised rate, exposure/ISO summary, thermal timeline) on announce and `achieved_frames` (per-frame `frames.ns`, `exposure_ns` with **honest** `exposure_provenance` — `per_frame` only if AVFoundation attaches it to the buffer, else `sampled`/`locked_constant`; `intrinsics` per frame from `isCameraIntrinsicMatrixDeliveryEnabled`) for `payload_begin`. Scalar form under lock. `absent` with `outside_buffer` when the interval is gone. Continuous `metadata` Stream for attitude/gravity as stream-anchored segments with coverage (I36); a `preview` Stream as live-only with `not_retained` absent segments. Readiness as a measurement (no state name crosses the wire, 5.15a); `interruption` on call/background with the gap recorded. |
| Spec | `CORE` §5.8, §5.11, §5.14, §5.15, §7.3, §8.4 |
| Unlocks | CT-I2, CT-I10, CT-I11, CT-I17, CT-I27, CT-I30, CT-I36, CT-I36a, CT-S1 (device) |
| Depends | L3, L7, D2 |
| Status | ☐ |

### D5 — Detect and Mint

| | |
|---|---|
| Deliverable | The acoustic onset detector (existing or minimal — accuracy is out of scope, `CONF` §6) emits **every** Candidate: `basis: acoustic`, `at` canonical (the library converts; for a microphone Source `convention: mid` so it is the raw instant after `tof_correction` with both value and sigma), `classifier` taxonomy, `evidence_capture_id` naming a candidate-anchored audio Capture on a separate `audio` Stream (5.12.1a) retained under an application bound with its absence asserted. Promotion policy as a `CaptureCore` callback into the library's Mint engine; hostless → Shot per promoted Candidate, `authority: device`; with a host → nominate and hold until the 8.2i deadline, mint only what would have been promoted (I32), never without an affine relation to `timebase_ref` (8.2i1). Candidate audio retention statement in the app (B7). |
| Spec | `CORE` §5.12, §8.1, §8.2i–j, §8.3; `MSG` §7 |
| Unlocks | CT-I6, CT-I8, CT-I23/CT-S4 (2, 3, 5, 6), CT-I26, CT-I29, CT-I32, CT-I33 |
| Depends | L10, D4 |
| Status | ☐ |

### D6 — Live link: sync, heartbeat, transfer queue, zero-host regime

| | |
|---|---|
| Deliverable | Sync responder (`t2`/`t3` as close to the socket as `Network.framework` allows, 6.1c) and prober per timebase; `heartbeat_ack` carrying `ProcessInfo.thermalState` mapped to the ordinal vocabulary, free storage, battery; `HostLinkState` (`connected`/`weak`/`lost`/`resyncing`) now driven by the library's liveness. Transfer queue on the bulk channel: announce immediately, payload queued, resumable from last ack, `already_present` honoured, preview never queued; **eviction only through the library's I38 predicate**, refuse to arm under storage pressure rather than shed (5.14g1, REQ-OFF-2). Link loss → mint locally, queue, `session_resume` with `minted_shots` and `pending_captures`, sync burst **before** queued payload resumes (4.3b), reconcile by `shot_link`. Capture never stops for any of this (7.4d). |
| Spec | `CORE` §6.3, §7.4, §8.3f–h, §5.14g; `MSG` §4.3, §5.4, §6, §8 |
| Unlocks | CT-I18 (device), CT-I21, CT-I38, CT-S4 assertion 7, CT-S5 (device), interop rows 1, 7, 8, 9 |
| Depends | L9, L10, D1, D3 |
| Status | ☐ |

### D7 — Rendezvous, device side

| | |
|---|---|
| Deliverable | Scan a `ppcp:` code (the existing `PairingView`): decode via the library, unknown `v` → "needs a newer app" (4.2b), expired → "expired" unless the clock is untrusted (4.4a/a1), `wifi` → `NEHotspotConfiguration` with consent **before** the endpoint walk (4.3f, 6a) and removal on session end or left to the user (6b), then walk `ep` in order. Secrets in the Keychain; `PRK` persisted only opt-in, visible, revocable, never from `mu > 1` (7.4). mDNS **advertise** `_ppcp._tcp` as `PPCP-<rid[0..3]>` with the TXT of 3.3 and nothing else, `rn` rotated every registration and ≤ 15 min; a listener for the discovery path. Local-network-permission denial detected and explained (`LocalNetworkBlockedView`, `RV` §8). Payloads never logged or exported (4.4c, 7.2b). |
| Spec | `RV` §2, §3, §4, §6, §7 |
| Unlocks | RT-3, RT-6, RT-7, RT-8, RT-9, RT-12 (review), RT-13 (review), RT-15, RT-16 (review) |
| Depends | L12, D1 |
| Status | ☐ |

### D8 — Markup

| | |
|---|---|
| Deliverable | Device-authored annotations (a stub drawing is enough) sent with `stream_id` for view-specific kinds, coalesced while dragging (5.18i); host annotations received and stored opaque; `nav_anchor` as `device_advisory` from the impact fiducial, never persisted as phase data. |
| Depends | L11 |
| Unlocks | CT-I37 (device) |
| Status | ☐ |

### D9 — Conformance harness mode and claim

| | |
|---|---|
| Deliverable | A debug-only "conformance harness" entry (beside the existing `DebugScreenGallery`) that runs the device peer over **plaintext** loopback sockets (the `direct` path) so `ppcp-conform` can drive it in the simulator without TLS or a QR; `docs/ppcp-conformance.md` with the profile set, the command, and the rows. A `make conform` target. |
| Depends | L14, D1–D8 |
| Unlocks | The PinPointCapture column of the matrix |
| Status | ☐ |

---

## 7. Sessions and gates

Each session runs one agent per repo in parallel. A session ends when every agent has reported, the orchestrator has updated this file and the matrix, and each repo is committed to `main`. Work a session could not finish rolls into the next one with its status recorded.

**Team L runs one step ahead of H and D by construction**: in every session H and D build against the `libppcp` API that landed in the *previous* session, and do platform work that needs no library in the same session. That is what keeps three agents independent and still parallel.

| Session | `libppcp` (L) | PinPointStudio (H) | PinPointCapture (D) | Gate to leave the session |
|---|---|---|---|---|
| **S1 — foundations** | L0, L1, L2, L3; **L12** (pulled forward — it has no dependency on the peer engine and both apps need its API for transport work); stub `include/ppcp/ppcp.h` listing every planned public symbol with a one-line contract, so H and D can code against it | H0 *(needs L0 — sequence inside the session: H starts on H1 while L0 lands)*, H1 | D0 *(same)*, D1 | `ctest` green in `libppcp`; ENC §5.1 and CORE §6.1.1 examples reproduce; RV §10 vectors reproduce; both transports complete a loopback TLS-PSK handshake with the §10 `K_tls` and report the negotiated mode |
| **S2 — the bundle path** ☑ closed 22 Aug (after a crash and recovery run — §9) | L4, L5, L6, L7, L8 | H2, H3 | D2, D3 | A hostless bundle written by `libppcp` tests imports into PinPointStudio idempotently; PinPointCapture writes a bundle from its real declaration on a simulator and `libppcp` reads it back; CT-I1/3/4/13/22/27/28/29/31 passing in `libppcp` |
| **S3 — the live path** (started 22 Aug; wave 1 = L9–L11 ∥ H4 ∥ D4, wave 2 = L13 ∥ H5, H7 ∥ D5, D6, D8) | L9, L10, L11, L13 (+ **L9 queue** from §9: drain partial-write, `session_manifest` originator, link-binder channel from header, Swift note on `ppcp_msg`; and **offline session offer** — `session_offer`/`session_accept`/`session_manifest` as peer originators plus bundle replay onto a live link, so a connected device can offer its stored sessions) | H4, H5 (+ the **offer list** UI: sessions a connected device offers, chosen in-app), H7 | D4, D5, D6 (+ **offering stored bundles** on connect), D8 | `ppcp-sim` ↔ `libppcp` full session; PinPointStudio establishes a session with `ppcp-sim` over H1 and arbitrates; PinPointCapture (simulator) establishes with `ppcp-sim` over D1, nominates and mints; CT-S1, S3, S4, S5, S6, S7 passing in `libppcp` |
| **S4 — conformance and rendezvous** | L14, L15, L16 | H6, H8 | D7, D9 | All three claim files exist and every matrix cell is one of *passing*, *n/a by profile*, *blocked: rig*, or has a named blocker |
| **S5 — interoperability and freeze** | L17; run every non-rig interop row in `CONF` §5 with the real pairs (PinPointStudio ↔ `ppcp-sim` as a foreign host; PinPointCapture no-host → bundle → PinPointStudio; PinPointStudio ↔ PinPointCapture on the simulator over loopback) | fixes from interop | fixes from interop | Freeze-readiness report written; errata in the specification; remaining open rows are rig or product decisions |

**Cross-team sync points inside a session** (the orchestrator relays; agents never message each other):
- When L0 lands, H and D are told the package is consumable.
- When the stub `ppcp.h` lands in S1, H and D are given its path and build against it; a symbol an app needs that the stub lacks is reported to the orchestrator, who adds it to L's queue — never implemented app-side.
- Any specification defect an agent finds is reported, not fixed in place; the orchestrator queues it for L17 and records it in §9 below.

---

## 8. Compliance tracking

**[`docs/conformance/matrix.md`](../conformance/matrix.md) is the compliance record.** One row per test — CT-I1…I38 (+I36a), CT-S1…S7, RT-1…RT-17, IOP-1…IOP-10 — and three columns, one per implementation. Each cell is exactly one of:

| Cell | Meaning |
|---|---|
| `—` | not started |
| `impl` | code exists; the test has not been run or does not pass |
| `pass` | the test passes; the claim file names the command |
| `n/a` | the implementation does not declare the profile, and the **negative** test passes (`CONF` §1d) |
| `rig` | blocked on the LED timecode rig (`CONF` §2d) — CT-S2, and parts of CT-I31 |
| `review` | an `RV` *review* method — verified by a named reviewer reading named code, with the commit recorded |
| `blocked: …` | anything else, with the reason |

The orchestrator updates the matrix at the end of every session from the agents' reports and from the JSON emitted by `ppcp-conform`. Nobody marks a cell `pass` by hand: it comes from a command that can be re-run.

Each implementation's **claim file** is the human-readable form: profile set, the command, the rows, and the date. The two application claim files live in their own repositories because the claim is theirs; the matrix in `libppcp` links to them.

---

## 9. Decisions, findings and errata log

Append-only. Newest last.

| Date | Raised by | Item | Disposition |
|---|---|---|---|
| 2026-08-22 | plan | A1–A13 above are decided for the programme | Stated; reverse by editing §3 and recording here |
| 2026-08-22 | plan | `RV` B7 — candidate audio retention bound and its user-visible statement is an **application** obligation | Assigned to D5; PinPointStudio has no candidate audio retention to state |
| 2026-08-22 | plan | `RV` B13 — whether the absence of forward secrecy is user-visible | **Product question for the user.** Both apps surface the negotiated mode (5.4k) in their diagnostic/About output so either answer is one line of UI away |
| 2026-08-22 | user | Requirements OPEN-4 — PinPointCapture licence | **Closed: MIT**, same as `libppcp`, to keep GPL out of App Store distribution. Ground rule 1 still holds — MIT-to-MIT does not license copying; the library stays the only shared artefact |
| 2026-08-22 | plan | `CORE` B8/B10 — timing defaults and `sampled` exposure accuracy need rig data | Out of scope; the matrix carries `rig` cells |
| 2026-08-22 | plan | `SessionLink` (`CORE` B2) | Type and message implemented in L4/L5 for comprehension (C1); **no implementation originates it** |
| 2026-08-22 | L (S1) | `ENC` §5.1 worked example is not in deterministic key order (`t1` sorts before `type`; `{ns,tb}` not `{tb,ns}`), so a 4e-honouring encoder cannot reproduce it | Erratum queued for L17: re-emit §5.1 deterministically (still 87 bytes) or mark it illustrative. Library carries `ppcp_message_encode_literal` only to reproduce it |
| 2026-08-22 | L (S1) | `CORE` 6.2d names no rounding rule for the row instant | Implemented round-half-away-from-zero; erratum queued: one sentence in 6.2d |
| 2026-08-22 | L (S1) | `RV` 4.3a promises byte-identical codes but does not say whether a defaulted optional (`mu: 1`) is emitted; the §10.3 vector emits it | Erratum queued: state the rule (emit as the vector does) |
| 2026-08-22 | L (S1) | `CORE` 5.3 `Timebase.kind` is not in the §10.3 open-registry list | Implemented closed; confirm in L17 |
| 2026-08-22 | H (S1) | **`CORE` §3 / `ENC` §2 say nothing about how a listener associates one peer's several TCP connections, or which is channel 0.** H groups by the pairing the PSK identity resolved to and orders by the dialler's serialised handshakes; D uses arrival order. Both work against themselves; they need a clause to meet | **Resolved as erratum E1** (user decision, 22 Aug): explicit `link_bind { link_id, channel }` as the first frame on every stream — `ENC` §2.1, `MSG` §3.0. L implements it in L5/L6 (S2); H adapts H1's listener in H2; D adapts D1's listener in D2. Both implicit rules are withdrawn |
| 2026-08-22 | H (S1) | **`RV` 5.3f binary identity fails through OpenSSL's TLS 1.2 PSK callbacks** (`strlen`-lengthed): an embedded `0x00` in `rn2` — ~1 connection in 16 — fails the handshake intermittently | Erratum queued: 5.3a excludes `0x00` from `rn2` bytes (cheapest; survives on both platforms). Pinned by a test in PinPointStudio |
| 2026-08-22 | D (S1) | **`RV` 5.3a/5.3b cannot be served by a Network.framework *listener*** — no server-side PSK resolver hook; a rotating per-connection identity is refused with `PSK_IDENTITY_NOT_FOUND`. The required pairing-code path (device dials) is unaffected; the discovery path where `RV` 3.5b recommends the device advertise is not implementable on iOS as written | Erratum queued: 3.5b becomes a MAY for a peer whose platform cannot resolve identities server-side, with the host advertising instead; D7 implements discovery with the **device browsing and dialling** (roles reversed from 3.5b) |
| 2026-08-22 | D (S1) | **`RV` 5.3c uniform failure is unachievable on iOS** (different alerts, different timing) — **narrowed on re-test**: because `K_tls` and `K_id` share one `PRK`, a wrong secret also produces an unresolvable identity, so the resolved-identity/wrong-key case cannot be reached by a scanned code, a persisted pairing or an attacker without `PRK`. The gap is real only for a future key schedule that separates the two | Note in L17 against 5.3c/5.3d; RT-11 stays `n/a` on the device's code path |
| 2026-08-22 | H, D (S1) | RT-17 (`review` method) needs a named human reviewer; an author cannot discharge it | **For the user to assign** |
| 2026-08-22 | L (S2) | **`ENC` 5a collides with eight message bodies.** `session_open`, `session_joined`, `session_resume`, `session_state`, `session_close`, `session_offer`, `session_accept` and `session_manifest` all define a body field named `session_id`, which 5a reserves. Emitting both produces a duplicate key, which `ENC` 4d makes malformed, so the two cannot coexist | Resolved in the library by **hoisting**: the body's `session_id` IS the envelope's, written once in the envelope position, read back out of the same flat map by the body decoder. Erratum queued for L17: 5a should say the reserved names may be used for the envelope's own purpose, or `MSG` §4/§9 should stop listing `session_id` as a body field |
| 2026-08-22 | L (S2) | **C3 cannot be derived from the message index.** `MSG` §11 tabulates the profile that confers ORIGINATION; the profile a RESPONDER needs is a different thing — `candidate` is conferred by Detect and consumed by Arbitrate, so a host with no Detect must not be told it cannot understand a Candidate | Library answers `profile_not_supported` only to the REQUEST class, from a responder-side table in the engine. Erratum queued for L17 (finding F-L6-1): `MSG` §11 gains a responder column, or §2.2.2 C3 says the rule is about requests |
| 2026-08-22 | L (S2) | **`MSG` 8.1i makes an `absent` preview segment unannounceable.** 8.1i forbids announcing a preview Capture with `transfer: pending`, and `pending` is the default state of every Capture — but the discarded preview segment 5.11c3 *requires* a peer to announce is `completeness: absent`, holds no payload, and has no other transfer state to carry | Library exempts `absent` preview segments: the rule is about queues and an absent Capture has nothing to queue. Erratum queued for L17: 8.1i says "a preview Capture holding payload", or `Capture.transfer` gains a meaning for the payload-less case |
| 2026-08-22 | L (S2) | `ENC` 7d's "partial **only if** the bundle did not assert otherwise, and never upgraded" makes an unasserted, untruncated bundle neither `complete` nor `partial` | Implemented as `unknown`, which is what I10 requires — completeness is asserted, never inferred. The reader reports the assertion and the truncation separately so `CT-I36` (c) and (d), which are the same bytes, stay distinguishable. Confirm the reading in L17 |
| 2026-08-22 | L (S2) | `ppcp_arena_take` aligned on the offset within the region, not on the absolute address, so an arena whose buffer began at an odd address returned misaligned storage for every aggregate | Library defect, not a specification one. Fixed in `src/ppcp_common.c`; found by the L5 catalogue test under UBSan decoding a `declare` |
| 2026-08-22 | orchestrator (S2) | **Session S2 crashed the build machine at 15:22.** The H agent built the PinPointStudio *application* with a bare `-j` (unlimited jobs) while the D agent looped `swift build` polling for L's headers and L was building; 16 GB was exhausted and the Mac rebooted at 16:26. The PinPointStudio build notes already said Ninja `-j8`; the agent, spawned from the `libppcp` session, had never read them | **Ground rule 7** added to §1; §10 now runs L ahead of H/D when they depend on it and briefs each agent with its own repo's notes. Recovery run: L alone (`-j4`), then H and D in parallel (`-j3` each). All uncommitted work survived on disk; nothing was lost |
| 2026-08-22 | user | Commit/push to `main` without per-change approval in PinPointStudio and PinPointCapture | **Granted for this programme's H*/D* packages only**; those repos otherwise keep their per-change rule. Recorded in each repo's memory |
| 2026-08-22 | H (S2) | **`ENC` §6 / `CORE` 5.7 — a payload has no declared container.** `payload_begin` carries `bytes`, `digest`, `chunk_bytes` and nothing saying what the bytes are; `format.codec` is a codec, not a container, and is three hops away. A receiver writing a clip to disk must guess the extension | Erratum queued for L17: a `container` (or media type) on `payload_begin` or on the Capture |
| 2026-08-22 | H (S2) | **`ENC` §7 does not require a bundle to carry `declare`**, yet `CORE` 8.5c scopes Capture identity by the minting peer and a bundle states that nowhere else; a bundle of bare `capture_announce` frames is unattributable and so un-deduplicable | Erratum queued for L17: §7 requires `declare` before any Capture-bearing frame, alongside 7c |
| 2026-08-22 | H (S2) | **`ppcp_peer_drain()` has no partial-write counterpart.** It dequeues whole frames and assumes the embedding wrote all of them; a short socket write under `CORE` T2 backpressure loses bytes the engine considers sent | **L9 queue**: a `drain` told how much was taken, or peek/commit |
| 2026-08-22 | H (S2) | The `libppcp` probe in `tests/cmake/PinPointTests.cmake` failed closed since H1 (FetchContent scope), so `ppcp_host_peer_test` had asserted the engine could not be built | Fixed in PinPointStudio `5fafe6f`; H2 re-verified against L6 |
| 2026-08-22 | D (S2) | **`ppcp_msg` (~48 KB) imports into Swift with the union as computed members**, so `msg.body.x.y = z` copies the whole union through a stack temporary — an ordinary synchronous test hit SIGBUS. The workable pattern (heap-allocate, pointer to `body`, `withMemoryRebound`) is not discoverable | **L9 queue**: note in `message.h`; consider accessor functions for the large arms |
| 2026-08-22 | D (S2) | **`bundle.h` has no originator for `session_manifest`**, the one message `ENC` 7c makes mandatory in a bundle; every other frame has a `ppcp_peer_*` entry point | **L9 queue**: `ppcp_peer_session_manifest(...)` |
| 2026-08-22 | D (S2) | **`ppcp_link_binder_offer`'s `stream_channel` cannot be supplied by a stream-per-connection transport** — a freshly accepted TCP connection carries no channel number outside the `link_bind` frame header the library then checks it against | **L9 queue**: read the channel from the header inside `offer`. Until then the device listener assembles links in its own actor |
| 2026-08-22 | D (S2) | **F-D2-1: `tb:hosttime` cannot be `mach_continuous_time`** as D2's text says — AVFoundation stamps with `mach_absolute_time`, which halts across sleep. D declares `tb:hosttime` as `monotonic` and a separate `tb:continuous` for `CORE` 5.5b | **Accepted**: D2's text is amended by this entry; a capture device declares what its frames are actually stamped with. A13 unchanged |
| 2026-08-22 | D (S2) | CT-I28's device test asserted two `measured` capabilities where `CORE` 5.6d makes a distinct lens a distinct Source; the correct count is one | Test corrected; row still passes |
| 2026-08-22 | D (S2) | `make test-app` hung in `LinkBindLoopbackTests` ("a first frame that is not link_bind closes the stream"): `accept()`, `channelBound()` and the three `NWConnection` awaits parked on continuations with no cancellation handling, so no timeout could ever fire — a live production bug too (a peer that completed TLS then said nothing would park an intake task forever) | **Fixed** PinPointCapture `5ffa2ad`: cancellation-aware waiters, connection torn down on cancel. 14/14 app tests, 89/89 core. No RT row moved |
| 2026-08-22 | user | **Sessions are not imported from files.** H3 shipped a menu item and a file picker; PinPointStudio has no menus and no native dialogs, and the user's intent is that a *connected* capture device offers its recorded sessions and the host chooses from a list | **Decided**: the bundle path is the engine, never a UI. The user-facing flow is `MSG` §9 `session_offer`/`session_accept`/`session_manifest` over the live link — S3, H4–H7 (and D4–D6 on the device side, which must be able to offer its stored bundles). H3's UI removed (PinPointStudio `00f50e2`); H3 stays done on its CT rows |

---

## 10. How an orchestration session is run

For the orchestrator, so every session is run the same way.

1. Read this file and the matrix. Build the three agent briefs from the session row in §7: the work package text verbatim, the ground rules of §1, the decisions of §3, the paths of the specification, and — for H and D — the path of `libppcp`'s public headers **and that repo's own `CLAUDE.md` and memory index, with its build recipe pasted verbatim and the job cap from rule 7**.
2. Launch the agents, each with its working directory set to its own repository. Run them in parallel only when the session row's packages are independent; when H and D depend on an L package that has not landed (S2: L6–L8), run L first and launch H and D when it is on `main`. Three agents in parallel means `-j3` each. Each brief ends with the same reporting contract: *what landed (files, commits), which matrix rows moved and to what state with the reproducing command, what was found wrong in the specification or in another team's API, what is unfinished.*
3. Relay cross-team items as they arrive (§7). Never let an agent fix another repo's problem in its own.
4. When all three report: verify the gate, update §4–§6 status boxes, update the matrix from the reports and the `ppcp-conform` output, append to §9, and commit each repo to `main`.
5. Record in memory where the session stopped.
