# `libppcp` — conformance claim

**The reference implementation's own claim. Every `pass` below comes from a command in this file, never from a hand.**

| | |
|---|---|
| Implementation | `libppcp`, the MIT reference implementation |
| Against | `PPCP-CORE` revision 9, `PPCP-MSG`, `PPCP-ENC`, `PPCP-CONF` 1.0; `PPCP-RV` revision 8 |
| Wire version | `ppcp/1.0` |
| Session | S1 — L0, L1, L2, L3, L12 · **S2 — L4, L5, L6, L7, L8** |
| Date | 2026-08-22 |
| Matrix | [`matrix.md`](matrix.md) — this file is the human-readable form of the `libppcp` column |

## The claim

> *`libppcp` implements the **Core, Capture, Detect, Mint, Arbitrate, Live, Markup and Offline** profiles of PPCP 1.0 — all eight — and passes every test in `PPCP-CONF` §3 and §4 carrying those profiles.*

**That claim is not yet true and is not yet made.** `CONF` 1a requires a claim to name its profiles, and 1b and 1c require every test carrying them to pass. At the end of session S2 the library implements the wire encoding, the timebase vocabulary, the canonical-instant conversion, the PPCP-RV payload and derivation, the `CORE` §5 type vocabulary, the forty-five-message catalogue, the peer engine and the bundle container. Not built: clock synchronisation (L9), detect/mint/arbitrate (L10), markup (L11) and the synthetic peer (L13) — and without the synthetic peer the *paired* rows are demonstrated only against this library's own second engine, which `CONF` §2c is explicit is not the same thing. What follows is the evidence that exists so far.

`libppcp` also claims `PPCP-RV` conformance **in part**: the pairing-code payload (`RV` §4), the key derivation (`RV` §5.1), the resolvable identifiers (§3.4) and the PSK identity (§5.3). It does **not** implement the TLS profile (§5.2), service discovery (§3) or network join (§6), and cannot: plan A7 and A8 put TLS and discovery in the applications, and `RV` 5.2i says compliance for those clauses is demonstrated by observed handshake rather than by an API. Those rows are `n/a` for this column by construction, as the matrix §5 preamble already records.

## The command

```
cmake --preset dev && cmake --build --preset dev -j4 && ctest --preset dev
```

Nineteen tests, all passing. The same suite passes under `san` (AddressSanitizer + UndefinedBehaviorSanitizer), `cov`, `rel` and `release`. `swift build` builds the same sources as the SwiftPM C target `CPPCP`.

Individual rows are reproduced by name, for example:

```
ctest --preset dev -R test_ct_s1     # CT-S1, CT-I17
ctest --preset dev -R test_ct_s6     # CT-S6 assertion 4 — all forty-five messages
ctest --preset dev -R test_ct_i24    # CT-S6 1-3, CT-I20, the peer engine, ENC 2.1
ctest --preset dev -R test_ct_i12    # CT-I12, CT-I34, the bundle container
ctest --preset dev -R test_ct_i38    # I38's four exits, I36's coverage, ENC §6
ctest --preset dev -R CT-I14         # the threshold grep
ctest --preset dev -R test_rv
```

## Rows moved this session

In the row format of [`matrix.md`](matrix.md). Only the `libppcp` column is this file's to move.

### `CONF` §3 — invariant tests

