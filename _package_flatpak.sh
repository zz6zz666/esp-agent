#!/usr/bin/env bash
#
# _package_flatpak.sh — Build Crush Claw Flatpak package
#
# Prerequisites: flatpak, flatpak-builder, org.freedesktop.Sdk//23.08
# Output: crush-claw.flatpak (exported as a Flatpak bundle or repo)
#
set -euo pipefail
cd "$(dirname "$0")"

VERSION="1.1.0"
MANIFEST="flatpak/io.github.zz6zz666.crush-claw.yml"
APP_ID="io.github.zz6zz666.crush-claw"
BUILD_DIR="build-cross/flatpak-build"
REPO_DIR="build-cross/flatpak-repo"
BUNDLE="crush-claw-${VERSION}.flatpak"

echo "=== Crush Claw Flatpak Builder v${VERSION} ==="
echo ""

# Check prerequisites
if ! command -v flatpak-builder &>/dev/null; then
  echo "ERROR: flatpak-builder not found. Install with: sudo apt install flatpak-builder"
  exit 1
fi

# Ensure freedesktop SDK is available (user mode)
echo "[1/4] Checking Flatpak SDK..."
SDK="org.freedesktop.Sdk//23.08"
if ! flatpak list --user --runtime 2>/dev/null | grep -q "org.freedesktop.Sdk"; then
  echo "  Installing ${SDK} (user mode)..."
  # Check/add flathub remote (user mode, China mirror for speed)
  if ! flatpak remotes --user 2>/dev/null | grep -q flathub-cn; then
    flatpak remote-add --user --no-gpg-verify --if-not-exists flathub-cn https://mirror.sjtu.edu.cn/flathub
  fi
  flatpak install --user -y flathub-cn "${SDK}" 2>&1 || {
    echo "  Mirror failed, trying official flathub..."
    if ! flatpak remotes --user 2>/dev/null | grep -q flathub; then
      flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
    fi
    flatpak install --user -y flathub "${SDK}" 2>&1 || {
      echo "  ERROR: Cannot install Flatpak SDK. Run: flatpak install flathub ${SDK}"
      exit 1
    }
  }
  echo "  SDK installed."
fi

# Build
echo "[2/4] Building Flatpak..."
rm -rf "${BUILD_DIR}" "${REPO_DIR}"

flatpak-builder \
  --user \
  --force-clean \
  --install-deps-from=flathub-cn \
  --repo="${REPO_DIR}" \
  "${BUILD_DIR}" \
  "${MANIFEST}" 2>&1 | tail -30

# Create bundle
echo "[3/4] Creating Flatpak bundle..."
flatpak build-bundle "${REPO_DIR}" "${BUNDLE}" "${APP_ID}" stable 2>&1

echo "[4/4] Done."
echo ""
echo "=== Done ==="
ls -lh "${BUNDLE}" 2>/dev/null || echo "Bundle: ${BUNDLE}"
echo ""
echo "Install with:"
echo "  flatpak install --user ./${BUNDLE}"
echo ""
echo "Run with:"
echo "  flatpak run ${APP_ID}"
