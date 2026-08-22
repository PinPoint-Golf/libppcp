# libppcp

**PPCP — the PinPoint Capture Protocol.** An open protocol for time-synchronised capture devices, its reference implementation, and its conformance suite.

A mobile phone has a 120–240 fps camera, a microphone, an IMU and a hardware encoder. PPCP is how that device — or any other — becomes a first-class capture source for a host that needs to know precisely *when* each frame was exposed, whether or not a host was present when it was captured.

| | |
|---|---|
| Specification | [`docs/specification/`](docs/specification/) — **PPCP 1.0, APPROVED for implementation** (22 August 2026) |
| Wire version | `ppcp/1.0` |
| Licence | Specification: open. Library: MIT. |
| Companion spec | [`PPCP-RV`](docs/specification/ppcp-rv.md) — rendezvous, pairing and the security model. **Approved**, versioned independently. Implementing it is optional; implementing PPCP is not. |

## Start here

[`docs/specification/README.md`](docs/specification/README.md) — what the specification settles, how it got here across four review rounds, and what remains open.

To build and run the conformance suite:

```
cmake --preset dev && cmake --build --preset dev -j3 && ctest --preset dev
```

The explicit job count is not decoration. On 22 August 2026 a build launched
with a bare `-j` alongside two others exhausted memory and took the machine
down; the plan's ground rule 7 now requires every build command to carry one.

Presets are `dev` (warnings as errors), `san` (ASan + UBSan), `cov` (gcov), `rel` (optimised, keeps debug info) and `release` (the shipping artefact). `swift build` builds the same sources as the SwiftPM C target `CPPCP`. What passes today is recorded in [`docs/conformance/claim-libppcp.md`](docs/conformance/claim-libppcp.md).

The specification is approved by both first-party implementation teams. It is **not yet frozen**: `ppcp/1.0` becomes stable when the conformance suite passes on both implementations and the interoperability pairings are demonstrated.

## Layout

| Path | Contents |
|---|---|
| `docs/specification/` | **The single authority on PPCP.** Core model, message catalogue, wire encoding, conformance |
| `docs/implementation/` | [The implementation plan](docs/implementation/implementation-plan.md) — work packages across `libppcp`, PinPointStudio and PinPointCapture, and the session tracker |
| `docs/conformance/` | [The conformance matrix](docs/conformance/matrix.md) — every `CT-*`, `RT-*` and interoperability row against all three implementations |
| `include/ppcp/` | **The port surface.** [`ppcp.h`](include/ppcp/ppcp.h) is the umbrella. Built: the CBOR codec and framing, the timebase and canonical-instant machinery, the `CORE` §5 vocabulary, the forty-five-message catalogue, the peer engine, Captures and bulk transfer, the bundle container and its replay onto a live link, clock synchronisation and liveness, Detect/Mint/Arbitrate, and Markup. [`planned.h`](include/ppcp/planned.h) is now empty of declarations — every symbol the applications code against has a definition — and stays in the tree for the next package that needs to publish a name ahead of its body |
| `src/` | The reference implementation. C11, no dependencies, sans-I/O — no socket, thread, timer, clock or file, asserted at build time by [`tests/purity.cmake`](tests/purity.cmake) |
| `tests/` | The conformance suite. Tests are named after the `CT-`/`RT-` row they satisfy |
| `tools/` | `ppcp-sim` and `ppcp-conform` — the synthetic peer and the conformance driver. Not yet written |
