# PPCP conformance matrix

**The compliance record for the three implementations. Updated at the end of every orchestration session; `pass` cells come from a command, never from a hand.**

| | |
|---|---|
| Against | `PPCP-CONF` 1.0 §3–§5; `PPCP-RV` 1.0 §9 |
| Plan | [`../implementation/implementation-plan.md`](../implementation/implementation-plan.md) §8 defines the cell vocabulary |
| Claims | `libppcp`: [`claim-libppcp.md`](claim-libppcp.md) · PinPointStudio: `PinPointStudio/docs/ppcp-conformance.md` · PinPointCapture: `PinPointCapture/docs/ppcp-conformance.md` |
| Last updated | 2026-08-22 — matrix created, nothing run |

Cells: `—` not started · `impl` code exists, not passing · `pass` passing, command in the claim file · `n/a` profile not declared, negative test passes · `rig` needs the LED timecode rig · `review` RV review method, reviewer and commit recorded · `blocked: …`

Profile columns: `libppcp` declares all eight profiles. PinPointStudio (host) declares Core, Capture, Detect, Arbitrate, Live, Offline, Markup. PinPointCapture (device) declares Core, Capture, Detect, Mint, Live, Offline, Markup.

## 1. Invariant tests — `CONF` §3

| Test | Invariant | Profile | Method | Work packages | `libppcp` | PinPointStudio | PinPointCapture |
|---|---|---|---|---|---|---|---|
| CT-I1 | I1 | Core | static | L1, L2 | — | — | — |
| CT-I2 | I2 | Core | fixture | L8, D4 | — | — | — |
| CT-I3 | I3 | Core | static | L2, L4 | — | — | — |
| CT-I4 | I4 | Core | static | L2, D2 | — | — | — |
| CT-I5 | I5 | Capture | paired | L6 | — | — | — |
| CT-I6 | I6 | Mint, Arbitrate | static | L4, L10, H5, D5 | — | — | — |
| CT-I7 | I7 | Mint, Arbitrate | paired | L10, H5, D5 | — | — | — |
| CT-I8 | I8 | Mint, Arbitrate | paired | L10, H5, D5 | — | — | — |
| CT-I9 | I9 | Core | static | L4, L10 | — | — | — |
| CT-I10 | I10 | Capture | paired | L7, D4 | — | — | — |
| CT-I11 | I11 | Capture | fixture | L7, D4 | — | — | — |
| CT-I12 | I12 | Capture | fixture | L8, H3, D3 | — | — | — |
| CT-I13 | I13 | Core | fixture | L1 | — | — | — |
| CT-I14 | I14 | Core | static | L6, H2 | — | — | — |
| CT-I15 | I15 | Offline | fixture | L8, H3 | — | — | — |
| CT-I16 | I16 | Offline | paired | L8, H3 | — | — | — |
| CT-I17 | I17 | Capture | injected | L3 → CT-S1 | — | — | — |
| CT-I18 | I18 | Core | paired | L9, H5, D6 | — | — | — |
| CT-I19 | I19 | Core | injected | L4 → CT-S3 | — | — | — |
| CT-I20 | I20 | Arbitrate | paired | L6, H5 | — | — | — |
| CT-I21 | I21 | Live | paired | L9, H5, D6 | — | — | — |
| CT-I22 | I22 | Capture | static | L4, D2 | — | — | — |
| CT-I23 | I23 | Mint | injected | L10 → CT-S4 | — | — | — |
| CT-I24 | I24 | Core | injected | L6 → CT-S6 | — | — | — |
| CT-I25 | I25 | Offline | static | L4 | — | — | — |
| CT-I26 | I26 | Detect | static | L4, L10, D5 | — | — | — |
| CT-I27 | I27 | Capture | static | L4, D4 | — | — | — |
| CT-I28 | I28 | Capture | static | L4, D2 | — | — | — |
| CT-I29 | I29 | Detect | static | L4, D5 | — | — | — |
| CT-I30 | I30 | Capture | paired | L7, D4 | — | — | — |
| CT-I31 | I31 | Capture | static | L4, D2 | — | — | — |
| CT-I32 | I32 | Mint | injected | L10, D5 | — | — | — |
| CT-I33 | I33 | Detect | injected | L10, D5 | — | — | — |
| CT-I34 | I34 | Offline | fixture | L8, H3, D3 | — | — | — |
| CT-I35 | I35 | Arbitrate | injected | L10, H5 | — | — | — |
| CT-I36 | I36 | Capture | fixture | L7, L8, D4 | — | — | — |
| CT-I36a | I36 | Capture | paired | L7, H4, D4 | — | — | — |
| CT-I37 | I37 | Markup | static | L11, H7, D8 | — | — | — |
| CT-I38 | I38 | Capture | paired | L7, D6 | — | — | — |

