# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Mark Liversedge
#
# header_self_contained.cmake — every public header compiles ON ITS OWN.
#
# Plan A3 makes include/ppcp/*.h the port surface, and a consumer includes what
# it needs and nothing more.  A header that leans on a declaration some OTHER
# header happened to provide first still compiles inside this repository — the
# .c file that uses it includes both — and fails in the consumer.
#
# ⚠ THIS IS NOT HYPOTHETICAL AND IT IS WHY THE GATE EXISTS.  In session C1,
# ppcp/bootstrap.h defined PPCP_BS_MAX_FRAME in terms of
# PPCP_FRAME_HEADER_BYTES while including only ppcp/rv.h.  Every target in this
# repository built and every test passed, because src/ppcp_bs_frame.c includes
# ppcp/frame.h two lines after bootstrap.h.  PinPointCapture consumes these
# headers as a CLANG MODULE — which is how every Swift consumer builds them —
# and a module gets no such help: it broke that repository's build outright,
# and the defect was found there rather than here.  One `#include` fixed it.
#
# So the assertion is made the only way it can be: compile each header alone,
# as its own translation unit, with nothing else included first.
#
# Cheap — a syntax-only pass over a dozen small headers — and it fails the
# build rather than producing a report nobody reads.

file(GLOB headers "${PPCP_INC_DIR}/ppcp/*.h")
list(SORT headers)

set(failures "")

foreach(h ${headers})
    get_filename_component(base "${h}" NAME)

    # A translation unit whose ENTIRE content is this one header.
    set(tu "${PPCP_BIN_DIR}/self_contained_${base}.c")
    file(WRITE "${tu}" "#include \"ppcp/${base}\"\n")

    execute_process(
        COMMAND ${PPCP_C_COMPILER} -fsyntax-only -I "${PPCP_INC_DIR}" "${tu}"
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE  err)

    if(NOT rc EQUAL 0)
        list(APPEND failures "${base}:\n${err}")
    endif()
endforeach()

if(failures)
    string(REPLACE ";" "\n" pretty "${failures}")
    message(FATAL_ERROR
        "A public header does not compile on its own (plan A3).\n"
        "${pretty}\n"
        "  Add the #include the header itself needs.  It builds here only\n"
        "  because some .c file includes another header first; a consumer\n"
        "  importing this as a Clang module gets no such help.")
endif()

list(LENGTH headers n)
message(STATUS "headers: all ${n} public headers compile standalone")
