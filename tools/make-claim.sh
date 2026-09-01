#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Mark Liversedge
#
# make-claim.sh — generates docs/conformance/claim-libppcp.md.  Work package L15.
#
# Plan ground rule 4: "a package is not done until the row it names is passing
# in the matrix, with a reproducible command", and §8: "nobody marks a cell
# `pass` by hand: it comes from a command that can be re-run".  A claim file
# maintained by hand is a claim file that drifts from the suite the moment
# anybody is busy.  So the evidence in it is generated and the prose around it
# is authored, and the file says which is which.
#
# Usage:  tools/make-claim.sh [BUILD_DIR]           (default build/dev)
#
# Exits non-zero if the suite does not pass: a claim generated from a red run
# would be a claim, and the point of this file is that it is evidence.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${1:-$ROOT/build/dev}
OUT=$ROOT/docs/conformance/claim-libppcp.md
TMP=$(mktemp -d 2>/dev/null || mktemp -d -t ppcpclaim)
trap 'rm -rf "$TMP"' EXIT

CONFORM=$BUILD/tools/ppcp-conform/ppcp-conform
ALL=core,capture,detect,mint,arbitrate,live,offline,markup,actuate

[ -x "$CONFORM" ] || { echo "make-claim.sh: no ppcp-conform at $CONFORM — build first" >&2; exit 2; }

echo "make-claim.sh: running the suite…" >&2
( cd "$BUILD" && ctest ) > "$TMP/ctest.txt" 2>&1 || {
    echo "make-claim.sh: the suite did not pass; the claim is not generated" >&2
    tail -20 "$TMP/ctest.txt" >&2
    exit 1
}

echo "make-claim.sh: running ppcp-conform, both roles…" >&2
"$CONFORM" --self --role host    --profiles "$ALL" --column libppcp --quiet \
           --json "$TMP/host.json"    --markdown "$TMP/host.md"
"$CONFORM" --self --role capture --profiles "$ALL" --column libppcp --quiet \
           --json "$TMP/capture.json" --markdown "$TMP/capture.md"

TOTAL=$(grep -c '^ *[0-9]*/[0-9]* Test' "$TMP/ctest.txt" || true)
DATE=$(date -u +%Y-%m-%d)
COMMIT=$(cd "$ROOT" && git rev-parse --short HEAD 2>/dev/null || echo unknown)

{
    cat "$ROOT/tools/claim/head.md"

    cat <<HDR

---

<!-- ===== everything from here to the closing marker is GENERATED ===== -->

## The evidence

Generated $DATE from commit \`$COMMIT\` by \`tools/make-claim.sh\`.

### The suite — $TOTAL rows, all passing

Every row below is a command: \`ctest --preset dev -R <name>\`.

| Row | Result |
|---|---|
HDR
    grep -E '^ *[0-9]+/[0-9]+ +Test +#[0-9]+' "$TMP/ctest.txt" |
        sed -E 's/^ *[0-9]+\/[0-9]+ +Test +#[0-9]+: +([^ ]+) .*(Passed|Failed).*/| `\1` | \2 |/'

    cat <<'HDR2'

### The paired and injected rows — `ppcp-conform`

`--self` stands a second `ppcp-sim` up as the peer under test over loopback, so
each row below is two processes, two TCP connections and a wire — not two
engines through a byte buffer, which `CONF` §2c says passes I19, I22, I24 and
I31 by accident. Every verdict carries the exact `ppcp-sim` command that
produced it; those are in the JSON the same run emits.

**The peer under test is a host:**

HDR2
    sed '1,2d' "$TMP/host.md"
    cat <<'HDR3'

**The peer under test is a capture peer:**

HDR3
    sed '1,2d' "$TMP/capture.md"

    cat <<'HDR4'

### The checked-in fixtures — `CONF` §2b

Written by `tools/ppcp-fixtures`, under version control, read back from disk by
`tests/test_fixtures.c`, and asserted byte-identical on every run by
`L15-fixtures-stable`. `ENC` 4e makes deterministic encoding a fair question to
ask of a wire format, and this is where it is asked.

| Fixture | Bytes | Rows |
|---|---|---|
HDR4
    for f in "$ROOT"/tests/fixtures/*.ppcpb; do
        n=$(basename "$f")
        b=$(wc -c < "$f" | tr -d ' ')
        case "$n" in
            ct-i12-video*) rows="CT-I12, CT-I34" ;;
            ct-i12-*)  rows="CT-I12" ;;
            ct-i2-*)   rows="CT-I2, CT-I11" ;;
            ct-i13-*)  rows="CT-I13" ;;
            ct-i15-*)  rows="CT-I15" ;;
            ct-i36-*)  rows="CT-I36 (c)(d)" ;;
            *)         rows="—" ;;
        esac
        printf '| `%s` | %s | %s |\n' "$n" "$b" "$rows"
    done

    cat <<'HDR5'

### The freeze gates — `CONF` §5b1, §5b2

| Gate | Command | Result |
|---|---|---|
HDR5
    if "$BUILD/tools/audit-profile-boundary/audit-profile-boundary" \
        "$ROOT/docs/specification" "$ROOT/src" > "$TMP/pb.txt" 2>&1; then
        pb=pass
    else
        pb=FAIL
    fi
    printf '| Profile boundary (5b1) | `ctest --preset dev -R L16-profile-boundary` | %s |\n' "$pb"
    printf '| Adjacent-MUST sweep (5b2) | `ctest --preset dev -R L16-adjacent-must` | generator runs; the sweep itself is L17 |\n'
    printf '\n%s\n' "$(grep -E '^  [0-9]+ messages' "$TMP/pb.txt" || true)"

    echo
    echo "<!-- ===== end of the generated evidence ===== -->"
    echo
    echo "---"
    echo
    cat "$ROOT/tools/claim/tail.md"
} > "$OUT"

echo "make-claim.sh: wrote $OUT" >&2
