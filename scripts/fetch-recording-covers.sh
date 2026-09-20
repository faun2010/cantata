#!/bin/sh
# Uses the packaged Qt downloader, proxy settings and cache; no Python needed.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cantata_bin=${CANTATA_BIN:-"$project_dir/dist/Cantata.app/Contents/MacOS/Cantata"}
if [ ! -x "$cantata_bin" ]; then
    echo "Build dist/Cantata.app first, or set CANTATA_BIN to your Cantata executable." >&2
    exit 2
fi
exec "$cantata_bin" --fetch-recording-covers "$@"
