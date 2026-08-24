# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Mark Liversedge
#
# Tests are named after the conformance row they satisfy wherever there is one
# (CONF §3, §4; RV §9), so `ctest -R CT-` runs the invariant suite and a failing
# row names itself.

set(PPCP_TESTS
    test_cbor
    test_frame
    test_envelope
    test_clock
    test_ct_i1
    test_ct_i3
    test_ct_i4
    test_ct_i13
    test_ct_i29
    test_ct_s1
    test_ct_i12
    test_ct_i24
    test_ct_i38
    test_ct_i21
    test_l9_queue
    test_ct_s4
    test_ct_i35
    test_ct_i37
    test_ct_s6
    test_umbrella
    test_fixtures
    test_rv
    test_rv_bootstrap
    test_rv_sas_uniform
    test_bs_frame
    test_bs_engine)

foreach(t ${PPCP_TESTS})
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} PRIVATE ppcp ppcp_build_flags)
    add_test(NAME ${t} COMMAND ${t})
endforeach()

# The checked-in bundle fixtures of CONF 2b.  The test reads them from the
# SOURCE tree, not from a copy: a fixture that lives beside the build is a
# fixture that can be regenerated without anyone noticing.
target_compile_definitions(test_fixtures PRIVATE
    PPCP_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# ENC 4e — deterministic encoding, asserted against the checked-in bytes.  The
# generator runs into a temporary directory and every file must come out
# byte-identical to the one under version control.  A difference is either a
# codec change that wants explaining in a commit message or a determinism bug,
# and both should be loud rather than quiet.
add_test(NAME L15-fixtures-stable
    COMMAND ${CMAKE_COMMAND}
        -DPPCP_FIXTURE_TOOL=$<TARGET_FILE:ppcp-fixtures>
        -DPPCP_FIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures
        -P ${CMAKE_CURRENT_SOURCE_DIR}/fixtures_stable.cmake)

# CT-I18 (static half) and CT-I9 (API surface) are assertions about what the
# public headers do NOT contain, which no C program can make about itself.
# CONF §3 says of both "assert by API surface, not by behaviour", so the
# assertion is a scan of the headers.
add_test(NAME CT-I18-api-surface
    COMMAND ${CMAKE_COMMAND}
        -DPPCP_INC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../include
        -P ${CMAKE_CURRENT_SOURCE_DIR}/api_surface.cmake)

# CT-I14's method is a grep, and CONF §3 says so: "Grep the implementation's
# protocol layer for a frame-rate, resolution, quality or confidence constant."
add_test(NAME CT-I14-no-thresholds
    COMMAND ${CMAKE_COMMAND}
        -DPPCP_SRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../src
        -DPPCP_INC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../include
        -P ${CMAKE_CURRENT_SOURCE_DIR}/no_thresholds.cmake)

# Plan A3 — every public header compiles ALONE.  A header that leans on
# something another header happened to provide first still builds here and
# fails in a consumer: PinPointCapture imports these as a Clang module and
# found exactly that in C1, in ppcp/bootstrap.h, against a green build here.
add_test(NAME A3-headers-self-contained
    COMMAND ${CMAKE_COMMAND}
        -DPPCP_INC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../include
        -DPPCP_BIN_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -DPPCP_C_COMPILER=${CMAKE_C_COMPILER}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/header_self_contained.cmake)

# The purity gate also runs as a test, so `ctest` reports it by name even
# though the build already failed if it was going to.
add_test(NAME purity
    COMMAND ${CMAKE_COMMAND}
        -DPPCP_SRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../src
        -DPPCP_INC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../include
        -DPPCP_LIB=$<TARGET_FILE:ppcp>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/purity.cmake)

# ---------------------------------------------------------------------------
# The *paired* rows, over real loopback sockets — work package L13.
#
# ⚠ WHY THESE EXIST BESIDE THE C TESTS.  The rows below already passed as two
# `libppcp` engines handing each other bytes through a buffer.  `CONF` §2c is
# explicit that an implementation tested only against itself passes I19, I22,
# I24 and I31 by accident, and matrix.md carries the warning.  These rows run
# the same invariants across TWO PROCESSES, TWO TCP CONNECTIONS and a link
# binding, with the counterpart presenting a declaration from a JSON file that
# no C test wrote — a different `timing.convention`, a different `geometry`,
# `unrelated` timebases, three clocks, a foreign profile set.
#
# `ppcp-sim` exits non-zero on any violation it observes, and each row adds the
# assertions that make it that row rather than a smoke test.
# ---------------------------------------------------------------------------
if(TARGET ppcp-sim)
    set(PPCP_SIM_SCENARIOS ${CMAKE_CURRENT_SOURCE_DIR}/../tools/scenarios)
    set(PPCP_RUN_PAIR      ${CMAKE_CURRENT_SOURCE_DIR}/../tools/run-pair.sh)

    function(ppcp_pair_row name ldecl lscenario lexpect ddecl dscenario dexpect run_ms)
        add_test(NAME ${name}
                 COMMAND sh ${PPCP_RUN_PAIR} $<TARGET_FILE:ppcp-sim>
                         ${PPCP_SIM_SCENARIOS}/${ldecl} ${lscenario} ${lexpect}
                         ${PPCP_SIM_SCENARIOS}/${ddecl} ${dscenario} ${dexpect}
                         ${run_ms})
        set_tests_properties(${name} PROPERTIES TIMEOUT 90)
    endfunction()

    # I7 — a Candidate arriving 700 ms after the Shot was issued ATTACHES, and
    # `t0` is not revised.  The simulator checks the second half ON THE WIRE:
    # a `shot` naming a Shot it has seen with a different `t0` is a violation.
    ppcp_pair_row(CT-I7-sockets
        reference-host.json reference-host
        "issued=1,arbiter_observed=2,candidates_rx=2,t0_revisions=0,relations_composed=0"
        reference-capture.json late-candidate-capture
        "candidates_tx=2,shot_candidates_max=2,t0_revisions=0,relations_composed=0"
        5000)

    # I8 — two Candidates of the SAME `basis` from DIFFERENT peers, both
    # retained on one Shot.  A per-modality slot would drop one, silently.
    ppcp_pair_row(CT-I8-sockets
        acoustic-host.json acoustic-host
        "issued=1,candidates_tx=1,candidates_rx=1,relations_composed=0"
        reference-capture.json nominating-capture
        "shot_candidates_max=2,t0_revisions=0,relations_composed=0"
        5000)

    # I12 / ENC 7a — a stored Session offered over a live link and replayed
    # into the ordinary ingest path: the same bytes, one format.
    ppcp_pair_row(CT-I12-sockets
        reference-host.json reference-host
        "offers_rx=1,captures_unique=1,payload_frames_rx=3,violations=0"
        reference-capture.json offer-session
        "offers_tx=1,accepts_rx=1,replays=1,violations=0"
        4000)

    # F-S5-3 — an offered Session replayed onto a LIVE link is a SECOND Session,
    # and the live one's `timebase_ref` is immutable for its whole life (CORE
    # 4.1a, I16).  Before the fix the engine's immutability guard fired only
    # when the incoming `session_id` MATCHED, so a replay rebound the live
    # `session_id`, `timebase_ref` and `session_params` silently: on the real
    # PinPointStudio-to-PinPointCapture pair every subsequent `shot` carried
    # `t0` in the exporting device's clock while the live Session declared
    # another, and the host's arbiter grouped two Sessions' Candidates as one.
    #
    # `live_ref_rebound` is checked by the counterpart after every event drain
    # and is a violation as well as a counter, so this row fails loudly rather
    # than by an unmet expectation.  `imported_frames_rx` asserts the replay
    # really did arrive — a row that passes because nothing happened is worse
    # than one that fails.
    ppcp_pair_row(F-S5-3-sockets-offer-during-live-session
        reference-host.json reference-host
        "live_ref_rebound=0,imported_frames_rx>=1,offers_rx=1,violations=0"
        reference-capture.json offer-session
        "live_ref_rebound=0,replays=1,violations=0"
        4000)

    # F-S5-2 / CORE 8.4 — a host that ASKS.  No host scenario originated
    # `capture_request` before this, so the DEVICE half of 8.4a — converting a
    # window expressed in the host's own convention into its own buffer's
    # timebase — could not be driven from outside at all, and 8.4b's `absent` /
    # `outside_buffer` answer was unobservable (raised by the S5 interop runs).
    ppcp_pair_row(CT-I22-sockets-capture-request
        reference-host.json requesting-host
        "capture_requests_tx>=1,issued=1,violations=0"
        reference-capture.json nominating-capture
        "capture_requests_rx>=1,violations=0"
        5000)

    # I18 — relations are MEASURED, never composed.  Three declared timebases
    # produce three probe sequences at each end, and `relations_composed`
    # counts held relations spanning two clocks of one peer, which is the only
    # shape a composition could take.
    ppcp_pair_row(CT-I18-sockets
        three-timebase-host.json reference-host
        "probe_timebases=3,relations_composed=0,violations=0"
        three-timebase-capture.json reference-capture
        "probe_timebases=3,relations_composed=0,relations_held>=3"
        5000)

    # I20 — arbitration belongs to a peer with `role: host` and to no other.
    # The engine refuses before a socket is opened, which is the assertion.
    add_test(NAME CT-I20-sockets-refusal
             COMMAND $<TARGET_FILE:ppcp-sim> --role capture --listen 0
                     --declaration ${PPCP_SIM_SCENARIOS}/reference-capture.json
                     --scenario arbitrate-as-capture --run-ms 500)
    set_tests_properties(CT-I20-sockets-refusal PROPERTIES
        WILL_FAIL TRUE TIMEOUT 30)

    # I20's positive half, on the wire: a `shot` claiming `authority: host`
    # from a peer that declared `role: capture` is a violation the receiver
    # raises, and a conformant pair never produces one.
    ppcp_pair_row(CT-I20-sockets
        reference-host.json reference-host
        "issued=1,violations=0,relations_composed=0"
        reference-capture.json reference-capture
        "shots_rx>=1,violations=0,relations_composed=0"
        5000)

    # I21 — one probe sequence per declared timebase, asserted against the
    # HOST as well as the device (CT-S5 assertion 4: a host with several
    # cameras on independent clocks runs it per clock).
    ppcp_pair_row(CT-I21-sockets
        three-timebase-host.json reference-host
        "probe_timebases=3,relations_held>=3,relations_composed=0"
        reference-capture.json reference-capture
        "probe_timebases=1,relations_composed=0,violations=0"
        5000)

    # I34 — re-import identity is `Capture.id` scoped by session and peer.
    # The same stored Session is replayed twice down the same link; the
    # importer sees the Capture twice and imports it once.
    ppcp_pair_row(CT-I34-sockets
        reference-host.json reference-host
        "captures_rx=2,captures_unique=1,captures_duplicate=1,violations=0"
        reference-capture.json offer-session-twice
        "replays=2,accepts_rx=1,violations=0"
        4000)

    # S5 — the relation-composition test, end to end.  Three timebases at each
    # end, a probe sequence per timebase on both, and nothing composed.
    ppcp_pair_row(CT-S5-sockets
        three-timebase-host.json reference-host
        "probe_timebases=3,relations_composed=0,violations=0"
        three-timebase-capture.json reference-capture
        "probe_timebases=3,relations_composed=0,violations=0"
        5000)

    # S6 assertion 1 — "parses `candidate` completely AND ARBITRATES OVER THE
    # RESULT" on a peer declaring Core + Arbitrate + Live + Offline and NOT
    # Detect.  This is the clause that could not be written before L13: the
    # library's own test could only reach comprehension.
    ppcp_pair_row(CT-S6-sockets-arbitrate
        arbiter-no-detect.json arbiter-no-detect
        "arbiter_observed=1,issued=1,candidates_tx=0,violations=0"
        reference-capture.json reference-capture
        "shots_rx>=1,violations=0"
        5000)

    # CT-I6's negative half (CONF 1d), over sockets.  ppcp-conform's row used to
    # pick `reference-host` as the counterpart, and for a peer under test that is
    # ITSELF a host — which is exactly who a negative Mint row is for — CORE 5.2b
    # and MSG 3.2c make that `role_conflict`, fatal, so the row died at `hello`
    # and asserted nothing (found by PinPointStudio, S4).  A minting capture peer
    # is the counterpart it needs.
    #
    # The assertion is `minted_shots_rx`, not `shots_rx`: a host declaring
    # Arbitrate may legitimately send `shot` — the catalogue binds it to the SET
    # Mint / Arbitrate — and 8.2k has it re-send the DEVICE's Shot unchanged.
    # What Mint confers is issuing on one's own authority.
    ppcp_pair_row(CT-I6-sockets
        arbiter-no-detect.json arbiter-no-detect
        "minted_shots_rx=0,issued=1,violations=0"
        reference-capture.json nominating-capture
        "minted_shots_rx=0,violations=0"
        6000)

    # S6 assertions 2 and 3 — an observer declaring Core + Live originates no
    # `candidate` and no `shot`, and answers a request whose behaviour it does
    # not implement with `error` / `profile_not_supported` WITHOUT closing the
    # transport: the frames keep flowing after it.
    ppcp_pair_row(CT-S6-sockets-observer
        reference-host.json reference-host
        "errors_rx>=1,violations=0,frames_rx>=10"
        observer-core.json observer
        "candidates_tx=0,shots_tx=0,violations=0,frames_rx>=10"
        3000)

    # S4 assertion 6 — the live-regime half of the zero-host path: a host that
    # receives a Candidate and never answers, and a peer that mints after the
    # 8.2i deadline and only for what it would have promoted hostless (I32).
    ppcp_pair_row(CT-S4-sockets-silent-host
        reference-host.json silent-host
        "issued=0,arbiter_observed=1,violations=0"
        reference-capture.json nominating-capture
        "minted=1,shots_tx=1,violations=0"
        5000)

    # CONF §5 — the `unrelated` pairing.  The host excludes and RETAINS every
    # Candidate and the peer mints nothing: no zero offset is substituted for
    # a relation that does not exist (8.2i1, MSG 10c).
    ppcp_pair_row(IOP-5-sockets-unrelated
        reference-host.json reference-host
        "arbiter_observed=1,issued=0,retained=1,violations=0"
        unrelated-capture.json unrelated-capture
        "minted=0,retained=1,shots_tx=0,violations=0"
        4000)

    # CONF §5 — a capture peer with a `continuous` Stream and a `preview`.
    # The segment and the discarded preview both reach the host, the preview
    # as `absent` / `not_retained` (5.11c3), and nothing is refused.
    # RT-4 — the psk_ke-only mode, validated in BOTH directions against a TLS
    # 1.3 server whose answer is known.  A mode that can only report "refused"
    # is not evidence: it would report refused for a malformed offer too, which
    # is exactly the false pass this replaced (plan §9, F-L13-2).  So one row
    # asserts a DHE-requiring server produces a refusal, and the other asserts
    # a server started with -allow_no_dhe_kex is reported as an RT-4 FAILURE.
    # The second row is also what proves the hand-built ClientHello and its PSK
    # binder are right: a wrong binder produces an alert, not a ServerHello
    # carrying `pre_shared_key`.
    #
    # Skipped where the OpenSSL CLI is absent — it is the peer under test here,
    # not a dependency of anything that ships.
    find_program(PPCP_OPENSSL_CLI openssl)
    if(PPCP_OPENSSL_CLI)
        add_test(NAME RT-4-psk-ke-only-refused
                 COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/../tools/run-psk-ke.sh
                         $<TARGET_FILE:ppcp-sim> ${PPCP_OPENSSL_CLI} refuse)
        add_test(NAME RT-4-psk-ke-only-accepted-is-a-failure
                 COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/../tools/run-psk-ke.sh
                         $<TARGET_FILE:ppcp-sim> ${PPCP_OPENSSL_CLI} accept)
        set_tests_properties(RT-4-psk-ke-only-refused
                             RT-4-psk-ke-only-accepted-is-a-failure
                             PROPERTIES TIMEOUT 60)
    else()
        message(STATUS "tests: no openssl CLI — the two RT-4 rows that validate "
                       "--psk-ke-only are not registered")
    endif()

    ppcp_pair_row(IOP-9-sockets-preview
        reference-host.json reference-host
        "captures_unique=2,violations=0"
        preview-capture.json preview-capture
        "violations=0,errors_rx=0"
        4000)

    # ================================================ L14 — the reference run
    #
    # `ppcp-conform` driving `libppcp` through the SAME instrument the two
    # applications are measured by (plan A11).  --self stands a second
    # `ppcp-sim` up as the peer under test over loopback, so these rows are two
    # processes, two TCP connections and a wire — not two engines through a byte
    # buffer, which CONF §2c says passes I19, I22, I24 and I31 by accident.
    #
    # Two rows because the row table is keyed on the role of the peer under
    # test: a host and a capture peer are asked different questions.  Negative
    # rows (CONF 1d) are NOT run here and the tool refuses to pretend otherwise
    # — libppcp declares all eight profiles, so it has no undeclared one to
    # prove anything about.
    set(PPCP_ALL_PROFILES core,capture,detect,mint,arbitrate,live,offline,markup)
    add_test(NAME L14-conform-self-host
             COMMAND $<TARGET_FILE:ppcp-conform> --self --role host --quiet
                     --profiles ${PPCP_ALL_PROFILES} --column libppcp)
    add_test(NAME L14-conform-self-capture
             COMMAND $<TARGET_FILE:ppcp-conform> --self --role capture --quiet
                     --profiles ${PPCP_ALL_PROFILES} --column libppcp)
    set_tests_properties(L14-conform-self-host L14-conform-self-capture
                         PROPERTIES TIMEOUT 240)
endif()

# ======================================================= L16 — the two audits
#
# CONF 5b1 and 5b2 are freeze gates, and a gate nobody can run is a gate nobody
# passes.  Both run here.
#
# The profile-boundary audit FAILS the build: it compares the specification's
# own message catalogue with the implementation's, message for message, profile
# for profile, clause for clause, and a drift between them is exactly the defect
# 5b1 describes — a MUST in one document requiring what the other forbids.
add_test(NAME L16-profile-boundary
    COMMAND $<TARGET_FILE:audit-profile-boundary>
            ${CMAKE_CURRENT_SOURCE_DIR}/../docs/specification
            ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# The adjacent-MUST sweep is a CHECKLIST GENERATOR, so what a test can assert is
# that it still parses the specification's clause convention.  That is not
# nothing: the day the convention moves, the sweep goes blind, and it would go
# blind silently.  The tool exits 1 when it parses no clauses at all.
add_test(NAME L16-adjacent-must
    COMMAND $<TARGET_FILE:audit-adjacent-must>
            ${CMAKE_CURRENT_SOURCE_DIR}/../docs/specification
            --out ${CMAKE_CURRENT_BINARY_DIR}/adjacent-must-sweep.md)

# ⛔ RT-20b(v) — THE RELAY'S OWN LEGS, AND THE ONE ASSERTION MOST EASILY
# SKIPPED.  §9's own wording: "each of the relay's own legs completes on
# demand, OR THE HARNESS IS TESTING ITS OWN BUG."  Every other row `ppcp-relay`
# emits is a statement about somebody else's code and each is worthless until
# this passes, because a peer failing against a broken relay fails in a way
# that looks exactly like a defect in the peer — and the cost lands on the team
# least able to see the cause.
#
# This row needs no application, which is why it can be the FIRST thing run
# rather than the last (CA3).  It covers six things:
#   the relay's initiator leg completes against an honest acceptor;
#   its acceptor leg completes against an honest initiator;
#   an interposition end to end, in which the two honest peers see DIFFERENT
#     six digits — §11.8 over sockets rather than as arithmetic;
#   11.5c's ordering, both mirrors (E54/R-23);
#   ⛔ and a NEGATIVE CONTROL: the same probe run against a stand-in built to
#     carry trap 2 must report a FAILURE.  Without it the ordering rows would
#     be untested tests — trap 2 is invisible on the wire, so nothing else in
#     this repository would notice a probe that had gone blind.
#
# ⚠ It is NOT RT-20c and nothing here may be read as it: both honest ends are
# `libppcp`'s own engine.  RT-20c needs PinPointStudio and PinPointCapture in
# those two places and nothing smaller substitutes for it (9g, B7).
#
# Skipped where the OpenSSL CLI is absent, on the same footing as the RT-4 rows
# above: X25519 never enters `libppcp` (ground rule 13, CA1), so the relay
# obtains key agreement across §11.11's boundary from a helper process, and
# `openssl` is the peer under test's supplier rather than a dependency of
# anything that ships.
if(TARGET ppcp-relay)
    find_program(PPCP_OPENSSL_CLI openssl)
    if(PPCP_OPENSSL_CLI)
        add_test(NAME RT-20b-relay-selftest
                 COMMAND $<TARGET_FILE:ppcp-relay> --selftest)
        set_tests_properties(RT-20b-relay-selftest PROPERTIES TIMEOUT 180)
    else()
        message(STATUS "tests: no openssl CLI — RT-20b(v), the relay self-test, "
                       "cannot run and the relay's own legs are UNVERIFIED")
    endif()
endif()

# RT-24c's CURVE half — the R-11 witness of RV §10.4.
#
# ⛔ X25519 IS NOT CONTRIBUTORY, and this is the demonstration rather than the
# assertion.  §10.4 publishes `pk_a` and `pk_a'`; both yield a bit-identical,
# NON-ZERO `Z`, so `BK`, `sid` and `PRK` are the same under the substitution
# and only `sas_raw` separates the two peers — 435948 against 485158.  That is
# why 11.6c2 forbids dropping `pk_i ‖ pk_a` from the SAS info "because Z
# already depends on them" (trap 5): the binding is the ONLY thing that
# separates them, and removing it is undetectable from outside.
#
# The DERIVATION half is in test_rv_bootstrap and needs no curve.  This half
# needs one, so it runs `openssl` directly rather than through ppcp-relay's
# agreement helper — that helper draws its own keypair and refuses to be told
# one (11.5a), which is correct for a handshake and useless for a witness over
# a published vector.  Skipped where the CLI is absent, like the RT-4 rows.
find_program(PPCP_OPENSSL_CLI openssl)
if(PPCP_OPENSSL_CLI)
    add_test(NAME RT-24c-r11-witness
             COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/../tools/rv-r11-witness.sh
                     ${PPCP_OPENSSL_CLI})
    set_tests_properties(RT-24c-r11-witness PROPERTIES TIMEOUT 60)
endif()
