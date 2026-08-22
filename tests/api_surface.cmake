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
# I37 — an Annotation reaches no computed quantity.

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
endforeach()

if(violations)
    string(REPLACE ";" "\n    " pretty "${violations}")
    message(FATAL_ERROR
        "API surface assertion failed (CT-I18 / CT-I9):\n    ${pretty}")
endif()

message(STATUS "api surface: no relation composition, no merge or rewrite operation")
