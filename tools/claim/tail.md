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
