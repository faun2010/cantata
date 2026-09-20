#!/bin/bash
# Build Cantata locally using the project's private macOS build toolchain.
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

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
    --help|-h)
        echo "Usage: $0 [--package]"
        echo "Build and verify the local Cantata.app."
        echo "Use --package to also create the DMG."
        ;;
    *)
        echo "Unknown option: $1 (use --help)" >&2
        exit 2
        ;;
esac
