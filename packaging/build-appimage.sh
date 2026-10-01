#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-appimage}"
APP_DIR="${APP_DIR:-$ROOT/AppDir}"
LINUXDEPLOY="${LINUXDEPLOY:-$ROOT/linuxdeploy}"

if [[ ! -x "$LINUXDEPLOY" ]]; then
  echo "Baixe linuxdeploy-x86_64.AppImage e indique o caminho em LINUXDEPLOY." >&2
  exit 1
fi
PLUGIN_DIR="$(dirname -- "$LINUXDEPLOY")"
if [[ ! -x "$PLUGIN_DIR/linuxdeploy-plugin-qt-x86_64.AppImage" ]]; then
  echo "Coloque linuxdeploy-plugin-qt-x86_64.AppImage ao lado de linuxdeploy." >&2
  exit 1
fi
export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
export EXTRA_PLATFORM_PLUGINS="${EXTRA_PLATFORM_PLUGINS:-libqwayland-egl.so;libqwayland-generic.so}"

cmake -S "$ROOT" -B "$BUILD_DIR" -DBUILD_KWIN_EFFECT=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD_DIR" --parallel
rm -rf "$APP_DIR"
DESTDIR="$APP_DIR" cmake --install "$BUILD_DIR"
APPIMAGE_EXTRACT_AND_RUN=1 "$LINUXDEPLOY" --appdir "$APP_DIR" \
  --desktop-file "$APP_DIR/usr/share/applications/ajuste-video.desktop" \
  --icon-file "$APP_DIR/usr/share/icons/hicolor/scalable/apps/ajuste-video.svg" \
  --plugin qt --output appimage
mv "$ROOT/Ajuste_de_vídeo-x86_64.AppImage" "$ROOT/ajuste-video-x86_64.AppImage"
echo "Gerado: $ROOT/ajuste-video-x86_64.AppImage"
