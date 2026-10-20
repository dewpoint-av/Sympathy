#!/usr/bin/env bash
#
# Sympathy - macOS drag-and-drop installer (.dmg).
#
# Packages the bundles staged in dist/ (by build_mac.sh) into a disk image
# with a symlink to /Library/Audio/Plug-Ins for drag-and-drop installation.
#
set -euo pipefail
cd "$(dirname "$0")"

VERSION="1.0.0"
STAGE="build/dmg-root"
OUT="installer/output/Sympathy-${VERSION}-mac.dmg"

for b in dist/Sympathy.vst3 dist/Sympathy.component dist/Sympathy.clap; do
    if [ ! -d "$b" ]; then
        echo "ERROR: $b not found. Run ./build_mac.sh first." >&2
        exit 1
    fi
done

echo "Preparing .dmg staging area..."
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -R dist/Sympathy.vst3      "$STAGE/"
cp -R dist/Sympathy.component "$STAGE/"
cp -R dist/Sympathy.clap      "$STAGE/"

# Drag-and-drop target: the system-wide Audio Plug-Ins folder.
ln -sfn "/Library/Audio/Plug-Ins" "$STAGE/Audio Plug-Ins"

cat > "$STAGE/README.txt" <<'EOF'
Sympathy - Physical Modeling Resonator
--------------------------------------

Drag each bundle into the matching folder inside "Audio Plug-Ins":

    Sympathy.vst3        ->  VST3/
    Sympathy.component   ->  Components/
    Sympathy.clap        ->  CLAP/

Then rescan your plug-ins in the DAW.

Per-user install (no admin prompt): drop the same bundles into the matching
subfolders of ~/Library/Audio/Plug-Ins. In Finder press Cmd+Shift+G and type
~/Library/Audio/Plug-Ins (the ~/Library folder is hidden by default).
EOF

mkdir -p installer/output
rm -f "$OUT"

echo "Creating disk image..."
hdiutil create -volname "Sympathy" -srcfolder "$STAGE" -ov -format UDZO "$OUT"

echo
echo "Created: $OUT"
