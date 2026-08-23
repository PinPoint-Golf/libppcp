# `libppcp` — conformance claim

**The reference implementation's own claim.**

> ⚠ **This file is GENERATED.** `tools/make-claim.sh` builds it from
> `tools/claim/head.md`, a live `ctest` run, and `ppcp-conform`'s own JSON.
> Editing it by hand is editing the output of a command, which is exactly the
> thing plan ground rule 4 exists to prevent — "nobody marks a cell `pass` by
> hand: it comes from a command that can be re-run". The authored prose lives in
> `tools/claim/head.md` and `tools/claim/tail.md`; everything between them is
> evidence.

| | |
|---|---|
| Implementation | `libppcp`, the MIT reference implementation |
| Against | `PPCP-CORE` revision 9 + errata E1–E4, `PPCP-MSG`, `PPCP-ENC`, `PPCP-CONF` 1.0; `PPCP-RV` revision 8 + errata E3, E4 |
| Wire version | `ppcp/1.0` |
| Sessions | S1 — L0–L3, L12 · S2 — L4–L8 · S3 — L9–L11, L13 · **S4 — L14, L15, L16** |
| Matrix | [`matrix.md`](matrix.md) — this file is the human-readable form of the `libppcp` column |

## The claim

> *`libppcp` implements the **Core, Capture, Detect, Mint, Arbitrate, Live, Markup and Offline** profiles of PPCP 1.0 — all eight — and passes every test in `PPCP-CONF` §3 and §4 carrying those profiles.*

**Not yet, and the gap is now small enough to name exactly.** `CONF` 1b and 1c require every test carrying a declared profile to pass, and the rows below say which do. What is left is not missing code: it is rows whose *method* is `rig` (physical ground truth nobody has measured), rows that are an application's to answer rather than a library's, and `CONF` 5c — a pairing against an implementation this team did not write, which no amount of work inside this repository can supply.

`libppcp` also claims `PPCP-RV` conformance **in part**: the pairing-code payload (`RV` §4), the key derivation (§5.1), the resolvable identifiers (§3.4) and the PSK identity (§5.3). It does **not** implement the TLS profile (§5.2), service discovery (§3) or network join (§6), and cannot: plan A7 and A8 put TLS and discovery in the applications, and `RV` 5.2i says compliance for those clauses is demonstrated by observed handshake rather than by an API. Those rows are `n/a` for this column by construction.

## The commands

Everything below comes from these, and each re-runs on its own:

```sh
cmake --preset dev && cmake --build --preset dev -j3 && ctest --preset dev
```

```sh
# the paired and injected rows, through the same instrument the applications use
build/dev/tools/ppcp-conform/ppcp-conform --self --role host \
    --profiles core,capture,detect,mint,arbitrate,live,offline,markup \
    --column libppcp --json host.json --markdown host.md
build/dev/tools/ppcp-conform/ppcp-conform --self --role capture \
    --profiles core,capture,detect,mint,arbitrate,live,offline,markup \
    --column libppcp --json capture.json --markdown capture.md
```

The same suite passes under `san` (AddressSanitizer + UndefinedBehaviorSanitizer), and `swift build` builds the same sources as the SwiftPM C target `CPPCP`.

---

<!-- ===== everything from here to the closing marker is GENERATED ===== -->

## The evidence

Generated 2026-08-23 from commit `a4c8a74` by `tools/make-claim.sh`.

### The suite — 46 rows, all passing

Every row below is a command: `ctest --preset dev -R <name>`.

