#!/usr/bin/env bash
#
# _package_appimage.sh — Build Crush Claw AppImage (all Linux arches)
#
# Uses pre-built cross-compiled binaries from build-cross/<arch>/.
# Dependencies resolved via ld-linux --list + qemu-user-static (cross-arch).
#
# Prerequisites: mksquashfs, qemu-user-static (for arm/aarch64 ldd)
# Output: crush-claw-<version>-<arch>.AppImage
#
set -euo pipefail
cd "$(dirname "$0")"

VERSION="1.1.0"
BUILD_BASE="${PWD}/build-cross"

declare -A AI_ARCH_MAP=(
  ["amd64"]="x86_64"
  ["i386"]="i686"
  ["arm64"]="aarch64"
  ["armhf"]="armhf"
)

# Dynamic linker path for each architecture
declare -A LD_PATH=(
  ["amd64"]="/lib64/ld-linux-x86-64.so.2"
  ["i386"]="/lib/ld-linux.so.2"
  ["arm64"]="/lib/ld-linux-aarch64.so.1"
  ["armhf"]="/lib/ld-linux-armhf.so.3"
)

# System libs to exclude from bundling
SYS_LIBS="ld-linux|linux-vdso|libc\.so|libm\.so|libdl\.so|libpthread\.so|librt\.so|libresolv\.so|libutil\.so|libnss|libstdc\+\+|libgcc_s|libBrokenLocale|libanl\.so"

echo "=== Crush Claw AppImage Builder v${VERSION} ==="
echo "  Host arch: $(uname -m)"
echo ""

for deb_arch in amd64 i386 arm64 armhf; do
  ai_arch="${AI_ARCH_MAP[$deb_arch]}"

  src_bin="${BUILD_BASE}/${deb_arch}/esp-claw-desktop"
  if [ ! -f "$src_bin" ]; then
    echo "  SKIP AppImage ${ai_arch}: binary not found"
    continue
  fi

  PKG_NAME="crush-claw-${VERSION}-${ai_arch}.AppImage"
  APPDIR="${BUILD_BASE}/AppDir-${deb_arch}"

  echo "[AppImage ${ai_arch}] Packaging..."

  rm -rf "${APPDIR}"
  mkdir -p "${APPDIR}/usr/bin"
  mkdir -p "${APPDIR}/usr/lib"
  mkdir -p "${APPDIR}/usr/share/crush-claw/defaults"
  mkdir -p "${APPDIR}/usr/share/crush-claw/assets/284_240"
  mkdir -p "${APPDIR}/usr/share/icons/hicolor/scalable/apps"
  mkdir -p "${APPDIR}/usr/share/applications"
  mkdir -p "${APPDIR}/usr/share/doc/crush-claw"
  mkdir -p "${APPDIR}/usr/lib/systemd/user"

  # Copy binary
  cp "$src_bin" "${APPDIR}/usr/bin/esp-claw-desktop"
  chmod 755 "${APPDIR}/usr/bin/esp-claw-desktop"

  # Copy CLI script
  cp crush-claw "${APPDIR}/usr/bin/"
  chmod 755 "${APPDIR}/usr/bin/crush-claw"

  # Copy data
  cp -r defaults/* "${APPDIR}/usr/share/crush-claw/defaults/"
  cp -r sim_hal/assets/284_240/* "${APPDIR}/usr/share/crush-claw/assets/284_240/" 2>/dev/null || true
  cp installer/assets/lobster.svg "${APPDIR}/usr/share/icons/hicolor/scalable/apps/"
  cp installer/assets/lobster.png "${APPDIR}/lobster.png"
  cp packaging/usr/share/applications/crush-claw.desktop "${APPDIR}/usr/share/applications/crush-claw.desktop"
  cp README.md "${APPDIR}/usr/share/doc/crush-claw/README.md"
  cp packaging/usr/lib/systemd/user/crush-claw.service "${APPDIR}/usr/lib/systemd/user/crush-claw.service"

  # ---- Bundle shared libraries via ld-linux --list ----
  local_ld="${LD_PATH[$deb_arch]}"

  if [ -x "$local_ld" ] || [ -L "$local_ld" ]; then
    echo "  Bundling shared libraries..."
    bundled=0
    # Use arch-specific dynamic linker to resolve deps (QEMU handles cross-arch)
    while IFS= read -r line; do
      # Lines look like: libfoo.so.X => /path/to/libfoo.so.X (0x...)
      libpath=$(echo "$line" | sed -n 's/.*=> *\([^ ]*\) .*/\1/p')
      [ -z "$libpath" ] && continue
      [ ! -f "$libpath" ] && continue

      libname=$(basename "$libpath")
      # Skip system libs
      if echo "$libname" | grep -qE "$SYS_LIBS"; then
        continue
      fi
      # Skip if already bundled
      [ -f "${APPDIR}/usr/lib/${libname}" ] && continue

      cp "$libpath" "${APPDIR}/usr/lib/"
      bundled=$((bundled + 1))
    done < <("$local_ld" --list "$src_bin" 2>/dev/null)
    echo "  Bundled ${bundled} libraries"
  else
    echo "  ld-linux not found for ${deb_arch}: ${local_ld}"
    echo "  Skipping library bundle (target system needs deps installed)"
  fi

  # Create AppRun
  cat > "${APPDIR}/AppRun" << 'APPRUNEOF'
#!/bin/bash
SELF=$(readlink -f "$0")
HERE=$(dirname "$SELF")
export PATH="${HERE}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"
export CRUSH_CLAW_DATA_DIR="${HOME}/.crush-claw"
exec "${HERE}/usr/bin/esp-claw-desktop" "$@"
APPRUNEOF
  chmod +x "${APPDIR}/AppRun"

  # Create .desktop file in AppDir root
  cp "${APPDIR}/usr/share/applications/crush-claw.desktop" "${APPDIR}/crush-claw.desktop"

  # Build AppImage
  if command -v mksquashfs &>/dev/null; then
    mksquashfs "${APPDIR}" "${PKG_NAME}" -root-owned -noappend -comp xz 2>&1 | tail -1

    echo "  Created: ${PKG_NAME}"
    ls -lh "${PKG_NAME}"
  else
    tar -czf "crush-claw-${VERSION}-${ai_arch}.tar.gz" -C build-cross "AppDir-${deb_arch}"
    echo "  Created: crush-claw-${VERSION}-${ai_arch}.tar.gz"
  fi
  echo ""
done

echo "=== Done ==="
ls -lh crush-claw-*.AppImage 2>/dev/null
