# Design review — PinPointCapture and PPCP

**Reviewed as owner of PinPointStudio, the host implementation.**

| | |
|---|---|
| Documents reviewed | `PinPointCapture/docs/capture-companion-requirements.md` (21 Aug 2026, with its 22 Aug review) |
| | `libppcp/docs/specification/` — `PPCP-CORE`, `PPCP-MSG`, `PPCP-ENC`, `PPCP-CONF`, `PPCP-RV` (scope), review disposition — all Draft 1, 22 Aug 2026 |
| Reviewer | PinPointStudio maintainer |
| Basis | A read of both document sets, plus the PinPointStudio source: `src/Gui/shot/`, `src/Buffer/`, `src/Video/`, `src/Audio/`, `src/LaunchMonitor/`, `docs/design/` |
| Date | 22 August 2026 |
| Verdict | **Approve to implement**, with five changes requested. Two of them block writing the conformance suite; none of them blocks starting. |

---

## 0. Summary

The specification is good. It is more rigorous than most of PinPointStudio's own internal
contracts, and several of its provisions — symmetric declaration, the refusal to compose
relations, absence being assertable rather than inferred, bundle-equals-recorded-stream — are
things I would want in PinPointStudio whether or not a phone ever connects to it.

My concerns are not about its quality. They are these:

