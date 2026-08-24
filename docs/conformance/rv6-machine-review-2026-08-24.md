# RV-6 guided pairing — machine review evidence, 24 August 2026

| | |
|---|---|
| **What this is** | An independent code review of `PPCP-RV` §11's derivation and boundary, performed by **`claude-fable-5`**, an AI model, on 24 August 2026 |
| **What it is NOT** | ⛔ **A discharge of [RT-24a](../specification/ppcp-rv.md#9-conformance) or [RT-27](../specification/ppcp-rv.md#9-conformance).** [`RV` §9](../specification/ppcp-rv.md#9-conformance) asks for a **named reviewer accountable for the reading**, and a model is not one. Those rows remain **maintainer-accepted, not reviewed** — [`matrix.md` §5b](matrix.md#5b-two-review-rows-are-accepted-rather-than-discharged), [`freeze-readiness.md`](freeze-readiness.md) blocking condition 7 |
| **Why it exists** | Because the alternative was nothing. The rows had no reviewer, the maintainer stated he is not qualified to approve a cryptographic review and has no one to consult, and this is materially better than an unexamined derivation. **It is evidence to hand the eventual reviewer** — above all [§4](#4-what-the-reviewer-could-not-determine--start-here), which tells them where to spend their time |
| **Commits reviewed** | `4b47dee` (L18, the derivation), `ad54ae0` (L19, frames), `bf9ee3f` (L20, the engine) |
| **Verified by the orchestrator** | Finding **F1** reproduced independently before recording — see [§3](#3-f1-the-one-substantive-finding-verified) |

---

## 1. What makes this stronger than a reading

⚠ **The reviewer did not only read the code — it reimplemented the specification and recomputed every published vector from scratch**, using its own RFC 7748 Montgomery ladder and Python's `hashlib`/`hmac`, sharing no code with the library.

That covers `pk_i`, `pk_a`, `ct`, `Z` in **both argument orders**, `BK`, `sas_raw`, the SAS, `K_c`, both MACs, the `expand`/`sid` pair, `PRK`/`K_tls`/`K_id`, **both derivation counter-vectors** (cause-1 `PRK 9b779245…`, cause-6 `sid 18dd04b1…`), the **R-11 witness** (the substituted `pk_a' 87abc1e8…` does yield a bit-identical, non-zero `Z`, digits `485158`), and **both interposer-quadruple legs** (`849063` / `576027`).

✅ **Every value reproduced exactly.** That makes this a **third independent reproduction** of §10.4, on top of the two first-party ones — which is the closest thing to the third implementation [`CONF` §2c](../specification/ppcp-conformance.md) says the set otherwise lacks.

## 2. The verdict on the two rows

**RT-24a — the transcript binding: correct in both directions, no violation found.**

- Bound where required: `sas_raw` info is `"ppcp1 sas" ‖ v ‖ pk_i ‖ pk_a`; `K_c` info is `"ppcp1 bs-confirm" ‖ transcript`. 11.6c2 satisfied — and the R-11 recomputation confirms the binding is **genuinely load-bearing**: under the substitution, `Z`, `BK`, `sid` and `PRK` are all identical and **only the transcript-bound `sas_raw` separates the two legs**.
- Bound nowhere else: the commitment hashes `label ‖ pk_i` only; `BK` takes salt + `Z`; both MACs are `HMAC(K_c, label)` with nothing appended; `sid` expands from its label passed **directly, not via the transcript-bearing `info` buffer**; `PRK` goes through §5.1 verbatim.
- Construction unambiguous: all three fields fixed-width (1 + 32 + 32), so no length prefixes are needed and no ambiguity exists. **Initiator-first is fixed by ROLE, not by arrival order** — `on_accept` and `on_reveal` each assign `pk_i`/`pk_a` explicitly.
- On resistance to a future third binding site — the question a review row exists to answer: *"genuinely good. One function, one transcript construction; the `sid` expand doesn't touch the `info` assembly buffer, so binding the transcript into it requires a deliberate rewrite, not a copy-paste slip"*, and the counter-vector tests fail the build if it happens.

**RT-27 — the X25519 boundary: honoured, no violation found**, with two qualifications ([F2](#f2), and the far side of the seam in [§4](#4-what-the-reviewer-could-not-determine--start-here)). Only `pk` and `Z` cross inward; no curve arithmetic anywhere; the derivation reads nothing but its four parameters and static labels; zero-`Z` and malformed-`v` correctly distinguished; erasure covered on every path the library controls, with E51's extension met because every abort routes through `ppcp_rv_bootstrap_wipe`.

**Also checked and found correct**, each being a hazard that would leave the wire byte-identical: 11.5c's ordering (**structurally enforced — `pk_own` is captured at `engine_init`, before any frame exists, so even an embedding that delayed *transmitting* `bs_accept` could not choose `pk_a` in response to `pk_i`**); constant-time commitment and MAC comparison via `ppcp_ct_equal`; MAC reflection (two labels plus an explicit own-MAC rejection, both failures collapsing to `rejected`); the SAS reduction and its bias (excess 7.6×10⁻⁶, immaterial, matching E54's arithmetic); and the frame codec's deterministic CBOR ordering and closed vocabulary.

## 3. F1 — the one substantive finding, verified

⚠ **Erasure discipline weakens below the RV-6 layer, and the file argues against itself.**

`ppcp_rv_bootstrap.c` and `ppcp_bs_engine.c` wipe through volatile pointers. The primitives beneath do not. In `ppcp_hmac_sha256` (`src/ppcp_sha256.c`), `k` is wiped through a volatile pointer **with a comment stating that `memset` is not guaranteed against a determined optimiser** — and the next two lines clear `pad` and `inner` with plain `memset`. **`pad` holds `key ^ 0x5c`, which inverts directly back to the key**, and for the confirmation MACs that key is `K_c`. The `ppcp_sha256 s` state is never cleared at all, so after `hkdf_extract("ppcp1 bootstrap", Z)` its 64-byte buffer retains up to 32 octets of **`Z`** in the dead frame. `ppcp_hkdf_expand` clears `t`/`block` — key stream and PRK-keyed state — with plain `memset`, and `ppcp_rv_derive` clears its `prk` local the same way.

**Orchestrator verification, 24 August 2026:** reproduced by reading `src/ppcp_sha256.c` — the volatile wipe of `k` is at line 172 and the plain `memset` of `pad`/`inner` at lines 175–176, exactly as described.

**Severity.** Not reachable by a network attacker, and it touches neither the wire nor the SAS property. But [7.2e](../specification/ppcp-rv.md#72-handling-the-pairing-secret), [11.6f](../specification/ppcp-rv.md#116-derivation) and **E51** are MUSTs *about memory*, [RT-23](../specification/ppcp-rv.md#9-conformance) is their review row, and **the top layer's care is partly undone one call down**. The fix is mechanical: one shared volatile-wipe helper — the library currently carries two private copies of `wipe()` plus these `memset`s — used wherever a secret-bearing local dies. ⛔ **Worth fixing before RT-23 is signed.**

**F2.** 11.11d is **comment-enforced, not type-enforced**: `ppcp_rv_bootstrap` and `ppcp_bs_engine` are transparent structs in public headers, so a caller *can* read `e->bs.bk` directly. Hard to avoid in allocation-free C, and [CA8](../implementation/cr-01-implementation-plan.md#3-decisions-this-plan-fixes) already accepts the same residue — but a named reviewer should know the boundary is **a discipline, not a compiler guarantee**.

**F3, trivia.** In `derive`, the `v == 0` check precedes the zero-`Z` check, so a caller passing both gets `MALFORMED` rather than `invalid_key`. Unreachable through the engine.

## 4. What the reviewer could not determine — START HERE

⛔ **This section is the reason the document exists. It tells a named reviewer where their time is worth most.**

1. **Machine-code constant-time.** `ppcp_ct_equal` and the zero-`Z` accumulator are constant-time **at source only**. Compiled output was not inspected, and a compiler is licensed to transform either. The same applies to whether the volatile-wipe idiom survives each shipping toolchain; register and spill copies are beyond any source-level fix.
2. **The far side of the seam.** 11.11e/f/g/h — CSPRNG quality, unclamped-scalar handling, the failed-call→`invalid_key` mapping, constant-time X25519, scalar erasure including 11.11h1's CryptoKit bound — live in **PinPointStudio and PinPointCapture**. Nothing in this repository can confirm them. **RT-27's review is only half-done here by construction.**
3. **The CBOR validator.** `ppcp_bs_frame_read` was read line by line, but `ppcp_cbor_validate`'s duplicate-key / tag / indefinite-length rejection was taken **on its documented contract, not by eye**. If it missed duplicate keys, the read loop's `seen |=` would not catch a repeated key (last-wins). No attack was constructible — a sender controls both copies of any duplicated value — but it is the one link verified by contract.
4. **RT-20b and RT-20c remain unrun, and this review substitutes for neither.** The 11.5c enforcement called "structural" above is structural **only for embeddings that use the engine as intended**. The relay is still the only external check.
5. **Everything human** — 11.1d, 11.7d, RT-25, RT-26 — is application and UI, invisible from this repository.

## 5. The reviewer's own caveat, in its words

> *"I am a language model: I can misread code and cannot observe compiled binaries, timing, or the two applications, so this review is evidence for a named reviewer, not a discharge of the rows."*
