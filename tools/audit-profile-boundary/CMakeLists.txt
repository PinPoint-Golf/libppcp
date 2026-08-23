# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Mark Liversedge
#
# audit-profile-boundary — PPCP-CONF 5b1, run as a ctest row.  L16.
add_executable(audit-profile-boundary main.c)
target_link_libraries(audit-profile-boundary PRIVATE ppcp_build_flags)
target_compile_definitions(audit-profile-boundary PRIVATE _POSIX_C_SOURCE=200809L)
