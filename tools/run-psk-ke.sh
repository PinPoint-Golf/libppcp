#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Mark Liversedge
#
# run-psk-ke.sh — validates `ppcp-sim --psk-ke-only` against a TLS 1.3 server
# whose answer is known, so the RT-4 mode is demonstrated to work rather than
# assumed to.
#
#   run-psk-ke.sh SIM OPENSSL refuse|accept
#
# `refuse`  a TLS 1.3 PSK server that requires (EC)DHE.  It rejects a psk_ke-only
#           ClientHello, which is what RV 5.2f requires of a conformant peer, and
#           ppcp-sim must exit 0.
# `accept`  the SAME server with -allow_no_dhe_kex, which accepts PSK-only key
#           exchange.  ppcp-sim must exit 1: that is the failure RT-4 exists to
#           detect, and a mode that cannot detect it is not evidence of anything.
#
# The second case is also what proves the hand-built ClientHello and its PSK
# binder are correct — a wrong binder would have produced an alert, not a
# ServerHello carrying `pre_shared_key`.
set -u

SIM=$1
OPENSSL=$2
MODE=$3

PSK=0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b
IDENTITY=ppcp-sim-psk-ke-only

# A port nobody else in this suite uses; the two modes differ so they can run
# in parallel.
case "$MODE" in
    refuse) PORT=48771; EXTRA= ;;
    accept) PORT=48772; EXTRA=-allow_no_dhe_kex ;;
    *) echo "run-psk-ke.sh: mode is refuse or accept" >&2; exit 2 ;;
esac

# shellcheck disable=SC2086
"$OPENSSL" s_server -accept $PORT -tls1_3 -nocert -psk "$PSK" \
    -psk_identity "$IDENTITY" $EXTRA -quiet >/dev/null 2>&1 &
SRV=$!
trap '{ kill $SRV; wait $SRV; } 2>/dev/null' EXIT

OUT=$(mktemp 2>/dev/null || mktemp -t ppcppsk)
trap '{ kill $SRV; wait $SRV; } 2>/dev/null; rm -f "$OUT"' EXIT

# The server takes a moment to bind.  Retry only while the OFFER never reached
# the wire — once ppcp-sim has said what it offered, its verdict is the answer
# and running it again would just be running it again.
i=0
RC=1
while [ $i -lt 40 ]; do
    i=$((i + 1))
    "$SIM" --psk-ke-only --connect "127.0.0.1:$PORT" --psk "$PSK" \
           --psk-identity "$IDENTITY" >"$OUT" 2>&1
    RC=$?
    cat "$OUT"
    grep -q "offered TLS 1.3" "$OUT" && break
    sleep 0.05
done

if ! grep -q "offered TLS 1.3" "$OUT"; then
    echo "run-psk-ke.sh: the ClientHello never reached the wire" >&2
    exit 1
fi

if [ "$MODE" = refuse ]; then
    [ $RC -eq 0 ] && exit 0
    echo "run-psk-ke.sh: a DHE-requiring server did NOT produce a refusal (exit $RC)" >&2
    exit 1
fi
[ $RC -ne 0 ] && exit 0
echo "run-psk-ke.sh: a server accepting psk_ke was NOT reported as an RT-4 failure" >&2
exit 1