## 2. Silent-failure tests — `CONF` §4

| Test | Invariants | Profile | Method | Work packages | `libppcp` | PinPointStudio | PinPointCapture |
|---|---|---|---|---|---|---|---|
| CT-S1 | I17, I22 | Capture | injected | L3, H4, D4 | — | — | — |
| CT-S2 | I22 | Capture | **rig** | — | rig | rig | rig |
| CT-S3 | I19 | Core | injected | L13, H2, D2 | — | — | — |
| CT-S4 | I20, I23 | Mint | injected | L10, L13, D3, D5, D6 | — | — | — |
| CT-S5 | I18 | Core | paired | L9, H5, D6 | — | — | — |
| CT-S6 | I24 | Core | injected | L5, L6, L13 | — | — | — |
| CT-S7 | I31 | Capture | injected | L13, D2 | — | — | — |

## 3. Interoperability pairings — `CONF` §5a

| # | A | B | Proves | Session | Status |
|---|---|---|---|---|---|
| IOP-1 | Reference device | reference host | happy path | S5 | — |
| IOP-2 | Reference device | synthetic third-party host, different camera conventions | I19, I22 | S5 | — |
| IOP-3 | Reference device, no host → bundle | reference host import | I20, I23, I16, I9 | S5 | — |
| IOP-4 | Reference host | observer-only peer (Core + Live) | I24 | S5 | — |
| IOP-5 | Reference host | peer declaring `unrelated` timebases | I3, 8.2i1 | S5 | — |
| IOP-6 | Reference host owning an acoustic Source | device with an acoustic Source | I8 | S5 | — |
| IOP-7 | Reference host that never issues `shot` | nominating peer | I32 | S5 | — |
| IOP-8 | Reference host delayed past the mint deadline | nominating peer | I35 | S5 | — |
| IOP-9 | Reference host | capture peer with `continuous` + `preview` Streams | I36 | S5 | — |
| IOP-10 | Bundle written by A | read by B, both directions | `ENC` 7a | S5 | — |

"Reference device" and "reference host" are, for this programme, PinPointCapture on the simulator and PinPointStudio; `ppcp-sim` stands in for the synthetic and degraded peers. `CONF` 5c (a pairing by an implementation not written by the reference team) remains open until a third party exists.

## 4. Freeze gates — `CONF` §5b1, §5b2

| Gate | Work package | Status |
|---|---|---|
| Profile-boundary audit runs in `ctest` and passes | L16 | — |
| Adjacent-MUST sweep run against revision 9 and recorded | L16, L17 | — |

## 5. PPCP-RV tests — `RV` §9

`libppcp` implements only the payload, derivation and identity parts of RV (plan A7, A8); rows that need a handshake, a socket or storage are `n/a` for it by construction and are demonstrated by the two applications.

| Test | Method | Asserts | Work packages | `libppcp` | PinPointStudio | PinPointCapture |
|---|---|---|---|---|---|---|
| RT-1 | static | §10.1 derivation vectors | L12 | — | — | — |
| RT-2 | static | §10.3 codes, `v` first in the all-fields payload, `sid` → UUID text | L12 | — | — | — |
| RT-3 | injected | unknown `v` → version report | L12, D7 | — | — | — |
| RT-4 | injected | strongest mode negotiated, never plaintext, outcome surfaced | H1, D1 | n/a | — | — |
| RT-5 | paired | second handshake on a `mu: 1` code refused | H6 | n/a | — | n/a |
| RT-6 | injected | expired code reported as expired, no connection | L12, H6, D7 | — | — | — |
| RT-7 | paired | TXT and instance name carry nothing persistent | H6, D7 | n/a | — | — |
| RT-8 | paired | `rid` rotates and resolves under the right `K_id` only | L12, H6, D7 | — | — | — |
| RT-9 | paired | diagnostic export carries no secret or payload | H6, D7 | n/a | — | — |
| RT-10 | injected | `session_resume` refused without a completed handshake | H1, D1 | n/a | — | — |
| RT-11 | injected | unknown identity and wrong key indistinguishable | H1 | n/a | — | n/a |
| RT-12 | **review** | CSPRNG at full width, protected storage, erasure | H6, D7 | n/a | — | — |
| RT-13 | **review** | network join with consent; not left attached | D7 | n/a | n/a | — |
| RT-14 | static | §10.2 PSK identity; differs per connection; empty hint at TLS 1.2 | L12, H1, D1 | — | — | — |
| RT-15 | paired | publisher refuses past `exp`; untrusted clock attempts | H6, D7 | n/a | — | — |
| RT-16 | **review** | no `PRK` persisted from `mu > 1` | H6, D7 | n/a | — | — |
| RT-17 | **review** | every platform mode offered, from a capability query | H1, D1 | n/a | — | — |
