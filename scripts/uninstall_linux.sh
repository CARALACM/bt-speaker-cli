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
# Removes bt-speaker-cli binary and systemd units; keeps config/state unless --purge is given.

set -euo pipefail

PURGE='no'
if [ "${1:-}" = '--purge' ]; then
    PURGE='yes'
fi

SUDO=''
if [ "$(id -u)" -ne 0 ]; then
    SUDO='sudo'
fi

systemctl --user disable --now bt-speaker-cli.service >/dev/null 2>&1 || true
$SUDO systemctl disable --now bt-speaker-cli.service >/dev/null 2>&1 || true

$SUDO rm -f /usr/local/bin/bt-speaker-cli
$SUDO rm -f /usr/local/lib/systemd/system/bt-speaker-cli.service
$SUDO rm -f /usr/local/lib/systemd/user/bt-speaker-cli.service
$SUDO rm -f /usr/local/lib/sysusers.d/bt-speaker-cli.conf
$SUDO rm -f /usr/local/share/dbus-1/system.d/bt-speaker-cli.conf
$SUDO rm -rf /usr/local/share/doc/bt-speaker-cli

$SUDO systemctl daemon-reload || true
systemctl --user daemon-reload >/dev/null 2>&1 || true

if [ "$PURGE" = 'yes' ]; then
    $SUDO rm -rf /var/lib/bt-speaker-cli /etc/bt-speaker-cli.conf
    rm -rf "$HOME/.local/state/bt-speaker-cli" "$HOME/.config/bt-speaker-cli"
    $SUDO userdel bt-speaker-cli >/dev/null 2>&1 || true
fi

printf 'bt-speaker-cli uninstalled.\n'
