# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Mark Liversedge
#
# audit-adjacent-must — the CONF 5b2 checklist generator.  L16.
add_executable(audit-adjacent-must main.c)
target_link_libraries(audit-adjacent-must PRIVATE ppcp_build_flags)
