#!/usr/bin/env bash
#
# Sympathy - macOS installer package (.pkg).
# Installs to /Library/Audio/Plug-Ins/{VST3,Components,CLAP}.
#
set -euo pipefail
cd "$(dirname "$0")"

VERSION="1.1.0"
PAYLOAD="build/pkg-payload"
OUT="installer/output/Sympathy-${VERSION}-mac.pkg"

for b in dist/Sympathy.vst3 dist/Sympathy.component dist/Sympathy.clap; do
    if [ ! -d "$b" ]; then
        echo "ERROR: $b not found. Run ./build_mac.sh first." >&2
        exit 1
    fi
done

echo "Preparing package payload..."
rm -rf "$PAYLOAD"
mkdir -p "$PAYLOAD/Library/Audio/Plug-Ins/VST3"
mkdir -p "$PAYLOAD/Library/Audio/Plug-Ins/Components"
mkdir -p "$PAYLOAD/Library/Audio/Plug-Ins/CLAP"
cp -R dist/Sympathy.vst3      "$PAYLOAD/Library/Audio/Plug-Ins/VST3/"
cp -R dist/Sympathy.component "$PAYLOAD/Library/Audio/Plug-Ins/Components/"
cp -R dist/Sympathy.clap      "$PAYLOAD/Library/Audio/Plug-Ins/CLAP/"

mkdir -p installer/output
rm -f "$OUT"

echo "Building installer package..."
mkdir -p build/scripts
cat > build/scripts/preinstall <<'EOF'
#!/bin/sh
# Remove any previous installation before overwriting.
rm -rf "/Library/Audio/Plug-Ins/VST3/Sympathy.vst3"
rm -rf "/Library/Audio/Plug-Ins/Components/Sympathy.component"
rm -rf "/Library/Audio/Plug-Ins/CLAP/Sympathy.clap"
exit 0
EOF
chmod +x build/scripts/preinstall

pkgbuild --root "$PAYLOAD" \
    --scripts build/scripts \
    --identifier "com.dewpoint.Sympathy" \
    --version "$VERSION" \
    --install-location "/" \
    "$OUT"

echo
echo "Created: $OUT"
