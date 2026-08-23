# `tools/scenarios` — declarations and scenarios for `ppcp-sim`

**The synthetic peer of [`PPCP-CONF` §2c](../../docs/specification/ppcp-conformance.md#2-required-test-infrastructure), and the files that tell it what to be.**

`CONF` 2c requires "a software peer simulator that both sides develop against, capable of presenting a **declaration different from the implementer's own**", and says that without that last capability §4's tests cannot be written at all. This directory is that capability.

Two things vary independently, and keeping them apart is what makes the tool usable from another repository's test suite without patching it:

| | varies by | example |
|---|---|---|
| **What the peer IS** | a JSON declaration in this directory | a different `timing.convention`, a `global` geometry, `provenance: measured` with a non-zero offset, `unrelated` timebases, three clocks, a foreign profile set |
| **What the peer DOES** | a named scenario in the tool | opens the Session, never answers a Candidate, arbitrates late, offers a stored Session, originates nothing |

So "a host that never answers a Candidate" is `--scenario silent-host` over any host declaration, and "a peer with a foreign convention" is any scenario over `foreign-capture.json`.

---

## Running it

```
ppcp-sim --role capture|host|observer
         --listen PORT | --connect HOST:PORT
         --declaration tools/scenarios/<file>.json
         --scenario <name>
         [--expect NAME=VALUE]... [--run-ms MS]
         [--port-file PATH] [--log-prefix NAME] [--quiet]
         [--psk-ke-only --psk HEX --psk-identity TEXT]
         [--list-scenarios] [--help]
```

- `--listen 0` binds a free port and `--port-file` is where it writes the port it got, so any number of runs go in parallel with no port to collide over.
- `--role` states what the caller expected; the role itself belongs to the declaration (`CORE` 5.2a), and a disagreement is refused rather than reconciled.
- Every frame is logged as one line on stderr — direction, channel, message type, size, `msg_id`.
- **Exit 0** means the run completed and every `--expect` held. **Exit 1** means a protocol violation it observed, an unmet expectation, or a transport failure, with a one-line reason on stderr.
- `--expect` also reads `NAME>=VALUE` and `NAME<=VALUE`, because a few counters — how many `relation_update` frames crossed a link in four seconds — are honestly timing-dependent and are not protocol properties.

Two peers at once:

```
tools/run-pair.sh <ppcp-sim> <listen-decl> <listen-scenario> <listen-expect> \
                             <dial-decl>   <dial-scenario>   <dial-expect>   [RUN_MS]
```

`<...-expect>` is a comma-separated list or `-`. This is what the `*-sockets` rows in `tests/CMakeLists.txt` run.

### What it refuses to let past

The tool exits non-zero on any of these, whichever end produced them:

| Check | Clause |
|---|---|
| A `shot` naming a Shot already seen, with a different `t0` | I7 |
| A message originated by a peer whose declared profiles do not confer it | I24, `CONF` §1d |
| `authority: host` on a Shot from a peer that declared `role: capture` | I20, 8.3d |
| A malformed frame, or one past the channel's `ENC` §8 limit | `ENC` 4, 8a |
| A held relation spanning two clocks of one peer — the only shape a composition can take | I18, 5.4c |
| A first frame on a stream that is not `link_bind`, or one whose channel disagrees with its header | `ENC` 2.1c |

The profile check is the one worth pointing at: it is I24 asserted **from the other side**, and it is exactly the check an implementation talking only to itself never makes.

---

## The declarations

| File | Role | Profiles | What is distinctive about it |
|---|---|---|---|
| `reference-host.json` | host | Core, Capture, Detect, Arbitrate, Live, Offline, Markup | A well-behaved host. One camera Source with `convention: start` and `geometry: global` — a machine-vision camera, deliberately not a phone |
| `reference-capture.json` | capture | Core, Capture, Detect, Mint, Live, Offline, Markup | The mobile default: `nominal_frame_start` with offset `0` and provenance **`assumed`** (A12), rolling shutter also `assumed`, and a microphone on the **same** timebase (I4) |
| `foreign-capture.json` | capture | Core, Capture, Detect, Mint, Live, Offline | A camera that is not an iPhone: `convention: start`, `geometry: global`, `intrinsics: fixed`, a `continuous` timebase with `epoch_stable: false` and 17 ppm of skew. **This is the declaration CT-S3 assertion 2 and IOP-2 need** |
| `measured-capture.json` | capture | Core, Capture, Detect, Mint, Live, Offline | `provenance: measured` with a **non-zero** offset (120 000 ns) and a measured `readout_ns`, both with sigmas. **The peer CT-S7 assertion 4 cannot be written without** — an assumed zero is correct against another assumed zero and wrong against this |
| `unrelated-capture.json` | capture | Core, Capture, Detect, Mint, Live, Offline | Declares its clock **`unrelated`** to the host's (5.4b). A legal, complete declaration whose honest consequence is that nothing can be expressed in `timebase_ref` |
| `observer-core.json` | observer | Core, Live | Owns no Source and declares an empty list (MSG 3.3d). The peer IOP-4 and CT-S6 need |
| `arbiter-no-detect.json` | host | Core, Arbitrate, Live, Offline | **No Detect**, no Sources. Parses `candidate` and arbitrates over it; may never originate one |
| `acoustic-host.json` | host | Core, Capture, Detect, Arbitrate, Live, Offline | A host owning its **own** acoustic Source, so two nominators share a `basis` (I8) |
| `three-timebase-capture.json` | capture | Core, Capture, Detect, Mint, Live, Offline | Camera, audio and network clocks, each with its own offset and skew. One probe sequence per timebase, never a composition (I21, I18) |
| `three-timebase-host.json` | host | Core, Capture, Detect, Arbitrate, Live, Offline | The same, on the **host** side: two cameras on independent clocks, which is what CT-S5 assertion 4 asks for by name |
| `preview-capture.json` | capture | Core, Capture, Detect, Mint, Live, Offline | Drives the `continuous` + `preview` Stream shape of I36 and 5.11j |

Every declaration is validated by the library on load, so a file that violates I19, I3, I28 or 5.6e is refused with a reason rather than put on a wire.

### Writing another one

The schema is the `CORE` §5 vocabulary, spelled the way the JSON files above spell it. The two fields with no counterpart in the specification are simulator-only and are on `timebases`:

- `offset_ns` — where this timebase reads relative to the process's monotonic clock, so three declared timebases are three offsets rather than three machines;
- `skew_ppm` — the rate error to inject, so a relation estimator has something to estimate.

That is `CONF` 2a's injectable clock: an offset and a skew are simulated, never waited for.

---

## The scenarios

`ppcp-sim --list-scenarios` prints this table from the source. Roles say which declarations a scenario accepts.

| Scenario | Role | Serves | What it does |
|---|---|---|---|
| `reference-host` | host | IOP-1, IOP-3, CT-I7, CT-I8, CT-I12, CT-I20, CT-I21, CT-I34, CT-S5 | Opens the Session, syncs per declared timebase, arms, arbitrates, accepts an offered Session |
| `reference-capture` | capture | IOP-1, CT-I18, CT-I21, CT-S5 | Opens a Stream per camera Source, syncs, nominates, mints what a host never answers, announces and transfers a Capture |
| `observer` | observer | IOP-4, CT-S6 (2), CT-I24 | Parses everything and originates nothing past `hello`, `declare` and its heartbeat acks |
| `arbiter-no-detect` | host | **CT-S6 (1)**, CT-I24 | Parses `candidate` completely and **arbitrates over the result** — the clause that could not be written before this tool existed |
| `silent-host` | host | IOP-7, CT-S4 (6), CT-I32 | Receives every Candidate and issues no Shot, so the nominating peer's 8.2i deadline is the only thing that fires |
| `late-host` | host | IOP-8, CT-I35, CT-I7 | Arbitrates only after the device was entitled to mint, so 8.2k's attach-rather-than-issue fires |
| `acoustic-host` | host | IOP-6, CT-I8 | Nominates from the host's own acoustic Source alongside the device's |
| `nominating-capture` | capture | IOP-2, IOP-5, IOP-7, IOP-8, CT-S3 (2), CT-S7 (4) | Nominates and mints and nothing else; the declaration decides what is foreign |
| `unrelated-capture` | capture | IOP-5, 8.2i1, CT-I3 | Declares its `unrelated` relation, nominates, and mints **nothing** |
| `requesting-host` | host | **CT-I22 (device half)**, CORE 8.4 | Arbitrates and then **asks for the clip**: a `capture_request` per issued Shot, its window in the host's own convention. Nothing originated one before, so 8.4a on the peer under test — converting that window into its own buffer's timebase — could not be driven from outside (F-S5-2) |
| `arbitrate-as-capture` | capture | CT-I20 | Exists to be **refused**: the engine will not build an arbiter for a peer that is not a host, and the tool exits non-zero before a socket is opened |
| `late-candidate-capture` | capture | CT-I7 | Two Candidates for one event, the second 700 ms after the host issued: it attaches and `t0` does not move |
| `preview-capture` | capture | IOP-9, CT-I36, CT-I36a | A `continuous` metadata Stream and a live-only `preview` alongside shot-windowed video; announces a segment and the discarded preview as `absent` / `not_retained` |
| `offer-session` | capture | IOP-3, IOP-10, CT-I12 | Offers a stored Session and replays its bundle onto the live link when the host accepts |
| `offer-session-twice` | capture | CT-I34 | The same offer, replayed twice: the importer sees each Capture once |

---

## The rows these run as

Registered in [`tests/CMakeLists.txt`](../../tests/CMakeLists.txt); `ctest --preset dev -R sockets` runs all of them.

| ctest row | Pair |
|---|---|
| `CT-I6-sockets` | `arbiter-no-detect` ↔ `nominating-capture` |
| `CT-I7-sockets` | `reference-host` ↔ `late-candidate-capture` |
| `CT-I22-sockets-capture-request` | `requesting-host` ↔ `nominating-capture` |
| `F-S5-3-sockets-offer-during-live-session` | `reference-host` ↔ `offer-session` |
| `CT-I8-sockets` | `acoustic-host` ↔ `nominating-capture` |
| `CT-I12-sockets` | `reference-host` ↔ `offer-session` |
| `CT-I18-sockets` | `three-timebase-host` ↔ `three-timebase-capture` |
| `CT-I20-sockets-refusal` | `arbitrate-as-capture`, alone, expected to fail |
| `CT-I20-sockets` | `reference-host` ↔ `reference-capture` |
| `CT-I21-sockets` | `three-timebase-host` ↔ `reference-capture` |
| `CT-I34-sockets` | `reference-host` ↔ `offer-session-twice` |
| `CT-S5-sockets` | `three-timebase-host` ↔ `three-timebase-capture` |
| `CT-S6-sockets-arbitrate` | `arbiter-no-detect` ↔ `reference-capture` |
| `CT-S6-sockets-observer` | `reference-host` ↔ `observer` |
| `CT-S4-sockets-silent-host` | `silent-host` ↔ `nominating-capture` |
| `IOP-5-sockets-unrelated` | `reference-host` ↔ `unrelated-capture` |
| `IOP-9-sockets-preview` | `reference-host` ↔ `preview-capture` |

---

## What `ppcp-conform` does with these

Work package L14's [`ppcp-conform`](../ppcp-conform/) is a table of conformance rows, and every row in it is one of these declarations with one of these scenarios — the tool holds no peer of its own. So a row is reproducible by hand from the command the tool's JSON reports, and adding a declaration here adds something the conformance run can use without a code change. `../README.md` has the command line and the exit codes.

The two that existed only for this are now used by it: **`foreign-capture.json` is CT-S3** and **`measured-capture.json` is CT-S7**, both against a host under test.

## For PinPointStudio and PinPointCapture

Nothing here is specific to `libppcp` being on the other end. Point `--connect` at a host's listener, or `--listen` and dial it from a device, and the tool is the counterpart:

```
# a foreign capture peer for a host under test
ppcp-sim --role capture --connect 127.0.0.1:9000 \
         --declaration tools/scenarios/foreign-capture.json \
         --scenario nominating-capture --expect violations=0

# a host that never answers, for a device under test
ppcp-sim --role host --listen 9000 --port-file /tmp/port \
         --declaration tools/scenarios/reference-host.json \
         --scenario silent-host --expect issued=0
```

The transport is **plaintext**: `RV` §2's `direct` path is conformant without rendezvous, and a simulator that spoke TLS would be testing OpenSSL rather than PPCP. Both applications have a plaintext loopback path for exactly this (plan D9; PinPointStudio's headless peer).

The one exception is `--psk-ke-only`, which exists so a host's refusal of a PSK-only key exchange is **demonstrated rather than asserted** (RT-4). The mode is hand-built — it needs no TLS library at all, and the two `RT-4-psk-ke-only-*` ctest rows validate it in both directions against a server whose answer is known. See the note at the top of [`../ppcp-sim/sim_tls.c`](../ppcp-sim/sim_tls.c) for what it proves and what it does not.
