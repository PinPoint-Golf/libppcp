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

add_subdirectory(ppcp-sim)
add_subdirectory(ppcp-conform)
