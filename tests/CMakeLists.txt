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
    test_ct_i24
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
