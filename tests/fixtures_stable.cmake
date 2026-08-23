# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Mark Liversedge
#
# ENC 4e — deterministic encoding, asserted against bytes under version
# control.  Regenerates every fixture into a temporary directory and compares.
#
# This is the check that gives the checked-in fixtures their value.  Without it
# they are just files; with it, a codec change that alters a byte of the wire
# format cannot land quietly.

set(tmp "${CMAKE_CURRENT_BINARY_DIR}/ppcp-fixtures-check")
file(REMOVE_RECURSE "${tmp}")
file(MAKE_DIRECTORY "${tmp}")

execute_process(COMMAND "${PPCP_FIXTURE_TOOL}" "${tmp}"
                RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "ppcp-fixtures exited ${rc}")
endif()

file(GLOB checked_in "${PPCP_FIXTURE_DIR}/*.ppcpb")
if(checked_in STREQUAL "")
    message(FATAL_ERROR "no fixtures are checked in at ${PPCP_FIXTURE_DIR}")
endif()

foreach(f ${checked_in})
    get_filename_component(name "${f}" NAME)
    if(NOT EXISTS "${tmp}/${name}")
        message(FATAL_ERROR "${name} is checked in but the generator no longer writes it")
    endif()
    file(SHA256 "${f}" a)
    file(SHA256 "${tmp}/${name}" b)
    if(NOT a STREQUAL b)
        message(FATAL_ERROR
            "${name} differs from the checked-in fixture.\n"
            "  checked in: ${a}\n"
            "  generated : ${b}\n"
            "Either the encoding changed — say so in the commit and regenerate — "
            "or deterministic encoding (ENC 4e) is broken.")
    endif()
endforeach()

file(GLOB generated "${tmp}/*.ppcpb")
list(LENGTH checked_in n_in)
list(LENGTH generated n_gen)
if(NOT n_in EQUAL n_gen)
    message(FATAL_ERROR
        "the generator writes ${n_gen} fixtures and ${n_in} are checked in; "
        "run tools/ppcp-fixtures over tests/fixtures and commit the result")
endif()

file(REMOVE_RECURSE "${tmp}")
message(STATUS "L15-fixtures-stable: ${n_in} fixtures byte-identical")
