#!/usr/bin/env bash
#
# package.sh — Build and package Crush Claw as a .deb
#
# Prerequisites:
#   sudo apt install build-essential cmake pkg-config \
#     libcurl4-openssl-dev liblua5.4-dev libsdl2-dev libjson-c-dev
#
# Output: crush-claw_1.1.0_amd64.deb

set -euo pipefail
cd "$(dirname "$0")"

VERSION="1.1.0"
ARCH="amd64"
PKG_NAME="crush-claw_${VERSION}_${ARCH}.deb"
BUILD_DIR="build"
PACKAGING_DIR="packaging"

echo "=== Crush Claw Package Builder v${VERSION} ==="
echo ""

# ---- 1. Build Release binary ----
echo "[1/7] Building Release binary..."
mkdir -p "${BUILD_DIR}"
cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" -- -j"$(nproc)"
echo "  Binary: ${BUILD_DIR}/esp-claw-desktop"

# ---- 2. Strip and copy binary ----
echo "[2/7] Stripping and copying binary..."
strip -o "${PACKAGING_DIR}/usr/bin/esp-claw-desktop" "${BUILD_DIR}/esp-claw-desktop"
chmod 755 "${PACKAGING_DIR}/usr/bin/esp-claw-desktop"
SIZE=$(du -h "${PACKAGING_DIR}/usr/bin/esp-claw-desktop" | cut -f1)
echo "  Size: ${SIZE}"

# ---- 3. Copy CLI script ----
echo "[3/7] Copying CLI script..."
cp crush-claw "${PACKAGING_DIR}/usr/bin/crush-claw"
chmod 755 "${PACKAGING_DIR}/usr/bin/crush-claw"

# ---- 4. Sync defaults from root defaults/ ----
echo "[4/7] Syncing defaults from root defaults/..."
rm -rf "${PACKAGING_DIR}/usr/share/crush-claw/defaults"
cp -r defaults "${PACKAGING_DIR}/usr/share/crush-claw/defaults"
echo "  Defaults synced ($(find defaults -type f | wc -l) files)"

# ---- 5. Install icons and desktop entry ----
echo "[5/7] Installing icons and desktop entry..."
mkdir -p "${PACKAGING_DIR}/usr/share/icons/hicolor/scalable/apps"
mkdir -p "${PACKAGING_DIR}/usr/share/pixmaps"
mkdir -p "${PACKAGING_DIR}/usr/share/applications"
cp installer/assets/lobster.svg "${PACKAGING_DIR}/usr/share/icons/hicolor/scalable/apps/lobster.svg"
cp installer/assets/lobster.png "${PACKAGING_DIR}/usr/share/pixmaps/lobster.png"
echo "  Icons installed"

# ---- 6. Copy README and desktop file ----
echo "[6/7] Copying documentation..."
cp README.md "${PACKAGING_DIR}/usr/share/doc/crush-claw/README.md"

# ---- 7. Build .deb ----
echo "[7/7] Building .deb package..."
INSTALLED_SIZE=$(du -sk "${PACKAGING_DIR}/usr" | cut -f1)
sed -i "s/^Installed-Size:.*/Installed-Size: ${INSTALLED_SIZE}/" "${PACKAGING_DIR}/DEBIAN/control"
sed -i "s/^Version:.*/Version: ${VERSION}/" "${PACKAGING_DIR}/DEBIAN/control"

dpkg-deb --build "${PACKAGING_DIR}" "${PKG_NAME}"

echo ""
echo "=== Done ==="
echo "Package: ${PKG_NAME}"
ls -lh "${PKG_NAME}"
echo ""
echo "Install with:"
echo "  sudo apt install ./${PKG_NAME}"
echo ""
echo "Verify with:"
echo "  dpkg -c ${PKG_NAME}"
echo "  dpkg --info ${PKG_NAME}"
