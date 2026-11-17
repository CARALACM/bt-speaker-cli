#!/usr/bin/env bash
#
# Copyright 2026 sendspin-cpp-cli Contributors
# Modified 2026 CARALACM for bt-speaker-cli
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Installs bt-speaker-cli from this checkout on Debian 12 (ARM64/x86_64), configures BlueZ
# as an A2DP speaker, and enables the systemd user service so it shares PulseAudio cleanly.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

MODE='user'
SPEAKER_NAME=''
OUTPUT_DEVICE='pulse'
AUTO_YES='no'
SKIP_DEPS='no'

usage() {
    cat <<'USAGE'
Usage: get_started_linux.sh [--name <name>] [--output <device>]
                            [--user-service | --system-service] [--skip-deps] [--yes]

  --name <name>      Bluetooth alias shown to PCs/phones (default: <hostname>-bt)
  --output <device>  Output device (default: pulse)
  --user-service     Install as a systemd user service sharing PulseAudio (default)
  --system-service   Install as a hardened systemd system service
  --skip-deps        Do not invoke apt-get to install build/runtime dependencies
  --yes              Run without interactive prompts
USAGE
}

while [ $# -gt 0 ]; do
    case "$1" in
        --name)
            SPEAKER_NAME="${2:-}"
            shift 2
            ;;
        --output)
            OUTPUT_DEVICE="${2:-}"
            shift 2
            ;;
        --user-service)
            MODE='user'
            shift
            ;;
        --system-service)
            MODE='system'
            shift
            ;;
        --skip-deps)
            SKIP_DEPS='yes'
            shift
            ;;
        --yes)
            AUTO_YES='yes'
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            printf 'Unknown option: %s\n' "$1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

SUDO=''
if [ "$(id -u)" -ne 0 ]; then
    SUDO='sudo'
fi

confirm() {
    local prompt="$1"
    if [ "$AUTO_YES" = 'yes' ]; then
        return 0
    fi
    printf '%s [Y/n] ' "$prompt"
    read -r answer
    case "$answer" in
        ''|y|Y|yes|YES) return 0 ;;
        *) return 1 ;;
    esac
}

if [ "$SKIP_DEPS" = 'no' ] && command -v apt-get >/dev/null 2>&1; then
    if confirm "Install Debian 12 build and Bluetooth/PulseAudio dependencies via apt-get?"; then
        PKGS=(build-essential cmake pkg-config libpulse-dev libdbus-1-dev libasound2-dev bluez)
        if systemctl --user is-active pipewire-pulse.service >/dev/null 2>&1 || \
           dpkg -s pipewire-pulse >/dev/null 2>&1; then
            PKGS+=(libspa-0.2-bluetooth)
        else
            PKGS+=(pulseaudio-module-bluetooth)
        fi
        $SUDO apt-get update
        $SUDO apt-get install -y "${PKGS[@]}"
    fi
fi

printf '\n==> Building bt-speaker-cli in %s/build\n' "$PROJECT_DIR"
cmake -B "$PROJECT_DIR/build" -S "$PROJECT_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$PROJECT_DIR/build" -j"$(nproc)"

printf '\n==> Installing bt-speaker-cli to /usr/local\n'
$SUDO cmake --install "$PROJECT_DIR/build" --component bt-speaker-cli

if [ -f /etc/bluetooth/main.conf ]; then
    if confirm "Configure /etc/bluetooth/main.conf for Audio Sink (Class = 0x200414, JustWorksRepairing = always)?"; then
        $SUDO python3 - <<'PY'
from pathlib import Path

conf = Path('/etc/bluetooth/main.conf')
text = conf.read_text()
lines = text.splitlines()

def upsert(lines, section, key, value):
    sec_idx = None
    for i, line in enumerate(lines):
        if line.strip() == f'[{section}]':
            sec_idx = i
            break
    if sec_idx is None:
        lines.extend([f'[{section}]', f'{key} = {value}'])
        return lines
    end_idx = len(lines)
    for j in range(sec_idx + 1, len(lines)):
        s = lines[j].strip()
        if s.startswith('[') and s.endswith(']'):
            end_idx = j
            break
        if s.startswith(f'{key} =') or s.startswith(f'#{key} =') or s.startswith(f'{key}='):
            lines[j] = f'{key} = {value}'
            return lines
    lines.insert(sec_idx + 1, f'{key} = {value}')
    return lines

lines = upsert(lines, 'General', 'Class', '0x200414')
lines = upsert(lines, 'General', 'JustWorksRepairing', 'always')
conf.write_text('\n'.join(lines) + '\n')
PY
        $SUDO systemctl enable --now bluetooth
        $SUDO systemctl restart bluetooth
    fi
fi

TARGET_USER="${SUDO_USER:-$USER}"
if [ "$MODE" = 'user' ]; then
    $SUDO usermod -aG audio,bluetooth "$TARGET_USER" || true
    $SUDO loginctl enable-linger "$TARGET_USER" || true

    CFG_DIR="$HOME/.config/bt-speaker-cli"
    CFG_FILE="$CFG_DIR/config"
    mkdir -p "$CFG_DIR"
    if [ ! -f "$CFG_FILE" ]; then
        {
            printf 'output = %s\n' "$OUTPUT_DEVICE"
            if [ -n "$SPEAKER_NAME" ]; then
                printf 'name = %s\n' "$SPEAKER_NAME"
            fi
            printf 'auto-connect = true\n'
            printf 'discoverable = true\n'
            printf 'allow-unpaired = true\n'
        } > "$CFG_FILE"
    fi

    systemctl --user daemon-reload
    systemctl --user enable --now bt-speaker-cli.service
    printf '\n==> bt-speaker-cli user service is running.\n'
    printf 'Check status with: bt-speaker-cli status\n'
else
    $SUDO systemd-sysusers
    if [ ! -f /etc/bt-speaker-cli.conf ]; then
        {
            printf 'output = %s\n' "$OUTPUT_DEVICE"
            if [ -n "$SPEAKER_NAME" ]; then
                printf 'name = %s\n' "$SPEAKER_NAME"
            fi
            printf 'auto-connect = true\n'
            printf 'discoverable = true\n'
            printf 'allow-unpaired = true\n'
            printf 'control-socket = /run/bt-speaker-cli/control.sock\n'
        } | $SUDO tee /etc/bt-speaker-cli.conf >/dev/null
    fi
    $SUDO systemctl reload dbus || true
    $SUDO systemctl daemon-reload
    $SUDO systemctl enable --now bt-speaker-cli.service
    printf '\n==> bt-speaker-cli system service is running.\n'
    printf 'Check status with: sudo bt-speaker-cli status --control-socket /run/bt-speaker-cli/control.sock\n'
fi
