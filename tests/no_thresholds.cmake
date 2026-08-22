# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Mark Liversedge
#
# CT-I14 — "Grep the implementation's protocol layer for a frame-rate,
# resolution, quality or confidence constant.  Assert every such threshold
# lives in a policy layer above it."
#
# CONF §3 names the method and the method is a grep, so this is a grep.  The
# protocol layer is `src/` and `include/ppcp/`; the policy layer is the
# embedding, and PinPointStudio's 120 fps floor lives in PinPointStudio (plan
# H2).  What the library carries instead is `ppcp_ingest_policy_fn` and
# `ppcp_promotion_policy` — function pointers, which is what a threshold looks
# like when the protocol refuses to hold it.
#
# What is deliberately NOT a violation:
#
#   - the STRUCTURAL limits of ENC §8 (frame sizes, string lengths, nesting
#     depth, element counts).  They are encoding limits, not quality
#     judgements, and 8a/8b give them their own failure semantics;
#   - the DEFAULTS the specification itself states — `coincidence_window_ns`
#     50 ms, `issue_hold_ns` 200 ms, `heartbeat_interval_ms` 1000 (CORE 5.10,
#     7.4a) — which are declared per Session on the wire, not applied by the
#     library to accept or reject anything;
#   - array capacities (`PPCP_MAX_*`), which bound storage in a library that
#     does not allocate.
#
# The scan therefore looks for the shape of a JUDGEMENT: a named constant whose
# name says fps, rate, resolution, width/height, quality, or confidence and
# whose spelling says min/max/floor/threshold/limit.

file(GLOB_RECURSE sources "${PPCP_SRC_DIR}/*.c" "${PPCP_SRC_DIR}/*.h")
file(GLOB headers "${PPCP_INC_DIR}/ppcp/*.h")

set(subject
    "FPS|FRAME_RATE|FRAMERATE|RESOLUTION|MEGAPIXEL|BITRATE|QUALITY|CONFIDENCE|NOISE")
set(judgement "MIN|MAX|FLOOR|CEIL|THRESHOLD|LIMIT|REQUIRED|ACCEPT|REJECT")

set(violations "")

foreach(f ${sources} ${headers})
    file(READ "${f}" text)
    get_filename_component(base "${f}" NAME)
    # `#define NAME value` where NAME names a quality subject and a judgement.
    if(text MATCHES "#define[ \t]+[A-Za-z_]*(${subject})[A-Za-z_]*(${judgement})[A-Za-z_]*[ \t]+[0-9]")
        list(APPEND violations "${base}: a #define names a quality threshold")
    endif()
    if(text MATCHES "#define[ \t]+[A-Za-z_]*(${judgement})[A-Za-z_]*(${subject})[A-Za-z_]*[ \t]+[0-9]")
        list(APPEND violations "${base}: a #define names a quality threshold")
    endif()
    # A comparison of a declared rate or confidence against a literal is the
    # same judgement written as code rather than as a constant.
    #
    # 0, 1, 0.0 and 1.0 are excluded because they are the DOMAIN of the field,
    # not an opinion about it: a rate is positive and a confidence lies in
    # [0, 1] by CORE §5, and refusing a negative rate is validation.  Any other
    # literal — 60, 120, 0.7 — is a judgement, and that is what I14 forbids
    # here and puts in the embedding instead.
    if(text MATCHES "(nominal_mhz|realised_rate_mhz|sustained_rate_mhz|confidence)[ \t]*(<|>|<=|>=)[ \t]*(0\\.[0-9]*[1-9]|[0-9]*[2-9][0-9]*|1[0-9]+)")
        list(APPEND violations "${base}: compares a declared rate or confidence with a literal")
    endif()
endforeach()

if(violations)
    list(REMOVE_DUPLICATES violations)
    string(REPLACE ";" "\n    " pretty "${violations}")
    message(FATAL_ERROR
        "CT-I14 failed — a threshold has moved into the protocol layer:\n    ${pretty}")
endif()

# The positive half: the decision exists, and it exists as a callback.
set(found_ingest FALSE)
foreach(h ${headers})
    file(READ "${h}" text)
    if(text MATCHES "ppcp_ingest_policy_fn")
        set(found_ingest TRUE)
    endif()
endforeach()
if(NOT found_ingest)
    message(FATAL_ERROR
        "CT-I14 failed — no ingest policy callback on the public surface; a "
        "library with neither a threshold nor a callback has simply dropped "
        "the decision")
endif()

message(STATUS "CT-I14: no rate, resolution, quality or confidence threshold in src/ or include/")
