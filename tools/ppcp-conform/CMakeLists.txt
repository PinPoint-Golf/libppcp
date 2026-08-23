# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Mark Liversedge
#
# ppcp-conform — the conformance driver of PPCP-CONF §1, §3 and §4.  L14.
#
# It links nothing of ppcp-sim's: it RUNS ppcp-sim, one process per row, which
# is what makes every row reproducible by hand from the command the JSON carries
# and what stops a row that crashes taking the run with it.  The path to the
# simulator and to the scenario declarations are baked in as defaults so the
# tool works from any working directory, and both are overridable.

add_executable(ppcp-conform
    main.c
    rows.c
    run.c
    report.c
    conform.h)

target_link_libraries(ppcp-conform PRIVATE ppcp_build_flags)
target_include_directories(ppcp-conform PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_dependencies(ppcp-conform ppcp-sim)

target_compile_definitions(ppcp-conform PRIVATE
    _POSIX_C_SOURCE=200809L
    PPCP_SIM_PATH="$<TARGET_FILE:ppcp-sim>"
    PPCP_SCENARIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../scenarios")
if(APPLE)
    target_compile_definitions(ppcp-conform PRIVATE _DARWIN_C_SOURCE)
endif()

if(PPCP_INSTALL)
    install(TARGETS ppcp-conform RUNTIME DESTINATION bin)
endif()