| Test | Invariant | Profile | Method | Work packages | `libppcp` | PinPointStudio | PinPointCapture |
|---|---|---|---|---|---|---|---|
| CT-I1 | I1 | Core | static | L1, L2 | pass | — | — |
| CT-I3 | I3 | Core | static | L2, L4 | pass | — | — |
| CT-I4 | I4 | Core | static | L2, D2 | pass | — | — |
| CT-I5 | I5 | Capture | paired | L6 | impl | — | — |
| CT-I12 | I12 | Capture | fixture | L8, H3, D3 | **pass** | — | — |
| CT-I13 | I13 | Core | fixture | L1 | impl | — | — |
| CT-I14 | I14 | Core | static | L6, H2 | **pass** | — | — |
| CT-I16 | I16 | Offline | paired | L8, H3 | impl | — | — |
| CT-I17 | I17 | Capture | injected | L3 → CT-S1 | pass | — | — |
| CT-I18 | I18 | Core | paired | L9, H5, D6 | impl | — | — |
| CT-I20 | I20 | Arbitrate | paired | L6, H5 | **pass** | — | — |
| CT-I22 | I22 | Capture | static | L4, D2 | impl | — | — |
| CT-I24 | I24 | Core | injected | L6 → CT-S6 | impl | — | — |
| CT-I29 | I29 | Detect | static | L4, D5 | impl | — | — |
| CT-I31 | I31 | Capture | static | L4, D2 | impl | — | — |
| CT-I30 | I30 | Capture | paired | L7, D4 | impl | — | — |
| CT-I34 | I34 | Offline | fixture | L8, H3, D3 | **pass** | — | — |
| CT-I36 | I36 | Capture | fixture | L7, L8, D4 | impl | — | — |
| CT-I36a | I36 | Capture | paired | L7, H4, D4 | impl | — | — |
| CT-I38 | I38 | Capture | paired | L7, D6 | impl | — | — |

### `CONF` §4 — silent-failure tests

| Test | Invariants | Profile | Method | Work packages | `libppcp` | PinPointStudio | PinPointCapture |
|---|---|---|---|---|---|---|---|
| CT-S1 | I17, I22 | Capture | injected | L3, H4, D4 | pass | — | — |
| CT-S6 | I24 | Core | injected | L5, L6, L13 | impl | — | — |

### `RV` §9 — rendezvous tests

| Test | Method | Asserts | Work packages | `libppcp` | PinPointStudio | PinPointCapture |
|---|---|---|---|---|---|---|
| RT-1 | static | §10.1 derivation vectors | L12 | pass | — | — |
| RT-2 | static | §10.3 codes, `v` first in the all-fields payload, `sid` → UUID text | L12 | pass | — | — |
| RT-3 | injected | unknown `v` → version report | L12, D7 | pass | — | — |
| RT-6 | injected | expired code reported as expired, no connection | L12, H6, D7 | impl | — | — |
| RT-8 | paired | `rid` rotates and resolves under the right `K_id` only | L12, H6, D7 | impl | — | — |
| RT-14 | static | §10.2 PSK identity; differs per connection; empty hint at TLS 1.2 | L12, H1, D1 | pass | — | — |

## What each row rests on, and what is still owed

**CT-I1** — `tests/test_ct_i1.c`. `ppcp_instant_make` is the only constructor and requires `tb`; `ppcp_clock_read` returns an `Instant` rather than a number; `ppcp_instant_encode` revalidates so a hand-zeroed struct cannot reach the wire; the decoder refuses a missing `tb`, an empty `tb`, and a bare integer where an Instant belongs. `Series` and `Interval` carry `tb` on the same terms.

**CT-I3** — `tests/test_ct_i3.c`. `ppcp_relation_make_affine` takes both sigmas as parameters, so the missing-sigma shape has no representation. The receiving half builds the malformed maps by hand, omitting each of the four fields `CORE` 5.4a names in turn. It also asserts the half a validator written only against 5.4a would miss: `unrelated` carrying an affine field is malformed (5.4b), and `unrelated` has no mapping, so `ppcp_relation_apply` refuses it.

**CT-I4** — `tests/test_ct_i4.c`. A relation from a timebase to itself — the only shape "identity asserted by relation" can take — is refused by both constructors and on receipt. Two Timebases sharing an `id` compare equal, and two ids are two clocks.
**Still owed:** the Source-level phrasing of the assertion ("two Sources declared on one clock share one `timebase_id`") needs the `Source` type, which is L4. Re-run then.

