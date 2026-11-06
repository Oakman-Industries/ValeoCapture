#!/bin/bash
# Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
# Raster resampling and macOS container generation only; source artwork is kept.
set -euo pipefail
project_dir=$(cd "$(dirname "$0")/.." && pwd)
if [ "$#" -ne 1 ]; then echo "Usage: scripts/build-icon.sh OUTPUT.icns" >&2; exit 2; fi
temporary=$(mktemp -d "${TMPDIR:-/tmp}/valeo-icon-XXXXXX")
trap 'rm -rf "$temporary"' EXIT
iconset="$temporary/AppIcon.iconset"
mkdir "$iconset"
for size in 16 32 128 256 512; do
    sips -z "$size" "$size" "$project_dir/assets/AppIcon.png" --out "$iconset/icon_${size}x${size}.png" >/dev/null
    retina=$((size * 2))
    sips -z "$retina" "$retina" "$project_dir/assets/AppIcon.png" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$1"
