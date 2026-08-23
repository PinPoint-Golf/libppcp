# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Mark Liversedge
#
# ppcp-fixtures — writes the checked-in bundle fixtures of CONF §2b.  L15.
#
# It holds a file, which is why it is a tool and not a test: tests/purity.cmake
# gates src/ and include/, and the library opens nothing.

add_executable(ppcp-fixtures main.c)
target_link_libraries(ppcp-fixtures PRIVATE ppcp ppcp_build_flags)