**CT-I13** — `tests/test_ct_i13.c` asserts the mechanism the invariant rests on: an unknown key at three nesting levels is skipped and every known field around it survives, including one positioned *after* the unknown, which is the real failure mode. An unknown message type is carried through. An unknown open-registry value passes. Unknown frame-header bits are reported rather than refused (`ENC` 3b).
**`impl`, not `pass`:** the row's method is *fixture* and there is no fixture format until L8, and the unknown `Source.kind` and `Candidate.basis` halves need L4.

**CT-I17 / CT-S1** — `tests/test_ct_s1.c`. `CONF` §3 defines CT-I17 as "see CT-S1", and all six of CT-S1's assertions pass, together with worked examples A–D of `CORE` §6.1.1 to the nanosecond:

1. A `1001120000`, B `1000250000`, C `999750000`, D the `870000` ns difference.
2. Offset `0` against `120000` differs by exactly `120000`, at two different exposures.
3. Doubling the exposure moves the result by exactly half the added exposure.
4. Round trip is bit-exact across four conventions × six timestamps × ten exposures, odd exposures included.
5. Row instants under both directions and `R == 1`, plus `global` geometry.
6. The scalar exposure form and an equivalent constant array give identical instants, frame by frame.

**CT-I18** — `tests/api_surface.cmake` is the static half: no function in any public header takes two `TimebaseRelation`s, which is the signature composition would have whatever it was called.
**`impl`, not `pass`:** the row's method is *paired* — one probe sequence per timebase against a three-timebase peer — which needs L9 and the synthetic peer.

**CT-I22 and CT-I31** — the Timing and geometry halves pass inside `tests/test_ct_s1.c`: `ppcp_timing_make` refuses `nominal_frame_start`, `ppcp_timing_make_nominal_frame_start` requires the offset *and* its provenance, a `start` profile carrying an offset is malformed, a `nominal_frame_start` profile without one is malformed, and `rolling_shutter.readout_ns` cannot be declared without `readout_provenance`.
**`impl`, not `pass`:** both rows are stated over a `CaptureProfile`, which is L4. `AchievedFrames.exposure_ns` provenance is already mandatory-with-the-value here, which is the third quantity I31 names.

**CT-I29** — `tests/test_ct_i29.c` covers the `Estimate` half, which `ENC` 4.1e says is where I29 is made structural.
**`impl`, not `pass`:** the row is stated over `Candidate.tof_correction`, which is L4/L10.

**CT-I12** — `tests/test_ct_i12.c`. A hostless bundle is written through `ppcp_bundle_writer` and read back through `ppcp_bundle_reader` into a real `ppcp_peer`, three times: video-only, IMU-only, and a Session with no Streams at all. Each loads, each yields the Session, the declaration and whatever Streams it had, and none of the three is an error. The reader is additionally driven one byte at a time and consumes nothing until a frame is whole.

**CT-I14** — `tests/no_thresholds.cmake`, run by `ctest` as `CT-I14-no-thresholds`. `CONF` §3 names the method — "grep the implementation's protocol layer" — so it is a grep of `src/` and `include/ppcp/` for a `#define` naming a rate, resolution, quality or confidence together with a judgement word, and for a comparison of a declared rate or confidence against a literal. `0`, `1`, `0.0` and `1.0` are excluded because a confidence in [0, 1] is the field's domain, not an opinion about it. The positive half asserts `ppcp_ingest_policy_fn` exists on the public surface: a library with neither a threshold nor a callback has not moved the decision, it has dropped it. The scan was itself checked by injecting `#define PPCP_MIN_FPS 120` and a `confidence > 0.7`, and it catches both.

**CT-I20** — `tests/test_ct_i24.c`. Two engines both declaring `role: host`: the responder answers `error`/`role_conflict` and closes, and the initiator, which can see the same collision in `hello_accept`, refuses in the same way — I20 is symmetric and only one direction is easy to remember. The second half is the non-host attempting to arbitrate: `ppcp_peer_arm` refuses a peer that is not `role: host` (7.3a), and C2 refuses `shot` and `capture_request` to a peer that declared neither Mint nor Arbitrate.

