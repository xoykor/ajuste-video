#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID} -eq 0 ]]; then
    echo "Execute como usuário normal; o script usa sudo somente para dependências." >&2
    exit 1
fi

if ! command -v pacman >/dev/null 2>&1; then
    echo "Este instalador requer CachyOS ou outra distribuição baseada em Arch com pacman." >&2
    exit 1
fi

script_source="${BASH_SOURCE[0]-}"
if [[ -n "${script_source}" && -f "${script_source}" ]]; then
    script_dir="$(cd -- "$(dirname -- "${script_source}")" && pwd)"
else
    script_dir="${PWD}"
fi
needs_checkout=false
if [[ ! -f "${script_dir}/CMakeLists.txt" ]]; then
    needs_checkout=true
fi

packages=(cmake qt6-base kconfig qt6-tools python extra-cmake-modules kwin vulkan-headers)
if [[ "${needs_checkout}" == true ]]; then
    packages+=(git)
fi
missing_packages=()
for package in "${packages[@]}"; do
    if ! pacman -Q "${package}" >/dev/null 2>&1; then
        missing_packages+=("${package}")
    fi
done
if ! command -v c++ >/dev/null 2>&1 || ! command -v make >/dev/null 2>&1; then
    missing_packages+=(base-devel)
fi
if ((${#missing_packages[@]})); then
    sudo pacman -S --needed "${missing_packages[@]}"
fi

checkout_dir=""
build_dir=""
cleanup() {
    if [[ -n "${build_dir}" && -d "${build_dir}" ]]; then
        rm -rf -- "${build_dir}"
    fi
    if [[ -n "${checkout_dir}" && -d "${checkout_dir}" ]]; then
        rm -rf -- "${checkout_dir}"
    fi
}
trap cleanup EXIT

if [[ "${needs_checkout}" == true ]]; then
    if ! command -v git >/dev/null 2>&1; then
        echo "Não encontrei git para baixar o código-fonte." >&2
        exit 1
    fi
    checkout_dir="$(mktemp -d "${TMPDIR:-/tmp}/ajuste-video.XXXXXX")"
    source_ref="${AJUSTE_VIDEO_VERSION:-main}"
    git clone --depth 1 --branch "${source_ref}" \
        https://github.com/xoykor/ajuste-video.git "${checkout_dir}/source"
    script_dir="${checkout_dir}/source"
fi

for command in cmake c++ make python3; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        echo "Comando necessário não encontrado: ${command}. Confira os pacotes de dependência." >&2
        exit 1
    fi
done

desktop_env="${XDG_CURRENT_DESKTOP:-}:${DESKTOP_SESSION:-}"
desktop_env="${desktop_env,,}"
is_kde=false
if [[ "${desktop_env}" == *kde* || "${desktop_env}" == *plasma* ]]; then
    is_kde=true
fi

install_prefix="/usr"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/ajuste-video-build.XXXXXX")"
cmake -S "${script_dir}" -B "${build_dir}" \
    -DBUILD_KWIN_EFFECT=ON -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${install_prefix}"
cmake --build "${build_dir}" --parallel "$(nproc)"
sudo cmake --install "${build_dir}"

if [[ "${is_kde}" == true ]]; then
    if ! command -v kwriteconfig6 >/dev/null 2>&1; then
        echo "Comando necessário não encontrado: kwriteconfig6. Instale o pacote kconfig." >&2
        exit 1
    fi

    # Clean up legacy scripted effect if previously installed
    rm -rf -- "${HOME}/.local/share/kwin/effects/ajustevideo-app"

    mapfile -t effect_settings < <(python3 - "${HOME}/.config/ajuste-video/settings.json" \
        "${HOME}/.config/kwinrc" <<'PY'
import configparser
import json
import math
import sys

settings_path, kwinrc_path = sys.argv[1:]
try:
    with open(settings_path, encoding="utf-8") as source:
        settings = json.load(source)
except (OSError, json.JSONDecodeError):
    settings = {}

config = configparser.ConfigParser(interpolation=None)
config.read(kwinrc_path, encoding="utf-8")
effect_group = "Effect-ajustevideo"
legacy_group = "Effect-ajustevideo-app"

def old_value(group, key, fallback):
    return config.get(group, key, fallback=fallback)

enabled = settings.get("enabled")
if enabled is None:
    enabled = old_value(effect_group, "Enabled", old_value(legacy_group, "Enabled", "false"))
if isinstance(enabled, bool):
    enabled = "true" if enabled else "false"
else:
    enabled = "true" if str(enabled).lower() in {"1", "true", "yes", "on"} else "false"
print(enabled)

controls = [
    ("brightness", "Brightness", 0.0, -0.20, 0.20),
    ("contrast", "Contrast", 1.0, 0.80, 1.20),
    ("gamma", "Gamma", 1.0, 0.50, 1.50),
    ("saturation", "Saturation", 1.0, 0.75, 1.25),
    ("hue", "Hue", 0.0, -30.0, 30.0),
    ("temperature", "ColorTemperature", 0.0, -0.25, 0.25),
]
for json_key, config_key, default, minimum, maximum in controls:
    raw = settings.get(json_key)
    if raw is None:
        raw = old_value(effect_group, config_key, old_value(legacy_group, config_key, str(default)))
    try:
        value = float(raw)
    except (TypeError, ValueError):
        value = default
    if not math.isfinite(value):
        value = default
    value = max(minimum, min(maximum, value))
    print(f"{value:.6f}")
PY
    )

    kwinrc="${HOME}/.config/kwinrc"
    keys=(Enabled Brightness Contrast Gamma Saturation Hue ColorTemperature)
    for index in "${!keys[@]}"; do
        kwriteconfig6 --file "${kwinrc}" --group Effect-ajustevideo \
            --key "${keys[index]}" -- "${effect_settings[index]}"
    done
    kwriteconfig6 --file "${kwinrc}" --group Effect-ajustevideo --key PreviewActive false
    kwriteconfig6 --file "${kwinrc}" --group Effect-ajustevideo --key PreviewTimestamp 0
    kwriteconfig6 --file "${kwinrc}" --group Plugins --key ajustevideoEnabled true
    kwriteconfig6 --file "${kwinrc}" --group Plugins --key ajustevideo-appEnabled false
    kwriteconfig6 --file "${kwinrc}" --group Effect-ajustevideo-app --key Enabled false

    if command -v qdbus6 >/dev/null 2>&1 && \
       qdbus6 org.kde.KWin /Effects org.freedesktop.DBus.Peer.Ping >/dev/null 2>&1; then
        if [[ "$(qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.isEffectLoaded ajustevideo-app)" == true ]]; then
            qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect ajustevideo-app >/dev/null 2>&1 || true
        fi
        qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.reconfigureEffect ajustevideo >/dev/null 2>&1 || \
            qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect ajustevideo >/dev/null 2>&1 || true
    fi

    echo "Ajuste de vídeo instalado com sucesso no sistema (efeito KWin ajustevideo ativado)."
else
    echo "Ajuste de vídeo instalado em ${install_prefix}. Abra o aplicativo para ativar o suporte deste ambiente."
fi
