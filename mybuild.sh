#!/bin/bash
# One-click local build: auto-detects the platform and dispatches to the
# matching build script.
#   macOS (Darwin): scripts/build-macos-local.sh -> dist/Cantata.app (+ optional DMG)
#   Linux (Ubuntu): scripts/build-linux-local.sh -> build/cantata (nothing installed)
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
platform="$(uname -s)"

usage() {
    echo "Usage: $0 [--package | --run]"
    echo "macOS  (default): build and verify the local Cantata.app."
    echo "  --package  also create the DMG."
    echo "Linux  (default): build ./build/cantata (Ubuntu; nothing installed)."
    echo "  --run      start cantata after a successful build."
}

case "$platform" in
    Darwin)
        case "${1:-}" in
            "")
                # Local compilation is the default; DMG packaging is opt-in.
                exec "$project_dir/scripts/build-macos-local.sh" --app-only
                ;;
            --package)
                shift
                if [[ $# -gt 0 ]]; then
                    echo "Usage: $0 [--package]" >&2
                    exit 2
                fi
                exec "$project_dir/scripts/build-macos-local.sh"
                ;;
            --run)
                echo "--run is only supported on Linux" >&2
                exit 2
                ;;
            --help|-h)
                usage
                ;;
            *)
                echo "Unknown option: $1 (use --help)" >&2
                exit 2
                ;;
        esac
        ;;
    Linux)
        case "${1:-}" in
            "")
                # Local build into build/ is the default; run is opt-in.
                exec "$project_dir/scripts/build-linux-local.sh"
                ;;
            --run)
                shift
                if [[ $# -gt 0 ]]; then
                    echo "Usage: $0 [--run]" >&2
                    exit 2
                fi
                exec "$project_dir/scripts/build-linux-local.sh" --run
                ;;
            --package)
                echo "--package is only supported on macOS" >&2
                exit 2
                ;;
            --help|-h)
                usage
                ;;
            *)
                echo "Unknown option: $1 (use --help)" >&2
                exit 2
                ;;
        esac
        ;;
    *)
        echo "Unsupported platform: $platform (expected Darwin or Linux)" >&2
        exit 1
        ;;
esac
