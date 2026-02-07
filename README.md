# homelab-screen - Linux Monitor for Thermalright AIO Cooler USB LCD

[![CI](https://github.com/Huskynarr/homelab-screen/actions/workflows/ci.yml/badge.svg)](https://github.com/Huskynarr/homelab-screen/actions/workflows/ci.yml)
[![Release](https://github.com/Huskynarr/homelab-screen/actions/workflows/release-rolling.yml/badge.svg)](https://github.com/Huskynarr/homelab-screen/actions/workflows/release-rolling.yml)
[![Coverage](https://img.shields.io/badge/coverage-100%25-brightgreen)](https://github.com/Huskynarr/homelab-screen/actions/workflows/ci.yml)

`homelab-screen` shows your Linux host metrics on a Thermalright AIO Cooler USB LCD
(240x320 panel).
It is made for homelab setups (especially Proxmox), but also works on regular Linux hosts.

## What You Get

- Live pages for CPU, RAM, network, system uptime/load, and hostname
- Automatic page rotation (default: every 7 seconds)
- Optional Proxmox pages (VM/CT counts, storage, version) when Proxmox tools are present
- A systemd service for unattended startup

## What It Does Not Do

- No web UI or remote API
- No Windows/macOS runtime metrics (Linux only)
- No hardware fan/RGB/OC control
- No persistent config file yet (configure via CLI flags / service args)

## Quick Start (Rolling Release, Recommended)

Use the latest successful `main` build from the moving `rolling` release tag:

```bash
# 1) Download package + checksum
curl -L -o homelab-screen-linux-amd64.tar.gz \
  https://github.com/Huskynarr/homelab-screen/releases/download/rolling/homelab-screen-linux-amd64.tar.gz

curl -L -o homelab-screen-linux-amd64.tar.gz.sha256 \
  https://github.com/Huskynarr/homelab-screen/releases/download/rolling/homelab-screen-linux-amd64.tar.gz.sha256

# 2) Verify + unpack
sha256sum -c homelab-screen-linux-amd64.tar.gz.sha256
tar -xzf homelab-screen-linux-amd64.tar.gz
cd homelab-screen-linux-amd64

# 3) Install binary + udev rule + systemd unit
sudo install -m 755 homelab-screen /usr/local/bin/homelab-screen
sudo install -m 644 99-thermalright-lcd.rules /etc/udev/rules.d/99-thermalright-lcd.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
sudo install -m 644 homelab-screen.service /etc/systemd/system/homelab-screen.service
sudo systemctl daemon-reload
sudo systemctl enable --now homelab-screen

# 4) Verify
systemctl status homelab-screen
journalctl -u homelab-screen -f
```

If the LCD was already connected before the udev rule was installed, unplug/replug it once.

## Build From Source (Optional)

Use this only if you want to develop locally or change code.

```bash
sudo apt install libusb-1.0-0-dev build-essential pkg-config
make
sudo make install
```

## Run Manually (Optional)

```bash
homelab-screen
homelab-screen --interval 3
homelab-screen --interface vmbr0
```

Use `homelab-screen --help` for all options.

## Documentation

- If you only want to run the app, this README is enough.
- Deployment details and service overrides: [`docs/deployment.md`](docs/deployment.md)
- Usage, options, troubleshooting: [`docs/usage.md`](docs/usage.md)
- Architecture, tests, CI/CD, release flow: [`docs/development.md`](docs/development.md)

## License and Attribution

Licensed under **GNU GPL v3.0 only**. See [`LICENSE`](LICENSE).

Protocol behavior and hardware understanding for the Thermalright AIO Cooler USB LCD
were derived from and informed by
[trlcd_libusb](https://github.com/NoNameOnFile/trlcd_libusb).

Attribution details are documented in [`NOTICE`](NOTICE).
