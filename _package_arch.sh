#!/usr/bin/env bash
#
# _package_arch.sh — Build Arch Linux packages for Crush Claw (native arch only)
#
# Prerequisites: makepkg, base-devel
# Output: crush-claw-<version>-<arch>.pkg.tar.zst
#
set -euo pipefail
cd "$(dirname "$0")"

VERSION="1.1.0"

echo "=== Crush Claw Arch Linux Package Builder v${VERSION} ==="
echo ""

if ! command -v makepkg &>/dev/null; then
  echo "ERROR: makepkg not found. Install with: sudo pacman -S base-devel"
  exit 1
fi

# Setup build directory
BUILD_DIR="build-cross/arch-pkg"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Copy arch packaging files
cp arch/PKGBUILD "$BUILD_DIR/PKGBUILD"
cp arch/crush-claw.install "$BUILD_DIR/crush-claw.install"

# Clone or link source (use current directory as source)
# For local build, override source in PKGBUILD
echo "[1/3] Setting up source..."
# Create a tarball from current project for makepkg
TARBALL="${BUILD_DIR}/crush-claw-${VERSION}.tar.gz"
tar --exclude='.git' --exclude='build' --exclude='build-cross' --exclude='build-all' \
    --exclude='*.deb' --exclude='*.rpm' --exclude='*.AppImage' --exclude='*.zip' \
    -czf "$TARBALL" -C .. "$(basename "$PWD")"

# Generate checksum
SHA256=$(sha256sum "$TARBALL" | cut -d' ' -f1)

# Update PKGBUILD for local build
sed -i "s|source=.*|source=(\"crush-claw-${VERSION}.tar.gz\")|" "${BUILD_DIR}/PKGBUILD"
sed -i "s|sha256sums=.*|sha256sums=('${SHA256}')|" "${BUILD_DIR}/PKGBUILD"
# Remove git submodule init (not needed when building from tarball)
sed -i 's|git submodule update.*||' "${BUILD_DIR}/PKGBUILD"

# Build package
echo "[2/3] Building package..."
(cd "$BUILD_DIR" && makepkg -sf --noconfirm 2>&1 | tail -20)

# Copy output
echo "[3/3] Copying package..."
cp "${BUILD_DIR}/"*.pkg.tar.* . 2>/dev/null || true

echo ""
echo "=== Done ==="
ls -lh crush-claw-*.pkg.tar.* 2>/dev/null
