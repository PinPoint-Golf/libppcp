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
