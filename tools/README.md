# `tools/`

Command line tools that demonstrate compliance. Both hold sockets and files, which is why they live here and not in `src/` — `tests/purity.cmake` gates `src/` and `include/` only, and the library owns no I/O (plan ground rule 8, `CORE` A.3).

| Tool | Package | State |
|---|---|---|
| [`ppcp-sim`](ppcp-sim/) | L13 | **Built.** The synthetic peer `PPCP-CONF` 2c requires: a peer over two TCP connections presenting **any** declaration from a JSON description. See [`scenarios/README.md`](scenarios/README.md) for the declarations, the scenarios and the conformance row each one serves |
| [`ppcp-fixtures`](ppcp-fixtures/) | L15 | **Built.** Writes the checked-in bundle fixtures of `CONF` 2b into [`../tests/fixtures/`](../tests/fixtures). `ctest -R L15-fixtures-stable` regenerates them into a temporary directory and compares byte for byte — `ENC` 4e makes that a fair question, and it is what stops a wire-format change landing quietly |
| [`audit-profile-boundary`](audit-profile-boundary/) | L16 | **Built.** `CONF` 5b1: reads the message catalogue from `PPCP-MSG` §11 AND from `src/ppcp_message.c` and asserts they agree message for message, profile for profile, clause for clause; then scans every normative clause in the set for one requiring a peer to originate a message no profile confers. Runs as `ctest -R L16-profile-boundary` |
| [`audit-adjacent-must`](audit-adjacent-must/) | L16 | **Built.** `CONF` 5b2's checklist generator. Groups every normative clause by the section that binds it, and — given a list of changed clause ids — emits only the sections they land in, which is the sweep keyed by revision diff. Runs as `ctest -R L16-adjacent-must` |
| [`ppcp-conform`](ppcp-conform/) | L14 | **Built.** Drives a peer under test through the *paired* and *injected* rows of `PPCP-CONF` §3 and §4, asserting on the wire through `ppcp-sim`, and emits JSON plus a Markdown fragment in the row format of [`../docs/conformance/matrix.md`](../docs/conformance/matrix.md). See below for the command line and the exit codes |

Neither has a dependency. `ppcp-sim`'s `--psk-ke-only` mode builds its TLS 1.3 ClientHello and its PSK binder from the library's own HKDF-SHA256 and HMAC-SHA256 rather than linking a TLS stack — see the note at the top of [`ppcp-sim/sim_tls.c`](ppcp-sim/sim_tls.c) for why that turned out to be the only honest way to do it.

## Scripts

| Script | What it does |
|---|---|
| [`run-pair.sh`](run-pair.sh) | Starts two `ppcp-sim` peers over loopback — one listening on a free port it reports through `--port-file`, one dialling it — and fails if either does. The `*-sockets` rows in [`../tests/CMakeLists.txt`](../tests/CMakeLists.txt) are calls to this |
| [`run-psk-ke.sh`](run-psk-ke.sh) | Validates `--psk-ke-only` against a TLS 1.3 server whose answer is known: one run must produce a refusal, the other — the same server with `-allow_no_dhe_kex` — must be reported as an RT-4 **failure**. A mode that can only ever say "refused" is not evidence |

## `ppcp-conform`

**The instrument all three implementations are measured by** (plan A11). An application tested only by its own unit tests is the single-implementation trap `PPCP-CONF` §2c describes; this drives it from outside, through its real transport, with the synthetic peer as the counterpart.

```
ppcp-conform --profiles LIST --role host|capture|observer
             ( --connect HOST:PORT | --listen PORT | --self )
             [--json PATH] [--markdown PATH] [--column NAME]
             [--only ROW[,ROW...]] [--sim PATH] [--scenarios DIR]
             [--psk HEX [--psk-identity TEXT]] [--list] [--quiet] [--help]
```

| Option | What it means |
|---|---|
| `--profiles` | **The claim** (`CONF` 1a): comma-separated, from `core capture detect mint arbitrate live offline markup`. A claim without a profile set is not a claim, so this is required and must include `core`. |
| `--role` | The role of the **peer under test**. The counterpart takes the complementary role each row names, so `--role host` runs the rows a host must answer and `--role capture` the rows a device must. |
| `--connect` | The peer under test listens; this tool dials it. The usual shape for a headless host or a device harness on loopback. |
| `--listen` | The peer under test dials; this tool listens on the port given. |
| `--self` | The **reference pairing**: a second `ppcp-sim` stands in for the peer under test over loopback. This is how `libppcp` fills its own matrix column with the same instrument the applications use, and it is what `ctest --preset dev -R L14-conform` runs. |
| `--column` | The matrix column the Markdown fragment fills — `libppcp`, `PinPointStudio`, `PinPointCapture`. |
| `--only` | Run a named subset, e.g. `--only CT-I7,CT-I21`. |
| `--sim`, `--scenarios` | Override the built-in paths to `ppcp-sim` and to `tools/scenarios/`. Both default to this build tree, so the tool works from any working directory. |
| `--psk`, `--psk-identity` | Passed through to the counterpart. Reserved for the TLS-PSK transport; see the note below for what is and is not available today. |
| `--list` | Print the row table — id, method, profiles, kind, the role it applies to, and the counterpart it uses — and exit 0. |

### Exit codes

