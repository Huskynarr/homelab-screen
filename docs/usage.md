# Usage

## CLI Options

| Option        | Argument | Default  | Description                       |
| ------------- | -------- | -------- | --------------------------------- |
| `--vid`       | HEX      | `0x0416` | USB Vendor ID                     |
| `--pid`       | HEX      | `0x5302` | USB Product ID                    |
| `--interval`  | SECS     | `7`      | Page rotation interval in seconds |
| `--interface` | NAME     | auto     | Network interface to monitor      |
| `--help`      | none     | n/a      | Show help                         |

Examples:

```bash
homelab-screen
homelab-screen --interval 3
homelab-screen --interface vmbr0
homelab-screen --vid 0416 --pid 5302
```

## Display Pages

| Page     | Availability            | Content                                   |
| -------- | ----------------------- | ----------------------------------------- |
| Overview | Always                  | CPU, RAM, temperature, load, time         |
| CPU      | Always                  | Large circular CPU gauge plus temperature |
| RAM      | Always                  | Large circular memory gauge               |
| Network  | Always                  | Interface, RX/TX rates, progress bars     |
| System   | Always                  | Hostname, uptime, load, clock/date        |
| Proxmox  | Proxmox tools available | Running/total VM and CT counts            |
| Storage  | Proxmox tools available | Storage pool usage bars                   |

## Proxmox Auto-Detection

If Proxmox tools are available, extra pages are enabled automatically.

| Command      | Purpose           |
| ------------ | ----------------- |
| `qm`         | VM metrics        |
| `pct`        | Container metrics |
| `pvesh`      | Storage metrics   |
| `pveversion` | Version display   |

## Service Defaults

| Item         | Value                                             |
| ------------ | ------------------------------------------------- |
| Unit file    | `homelab-screen.service`                          |
| Restart      | `Restart=on-failure`, `RestartSec=5`              |
| Hardening    | `NoNewPrivileges`, `Protect*`, `PrivateTmp`, etc. |
| udev rule    | `udev/99-thermalright-lcd.rules`                  |
| Access model | `TAG+="uaccess"` with `0660` mode                 |

To set custom CLI flags in service mode, override `ExecStart` using a systemd drop-in.

## Troubleshooting

| Symptom                                   | Checks / Fixes                                                                   |
| ----------------------------------------- | -------------------------------------------------------------------------------- |
| Device not found (`VID:0416 PID:5302`)    | Run `lsusb`; verify cable/port; test explicit `--vid/--pid`                      |
| Failed to claim interface                 | Verify udev rule; reload rules; replug device; test one root-run for diagnosis   |
| No Proxmox pages                          | Expected on non-Proxmox; on Proxmox verify `which pvesh qm pct`                  |
| Temperature shows `--`                    | Load sensor module (`coretemp`/`k10temp`); verify thermal files in `/sys`        |
| Service runs but display is blank/corrupt | Check `journalctl -u homelab-screen -f`; replug USB; test another cable/USB port |

## Known Limitations

| Area              | Limitation                                                |
| ----------------- | --------------------------------------------------------- |
| Linux assumptions | Metric collection expects Linux `/proc` and `/sys` layout |
| Proxmox storage   | Storage parsing depends on expected `pvesh` output shape  |
| Refresh loop      | Update loop is fixed to roughly 10 FPS                    |
| UI labels         | UI language/labels are currently static                   |

## Uninstall

```bash
sudo systemctl disable --now homelab-screen
sudo rm -f /etc/systemd/system/homelab-screen.service
sudo rm -f /etc/udev/rules.d/99-thermalright-lcd.rules
sudo systemctl daemon-reload
sudo rm -f /usr/local/bin/homelab-screen
```
