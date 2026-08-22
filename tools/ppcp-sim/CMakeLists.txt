# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Mark Liversedge
#
# ppcp-sim — the synthetic peer of PPCP-CONF §2c.  Work package L13.
#
# POSIX sockets in C11, no dependency except the library itself.  The one
# exception is the RT-4 mode, which needs a TLS stack to offer `psk_ke` alone;
# it is OPTIONAL at configure time and its absence disables that mode and
# nothing else.  libppcp itself never links OpenSSL — purity.cmake gates the
# library, and this target is not the library.

add_executable(ppcp-sim
    main.c
    sim_json.c
    sim_decl.c
    sim_net.c
    sim_run.c
    sim_tls.c
    sim.h
    sim_json.h
    sim_run_steps.inc)

target_link_libraries(ppcp-sim PRIVATE ppcp ppcp_build_flags)
target_include_directories(ppcp-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# POSIX 2008 for poll(2), getaddrinfo(3) and strtok_r(3) under -std=c11, which
# defines __STRICT_ANSI__ and would otherwise hide them.
target_compile_definitions(ppcp-sim PRIVATE _POSIX_C_SOURCE=200809L)
if(APPLE)
    target_compile_definitions(ppcp-sim PRIVATE _DARWIN_C_SOURCE)
endif()

option(PPCP_SIM_TLS "Build ppcp-sim's psk_ke-only mode (needs OpenSSL 1.1.1+)" ON)
if(PPCP_SIM_TLS)
    find_package(OpenSSL 1.1.1 QUIET)
    if(OpenSSL_FOUND)
        target_link_libraries(ppcp-sim PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        target_compile_definitions(ppcp-sim PRIVATE PPCP_SIM_TLS=1)
        message(STATUS "ppcp-sim: --psk-ke-only enabled (OpenSSL ${OPENSSL_VERSION})")
    else()
        message(STATUS "ppcp-sim: OpenSSL not found — --psk-ke-only is unavailable "
                       "in this build (RT-4 stays `impl`)")
    endif()
endif()

# The scenario declarations sit beside the binary too, so a consumer that
# installs the tool gets something it can actually run.
if(PPCP_INSTALL)
    install(TARGETS ppcp-sim RUNTIME DESTINATION bin)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../scenarios
            DESTINATION share/ppcp)
endif()
