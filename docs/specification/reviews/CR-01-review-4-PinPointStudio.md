# CR-01 review pass 4 — PinPointStudio on E43–E48 and the X25519 seam

| | |
|---|---|
| **Reviewing** | `PPCP-RV` revision 9 as amended by **E43–E48** (`4637191` → `19b6379`), the [third review response](../Projects/libppcp/docs/changerequests/CR-01-review-response-3.md), and the [X25519 seam note](../Projects/libppcp/docs/changerequests/CR-01-x25519-seam.md) |
| **Scope** | The unreviewed text again: **§11.11 and the agreed `libppcp` surface**, **E47** (which the seam note itself flags as having landed after the third pass), and the new counter-vectors. §1–§10 and the E34–E46 body are unchanged and have been accepted three times. |
| **Reviewed by** | PinPointStudio, 24 August 2026 |
| **Position** | **Ready to implement.** Nothing below is in the specification proper. Five findings: three in the API note — which is explicitly *decision only, nothing implemented* — one in E47, and one test-row split. |
| **Vectors** | **All three new counter-vectors reproduce**, including the R-11 witness. |

**Nothing was changed in the `libppcp` repository.**

---

## 1. Verification

**The counter-vectors of §10.4 reproduce**, recomputed independently:

```
cause 1  PRK   9b77924572627d0e6d1c51fc679a3596ccd1c4a7dff7943da2ef856ef64dc1ba   ✔
cause 6  sid   18dd04b1da8342a6b4248fb1bd2d0626   ✔   (differs from 1cc4b886… in octet 0)
R-11 witness   pk_a'  87abc1e8…dad34208   ✔
               X25519(sk_i, pk_a') = 7c79d7b5…02956a6a   ✔  identical, non-zero
               digits 485158 (pk_a')  vs  435948 (pk_a)   ✔
```

Publishing counter-vectors is the right call and is the strongest single change in this pass. §10.3 publishes what a correct implementation yields; these publish what three specific wrong ones yield, and the two silent causes now have printed values an implementer can grep for. **The `sid` counter-vector differing in its first octet is a genuinely good piece of vector design** — one printed line settles it, which is exactly what the `PSK_IDENTITY_NOT_FOUND` triage needed.

**The E42 attribution correction is accurate.** I checked both pass-1 reviews as filed: `295 967 296` and the inverted *"and the rest"* are verbatim from PinPointCapture's N1, and PinPointStudio's pass-1 text reads *"values below `967 296` are very slightly favoured"* — the correct count in the correct direction. Only `2.3 × 10⁻⁷` was ours, and it was wrong. Correcting an attribution about who got what wrong, in a note about wrong numbers, is more care than the situation strictly required and it is right.

---

## 2. The seam — accepted, and the reasoning beats the finding that prompted it

**§11.11 and the parameter decision are right, and they are a better answer than the callback PinPointStudio proposed.** Recording that plainly because the finding was ours and the correction to it is the useful part.

