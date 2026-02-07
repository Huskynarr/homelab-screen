# Deployment Guide

If you only want to run `homelab-screen`, the main setup in `README.md` is enough.
This page is for deeper deployment choices and operational details.

## Runtime Requirements

- Linux host with `systemd` and `udev`
- Thermalright AIO Cooler USB LCD panel (240x320)
- Access to `/usr/local/bin`, `/etc/systemd/system`, and `/etc/udev/rules.d`

## Installation Paths

1. Recommended: use the rolling release package from GitHub Releases.
2. Optional: build from source if you want local code changes.

## Source Build Prerequisites

Use this only for source builds:

- Debian / Ubuntu / Proxmox: `apt install libusb-1.0-0-dev build-essential pkg-config`
- Fedora / RHEL: `dnf install libusb1-devel gcc make pkgconfig`
- Arch: `pacman -S libusb base-devel pkgconf`
- openSUSE: `zypper install libusb-1_0-devel gcc make pkg-config`

## Service Override Example

To change CLI arguments in service mode, create a systemd drop-in:

```bash
sudo systemctl edit homelab-screen
```

Example content:

```ini
[Service]
ExecStart=
ExecStart=/usr/local/bin/homelab-screen --interface vmbr0 --interval 3
```

Then reload and restart:

```bash
sudo systemctl daemon-reload
sudo systemctl restart homelab-screen
```

## Verify Runtime

```bash
systemctl status homelab-screen
journalctl -u homelab-screen -f
```

Healthy startup usually shows:

1. USB device opened
2. selected network interface
3. periodic page updates

## Uninstall

```bash
sudo systemctl disable --now homelab-screen
sudo rm -f /etc/systemd/system/homelab-screen.service
sudo rm -f /etc/udev/rules.d/99-thermalright-lcd.rules
sudo systemctl daemon-reload
sudo rm -f /usr/local/bin/homelab-screen
```