| Row | Result |
|---|---|
| `test_cbor` | Passed |
| `test_frame` | Passed |
| `test_envelope` | Passed |
| `test_clock` | Passed |
| `test_ct_i1` | Passed |
| `test_ct_i3` | Passed |
| `test_ct_i4` | Passed |
| `test_ct_i13` | Passed |
| `test_ct_i29` | Passed |
| `test_ct_s1` | Passed |
| `test_ct_i12` | Passed |
| `test_ct_i24` | Passed |
| `test_ct_i38` | Passed |
| `test_ct_i21` | Passed |
| `test_l9_queue` | Passed |
| `test_ct_s4` | Passed |
| `test_ct_i35` | Passed |
| `test_ct_i37` | Passed |
| `test_ct_s6` | Passed |
| `test_umbrella` | Passed |
| `test_fixtures` | Passed |
| `test_rv` | Passed |
| `L15-fixtures-stable` | Passed |
| `CT-I18-api-surface` | Passed |
| `CT-I14-no-thresholds` | Passed |
| `purity` | Passed |
| `CT-I7-sockets` | Passed |
| `CT-I8-sockets` | Passed |
| `CT-I12-sockets` | Passed |
| `CT-I18-sockets` | Passed |
| `CT-I20-sockets-refusal` | Passed |
| `CT-I20-sockets` | Passed |
| `CT-I21-sockets` | Passed |
| `CT-I34-sockets` | Passed |
| `CT-S5-sockets` | Passed |
| `CT-S6-sockets-arbitrate` | Passed |
| `CT-S6-sockets-observer` | Passed |
| `CT-S4-sockets-silent-host` | Passed |
| `IOP-5-sockets-unrelated` | Passed |
| `RT-4-psk-ke-only-refused` | Passed |
| `RT-4-psk-ke-only-accepted-is-a-failure` | Passed |
| `IOP-9-sockets-preview` | Passed |
| `L14-conform-self-host` | Passed |
| `L14-conform-self-capture` | Passed |
| `L16-profile-boundary` | Passed |
| `L16-adjacent-must` | Passed |

### The paired and injected rows — `ppcp-conform`

`--self` stands a second `ppcp-sim` up as the peer under test over loopback, so
each row below is two processes, two TCP connections and a wire — not two
engines through a byte buffer, which `CONF` §2c says passes I19, I22, I24 and
I31 by accident. Every verdict carries the exact `ppcp-sim` command that
produced it; those are in the JSON the same run emits.

**The peer under test is a host:**


| Test | Invariant | Profile | Method | libppcp |
|---|---|---|---|---|
| CT-I7 | I7 | Mint, Arbitrate | paired | pass |
| CT-I8 | I8 | Mint, Arbitrate | paired | pass |
| CT-I20 | I20 | Arbitrate | paired | pass |
| CT-I21 | I21 | Live | paired | pass |
| CT-I36a | I36 | Capture | paired | pass |
| CT-S5 | I18 | Core | paired | pass |
| CT-S6 | I24 | Core | injected | pass |
| IOP-5 | I3, 8.2i1 | Core | paired | pass |
| CT-I12 | I12 | Offline | paired | pass |
| CT-S3 | I19 | Core | injected | pass |
| CT-S7 | I31 | Capture | injected | pass |

Every `pass` above came from a command; the commands are in the JSON beside this file, one per row, and each re-runs on its own.

**The peer under test is a capture peer:**


| Test | Invariant | Profile | Method | libppcp |
|---|---|---|---|---|
| CT-S4 | I20, I23 | Mint | injected | pass |
| CT-I35 | I35 | Arbitrate | injected | pass |
| CT-I18 | I18 | Core | paired | pass |

Every `pass` above came from a command; the commands are in the JSON beside this file, one per row, and each re-runs on its own.

### The checked-in fixtures — `CONF` §2b

Written by `tools/ppcp-fixtures`, under version control, read back from disk by
`tests/test_fixtures.c`, and asserted byte-identical on every run by
`L15-fixtures-stable`. `ENC` 4e makes deterministic encoding a fair question to
ask of a wire format, and this is where it is asked.

