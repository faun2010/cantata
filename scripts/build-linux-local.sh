#!/bin/bash
# Build Cantata locally on Linux (Ubuntu). Nothing is installed into system directories.
# Options follow docs/linux-local-build.md: no Qt Multimedia/Qt Multimedia dev packages,
# no KF6, so HTTP stream playback is off and the vendored K* copies are compiled.
set -euo pipefail

run_app=false
case "${1:-}" in
    --run) run_app=true ;;
    --help|-h)
        echo "Usage: scripts/build-linux-local.sh [--run]"
        echo "Build cantata in the project-local build directory; nothing is installed."
        echo "Use --run to start the freshly built cantata right after a successful build."
        exit 0 ;;
    "") ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
esac
if [[ $# -gt 1 ]]; then
    echo "Expected at most one option; use --help." >&2
    exit 2
fi

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${CANTATA_BUILD_DIR:-$project_dir/build}"

cmake -S "$project_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_HTTP_STREAM_PLAYBACK=OFF \
    -DBUNDLED_KCATEGORIZEDVIEW=ON \
    -DBUNDLED_KARCHIVE=ON
cmake --build "$build_dir" --parallel "${CANTATA_BUILD_JOBS:-$(nproc)}"

# Headless sanity check: --version exits before any GUI setup.
QT_QPA_PLATFORM=offscreen "$build_dir/cantata" --version
echo "Build ready: $build_dir/cantata"
echo "Run it with: $build_dir/cantata"
if [[ "$run_app" == true ]]; then
    exec "$build_dir/cantata"
fi
