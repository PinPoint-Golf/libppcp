/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * The port surface compiles.
 *
 * `ppcp/ppcp.h` is what PinPointStudio and PinPointCapture bind to (plan A3),
 * and until this test existed nothing in the repository included it — so a
 * header that no longer compiled, or two headers that had grown a duplicate
 * typedef between them, would have been found by the applications rather than
 * here.  It is one translation unit and it is the cheapest possible guard.
 */
#include "ppcp/ppcp.h"

#include "test_util.h"

int main(void)
{
    TEST("the umbrella header compiles and the versions are readable");
    CHECK(ppcp_library_version() != NULL);
    CHECK(ppcp_wire_version() != NULL);
    CHECK(strcmp(ppcp_wire_version(), PPCP_WIRE_VERSION) == 0);

    /* Two headers that landed in different work packages, reachable through
     * the one include: L4's vocabulary and L5's catalogue. */
    TEST("L4 and L5 are on the port surface, not only in the archive");
    CHECK_EQ_I(ppcp_msg_count(), PPCP_MSG_COUNT);
    CHECK(ppcp_msg_for(PPCP_MT_HELLO) != NULL);
    CHECK(ppcp_source_kind_is_camera(NULL) == false);

    TEST_MAIN_END();
}