**CT-I34** — `tests/test_ct_i12.c`. The bundle carries exactly the two Captures the row names: a `complete` one whose `transfer` is `pending` and which therefore has no `digest` yet, and an `absent` one that will never have one. Imported twice through one `ppcp_capture_index`, the count stays at two. The key is `Capture.id` scoped by session and owning peer — the same id from a different peer, or in a different Session, is a different Capture — and the digest is deliberately not in the key, which is what the two awkward Captures exist to prove.

**CT-I38** — `tests/test_ct_i38.c` asserts each of 5.14g's four exits independently, which the row insists on because a test of the first alone would still pass the contradiction the other three were added to fix: a `confirmed` Capture, an `absent` one, one the receiver answered `already_present`, and a discarded preview segment. Then the refusals: `ppcp_transfer_set` refuses `confirmed` and `ppcp_capture_set_transfer` already did, so an owner cannot assert it; a `capture_committed` whose digest does not match confirms nothing; and `ppcp_transfer_mark_shed` — the call a retention policy under storage pressure would make — refuses a shot-anchored Capture while allowing candidate evidence (5.14g1, 5.12.1b).
**`impl`, not `pass`:** the row's method is *paired*, and its last clause — import a bundle, then `capture_committed` on the next connection with the owning peer, accepted against a **closed** Session (5.14h, 5.14h1) — needs two live peers and a session that has ended.

**CT-I36 / CT-I36a** — `tests/test_ct_i38.c` for the coverage rule's four cases and `tests/test_ct_i12.c` for (c) and (d) at the container level. A hole between announced segments is a defect in a `complete`, `partial` and `unknown` Session alike (5.11c1 — nothing truncates a bundle in the middle); an `absent` segment carrying its interval and reason satisfies coverage; an unaccounted tail is a defect only where the Session was asserted `complete`. Overlapping segments are refused (5.14e), and accounting is over announced Captures rather than arrived payload (5.11d). For CT-I36a: a preview Capture holding payload cannot be announced `transfer: pending`, and the bundle writer refuses a `capture_announce` on a Stream it saw opened with `kind: preview`.
**`impl`, not `pass`:** both rows are stated over a replayed session from a real capture device under induced contention.

**CT-S6** — `tests/test_ct_s6.c` carries assertion 4: all forty-five messages of `MSG` §11 are built, encoded on the channel their catalogue row names, and decoded back, through a decoder that takes no profile parameter. `tests/test_ct_i24.c` carries 2 and 3, and the first half of 1: a peer declaring `Core + Arbitrate + Live + Offline` parses a `candidate` with an unknown `basis` and its `tof_correction` completely, never originates one, and answers `error`/`profile_not_supported` to a `stream_open` whose behaviour it does not implement — with both ends still open afterwards.
**`impl`, not `pass`:** assertion 1 also says "and arbitrates over the result", which is L10, and the row's method is *injected* against the synthetic peer of L13.

**RT-1** — `tests/test_rv.c`. `PRK`, `K_tls` and `K_id` of `RV` §10.1 reproduce byte-for-byte, and deriving from a persisted `PRK` (5.1c) gives the same two keys. SHA-256 and HMAC-SHA256 are additionally checked against FIPS 180-4 and RFC 4231, because §10 exercises them only in composition.

**RT-2** — both codes of `RV` §10.3 encode byte-for-byte and decode back to every field: the 75-octet minimal code at 105 URI characters, and the 133-octet all-fields code at 183. The all-fields code is asserted to begin `a8 61 76 01` — `map(8)`, `"v"`, `1` — which is the only vector that can demonstrate 4.3b. `sid` → `Session.id` as canonical lowercase UUID text, and back, with non-canonical forms refused.

**RT-3** — a `v = 2` payload returns `PPCP_ERR_VERSION_NEWER`, a code distinct from every other failure, with no other field acted on (4.2d). A payload whose first key is not `v` is malformed (4.2a); an undecodable payload is an invalid code (4.4b); unknown keys at any depth are ignored (4.2c).

