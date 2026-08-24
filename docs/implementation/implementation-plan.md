# PPCP implementation plan — three teams, one protocol

**The tracker for bringing PPCP 1.0 and PPCP-RV 1.0 into `libppcp`, PinPointStudio and PinPointCapture.**

| | |
|---|---|
| Status | **Programme complete — five sessions closed 22–23 Aug 2026.** Remaining items are in `docs/conformance/freeze-readiness.md`. This file is the record |
| Successor | ⚠ **This plan is closed and is not amended.** Work arising from change request CR-01 (RV-6, guided pairing) is tracked separately in [`cr-01-implementation-plan.md`](cr-01-implementation-plan.md), which continues the L/H/D numbering from L18, H9 and D10 so identifiers stay unique across both |
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
7. **Builds are bounded, and an agent reads its repo's build notes before its first build.** On 22 August 2026 a full PinPointStudio app build launched with a bare `-j` (unlimited jobs), concurrent with a `swift build` polling loop in PinPointCapture and a `libppcp` build, exhausted memory and crashed the machine; forty minutes of session S2 went with it. So: every build command carries an explicit job count (`-j3` per agent when three run in parallel; never bare `-j`); no agent loops a build command waiting on another repo — poll `git log`, build once; and an agent working in PinPointStudio or PinPointCapture reads that repo's `CLAUDE.md` and `~/.claude/projects/<repo>/memory/MEMORY.md` (and the build notes it indexes) before it builds anything — those notes already said Ninja `-j8`, and the agent that crashed the machine had never seen them.

   ⚠ **Amended 24 August 2026 by the maintainer: the prohibition on an agent building the PinPointStudio *application* target is DROPPED.** It was added after the crash as a proxy for the real cause, and the real cause is now known and fixed — **unbounded parallelism** (`jom -j` / a bare `-j`), not the app build. **Everything else in this rule stands and stands harder**: an explicit job count on every command, the total at or below 8, no build loops, and the repo's build notes read first. ⛔ **Note that Ninja with no `-j` is itself unbounded** — it defaults to cores + 2, which is 12 on this machine — so an app build needs its `-j` *more* than a Makefiles one, not less.
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
| Status | ☑ done — S3. `ctest --preset dev -R test_ct_i21` (and `--preset san`). CT-I18, CT-I21 and CT-S5 **pass**; CT-S4 assertion 7 passes inside the same binary. The **L9 queue** landed with it — `ctest --preset dev -R test_l9_queue`: drain peek/commit, `session_offer`/`session_accept`/`session_manifest` originators, `ppcp_bundle_replay` honouring `have_digests` (9.1a), `ppcp_link_binder_offer` taking the channel from the frame header, and the Swift note in `message.h`. Three items the H4 agent raised also landed here (F-H4-1, the event channel, F-H4-2) |

### L10 — Detect, Mint, Arbitrate

| | |
|---|---|
| Deliverable | **Detect**: `candidate` emission requires a declared Source with a declared Timebase (I26); `Candidate.at` is canonical, converted by the library from the nominator's raw instant + profile + exposure (I33), `tof_correction` both-or-neither (I29), every candidate emitted (7.1d). **Mint**: promotion is a **callback** (I14); no window; one Candidate per Shot, `authority: device`; the 8.2i deadline (`issue_hold_ns + heartbeat_interval_ms`) with the promotion condition (I32); 8.2i1 — no affine relation to `timebase_ref` → no Shot; `shot` sent immediately. **Arbitrate** (role host only, I20): convert candidates via current relations (no second canonical conversion), exclude-and-retain on missing/unrelated/uncertain (policy callback), coincidence grouping within `coincidence_window_ns`, issue no earlier than `issue_hold_ns` and no later than the mint deadline (8.2h), late candidates attach without moving `t0` (I7), two same-basis candidates from different peers both retained (I8), 8.2k attach to a device-minted Shot, 8.2l `shared_candidate` link. Shot extension merge that is additive and order-independent (5.13d–e). `ShotLink` with `confirmed_by` rules. `capture_request` served with `outside_buffer` as a result not an error (8.4). |
| Spec | `CORE` §5.12, §5.13, §5.16, §8 |
| Unlocks | CT-I6, CT-I7, CT-I8, CT-I9, CT-I23/CT-S4 (1–6), CT-I26, CT-I29, CT-I32, CT-I33, CT-I35 |
| Status | ☑ done — S3. `ctest --preset dev -R test_ct_s4` (Detect and Mint) and `-R test_ct_i35` (Arbitrate). CT-I6, I7, I8, I9, I23/CT-S4, I26, I29, I32, I33 and I35 **pass**; CT-I20's arbitration half is re-asserted (`ppcp_arbiter_new` refuses a non-host). One library decision the specification does not make: **which contributing Candidate sets `t0`** — see §9 |

### L11 — Markup

| | |
|---|---|
| Deliverable | `annotation` either direction; opaque `body` ≤ 8 KiB round-tripped byte-identical; supersession by `id`, `revision`, then bytewise `author_peer_id` — total order, converges in both delivery orders; `at` timebase per 5.18g; no API path from an Annotation to any computed quantity (I37, by surface). |
| Spec | `CORE` §5.18; `MSG` §9.0 |
| Unlocks | CT-I37 |
| Status | ☑ done — S3. `ctest --preset dev -R test_ct_i37` and `ctest --preset dev -R CT-I18-api-surface`. CT-I37 **passes**: the equal-revision race converges in both delivery orders, an unrecognised `format` round-trips byte-identical, and I37's "by API surface" half is a scan that refuses any signature putting an Annotation and a Shot, Candidate, Calibration or TimebaseRelation together. `planned.h` is now empty of declarations |

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
| Status | ☑ **done — S3 wave 2.** `ctest --preset dev` is 40/40 and `ctest --preset san` is 40/40. `tools/ppcp-sim` is a peer over two TCP connections presenting any declaration from JSON; `tools/scenarios/` holds eleven declarations, fourteen scenarios and a README mapping each to the row it serves; `tools/run-pair.sh` runs two of them over loopback and `ctest --preset dev -R sockets` runs the fourteen rows registered in `tests/CMakeLists.txt`. **The eight paired rows the claim file named now each have a socket twin** (CT-I7, I8, I12, I18, I20, I21, I34, S5), and three more the package existed to unlock landed with them: **CT-S6 moves to `pass`** — assertion 1's "and arbitrates over the result" is `CT-S6-sockets-arbitrate` and was unwritable before this tool — **CT-I24 moves to `pass`** with it, and CT-S4 assertion 6 gains `CT-S4-sockets-silent-host`. Two `CONF` §5 pairings run as harness rows (`IOP-5` unrelated, `IOP-9` continuous+preview) and five more are harness-ready for the real pairs in S5. `--psk-ke-only` is hand-built (no OpenSSL, no dependency) and validated in **both** directions by two ctest rows against `openssl s_server`. **Still owed by this package:** nothing. **Not moved and why:** CT-S3 and CT-S7 stay `—` for `libppcp` — both are stated over an implementation under test converting instants against a synthetic peer, and L13 delivered the peer (`foreign-capture.json`, `measured-capture.json`) rather than the conversion, which is L15's C test and the applications' own rows |

### L14 — Conformance tool (`tools/ppcp-conform`)

| | |
|---|---|
| Deliverable | Drives a peer-under-test over its two sockets (or a bundle file) through the *paired* and *injected* scenarios, asserting on the wire, and emits a machine-readable result (JSON) plus a Markdown fragment in the exact row format of [`matrix.md`](../conformance/matrix.md). Takes the claimed profile set as input and applies `CONF` §1b–d: positive tests for declared profiles, negative tests for undeclared ones. |
| Spec | `CONF` §1, §3, §4 |
| Unlocks | The *passing* state for the two application columns of the matrix |
| Status | ☑ **done — S4.** `tools/ppcp-conform` drives a peer under test over its two sockets — `--connect`, `--listen`, or `--self` for the reference pairing — through the *paired* and *injected* rows, asserting on the wire through `ppcp-sim`, and emits JSON plus a Markdown fragment in the matrix's row format. `--profiles` is the claim (1a): declared profiles run positive rows (1b), undeclared ones run negative rows (1d) and pass as `n/a`. It RUNS `ppcp-sim`, one process per row, so every verdict carries the command that produced it. Exit 0/1/2/**3**, the last for "no row applied", because reporting zero failures about an empty run is the failure mode it exists to avoid. `ctest -R L14-conform` is the reference run, both roles. **Three deliberate refusals**, each documented where an implementer reads it: only `paired` and `injected` rows (a `static` or `fixture` row is answered better by the implementation's own suite); **CT-I34 is dropped** although the matrix calls it paired, because nothing on the wire distinguishes an importer that de-duplicated from one that imported twice; and `--self` will not run a negative row, because the stand-in reads a declaration FILE whose profile set is whatever that file says — it passed by luck the first time it was tried. **Not delivered:** a TLS-PSK transport. `ppcp-sim`'s `--psk-ke-only` is a hand-built ClientHello for RT-4 and speaks no application data, so a TLS-1.2-PSK-only device cannot be driven; the honest route is the device's own plaintext harness path (D9), which erratum **E4** was written to make conformant |

