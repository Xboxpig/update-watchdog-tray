#!/usr/bin/env bash

set -euo pipefail

bin_dir="${HOME}/.local/bin"
unit_dir="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"

systemctl --user disable --now update-watchdog.service 2>/dev/null || true
rm -f "$unit_dir/update-watchdog.service"
rm -f "$bin_dir/update-watchdog-tray" "$bin_dir/update-watchdog"
systemctl --user daemon-reload

printf 'Update Watchdog Tray uninstalled. State remains in ~/.local/state/update-watchdog.\n'