**RT-6** — the 4.4a / 4.4a1 decision helper is implemented and tested in all four states: no `exp`; trusted clock before and after; untrusted clock after, which reports *possibly* expired and attempts anyway.
**`impl`, not `pass`:** the row also asserts "with no connection attempted", which a library holding no socket cannot demonstrate. It is demonstrable only in the applications.

**RT-8** — `ppcp_rv_resolve_rid` and `ppcp_rv_resolve_psk_identity` resolve under the correct `K_id` only, over a set of held pairings, and `rid` changes when `rn` rotates.
**`impl`, not `pass`:** the row's method is *paired* and asserts rotation across re-registration, which is a discovery behaviour and lives in the applications (plan A8).

**RT-14** — the §10.2 PSK identity reproduces byte-for-byte, differs across connections when `rn2` changes (in the tag as well as the nonce), resolves under the correct `K_id` only, and contains no run of four `sid` bytes anywhere in its seventeen. A short identity and a wrong leading version byte are refused rather than transcoded (5.3f).
**The TLS 1.2 `psk_identity_hint` clause is `n/a` for this column** and is demonstrated by the two applications, per plan A7 and the matrix §5 preamble.

## Specification defects and ambiguities found

*Raised, not fixed. Ground rule 3: the specification is the authority and changes first; these are for the L17 errata pass.*

**1. `ENC` §5.1's worked example is not deterministically encoded.** Its key order is `type`, `msg_id`, `probe_seq`, `timebase_id`, `t1`. RFC 8949 §4.2.1 orders by the bytewise order of the *encoded* key, so `"t1"` (`62 74 31`) sorts before `"type"` (`64 74 79 70 65`) and the deterministic order is `t1`, `type`, `msg_id`, `probe_seq`, `timebase_id`. The same holds one level down: the example encodes `t1` as `{tb, ns}` and deterministic order is `{ns, tb}`.

`ENC` 4e makes deterministic encoding a SHOULD, so the example is a legal encoding — but an encoder that honours 4e cannot produce it, and 4e's stated purpose is "it makes fixtures byte-reproducible, which is what makes a regression suite useful". A normative worked example that the recommended encoder cannot reproduce undercuts exactly that.

*Effect here:* `ppcp_message_encode_literal()` exists solely to reproduce it, and the test asserts both forms. *Suggested erratum:* re-emit the §5.1 example in deterministic order (the byte count is unchanged at 87), or state explicitly that the example is illustrative and not deterministic.

**2. `CORE` 6.2d does not specify rounding for the row instant.** `canonical_first + readout_ns × r / (R − 1)` is exact over the reals and is implemented in integer nanoseconds. For any `R − 1` that does not divide `readout_ns × r` — the ordinary case, since `R` is typically 1080 or 2160 — two conformant implementations may differ by one nanosecond, and under `bottom_to_top` they will differ in opposite directions from a truncating implementation. That is below any measurable threshold, but it is the kind of difference a byte-exact fixture comparison will surface.

*Effect here:* round half away from zero, which is exact at both ends under either direction. *Suggested erratum:* one sentence in 6.2d naming a rounding rule.

**3. `RV` §10.3's minimal vector carries `mu: 1`, which 4.3 says is the default.** The vector is reproduced exactly as printed, including `mu`, and 4.3a's byte-identical-code promise depends on a publisher making the same choice. Two publishers that agree on every field but disagree about whether to emit a defaulted `mu` produce different codes for the same pairing. Not a defect in the vector, which is explicit and correct; a gap in 4.3a, which does not say whether an optional field at its default is emitted.

*Suggested erratum:* state in 4.3a whether a defaulted optional field is emitted or omitted.