1. **Two model defects that v1 will ship and the conformance suite will certify.** The
   Candidate-to-Shot identity in the zero-host regime ([F1](#f1)), and the absence of any model of
   *when* a host may issue a Shot ([F2](#f2)).
2. **The launch-monitor path the specification just hardened does not match the launch monitor
   PinPointStudio actually integrates with** ([F4](#f4)). The review comment that prompted the
   change was right that the CSV is not a Candidate; the resolution it received assumes a file
   shape we do not have.
3. **The host obligations are almost entirely unbuilt, and the requirements document describes
   several of them in the present tense** ([F5](#f5)). The most load-bearing timing argument in
   §5.2 rests on protecting a clock-bias estimator that does not exist in `src/`.
4. **§15, "Host-side integration", is two requirements long** ([F7](#f7)), and REQ-HOST-1 names a
   seam the device cannot land on. The host cost of this project is currently invisible in the
   document that scopes it.

Everything below is ordered by severity, and every claim about PinPointStudio names the file it
came from.

---

## 1. Findings

### F1 — `I23` turns every diagnostic candidate into a Shot, in the regime v1 ships {#f1}

**Severity: highest. Blocks writing `CT-S4`.**

Three provisions are individually right and jointly produce a wrong system:

- **`I23` / `CORE` §8.3a** — in a Session with no host, *"every Candidate becomes exactly one
  Shot"*, and no coincidence window is applied.
- **`I8` / `CORE` §5.12c and REQ-PRIV-4** — candidates are never discarded, *including losers and
  rejected ones*, because the diagnostic value is explaining why detection fired, including when
  it fired wrongly. Retention attaches to Candidates precisely because a rejected candidate has
  no Shot.
- **REQ-MIC-5** — the transients to discriminate include **ball-into-screen, roughly 9 ms after
  impact at 3 m**, club-on-mat, dropped club and an adjacent player.

`CONF` §4.4 assertion 2 then states the consequence as a required test: *"Two candidates 10 ms
apart produce two Shots, not one."* Nine milliseconds is inside ten. A correctly-implemented
offline device that nominates both the impact and the screen strike — which `I8` and REQ-PRIV-4
positively encourage it to do — mints **two Shots for one swing**, and passes conformance for
doing so. REQ-OBS-4's diagnostic mode, which deliberately lowers the emission threshold, multiplies
this by whatever the sub-threshold rate turns out to be.

The device's only escape is to suppress the candidate, which destroys exactly the evidence the
candidate-attached retention design exists to preserve. The two requirements are in direct
opposition and the protocol currently resolves it in favour of the wrong one.

**The argument in §2.2.1 for keeping the coincidence window out of Mint is correct** and I am not
asking for it back. Applying a window in a zero-host session really would collapse distinct
candidates and produce different output from the same evidence. The defect is narrower: it is the
*identity* between Candidate and Shot, not the absence of the window.

**Requested change.** Break the identity. Let a Mint peer decide which of its own Candidates it
promotes to a Shot, and require that every Candidate — promoted, unpromoted, sub-threshold — is
still emitted and retained with its evidence. `I23` then reads:

> In a Session with no host, no coincidence window is applied, and every Shot carries exactly one
> Candidate.

That keeps everything §2.2.1 was protecting (no window, no arbitration, one nominator per shot,
`authority: device`), keeps `I8` intact, and stops promotion policy — which is detector tuning,
and therefore squarely `I14` territory — from being fixed by an invariant. `CT-S4` assertion 2
becomes "two candidates 10 ms apart produce two Shots *or* one Shot and one unpromoted Candidate,
and both Candidates are retained either way", with assertion 4 unchanged.

---

### F2 — the protocol does not model *when* a host may issue a Shot, and `I7` makes that expensive {#f2}

**Severity: high. Blocks writing `CT-S4` assertion 4 and any device-side timeout.**

`CORE` §8.2 specifies the coincidence window and specifies that `t0` is never revised (`I7`,
§8.2e). It says nothing at all about how long a host waits before issuing. That gap is not
theoretical for PinPointStudio, because our arbiter cannot issue on the first candidate.

From `src/Gui/shot/shot_arbiter.h`:

```
holdMs       = 200    // collect window opened by the FIRST candidate
matchTolMs   = 40     // cross-modal agreement tolerance
refractoryMs = 1500   // minimum interval between commits
strongConf   = 0.8    // lone-candidate commit threshold
```

and the committed instant is *"the most authoritative agreeing modality"* — the enum order
`Acoustic > Imu > Ball` **is** the priority, because acoustic onset is sample-accurate and ball
launch is coarse. So if the IMU candidate arrives first and the acoustic candidate arrives 30 ms
later, issuing promptly locks `t0` to the worse estimate and `I7` forbids correcting it. Waiting
is the right behaviour and the protocol does not describe it.

Two consequences:

- **A device cannot know when it has been abandoned.** `CORE` §8.3d says a peer whose link drops
  enters the zero-host regime; §7.4c gives three missed heartbeats. But a peer that nominated a
  Candidate to a *live, healthy* host has no stated deadline after which it should conclude no
  Shot is coming and mint its own. At `heartbeat_interval_ms` of 1000 that is a three-second
  ambiguity around every shot, and two conformant implementations can disagree about whether a
  Shot exists.
- **The 50 ms default conflates two different quantities.** `coincidence_window_ns` is a *pairwise
  tolerance* — are these two nominations the same event. What a host also needs is a *hold
  deadline* — how long to collect before deciding. Ours are 40 ms and 200 ms respectively, and
  they are not the same number for the same reason that a tolerance and a timeout are not the
  same thing. `Annex B8` correctly says the 50 ms figure is a proposal rather than a measurement;
  I would add that it is currently being asked to do two jobs.

**Requested change.** Add `Session.issue_hold_ns` beside `coincidence_window_ns`, with a normative
statement that a host MUST NOT issue a Shot before the hold expires on its earliest contributing
Candidate, and that a nominating peer MAY mint locally once the hold plus a stated margin has
passed with no `shot`. One field, one MUST, and it closes a silent interop hazard between two
otherwise conformant peers. This also answers **Q4**: settle both numbers from rig data, but split
the field before measuring, or the measurement will not know which quantity it is estimating.

---

### F3 — `I8` is not satisfiable by PinPointStudio's arbiter, and the arbiter cannot represent two microphones {#f3}

**Severity: high. Host work, not a specification defect — but it is the first work item and it
should be recorded rather than discovered.**

`ShotArbiter::decide()` (`src/Gui/shot/shot_arbiter.h`) sets `m_count = 0` and returns a
`Decision`. The candidates are gone. `cancel()` does the same. Nothing is persisted, nothing
reaches `swing.json`, and there is no id on a candidate to reference if it were. `I8` requires
that candidates — winners, losers and excluded — are retained along with their evidence, and
`CT-I8` asserts the excluded Candidate is present in `Shot.candidates`. We fail that today by
construction.

Worse, and less obvious: **the arbiter models three modalities and a phone makes a fourth
nominator that collides with an existing one.**

```
enum class ArbSource : uint8_t { Acoustic = 0, Imu = 1, Ball = 2 };
static constexpr int kModalities = 3;
```

`decide()` keeps the *highest-confidence candidate per modality*. A phone at a range is a second
acoustic nominator with a different microphone, a different distance and therefore a different
time-of-flight constant — which is exactly why `CORE` §8.1d and `MSG` Annex A.3 model two
microphones as two Sources with two calibrations. Under the current arbiter the host mic and the
phone mic compete for one slot and the loser is silently dropped. That is an `I8` violation, and
it destroys the single thing §6.4 of the requirements says the phone's microphone is *for*:
independent phone↔host clock verification per shot.

Two more collisions worth naming now:

- **`kMaxCandidates = 8`.** A silent truncation, not an error. Diagnostic mode (REQ-OBS-4) plus
  two or three peers will reach it.
- **`refractoryMs = 1500`, enforced in `report()`.** A candidate arriving inside the refractory
  returns `false` and is never collected. So we drop candidates at the front door as well as the
  back, and `I8` is violated before arbitration is even reached.

Nothing here is the protocol's fault. It is the cost of `I8`, it is correct that `I8` exists, and
the work is: give candidates identity, retain them with their evidence, replace the fixed modality
enum with a per-Source collection, and make the refractory a *promotion* rule rather than an
*admission* rule.

---

### F4 — the launch-monitor path the specification hardened does not match the launch monitor we have {#f4}

**Severity: high. This is the one place I think the review cycle reached the right conclusion from
a wrong picture, and has now written the wrong picture into three normative documents.**

The change: `CORE` §8.1b + `I26`, `MSG` §7.1c and `MSG` §9.3c now state that a file-imported
launch monitor record is not a Candidate and is reconciled through `ShotLink` with
`basis: sequence_alignment` or `manual`. Disposition §2.1 treats it as a v1 defect on the strength
of design screen B5, *"Studio holds 29 shots from a launch monitor."*

The premise is right — a CSV row has no Peer, no Timebase and no clock relation, so it cannot be a
Source in the sense nomination requires. The resolution assumes a file shape we do not have.

From `src/LaunchMonitor/gcquad_csv_parser.h` and `gcquad_monitor.h`:

> *"Reads FSX2020's LastShot.CSV — a **two-line file, one header row and one shot**."*
>
> *"The file is written by FSX2020 ... shortly after each shot, and is **REWRITTEN IN PLACE**
> rather than appended to or replaced."*

and from `src/LaunchMonitor/shot_pairing.h`:

> *"It carries **no timestamp we can trust**, and its Shot ID is FSX2020's own counter with **no
> relationship to ours**. Nothing in the file says which of our swings it describes."*
>
> ```
> shot detected  ──► arm for this swing (displacing whatever was armed)
> reading arrives ─► armed?  yes ─► it is this swing's; disarm
>                            no  ─► discard it
> ```

Four consequences:

1. **There is no sequence to align.** `basis: sequence_alignment` is inapplicable to the only
   launch monitor we integrate. The file holds one row. The reasoning in REQ-OFF-12 — *"~50
   ordered shots with inter-shot intervals is a well-determined sequence-alignment problem"* — is
   sound, and describes a session export that FSX2020 does not write to that path.
2. **The 29 rows in screen B5 are not a file.** They are 29 readings PinPointStudio accumulated
   one at a time, by polling every ~250 ms while it was running, each already claimed by an armed
   slot and written into a `swing.json`. They exist as our records, not as importable foreign ones.
3. **If PinPointStudio is not running, the record does not exist.** The row is overwritten by the
   next shot. So in UC-1 and UC-4 — a phone at a range with no host — there is nothing to
   reconcile against later, ever. Any product flow that implies otherwise is promising something
   the file cannot deliver.
4. **In PPCP terms the reading is a third shape the model cannot express.** It arrives *live*, it
   is attributable by *arrival order*, and it has *no clock*. It is not a live nominator (§8.1c
   requires a Timebase); it is not an offline record (§8.1b routes to a reconciliation that needs
   a sequence). Forced to choose, an implementer will either fabricate a timebase — the exact
   failure `5.4b` exists to prevent — or demote a live signal to an offline path that cannot carry
   it.

**Requested change.** Keep the two-path split; it is right. Add the third shape: a record with a
Peer and **no** clock relation, attributable by arrival order rather than by instant, which may be
associated with a Shot without ever being converted into `Session.timebase_ref`. If that is too
much for v1, then at minimum narrow the language: say plainly that `sequence_alignment` presumes a
multi-record export, and that a single-record live-rewritten source is out of scope for
reconciliation and must be paired at capture time by the host. **`Annex B4` should be reopened on
this evidence** — the call it records was taken against a picture of the integration that the code
does not support.

---

### F5 — the host capabilities the argument rests on are proposals and placeholders {#f5}

**Severity: medium as a document defect, high as a planning risk.** Recorded so the schedule is
costed honestly.

Several places in the requirements describe PinPointStudio capabilities in the present tense that
do not exist. The specification then inherits those assumptions.

| Claim | Reality in `PinPointStudio` |
|---|---|
| REQ-EXP-2 rationale: a mismatch *"will corrupt the existing clock-bias estimator (PinPoint fusion P1)"* | `clockBias` appears nowhere in `src/`. P1 is item 1 of an **unimplemented proposal** (`docs/design/analysis_pipeline_fusion_architecture_proposal.md` §279). As proposed it estimates a **per-swing scalar offset** from the ball-launch frame — no rate term, no sigma. PPCP relations require offset **and** skew with **both** sigmas mandatory (`I3`), filtered never stepped (§6.3e), one exchange per timebase (`I21`). None of that machinery exists. |
| §2.1 stage table assigns *extrinsics and multi-device registration* to the host | `src/Gui/calibration/CameraCalibrationFlow.qml`: *"Capture a ChArUco target from both cameras to solve the stereo extrinsics. **This step isn't available yet** — it'll appear here once the calibration pipeline lands."* **UC-2 — the phone behind the golfer resolving occlusion — is the use case that most needs it.** PPCP's `Calibration` entity has nothing to populate it from on the host side. |
| `I19` / `CT-S3`: the host declares `timing`, `geometry` and `intrinsics` for every Source it owns | We hold no readout time, no exposure convention and no intrinsics for the FLIR cameras. `CONF` §4.3 warns that the reference host *"will always pass this by accident"*. PinPointStudio will fail it honestly, because there is nothing to declare. |
| REQ-TIME-1 / `I1`: every timestamp names a timebase | `src/Buffer/types.h`: `IndexEntry { int64_t timestamp_us; ... }` — untagged, microseconds. `docs/design/event_buffer_design.md` design goal 7 is *"Real-time timestamps — sub-100 µs accuracy, **single shared monotonic clock**"*. |
| REQ-CLIP-1: MP4/**HEVC** | `src/Buffer/types.h` `PixelFormat` carries `MJPEG` and `H264_NAL`. **No HEVC.** |
| REQ-SESS-5/6: payload may lag arbitrarily behind the event | `docs/design/deferred_sources_design.md` is **AS BUILT** and is the right seam — but a deferred source has a **deadline**, and its stitch feeds `std::vector<ImuSample>`. There is no deferred *video* equivalent. |

On the timebase point specifically, I want the decision written down before someone tries to
implement it the other way: **PinPointStudio will be a conformant PPCP peer at its edge, and will
remain single-clock internally.** The conversion happens at the protocol boundary; `IndexEntry`
does not gain a timebase id. Microsecond resolution is inside `P4`'s sub-millisecond
recommendation, and the buffer's single-clock guarantee is what makes the merger and the timeline
index work at all. What that costs us is `I19`: to declare our own cameras honestly we have to
*acquire* the conventions we currently assume, which is real work and is not on any plan.

One genuinely good piece of news, and it is worth naming because it is the load-bearing seam:
`deferred_sources_design.md` §3.3 already anticipates this integration by name —

> *"The reserved-id question returns the day a deferred source arrives that was **NEVER live** — a
> force plate, **a phone-hosted sensor**, a camera burst."*

That is `EventBuffer::reserveSourceId(SourceDescriptor)`, designed and deliberately not built. It
is the first buffer-side change the phone requires, and the design note already says what it
should be.

---

### F6 — PinPointStudio's shot pipeline is serialised, and the protocol assumes it is not {#f6}

**Severity: medium-high. The single largest piece of host work the integration implies, and it is
not mentioned anywhere in either document.**

`src/LaunchMonitor/shot_pairing.h` records the invariant explicitly:

> *"ShotProcessor handles exactly one shot at a time, and `ShotController::armed` requires the
> processor to be idle, **so no new shot can be detected while one is processing or replaying**.
> ... analysis alone runs 12–37 s."*

Against that, PPCP requires:

- arm and disarm cycle freely inside one open Session (`MSG` §5.2c, `CORE` §7.3e);
- armed-and-reviewing is *"the normal range state, not an edge case"* (REQ-STATE-4);
- the entire two-channel design exists so shot events keep arriving while payloads lag
  (`CORE` §3.1, REQ-SESS-5);
- *"A session where every shot is correlated and half the video syncs later is a success, not a
  failure."*

A golfer at a range hitting every 20 seconds, with a phone in the session, produces Shots
PinPointStudio cannot currently accept. This is not a protocol defect — the protocol is right and
we are the ones with the constraint — but it is a substantial re-architecture (decouple
correlation from analysis, queue analysis, admit shots while busy) and it needs to appear in the
plan rather than surfacing during integration.

---

### F7 — REQ-HOST-1 names a seam the device cannot land on, and §15 is two requirements long {#f7}

**Severity: medium. Cheap to fix now.**

REQ-HOST-1 says the device *"lands behind PinPoint's existing camera abstraction as another device
implementation, not as a bespoke integration."* It cannot. `src/Video/video_input_base.h` is a
live-push frame source:

```cpp
virtual bool start(const QString &deviceId = {}) = 0;
virtual void stop() / suspend() / resume() = 0;
virtual CameraCapabilities queryCapabilities() const = 0;
signals: void videoFrameReady(const QVideoFrame &frame);
         void rawVideoFrameReady(const RawVideoFrame &frame);
```

A PPCP peer negotiates a version, declares timebases and calibration, nominates candidates, mints
shots, holds a ring buffer we do not control, serves retrospective capture requests and delivers
payload on a second flow-controlled channel — possibly days later, possibly from a file. Putting
that behind `VideoInputBase` would either strip everything the protocol exists to carry, or smuggle
a whole session model in behind a camera interface, which is worse.

**The correct seam is the session/shot layer**, beside `ShotController` and `LaunchMonitorBase`,
not the frame layer. `VideoInputBase` is the right home only for a live *preview* stream, if we
ever want one. The factory-and-abstract-base rule REQ-HOST-1 invokes is right; it is pointing at
the wrong base class.

And §15 needs to be more than two requirements. On the evidence above the host work is:

1. Candidate identity, retention and evidence ([F3](#f3)) — replaces `ShotArbiter`'s fixed
   3-modality enum and its discard-on-decide.
2. Session and Shot identity ([F8](#f8)).
3. A camera calibration pipeline, including extrinsics, without which UC-2 does not function
   ([F5](#f5)).
4. `EventBuffer::reserveSourceId()` and a deferred *video* source ([F5](#f5)).
5. HEVC in `PixelFormat` and a decode path for a phone-authored MP4 ([F5](#f5)).
6. Decoupling correlation from analysis so shots may arrive while the processor is busy ([F6](#f6)).
7. Acquiring and declaring our own cameras' `timing`, `geometry` and `intrinsics` (`I19`).
8. A PPCP boundary layer converting to host time, leaving the buffer single-clock ([F5](#f5)).

That list is the honest cost. None of it is unreasonable; all of it is currently invisible.

---

### F8 — PinPointStudio has no identifiers of the kind the protocol requires {#f8}

**Severity: medium. Decide before the first bundle is written.**

- `Session.id` is a UUID in PPCP. In PinPointStudio a session id **is a filesystem directory
  path** — `src/Gui/review/session_review_controller.h`: *"loadSession() takes the same string as
  `sessionId` — a session's id IS its absolute dir path."*
- `Shot.id` is an opaque `Id` in PPCP. In PinPointStudio it is an `int` ordinal
  (`src/Analysis/diagnostic_ledger.h`, `src/Gui/diagnostics/session_diagnostics_model.h`), and it
  is already persisted in the ledger.
- `CORE` §8.5c keys idempotent re-import on `Session.id` **plus the minting `Peer.id`**, and
  Capture identity on `Capture.digest`. A directory path and an ordinal survive neither.

This lands on top of the `swing.json` backward-compatibility question already open, and the two
should be settled together: identity, recorded-versus-derived split, and analysis provenance
stamping are one decision, not three.

---

## 2. What I checked and think is right

Recorded so it does not get traded away later, and because a review that only lists problems gets
read as a rejection.

- **Two channels, and the explicit refusal to let one TCP connection satisfy it** (`CORE` §3.1,
  `T5`). Right, and the observation that it is invisible in every sequence diagram is exactly why
  it needed saying twice.
- **No composition of relations** (`I18`), with the replacement obligation stated as a *peer*
  obligation binding hosts (`§5.4.1b`, `I21`). The Android `UNKNOWN` argument — that composition
  is unavailable precisely in the case that motivates it — is the best paragraph in the document.
- **Symmetric declaration**, including the host-owning-no-Sources corollary and its wire-level
  form at `MSG` §3.3d. This is the single provision that makes the protocol worth publishing, and
  it is also the most expensive one for PinPointStudio ([F5](#f5)) — which is the evidence that it
  is real rather than decorative.
- **`I14` — no thresholds in the protocol.** Consistent with PinPointStudio's own standing rule
  that analysis gates on available data and devices and never on a session type. The declaration
  model is the same shape, which is a good sign for the two fitting together.
- **`completeness` and `transfer` as independent axes**, and absence being *asserted* rather than
  inferred (`I10`, §5.14a). `partial + present` is a real state we have hit with the wrist sensor
  and currently cannot express.
- **Readiness as a measurement rather than a state name** (`§5.15a`, `MSG` §5.2b). Correct, and
  the MUST NOT in two places is the right protection.
- **Bundle equals recorded stream equals fixture** (`ENC` §7, `CONF` §2b). The decision I would
  defend hardest. It also gives us a regression corpus at no cost, and PinPointStudio's
  re-analysis path has already proved the converge-the-two-paths pattern works —
  `deferred_sources_design.md` §3.1 makes precisely this argument about `SwingDiskSource`.
- **The `Capture.anchor` fix (`I27`) and `Candidate.id`.** Found while writing, and both are
  necessary for anything diagnostic to reference a rejected candidate. Without them [F3](#f3)
  would have no landing place.
- **Build order `Annex A.1`, step 5 before step 9** — bundle before live. Right, and it is also
  the order that lets PinPointStudio consume the phone before doing the arbiter and concurrency
  work in [F3](#f3) and [F6](#f6). See OPEN-6 below.

---

## 3. Answers to the questions asked

### The specification's Q1–Q7

| | Question | Position |
|---|---|---|
| **Q1** | CBOR with text keys | **Keep.** Agreed and not close. The 40% saving is on a message class that totals ~4 KB per sync burst, and our only field-diagnosis channel is a user-attached bundle read by whoever picks up the issue. A wire readable in a hex dump is worth more than the bytes. |
| **Q2** | `SessionLink` defined now | **Keep, but it is the one I would trade.** It is honest about being provisional and costs one type. My reservation differs from the original reviewer's: I would rather the same page had been spent on the issue-timing hole ([F2](#f2)), which is a v1 interop hazard, than on a v3 alignment type. If effort is scarce, `SessionLink` waits and [F2](#f2) does not. |
| **Q3** | `t + offset + d/2` | **Agree, no argument.** Nominal frame start plus the offset is the actual exposure start; half the exposure past that is mid-exposure. The placement is right and the worked examples make it checkable. |
| **Q4** | 50 ms coincidence default | **Do not carry it as a default until the field is split** — it is currently doing two jobs ([F2](#f2)). Ours are 40 ms cross-modal tolerance inside a 200 ms hold. Settle both from rig data as `B8` says, but split first, or the measurement will not know which quantity it is estimating. |
| **Q5** | Version support window | **Two MINOR versions back, with a twelve-month floor, whichever is longer**, and the host states its window in `hello_accept`. The asymmetry is permanent: we ship at FOSS pace and the app is App Store gated. One addition — `unsupported_version` is fatal, so the app cannot tell the user *which end* is stale unless the host says so in the `error` detail. Add that. |
| **Q6** | Candidate audio retention unbounded | **Confirmed, and I accept the division.** The bound is the application's; the protocol's job is expressibility and assertable absence, and `§5.12.1c` and `§13c` do that honestly. Note that in the zero-host regime the retention question and the shot-count question are the same question, so fixing [F1](#f1) also bounds this. |
| **Q7** | `PPCP-RV` does not exist | **Agreed on both halves** — not blocking implementation, blocking the openness claim. **RV-2 (the QR payload, with a version marker in its first field) before any release that ships a QR.** Non-negotiable: it is the one item that cannot be fixed after the fact. Everything else in RV can follow at leisure. |

### The requirements document's open decisions

| | Position |
|---|---|
| **OPEN-3** — minimum device tier | 120 fps at 1080p, **named as PinPointStudio's ingest policy** rather than as a property of the device, exactly as review point 3 asks. Add the optical gate when there is a measured noise figure to gate on; until then the app must say "not measured" rather than showing a cold sample. `I28` makes that enforceable, which is why `I28` was worth adding. |
| **OPEN-4** — app licence | Non-GPL app, MIT library. No PinPointStudio impact; `REQ-LIC-5` keeps LGPL transports on our side of the line, which is the same call we made for `libwrist`. |
| **OPEN-5** — support window | See Q5. |
| **OPEN-6** — tethered-only v1 | **I would rather v1 shipped offline-only than tethered-only.** Offline is what UC-1 needs, it is the path with no clock pressure, `Annex A.1` already recommends bundle before live, and it is the path PinPointStudio can consume *without* first doing the arbiter work ([F3](#f3)) and the concurrency work ([F6](#f6)). Tethered-first front-loads the hardest host work for the least-used case. The document's own answer — the UI can wait, the schema cannot — is right; I am asking to go one step further and let the *live path* wait too. |
| **OPEN-7** — shared vs native core | Agree: decide at port-surface enumeration. The binding constraint (`REQ-PORT-3`, no platform type crosses a boundary) is the part that matters and it is already stated. |

---

## 4. What I want before I sign off

Ordered. None of these blocks starting work; items 1 and 2 block writing the conformance suite,
because `CONF` will otherwise certify the defects.

1. **Fix [F1](#f1)** — break the Candidate-to-Shot identity in `I23`, and amend `CT-S4`
   assertion 2 with it.
2. **Model the issue hold ([F2](#f2))** — one Session parameter and one MUST, plus a stated
   deadline after which a nominating peer may mint locally.
3. **Add the third nominator shape ([F4](#f4))** — a live record with a Peer and no clock — or
   narrow the launch-monitor language and reopen `Annex B4`. As written, no launch monitor
   PinPointStudio integrates with today can be reconciled the way `MSG` §9.3c describes.
4. **Rewrite REQ-HOST-1 and expand §15 ([F7](#f7))** to name the eight host work items. The seam
   is the session layer, not `VideoInputBase`.
5. **Correct the present-tense claims about PinPointStudio ([F5](#f5))** — mark fusion P1 clock
   bias, camera intrinsics and stereo extrinsics as *not built*. The timing argument in §5.2
   currently rests on protecting an estimator that does not exist, and that matters because it is
   the argument the whole exposure-convention contract is justified by. The contract is right
   anyway; it just needs a true reason.

Two things I would add to the plan rather than to the documents:

- **Build the software peer simulator early** (`CONF` §2c). Three of the six silent-failure tests
  are untestable without a synthetic peer that declares something we would not, and it is also the
  only way PinPointStudio's side gets developed without a phone and a golf swing in the loop.
- **The LED timecode rig before the protocol** (REQ-TEST-1). It is the only source of end-to-end
  ground truth, it measures rolling-shutter readout in the same experiment, and it is the only
  thing that will tell us whether our own cameras' declared conventions are right — which is the
  `I19` obligation we currently cannot meet.

---

## 5. One closing observation

The specification says, twice, that if implementation shows something here to be wrong that is the
expected outcome rather than a failure, and that the change belongs in the specification first and
the code second.

Taking that seriously: [F4](#f4) is that case arriving early. The review that produced the
launch-monitor change was reasoning from a design screen, and the code says something different.
The right response is not to patch the screen — it is to fix the model, reopen `B4`, and let the
product flow follow. Doing it in that order is the whole point of having written this down.
