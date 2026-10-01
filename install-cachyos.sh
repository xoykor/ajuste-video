#!/usr/bin/env bash
set -euo pipefail

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${TMPDIR:-/tmp}/ajuste-video-build-${UID}"
install_prefix="${HOME}/.local"

cmake -S "$here" -B "$build_dir" -DBUILD_KWIN_EFFECT=OFF \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$install_prefix"
cmake --build "$build_dir" --parallel "$(nproc)"
cmake --install "$build_dir"

printf '\nAplicativo instalado em %s. Abra Ajuste de vídeo e escolha “Ativar no KWin”.\n' "$install_prefix"
