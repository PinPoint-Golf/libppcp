#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Mark Liversedge
#
# run-pair.sh — two `ppcp-sim` peers over real loopback sockets.
#
# This is what turns a *paired* conformance row into evidence.  A `pass` that
# came from two engines through a byte buffer is a real end-to-end run and is
# NOT an interoperability demonstration (CONF §2c); a `pass` that came from
# here crossed two processes, two TCP connections, a link binding and a wire.
#
# Usage:
#   run-pair.sh SIM LISTEN_DECL LISTEN_SCENARIO LISTEN_EXPECT \
#                   DIAL_DECL   DIAL_SCENARIO   DIAL_EXPECT   [RUN_MS]
#
# EXPECT is a comma-separated list for --expect, or `-` for none.  The listener
# binds port 0 and writes the port it got, so any number of these run in
# parallel without a port to collide over.  Exits non-zero if either peer does.
set -u

if [ $# -lt 7 ]; then
    echo "run-pair.sh: wants seven arguments; see the comment at the top" >&2
    exit 2
fi

SIM=$1
LDECL=$2
LSC=$3
LEXP=$4
DDECL=$5
DSC=$6
DEXP=$7
RUNMS=${8:-4000}

TMP=$(mktemp -d 2>/dev/null || mktemp -d -t ppcpsim)
PORTFILE="$TMP/port"
trap 'rm -rf "$TMP"' EXIT

set -- --listen 0 --port-file "$PORTFILE" --declaration "$LDECL" --scenario "$LSC" \
       --run-ms "$RUNMS" --log-prefix listener
[ "$LEXP" = "-" ] || set -- "$@" --expect "$LEXP"
"$SIM" "$@" &
LPID=$!

i=0
while [ ! -s "$PORTFILE" ]; do
    i=$((i + 1))
    if [ $i -gt 500 ]; then
        echo "run-pair.sh: the listener never bound a port" >&2
        kill $LPID 2>/dev/null
        exit 1
    fi
    sleep 0.01
done
PORT=$(cat "$PORTFILE")

set -- --connect "127.0.0.1:$PORT" --declaration "$DDECL" --scenario "$DSC" \
       --run-ms "$RUNMS" --log-prefix dialler
[ "$DEXP" = "-" ] || set -- "$@" --expect "$DEXP"
"$SIM" "$@"
DRC=$?

wait $LPID
LRC=$?

if [ $LRC -ne 0 ] || [ $DRC -ne 0 ]; then
    echo "run-pair.sh: listener exited $LRC, dialler exited $DRC" >&2
    exit 1
fi
exit 0
