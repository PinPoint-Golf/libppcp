# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Mark Liversedge
#
# purity.cmake — assert that libppcp is still sans-I/O.
#
# Plan ground rule 7 and CORE A.3: the library owns no socket, thread, timer,
# clock or file.  That is what lets one implementation serve both ends of the
# protocol, keeps LGPL transport dependencies out of an MIT library
# (REQ-TRANS-3), and makes fixture replay and the simulator trivial.
#
# The property is easy to state and easy to erode one convenience at a time, so
# it is a build failure rather than a note.  Two passes:
#
#   1. Source pass — every `#include <...>` in src/ and include/ against a
#      whitelist.  Anything outside it fails, so "any non-libc header" is caught
#      by construction rather than by a blocklist that a new platform header
#      walks straight past.
#   2. Symbol pass — the archive's undefined symbols, for the case where a
#      compiler builtin or a macro reaches libc with no header of its own.
#
# ⚠ stdlib.h is deliberately NOT whitelisted.  It is libc, but it is also
# malloc, rand and getenv: a sans-I/O library that allocates has taken over a
# decision the embedding is supposed to make, and ENC 3a/§8 require the frame
# limits to be enforced *before* allocating — which this library does by never
# allocating at all.  math.h is likewise absent: every rounding decision here is
# specified to the nanosecond and is done in integer arithmetic.

set(allowed_headers
    stdint.h stddef.h stdbool.h string.h limits.h stdarg.h)

file(GLOB_RECURSE pure_sources "${PPCP_SRC_DIR}/*.c" "${PPCP_SRC_DIR}/*.h"
                               "${PPCP_INC_DIR}/*.h")

set(violations "")

foreach(f ${pure_sources})
    file(STRINGS "${f}" lines REGEX "^[ \t]*#[ \t]*include[ \t]*<")
    foreach(line ${lines})
        string(REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]*<([^>]+)>.*$" "\\1" hdr "${line}")
        list(FIND allowed_headers "${hdr}" idx)
        if(idx EQUAL -1)
            get_filename_component(base "${f}" NAME)
            list(APPEND violations "${base}: #include <${hdr}>")
        endif()
    endforeach()
endforeach()

if(violations)
    string(REPLACE ";" "\n    " pretty "${violations}")
    message(FATAL_ERROR
        "libppcp must stay sans-I/O (plan ground rule 7, CORE A.3).\n"
        "  Only these headers may be included by src/ or include/: ${allowed_headers}\n"
        "  Found:\n    ${pretty}\n"
        "  A socket, a thread, a clock, a file or a platform header belongs in the\n"
        "  embedding application, which supplies bytes and timestamps to this library.")
endif()

# ---------------------------------------------------------------------------
# Symbol pass.  nm is not present everywhere (and its flags differ between the
# BSD and GNU builds), so this degrades to a status message rather than a
# failure when it cannot run.  The source pass above is the load-bearing one.
# ---------------------------------------------------------------------------
if(NOT DEFINED PPCP_LIB OR NOT EXISTS "${PPCP_LIB}")
    message(STATUS "purity: sources clean; no archive to scan")
    return()
endif()

execute_process(COMMAND nm -u "${PPCP_LIB}"
                OUTPUT_VARIABLE nm_out
                ERROR_VARIABLE  nm_err
                RESULT_VARIABLE nm_rc)

if(NOT nm_rc EQUAL 0)
    message(STATUS "purity: sources clean; nm unavailable, symbol pass skipped")
    return()
endif()

# Each entry is matched as a whole undefined symbol, with or without the leading
# underscore Mach-O adds.
set(forbidden_symbols
    clock_gettime gettimeofday time mach_absolute_time nanosleep usleep sleep
    pthread_create pthread_mutex_lock thrd_create
    socket connect bind listen accept recv recvfrom send sendto poll select
    fopen freopen open open64 creat read write close lseek unlink stat
    printf fprintf sprintf snprintf puts fwrite fread
    malloc calloc realloc free
    rand srand random arc4random getenv setenv exit abort)

set(sym_violations "")
foreach(sym ${forbidden_symbols})
    if(nm_out MATCHES "[ \t]U[ \t]+_?${sym}([ \t\r\n]|$)")
        list(APPEND sym_violations "${sym}")
    endif()
endforeach()

if(sym_violations)
    string(REPLACE ";" ", " pretty "${sym_violations}")
    message(FATAL_ERROR
        "libppcp must stay sans-I/O (plan ground rule 7) but the archive "
        "references: ${pretty}")
endif()

message(STATUS "purity: no clock, thread, socket, file, allocator or RNG reached")
