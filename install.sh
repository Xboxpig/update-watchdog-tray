#!/usr/bin/env bash

set -euo pipefail

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
bin_dir="${HOME}/.local/bin"
unit_dir="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"

for command in cc pkg-config curl jq notify-send vercmp systemctl; do
    command -v "$command" >/dev/null || {
        printf 'Missing dependency: %s\n' "$command" >&2
        exit 1
    }
done

pkg-config --exists ayatana-appindicator3-0.1 gtk+-3.0 || {
    printf 'Missing GTK3 or Ayatana AppIndicator development files.\n' >&2
    exit 1
}

make -C "$repo_dir"
install -Dm755 "$repo_dir/build/update-watchdog-tray" "$bin_dir/update-watchdog-tray"
install -Dm755 "$repo_dir/scripts/update-watchdog" "$bin_dir/update-watchdog"
install -Dm644 "$repo_dir/systemd/update-watchdog.service" "$unit_dir/update-watchdog.service"

systemctl --user daemon-reload
systemctl --user enable --now update-watchdog.service
printf 'Update Watchdog Tray installed and started.\n'
