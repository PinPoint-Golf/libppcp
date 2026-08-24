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
| Against | `PPCP-CORE` revision 9 + errata E1–E4, `PPCP-MSG`, `PPCP-ENC`, `PPCP-CONF` 1.0; **`PPCP-RV` revision 9 as amended by errata E30–E55** |
| Wire version | `ppcp/1.0` |
| Sessions | S1 — L0–L3, L12 · S2 — L4–L8 · S3 — L9–L11, L13 · S4 — L14, L15, L16 · S5 — L17 · **CR-01: C1 — L18, L19, L20 · C2 — L21, L22** |
| Matrix | [`matrix.md`](matrix.md) — this file is the human-readable form of the `libppcp` column |

## The claim

> *`libppcp` implements the **Core, Capture, Detect, Mint, Arbitrate, Live, Markup and Offline** profiles of PPCP 1.0 — all eight — and passes every test in `PPCP-CONF` §3 and §4 carrying those profiles.*

**Not yet, and the gap is now small enough to name exactly.** `CONF` 1b and 1c require every test carrying a declared profile to pass, and the rows below say which do. What is left is not missing code: it is rows whose *method* is `rig` (physical ground truth nobody has measured), rows that are an application's to answer rather than a library's, and `CONF` 5c — a pairing against an implementation this team did not write, which no amount of work inside this repository can supply.

`libppcp` also claims `PPCP-RV` conformance **in part**: the pairing-code payload (`RV` §4), the key derivation (§5.1), the resolvable identifiers (§3.4), the PSK identity (§5.3) and, since CR-01, **the arithmetic and the frames of §11's guided pairing** — the derivation of §11.6, the five frames of §11.4 and the exchange of §11.5 as a sans-I/O engine. It does **not** implement the TLS profile (§5.2), service discovery (§3) or network join (§6), and cannot: plan A7 and A8 put TLS and discovery in the applications, and `RV` 5.2i says compliance for those clauses is demonstrated by observed handshake rather than by an API. Those rows are `n/a` for this column by construction.

## ⛔ RV-6 — what is NOT claimed, stated as a named row rather than by silence

> **`RT-20c` is `unrun`, and no aggregate pass for RV-6 is claimed here or anywhere.**

