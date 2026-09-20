#!/usr/bin/env bash

set -euo pipefail

bin_dir="${HOME}/.local/bin"
unit_dir="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
autostart_dir="${XDG_CONFIG_HOME:-$HOME/.config}/autostart"
data_dir="${XDG_DATA_HOME:-$HOME/.local/share}/update-watchdog"

systemctl --user disable --now update-watchdog.service 2>/dev/null || true
rm -f "$unit_dir/update-watchdog.service"
rm -f "$autostart_dir/update-watchdog.desktop"
rm -f "$bin_dir/update-watchdog-tray" "$bin_dir/update-watchdog" \
    "$bin_dir/update-cc-switch" "$bin_dir/update-chatgpt-desktop"
rm -f "$data_dir/PKGBUILD.in" "$data_dir/PKGBUILD.chatgpt.in" \
    "$data_dir/chatgpt-launcher.sh"
systemctl --user daemon-reload

printf 'Update Watchdog Tray uninstalled. State remains in ~/.local/state/update-watchdog.\n'
