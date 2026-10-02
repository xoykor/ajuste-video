#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID} -eq 0 ]]; then
    echo "Execute como usuário normal; o desinstalador não precisa de sudo." >&2
    exit 1
fi

config_dir="${XDG_CONFIG_HOME:-${HOME}/.config}"
data_dir="${XDG_DATA_HOME:-${HOME}/.local/share}"
kwinrc="${config_dir}/kwinrc"

if [[ -e "${kwinrc}" ]]; then
    if ! command -v kwriteconfig6 >/dev/null 2>&1; then
        echo "Não encontrei kwriteconfig6; instale kconfig para desligar o plugin KWin com segurança." >&2
        exit 1
    fi
    kwriteconfig6 --file "${kwinrc}" --group Plugins --key ajustevideo-appEnabled false
    kwriteconfig6 --file "${kwinrc}" --group Effect-ajustevideo-app --key Enabled false
    kwriteconfig6 --file "${kwinrc}" --group Effect-ajustevideo-app --key PreviewActive false
    kwriteconfig6 --file "${kwinrc}" --group Effect-ajustevideo-app --key PreviewTimestamp 0
    kwriteconfig6 --file "${kwinrc}" --group Plugins --key ajustevideoEnabled false
    kwriteconfig6 --file "${kwinrc}" --group Effect-ajustevideo --key Enabled false

    # Remove only this effect from the running compositor.
    if command -v qdbus6 >/dev/null 2>&1; then
        qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect ajustevideo-app >/dev/null 2>&1 || true
        qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect ajustevideo >/dev/null 2>&1 || true
    fi
fi

extension_id="ajuste-video@xoykor"
if command -v gnome-extensions >/dev/null 2>&1; then
    gnome-extensions disable "${extension_id}" >/dev/null 2>&1 || true
fi
if command -v gsettings >/dev/null 2>&1 && command -v python3 >/dev/null 2>&1; then
    current_extensions="$(gsettings get org.cinnamon enabled-extensions 2>/dev/null || true)"
    if [[ -n "${current_extensions}" ]]; then
        updated_extensions="$(python3 - "${current_extensions}" "${extension_id}" <<'PY'
import ast
import sys

try:
    extensions = ast.literal_eval(sys.argv[1])
except (SyntaxError, ValueError):
    raise SystemExit(0)
if isinstance(extensions, list):
    print(repr([item for item in extensions if item != sys.argv[2]]))
PY
        )"
        if [[ -n "${updated_extensions}" ]]; then
            gsettings set org.cinnamon enabled-extensions "${updated_extensions}" >/dev/null 2>&1 || true
        fi
    fi
fi

rm -f -- "${HOME}/.local/bin/ajuste-video"
rm -f -- "${data_dir}/applications/ajuste-video.desktop"
rm -f -- "${data_dir}/icons/hicolor/scalable/apps/ajuste-video.svg"
rm -rf -- \
    "${data_dir}/ajuste-video" \
    "${XDG_RUNTIME_DIR:-/run/user/${UID}}/ajuste-video" \
    "${data_dir}/kwin/effects/ajustevideo-app" \
    "${data_dir}/gnome-shell/extensions/${extension_id}" \
    "${data_dir}/cinnamon/extensions/${extension_id}" \
    "${XDG_CACHE_HOME:-${HOME}/.cache}/ajuste-video"

echo "Ajuste de vídeo removido. As preferências em ${config_dir}/ajuste-video/ foram preservadas."
