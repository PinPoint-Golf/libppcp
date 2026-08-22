# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Mark Liversedge
#
# CT-I18 (static half) and CT-I9 — asserted by API surface, not by behaviour.
#
# CONF §3 is explicit about the method for both: "The implementation exposes no
# operation that merges or rewrites a Shot on reconciliation.  Assert by API
# surface, not by behaviour."  A C program cannot make that assertion about
# itself, so the test reads the public headers.
#
# I18 — `TimebaseRelation` is never composed.  A needed relation is measured and
# declared directly.  The failure mode is not a function called `compose`: it is
# a helpful utility that takes two relations and returns a third, whatever it is
# called.  So the scan is for a signature, not for a name.
#
# I9 — reconciliation creates links; no entity is rewritten or merged.
#
# I37 — an Annotation reaches no Shot, Candidate, calibration or computed
# quantity, and `kind: nav_anchor` is never written as phase data.  CONF §3 for
# CT-I37 says the method is the API surface, and the failure mode is not a
# function called `annotation_to_shot`: it is any signature that puts an
# Annotation and a computed entity in the same call.  So the scan is for that
# pairing, not for a name.

file(GLOB headers "${PPCP_INC_DIR}/ppcp/*.h")

set(banned_names
    "ppcp_relation_compose"
    "ppcp_relation_chain"
    "ppcp_relation_derive"
    "ppcp_timebase_compose"
    "ppcp_shot_merge"
    "ppcp_session_merge"
    "ppcp_candidate_merge"
    "ppcp_annotation_apply_to_shot")

set(violations "")

foreach(h ${headers})
    file(READ "${h}" text)
    get_filename_component(base "${h}" NAME)

    # Anchored on PPCP_API and bounded by the `;` that ends each declaration,
    # so a comment SAYING one of these does not exist does not read as one that
    # does.  planned.h says exactly that, deliberately.
    foreach(n ${banned_names})
        if(text MATCHES "PPCP_API[^;]*${n}[ \t]*\\(")
            list(APPEND violations "${base}: declares ${n}")
        endif()
    endforeach()

    # A declaration taking two `const ppcp_timebase_relation *` is composition
    # whatever it is called (CORE 5.4c, I18).
    if(text MATCHES "const[ \t]+ppcp_timebase_relation[ \t]*\\*[^;]*const[ \t]+ppcp_timebase_relation[ \t]*\\*")
        list(APPEND violations "${base}: a function takes two TimebaseRelations — that is composition (I18)")
    endif()

    # I37 — a declaration mentioning an Annotation AND one of the entities
    # 5.18c names is a path from markup into derived data, whatever it is
    # called.  `ppcp_stream` is deliberately not in the list: 5.18g and 5.18j
    # make an annotation NAME a Stream, and a Stream is a declaration rather
    # than a computed quantity.
    foreach(entity ppcp_shot ppcp_candidate ppcp_calibration ppcp_timebase_relation)
        if(text MATCHES "PPCP_API[^;]*ppcp_annotation[^;]*${entity}[ \t_]")
            list(APPEND violations "${base}: an Annotation meets ${entity} in one signature (I37)")
        endif()
        if(text MATCHES "PPCP_API[^;]*${entity}[ \t_][^;]*ppcp_annotation")
            list(APPEND violations "${base}: an Annotation meets ${entity} in one signature (I37)")
        endif()
    endforeach()
endforeach()

# The positive half of I37: markup exists, and it exists as its own header with
# nothing reaching out of it.  A library with no Annotation at all would pass
# every scan above by having dropped the feature.
if(NOT EXISTS "${PPCP_INC_DIR}/ppcp/markup.h")
    message(FATAL_ERROR "CT-I37: ppcp/markup.h is missing; the scan above proves nothing")
endif()

if(violations)
    string(REPLACE ";" "\n    " pretty "${violations}")
    message(FATAL_ERROR
        "API surface assertion failed (CT-I18 / CT-I9):\n    ${pretty}")
endif()

message(STATUS "api surface: no relation composition, no merge or rewrite operation")
