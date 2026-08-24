# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Mark Liversedge
#
# The tools that demonstrate compliance.  They hold sockets and files, which is
# why they are here and not in src/ — tests/purity.cmake gates src/ and
# include/ and this directory is deliberately outside it.
#
#   ppcp-sim      L13 — the synthetic peer CONF 2c requires.
#   ppcp-conform  L14 — the conformance driver.  It RUNS ppcp-sim, one process
#                 per row, so every verdict carries a command that re-runs.
#   ppcp-relay    L21 — the deliberate man in the middle of RV-6 (RT-20b,
#                 RT-20c).  It holds BOTH legs' key material, which is why it
#                 is here and not in src/ (CA8), and why it obtains its key
#                 agreement from a helper process rather than from a curve in
#                 this repository (ground rule 13, §11.11).

add_subdirectory(ppcp-sim)
add_subdirectory(ppcp-conform)
add_subdirectory(ppcp-relay)
add_subdirectory(ppcp-fixtures)
add_subdirectory(audit-profile-boundary)
add_subdirectory(audit-adjacent-must)