### L15 — Reference conformance run

| | |
|---|---|
| Deliverable | Every CT row with method static/fixture/injected implemented as a C test; every paired row run `ppcp-sim ↔ libppcp` through `ppcp-conform`; `claim-libppcp.md` generated. Fixture bundles under `tests/fixtures/` for CT-I2, I11, I12, I13, I15, I34, I36. |
| Spec | `CONF` §3, §4 |
| Unlocks | The `libppcp` column of the matrix |
| Status | ☑ **done — S4.** **Part A**, the §9 queue: all seven findings the two application teams raised in S3 are closed, each with its commit in §9. **Part B**: eight fixture bundles under `tests/fixtures/`, written by `tools/ppcp-fixtures`, checked in, and read back from disk by `tests/test_fixtures.c` — which is a different claim from a test that builds bytes and reads them back in the same process: it proves the format did not move. `L15-fixtures-stable` regenerates them and compares byte for byte, so `ENC` 4e is asserted rather than assumed. `docs/conformance/claim-libppcp.md` is **generated** by `tools/make-claim.sh` from a live `ctest` run and `ppcp-conform`'s own JSON; the authored prose lives in `tools/claim/head.md` and `tools/claim/tail.md` and the file says which is which. The generator refuses to write a claim from a red run. **CT-S3 and CT-S7 are now writable and written** — `foreign-capture.json` and `measured-capture.json` through `ppcp-conform`, which is what L13 delivered the peer for. **Owed by this package and not delivered:** a fixture for CT-I11's *shot-windowed* half (gaps rejected on a `shot_windowed` Stream is a static assertion the fixture does not make), and CT-I2's second half — that no *consumer* derives a timestamp from position — which is an assertion about an embedding, not about this library |

### L16 — Specification audits as tooling

| | |
|---|---|
| Deliverable | `tools/audit-profile-boundary` — reads the L5 message table and the normative clause list, asserts every clause that requires originating a message is bound to a profile that confers it (`CONF` 5b1). `tools/audit-adjacent-must` — a checklist generator for the sweep of `CONF` 5b2, keyed by revision diff. Both run in `ctest`. |
| Spec | `CONF` §5b1, §5b2 |
| Unlocks | Freeze readiness |
| Status | ☑ **done — S4.** `tools/audit-profile-boundary` reads the message catalogue from `PPCP-MSG` §11 **and** from `src/ppcp_message.c` and asserts they agree message for message, profile for profile, clause for clause — 45 each way — then scans every normative clause in the set for one requiring a peer to originate a message no profile confers. It fails the build. `tools/audit-adjacent-must` groups all 466 normative clauses by the section that binds them and, keyed by a list of changed clause ids, emits only the sections those land in; `docs/conformance/adjacent-must-sweep-s4.md` is this session's, for E2/E3/E4. Both run in `ctest` (`L16-profile-boundary`, `L16-adjacent-must`). **What neither does:** decide. A contradiction between two MUSTs is a question about meaning; the audit removes the excuse for sweeping from memory, which is what failed four times, and it does not remove the sweep |

### L17 — Errata and freeze report

