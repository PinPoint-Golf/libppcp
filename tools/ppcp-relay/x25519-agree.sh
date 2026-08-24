#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Mark Liversedge
#
# x25519-agree.sh — the supplier side of PPCP-RV §11.11's boundary, for
# `ppcp-relay`.  Work package L21.
#
# ⛔ WHY THIS IS A SEPARATE PROCESS AND NOT A FUNCTION IN THE RELAY.  Ground
# rule 13 and CA1: X25519 never enters `libppcp`.  §11.11 says why in the
# document's own words — SHA-256, HMAC and HKDF are short enough that an
# implementation reads them and a reviewer checks them, and "X25519 is
# constant-time field arithmetic over 2^255-19 and is not"; A16 adds that
# `libppcp` is the implementation that "should neither hand-roll nor vendor"
# it.  The relay needs real key agreement on both of its legs — RT-20b(i),
# (iii) and (v) are unreachable without it — so the primitive is SUPPLIED,
# exactly as §11.11 says it is supplied to the library, and the boundary is a
# process boundary instead of a function call.
#
# 11.11a/11.11d — EXACTLY TWO VALUES CROSS, and each is 32 octets: a public
# key `pk`, and a shared secret `Z`.  `BK`, `sas_raw`, the digits, `K_c`,
# either MAC, `sid` and `PRK` do not, and could not: this script has never
# heard of them.  The boundary is below the derivation, not inside it.
#
# 11.11h — THE PRIVATE SCALAR NEVER LEAVES THIS PROCESS.  It is generated
# here, held in a shell variable, and dies with the process.  It is never
# written to disk, never passed on a command line (which would put it in
# `ps`), and never sent back over the pipe.  The relay therefore has nothing
# to erase, which §11.11h calls "half the point of putting the boundary here".
# The obligation to be constant-time (11.11g) is OpenSSL's and is one nothing
# downstream can check — which is also what 11.11g says.
#
# PROTOCOL, one command per line on stdin, one reply per line on stdout:
#
#   keygen              -> pk <64 hex>     a fresh keypair for ONE attempt
#   agree <64 hex>      -> z <64 hex>      Z = X25519(sk, peer_pk)
#                       -> err <reason>    11.11f: FAILURE IS DISTINGUISHABLE
#   quit                -> (exits 0)
#
# ⛔ 11.11f is the reason `err` exists and is a separate reply rather than an
# empty `z`.  OpenSSL FAILS the call for a small-order peer key (measured:
# exit 1, "Key derivation failed"); CryptoKit throws; something else may yet
# return zeros.  A boundary that dropped that signal, or reported it as a
# transport error, would make 11.6b unimplementable on the far side of it.
# The relay maps `err` and an all-zero `z` to the same thing — abort with
# `invalid_key` — and NEVER to a retry.
#
# 11.5a — `keygen` is called once per attempt and the relay never asks twice
# for one leg, because a reused ephemeral is not ephemeral.

set -eu

die() { printf 'fatal: %s\n' "$1" >&2; exit 2; }

command -v openssl >/dev/null 2>&1 || die "no openssl in PATH"
command -v perl    >/dev/null 2>&1 || die "no perl in PATH"

# The two DER wrappers RFC 8410 puts around a raw X25519 key.  These are
# ASN.1 structure, not key material: 302a3005..032100 is SubjectPublicKeyInfo
# with the id-X25519 OID and a 33-octet BIT STRING, and 302e0201..04220420 is
# the PKCS#8 equivalent.  ⛔ There is no key-shaped constant in this file and
# there is not meant to be — CA8, and 10.4 says plainly that a peer shipping
# a published private scalar "would be trivially impersonable by anyone
# reading this document".  Every key here is drawn fresh by OpenSSL.
SPKI_PREFIX=302a300506032b656e032100

hex2bin() { perl -e 'print pack("H*", $ARGV[0])' "$1"; }
bin2hex() { perl -e 'local $/; my $d = <STDIN>; print unpack("H*", $d)'; }

TMPD=
cleanup() { KEY=; [ -n "$TMPD" ] && rm -rf "$TMPD"; }
trap cleanup EXIT HUP INT TERM

TMPD=$(mktemp -d "${TMPDIR:-/tmp}/ppcp-agree.XXXXXX") || die "mktemp failed"
chmod 700 "$TMPD"

KEY=

# The 32 raw octets of this keypair's public key, from the DER SPKI.
own_pk_hex() {
    printf '%s\n' "$KEY" \
      | openssl pkey -pubout -outform DER 2>/dev/null \
      | bin2hex \
      | sed "s/^$SPKI_PREFIX//"
}

# `peer` is PUBLIC, so a file is fine for it; the private key goes to OpenSSL
# on stdin (/dev/stdin) and never touches the filesystem.
derive() {
    peer_hex=$1
    hex2bin "${SPKI_PREFIX}${peer_hex}" > "$TMPD/peer.der" 2>/dev/null || return 1
    openssl pkey -pubin -inform DER -in "$TMPD/peer.der" -out "$TMPD/peer.pem" 2>/dev/null \
        || return 1
    printf '%s\n' "$KEY" \
      | openssl pkeyutl -derive -inkey /dev/stdin -peerkey "$TMPD/peer.pem" 2>/dev/null \
      | bin2hex
}

while IFS= read -r line; do
    cmd=${line%% *}
    arg=${line#* }
    case "$cmd" in
    keygen)
        [ -z "$KEY" ] || { printf 'err keygen-twice\n'; continue; }
        KEY=$(openssl genpkey -algorithm X25519 2>/dev/null) \
            || { printf 'err genpkey-failed\n'; continue; }
        pk=$(own_pk_hex)
        case "$pk" in
        ????????????????????????????????????????????????????????????????)
            printf 'pk %s\n' "$pk" ;;
        *)  printf 'err pk-encoding\n' ;;
        esac
        ;;
    agree)
        [ -n "$KEY" ] || { printf 'err no-key\n'; continue; }
        case "$arg" in
        ????????????????????????????????????????????????????????????????) ;;
        *) printf 'err bad-peer-key\n'; continue ;;
        esac
        # ⛔ 11.11f.  A non-zero exit here is an ATTACK SIGNAL, not a
        # transport hiccup, and it is reported as `err` so the relay can tell
        # the two apart.  It is never retried.
        if z=$(derive "$arg") && [ -n "$z" ]; then
            printf 'z %s\n' "$z"
        else
            printf 'err derive-failed\n'
        fi
        ;;
    quit) exit 0 ;;
    '')   ;;
    *)    printf 'err unknown-command\n' ;;
    esac
done
exit 0
