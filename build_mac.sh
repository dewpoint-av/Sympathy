#!/usr/bin/env bash
#
# Sympathy - macOS build script.
#
# Builds universal2 (Apple Silicon + Intel) VST3, AU and CLAP bundles in
# Release and stages them into dist/ for packaging.
#
# Requirements: macOS with Xcode or the Command Line Tools, and CMake
# (brew install cmake).
#
set -euo pipefail
cd "$(dirname "$0")"

CONFIG="Release"
ARCHS="arm64;x86_64"          # universal 2 (Apple Silicon + Intel)
DEPLOY_TARGET="10.15"

if ! command -v cmake >/dev/null 2>&1; then
    echo "ERROR: cmake not found. Install it with: brew install cmake" >&2
    exit 1
fi

echo "[1/3] Configuring CMake (universal2, $CONFIG)..."
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DCMAKE_OSX_ARCHITECTURES="$ARCHS" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOY_TARGET"

echo "[2/3] Building..."
NCPU="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
cmake --build build --config "$CONFIG" --parallel "$NCPU"

echo "[3/3] Staging bundles into dist/..."
ART_ROOT="build/Sympathy_artefacts"
CFG_DIR=""
for d in "$ART_ROOT/$CONFIG" "$ART_ROOT"; do
    if [ -d "$d/VST3" ]; then CFG_DIR="$d"; break; fi
done
if [ -z "$CFG_DIR" ]; then
    echo "ERROR: could not locate build artefacts under $ART_ROOT" >&2
    exit 1
fi

rm -rf dist
mkdir -p dist
cp -R "$CFG_DIR/VST3/Sympathy.vst3"      dist/
cp -R "$CFG_DIR/AU/Sympathy.component"   dist/
cp -R "$CFG_DIR/CLAP/Sympathy.clap"      dist/

echo "Done. Staged bundles:"
ls -1 dist/
echo
echo "Next: ./make_dmg.sh to create the drag-and-drop installer."
