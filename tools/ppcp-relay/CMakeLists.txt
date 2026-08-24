# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Mark Liversedge
#
# ppcp-relay — the deliberate man in the middle of PPCP-RV RT-20b/RT-20c.  L21.
#
# ⛔ IT LINKS NO CRYPTOGRAPHIC LIBRARY AND CONTAINS NO CURVE.  Ground rule 13
# and CA1 keep X25519 out of `libppcp`; §11.11 makes it a parameter supplied
# across a boundary, and here that boundary is a process.  `x25519-agree.sh`
# drives whatever key agreement the machine already has (OpenSSL's `openssl`
# binary by default) and hands back two 32-octet values.  So this tool adds
# NO build dependency to the repository — plan A1's "no dependencies" is
# intact — and the private scalar is never in this process at all (11.11h).
#
# It lives beside `ppcp-conform` deliberately: L21 says both teams must run
# THE SAME relay rather than two, and it emits rows in the format that tool
# already uses.
#
# POSIX only.  It is a test instrument for two macOS teams and it forks a
# shell; there is no Windows story and pretending otherwise would be a seam
# nobody exercises.
if(WIN32)
    message(STATUS "ppcp-relay: POSIX only, not built on this platform")
    return()
endif()

add_executable(ppcp-relay
    main.c
    rl_agree.c
    rl_net.c
    rl_leg.c
    rl_run.c
    rl_report.c
    relay.h)

target_link_libraries(ppcp-relay PRIVATE ppcp ppcp_build_flags)
target_include_directories(ppcp-relay PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(ppcp-relay PRIVATE
    _POSIX_C_SOURCE=200809L
    PPCP_AGREE_HELPER="${CMAKE_CURRENT_SOURCE_DIR}/x25519-agree.sh")
if(APPLE)
    target_compile_definitions(ppcp-relay PRIVATE _DARWIN_C_SOURCE)
endif()

if(PPCP_INSTALL)
    # ⛔ NOT installed.  CA8's validating question is "another project ships
    # this", and a man-in-the-middle harness carrying both legs' key material
    # is precisely the thing that should not arrive on a consumer's PATH.  It
    # is built, run from the build tree, and goes no further.
endif()
