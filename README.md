# Update Watchdog Tray

A tiny native C system-tray watchdog for KDE Plasma on Wayland. It checks for
updates once per hour and sends a desktop notification when a newer version is
available.

Currently monitored:

- [CC Switch](https://github.com/farion1231/cc-switch) latest GitHub release
- [OpenAI Codex](https://www.npmjs.com/package/@openai/codex) newest active
  pre-release (`alpha`, `beta`, `rc`, `next`, or `canary`)

The application uses GTK3 and Ayatana AppIndicator to register a native
StatusNotifierItem with Plasma. It does not use Qt. Clipboard actions write
through KDE Klipper's D-Bus API, with GTK clipboard as a fallback.

## Features

- Native compiled tray application, no Electron or Python runtime
- Hourly checks plus a manual **Check now** action
- Current and available versions shown in the tray menu
- One desktop notification per newly discovered version
- Copy exact update commands to the KDE clipboard
- Starts with the Plasma session through a systemd user service
- Never installs updates automatically

## Requirements

Arch Linux / Manjaro:

```bash
sudo pacman -S --needed base-devel gtk3 libayatana-appindicator \
  curl jq libnotify pacman-contrib git
```

The CC Switch check expects the AUR package `cc-switch-bin`. The Codex check
expects a global pnpm installation of `@openai/codex`.

## Install

```bash
git clone https://github.com/Xboxpig/update-watchdog-tray.git
cd update-watchdog-tray
./install.sh
```

The installer builds the C binary, copies both executables to `~/.local/bin`,
installs the user service, and starts it immediately.

## Usage

The icon appears in the Plasma system tray. If Plasma hides it, open the tray
configuration and set **Update Watchdog** to **Always shown**.

```bash
# Service status
systemctl --user status update-watchdog.service

# Follow logs
journalctl --user -u update-watchdog.service -f

# Restart the tray
systemctl --user restart update-watchdog.service
```

## Uninstall

```bash
./uninstall.sh
```

Runtime state is kept under `~/.local/state/update-watchdog` and is deliberately
left intact during uninstall so notification history is preserved.

## License

MIT
