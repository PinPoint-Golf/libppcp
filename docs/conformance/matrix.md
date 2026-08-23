# PPCP conformance matrix

**The compliance record for the three implementations. Updated at the end of every orchestration session; `pass` cells come from a command, never from a hand.**

| | |
|---|---|
| Against | `PPCP-CONF` 1.0 §3–§5; `PPCP-RV` 1.0 §9 |
| Plan | [`../implementation/implementation-plan.md`](../implementation/implementation-plan.md) §8 defines the cell vocabulary |
| Claims | `libppcp`: [`claim-libppcp.md`](claim-libppcp.md) · PinPointStudio: `PinPointStudio/docs/ppcp-conformance.md` · PinPointCapture: `PinPointCapture/docs/ppcp-conformance.md` |
| Last updated | 2026-08-23 — **S5 wave 2 (L17)**. Both application columns re-read from their S5 claim files: PinPointStudio `5f9d53c` (`docs/ppcp-conformance.md` §10.4, §11), PinPointCapture `b83fdc7` (`docs/ppcp-conformance.md` §3, §4a and `docs/conformance/ppcp-conform.json`). All ten `CONF` §5a pairings pass; see [`freeze-readiness.md`](freeze-readiness.md) for what they do and do not prove |

Cells: `—` not started · `impl` code exists, not passing · `pass` passing, command in the claim file · `n/a` profile not declared, negative test passes · `rig` needs the LED timecode rig · `review` RV review method, reviewer and commit recorded · `blocked: …`

⚠ **A `pass` in the `libppcp` column of a *paired* row used to mean two `libppcp` engines run against each other through a byte buffer.** That is a real end-to-end run and it is **not** an interoperability demonstration: `CONF` §2c says an implementation tested only against itself passes I19, I22, I24 and I31 by accident.

