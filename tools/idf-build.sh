#!/usr/bin/env bash
set -euo pipefail

project=${1:-.}
target=${2:-esp32c3}
build_dir=${3:-build}
profile=${4:-}
idf_root=${IDF_PATH:-"$HOME/esp/esp-idf"}

if [[ ! -f "$idf_root/export.sh" ]]; then
    echo "ESP-IDF export.sh not found: $idf_root/export.sh" >&2
    exit 2
fi

# shellcheck disable=SC1090
source "$idf_root/export.sh" >/dev/null
# Keep profiles hermetic.  ESP-IDF otherwise writes the selected defaults back
# to a root sdkconfig, so a production build can accidentally change a later
# development-C3 build (and vice versa).
args=(-C "$project" -B "$build_dir" -D "IDF_TARGET=$target" -D "SDKCONFIG=$build_dir/sdkconfig")
if [[ -n "$profile" ]]; then
    if [[ ! -f "$project/sdkconfig.$profile" ]]; then
        echo "Unknown sdkconfig profile: $profile" >&2
        exit 2
    fi
    args+=(-D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.$profile")
fi
idf.py "${args[@]}" build