| Fixture | Bytes | Rows |
|---|---|---|
| `ct-i12-empty.ppcpb` | 809 | CT-I12, CT-I34 |
| `ct-i12-imu.ppcpb` | 1446 | CT-I12, CT-I34 |
| `ct-i12-video.ppcpb` | 1448 | CT-I12, CT-I34 |
| `ct-i13-unknowns.ppcpb` | 1577 | CT-I13 |
| `ct-i15-wall-step.ppcpb` | 1473 | CT-I15 |
| `ct-i2-i11-series-gap.ppcpb` | 1718 | CT-I2, CT-I11 |
| `ct-i36-truncated-complete.ppcpb` | 1349 | CT-I36 (c)(d) |
| `ct-i36-truncated-partial.ppcpb` | 1348 | CT-I36 (c)(d) |

### The freeze gates — `CONF` §5b1, §5b2

| Gate | Command | Result |
|---|---|---|
| Profile boundary (5b1) | `ctest --preset dev -R L16-profile-boundary` | pass |
| Adjacent-MUST sweep (5b2) | `ctest --preset dev -R L16-adjacent-must` | generator runs; the sweep itself is L17 |

  45 messages, 45 profile bindings checked against both tables, 0 unconferred origination MUSTs

<!-- ===== end of the generated evidence ===== -->

---

## What is not claimed, and why

| | Why |
|---|---|
| **CT-S2, and the `rig` half of CT-I31** | The LED timecode rig of `CONF` 2d does not exist. Every timing constant nobody has measured is declared `assumed` (plan A12), which is the honest position and not a passing one. |
| **`CONF` 5c — a pairing with a foreign implementation** | Both ends of every paired row in this file are `libppcp`. `tools/scenarios/` makes the *declaration* foreign — a different convention, a `global` geometry, a measured non-zero offset, three clocks, a profile set that omits Detect — which is what `CONF` 2c requires and what stops an implementation passing I19, I22, I24 and I31 by accident. It does not make the *implementation* foreign, and nothing in this repository can. |
| **The `RV` rows needing a handshake, a socket or storage** | Plan A7 and A8 put TLS, discovery and network join in the applications. `RV` 5.2i is explicit that compliance there is shown by an observed handshake, not by an API assertion. |
| **CT-I15 beyond the library's own surface** | The fixture proves `libppcp` computes no interval on a `wall` timebase — it computes no interval on any timebase, because there is no `ppcp_instant_diff` in the public headers at all. It does not prove an embedding will not, which is why CT-I15 is a separate cell per implementation. |
| **CT-I34 from outside** | It is a fixture row here and is deliberately absent from `ppcp-conform`: nothing on the wire distinguishes an importer that de-duplicated from one that imported twice. A conformance instrument that claimed it from outside would be claiming what it cannot see. |

## Specification defects found, and what was done about them

Every one is in the plan's §9 log with the commit that closed it. The four that changed the specification are errata, recorded in `PPCP-CORE`'s errata table:

| # | Clause | What was wrong |
|---|---|---|
| **E1** | `ENC` §2.1 | Two implementations invented two different implicit rules for associating a peer's connections into a link. Both worked against themselves; neither would have met the other. `link_bind` makes it explicit. |
| **E2** | `MSG` 6.1g | `sync_probe.timebase_id` addressed the *prober's* clocks (6.1d) and 6.1b left the responder's to the responder, so a peer with one clock could not measure two clocks of one counterpart. I21's remote half was unreachable. |
| **E3** | `RV` 7.3a, 7.3f, 7.5c | `mu` counted *handshakes*, and a PPCP link is two or three TLS handshakes over one `K_tls`. The default `mu: 1` was spent by the control channel and the bulk channel of the same link refused. It counts **pairings**, and spends the **code** rather than the pairing — without which §7.5's reconnection was dead letter by default. |
| **E4** | `RV` 2c, 2c1, RT-5 | "There is no unauthenticated path" forbade the plaintext transport `CONF` §2c's own **required** test infrastructure runs over, while 9a permits it. Jointly unsatisfiable for a peer that both claims RV and is testable. |

The seven findings the two application teams raised against this library in session S3 — the event-ring drop, the unreadable Session parameters on the originating path, the unreachable remote half of I21, the missing mint readback, the missing `session_resume` originator, 6.1c's inexpressible escape, and Live's silent precondition — are all closed, each with its commit, in plan §9.