| Code | Meaning |
|---|---|
| **0** | Every applicable row passed. |
| **1** | At least one applicable row failed. The reason is on stderr and in the JSON, per row. |
| **2** | The invocation was wrong: an unknown profile, no `--profiles`, no target, or no `ppcp-sim` at the path. |
| **3** | **No row applied to this claim and role.** Deliberately not 0: a claim naming profiles this tool has no row for has not been measured, and reporting "0 failures" about an empty run is the failure mode the code exists to avoid. |

### Positive and negative rows — `CONF` §1b and §1d

A row for a **declared** profile runs positive: the behaviour the profile confers must be demonstrable. A row for a profile the claim does **not** name runs negative: the messages are parsed and never originated (I24), and the cell is `n/a` when it passes, which is the matrix's vocabulary for exactly that.

`--self` does **not** run negative rows and says so. Under `--self` the peer under test is a `ppcp-sim` reading a declaration *file*, whose profile set is whatever that file says rather than whatever `--profiles` says; a negative row run that way asserts nothing about the claim and can pass by luck. It did, the first time it was tried.

### What it does not do

It does not drive the peer under test. An instrument that told the implementation what to send would be testing its own script. Every assertion is made on the wire by the counterpart, from counters `ppcp-sim` maintains — and the counterpart also refuses, on its own account, every violation in [`scenarios/README.md`](scenarios/README.md)'s table, so `violations=0` on a row is doing more work than it looks like it is.

It also carries **only** the `paired` and `injected` rows. `static` and `fixture` rows are decidable from a declaration or a recorded stream and belong in the implementation's own suite, against the fixtures in this repository. And a paired row whose property is not visible from outside is not here either: CT-I34 — re-import is idempotent — is dropped for exactly that reason, because nothing on the wire distinguishes an importer that de-duplicated from one that imported twice.

### TLS-PSK

The transport is plaintext, which `RV` §2's `direct` path makes conformant and which erratum E4 (`RV` 2c1) states explicitly for test harnesses. `--psk` is passed to the counterpart but `ppcp-sim` has no TLS **transport**: its `--psk-ke-only` mode is a hand-built ClientHello for RT-4 and speaks no application data. So a TLS-1.2-PSK-only device — `Network.framework`, `RV` 5.4b1 — **cannot be driven by this tool today**, and the honest answer is a plaintext harness path in the device (plan D9), fenced to a non-shipping build. That is what PinPointCapture does and it is why erratum E4 was written.

### Example

```sh
# the reference run, both roles
ppcp-conform --self --role host    --profiles core,capture,detect,mint,arbitrate,live,offline,markup \
             --column libppcp --json host.json --markdown host.md
ppcp-conform --self --role capture --profiles core,capture,detect,mint,arbitrate,live,offline,markup \
             --column libppcp --json capture.json

# a headless host listening on 9000
ppcp-conform --connect 127.0.0.1:9000 --role host \
             --profiles core,capture,detect,arbitrate,live,offline,markup \
             --column PinPointStudio --json pps.json --markdown pps.md
```

## The two audits — `PPCP-CONF` §5b1 and §5b2

Both are freeze gates, and a gate nobody can run is a gate nobody passes.

```
audit-profile-boundary SPEC_DIR SRC_DIR
audit-adjacent-must    SPEC_DIR [--changed 6.1g,7.3f,…] [--changed-from FILE] [--out PATH]
```

**`audit-profile-boundary` fails the build.** The defect 5b1 describes is a MUST written in one section requiring a peer to originate a message the catalogue binds to a profile that peer need not declare: every peer that obeys the MUST then violates I24, and every peer that obeys I24 violates the MUST. Neither implementation notices, because each reads one document at a time. So this reads both — the specification's own catalogue and `src/ppcp_message.c`'s — and compares them exactly. It also prints the messages no MUST anywhere requires anyone to originate; that is not a failure (an OPTIONAL message is a real thing) but it is where a 5b2 sweep starts, because a message nothing requires is a message nothing tests.

It deliberately does **not** infer which profile a clause's *subject* holds. "A capture peer", "an arbitrating host" are prose, and inferring a profile set from prose would produce confidence rather than evidence. What it can decide exactly it decides; what it cannot, it prints.

**`audit-adjacent-must` decides nothing at all, on purpose.** A contradiction between two MUSTs is a question about meaning and no program can read. What it removes is the excuse: `CORE`'s revision 8 accepted this as a process change after the *fourth* instance of a new MUST contradicting one in an adjacent section, and every one of the four was found by a reviewer reading two sections at once and missed by everyone reading them one at a time. Keyed by revision diff, it emits exactly the sections the changed clauses land in, with every clause in each of them and a checkbox.

[`../docs/conformance/adjacent-must-sweep-s4.md`](../docs/conformance/adjacent-must-sweep-s4.md) is the sweep for session S4's three errata (E2, E3, E4), generated by:

```sh
audit-adjacent-must docs/specification --changed 6.1g,7.3a,7.3f,7.5c,2c,2c1 \
                    --out docs/conformance/adjacent-must-sweep-s4.md
```

## `ppcp-fixtures`

```
ppcp-fixtures DIRECTORY
```

Writes the eight bundle fixtures `tests/test_fixtures.c` reads back. They are under version control on purpose: a fixture a test builds in memory and reads back in the same process proves the codec agrees with itself, and one written at an earlier commit proves the format did not move. Regenerate with

```sh
tools/ppcp-fixtures tests/fixtures    # then commit the diff, and say why in the message
```