**Since L13 (S3 wave 2) the eight rows that warning named are also run over real loopback sockets** — two processes, two TCP connections, a `link_bind` on each, and a counterpart presenting a declaration from a JSON file that no C test wrote. See [§6](#6-the-socket-paired-rows) for the row-to-command map. The warning still stands for the *third-party* half of `CONF` 5c: both ends are still `libppcp`, and the foreignness is in the declaration rather than in the implementation. What has changed is that a hardcoded convention, an assumed-zero offset or a missing profile check now has something to disagree with.

Profile columns: `libppcp` declares all eight profiles. PinPointStudio (host) declares Core, Capture, Detect, Arbitrate, Live, Offline, Markup. PinPointCapture (device) declares Core, Capture, Detect, Mint, Live, Offline, Markup.

## 1. Invariant tests — `CONF` §3

| Test | Invariant | Profile | Method | Work packages | `libppcp` | PinPointStudio | PinPointCapture |
|---|---|---|---|---|---|---|---|
| CT-I1 | I1 | Core | static | L1, L2 | pass | — | — |
| CT-I2 | I2 | Core | fixture | L8, **L15**, D4 | pass | — | pass (own half) |
| CT-I3 | I3 | Core | static | L2, L4 | pass | — | — |
| CT-I4 | I4 | Core | static | L2, D2 | pass | — | pass |
| CT-I5 | I5 | Capture | paired | L6 | impl | — | — |
| CT-I6 | I6 | Mint, Arbitrate | static | L4, L10, H5, **L17** | pass | n/a — the negative Mint row RAN and passed (`CONF` 1d), 23 Aug at `4d0e04a` | pass |
| CT-I7 | I7 | Mint, Arbitrate | paired | L10, H5, D5 | pass | pass (conform) | — |
| CT-I8 | I8 | Mint, Arbitrate | paired | L10, H5, D5 | pass | pass (conform) | pass (own half) |
| CT-I9 | I9 | Core | static | L4, L10 | pass | — | — |
| CT-I10 | I10 | Capture | paired | L7, D4 | — | — | pass (owner half) |
| CT-I11 | I11 | Capture | fixture | L7, **L15**, D4 | pass | — | pass |
| CT-I12 | I12 | Capture | fixture | L8, **L15**, H3, D3 | pass | pass (conform) | pass (own half) |
| CT-I13 | I13 | Core | fixture | L1, **L15** | pass | — | — |
| CT-I14 | I14 | Core | static | L6, H2 | pass | pass | — |
| CT-I15 | I15 | Offline | fixture | L8, **L15**, H3 | pass | impl | — |
| CT-I16 | I16 | Offline | paired | L8, H3 | impl | impl | — |
| CT-I17 | I17 | Capture | injected | L3 → CT-S1 | pass | — | impl |
| CT-I18 | I18 | Core | paired | L9, H5, D6 | pass | pass (negative half) | pass (conform) |
| CT-I19 | I19 | Core | injected | L4 → CT-S3 | — | pass | pass (own half) |
| CT-I20 | I20 | Arbitrate | paired | L6, H5 | pass | pass (conform) | — |
| CT-I21 | I21 | Live | paired | L9, H5, D6 | pass | pass (conform) | pass (own half) |
| CT-I22 | I22 | Capture | static | L4, D2 | impl | — | pass |
| CT-I23 | I23 | Mint | injected | L10 → CT-S4 | pass | — | pass (own half) |
| CT-I24 | I24 | Core | injected | L6, L13 → CT-S6 | pass | — | — |
| CT-I25 | I25 | Offline | static | L4 | — | — | — |
| CT-I26 | I26 | Detect | static | L4, L10, D5 | pass | — | pass |
| CT-I27 | I27 | Capture | static | L4, D4 | — | — | pass |
| CT-I28 | I28 | Capture | static | L4, D2 | — | — | pass |
| CT-I29 | I29 | Detect | static | L4, L10, D5 | pass | — | pass |
| CT-I30 | I30 | Capture | paired | L7, D4 | impl | — | pass (own half) |
| CT-I31 | I31 | Capture | static | L4, D2 | impl | — | pass |
| CT-I32 | I32 | Mint | injected | L10, D5 | pass | — | impl |
| CT-I33 | I33 | Detect | injected | L10, D5 | pass | — | pass (own half) |
| CT-I34 | I34 | Offline | fixture | L8, **L15**, H3, D3 | pass | pass | pass |
| CT-I35 | I35 | Arbitrate | injected | L10, H5 | pass | impl | pass (conform) |
| CT-I36 | I36 | Capture | fixture | L7, L8, **L15**, D4 | pass | — | pass |
| CT-I36a | I36 | Capture | paired | L7, H4, D4 | pass (conform) | pass (conform) | pass (own half) |
| CT-I37 | I37 | Markup | static | L11, H7, D8 | pass | pass | pass (device) |
| CT-I38 | I38 | Capture | paired | L7, D6 | impl | — | pass (own half) |

## 2. Silent-failure tests — `CONF` §4

| Test | Invariants | Profile | Method | Work packages | `libppcp` | PinPointStudio | PinPointCapture |
|---|---|---|---|---|---|---|---|
| CT-S1 | I17, I22 | Capture | injected | L3, H4, D4 | pass | impl | impl (6 pass) |
| CT-S2 | I22 | Capture | **rig** | — | rig | rig | rig |
| CT-S3 | I19 | Core | injected | L13, **L14**, H2, D2 | pass | pass (conform) | — |
| CT-S4 | I20, I23 | Mint | injected | L10, L13, D3, D5, D6 | pass | — | pass (conform; 1, 2, 3, 5, 6 pass, 7 impl — the live half needs a host link) |
| CT-S5 | I18 | Core | paired | L9, H5, D6 | pass | pass (conform) | pass (simulator half) |
| CT-S6 | I24 | Core | injected | L5, L6, L13 | pass | pass (conform) | — |
| CT-S7 | I31 | Capture | injected | L13, **L14**, D2 | pass | pass (conform) | impl (1–3 pass; **4 blocked: a phone** — a simulator enumerates no camera Source) |

## 3. Interoperability pairings — `CONF` §5a

| # | A | B | Proves | Session | Status |
|---|---|---|---|---|---|
| IOP-1 | Reference device | reference host | happy path | S5 | **pass — the real pair.** PinPointCapture ↔ PinPointStudio over **TLS 1.2 `TLS_PSK_WITH_AES_128_GCM_SHA256`** on loopback, both ends agreeing the mode and its absence of forward secrecy independently. Transport, `hello`/`declare`, Session, Streams, sync, arm, Candidates, arbitration, offer and replay all ran. **No Capture carried bytes** — every one is `absent` (8.4b), because the device is a simulator with no camera. Also passes against `ppcp-sim`'s `reference-host` |
| IOP-2 | Reference device | synthetic third-party host, different camera conventions | I19, I22 | S5 | **pass** — PinPointCapture against `three-timebase-host.json`, `make conform-iop`. ⚠ **The half the row is named for did not run**: `declared_camera: false`, so no camera declaration met a foreign one. That half needs a phone |
| IOP-3 | Reference device, no host → bundle | reference host import | I20, I23, I16, I9 | S5 | **pass** — two bundles PinPointCapture wrote, imported by PinPointStudio (`ses-interop-one-shot.ppcpbndl` 13 frames, `ses-interop-two-shots.ppcpbndl` 20 frames; both `partial`, **0 clips**, 0 commits). **IOP-3-live** additionally passes: a Session replayed onto the live link is imported and not merged (erratum E28) |
| IOP-4 | Reference host | observer-only peer (Core + Live) | I24 | S5 | **pass** — PinPointStudio against `observer-core.json`: `declares_rx 1`, `candidates_rx 0`, `shots_rx 0`, `issued 0`, `heartbeat_acks 6`, `errors_fatal 0` |
| IOP-5 | Reference host | peer declaring `unrelated` timebases | I3, 8.2i1 | S5 | **pass** — PinPointStudio against `unrelated-capture.json`: `candidates_rx 1`, `retained 1`, `issued 0`, `groups 0`, and no zero offset substituted. `excluded` is 0 and that is correct, not a miss (erratum E19, `CONF` 5a1) |
| IOP-6 | Reference host owning an acoustic Source | device with an acoustic Source | I8 | S5 | **pass — the real pair.** PinPointCapture ↔ PinPointStudio with both peers nominating: **one Shot referencing four Candidates from both peers** (`max_shot_candidates: 4`, `nominations_refused: 0`). Also passes against `ppcp-sim`'s `acoustic-host` |
| IOP-7 | Reference host that never issues `shot` | nominating peer | I32 | S5 | **pass** — PinPointStudio never issuing: `issued 0`, `candidates_rx 1`, `shots_rx 1`, `adopted 1` — the device's own 8.2i deadline is the only thing that fired |
| IOP-8 | Reference host delayed past the mint deadline | nominating peer | I35 | S5 | **pass** — PinPointStudio delayed 3 s against a 1.2 s deadline: `issued 0`, `adopted 1`, `shots_rx 1`, sim `minted 1`, `t0_revisions 0`. The host attached rather than issuing a second Shot |
| IOP-9 | Reference host | capture peer with `continuous` + `preview` Streams | I36 | S5 | **pass** — PinPointStudio against `preview-capture.json`: `streams_rx 3` (1 preview, 2 continuous), `captures_rx 2`, `captures_absent 1`, `captures_not_retained 1`, **`preview_payload_frames 0`** |
| IOP-10 | Bundle written by A | read by B, both directions | `ENC` 7a | S5 | **pass, both directions.** PinPointStudio's `pinpointstudio-host-session.ppcpbndl` (5 frames, 1657 bytes, `declare` at offset 424 per erratum E9) read by PinPointCapture; PinPointCapture's two bundles read by PinPointStudio. Also demonstrated over the live link in the real pair (`replay_completed: true`) |

"Reference device" and "reference host" are, for this programme, PinPointCapture on the simulator and PinPointStudio; `ppcp-sim` stands in for the synthetic and degraded peers. `CONF` 5c (a pairing by an implementation not written by the reference team) remains open until a third party exists.

## 4. Freeze gates — `CONF` §5b1, §5b2

| Gate | Work package | Status |
|---|---|---|
| Profile-boundary audit runs in `ctest` and passes | L16 | **pass** — `ctest --preset dev -R L16-profile-boundary`. 45 messages compared between `PPCP-MSG` §11 and `src/ppcp_message.c`, message for message, profile for profile, clause for clause; no MUST anywhere requires originating a message no profile confers. Since erratum E18 it also asserts §11's **Required by** column against the documents, so the 5b2 sweep's answer cannot go stale |
| Adjacent-MUST sweep run and recorded | L16, L17 | **pass — run, not merely generated.** `ctest --preset dev -R L16-adjacent-must` builds it; [`adjacent-must-sweep-s4.md`](adjacent-must-sweep-s4.md) covers S4's three errata and [`adjacent-must-sweep-s5.md`](adjacent-must-sweep-s5.md) covers S5's twenty-five, 492 clauses in 26 sections. **Reading the S5 sweep found five clauses that disagreed with the new errata** (I13 and `CORE` 10.1d vs E11; 5.11j vs E16; I8 vs E29; I16 vs E28); all five reconciled |

## 5. PPCP-RV tests — `RV` §9

`libppcp` implements only the payload, derivation and identity parts of RV (plan A7, A8); rows that need a handshake, a socket or storage are `n/a` for it by construction and are demonstrated by the two applications.

| Test | Method | Asserts | Work packages | `libppcp` | PinPointStudio | PinPointCapture |
|---|---|---|---|---|---|---|
| RT-1 | static | §10.1 derivation vectors | L12 | pass | — | pass |
| RT-2 | static | §10.3 codes, `v` first in the all-fields payload, `sid` → UUID text | L12 | pass | — | — |
| RT-3 | injected | unknown `v` → version report | L12, D7 | pass | — | pass |
| RT-4 | injected | strongest mode negotiated, never plaintext, outcome surfaced | H1, D1, **L13** | n/a | impl — the counterpart now exists | impl — the counterpart now exists |
| RT-5 | paired | second handshake on a `mu: 1` code refused | H6 | n/a | pass | n/a |
| RT-6 | injected | expired code reported as expired, no connection | L12, H6, D7 | impl | n/a (publishes, does not scan) | pass |
| RT-7 | paired | TXT and instance name carry nothing persistent | H6, D7 | n/a | pass (browser half) | pass |
| RT-8 | paired | `rid` rotates and resolves under the right `K_id` only | L12, H6, D7 | impl | pass | pass |
| RT-9 | paired | diagnostic export carries no secret or payload | H6, D7 | n/a | pass | pass |
| RT-10 | injected | `session_resume` refused without a completed handshake | H1, D1 | n/a | impl | impl |
| RT-11 | injected | unknown identity and wrong key indistinguishable | H1 | n/a | pass | n/a (code path; plan §9, narrowed) |
| RT-12 | **review** | CSPRNG at full width, protected storage, erasure | H6, D7 | n/a | review | review |
| RT-13 | **review** | network join with consent; not left attached | D7 | n/a | n/a | review |
| RT-14 | static | §10.2 PSK identity; differs per connection; empty hint at TLS 1.2 | L12, H1, D1 | pass | pass (wire) | impl |
| RT-15 | paired | publisher refuses past `exp`; untrusted clock attempts | H6, D7 | n/a | pass (publisher half) | impl |
| RT-16 | **review** | no `PRK` persisted from `mu > 1` | H6, D7 | n/a | pass | review |
| RT-17 | **review** | every platform mode offered, from a capability query | H1, D1 | n/a | review — reviewer unassigned | review — reviewer unassigned |

## 6. The socket-paired rows

**What `ctest --preset dev -R sockets` runs, and why it is a different claim from the C tests.**

Each row below starts two `ppcp-sim` processes, dials two TCP connections between them, binds each connection to a channel with `link_bind` (`ENC` §2.1), and asserts on counters both ends maintain. The simulator exits non-zero on any violation it observes — a revised `t0` (I7), a message originated by a peer whose declared profiles do not confer it (I24), `authority: host` from a peer that declared `role: capture` (I20), a held relation spanning two clocks of one peer (I18), a malformed frame, or a first frame that is not `link_bind`.

The declarations are in [`../../tools/scenarios/`](../../tools/scenarios/) and its `README.md` maps every one of them to the row it serves.

| ctest row | Pair | Asserts |
|---|---|---|
| `CT-I7-sockets` | `reference-host` ↔ `late-candidate-capture` | A Candidate emitted 700 ms after the Shot was issued attaches; `t0` does not move |
| `CT-I8-sockets` | `acoustic-host` ↔ `nominating-capture` | Two Candidates of one `basis` from two peers, both on one Shot |
| `CT-I12-sockets` | `reference-host` ↔ `offer-session` | A stored Session offered over the live link and replayed into the ingest path |
| `CT-I18-sockets` | `three-timebase-host` ↔ `three-timebase-capture` | Three probe sequences at each end; nothing composed |
| `CT-I20-sockets-refusal` | `arbitrate-as-capture`, alone | The engine refuses to build an arbiter for a peer that is not a host |
| `CT-I20-sockets` | `reference-host` ↔ `reference-capture` | No `shot` claims host authority from a capture peer |
| `CT-I21-sockets` | `three-timebase-host` ↔ `reference-capture` | The per-timebase rule against the **host** (CT-S5 assertion 4) |
| `CT-I34-sockets` | `reference-host` ↔ `offer-session-twice` | The same Session replayed twice; each Capture imported once |
| `CT-S5-sockets` | `three-timebase-host` ↔ `three-timebase-capture` | Relations measured and never composed, both ends |
| `CT-S6-sockets-arbitrate` | `arbiter-no-detect` ↔ `reference-capture` | Assertion 1's second clause: a peer with Arbitrate and **no Detect** parses `candidate` **and arbitrates over the result** |
| `CT-S6-sockets-observer` | `reference-host` ↔ `observer` | Assertions 2 and 3: an observer originates neither, answers `profile_not_supported`, and the transport stays open |
| `CT-S4-sockets-silent-host` | `silent-host` ↔ `nominating-capture` | Assertion 6: a host that never answers, and a peer that mints only at the 8.2i deadline |
| `IOP-5-sockets-unrelated` | `reference-host` ↔ `unrelated-capture` | The host excludes and **retains**; the peer mints nothing; no zero is substituted |
| `IOP-9-sockets-preview` | `reference-host` ↔ `preview-capture` | A `continuous` segment and a discarded `preview` as `absent`/`not_retained` |
| `CT-I6-sockets` | `arbiter-no-detect` ↔ `nominating-capture` | A peer without Mint parses a device-authority `shot` and issues none on its own authority (`minted_shots_rx=0`) |
| `CT-I22-sockets-capture-request` | `requesting-host` ↔ `nominating-capture` | A host **asks** (8.4a): the device half of I22 — a window in the host's convention converted into the peer's own buffer — is drivable from outside for the first time (F-S5-2) |
| `F-S5-3-sockets-offer-during-live-session` | `reference-host` ↔ `offer-session` | An offered Session replayed onto a live link does **not** rebind the live Session's `timebase_ref` (`live_ref_rebound=0`, `imported_frames_rx>=1`) — erratum E28 |
| `RT-4-psk-ke-only-refused` | `ppcp-sim` ↔ `openssl s_server -tls1_3` | A DHE-requiring peer refuses a `psk_ke`-only ClientHello |
| `RT-4-psk-ke-only-accepted-is-a-failure` | the same, `-allow_no_dhe_kex` | A peer that **accepts** `psk_ke` is reported as an RT-4 failure — which is also what proves the hand-built ClientHello and its PSK binder are correct |

The two RT-4 rows are skipped where the OpenSSL CLI is absent; it is the peer under test there, not a dependency of anything that ships.

## 7. The `ppcp-conform` rows — work package L14

**A different claim again from §6, and the one the two application columns will be filled from.**

§6's rows are two `ppcp-sim` processes measured against each other. These are a **peer under test** driven from outside, through its real transport, by an instrument that takes the claimed profile set as input and applies `CONF` §1b–d: positive rows for declared profiles, negative rows for undeclared ones, `n/a` when a negative row passes. `libppcp`'s own column is filled by `--self`, which stands a second `ppcp-sim` up as the peer under test over loopback — the same instrument, so the reference and the applications are measured the same way (plan A11).

```sh
# the reference run, both roles; ctest -R L14-conform runs exactly this
ppcp-conform --self --role host    --profiles core,capture,detect,mint,arbitrate,live,offline,markup \
             --column libppcp --json host.json --markdown host.md
ppcp-conform --self --role capture --profiles core,capture,detect,mint,arbitrate,live,offline,markup \
             --column libppcp --json capture.json

# an application, over its own loopback transport
ppcp-conform --connect 127.0.0.1:9000 --role host \
             --profiles core,capture,detect,arbitrate,live,offline,markup \
             --column PinPointStudio --json pps.json --markdown pps.md
```

Exit **0** all applicable rows passed · **1** a row failed · **2** bad invocation · **3** no row applied, which is deliberately not 0. `tools/README.md` carries the full contract.

| Row | Peer under test | Counterpart declaration / scenario | Asserts |
|---|---|---|---|
| CT-I7 | host | `reference-capture` / `late-candidate-capture` | A Candidate 700 ms after the Shot attaches; `t0` is byte-identical |
| CT-I8 | host | `reference-capture` / `nominating-capture` | Every Candidate retained and present in `Shot.candidates` |
| CT-I20 | host | `reference-capture` / `reference-capture` | No `authority: host` Shot from a peer that declared `role: capture` |
| CT-I21 | host | `three-timebase-capture` / `reference-capture` | Three probe sequences, three directly-measured relations, none composed |
| CT-I36a | host | `preview-capture` / `preview-capture` | A `continuous` segment and a discarded `preview` as `absent` / `not_retained` |
| CT-S5 | host | `three-timebase-capture` / `reference-capture` | Relations measured, never composed, both ends |
| CT-S6 | host | `observer-core` / `observer` | An observer originates nothing past `hello`, `declare` and its acks |
| IOP-5 | host | `unrelated-capture` / `unrelated-capture` | Excluded and **retained**; no Shot, and no zero substituted |
| CT-I12 | host | `reference-capture` / `offer-session` | A stored Session offered live and replayed into the same ingest path |
| **CT-S3** | host | **`foreign-capture`** / `nominating-capture` | A camera that is not a phone — `start` convention, `global` geometry, 17 ppm — accepted on its declaration |
| **CT-S7** | host | **`measured-capture`** / `nominating-capture` | `provenance: measured` with a **non-zero** offset: the accident `CONF` 2c exists to prevent |
| CT-S4 | capture | `reference-host` / `silent-host` | A host that never issues: only the peer's own 8.2i deadline fires |
| CT-I35 | capture | `reference-host` / `late-host` | A host past the deadline attaches rather than issuing; `t0` does not move |
| CT-I18 | capture | `three-timebase-host` / `reference-host` | Three clocks probed directly; nothing composed |
| CT-I6, CT-I20n, CT-I26, CT-I25 | any | negative rows (`CONF` 1d) | Run only where the profile is **not** declared: parsed, never originated |

**Not in this table, on purpose.** `static` and `fixture` rows, which the implementation's own suite answers against the checked-in fixtures of `tests/fixtures/`; and **CT-I34**, although §1 calls it paired — nothing on the wire distinguishes an importer that de-duplicated from one that imported twice, so it is a fixture row and claiming it from outside would be claiming what cannot be seen.
