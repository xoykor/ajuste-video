#!/usr/bin/env bash
set -euo pipefail

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

if [[ "${XDG_SESSION_TYPE:-}" != "wayland" ]]; then
    printf 'Aviso: a sessão atual não parece ser Wayland (XDG_SESSION_TYPE=%s).\n' "${XDG_SESSION_TYPE:-desconhecido}"
fi

sudo pacman -S --needed base-devel cmake extra-cmake-modules kwin qt6-base kconfig vulkan-headers
build_dir="${TMPDIR:-/tmp}/ajuste-video-build-${UID}"
rm -rf "$build_dir"
cmake -S "$here" -B "$build_dir" -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$build_dir" --parallel "$(nproc)"
sudo cmake --install "$build_dir"

kwriteconfig6 --file kwinrc --group Plugins --key ajustevideoEnabled true
qdbus6 org.kde.KWin /KWin reconfigure || true

printf '\nInstalação concluída. Encerre a sessão Plasma e entre novamente para carregar o efeito.\n'
