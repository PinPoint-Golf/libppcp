# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Mark Liversedge
#
# libppcp — the MIT reference implementation of PPCP 1.0.
#
# Plan A1: C11, sans-I/O, CMake, no dependencies, laid out like libwrist.

cmake_minimum_required(VERSION 3.16)

project(libppcp
    VERSION 0.1.0
    DESCRIPTION "Sans-I/O C11 reference implementation of the PinPoint Capture Protocol"
    LANGUAGES C)

# ---------------------------------------------------------------------------
# Top-level or embedded?
#
# PinPointStudio consumes this by FetchContent (plan A4) and PinPointCapture by
# SwiftPM (plan A5).  Adding libppcp to a project must change NOTHING about that
# project: not its build type, not its test registry, not its warning flags.
# Spelled as a CMAKE_SOURCE_DIR comparison rather than PROJECT_IS_TOP_LEVEL
# because that variable is CMake 3.21+ and this project promises 3.16.
# ---------------------------------------------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(PPCP_IS_TOP_LEVEL ON)
else()
    set(PPCP_IS_TOP_LEVEL OFF)
endif()

option(PPCP_BUILD_TESTS     "Build the conformance and unit test suite" ${PPCP_IS_TOP_LEVEL})
option(PPCP_BUILD_TOOLS     "Build the command line tools"              ${PPCP_IS_TOP_LEVEL})
option(PPCP_ENABLE_ASAN     "Build with AddressSanitizer"               OFF)
option(PPCP_ENABLE_UBSAN    "Build with UndefinedBehaviorSanitizer"     OFF)
option(PPCP_ENABLE_COVERAGE "Build with gcov coverage instrumentation"  OFF)
# -Werror is a gate on this project's own build, not a property of the code: a
# consumer's newer compiler warning where ours does not is a break they cannot
# fix and did not cause.
option(PPCP_WERROR          "Treat warnings as errors"                  ${PPCP_IS_TOP_LEVEL})
option(PPCP_INSTALL         "Generate install rules"                    ${PPCP_IS_TOP_LEVEL})

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(PPCP_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Warning and instrumentation flags, behind an INTERFACE target so nothing
# leaks to a consumer.
# ---------------------------------------------------------------------------
add_library(ppcp_build_flags INTERFACE)

if(MSVC)
    target_compile_options(ppcp_build_flags INTERFACE /W4)
    if(PPCP_WERROR)
        target_compile_options(ppcp_build_flags INTERFACE /WX)
    endif()
else()
    target_compile_options(ppcp_build_flags INTERFACE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wconversion -Wsign-conversion
        -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith)
    if(PPCP_WERROR)
        target_compile_options(ppcp_build_flags INTERFACE -Werror)
    endif()
endif()

if(PPCP_ENABLE_ASAN)
    target_compile_options(ppcp_build_flags INTERFACE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(ppcp_build_flags INTERFACE -fsanitize=address)
endif()
if(PPCP_ENABLE_UBSAN)
    target_compile_options(ppcp_build_flags INTERFACE -fsanitize=undefined -fno-sanitize-recover=all)
    target_link_options(ppcp_build_flags INTERFACE -fsanitize=undefined)
endif()
if(PPCP_ENABLE_COVERAGE)
    target_compile_options(ppcp_build_flags INTERFACE --coverage -O0 -g)
    target_link_options(ppcp_build_flags INTERFACE --coverage)
endif()

# ---------------------------------------------------------------------------
# The library.
#
# Ground rule 7: no socket, no thread, no timer, no clock, no file.  Every
# source below is arithmetic over caller-owned buffers.  tests/purity.cmake is
# what keeps that true, and it runs as a POST_BUILD step so an erosion breaks
# the build rather than a report nobody reads.
# ---------------------------------------------------------------------------
set(PPCP_SOURCES
    src/ppcp_common.c
    src/ppcp_cbor_encode.c
    src/ppcp_cbor_decode.c
    src/ppcp_frame.c
    src/ppcp_envelope.c
    src/ppcp_time.c
    src/ppcp_sim_clock.c
    src/ppcp_timing.c
    src/ppcp_sha256.c
    src/ppcp_base64url.c
    src/ppcp_rv_derive.c
    src/ppcp_rv_payload.c
    src/ppcp_codec.c
    src/ppcp_model_capability.c
    src/ppcp_model_source.c
    src/ppcp_model_session.c
    src/ppcp_model_event.c
    src/ppcp_message.c
    src/ppcp_peer.c
    src/ppcp_sync.c
    src/ppcp_transfer.c
    src/ppcp_bundle.c)

set(PPCP_HEADERS
    include/ppcp/version.h
    include/ppcp/common.h
    include/ppcp/cbor.h
    include/ppcp/frame.h
    include/ppcp/envelope.h
    include/ppcp/time.h
    include/ppcp/timing.h
    include/ppcp/hash.h
    include/ppcp/rv.h
    include/ppcp/model.h
    include/ppcp/message.h
    include/ppcp/peer.h
    include/ppcp/transfer.h
    include/ppcp/bundle.h
    include/ppcp/planned.h
    include/ppcp/ppcp.h)

add_library(ppcp STATIC ${PPCP_SOURCES} ${PPCP_HEADERS})
add_library(ppcp::ppcp ALIAS ppcp)

target_include_directories(ppcp
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(ppcp PRIVATE ppcp_build_flags)
set_target_properties(ppcp PROPERTIES
    C_VISIBILITY_PRESET hidden
    VERSION ${PROJECT_VERSION})

# ---------------------------------------------------------------------------
# Purity gate — plan ground rule 7 / CORE A.3.
#
# Two passes.  The source pass reads every #include in src/ and include/ against
# a whitelist of freestanding libc headers; the symbol pass reads the archive's
# undefined symbols.  The source pass catches a header before it is used; the
# symbol pass catches a builtin or a macro that reaches libc without one.
# ---------------------------------------------------------------------------
add_custom_command(TARGET ppcp POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DPPCP_SRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/src
        -DPPCP_INC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/include
        -DPPCP_LIB=$<TARGET_FILE:ppcp>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/purity.cmake
    VERBATIM
    COMMENT "purity: asserting libppcp is still sans-I/O")

if(PPCP_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(PPCP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(PPCP_INSTALL)
    include(GNUInstallDirs)
    install(TARGETS ppcp ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(DIRECTORY include/ppcp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
