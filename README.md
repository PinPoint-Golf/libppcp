# libppcp

**PPCP — the PinPoint Capture Protocol.** An open protocol for time-synchronised capture devices, its reference implementation, and its conformance suite.

A mobile phone has a 120–240 fps camera, a microphone, an IMU and a hardware encoder. PPCP is how that device — or any other — becomes a first-class capture source for a host that needs to know precisely *when* each frame was exposed, whether or not a host was present when it was captured.

| | |
|---|---|
| Specification | [`docs/specification/`](docs/specification/) — **Draft 1, for approval to implement** |
| Wire version | `ppcp/1.0` |
| Licence | Specification: open. Library: MIT. |
| Companion spec | `PPCP-RV` — rendezvous and pairing, versioned independently. [Scope only](docs/specification/ppcp-rv-scope.md); not yet written. |

## Start here

[`docs/specification/README.md`](docs/specification/README.md) — what the draft settles, what changed since the protocol overview, and the seven questions it is asking reviewers to decide.

## Layout

| Path | Contents |
|---|---|
| `docs/specification/` | **The single authority on PPCP.** Core model, message catalogue, wire encoding, conformance |
| `src/`, `tests/`, `tools/` | Reference implementation. Empty until the draft is approved. |
