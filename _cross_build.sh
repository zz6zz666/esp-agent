#!/usr/bin/env bash
#
# _cross_build.sh — Cross-compile and package Crush Claw for all Linux targets
#
# Targets: amd64, i386, arm64, armhf
# Output: build-cross/<arch>/esp-claw-desktop
#         crush-claw_<version>_<arch>.deb
#
set -euo pipefail
cd "$(dirname "$0")"

NPROC=$(nproc)
VERSION="1.1.0"
BUILD_BASE="build-cross"
PACKAGING_TEMPLATE="packaging"

echo "=== Crush Claw Cross-Platform Build & Package ==="
echo ""

do_build() {
  local label="$1"    # display name
  local dir="$2"      # output dir under BUILD_BASE
  local toolchain="$3" # path to toolchain file or empty for native
  local cflags="$4"
  local pkgcfg="$5"   # PKG_CONFIG_LIBDIR or empty

  echo "[${label}] Building..."
  mkdir -p "${BUILD_BASE}/${dir}"

  local cmake_args=(-B "${BUILD_BASE}/${dir}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="${cflags}")
  if [ -n "$toolchain" ]; then
    cmake_args+=(-DCMAKE_TOOLCHAIN_FILE="${toolchain}")
  fi

  if [ -n "$pkgcfg" ]; then
    env PKG_CONFIG_LIBDIR="${pkgcfg}" cmake "${cmake_args[@]}"
  else
    cmake "${cmake_args[@]}"
  fi

  cmake --build "${BUILD_BASE}/${dir}" -- -j"${NPROC}"
  echo "  Done: ${BUILD_BASE}/${dir}/esp-claw-desktop"
  echo ""
}

do_package() {
  local arch="$1"
  local strip_tool="$2"

  local src="${BUILD_BASE}/${arch}/esp-claw-desktop"
  local pkg_name="crush-claw_${VERSION}_${arch}.deb"

  if [ ! -f "$src" ]; then
    echo "  SKIP package ${arch}: binary not found"
    return
  fi

  echo "[Package ${arch}] Packaging..."
  local pkg_dir="${BUILD_BASE}/deb-${arch}"
  rm -rf "$pkg_dir"
  cp -r "$PACKAGING_TEMPLATE" "$pkg_dir"

  sed -i "s/^Architecture:.*/Architecture: ${arch}/" "${pkg_dir}/DEBIAN/control"
  sed -i "s/^Version:.*/Version: ${VERSION}/" "${pkg_dir}/DEBIAN/control"

  mkdir -p "${pkg_dir}/usr/bin"
  $strip_tool -o "${pkg_dir}/usr/bin/esp-claw-desktop" "$src"
  chmod 755 "${pkg_dir}/usr/bin/esp-claw-desktop"

  cp crush-claw "${pkg_dir}/usr/bin/crush-claw"
  chmod 755 "${pkg_dir}/usr/bin/crush-claw"

  local installed_size
  installed_size=$(du -sk "${pkg_dir}/usr" | cut -f1)
  sed -i "s/^Installed-Size:.*/Installed-Size: ${installed_size}/" "${pkg_dir}/DEBIAN/control"

  dpkg-deb --build "${pkg_dir}" "${pkg_name}"
  echo "  Created: ${pkg_name}"
  echo ""
}

# ---- Build all targets ----
do_build "amd64" "amd64" "" "-O2 -m64" ""
do_build "i386"  "i386"  "${PWD}/cmake/toolchain-i686.cmake"    "-O2 -m32" "/usr/lib/i386-linux-gnu/pkgconfig"
do_build "arm64" "arm64" "${PWD}/cmake/toolchain-aarch64.cmake" "-O2"     "/usr/lib/aarch64-linux-gnu/pkgconfig"
do_build "armhf" "armhf" "${PWD}/cmake/toolchain-armhf.cmake"   "-O2"     "/usr/lib/arm-linux-gnueabihf/pkgconfig"

# ---- Package all targets ----
echo "=== Packaging ==="
echo ""
do_package "amd64" "strip"
do_package "i386"  "i686-linux-gnu-strip"
do_package "arm64" "aarch64-linux-gnu-strip"
do_package "armhf" "arm-linux-gnueabihf-strip"

# ---- Summary ----
echo "=== Build Summary ==="
for arch in amd64 i386 arm64 armhf; do
  bin="${BUILD_BASE}/${arch}/esp-claw-desktop"
  if [ -f "$bin" ]; then
    file "$bin"
    ls -lh "$bin"
  else
    echo "  ${arch}: FAILED (binary not found)"
  fi
done

echo ""
echo "=== Packages ==="
ls -lh crush-claw_*.deb 2>/dev/null
