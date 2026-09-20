#!/usr/bin/env bash

set -euo pipefail

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
bin_dir="${HOME}/.local/bin"
unit_dir="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
autostart_dir="${XDG_CONFIG_HOME:-$HOME/.config}/autostart"
data_dir="${XDG_DATA_HOME:-$HOME/.local/share}/update-watchdog"

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
install -Dm755 "$repo_dir/scripts/update-cc-switch" "$bin_dir/update-cc-switch"
install -Dm755 "$repo_dir/scripts/update-chatgpt-desktop" "$bin_dir/update-chatgpt-desktop"
install -Dm644 "$repo_dir/packaging/PKGBUILD.in" "$data_dir/PKGBUILD.in"
install -Dm644 "$repo_dir/packaging/PKGBUILD.chatgpt.in" "$data_dir/PKGBUILD.chatgpt.in"
install -Dm755 "$repo_dir/packaging/chatgpt-launcher.sh" "$data_dir/chatgpt-launcher.sh"
install -Dm644 "$repo_dir/systemd/update-watchdog.service" "$unit_dir/update-watchdog.service"
install -Dm644 "$repo_dir/autostart/update-watchdog.desktop" "$autostart_dir/update-watchdog.desktop"

systemctl --user daemon-reload
systemctl --user enable update-watchdog.service
systemctl --user restart update-watchdog.service
printf 'Update Watchdog Tray installed and started.\n'
