#!/usr/bin/env bash
#
# package.sh — Build and package Crush Claw as a .deb
#
# Prerequisites:
#   sudo apt install build-essential cmake pkg-config \
#     libcurl4-openssl-dev liblua5.4-dev libsdl2-dev libjson-c-dev
#
# Output: crush-claw_1.0.0_amd64.deb

set -euo pipefail
cd "$(dirname "$0")"

VERSION="1.0.1"
ARCH="amd64"
PKG_NAME="crush-claw_${VERSION}_${ARCH}.deb"
BUILD_DIR="build"
PACKAGING_DIR="packaging"

echo "=== Crush Claw Package Builder v${VERSION} ==="
echo ""

# ---- 1. Build Release binary ----
echo "[1/5] Building Release binary..."
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j"$(nproc)"
cd ..
echo "  Binary: ${BUILD_DIR}/esp-claw-desktop"

# ---- 2. Strip and copy binary ----
echo "[2/5] Stripping and copying binary..."
strip -o "${PACKAGING_DIR}/usr/bin/esp-claw-desktop" "${BUILD_DIR}/esp-claw-desktop"
chmod 755 "${PACKAGING_DIR}/usr/bin/esp-claw-desktop"
SIZE=$(du -h "${PACKAGING_DIR}/usr/bin/esp-claw-desktop" | cut -f1)
echo "  Size: ${SIZE}"

# ---- 3. Copy CLI script ----
echo "[3/5] Copying CLI script..."
cp crush-claw "${PACKAGING_DIR}/usr/bin/crush-claw"
chmod 755 "${PACKAGING_DIR}/usr/bin/crush-claw"

# ---- 4. Copy README ----
echo "[4/5] Copying documentation..."
cp README.md "${PACKAGING_DIR}/usr/share/doc/crush-claw/README.md"

# ---- 5. Build .deb ----
echo "[5/5] Building .deb package..."
# Update control file with correct installed size
INSTALLED_SIZE=$(du -sk "${PACKAGING_DIR}/usr" | cut -f1)
sed -i "s/^Installed-Size:.*/Installed-Size: ${INSTALLED_SIZE}/" "${PACKAGING_DIR}/DEBIAN/control"

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