[9g](../specification/ppcp-rv.md#9-conformance) is a MUST and it is the reason this section exists: *"a conformance claim to §11 names RT-20c explicitly and states its result, and MUST NOT report an aggregate pass for RV-6 while it is unrun."* An earlier revision of this file satisfied 9g **by silence** — it made no RV-6 claim at all — and silence stops being a defence the moment there is RV-6 code to claim about. There is.

**RT-20c needs both shipping implementations either side of a deliberate relay: both displaying mismatched digits, both declining, neither pairing.** PinPointStudio is initiator-only and PinPointCapture acceptor-only, so nothing in this repository can supply it and nothing smaller substitutes for it ([B7](../specification/ppcp-rv.md#annex-b--open-issues)).

⚠ **And the weight of the green below is the hazard, not the reassurance.** Every other row in the table is arithmetic and bookkeeping between two parties that are **both behaving**. RT-20b and RT-20c are the only rows in which somebody is attacking. [11.1d](../specification/ppcp-rv.md#111-what-this-path-is-and-the-one-thing-it-cannot-be) names the extreme case plainly: a peer that quietly compares the six digits in software passes every static test in the document and authenticates nothing.

| Row | Method | What it says | `libppcp` | Command |
|---|---|---|---|---|
| RT-18 | static | every §10.4 row byte-for-byte, against E30–E55 | pass | `ctest --preset dev -R test_rv_bootstrap` |
| RT-19 | injected | reveal ≠ commitment → `commitment_mismatch`, nothing derived | pass | `ctest --preset dev -R test_bs_engine` |
| RT-20a(a) | static | the interposer quadruple, no curve: `849063` ≠ `576027` | pass | `ctest --preset dev -R test_rv_bootstrap` |
| RT-20a(b) | static | no collision over 200 000 quadruples; digits uniform by χ². ⛔ never the rate | pass — **`Z` drawn from the RNG, not from key agreement** (see below) | `ctest --preset dev -R test_rv_sas_uniform` |
| **RT-20b** | injected | one real peer against the relay, **including 11.5c's ordering** | **the relay's own half passes; the peer half is H's and D's** | `ppcp-relay --selftest` |
| **RT-20c** | paired | ⛔ **both implementations either side of the relay. THIS is the RV-6 claim** | ⛔ **`unrun`** | needs PinPointStudio and PinPointCapture |
| RT-21 | injected | small-order `pk` → `invalid_key`, no derivation, **not retried** | pass (zero half) | `ctest --preset dev -R test_rv_bootstrap` |
| RT-22 | paired | the bootstrap window's TXT record and its withdrawal | `n/a` — no discovery here | — |
| RT-23 | review | ephemeral key, `Z`, `BK`, `K_c` erased on completion **and abort** | `n/a` — the key agreement is not here | — |
| RT-24 | injected | `bs_accept.v` ≠ sent `v` aborts | pass | `ctest --preset dev -R test_bs_engine` |
| RT-24a | review | the transcript bound into `sas_raw` and `K_c` **and nothing else** | ⚠ **maintainer-accepted, not independently reviewed** | see `matrix.md` §5b |
| RT-24b | static | both derivation counter-vectors: `PRK 9b779245…`, `sid 18dd04b1…` | pass | `ctest --preset dev -R test_rv_bootstrap` |
| RT-24c | static | the R-11 witness — `X25519(sk_i, pk_a')` = `Z` exactly, from a **different** public key, non-zero | pass (**both halves**) | `ctest --preset dev -R test_rv_bootstrap` and `ctest --preset dev -R RT-24c` |
| RT-25 | review | one attempt at a time; digits for one (trap 3) | `n/a` — no window here | — |
| RT-26 | review | affirmative control not the default; no retry affordance | `n/a` — no user interface here | — |
| RT-27 | review | only `pk` and `Z` cross the §11.11 boundary; both failure halves → `invalid_key` | ⚠ **maintainer-accepted, not independently reviewed** | see `matrix.md` §5b |

**What `RT-20b` means in this column, precisely.** The relay of [L21](../implementation/cr-01-implementation-plan.md) is in this repository and `--selftest` demonstrates six things without any application: both of the relay's own legs complete against an honest counterpart ([RT-20b(v)](../specification/ppcp-rv.md#9-conformance) — *"or the harness is testing its own bug"*); an interposition end to end in which the two honest peers see **different** six digits; both mirrors of [11.5c](../specification/ppcp-rv.md#115-the-exchange)'s ordering; and ⛔ a **negative control** in which the same probe, run against a stand-in built to carry [trap 2](../implementation/cr-01-implementation-plan.md#4-the-traps), must report a **failure** — without which the ordering rows would be untested tests, trap 2 being invisible on the wire. **Both honest ends there are `libppcp`'s own engine, so it is not RT-20c and must not be read as it.**

**`RT-24c` now has both halves, and the curve half is a command.** The plan put it application-side *"since it needs a curve"*. It still does — but a curve at arm's length is what `tools/rv-r11-witness.sh` uses, driving `openssl` over §10.4's published vector, and [ground rule 4](../implementation/implementation-plan.md) is satisfied because it re-runs. It asserts what [11.6c2](../specification/ppcp-rv.md#116-derivation) rests on: `X25519(sk_i, pk_a)` and `X25519(sk_i, pk_a')` are **bit-identical and non-zero** from two different public keys, so `BK`, `sid` and `PRK` are the same under the substitution and **only `sas_raw` separates the two peers**. ⛔ That is why dropping `pk_i ‖ pk_a` from the SAS info *"because `Z` already depends on them"* ([trap 5](../implementation/cr-01-implementation-plan.md#4-the-traps)) is undetectable from outside and removes the only separation there is. Checked that the row can fail: one bit changed in `pk_a'` and `Z'` diverges.

**Why `RT-20a(b)`'s `Z` is not from a curve, and what that costs.** [Ground rule 13](../implementation/cr-01-implementation-plan.md#1-ground-rules) and [11.11](../specification/ppcp-rv.md#1111-where-x25519-comes-from) keep X25519 out of this library — [A16](../specification/ppcp-rv.md#annex-a--decisions-and-alternatives) says it is the primitive `libppcp` should "neither hand-roll nor vendor" — so the row's *"where key agreement is available"* does not obtain here. [11.11c](../specification/ppcp-rv.md#1111-where-x25519-comes-from) makes 11.6c–11.6e a **pure function** of `Z`, `v`, `pk_i` and `pk_a`, so the digits' distribution is a property of HKDF over its inputs and a uniform `Z` is if anything a cleaner input than a curve's. ⛔ **It therefore does not show that X25519's own outputs are well distributed, and it does not show that two legs of a real interposition differ** — the first is OpenSSL's and CryptoKit's to answer, the second is RT-20b's and needs the relay.

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

```sh
# ⛔ RT-20b(v) — the relay's own legs, and the negative control that proves the
# ordering probe can fail.  Needs `openssl` on PATH: X25519 never enters this
# library, so the relay obtains key agreement across §11.11's boundary from a
# helper process and only `pk` and `Z` cross it.
build/dev/tools/ppcp-relay/ppcp-relay --selftest
```

The same suite passes under `san` (AddressSanitizer + UndefinedBehaviorSanitizer), and `swift build` builds the same sources as the SwiftPM C target `CPPCP`.

---

<!-- ===== everything from here to the closing marker is GENERATED ===== -->

## The evidence

Generated 2026-08-24 from commit `d579a7f` by `tools/make-claim.sh`.

### The suite — 56 rows, all passing

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
| `test_rv_bootstrap` | Passed |
| `test_rv_sas_uniform` | Passed |
| `test_bs_frame` | Passed |
| `test_bs_engine` | Passed |
| `L15-fixtures-stable` | Passed |
| `CT-I18-api-surface` | Passed |
| `CT-I14-no-thresholds` | Passed |
| `A3-headers-self-contained` | Passed |
| `purity` | Passed |
| `CT-I7-sockets` | Passed |
| `CT-I8-sockets` | Passed |
| `CT-I12-sockets` | Passed |
| `F-S5-3-sockets-offer-during-live-session` | Passed |
| `CT-I22-sockets-capture-request` | Passed |
| `CT-I18-sockets` | Passed |
| `CT-I20-sockets-refusal` | Passed |
| `CT-I20-sockets` | Passed |
| `CT-I21-sockets` | Passed |
| `CT-I34-sockets` | Passed |
| `CT-S5-sockets` | Passed |
| `CT-S6-sockets-arbitrate` | Passed |
| `CT-I6-sockets` | Passed |
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
| `RT-20b-relay-selftest` | Passed |
| `RT-24c-r11-witness` | Passed |

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
| `ct-i12-empty.ppcpb` | 809 | CT-I12 |
| `ct-i12-imu.ppcpb` | 1446 | CT-I12 |
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
| ⛔ **`RT-20c`, and with it any aggregate pass for RV-6** | [9g](../specification/ppcp-rv.md#9-conformance) is a MUST: a claim names RT-20c and states its result, and reports **no aggregate** while it is unrun. It is unrun. It needs **both shipping implementations either side of a deliberate relay** — both showing mismatched digits, both declining, neither pairing — and PinPointStudio is initiator-only while PinPointCapture is acceptor-only, so no two things in this repository can stand in for them. ⚠ **RT-20a and RT-20b passing is not it.** Every other RV-6 row above is arithmetic between two parties that are both behaving; RT-20b and RT-20c are the only rows in which somebody is attacking, and [11.1d](../specification/ppcp-rv.md#111-what-this-path-is-and-the-one-thing-it-cannot-be) names the extreme — a peer that quietly compares the digits itself passes every static test in the document and authenticates nothing. |
| **What RT-20c would still not show** | That a tired operator at bay four *notices* a mismatch. §9 says so directly: RT-20c shows the protocol emits a mismatch signal, not that anybody reads it. That half is [11.7d](../specification/ppcp-rv.md#117-the-short-authentication-string) and [11.9c](../specification/ppcp-rv.md#119-aborting-and-the-one-attempt-rule), it is human factors, and [§7.1](../specification/ppcp-rv.md#71-threat-model)'s *not defended* table already says an operator who affirms without comparing has authenticated the attacker. No protocol test reaches it and none of these should be read as if it did. |
| **`CONF` 5c — a pairing with a foreign implementation** | Both ends of every paired row in this file are `libppcp`. `tools/scenarios/` makes the *declaration* foreign — a different convention, a `global` geometry, a measured non-zero offset, three clocks, a profile set that omits Detect — which is what `CONF` 2c requires and what stops an implementation passing I19, I22, I24 and I31 by accident. It does not make the *implementation* foreign, and nothing in this repository can. |
| **The `RV` rows needing a handshake, a socket or storage** | Plan A7 and A8 put TLS, discovery and network join in the applications. `RV` 5.2i is explicit that compliance there is shown by an observed handshake, not by an API assertion. |
| **CT-I15 beyond the library's own surface** | The fixture proves `libppcp` computes no interval on a `wall` timebase — it computes no interval on any timebase, because there is no `ppcp_instant_diff` in the public headers at all. It does not prove an embedding will not, which is why CT-I15 is a separate cell per implementation. |
| **CT-I34 from outside** | It is a fixture row here and is deliberately absent from `ppcp-conform`: nothing on the wire distinguishes an importer that de-duplicated from one that imported twice. A conformance instrument that claimed it from outside would be claiming what it cannot see. |

## Specification defects found, and what was done about them

Every one is in the plan's §9 log with the commit that closed it. **Twenty-nine changed the specification and are errata**, recorded in [`PPCP-CORE`'s errata table](../specification/ppcp-core.md#errata-after-revision-9), which is the authoritative list. The ones this library found or fixed:

| # | Clause | What was wrong |
|---|---|---|
| **E1** | `ENC` §2.1 | Two implementations invented two different implicit rules for associating a peer's connections into a link. Both worked against themselves; neither would have met the other. `link_bind` makes it explicit. |
| **E2** | `MSG` 6.1g | `sync_probe.timebase_id` addressed the *prober's* clocks (6.1d) and 6.1b left the responder's to the responder, so a peer with one clock could not measure two clocks of one counterpart. I21's remote half was unreachable. |
| **E3** | `RV` 7.3a, 7.3f, 7.5c | `mu` counted *handshakes*, and a PPCP link is two or three TLS handshakes over one `K_tls`. The default `mu: 1` was spent by the control channel and the bulk channel of the same link refused. It counts **pairings**, and spends the **code** rather than the pairing — without which §7.5's reconnection was dead letter by default. |
| **E4** | `RV` 2c, 2c1, RT-5 | "There is no unauthenticated path" forbade the plaintext transport `CONF` §2c's own **required** test infrastructure runs over, while 9a permits it. Jointly unsatisfiable for a peer that both claims RV and is testable. |
| **E5** | `ENC` §5.1 | The document's only worked example was not in deterministic key order, so an encoder honouring 4e could not reproduce it. Re-emitted; the old ordering stays legal on receipt and `ppcp_message_encode_literal()` still produces it. |
| **E6** | `ENC` 5a1 | 5a reserved `session_id`, 4d makes a duplicate key malformed, and `MSG` lists the field in eight bodies — so `session_open` was unencodable. The body's `session_id` **is** the envelope's. |
| **E7** | `ENC` 6g, 6h | A payload had no declared container. A receiver writing a clip to disk had to guess an extension from `format.codec`, which is a codec three hops away. |
| **E8** | `ENC` 7d, 7d1 | Two completeness states named where the protocol has three: an unasserted, untruncated bundle was neither. The reader reports the assertion and the truncation separately. |
| **E9** | `ENC` 7h | A bundle need not carry `declare`, yet 8.5c scopes Capture identity by the minting peer — so a file of bare `capture_announce` frames was unattributable and un-deduplicable. |
| **E10** | `CORE` 6.1f, 6.2e | Neither division said how to round, and every worked example has an even `d`, so two implementations could differ by a nanosecond with both examples passing. |
| **E11** | `CORE` 5.3c, §10.3 | `Timebase.kind` is closed, and §10.3 now says which vocabularies are open. A peer cannot ignore whether a clock halts across sleep. |
| **E12** | `CORE` 5.14d1 | An `absent` Capture may carry `interval` whatever its anchor: 8.4b's answer is shot-anchored, and the table forbade the field that says which span left the buffer. |
| **E13** | `CORE` 5.8l | `AchievedSummary` is camera vocabulary and 5.11b requires a segment on every continuous Stream, including one with no frames. |
| **E14** | `CORE` 8.2b1 | §8.2 never said **which** contributing Candidate sets `t0`, so two conformant hosts could issue different `t0` for one event and I7 would freeze both. |
| **E15** | `CORE` C3, C3a, C3b | C3 binds the **request** class only, and the catalogue's origination column is not the profile a **responder** needs. |
| **E16** | `MSG` 8.1i, 8.1i1 | 8.1i forbade announcing a preview Capture `pending`, and 5.11c3 *requires* announcing the discarded preview segment, which has no other transfer state to carry. |
| **E17** | `MSG` 5.1e | `stream_close.closed_at` is optional and is in the Stream's timebase, which a consumer closing the Stream has no reading of. |
| **E18** | `MSG` 1c, §11 | The `CONF` 5b2 sweep: **27 of 45 messages were required by no normative clause**, and seven of those were responses nothing obliged a peer to send. §11 gains a **Required by** column and the 5b1 audit asserts it on every run. |
| **E19** | `CONF` §3, CT-S1, CT-S4(1), 5a1 | Four editorial corrections, one of them substantive: CT-S4 assertion 1 required a hostless session to run `arm`, which `CORE` 7.3b forbids. |
| **E20** | `RV` 4.3a1 | 4.3a promised byte-identical codes and did not say whether a defaulted optional is emitted. It is. |
| **E21** | `RV` 5.3a1 | **No octet of the PSK identity may be `0x00`.** A `strlen`-lengthed PSK interface truncated it and the handshake failed roughly one connection in sixteen. |
| **E22** | `RV` 5.3c1 | Scope: the wrong-key branch 5.3c and 5.3d equalise is unreachable while both keys come from one `PRK`. |
| **E23** | `RV` 3.5d | A peer whose platform has no server-side PSK resolver does not advertise for reconnection. |
| **E24–E27** | `RV` 4.4a2, 3.3d–e, 7.4h, 3.4d1–2 | Four questions L17 was asked to **decide**, each recorded as reversible: what may substitute for the boot-clock test, one range syntax across the set, whether a persisted pairing keeps a network hint, and what a multi-pairing peer advertises. |
| **E28** | `MSG` 4.1a1, 9.1b | **A `session_open` naming a different `session_id` opens a SECOND Session and changes nothing about the first.** 4.1a's immutability rule was written about the *same* id, so one `session_offer` accepted mid-session silently rebound the host's `timebase_ref` to the exporting device's clock. |
| **E29** | `CORE` 8.2d1 | A Candidate excluded for want of a relation was never revisited, so a peer nominating before the sync burst converged was silently unarbitrated for the whole Session. |

The seven findings the two application teams raised against this library in session S3 — the event-ring drop, the unreadable Session parameters on the originating path, the unreachable remote half of I21, the missing mint readback, the missing `session_resume` originator, 6.1c's inexpressible escape, and Live's silent precondition — are all closed, each with its commit, in plan §9.
