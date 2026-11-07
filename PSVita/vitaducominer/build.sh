#!/bin/sh
# Build script for VitaDucoMiner (PS Vita) using CMake only.
# Usage: ./build.sh           # configure + build
#        ./build.sh clean     # remove build directory (keeps sce_sys)

export VITASDK=/usr/local/vitasdk
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

if [ "$1" = "clean" ]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    echo "Done. sce_sys is untouched."
    exit 0
fi

if [ -z "$VITASDK" ]; then
    echo "Error: VITASDK environment variable is not set."
    echo "Export it first, e.g. export VITASDK=/usr/local/vitasdk"
    exit 1
fi

echo "Configuring CMake..."
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"

echo "Building..."
cmake --build "$BUILD_DIR"

echo "Build complete. Output: $BUILD_DIR/vitaducominer.vpk"
