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