The argument in §2 of the seam note is the one that settles it: `ppcp_rv_random_fn` exists because [5.3a1](../Projects/libppcp/docs/specification/ppcp-rv.md#53-psk-identity)'s rejection sampling runs a **loop the library owns** and only the library knows when to stop. Key agreement has no loop — the component holding the scalar already has both inputs and computes `Z` once. `rv.h`'s own header already says *"every random value below is a PARAMETER"*, and B17 was a case of that rule rather than an exception to it. Our proposal reached for the one precedent in the file that is a special case.

**And the third consequence is worth more than the tidiness.** [11.11c](../Projects/libppcp/docs/specification/ppcp-rv.md#1111-where-x25519-comes-from) making the whole chain a pure function of `Z, v, pk_i, pk_a` puts RT-18 in the **one component both applications share**, rather than twice in two applications that would then be agreeing with themselves. That is [B7](../Projects/libppcp/docs/specification/ppcp-rv.md#annex-b--open-issues) addressed rather than restated, and it is the first thing in this whole change request that makes an interop claim cheaper rather than dearer.

[11.11f](../Projects/libppcp/docs/specification/ppcp-rv.md#1111-where-x25519-comes-from) splitting across the boundary — the library sees only the zero, the application only the failure, both map to `invalid_key`, neither is a transport error, neither is retried — is exactly right and is the clause most likely to be got wrong by whoever writes the glue.

**Three findings on the surface, all of them small, none of them design.**

### R-16 — the agreed surface cannot assert two of RT-18's own rows

[RT-18](../Projects/libppcp/docs/specification/ppcp-rv.md#9-conformance) requires reproduction of *"`pk`, the commitment, `Z`, **`BK`**, the six displayed digits, both confirmation MACs, `sid` … and the `PRK`, `K_tls` and `K_id`"*.

`ppcp_rv_bootstrap_derive()` returns `sas`, `k_c`, `mac_i`, `mac_a`, `sid`, `prk`, `k_tls`, `k_id`. **It does not return `BK`**, and it returns the reduced `sas` rather than the four octets of `sas_raw` that §10.4 publishes as a row.

So the component the seam note nominates as RT-18's home cannot assert two rows of the vector RT-18 exists for. That is not fatal — the applications can recompute `BK` from `Z` with one HMAC — but it defeats the specific gain claimed for the design, and it means the shared harness silently checks a subset.

**Add `bk[32]` and `sas_raw[4]` to `ppcp_rv_bootstrap`.** There is no new exposure: the struct already carries `prk` and `k_c`, and the caller already holds `Z`, from which `BK` is a single HMAC anyway.

### R-17 — the commitment has a constant, no function, and is the one `ppcp1` label that will get spelled three times

`PPCP_RV_BS_CT_BYTES` is defined in the proposed header and **nothing in the surface uses it**, which is its own hint. `ct = SHA-256("ppcp1 bs-commit" || pk_i)` ([11.5b](../Projects/libppcp/docs/specification/ppcp-rv.md#115-the-exchange)) is computable from the already-public `ppcp_sha256_hash`, so both applications will write that string themselves.

Every other `ppcp1`-labelled input in this document lives inside the library — `"ppcp1 bootstrap"`, `"ppcp1 sas"`, `"ppcp1 bs-confirm"`, `"ppcp1 rid"`, `"ppcp1 tls-psk"`. The commitment label would be the only one spelled in the applications, twice, and a typo in it is invisible except through §10.4's `ct` row — which RT-18 checks only in whichever component runs it, and per R-16 that is meant to be the library.

**Add `ppcp_rv_bs_commit(const uint8_t pk_i[32], uint8_t out[32])`.** Three lines, no dependency, and it is the difference between one spelling and three. This is [ground rule 1](../Projects/libppcp/docs/implementation/implementation-plan.md) — *if two repositories need the same thing it belongs in `libppcp`* — applied to a string.

### R-18 — one struct holds both what must be erased and what must be kept

`ppcp_rv_bootstrap` carries `k_c` and `sas` alongside `prk`, `k_tls`, `k_id` and `sid`. But:

- [11.6f](../Projects/libppcp/docs/specification/ppcp-rv.md#116-derivation) requires `K_c` erased when the handshake ends, **whether it succeeded or failed**;
- [11.7f](../Projects/libppcp/docs/specification/ppcp-rv.md#117-the-short-authentication-string) forbids the digits being reused, cached or shown after the attempt ends;
- `prk` / `k_tls` / `k_id` / `sid` are precisely what survives.

The natural thing for a caller to do is keep the struct, because it holds the `PRK`. Doing so keeps `K_c` and the digits alive with it, against two MUSTs. The note tells the caller to erase `z` and its own scalar ([11.11h](../Projects/libppcp/docs/specification/ppcp-rv.md#1111-where-x25519-comes-from)) and says nothing about the struct.

**Either split the output** — an ephemeral half and a persistable half — **or state the obligation in the header**: copy out `prk`, `k_tls`, `k_id`, `sid`, then wipe the whole struct. A header comment is enough; RT-23 already covers the property and this is where an implementer will look.

⚠ **Related, one line:** the note says the call *"fails for an all-zero `z` (11.6b) and for `v == 0`"*. [11.11f](../Projects/libppcp/docs/specification/ppcp-rv.md#1111-where-x25519-comes-from) requires the zero case to become `invalid_key` specifically. Those two failures need **distinguishable** results, or the header must say the caller validates `v` before calling — otherwise a conscientious implementation maps a programming error to an attack signal.

---

## 3. R-19 — E47 understates its cost and overstates its privacy gain

The seam note flags E47 as unreviewed. It is the only new clause outside §11, and the analysis behind it is right: with the host accumulating pairings, the counterparts unable to advertise, and one instance advertised at a time, ten pairings at the 15-minute floor is a two-and-a-half-hour wait. The clause is needed.

**Two corrections and a cheaper way to get the same result.**

**(a) It is not *"only multicast chatter"*.** [3.2a](../Projects/libppcp/docs/specification/ppcp-rv.md#32-instance-name) **MUST**s the instance name to be `PPCP-` plus the first four bytes of `rid`. Rotating `rid` therefore **renames the service instance**, which is a full deregister / probe / announce cycle — not a TXT update. At twenty seconds that is continuous, and [3.6a](../Projects/libppcp/docs/specification/ppcp-rv.md#36-multicast-is-not-to-be-relied-on) says of exactly these networks that they rate-limit and drop multicast. **The mitigation for a discovery problem can trigger the condition that breaks discovery**, which is a sharper objection than the cost being merely unwelcome.

**(b) *"strictly better for unlinkability"* is not true within a session.** The `A`/`AAAA` record and the port do not change across a rotation, so an on-link observer links every one of them trivially — the rotations are the same host by inspection. Rotation's real value is the one [3.4e](../Projects/libppcp/docs/specification/ppcp-rv.md#34-resolvable-identifiers) claims: that *"observations in two venues cannot be correlated"*. That is unaffected by rotating faster inside one venue. Faster rotation is **neutral** here, not better, and the clause should not lean on a benefit it does not have.

**(c) The cheap version.** Let a fast-rotating advertiser keep a **stable, random, per-session instance name** and rotate only the TXT `rn` / `rid`. That is one announcement per rotation instead of a full cycle; it satisfies [3.2b](../Projects/libppcp/docs/specification/ppcp-rv.md#32-instance-name) in full, since a per-session random name persists across nothing; and by (b) it costs nothing in privacy that the address does not already cost. It needs a carve-out in 3.2a, which is a MUST written for a peer rotating every fifteen minutes rather than every twenty seconds.

**(d) Checked and sound, recorded so it is not raised as the obvious objection:** fast rotation does **not** leak the count of held pairings. [3.4a](../Projects/libppcp/docs/specification/ppcp-rv.md#34-resolvable-identifiers) already forces rotation, so an observer sees churning `rid`s either way and cannot distinguish one pairing rotating from ten. [B3](../Projects/libppcp/docs/specification/ppcp-rv.md#annex-b--open-issues)'s property survives E47 intact.

---

## 4. R-20 — RT-24b's third block cannot run where its first two do. **Test-row split.**

[RT-24b](../Projects/libppcp/docs/specification/ppcp-rv.md#9-conformance) is `static` and bundles three things: the two derivation counter-vectors, and the R-11 witness.

The first two are pure derivation — they run in the shared component, which is [11.11c](../Projects/libppcp/docs/specification/ppcp-rv.md#1111-where-x25519-comes-from)'s whole point. **The witness needs an actual X25519 call** on `pk_a'`, which per the seam decision the library cannot make: it has no key agreement, by design.

So one third of a single row is unrunnable in the component the other two thirds belong in. Split it, or mark the witness explicitly application-side. Left as one row, a harness author finds part of it impossible and is as likely to drop the row as to split it — and the witness is the block the specification says to run *"before deciding [11.6c2](../Projects/libppcp/docs/specification/ppcp-rv.md#116-derivation) is over-cautious"*, which makes it the one that should be hardest to skip.

---

## 5. Accepted without change

- **11.6c2 and the deletion of the `Z`-commits claim.** Deleting the third leg rather than repairing it is right — the argument carries without it — and turning the finding into a MUST NOT plus a published witness is more than was asked. The ⚠ noting that this is *not* an attack and needs no new check is important and correctly placed: without it the next reader adds a cofactor check that buys nothing.
- **§11.11 in full**, and particularly 11.11e (the scalar crosses unclamped; clamping is idempotent so either side may clamp) — that is exactly the class of silent divergence E39/E41/E46 each landed in, caught before it could happen rather than after.
- **11.4c1** — rejecting unknown map keys. The right call, and stating that it makes B18 a v2-from-scratch matter is the honest consequence rather than a hidden one.
- **11.9d1** — the code offered on the first `unsupported_version`. This is the clause that turns the version gap from *permanently unpairable* into *pairs by code, this once*, and it is why negotiation can stay unanswered.
- **RT-24a reworded to `review`**, and RT-27. RT-27 is the right shape for a boundary obligation nothing downstream can check.
- **Question 3 answered, and the reasoning recorded.** *Host-advertises / device-dials is the better pattern on its own merits* is a stronger statement than *"3.5d leaves no choice"*, and it is the one that will still be true if the platform limitation ever lifts.
- **The relay observation taken up** — building RT-20's relay produces the third both-roles implementation, so build it early rather than last.

---

## 6. Summary

| | Finding | Ask | Where |
|---|---|---|---|
| **R-16** | Surface cannot assert RT-18's `BK` row (or `sas_raw`) | Add `bk[32]`, `sas_raw[4]` to the struct | API note |
| **R-17** | Commitment label would be spelled in both applications | Add `ppcp_rv_bs_commit()` | API note |
| **R-18** | One struct mixes must-erase (`k_c`, `sas`) with must-keep (`prk`…); and zero-`Z` vs `v==0` need distinct results | Split, or state the wipe obligation in the header | API note |
| **R-19** | E47: rotation renames the instance (full mDNS cycle, not a TXT update) on networks 3.6a says drop multicast; *"strictly better for unlinkability"* is neutral within a session | Carve-out in 3.2a for a stable per-session random instance name | Specification |
| **R-20** | RT-24b's witness needs X25519 and cannot run where the row's other two blocks do | Split the row | Specification |

**None of R-16 to R-18 is in the specification** — they are against a document that says of itself *"decision only, nothing is implemented"*, which is the right time to raise them. R-19 and R-20 are a carve-out and a table cell.

### From this side, §11 is done

**§1–§10: yes, unreservedly.** **§11: implement it.** Three passes, fifteen findings, and each pass found a defect the previous pass's fix had introduced — E40 after E34, E43 after E40. That trajectory closed: this pass found nothing in the clauses, and the two most serious findings in the whole change request (E34, E43) were both in *explanations* rather than in normative text, which the document now says in its own header note. That is the right lesson and it is the one worth carrying into the code.

**What has still not moved, and should keep being said:**

- **RT-20 cannot run.** Two implementations either side of a deliberate relay, and neither has written a line. The vectors now reproduce four times across two implementations and an erratum boundary; the counter-vectors reproduce too; **none of that is a demonstration of the property §11 exists to deliver.** No conformance claim to RV-6 until it runs.
- **B14's iOS device run** — a ship gate, not a code gate, and not ours.
- **3.7h.** RV-6 over mDNS works in an office and not at a driving range. The specification is right to say nothing further; the host-side work item is *how a window is reached without multicast*, and it is the difference between conformant and useful.
- **The programme is closed and nothing is scheduled.** RV-6 is a specified, reviewed, unbuilt feature — and on this side the first code change is not ours at all: `ppcp_channel_validate()` still returns `PPCP_ERR_MALFORMED` for channel 255, so `libppcp` cannot emit a bootstrap frame today. **libppcp first, then both applications.**
