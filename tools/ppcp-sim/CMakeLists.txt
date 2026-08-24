# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Mark Liversedge
#
# ppcp-sim — the synthetic peer of PPCP-CONF §2c.  Work package L13.
#
# POSIX sockets in C11 and no dependency at all, including for the RT-4 mode.
#
# That mode was written against OpenSSL first, as the work package allowed, and
# OpenSSL turned out not to be able to do it: its client always advertises
# `psk_dhe_ke`, and forcing an empty group list makes the LOCAL stack refuse to
# build a ClientHello before a byte reaches the wire — a failure indistinguishable
# from the host refusal RT-4 is trying to observe.  So sim_tls.c builds the
# ClientHello itself and computes the PSK binder with the library's own
# HKDF-SHA256, and the dependency went away with the defect.

add_executable(ppcp-sim
    main.c
    sim_json.c
    sim_decl.c
    sim_net.c
    sim_run.c
    sim_tls.c
    sim_platform.c
    sim.h
    sim_platform.h
    sim_json.h
    sim_run_steps.inc)

target_link_libraries(ppcp-sim PRIVATE ppcp ppcp_build_flags)
target_include_directories(ppcp-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
    # sim_platform.h's Winsock wrappers and WSAPoll (Vista+) for poll();
    # bcrypt for BCryptGenRandom, replacing /dev/urandom.
    target_link_libraries(ppcp-sim PRIVATE ws2_32 bcrypt)
else()
    # POSIX 2008 for poll(2), getaddrinfo(3) and strtok_r(3) under -std=c11, which
    # defines __STRICT_ANSI__ and would otherwise hide them.
    target_compile_definitions(ppcp-sim PRIVATE _POSIX_C_SOURCE=200809L)
    if(APPLE)
        target_compile_definitions(ppcp-sim PRIVATE _DARWIN_C_SOURCE)
    endif()
endif()

# The scenario declarations sit beside the binary too, so a consumer that
# installs the tool gets something it can actually run.
if(PPCP_INSTALL)
    install(TARGETS ppcp-sim RUNTIME DESTINATION bin)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../scenarios
            DESTINATION share/ppcp)
endif()
