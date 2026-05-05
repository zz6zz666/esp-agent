#!/usr/bin/env bash
#
# _run_desktop.sh — Quick build-and-run helper (dev convenience)
#
# For production use, prefer: ./crush-claw start
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

case "${1:-run}" in
    build)
        ./crush-claw build
        ;;
    run)
        echo "=== Quick run (foreground) ==="
        echo "For daemon mode, use: ./crush-claw start"
        echo ""
        exec ./build/esp-claw-desktop
        ;;
    clean)
        ./crush-claw clean
        ;;
    *)
        echo "Usage: $0 {build|run|clean}"
        exit 1
        ;;
esac
