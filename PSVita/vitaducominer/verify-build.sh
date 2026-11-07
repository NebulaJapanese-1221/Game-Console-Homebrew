#!/bin/sh
# Build verification script for VitaDucoMiner
# Runs a clean build and verifies output artifacts exist.
# Usage: ./verify-build.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
VPK="$BUILD_DIR/vitaducominer.vpk"
SELF="$BUILD_DIR/vitaducominer.self"

if [ -z "$VITASDK" ]; then
    echo "Error: VITASDK environment variable is not set."
    echo "Export it first, e.g. export VITASDK=/usr/local/vitasdk"
    exit 1
fi

echo "=== VitaDucoMiner Build Verification ==="
echo ""

echo "[1/3] Cleaning previous build..."
rm -rf "$BUILD_DIR"

echo "[2/3] Running clean build..."
"$SCRIPT_DIR/build.sh" > /tmp/vitaducominer_build.log 2>&1
if [ $? -ne 0 ]; then
    echo "FAIL: Build failed. See /tmp/vitaducominer_build.log"
    tail -30 /tmp/vitaducominer_build.log
    exit 1
fi
echo "    Build succeeded."

echo "[3/3] Verifying output artifacts..."
if [ ! -f "$VPK" ]; then
    echo "FAIL: $VPK not found"
    exit 1
fi
echo "    Found: $VPK ($(stat -c %s "$VPK") bytes)"

if [ ! -f "$SELF" ]; then
    echo "FAIL: $SELF not found"
    exit 1
fi
echo "    Found: $SELF ($(stat -c %s "$SELF") bytes)"

echo ""
echo "=== All checks passed ==="
