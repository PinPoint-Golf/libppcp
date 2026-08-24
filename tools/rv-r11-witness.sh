#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Mark Liversedge
#
# rv-r11-witness.sh — RT-24c's curve half.  PPCP-RV §10.4, the R-11 witness.
#
# ⛔ WHAT IT WITNESSES, AND WHY IT IS THE ARGUMENT UNDER TRAP 5.  X25519 IS NOT
# CONTRIBUTORY: a DIFFERENT public key can yield a BIT-IDENTICAL, NON-ZERO
# shared secret.  §10.4 publishes the pair — `pk_a` and `pk_a'` — and both give
# exactly
#
#     Z = 7c79d7b5f31b9aac367477f5f7c7a68b5c44cac28ed5c902a59ec48c02956a6a
#
# so `BK`, `sid` and `PRK` are identical under the substitution and ONLY
# `sas_raw` separates the two peers: 435948 against 485158.
#
# That is the whole reason 11.6c2 forbids dropping `pk_i ‖ pk_a` from the
# `sas_raw` info on the grounds that `Z` already depends on both keys.  It
# reads as a redundancy and it is not one: that binding is the ONLY thing
# separating the two peers under substitution, and removing it is undetectable
# from outside.  ⛔ A test that never runs a curve can assert the derivation
# half of this — libppcp's own suite does, from the published `Z` — but it
# cannot demonstrate the premise the clause rests on, which is that a real
# X25519 really does produce that collision.  This does.
#
# WHY IT CALLS `openssl` DIRECTLY RATHER THAN GOING THROUGH ppcp-relay's
# agreement helper.  The helper generates its own keypair and refuses to be
# told one (11.5a: a FRESH draw per attempt, and an interface that accepted a
# caller's scalar would be an interface for reusing one).  A witness over a
# PUBLISHED vector needs a chosen scalar, so it uses the curve directly.  That
# is also why the two published private keys appear below and NOT anywhere
# under tools/ppcp-relay/ — CA8, and §10.4's own warning that a peer shipping
# either "would be trivially impersonable by anyone reading this document".
# These are a specification's worked example, not a credential.
#
# Usage:  tools/rv-r11-witness.sh [OPENSSL]

set -eu

OPENSSL=${1:-openssl}
command -v "$OPENSSL" >/dev/null 2>&1 || { echo "no openssl at $OPENSSL" >&2; exit 2; }
command -v perl       >/dev/null 2>&1 || { echo "no perl in PATH" >&2; exit 2; }

TMPD=$(mktemp -d 2>/dev/null || mktemp -d -t r11) || exit 2
trap 'rm -rf "$TMPD"' EXIT

SPKI=302a300506032b656e032100
PKCS8=302e020100300506032b656e04220420

# §10.4's published vector, revision 9 as amended by E30–E55.
SK_I=202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f
PK_I=358072d6365880d1aeea329adf9121383851ed21a28e3b75e965d0d2cd166254
PK_A=675dd574ed7789310b3d2e7681f3790b466c773b1521fecf36577958371ea52f
PK_A2=87abc1e84c4c5572d2b1e63c69f5617a215518cf6261eb5a0e7db49ddad34208
Z_WANT=7c79d7b5f31b9aac367477f5f7c7a68b5c44cac28ed5c902a59ec48c02956a6a
ZERO=0000000000000000000000000000000000000000000000000000000000000000

hex2bin() { perl -e 'print pack("H*", $ARGV[0])' "$1"; }
bin2hex() { perl -e 'local $/; print unpack("H*", <STDIN>)'; }

hex2bin "${PKCS8}${SK_I}" | "$OPENSSL" pkey -inform DER -out "$TMPD/ski.pem" 2>/dev/null
hex2bin "${SPKI}${PK_A}"  | "$OPENSSL" pkey -pubin -inform DER -out "$TMPD/pka.pem"  2>/dev/null
hex2bin "${SPKI}${PK_A2}" | "$OPENSSL" pkey -pubin -inform DER -out "$TMPD/pka2.pem" 2>/dev/null

fail=0
say() { printf '  %s\n' "$1"; }
expect() { # name got want
    if [ "$2" = "$3" ]; then
        say "ok   $1"
    else
        say "FAIL $1"
        say "       got  $2"
        say "       want $3"
        fail=1
    fi
}

echo "RT-24c — the R-11 witness (RV §10.4, 11.6c2, trap 5)"

# 0. The vector reproduces at all: the published pk_i really is this scalar's.
PK_I_GOT=$("$OPENSSL" pkey -in "$TMPD/ski.pem" -pubout -outform DER \
           | bin2hex | sed "s/^$SPKI//")
expect "pk_i = X25519(sk_i, 9)" "$PK_I_GOT" "$PK_I"

# 1. The honest leg.
Z1=$("$OPENSSL" pkeyutl -derive -inkey "$TMPD/ski.pem" -peerkey "$TMPD/pka.pem" | bin2hex)
expect "Z = X25519(sk_i, pk_a)" "$Z1" "$Z_WANT"

# 2. ⛔ THE WITNESS.  A DIFFERENT public key, and the SAME shared secret.
Z2=$("$OPENSSL" pkeyutl -derive -inkey "$TMPD/ski.pem" -peerkey "$TMPD/pka2.pem" | bin2hex)
expect "Z = X25519(sk_i, pk_a') — pk_a' != pk_a, Z IDENTICAL" "$Z2" "$Z_WANT"

# 3. And non-zero, so 11.6b's invalid_key does NOT fire and nothing upstream
#    of the derivation notices the substitution.  This is what makes the
#    binding in 11.6c2 load-bearing rather than decorative.
if [ "$Z2" = "$ZERO" ]; then
    say "FAIL Z' is all-zero — the witness would be caught by 11.6b and is not a witness"
    fail=1
else
    say "ok   Z' is non-zero — 11.6b does not fire, so only sas_raw separates the peers"
fi

if [ "$PK_A" = "$PK_A2" ]; then
    say "FAIL pk_a and pk_a' are the same key; the vector is wrong"
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "RT-24c: FAILED"
    exit 1
fi
echo "RT-24c: the derivation half is in test_rv_bootstrap (435948 vs 485158);"
echo "        this is the curve half, and both are now commands."
exit 0
