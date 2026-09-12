#!/bin/bash
# Build using an unpacked Qt SDK. Nothing is installed into system directories.
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
qt_dir="${CANTATA_QT_DIR:-$project_dir/build-deps/qt/6.10.3/macos}"
build_dir="${CANTATA_BUILD_DIR:-$project_dir/build-macos}"
output_dir="${CANTATA_OUTPUT_DIR:-$project_dir/dist}"
xcode_dir="${CANTATA_XCODE_DIR:-/Applications/Xcode.app/Contents/Developer}"
taglib_dir="${CANTATA_TAGLIB_DIR:-/opt/homebrew/opt/taglib}"

if [[ ! -x "$qt_dir/bin/macdeployqt" ]]; then
    echo "Set CANTATA_QT_DIR to an unpacked Qt 6 SDK containing macdeployqt." >&2
    exit 1
fi

cmake -S "$project_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$xcode_dir/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang" \
    -DCMAKE_CXX_COMPILER="$xcode_dir/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++" \
    -DCMAKE_OSX_SYSROOT="$xcode_dir/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DCMAKE_PREFIX_PATH="$qt_dir;$taglib_dir" \
    -DENABLE_AVAHI=OFF -DENABLE_FFMPEG=OFF -DENABLE_MPG123=OFF \
    -DENABLE_DEVICES_SUPPORT=OFF \
    -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF
cmake --build "$build_dir" --parallel "${CANTATA_BUILD_JOBS:-8}"
cmake --install "$build_dir" --prefix "$output_dir"
python3 "$project_dir/scripts/check-macos-bundle.py" "$output_dir/Cantata.app"
codesign --verify --deep --strict "$output_dir/Cantata.app"
echo "App ready: $output_dir/Cantata.app"
