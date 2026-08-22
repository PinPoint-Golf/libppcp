# `tools/`

Command line tools that demonstrate compliance. Both hold sockets and files, which is why they live here and not in `src/` — `tests/purity.cmake` gates `src/` and `include/` only, and the library owns no I/O (plan ground rule 8, `CORE` A.3).

| Tool | Package | State |
|---|---|---|
| [`ppcp-sim`](ppcp-sim/) | L13 | **Built.** The synthetic peer `PPCP-CONF` 2c requires: a peer over two TCP connections presenting **any** declaration from a JSON description. See [`scenarios/README.md`](scenarios/README.md) for the declarations, the scenarios and the conformance row each one serves |
| `ppcp-conform` | L14 | Not built — S4. Drives a peer under test through the *paired* and *injected* scenarios, asserting on the wire, and emits JSON plus a Markdown fragment in the row format of [`../docs/conformance/matrix.md`](../docs/conformance/matrix.md) |

Neither has a dependency. `ppcp-sim`'s `--psk-ke-only` mode builds its TLS 1.3 ClientHello and its PSK binder from the library's own HKDF-SHA256 and HMAC-SHA256 rather than linking a TLS stack — see the note at the top of [`ppcp-sim/sim_tls.c`](ppcp-sim/sim_tls.c) for why that turned out to be the only honest way to do it.

## Scripts

| Script | What it does |
|---|---|
| [`run-pair.sh`](run-pair.sh) | Starts two `ppcp-sim` peers over loopback — one listening on a free port it reports through `--port-file`, one dialling it — and fails if either does. The `*-sockets` rows in [`../tests/CMakeLists.txt`](../tests/CMakeLists.txt) are calls to this |
| [`run-psk-ke.sh`](run-psk-ke.sh) | Validates `--psk-ke-only` against a TLS 1.3 server whose answer is known: one run must produce a refusal, the other — the same server with `-allow_no_dhe_kex` — must be reported as an RT-4 **failure**. A mode that can only ever say "refused" is not evidence |
