#!/usr/bin/env bash
#
# _package_linglong.sh — Build Crush Claw 如意玲珑 (Linglong) package
#
# Prerequisites: ll-builder (linglong-builder), linglong CLI tools
# Output: io.github.zz6zz666.crush-claw_<version>_<arch>_<runtime>.layer
#
# Install linglong:
#   # Deepin / UOS
#   sudo apt install linglong-builder linglong-bin
#   # Or download from: https://linglong.dev/guide/start/install.html
#
set -euo pipefail
cd "$(dirname "$0")"

VERSION="1.1.0"
MANIFEST="linglong/linglong.yaml"
APP_ID="io.github.zz6zz666.crush-claw"

echo "=== Crush Claw 如意玲珑 (Linglong) Builder v${VERSION} ==="
echo ""

# Check prerequisites
if ! command -v ll-builder &>/dev/null; then
  echo "ERROR: ll-builder not found."
  echo "Install with: sudo apt install linglong-builder linglong-bin"
  echo "Or visit: https://linglong.dev/guide/start/install.html"
  exit 1
fi

# Check linglong service
if ! systemctl --user is-active linglong-upgrade-service &>/dev/null 2>&1; then
  echo "Starting linglong service..."
  systemctl --user start linglong-upgrade-service 2>/dev/null || true
  sleep 2
fi

echo "[1/3] Building linglong package..."
# ll-builder build reads linglong.yaml from current directory
# We copy the manifest and build from there
ll-builder build \
  --file "${MANIFEST}" \
  --skip-fetch-source \
  --skip-run-container 2>&1 | tail -20

echo "[2/3] Exporting linglong layer..."
ll-builder export --file "${MANIFEST}" 2>&1 | tail -10

echo "[3/3] Finding output..."
# Linglong outputs to linglong/output/ or current directory
find . -maxdepth 3 -name "${APP_ID}*" -type f 2>/dev/null | while read f; do
  echo "  Found: $f"
  ls -lh "$f"
done

echo ""
echo "=== Done ==="
echo ""
echo "Install with:"
echo "  ll-cli install ./${APP_ID}_*.layer"
echo ""
echo "Run with:"
echo "  ll-cli run ${APP_ID}"
