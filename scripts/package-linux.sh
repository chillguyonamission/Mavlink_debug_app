#!/usr/bin/env bash
# Build an AppImage from an existing build: scripts/package-linux.sh [build-dir] [output-dir]
# Bundles Qt (and WebEngine if the build has it) and the system's GStreamer plugins. Runs on the build's
# Ubuntu release or newer (glibc). Running an AppImage needs libfuse2 (24.04: sudo apt install libfuse2t64).
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD=${1:-build}
OUT=${2:-dist}
TOOLS=${TOOLS:-$BUILD/appimage-tools}
LINUXDEPLOY_TAG=1-alpha-20251107-1
QT_PLUGIN_TAG=1-alpha-20250213-1
GST_PLUGIN_COMMIT=432af639725bc8b99df12351e86bb2e8a6b3585f

mkdir -p "$TOOLS" "$OUT"
fetch() { [ -s "$TOOLS/$2" ] || curl -fsSL --retry 3 -o "$TOOLS/$2" "$1"; chmod +x "$TOOLS/$2"; }
fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_TAG/linuxdeploy-x86_64.AppImage" linuxdeploy-x86_64.AppImage
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/$QT_PLUGIN_TAG/linuxdeploy-plugin-qt-x86_64.AppImage" linuxdeploy-plugin-qt-x86_64.AppImage
fetch "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gstreamer/$GST_PLUGIN_COMMIT/linuxdeploy-plugin-gstreamer.sh" linuxdeploy-plugin-gstreamer.sh

APPDIR=$BUILD/AppDir
rm -rf "$APPDIR"
cmake --install "$BUILD" --prefix "$APPDIR/usr"  # binaries + message_definitions next to them

export APPIMAGE_EXTRACT_AND_RUN=1           # the tools run without FUSE (CI, containers)
export QMAKE=${QMAKE:-$(command -v qmake6 || echo /usr/lib/qt6/bin/qmake)}
export GSTREAMER_INCLUDE_BAD_PLUGINS=1      # h264parse, tsdemux
export LDAI_OUTPUT="$OUT/MavlinkDebugApp-$(git describe --always --dirty --exclude '*')-x86_64.AppImage"
export PATH="$TOOLS:$PATH"
linuxdeploy-x86_64.AppImage --appdir "$APPDIR" \
    -e "$APPDIR/usr/bin/mavlink_capture" \
    -d packaging/mavlink-debug-app.desktop -i packaging/mavlink-debug-app.svg \
    --plugin qt --plugin gstreamer --output appimage
echo "Built $LDAI_OUTPUT"
