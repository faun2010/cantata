#!/bin/bash
# Package an existing self-contained app using only macOS deployment tools.
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
if [[ "${1:-}" == --help || "${1:-}" == -h ]]; then
    echo "Usage: scripts/package-macos-dmg.sh [Cantata.app] [output.dmg]"
    echo "Defaults: dist/Cantata.app and Cantata-VERSION-ARCH.dmg beside the app."
    exit 0
fi
if [[ $# -gt 2 ]]; then
    echo "Expected an app path and optional DMG path; use --help." >&2
    exit 2
fi

app_path="${1:-${CANTATA_OUTPUT_DIR:-$project_dir/dist}/Cantata.app}"
if [[ ! -d "$app_path/Contents/MacOS" || ! -f "$app_path/Contents/Info.plist" ]]; then
    echo "App bundle not found: $app_path" >&2
    exit 1
fi
app_path="$(cd "$app_path" && pwd -P)"
version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app_path/Contents/Info.plist")"
executable="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$app_path/Contents/Info.plist")"
architecture="$(lipo -archs "$app_path/Contents/MacOS/$executable" | tr ' ' '-')"
if [[ ! "$version" =~ ^[0-9][0-9A-Za-z.+-]*$ || ! "$architecture" =~ ^[A-Za-z0-9_-]+$ ]]; then
    echo "Invalid bundle version or architecture." >&2
    exit 1
fi
dmg_path="${2:-$(dirname "$app_path")/Cantata-$version-$architecture.dmg}"
if [[ "$dmg_path" != *.dmg ]]; then
    echo "Output must end in .dmg: $dmg_path" >&2
    exit 2
fi
mkdir -p "$(dirname "$dmg_path")"
dmg_path="$(cd "$(dirname "$dmg_path")" && pwd -P)/$(basename "$dmg_path")"
case "$dmg_path" in
    "$app_path"/*) echo "DMG output must be outside the app bundle." >&2; exit 2 ;;
esac

python3 "$project_dir/scripts/check-macos-bundle.py" "$app_path"
codesign --verify --deep --strict "$app_path"

build_dir="${CANTATA_BUILD_DIR:-$project_dir/build-macos}"
mkdir -p "$build_dir"
stage_dir="$(mktemp -d "$build_dir/dmg.XXXXXX")"
stage_dir="$(cd "$stage_dir" && pwd -P)"
echo "DMG staging directory (retained for manual cleanup): $stage_dir"
mkdir "$stage_dir/contents"
ditto "$app_path" "$stage_dir/contents/Cantata.app"
ln -s /Applications "$stage_dir/contents/Applications"

hdiutil create -volname Cantata -srcfolder "$stage_dir/contents" \
    -fs HFS+ -format UDZO -imagekey zlib-level=9 "$stage_dir/Cantata.dmg"
hdiutil verify "$stage_dir/Cantata.dmg"
# Keep the previous image until the user explicitly runs the cleanup command.
if [[ -e "$dmg_path" ]]; then
    mv "$dmg_path" "$stage_dir/previous.dmg"
fi
mv "$stage_dir/Cantata.dmg" "$dmg_path"
echo "DMG ready: $dmg_path"
echo "Manual cleanup preview: python3 $project_dir/scripts/clean-macos-local.py"