| | |
|---|---|
| Deliverable | Every specification defect found during L1–L16 and by teams H and D recorded in `docs/specification/` change history as errata with the clause amended; `README.md` status line updated; a freeze-readiness report listing the matrix rows still open and why (rig, product decision, second implementation). |
| Spec | `CORE` §0, Annex A.4, Annex B0 |
| Status | ☑ **done.** Wave 1 (`d043135`…`ba1cbcb`): **errata E5–E29** into the normative text with `CORE`'s errata table, `README.md` and both change histories; **E1–E4 verified** in the clause text; **E24–E27** decided and marked reversible; the `CONF` 5b2 sweep of all 45 messages, whose answer is now a column in `MSG` §11 that the 5b1 audit asserts on every run; CT-I6's minting-capture counterpart; and three cross-team findings closed in session (**F-S5-3** + E28, **F-S5-1** + E29, **F-S5-2**). Wave 2: **F-S5-6** fixed (`ppcp-sim` calls `ppcp_arbiter_reconsider()` at both sites, so E29 is observable through the tool and PinPointCapture can drop its hold), **F-S5-5** recorded, both application columns of the matrix re-read from their S5 claim files, and [`docs/conformance/freeze-readiness.md`](../conformance/freeze-readiness.md) written. **Recommendation: freeze the documents, do not declare `ppcp/1.0` stable** — five blocking conditions, of which two are a phone and two are a named reviewer. 49/49 green. |

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
| Status | ☑ done — S2 (`b025904`, `63eb71b`; box filled 23 Aug after re-verification — S2's crash close-out missed it). `ctest --test-dir build/ppcp-tests -R 'ppcp_host_peer_test|ppcp_source_declaration_test'` 2/2: CT-I14, CT-I19, CT-S3 (host side) pass. The 120 fps floor is the ingest callback; own Sources declared from `CameraCapabilities`; listener binds by `link_bind`. Re-verified against L6 in S2 recovery and against L9–L15 in S3/S4 |

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
| Status | ☑ done — S3 (`ea31994`). `ctest --test-dir build/ppcp-tests -R ppcp_video_input_test`. CT-I36a host pass; CT-S1 host impl (assertion 4 is `capture_request`, H5). Clips leave as `clipReady(PpcpClip)`, not `videoFrameReady` — arrival stamping would destroy the canonical instant. App target unverified |

### H5 — Live session: sync, heartbeat, arm, arbitration bridge

| | |
|---|---|
| Deliverable | Session open with `tb:host` as `timebase_ref` and both arbitration parameters; sync prober per device timebase and **per host timebase** (I21 — the host with several cameras on independent clocks runs it per clock); heartbeat at the session interval reporting degradation to the UI; `arm`/`disarm` wired to the existing SHOT/armed flow. **Arbitration bridge**: the existing acoustic and IMU shot detectors nominate as Candidates from host-owned Sources into the library's Arbitrate engine; the existing arbiter is replaced by, not layered over, `ppcp` arbitration for any session containing a PPCP peer; `capture_request` for a `t0` the device never nominated. The GCQuad CSV row becomes a `ShotLink` with `basis: arrival_pairing`, `confirmed_by: observer` — never a Candidate (8.1). |
| Spec | `CORE` §6.3, §7, §8.1–8.2, §8.4, §8.5f |
| Unlocks | CT-I7, CT-I8, CT-I18, CT-I20, CT-I21, CT-I35, CT-S5 (host), interop rows 1, 5, 6, 7, 8 |
| Depends | L9, L10 |
| Status | ☑ done — S3 (`17e192a`…`53fca4b`). `ctest --test-dir build/ppcp-tests` 10/10. CT-I7/I8/I20/I21 pass, CT-I18 negative half, CT-I35/CT-S5 impl (no RTT distribution in-process). Offer list in-screen under DEVICES (`PpcpOfferList.qml`, `ppcpOffers`). **Nothing constructs a `PpcpHostPeer` yet** — `registerPpcpPeer`, `setTimebaseOffsetNs`, `clipReady` joins have no caller (S4 H6/H8; `clipReady` blocked on host item 2). `tof_correction` never sent (unmeasured). App target unverified |

### H6 — Rendezvous, host side

| | |
|---|---|
| Deliverable | Publish a pairing code: fresh `psk`/`sid` per code from a CSPRNG, every reachable address in `ep` (wired, wireless, hotspot), `mu: 1`, `exp` short, optional `wifi`; rendered as a QR at ECC ≥ M in a "Pair a device" panel. Outstanding-code table with single-use and close-invalidation (7.3a, b, e). mDNS **browse only** (querier role, never binding 5353): resolve `rid` against persisted pairings (`PRK` in the OS keychain/secret store, opt-in and revocable, 7.4b), dial on match, never dial an unresolved `rid` (3.4c). Discovery failure is silent fallback (3.6). Diagnostic export provably free of secrets and payloads (RT-9). |
| Spec | `RV` §3, §4, §5, §7 |
| Unlocks | RT-5, RT-6, RT-7 (browser half), RT-8, RT-9, RT-12 (review), RT-15, RT-16 (review) |
| Depends | L12, H1 |
| Status | ☑ done — S4 (`6b9b1af`). `ctest --test-dir build/ppcp-tests -R ppcp_rendezvous_test`; RT-5/7/8/9/15/16 pass, RT-6/13 n/a, RT-12 review. Found erratum E3 (`mu` counts pairings). Keychain store and `DNSServiceBrowse` compiled, not exercised at runtime; browser not yet started by `PpcpHostService` |

### H7 — Markup and annotations

| | |
|---|---|
| Deliverable | Receive `annotation` from a device and persist losslessly against the Shot; send host-authored lines/planes back with `stream_id` naming the view; supersession by the library; nothing in Analysis reads an Annotation (I37 — asserted by the absence of an include). |
| Depends | L11 |
| Unlocks | CT-I37 (host) |
| Status | ☑ done — S3 (`ppcp_annotation_store`, `ppcp_annotation_test`). CT-I37 host pass; I37 asserted by grep of src/Analysis |

### H8 — Conformance claim

| | |
|---|---|
| Deliverable | `docs/ppcp-conformance.md` stating the profile set, the `ppcp-conform` command that reproduces it, and the results pasted in matrix row format. A `ctest` target that runs `ppcp-conform` against a headless `ppcp_host_peer` over loopback. |
| Depends | L14, H1–H7 |
| Unlocks | The PinPointStudio column of the matrix |
| Status | ☑ done — S4 (`f2b2522`…`e4710f2`). `ctest --test-dir build/ppcp-tests -R ppcp_conformance`: 11/12 applicable rows pass via `ppcp-conform` against headless `ppcp_conform_host` over a harness-only plaintext channel (`PP_PPCP_PLAINTEXT_HARNESS`, default OFF, fatal under shipping). CT-I6 `impl` — the tool picks a host counterpart (§9). Found and fixed four host defects: no `declare` ever sent, listener dropped bytes after `link_bind`, `syncTimebase` unset, one engine for all links |

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
| Status | ☑ done — S3 (`30478bb`…`86b6d62`). `make test-core`. CT-I2/I10/I11/I27/I30/I31/I36/I36a pass (own halves), CT-S1(6), CT-S7(3). **No ring buffer pre-existed** — REQ-BUF-1 was only a requirement; built in CaptureCore. `RingBufferRecorder` AVAssetWriter wiring has never run on a real camera |

### D5 — Detect and Mint

| | |
|---|---|
| Deliverable | The acoustic onset detector (existing or minimal — accuracy is out of scope, `CONF` §6) emits **every** Candidate: `basis: acoustic`, `at` canonical (the library converts; for a microphone Source `convention: mid` so it is the raw instant after `tof_correction` with both value and sigma), `classifier` taxonomy, `evidence_capture_id` naming a candidate-anchored audio Capture on a separate `audio` Stream (5.12.1a) retained under an application bound with its absence asserted. Promotion policy as a `CaptureCore` callback into the library's Mint engine; hostless → Shot per promoted Candidate, `authority: device`; with a host → nominate and hold until the 8.2i deadline, mint only what would have been promoted (I32), never without an affine relation to `timebase_ref` (8.2i1). Candidate audio retention statement in the app (B7). |
| Spec | `CORE` §5.12, §8.1, §8.2i–j, §8.3; `MSG` §7 |
| Unlocks | CT-I6, CT-I8, CT-I23/CT-S4 (2, 3, 5, 6), CT-I26, CT-I29, CT-I32, CT-I33 |
| Depends | L10, D4 |
| Status | ☑ done — S3 (`49e7ece`, `f8f9657`). CT-I6/I26/I29 pass; CT-I8/I23/I33 own half; CT-I32 impl. `MicrophoneOnsetSource` never run on a real microphone |

### D6 — Live link: sync, heartbeat, transfer queue, zero-host regime

| | |
|---|---|
| Deliverable | Sync responder (`t2`/`t3` as close to the socket as `Network.framework` allows, 6.1c) and prober per timebase; `heartbeat_ack` carrying `ProcessInfo.thermalState` mapped to the ordinal vocabulary, free storage, battery; `HostLinkState` (`connected`/`weak`/`lost`/`resyncing`) now driven by the library's liveness. Transfer queue on the bulk channel: announce immediately, payload queued, resumable from last ack, `already_present` honoured, preview never queued; **eviction only through the library's I38 predicate**, refuse to arm under storage pressure rather than shed (5.14g1, REQ-OFF-2). Link loss → mint locally, queue, `session_resume` with `minted_shots` and `pending_captures`, sync burst **before** queued payload resumes (4.3b), reconcile by `shot_link`. Capture never stops for any of this (7.4d). |
| Spec | `CORE` §6.3, §7.4, §8.3f–h, §5.14g; `MSG` §4.3, §5.4, §6, §8 |
| Unlocks | CT-I18 (device), CT-I21, CT-I38, CT-S4 assertion 7, CT-S5 (device), interop rows 1, 7, 8, 9 |
| Depends | L9, L10, D1, D3 |
| Status | ☑ done — S3 (`…125296b`). `make test-core` 145/145, `make test-app` 23/23. CT-I18/I21/I38 own half; CT-S4 (1,2,3,5) pass; listener on `ppcp_link_binder` (F-D3-3 closed). Offers stored sessions on connect, replays via `ppcp_bundle_replay`. CT-S5/S4(6)/interop 1,7,8,9 blocked on D9 (sim speaks plaintext or TLS1.3 psk_ke; device can do neither). **`AppModel` does not compose `DetectAndMint`/`SessionOfferService`/`PreviewProducer` yet** |

### D7 — Rendezvous, device side

| | |
|---|---|
| Deliverable | Scan a `ppcp:` code (the existing `PairingView`): decode via the library, unknown `v` → "needs a newer app" (4.2b), expired → "expired" unless the clock is untrusted (4.4a/a1), `wifi` → `NEHotspotConfiguration` with consent **before** the endpoint walk (4.3f, 6a) and removal on session end or left to the user (6b), then walk `ep` in order. Secrets in the Keychain; `PRK` persisted only opt-in, visible, revocable, never from `mu > 1` (7.4). mDNS **advertise** `_ppcp._tcp` as `PPCP-<rid[0..3]>` with the TXT of 3.3 and nothing else, `rn` rotated every registration and ≤ 15 min; a listener for the discovery path. Local-network-permission denial detected and explained (`LocalNetworkBlockedView`, `RV` §8). Payloads never logged or exported (4.4c, 7.2b). |
| Spec | `RV` §2, §3, §4, §6, §7 |
| Unlocks | RT-3, RT-6, RT-7, RT-8, RT-9, RT-12 (review), RT-13 (review), RT-15, RT-16 (review) |
| Depends | L12, D1 |
| Status | ☑ done — S4 (`d732f8f`, `dd6e556`). `make test-core`: RT-3/6/7/8/9 pass, RT-15 impl, RT-12/13/16 review. Mic-to-ball distance setting in (§9 decision). Keychain ThisDeviceOnly and HotspotConfiguration entitlement unverified on a phone |

### D8 — Markup

| | |
|---|---|
| Deliverable | Device-authored annotations (a stub drawing is enough) sent with `stream_id` for view-specific kinds, coalesced while dragging (5.18i); host annotations received and stored opaque; `nav_anchor` as `device_advisory` from the impact fiducial, never persisted as phase data. |
| Depends | L11 |
| Unlocks | CT-I37 (device) |
| Status | ☑ done — S3. CT-I37 device pass |

### D9 — Conformance harness mode and claim

| | |
|---|---|
| Deliverable | A debug-only "conformance harness" entry (beside the existing `DebugScreenGallery`) that runs the device peer over **plaintext** loopback sockets (the `direct` path) so `ppcp-conform` can drive it in the simulator without TLS or a QR; `docs/ppcp-conformance.md` with the profile set, the command, and the rows. A `make conform` target. |
| Depends | L14, D1–D8 |
| Unlocks | The PinPointCapture column of the matrix |
| Status | ☑ done — S4 (`b9667df`, `fe08d11`, `b8e8153`). `make conform` exit 0: CT-S4, CT-I35, CT-I18 pass, CT-I20n n/a, via `ppcp-conform` against the DEBUG plaintext harness (`PpcpDirectTransport`, no key material can reach it). CT-S4(6) pass after the device started publishing its own relation estimate (6.1f). 166 core / 25 app tests |

---

## 7. Sessions and gates

Each session runs one agent per repo in parallel. A session ends when every agent has reported, the orchestrator has updated this file and the matrix, and each repo is committed to `main`. Work a session could not finish rolls into the next one with its status recorded.

**Team L runs one step ahead of H and D by construction**: in every session H and D build against the `libppcp` API that landed in the *previous* session, and do platform work that needs no library in the same session. That is what keeps three agents independent and still parallel.

| Session | `libppcp` (L) | PinPointStudio (H) | PinPointCapture (D) | Gate to leave the session |
|---|---|---|---|---|
| **S1 — foundations** | L0, L1, L2, L3; **L12** (pulled forward — it has no dependency on the peer engine and both apps need its API for transport work); stub `include/ppcp/ppcp.h` listing every planned public symbol with a one-line contract, so H and D can code against it | H0 *(needs L0 — sequence inside the session: H starts on H1 while L0 lands)*, H1 | D0 *(same)*, D1 | `ctest` green in `libppcp`; ENC §5.1 and CORE §6.1.1 examples reproduce; RV §10 vectors reproduce; both transports complete a loopback TLS-PSK handshake with the §10 `K_tls` and report the negotiated mode |
| **S2 — the bundle path** ☑ closed 22 Aug (after a crash and recovery run — §9) | L4, L5, L6, L7, L8 | H2, H3 | D2, D3 | A hostless bundle written by `libppcp` tests imports into PinPointStudio idempotently; PinPointCapture writes a bundle from its real declaration on a simulator and `libppcp` reads it back; CT-I1/3/4/13/22/27/28/29/31 passing in `libppcp` |
| **S3 — the live path** ☑ closed 23 Aug ( wave 1 = L9–L11 ∥ H4 ∥ D4, wave 2 = L13 ∥ H5, H7 ∥ D5, D6, D8) | L9, L10, L11, L13 (+ **L9 queue** from §9: drain partial-write, `session_manifest` originator, link-binder channel from header, Swift note on `ppcp_msg`; and **offline session offer** — `session_offer`/`session_accept`/`session_manifest` as peer originators plus bundle replay onto a live link, so a connected device can offer its stored sessions) | H4, H5 (+ the **offer list** UI: sessions a connected device offers, chosen in-app), H7 | D4, D5, D6 (+ **offering stored bundles** on connect), D8 | `ppcp-sim` ↔ `libppcp` full session; PinPointStudio establishes a session with `ppcp-sim` over H1 and arbitrates; PinPointCapture (simulator) establishes with `ppcp-sim` over D1, nominates and mints; CT-S1, S3, S4, S5, S6, S7 passing in `libppcp` |
| **S4 — conformance and rendezvous** ☑ closed 23 Aug ( wave 1 = L15→L14→L16 ∥ H-compose + H6 ∥ D-compose + D7 + D9-harness; wave 2 = H8 ∥ D9-claim) | L15 (the §9 queue first), L14, L16 | **H-compose** (construct `PpcpHostPeer` in the app; wire `registerPpcpPeer`/`setTimebaseOffsetNs`; syntax-only ctest row over app-side `src/Ppcp/*.cpp`), H6, H8 | **D-compose** (`AppModel` composes `DetectAndMint`/`SessionOfferService`/`PreviewProducer`), D7 (+ mic-to-ball distance setting, §9), D9 | All three claim files exist and every matrix cell is one of *passing*, *n/a by profile*, *blocked: rig*, or has a named blocker |
| **S5 — interoperability and freeze** ☑ closed 23 Aug ( wave 1 = L17 errata + conform CT-I6 fix ∥ H: IOP-4..9 vs `ppcp-sim`, IOP-3/10 import half, TLS listener script ∥ D: IOP-2 vs `ppcp-sim`, IOP-3/10 bundles; wave 2 = the real PPS ↔ PPC pair over TLS on loopback (IOP-1, 6, 10), then L17 freeze report) | L17; run every non-rig interop row in `CONF` §5 with the real pairs (PinPointStudio ↔ `ppcp-sim` as a foreign host; PinPointCapture no-host → bundle → PinPointStudio; PinPointStudio ↔ PinPointCapture on the simulator over loopback) | fixes from interop | fixes from interop | Freeze-readiness report written; errata in the specification; remaining open rows are rig or product decisions |

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
| 2026-08-22 | L (S3) | **`CONF` §4.4 assertion 1 asks a hostless Session to run `arm` end to end**, and `CORE` 7.3b forbids recording one: `arm` is conferred by Live, and a bundle with nobody controlling carries the effect (Streams, `readiness`, Captures) rather than a command nobody sent. The same defect is already in `CONF` 5b1's own list of four | Erratum queued for L17: strike `arm` from CT-S4 assertion 1, or say it applies only where a host is present. The library refuses it both ways — `ppcp_bundle_writer_append_msg` on a hostless bundle and `ppcp_peer_arm` on a non-host — and the test asserts both refusals |
| 2026-08-22 | L (S3) | **`CORE` §8.2 does not say WHICH contributing Candidate sets `t0`.** 8.2b groups them and 8.2h says when to issue, but nothing names the winner, and two conformant hosts can therefore issue different `t0` for one event — which I7 then freezes | **Library decision**, recorded rather than assumed: the Candidate with the smallest combined timing uncertainty (the relation's `offset_sigma_ns` at that instant, widened by `tof_correction.sigma_ns` where present), ties broken by the earliest instant so the choice is order-independent. That is 8.2h's own rationale read as a rule — a sample-accurate acoustic nomination should win over a fast IMU one. `Candidate.confidence` is deliberately **not** consulted: it is a belief that the event happened, not a statement about when, and using it would put a quality judgement in the protocol layer (I14). Erratum queued: 8.2 states a rule or states that the choice is the host's |
| 2026-08-22 | L (S3) | **`MSG` §11's `stream_close` lists `closed_at` unmarked while `CORE` 5.11 has `Stream.closed_at` at 0..1**, and 5.1d lets the CONSUMER close a Stream whose timebase is the owner's clock — which the consumer has no reading of (F-H4-2, raised by H) | Library encodes `closed_at` when present and tolerates its absence on receipt; `ppcp_peer_stream_close()` accepts NULL. Erratum queued for L17: either 5.11a1 names the closing peer's timebase for `closed_at`, or `MSG` §11 marks it optional. Until then a consumer-originated close is only expressible under the second reading |
| 2026-08-22 | L (S3) | **5.11j was enforced outbound but not inbound** (F-H4-1, raised by H): `peer_handle`'s `capture_announce` arm passed `is_preview: false` unconditionally, so a receiver either re-ran `ppcp_capture_validate_in_stream` itself or silently accepted the one announce 5.11j says it never sees | Library defect, not a specification one. Fixed: the Stream's `kind` is resolved from `Capture.stream_id` against the Streams the engine recorded, and a preview Capture announced `pending` is answered `error`/`malformed` without closing |
| 2026-08-22 | H (S3) | `ppcp_event` did not carry the channel a frame arrived on, so `CORE` 5.11h — preview payload on a bulk channel **distinct** from shot payload — was unverifiable by a receiver | Added `ppcp_event.channel`, zero for an event the engine raised itself (link lost, link restored) rather than decoded |
| 2026-08-22 | L (S3) | **`CORE` 7.4b makes `heartbeat_ack`'s thermal, storage and battery mandatory, and a library holding none of them cannot answer honestly** | A peer configured with no `health_report` callback answers `error`/`profile_not_supported` rather than an ack full of zeroes — a fabricated `nominal` is exactly the "silently accepting worse data" 7.4b exists to prevent. Same shape for `sync_probe` with no declared `sync_timebase`: 6.1b requires the responder's own declared clock and there is no honest substitute. No erratum; recorded because it is a visible refusal an implementer meeting it will want explained |
| 2026-08-22 | D (S3) | **F-D4-1: `ppcp_peer_capture_announce` never called `ppcp_capture_validate_in_stream`** although the engine holds `p->streams`, so a peer could originate `{stream: true}` on a `shot_windowed` Stream it had opened itself, or gaps on one — CT-I27's second assertion and CT-I11's negative half unenforced at origination. The origination-side twin of F-H4-1 | Library defect, not a specification one. Fixed with F-H4-1: where the engine has seen the named Stream opened, the Capture is validated against it before the frame is queued, and `is_preview` must agree with the Stream rather than being a claim taken on trust. A Stream the engine never saw opened is still allowed, because 8.4b lets a peer announce an `absent` Capture for an interval on a Stream that has closed |
| 2026-08-22 | D (S3) | **F-D4-2: `CORE` §5.14 — an `absent` shot-anchored Capture may not carry an `interval`**, so a hostless `outside_buffer` announce cannot say WHICH span it lost. 5.14d makes the interval mandatory for a stream-anchored segment and absent otherwise, and 8.4b's answer is shot-anchored | Erratum queued for L17. The information a consumer wants — "the buffer no longer reached back that far" — has nowhere to go, and `absent_reason: outside_buffer` alone does not carry it |
| 2026-08-22 | D (S3) | **F-D4-3: `CORE` §5.8 `AchievedSummary` is camera vocabulary, yet 5.11b requires stream-anchored Captures on every `continuous` Stream.** A 100 Hz attitude stream has no frames, no exposure and no ISO, so the summary a segment is supposed to carry does not apply to it | Erratum queued for L17: either `AchievedSummary` becomes optional for a non-framed Stream, or 5.8 gains a form that says "samples" rather than "frames" |
| 2026-08-22 | D (S3) | **F-D4-4: `CONF` editorial.** CT-S1 says "the other four" over six assertions; CT-S4 (1) lists `arm` in the zero-host path against `CORE` 7.3b (the same defect L raised independently, above); and "I36a" is written as an invariant when §3 is giving I36 a second test | Editorial errata queued for L17. The library's tests follow the corrected readings and say so at each site |
| 2026-08-22 | L (S3) | **F-L13-1: a burst feed silently loses events.** `ppcp_peer_feed()` consumes as many whole frames as the caller's buffer holds; the event ring is `PPCP_PEER_EVENT_QUEUE` (four) deep and overflow drops the OLDEST event with nothing the embedding can read to discover it. One socket read carrying a replayed bundle — `session_open`, `declare`, `stream_open`, `capture_announce`, `session_manifest`, three payload frames — lost the `capture_announce` at the receiver while its payload frames arrived | **Library defect, unfixed, and it reaches both applications**: PinPointStudio's bundle import feeds a whole file through this path, and a device offering a stored Session over a live link produces exactly this burst. `ppcp-sim` works around it by feeding ONE FRAME PER FEED and draining events between, which is what every embedding must do today. Two candidate fixes for L15: `feed` stops consuming while the event queue is full and reports what it took (which matches the "the engine buffers nothing" design), or a dropped-event counter the embedding can read. Relayed to H and D |
| 2026-08-22 | L (S3) | **F-L13-2: the `psk_ke`-only mode was a false pass twice, and both looked like the host refusal RT-4 is trying to observe.** (i) OpenSSL's client always advertises `psk_dhe_ke`, and forcing an empty group list makes the LOCAL stack refuse to build a ClientHello ("no suitable groups") — nothing reached the wire and the tool reported success. (ii) With the ClientHello hand-built, the first server answer was `missing_extension` for the absent `signature_algorithms`, a refusal about the wrong thing, reported the same way | **Tool defect, fixed.** `tools/ppcp-sim/sim_tls.c` now builds the ClientHello byte by byte and computes the PSK binder with the library's own HKDF-SHA256 and HMAC-SHA256 (RFC 8446 §4.2.11.2, §7.1), so the tool has no dependency and neither does the library. The mode is validated in BOTH directions by `RT-4-psk-ke-only-refused` and `RT-4-psk-ke-only-accepted-is-a-failure` against `openssl s_server`, with and without `-allow_no_dhe_kex`. The second row is also what proves the binder is right — a wrong binder yields an alert, not a ServerHello carrying `pre_shared_key`. **The general lesson is worth keeping**: a negative-conformance tool that can only ever report "refused" is not evidence, because it reports refused for a malformed offer too |
| 2026-08-22 | L (S3) | **`CONF` §2c is met, and the part of it that is not.** `ppcp-sim` presents a declaration different from the implementer's own — a different `timing.convention`, a `global` geometry, `provenance: measured` with a non-zero offset, `unrelated` relations, three clocks, a foreign profile set — and the eight paired rows now run against it over sockets | Recorded rather than assumed: **both ends are still `libppcp`.** The foreignness is in the DECLARATION, not in the implementation, so `CONF` 5c stays open and a bug shared by both ends of a link is still invisible. What changed is that a hardcoded convention, an assumed-zero offset, a composed relation or a missing profile check now has something to disagree with. `ppcp-sim` refuses a run on four checks that exist only because there are two ends: a revised `t0` (I7), a message originated by a peer whose declared profiles do not confer it (I24), `authority: host` from a peer declaring `role: capture` (I20), and a held relation spanning two clocks of one peer (I18) |
| 2026-08-22 | L (S3) | **The `late-host` scenario needed retuning before it exercised 8.2k at all.** With the host's arbitration delayed by 2 s and the device's mint deadline at `issue_hold_ns + heartbeat_interval_ms` = 1.2 s after the Candidate's instant, the two landed 70 ms apart and the host won the race — so the run looked like a normal arbitration and asserted nothing about I35 | Not a defect in anything; recorded because it is the shape of an interoperability test that passes for the wrong reason. The delay is now 3 s and the run shows what I35 is about: the device mints, the host attaches rather than issuing a second Shot, `issued` stays 0. Anyone writing IOP-8 against a real host in S5 needs the same margin |
| 2026-08-23 | orchestrator (S3) | **Session S3 closed.** Wave 1: L9–L11, H4, D4. Wave 2: L13, H5+H7 (+ offer list), D5+D6+D8 (+ stored-session offers). The session hit its usage limit mid-wave-2; every agent had been told to commit after each green step and the cut-off lost nothing | All three repos on `main`. **L15 queue** assembled below. S4 next: L14, L15, L16; H6, H8; D7, D9 — with the two **composition gaps** first: nothing constructs `PpcpHostPeer` in PinPointStudio, and `AppModel` does not compose `DetectAndMint`/`SessionOfferService`/`PreviewProducer` in PinPointCapture |
| 2026-08-23 | plan | **D4's premise was wrong**: PinPointCapture had no fragment ring buffer; REQ-BUF-1 was a requirement only | Built in `CaptureCore` (`FragmentRing`, extraction, coverage). `RingBufferRecorder`'s AVAssetWriter segment delivery has never run on a real camera (simulator has no 150 fps camera) — **user to exercise on a phone** |
| 2026-08-23 | L (S3) | **F-L13-1 — `ppcp_peer_feed()` consumes unboundedly many frames per call and the 4-deep event ring drops the OLDEST event silently** on overflow. One socket read carrying a replayed bundle lost `capture_announce`. Both apps now feed one frame per call and drain between; PinPointStudio carries a test that goes red when this is fixed. D adds: `ppcp_bundle_reader_feed` drains the sink's answers but not its events, so a bundle replayed into a live peer overflows the same way | **L15**: feed stops when the queue is full and reports what it took, or a dropped-event counter; and the reader feed path must surface events |
| 2026-08-23 | L (S3) | `ppcp-sim`'s `psk_ke`-only mode was a false pass twice (OpenSSL refused locally; then a missing `signature_algorithms`). Now hand-built on the library's HKDF/HMAC and proven both ways against `openssl s_server` | Lesson recorded: a negative-conformance tool that can only say "refused" is not evidence. `CONF` 2c met; 5c (a foreign *implementation*) stays open until S5's real pairs |
| 2026-08-23 | H (S3) | **F-H5-2 — `ppcp_peer_session_params()` is NULL on the peer that originated `session_open`**; a host cannot read back `timebase_ref`, `coincidence_window_ns`, `issue_hold_ns` (8.2b, 8.2h) and keeps a drifting second copy. D finds the same root (**F-D6-3**): `has_session_params` not set on the originating path, so `ppcp_peer_zero_host()` is false for CORE 4.1b's hostless case — minting works only because absent parameters read as zero | **L15**: set session params on origination; `zero_host` derived from the absence of arbitration parameters, not from the path |
| 2026-08-23 | H (S3) | **F-H5-1 — the remote half of I21 is unreachable**: a responder answers with its single `sync_timebase` and estimators key on the local timebase, so a host cannot probe two clocks of one device | **L15**: per-timebase responder (`sync_probe.timebase_id` echoed), estimator keyed on (local, remote) |
| 2026-08-23 | H (S3) | **F-H5-3** — `health_report` is a precondition for liveness: without it every `heartbeat` is answered `profile_not_supported` and §7.4 never runs. Cost an hour and two wrongly-raised defects | **L15**: document in `peer.h`; consider refusing `ppcp_peer_init` with Live declared and no `health_report` |
| 2026-08-23 | H (S3) | `tof_correction` is never sent by the host: 8.1d wants it and this host does not measure microphone-to-ball distance | Nothing invented. **Product question**: a host-side distance setting, or hosts nominate without ToF |
| 2026-08-23 | D (S3) | **F-D5-1** — `ppcp_mint_pump` gives no way to learn which Shots it minted (the arbiter has `ppcp_arbiter_shot_at`); D decodes its own queued frames via `drain_peek` | **L15**: `ppcp_mint_shot_at` |
| 2026-08-23 | D (S3) | **F-D6-1 — no `ppcp_peer_session_resume()`** for a message MSG 4.3a makes a MUST; it is the only `ppcp_msg` the device builds by hand, and one of the large arms | **L15**: originator |
| 2026-08-23 | D (S3) | **F-D6-2** — the sync responder cannot stamp `t2`/`t3` near the socket, and 6.1c's escape (`t3 == t2` as a declaration) is not expressible | **L15**: `ppcp_peer_sync_reply_stamps(t2, t3)` or a callback at the responder |
| 2026-08-23 | D (S3) | **F-D6-4** — `ppcp-sim` speaks plaintext or TLS 1.3 `psk_ke`; the device can reach neither (Network.framework: TLS 1.2 PSK only, RV 5.4b1). CT-S5 device, CT-S4(6), interop 1/7/8/9 are blocked on **D9's `direct` path**, not on libppcp | D9 first in S4-D |
| 2026-08-23 | D (S3) | libppcp's F-D4-1 fix correctly broke a device fixture (`segment` on a `shot_windowed` Stream, wrong since D3) — the engine had not checked | Fixture corrected; recorded as evidence the fix works |
| 2026-08-23 | H, D (S3) | Both apps' `ppcp-sim` use was limited: H's sandbox refused a binary outside its repo; D cannot speak its TLS. Rows against the sim are libppcp's only this session | S4: `ppcp-conform` (L14) drives the apps from outside through their real transports (A11), which is the intended route |
| 2026-08-23 | user | **PinPointCapture gets a microphone-to-ball distance setting.** D5's `AcousticTimeOfFlight` (distance + sigma, 343 m/s) exists but nothing in the app supplies a distance, so every device Candidate goes out without `tof_correction` | **Decided.** S4 **D7**: an in-app setting (per session, persisted with a sensible default and a sigma reflecting that it is a user estimate, not a measurement) feeding `CandidateFactory`; recorded in the session bundle so the correction is reproducible. The host-side question (PinPointStudio's own microphone) stays open |

| 2026-08-23 | L (S4) | **F-L13-1 closed** (`27b40c4`). `ppcp_peer_feed()` stops before a frame whose events would not fit — two slots of headroom, because a `hello` raises two — reports what it consumed, and `ppcp_peer_feed_stalled()` tells "no event room" from "not a whole frame yet". `ppcp_bundle_reader_feed()` does the same against its sink, with `ppcp_bundle_reader_stalled()`; the frame that did not fit is not delivered, counted or inspected. `ppcp_peer_events_dropped()` counts what the ring can still lose — only the engine's own tick events now | Closed. **PinPointStudio's `F_L13_1_…` test is EXPECTED to go red**: it asserted the drop. Three libppcp tests went red on the fix and each was the defect showing (I12's replay, L9's short-write round trip, I35's orphan `capture_request`) |
| 2026-08-23 | L (S4) | **F-H5-2 and F-D6-3 closed** (`42a690a`). `ppcp_peer_session_open()` records `session_params` on the ORIGINATING path too, so a host and a hostless device read back `timebase_ref`, `coincidence_window_ns` and `issue_hold_ns`. `ppcp_peer_zero_host()` is derived from the absence of the arbitration parameters on both paths and no longer falls through to the link state | Closed. CT-S4 carried the defect twice as a workaround — a device swapped for a sink fed its own frame, and a SECOND host engine of the same identity — and both are gone |
| 2026-08-23 | L (S4) | **F-H5-1 closed** (`fb5bbb0`), and it needed a specification change: MSG 6.1d addresses the PROBER's clocks and 6.1b leaves the responder's to the responder, so no peer could ask for a named remote clock. **Erratum E2** — `PPCP-MSG` 6.1g, listed in a new errata table in `PPCP-CORE` — makes a probe whose `timebase_id` names a timebase the RESPONDER declared answerable on that timebase. Sequences key on the PAIR: `ppcp_peer_sync_add_target`, `ppcp_peer_sync_probe_to`, `ppcp_peer_sync_observe_to`, `ppcp_peer_sync_estimator_for_pair` | Closed. CT-I21 gains the remote half as a test and it fails with the E2 branch disabled. A responder that does not implement E2 answers on its own clock and the prober sees it — evidence, not breakage |
| 2026-08-23 | L (S4) | **F-D5-1, F-D6-1, F-D6-2 and F-H5-3 closed** (`838b089`). `ppcp_mint_shot_at`/`ppcp_mint_shot_for`; `ppcp_peer_session_resume()` with the outage's minted Shots and pending transfers, adopting the Session locally and arming 4.3b's burst, answered with `session_joined` (which MSG §12 requires and nothing implemented); `ppcp_peer_sync_reply_stamps()` and `ppcp_peer_sync_set_zero_residence()` for 6.1c; `ppcp_peer_new()` REFUSES Live with no `health_report` | Closed. The Live refusal is a behaviour change both apps will meet at construction: five libppcp rigs were declaring Live with no health source. CT-I21's "no health source" case is rewritten as the honest one — a peer whose DECLARATION does not confer Live |

| 2026-08-23 | H (S4) | **F-H6-1 — `RV` 7.3a counted handshakes and `mu` cannot mean handshakes.** A link is two (optionally three) TCP connections each with its own TLS session over one `K_tls` (`CORE` §3.1, `ENC` §2.1), so the default `mu: 1` was spent by the control channel and the bulk channel **of the same link** refused: every conformant pairing died on its second channel. **F-H6-1a**, second-order: 7.3a + 7.5a + 7.5c made a `mu: 1` code permit one link and no reconnection, so §7.5 was dead letter by default | **Erratum E3, written now rather than deferred** (`RV` 7.3a reworded, 7.3f added, 7.5c amended; listed in `CORE`'s errata table). `mu` counts **pairings** and invalidates the **code**, not the pairings made from it. `libppcp` encodes no counter — 7.3a is the publisher's, and the publisher is the embedding — so the only code change is `rv.h` saying "count links" where an implementer reads it |

| 2026-08-23 | D (S4) | **F-D9-1 — `RV` 2c ("there is no unauthenticated path") forbids the plaintext transport `CONF` §2c's own REQUIRED test infrastructure runs over**, while `RV` 9a permits it. Jointly unsatisfiable for a peer that both claims RV and is testable. The device fenced its plaintext harness to DEBUG builds | **Erratum E4, written now** — it is `ppcp-conform`'s premise (A11: the tool drives both applications from outside over their real transports, and the sim is plaintext by design). `RV` 2c1 scopes 2c to the three rendezvous paths and states what still binds a claiming peer on a handed-in socket: no code key material, no persisted `PRK`, no resolvable identifier, not presented as paired, and **not offered in a shipping configuration**. Also fixed with it: **`RV` RT-5 encoded the pre-E3 reading** ("a second handshake with a `mu: 1` code is refused") and would have made a harness certify the bug |
| 2026-08-23 | D (S4) | **F-D7-1** — `RV` 4.4a1's "never synchronised since boot" is not readable on iOS; only the build-date half is implementable. **F-D7-2** — `pv`'s range syntax (`1.0-1.2`) is defined nowhere, and `MSG` 3.1b spells the same idea a third way. **F-D7-3** — a persisted pairing cannot rejoin the network its code named: 4.4c discards the payload, §7.4 persists only `PRK`, 6b removed the configuration, so §7.4's workflow fails at a venue with its own network. **F-D7-4** — 3.4d leaves a device holding several pairings unable to advertise usefully, and 3.5b makes the device the advertising side | **Errata for L17**, all four. Deferred rather than written now because each needs a decision, not a clarification: what a device may substitute for 4.4a1's boot test; one range syntax across `RV` and `MSG`; whether a persisted pairing carries a network hint or the user rejoins; and what a multi-pairing device advertises |

| 2026-08-23 | L (S4) | **L14, L15 and L16 landed.** `ppcp-conform` (the instrument all three implementations are measured by), eight checked-in fixture bundles with a byte-identity check, a **generated** `claim-libppcp.md`, and the two freeze-gate audits in `ctest`. CT-S3 and CT-S7 move to `pass` for `libppcp`: `foreign-capture.json` and `measured-capture.json` through the conformance tool, which is what L13 built those peers for. CT-I2, I11, I13, I15 and I36 move to `pass` on fixtures read back from disk | Recorded. **Three refusals worth carrying into S5**, each documented at the point an implementer meets it: `ppcp-conform` carries only *paired* and *injected* rows; **CT-I34 is not claimable from outside** — nothing on the wire distinguishes an importer that de-duplicated from one that imported twice; and `--self` will not run a `CONF` 1d negative row, because the stand-in's profile set comes from a declaration FILE and not from `--profiles`. It passed by luck the first time it was tried, which is why the refusal is in the code and not in a comment |
| 2026-08-23 | L (S4) | **The audits found nothing broken, and that is the result.** 45 messages agree between `PPCP-MSG` §11 and `src/ppcp_message.c` on profile and clause; no MUST anywhere requires originating a message no profile confers. Two facts fell out of writing it: the specification binds `shot` to a SET (Mint / Arbitrate) while the engine carries one `originating_profile` per message, so the check is membership rather than equality; and **27 of the 45 catalogued messages are required by no MUST in any document** | Both recorded rather than actioned. The second is not a defect — an OPTIONAL message is a real thing — but it is the list a `CONF` 5b2 sweep should start from, because a message nothing requires is a message nothing tests. **L17 input** |
| 2026-08-23 | orchestrator (S4) | **Session S4 closed.** All three claim files exist; both application columns come from `ppcp-conform` over plaintext loopback (E4). Hit the usage limit once mid-wave-2; nothing lost | S5 next: L17 (E1–E4 into the spec + F-D7-1..4 decisions + the CT-I6 counterpart), real-pair interop. **Needs Mark with a phone** (see memory) |
| 2026-08-23 | H (S4) | **`ppcp-conform`'s CT-I6 row picks `reference-host` as the counterpart for a host under test**; CORE 5.2b / MSG 3.2c require `role_conflict` (fatal), so the row dies at `hello` | **L17**: CT-I6 needs a minting-capture counterpart. PinPointStudio's run excuses exactly that row, reason in `run-conform.sh` |
| 2026-08-23 | H (S4) | Four host defects found by the conformance run, all fixed (`3351362`): no `declare` was ever sent (MSG 3.3c); the listener discarded bytes read past `link_bind` so a peer that coalesces `link_bind`+`hello` never got past the handshake; `syncTimebase` unset so every device `sync_probe` was answered `error`; one engine shared by all links so the second device after a drop never got a Session | Evidence that CONF 2c's "foreign" counterpart is what finds the bugs a self-written pair cannot |
| 2026-08-23 | H (S4) | The old `F_L13_1` guard in PinPointStudio was not red after L15's fix — it asserted a property both the defect and the fix satisfy | Rewritten on `events_dropped`/`feed_stalled`. Lesson: a regression guard must fail on the fix |
| 2026-08-23 | D (S4) | CT-S4(6) was blocked on the device never calling `publishRelations()` (6.1f) — three defects, all the device's; the library refused correctly each time and invented nothing | Fixed; row passes via `ppcp-conform` |


| 2026-08-23 | L (S5) | **L17 wave 1: errata E5–E29 into the normative text**, in five commits (`d043135` ENC E5–E9, `8ddb18b` CORE E10–E15, `43fdd4e` MSG E16–E18, `da2cb7a` CONF E19 + RV E20–E27, `fce70e2` README and claim). E1–E4 were re-read and are fully in the clause text, not only in the history. Every S1–S4 item with a disposition "erratum queued for L17" is written; each carries an italic erratum note at the clause, a row in `CORE`'s errata table, and the finding that produced it | Done. Three of them changed the library as well: `ENC` 6g/E7 added `payload_begin.container` (`ppcp_peer_payload_begin_as()`, the old entry point delegating with NULL so no embedding breaks); `ENC` 7h/E9 is enforced in the bundle writer and **went red immediately** in `test_ct_i38` and `test_l9_queue`, both of which built bundles with no `declare` — the unattributable file 8.5c and I34 forbid; `RV` 5.3a1/E21 added `ppcp_rv_psk_identity_draw()` |
| 2026-08-23 | L (S5) | **F-D7-1..4 decided, all four reversible.** **E24**: 4.4a1's "never synchronised since boot" is not readable on iOS, so the trigger becomes three individually-optional tests of which only the build-date one is required — 7.3e bounds a false negative to one round trip. **E25**: one **range** syntax (`LOW-HIGH`, inclusive, within a MAJOR, comma-separated across them) for `pv` and `detail.supported`; `hello.versions` stays an ordered **list**, because an initiator offers rather than describes. **E26**: a persisted pairing MAY keep the network **name** and never the passphrase — without it §7.4's workflow failed at exactly the venue it was written for. **E27**: one advertised instance at a time, rotating on the 15-minute `rn` rotation, recently-used first, and a multi-pairing peer SHOULD browse as well; Annex B3 narrowed | Decided by L17, recorded as reversible in each clause and in `CORE`'s errata table. **Both app teams should read E24–E27**: E25 changes what `pv` and `detail.supported` must contain, and E26 permits a device to persist one field it currently discards |
| 2026-08-23 | L (S5) | **The `CONF` 5b2 sweep of all 45 messages, and what it found.** Of the 27 required by no normative clause: **seven were responses nothing obliged a peer to send** — receive `hello`, `declare`, `session_open`, `stream_open`, `heartbeat` or `sync_probe`, never reply, violate nothing. That is the one real hole and `MSG` 1c closes it. Eight had an obligation that did not name the message ("Emitted whenever a step is observed", "runs a separate probe sequence", "is transferred as"), each now reworded. Fifteen are deliberately optional | **E18**: `MSG` §11 gains a **Required by** column carrying the sweep's answer per message, and `audit-profile-boundary` asserts that column against the documents on every run — a message marked **opt** that some clause has begun to require, or a citation no clause supports, fails `ctest -R L16-profile-boundary`. A sweep written down and never re-checked is the state the column exists to leave behind |
| 2026-08-23 | L (S5) | **Two defects in the freeze gate itself, found by using it.** (i) `audit-profile-boundary` matches its origination verbs with surrounding spaces, so `a peer **emits** \`x\`` hid the verb behind the bold — a gate a bold verb defeats is not a gate. Emphasis is stripped before the line is read. (ii) Adding a column to `MSG` §11 silently switched **off** the clause comparison, which read a fixed field index; it now reads the last field | Both fixed in the same commit as E18. The second is the more interesting: the gate did not fail, it stopped checking, and nothing said so |
| 2026-08-23 | L (S5) | **CT-I6's counterpart (H, S4) fixed.** The row picked `reference-host`, and for a peer under test that is itself a host — which is exactly who a negative Mint row is for — `CORE` 5.2b / `MSG` 3.2c make that `role_conflict`, fatal, so the row died at `hello`. It now runs a **minting capture peer**, and asserts a new `ppcp-sim` counter `minted_shots_rx` rather than `shots_rx`: a host declaring Arbitrate may legitimately send `shot` (the catalogue binds it to the SET Mint / Arbitrate) and under 8.2k it re-sends the DEVICE's Shot unchanged. What Mint confers is issuing on one's **own** authority | `a371748`. New `CT-I6-sockets` row runs the pairing in `libppcp`'s own suite. **PinPointStudio can drop the excuse in `run-conform.sh` and re-run the row** |
| 2026-08-23 | L (S5) | **The `L14-conform` flake, chased rather than re-run.** Both peers exit at the same `--run-ms`, so whichever reached it first reset the other's socket and the loser reported "read on channel 0 failed: Connection reset by peer" as a **protocol violation** — one run in three under load, never alone | `a371748`. `ECONNRESET`/`EPIPE` is an orderly end of run; a reset arriving too early is still caught, by the `--expect` counters coming up short. Six consecutive clean runs |
| 2026-08-23 | H, D (S5) | **F-S5-3 — an offered Session replayed onto a live link rebound the LIVE Session's `timebase_ref`, silently.** `peer_on_session_open` guarded `CORE` 4.1a / I16 only where the incoming `session_id` **matched** — 4.1a is written about "the same `session_id`" and read literally guards nothing against a different one. A replayed bundle carries the stored Session's own `session_open`, so `session_id`, `timebase_ref` and `session_params` were rebound to the exporting device's. Observed on the host: every subsequent `shot` carried `t0` in the device's `tb:hosttime` while the live Session declared `tb:host`, so the device's own conversion became the identity; and the arbiter showed `candidates_foreign 4, adopted 4, groups 2` where the device nominated 2 live. Nothing malformed, no error at either end | **Closed** `a371748`, and it is the most serious finding of the programme after E1. Library: a different `session_id` opens a **second** Session context and the live one is untouched; `ppcp_event.imported` tells an embedding which Session a frame belongs to; `ppcp_peer_imported_session_id/_timebase_ref/_session_params` read it. **Erratum E28** (`MSG` 4.1a1, 9.1b) states it normatively. Guard: `F-S5-3-sockets-offer-during-live-session`, verified **red** with the fix disabled. **Both apps must route imported frames by `ppcp_event.imported`** and must not feed them to a live arbiter |
| 2026-08-23 | H, D (S5) | **F-S5-1 — an arbiter never revisited a Candidate excluded for want of a relation.** On a live link the relation always arrives late, because §6.3's burst is still converging while the first swings are taken, so a peer that nominated early was silently unarbitrated for the whole Session — no error, no Shot, every Candidate present and retained exactly as 8.2d requires | **Decided: it MUST be revisited.** **Erratum E29** (`CORE` 8.2d1): reconsidered when a relation becomes available — joins its group before issue, attaches under 8.2e with `t0` unrevised after. 8.2h's bound on *issuing* is untouched. `ppcp_arbiter_reconsider()` implements it and the header says the embedding must call it on a relation change, because the library owns no event loop (`9571b01`). **Both apps must call it** from their `relation_update` handling |
| 2026-08-23 | H, D (S5) | **F-S5-2 — no `ppcp-sim` host originated `capture_request`**, so 8.4a on the peer under test (converting a window expressed in the host's convention into its own buffer's timebase — the device half of I22) could not be driven from outside, and 8.4b's `absent`/`outside_buffer` answer was unobservable | Closed `9571b01`: `requesting-host` scenario, counters `capture_requests_tx`/`_rx`, and `CT-I22-sockets-capture-request` |
| 2026-08-23 | H, D (S5) | `CONF` §5's `unrelated` row says the host "excludes and retains every Candidate", and 8.2d's *uncertainty* exclusion is never reached by a peer with **no** relation — there the Candidate is retained un-grouped under 8.2i1 | **Erratum E19** adds `CONF` 5a1 saying so. Both readings satisfy the row and both are observable; an implementer reading it as "8.2d fires" looks for an exclusion event that never arrives |
| 2026-08-23 | L (S5) | The adjacent-MUST sweep for S5's own errata is `docs/conformance/adjacent-must-sweep-s5.md`, 26 sections. Reading it found **five** places where a new clause and an old one now disagreed: I13 and 10.1d against E11's closed vocabularies; 5.11j's "a consumer never sees `transfer: pending` on a preview Capture" against E16; I8 against E29; I16 against E28 | All five reconciled in the same pass. This is 5b2 doing exactly what `CORE` revision 8 accepted it for, on the first run against a batch of errata rather than after a fourth reviewer found the fourth instance |

| 2026-08-23 | D (S5) | **F-S5-5 — PinPointCapture advertised a `pv` every conformant browser must discard.** `DiscoveryAdvertisement` defaulted `pv` to the wire **token** `ppcp/1.0`, which is not a version range: `RV` 3.3d requires each endpoint to be `MAJOR.MINOR`, and 3.3d's own rule is that a reader which cannot parse a range **ignores the advertisement rather than guessing**. The device would have been invisible on the discovery path to anyone who implemented the clause | **Fixed** in PinPointCapture `b83fdc7` (`pv` is now `1.0`; the browser side parses rather than string-matches, and the negative assertion — `ppcp/1.0` must NOT read as a range — is in `ErrataTests.swift`). **This is erratum E25 earning its keep**: nothing before it could have caught the defect, because the syntax was defined nowhere. It is the clearest evidence in the programme that the errata are not bookkeeping |
| 2026-08-23 | D (S5) | **F-S5-6 — `ppcp-sim` never called `ppcp_arbiter_reconsider()`**, so E29 was **unobservable through the conformance tool**: against that counterpart a Candidate nominated before §6.3's burst converged stayed retained for the life of the Session, exactly as F-S5-1 described, and a host that never reconsidered was indistinguishable from one that did. PinPointCapture's IOP-2 row therefore kept its `nominateOnlyOnceConvertible` hold | **Fixed in `libppcp` tools** (this commit). The simulator now calls it at **both** sites 6.3d makes distinct — on a `relation_update` arriving from the peer, and after its own estimator publishes one — and a `reconsidered` counter makes the call assertable through `--expect`. **PinPointCapture can drop the `nominateOnlyOnceConvertible` hold.** ⚠ A socket row that *reaches* 8.2d could not be built: on loopback the burst converges in tens of milliseconds, and every way of delaying it (withholding the declared relation, holding the publish, holding the pump, holding the on-connect burst) also delays the Candidate behind it. E29's evidence stays the deterministic C test in `test_ct_i35`, which was verified red with the fix disabled |

| 2026-08-23 | L (S5) | **L17 wave 2: the freeze-readiness report** ([`docs/conformance/freeze-readiness.md`](../conformance/freeze-readiness.md)). Both application columns of the matrix re-read from PinPointStudio `5f9d53c` and PinPointCapture `b83fdc7`; all ten `CONF` §5a pairings now pass, three of them between the two real applications | **Recommendation: freeze the four `PPCP` documents and `PPCP-RV` against anything but errata; do NOT declare `ppcp/1.0` stable.** Five blocking conditions: (1) a Capture carrying **bytes** crosses between the two applications — the whole `payload_*` family, both digests, `already_present`, resume and `capture_committed` have never been exercised between them; (2) a camera declaration meets a foreign one on real hardware (CT-S1 1–5, CT-S7 (4), CT-I17, IOP-2's named half); (3) **RT-12 discharged by a named reviewer** in both apps — `RV` §9 calls it the requirement the whole security model rests on and the one no test can catch, and it is currently discharged by nobody; (4) **RT-17** likewise; (5) the matrix gains a cell state for "not this peer's row" so its 48 `—` cells can be classified. Seven further items are recorded as **accepted rather than pending**, `CONF` 5c among them |
| 2026-08-23 | L (S5) | **The matrix cannot currently answer the question the freeze asks of it.** 189 cells: 93 `pass`, 48 `—`, 23 `impl`, 16 `n/a`, 6 `review`, 3 `rig`. The plan's completion test is *every row `pass`, `n/a` by profile, or `blocked: rig`, in all three columns*, and 48 cells meet none — but they are **not** 48 pieces of missing work: the vocabulary has no term for "this row is not this implementation's to run", so a host column carries `—` for CT-I1 exactly as it would for work nobody started | **A defect in the record, not in the implementations.** Needs a sixth cell state, `n/a: not this peer's row`, distinguished from `n/a` (profile not declared, negative test passed). Listed as blocking condition 5 because no amount of implementation work closes it |
| 2026-08-23 | L (S5) | **Clause identifiers are not unique across the document set**, and nothing enforces it: erratum E5 gave `ENC` §5.1 clauses `5.1a`/`5.1b`, and `CORE` §5.1 and `MSG` §5.1 already had clauses of those names. Visible in the S5 sweep, whose `--changed 5.1a,5.1b` emitted three unrelated sections | **Recorded, not fixed.** Every citation in the set is document-qualified and the sweep generator over-emits, which is the safe direction. Renumbering now would invalidate every citation in three repositories; the freeze is the moment to decide whether identifiers become globally unique |
| 2026-08-23 | orchestrator (S5) | **Session S5 closed; the programme's five sessions are complete.** Every CONF §5 pairing passes, including the real PinPointStudio ↔ PinPointCapture pair over TLS 1.2 PSK on loopback (IOP-1/6/10). 29 errata (E1–E29) in the specification text. Freeze-readiness report: `docs/conformance/freeze-readiness.md` — **freeze the documents against anything but errata; do not declare `ppcp/1.0` stable** until: (1) a Capture carrying bytes crosses between the two apps (phone: PPC ring buffer unwired); (2) a camera declaration meets a foreign one on hardware; (3) RT-12 and (4) RT-17 discharged by a named reviewer; (5) the matrix gains a "not this peer's row" state | Remaining work is hardware, human review and product decisions, not sessions. Matrix: 189 cells — 93 pass, 48 —, 23 impl, 16 n/a, 6 review, 3 rig |
| 2026-08-23 | L (S5) | Clause identifiers are not unique across documents (E5 gave ENC `5.1a/b`; CORE and MSG §5.1 already have them). Every citation is document-qualified; nothing is wrong today | **Decision for the freeze**: renumber (invalidates citations in three repos) or adopt document-prefixed ids. Deliberately not acted on |

---

## 10. How an orchestration session is run

For the orchestrator, so every session is run the same way.

1. Read this file and the matrix. Build the three agent briefs from the session row in §7: the work package text verbatim, the ground rules of §1, the decisions of §3, the paths of the specification, and — for H and D — the path of `libppcp`'s public headers **and that repo's own `CLAUDE.md` and memory index, with its build recipe pasted verbatim and the job cap from rule 7**.
2. Launch the agents, each with its working directory set to its own repository. Run them in parallel only when the session row's packages are independent; when H and D depend on an L package that has not landed (S2: L6–L8), run L first and launch H and D when it is on `main`. Three agents in parallel means `-j3` each. Each brief ends with the same reporting contract: *what landed (files, commits), which matrix rows moved and to what state with the reproducing command, what was found wrong in the specification or in another team's API, what is unfinished.*
3. Relay cross-team items as they arrive (§7). Never let an agent fix another repo's problem in its own.
4. When all three report: verify the gate, update §4–§6 status boxes, update the matrix from the reports and the `ppcp-conform` output, append to §9, and commit each repo to `main`.
5. Record in memory where the session stopped.
