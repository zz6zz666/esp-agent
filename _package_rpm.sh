#!/usr/bin/env bash
#
# _package_rpm.sh — Build RPM packages for Crush Claw (all Linux arches)
#
# Prerequisites: rpmbuild, strip (cross-arch variants)
# Output: crush-claw-<version>-1.<arch>.rpm
#
# Like DEB's dpkg-deb, this is a pure packaging step — no arch validation.
# Pre-built cross-compiled binaries are expected in build-cross/<arch>/.
#
set -euo pipefail
cd "$(dirname "$0")"

VERSION="1.1.0"
BUILD_BASE="${PWD}/build-cross"
RPM_BASE="${BUILD_BASE}/rpmbuild"
SPEC_TEMPLATE="rpm/crush-claw.spec"

declare -A RPM_ARCH_MAP=(
  ["amd64"]="x86_64"
  ["i386"]="i686"
  ["arm64"]="aarch64"
  ["armhf"]="armv7hl"
)

declare -A STRIP_MAP=(
  ["amd64"]="strip"
  ["i386"]="i686-linux-gnu-strip"
  ["arm64"]="aarch64-linux-gnu-strip"
  ["armhf"]="arm-linux-gnueabihf-strip"
)

echo "=== Crush Claw RPM Package Builder v${VERSION} ==="
echo ""

for deb_arch in amd64 i386 arm64 armhf; do
  rpm_arch="${RPM_ARCH_MAP[$deb_arch]}"
  strip_tool="${STRIP_MAP[$deb_arch]}"

  src_bin="${BUILD_BASE}/${deb_arch}/esp-claw-desktop"
  if [ ! -f "$src_bin" ]; then
    echo "  SKIP RPM ${rpm_arch}: binary not found at ${src_bin}"
    continue
  fi

  if ! command -v "$strip_tool" &>/dev/null; then
    echo "  SKIP RPM ${rpm_arch}: strip tool '${strip_tool}' not found"
    continue
  fi

  echo "[RPM ${rpm_arch}] Packaging..."

  local_rpm="${RPM_BASE}/${rpm_arch}"
  rm -rf "$local_rpm"
  mkdir -p "${local_rpm}"/{SOURCES,SPECS,RPMS,SRPMS,BUILD,BUILDROOT}

  # Copy spec (no arch substitution needed — --target handles it)
  cp "${SPEC_TEMPLATE}" "${local_rpm}/SPECS/crush-claw.spec"

  # Strip and copy binary
  $strip_tool -o "${local_rpm}/SOURCES/esp-claw-desktop" "$src_bin"
  chmod 755 "${local_rpm}/SOURCES/esp-claw-desktop"

  # Copy CLI script
  cp crush-claw "${local_rpm}/SOURCES/crush-claw"
  chmod 755 "${local_rpm}/SOURCES/crush-claw"

  # Copy defaults
  rm -rf "${local_rpm}/SOURCES/defaults"
  cp -r defaults "${local_rpm}/SOURCES/defaults"

  # Copy icons
  cp installer/assets/lobster.svg "${local_rpm}/SOURCES/lobster.svg"
  cp installer/assets/lobster.png "${local_rpm}/SOURCES/lobster.png"

  # Copy desktop entry
  cp packaging/usr/share/applications/crush-claw.desktop "${local_rpm}/SOURCES/crush-claw.desktop"

  # Copy README
  cp README.md "${local_rpm}/SOURCES/README.md"

  # Copy systemd service
  cp packaging/usr/lib/systemd/user/crush-claw.service "${local_rpm}/SOURCES/crush-claw.service"

  # Copy emote assets
  rm -rf "${local_rpm}/SOURCES/assets_284_240"
  if [ -d "sim_hal/assets/284_240" ]; then
    cp -r sim_hal/assets/284_240 "${local_rpm}/SOURCES/assets_284_240"
  else
    mkdir -p "${local_rpm}/SOURCES/assets_284_240"
  fi

  # Build RPM — pure packaging, no arch validation
  rpmbuild -bb \
    --define "_topdir ${local_rpm}" \
    --define "dist .fc41" \
    --target "${rpm_arch}-linux" \
    "${local_rpm}/SPECS/crush-claw.spec" 2>&1 | tail -3

  out_rpm=$(find "${local_rpm}/RPMS" -name "*.rpm" -type f 2>/dev/null)
  if [ -n "$out_rpm" ]; then
    cp "$out_rpm" "crush-claw-${VERSION}-1.fc41.${rpm_arch}.rpm"
    echo "  Created: crush-claw-${VERSION}-1.fc41.${rpm_arch}.rpm"
    ls -lh "crush-claw-${VERSION}-1.fc41.${rpm_arch}.rpm"
  else
    echo "  FAILED: no RPM produced for ${rpm_arch}"
  fi
  echo ""
done

echo "=== Done ==="
ls -lh crush-claw-*.rpm 2>/dev/null
