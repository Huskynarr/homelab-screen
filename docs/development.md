# Development

## Repository Layout

| Path                          | Purpose                                                   |
| ----------------------------- | --------------------------------------------------------- |
| `src/state.c`                 | Global runtime state and signal handler                   |
| `src/metrics.c`               | Linux metrics collection (`/proc`, `/sys`, network)       |
| `src/proxmox.c`               | Optional Proxmox detection and metric collection          |
| `src/render.c`                | UI rendering and page drawing                             |
| `src/usb.c`                   | USB protocol init/cleanup/frame transfer                  |
| `src/cli.c`                   | CLI parsing and validation                                |
| `src/main.c`                  | Main loop, page rotation, orchestration                   |
| `src/trlcd.h`                 | Shared declarations and constants                         |
| `tests/test_homelab_screen.c` | Single-file unit test harness (includes compatibility TU) |
| `tests/mock_libusb.h`         | libusb test doubles                                       |
| `homelab-screen.c`            | Compatibility translation unit for tests                  |

## Build, Test, Lint

```bash
# Release build
make

# Unit tests
make test

# Coverage (enforced at 100% for src/*)
make coverage

# Sanitizers (ASan + UBSan)
make debug

# Warnings-as-errors build
make clean
make CFLAGS='-Wall -Wextra -pedantic -Werror -O2'

# Static analysis
clang --analyze $(find src -maxdepth 1 -type f -name '*.c' | sort) $(pkg-config --cflags libusb-1.0)
```

## Markdown Formatting

The repository includes a lightweight table formatter:

```bash
make fmt-md
```

It formats Markdown tables in `README.md` and all files in `docs/`.

## CI/CD

### CI (`.github/workflows/ci.yml`)

Runs on every push to `main`, pull request, and manual dispatch:

1. Build (release flags)
2. Unit tests
3. Sanitizer build
4. Warnings-as-errors build
5. `clang --analyze` over all `src/*.c` files (discovered dynamically)

### Rolling Release (`.github/workflows/release-rolling.yml`)

Runs on every push to `main` and manual dispatch:

1. `verify` job performs the same quality gates (build, tests, `-Werror`, static analysis)
2. `rolling-release` job runs only if `verify` succeeds
3. Tag `rolling` is force-moved to the latest `main` commit
4. The GitHub Release for `rolling` is updated in place with the newest artifacts

This is intentionally a rolling model (single moving release), not semantic versioning.

## Packaging

```bash
make package
```

Artifacts are written to `dist/`:

| File                                            | Description      |
| ----------------------------------------------- | ---------------- |
| `dist/homelab-screen-linux-amd64.tar.gz`        | Release archive  |
| `dist/homelab-screen-linux-amd64.tar.gz.sha256` | SHA-256 checksum |
