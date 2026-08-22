# `tools/`

Command line tools that demonstrate compliance. Empty at session S1 by design — the two that go here are named in the implementation plan:

| Tool | Package | What it is |
|---|---|---|
| `ppcp-sim` | L13 | The synthetic peer `PPCP-CONF` 2c requires: a peer over two TCP sockets that can present **any** declaration from a JSON description — a different `timing.convention`, a different `geometry`, a foreign profile set, `unrelated` timebases, a host that never answers a Candidate. Without it, CT-S3, S4, S6 and S7 cannot be written at all. |
| `ppcp-conform` | L14 | Drives a peer under test through the *paired* and *injected* scenarios, asserting on the wire, and emits JSON plus a Markdown fragment in the row format of `docs/conformance/matrix.md`. |

Both are test infrastructure and both hold sockets, which is why they live here and not in `src/` — `tests/purity.cmake` gates `src/` and `include/` only.
