# Update Watchdog Tray

A tiny native C system-tray watchdog for Manjaro KDE Plasma on Wayland. It
tracks selected fast-moving applications that are unavailable from, or
intentionally managed outside, the AUR; checks once per hour; and sends a
desktop notification when a newer version is available.

Currently monitored:

- [CC Switch](https://github.com/farion1231/cc-switch) latest GitHub release
- ChatGPT Desktop latest official Linux stable release
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
- Update CC Switch directly from its GitHub release with a native progress dialog
- Update ChatGPT Desktop from OpenAI's official Linux repository through the same progress dialog
- Keep CC Switch under pacman management by building a verified local package
- Keep ChatGPT Desktop under pacman management and preserve the required appmenu `DT_NEEDED` crash workaround
- Starts with the Plasma session through a systemd user service
- Registers a KDE Plasma autostart entry that activates the user service at login
- Never installs updates automatically

## Requirements

Arch Linux / Manjaro:

```bash
sudo pacman -S --needed base-devel gtk3 libayatana-appindicator \
  appmenu-gtk-module patchelf curl jq libnotify pacman-contrib git
```

The CC Switch check expects the pacman package `cc-switch-bin`. Its updater
uses the x86_64 Debian asset from the latest GitHub release and the SHA-256
digest returned by the GitHub Releases API. The ChatGPT check expects
`chatgpt-desktop`; its updater consumes OpenAI's official Debian repository
metadata, verifies the published size and SHA-256, builds local package release
`2`, and patches the Electron binary to retain
`/usr/lib/gtk-3.0/modules/libappmenu-gtk-module.so`. The Codex check expects a
global pnpm installation of `@openai/codex`.

## Install

```bash
git clone https://github.com/Xboxpig/update-watchdog-tray.git
cd update-watchdog-tray
./install.sh
```

The installer builds the C binary, installs the watchdog and both package
upgrade helpers under `~/.local/bin`, installs the user service, and starts it
immediately.

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
