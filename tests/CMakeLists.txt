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
    test_rv)

foreach(t ${PPCP_TESTS})
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} PRIVATE ppcp ppcp_build_flags)
    add_test(NAME ${t} COMMAND ${t})
endforeach()

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
    ppcp_pair_row(IOP-9-sockets-preview
        reference-host.json reference-host
        "captures_unique=2,violations=0"
        preview-capture.json preview-capture
        "violations=0,errors_rx=0"
        4000)
endif()