**4. `ENC` 5a reserves `session_id`, and eight message bodies define a field with that name.** `session_open`, `session_joined`, `session_resume`, `session_state`, `session_close`, `session_offer`, `session_accept` and `session_manifest` all carry `session_id` in their body per `MSG` §4 and §9. 5a forbids a body using the name "as a field name for **any other purpose**" — and these eight use it for exactly the envelope's purpose. Emitting both produces a duplicate key in one map, which `ENC` 4d makes malformed, so the two cannot coexist and one has to be the other.

*Effect here:* the encoder hoists the body's `session_id` into the envelope, which is the position a receiver routing on session reads before it knows the type; the body decoders read it back out of the same flat map, so the wire form is unchanged and unambiguous. *Suggested erratum:* 5a says the reserved names may be used for the envelope's own purpose, or `MSG` §4 and §9 stop listing `session_id` as a body field.

**5. `MSG` §11 tabulates only the profile that confers ORIGINATION, and C3 needs the other one.** `CORE` 2.2.2 C3 requires a peer receiving a message whose *behaviour* it does not implement to answer `error`/`profile_not_supported`. The profile that confers origination is not the profile a responder needs: `candidate` is conferred by Detect and consumed by Arbitrate, so a host declaring `Core + Arbitrate + Live + Offline` would be answering "I cannot understand a Candidate" to the very message the profile split exists to let it read (C1).

*Effect here:* the engine applies C3 only to the **request** class, from a responder-side table it carries itself. Events are comprehended and ignored. *Suggested erratum:* a responder column in the §11 index, or a sentence in C3 saying the obligation is about requests.

**6. `ENC` 7d leaves a third state unnamed.** "The reader treats the Session as `completeness: partial` **only if the bundle itself did not assert otherwise**, and never upgrades a partial Session to complete on the strength of what happened to be present." A bundle that asserted nothing and was not truncated is therefore neither: it is not `partial`, and inferring `complete` is exactly what I10 forbids.

*Effect here:* the reader reports `unknown`, and reports the assertion and the truncation separately so that `CT-I36` (c) and (d) — the same bytes, differing only in what was claimed — stay distinguishable. *Suggested erratum:* 7d names the third state.

**7. `MSG` 8.1i makes an `absent` preview segment unannounceable.** 8.1i forbids announcing a preview Capture with `transfer: pending`. `pending` is the default state of every Capture, and the discarded preview segment 5.11c3 *requires* a peer to announce is `completeness: absent` — it holds no payload, so there is no other transfer state it could honestly carry.

*Effect here:* `absent` preview segments are exempt from the rule, because the rule is about queues and an absent Capture has nothing to queue. *Suggested erratum:* 8.1i reads "a preview Capture **holding payload**", or `Capture.transfer` gains a stated meaning for the payload-less case.

**8. `CORE` 5.3 does not say whether `Timebase.kind` is an open registry.** §10.3 lists eight open registries and `Timebase.kind` is not among them, so it reads as closed — which is what is implemented here (an unknown `kind` is malformed). Worth confirming, because I13 is phrased over "`kind` values" generally and a reader could take it to cover this one.

## What is not claimed

Not started, and named so nobody reads a silence as a claim: CT-I2, I6–I11, I15, I19, I21, I23, I25–I28, I32, I33, I35, I37; CT-S2 (`rig`), S3, S4, S5, S7; RT-4, 5, 7, 9–13, 15–17; every interoperability pairing. `impl` and not yet `pass`: CT-I5, I13, I16, I18, I22, I24, I29, I30, I31, I36, I36a, I38, CT-S6, RT-6, RT-8. The work packages that reach them are L7, L9–L11 and L13–L15 across sessions S3 and S4.

**One thing worth stating plainly about the *paired* rows.** CT-I12, CT-I20 and CT-I34 are demonstrated by running two `libppcp` engines against each other through a byte buffer. That is a real end-to-end run and it is not an interoperability demonstration: `CONF` §2c says an implementation tested only against itself passes I19, I22, I24 and I31 by accident. The synthetic peer of L13 is what turns these into evidence against a foreign declaration, and until it exists these rows rest on one implementation agreeing with itself.
