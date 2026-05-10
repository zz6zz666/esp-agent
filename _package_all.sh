#!/usr/bin/env bash
#
# _package_all.sh — Strip and package Crush Claw for all Linux targets
#
# Targets: amd64, i386, arm64, armhf
# Output: crush-claw_<version>_<arch>.deb
#
set -euo pipefail
cd "$(dirname "$0")"

VERSION="1.1.0"
BUILD_BASE="build-cross"
PACKAGING_TEMPLATE="packaging"

echo "=== Crush Claw Cross-Platform Package Builder ==="
echo ""

for arch_tuple in "amd64:amd64" "i386:i386" "arm64:arm64" "armhf:armhf"; do
  arch="${arch_tuple#*:}"
  tuple="${arch_tuple%:*}"
  
  echo "[${arch}] Packaging..."

  SRC="${BUILD_BASE}/${arch}/esp-claw-desktop"
  PKG_NAME="crush-claw_${VERSION}_${arch}.deb"
  
  if [ ! -f "$SRC" ]; then
    echo "  SKIP: ${SRC} not found"
    continue
  fi

  # Create per-arch packaging dir
  PKG_DIR="${BUILD_BASE}/deb-${arch}"
  rm -rf "$PKG_DIR"
  cp -r "$PACKAGING_TEMPLATE" "$PKG_DIR"

  # Update control for target arch
  sed -i "s/^Architecture:.*/Architecture: ${arch}/" "${PKG_DIR}/DEBIAN/control"
  sed -i "s/^Version:.*/Version: ${VERSION}/" "${PKG_DIR}/DEBIAN/control"

  # Strip and install binary
  mkdir -p "${PKG_DIR}/usr/bin"
  case "$arch" in
    amd64)  STRIP=strip ;;
    i386)   STRIP=i686-linux-gnu-strip ;;
    arm64)  STRIP=aarch64-linux-gnu-strip ;;
    armhf)  STRIP=arm-linux-gnueabihf-strip ;;
  esac
  $STRIP -o "${PKG_DIR}/usr/bin/esp-claw-desktop" "$SRC"
  chmod 755 "${PKG_DIR}/usr/bin/esp-claw-desktop"
  
  # Copy CLI script (architecture independent)
  cp crush-claw "${PKG_DIR}/usr/bin/crush-claw"
  chmod 755 "${PKG_DIR}/usr/bin/crush-claw"

  # Recalculate installed size
  INSTALLED_SIZE=$(du -sk "${PKG_DIR}/usr" | cut -f1)
  sed -i "s/^Installed-Size:.*/Installed-Size: ${INSTALLED_SIZE}/" "${PKG_DIR}/DEBIAN/control"

  # Build .deb
  dpkg-deb --build "${PKG_DIR}" "${PKG_NAME}"
  echo "  Created: ${PKG_NAME} ($(ls -lh ${PKG_NAME} | awk '{print $5}'))"
done

echo ""
echo "=== All packages ==="
ls -lh crush-claw_*.deb 2>/dev/null
