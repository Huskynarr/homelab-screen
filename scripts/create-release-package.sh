#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

ARCHIVE_BASE="homelab-screen-linux-amd64"
PACKAGE_DIR="dist/${ARCHIVE_BASE}"

rm -rf dist
mkdir -p "$PACKAGE_DIR"

install -m 755 homelab-screen "$PACKAGE_DIR/homelab-screen"
install -m 644 homelab-screen.service "$PACKAGE_DIR/homelab-screen.service"
install -m 644 udev/99-thermalright-lcd.rules "$PACKAGE_DIR/99-thermalright-lcd.rules"
install -m 644 README.md "$PACKAGE_DIR/README.md"
install -m 644 LICENSE "$PACKAGE_DIR/LICENSE"
install -m 644 NOTICE "$PACKAGE_DIR/NOTICE"
install -m 644 Makefile "$PACKAGE_DIR/Makefile"
install -d "$PACKAGE_DIR/docs"
install -m 644 docs/*.md "$PACKAGE_DIR/docs/"

tar -C dist -czf "dist/${ARCHIVE_BASE}.tar.gz" "$ARCHIVE_BASE"
if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "dist/${ARCHIVE_BASE}.tar.gz" > "dist/${ARCHIVE_BASE}.tar.gz.sha256"
else
    shasum -a 256 "dist/${ARCHIVE_BASE}.tar.gz" > "dist/${ARCHIVE_BASE}.tar.gz.sha256"
fi
