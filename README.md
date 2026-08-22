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

The specification is approved by both first-party implementation teams. It is **not yet frozen**: `ppcp/1.0` becomes stable when the conformance suite passes on both implementations and the interoperability pairings are demonstrated.

## Layout

| Path | Contents |
|---|---|
| `docs/specification/` | **The single authority on PPCP.** Core model, message catalogue, wire encoding, conformance |
| `docs/implementation/` | [The implementation plan](docs/implementation/implementation-plan.md) — work packages across `libppcp`, PinPointStudio and PinPointCapture, and the session tracker |
| `docs/conformance/` | [The conformance matrix](docs/conformance/matrix.md) — every `CT-*`, `RT-*` and interoperability row against all three implementations |
| `src/`, `tests/`, `tools/` | Reference implementation. Empty — the specification is approved and this is where the work starts. |
