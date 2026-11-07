#!/bin/bash
# Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
# Modern build host only. Never installs an extension or changes boot policy.
set -euo pipefail
project_dir=$(cd "$(dirname "$0")/.." && pwd)
profiles_dir=""
calibration_file=""
capture_mode=OFF
while [ "$#" -gt 0 ]; do
    case "$1" in
        --leaf-profiles) [ "$#" -ge 2 ] || exit 2; profiles_dir=$2; shift 2 ;;
        --leaf-calibration) [ "$#" -ge 2 ] || exit 2; calibration_file=$2; shift 2 ;;
        --enable-capture) capture_mode=ON; shift ;;
        *) echo "Usage: scripts/build-app.sh [--leaf-profiles PRIVATE_BANK] [--leaf-calibration PRIVATE_JSON] [--enable-capture]" >&2; exit 2 ;;
    esac
done
if [ "$capture_mode" = ON ] && { [ -z "$profiles_dir" ] || [ -z "$calibration_file" ]; }; then
    echo "Functional development build requires reviewed Leaf profiles and camera calibration" >&2
    exit 2
fi
output_dir="$project_dir/build-app"
if [ "$capture_mode" = ON ]; then output_dir="$project_dir/build-functional-app"; fi
mkdir -p "$output_dir"
app_dir="$output_dir/ValeoCapture.app"
service_dir="$app_dir/Contents/XPCServices/CaptureService.xpc"
python3 - "$app_dir" <<'PY'
import subprocess, sys
prefix = sys.argv[1] + '/Contents/'
if any(line.startswith(prefix) for line in subprocess.check_output(['ps', '-axo', 'command='], text=True).splitlines()):
    raise SystemExit('Quit this app and its capture service normally before rebuilding loaded executables')
PY
mkdir -p "$app_dir/Contents/MacOS" "$app_dir/Contents/Resources" "$service_dir/Contents/MacOS"
cp "$project_dir/app/Info.plist" "$app_dir/Contents/Info.plist"
cp "$project_dir/app/service/Info.plist" "$service_dir/Contents/Info.plist"
bash "$project_dir/scripts/build-icon.sh" "$app_dir/Contents/Resources/AppIcon.icns"
for architecture in x86_64 arm64; do
    minimum=10.15.7
    if [ "$architecture" = arm64 ]; then minimum=11.0; fi
    slice_dir="$output_dir/$architecture"
    cmake -S "$project_dir" -B "$slice_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES="$architecture" -DCMAKE_OSX_DEPLOYMENT_TARGET="$minimum" \
        -DVALEO_BUILD_TESTS=OFF -DVALEO_WITH_JPEGLS=ON -DVALEO_EXPERIMENTAL_READS="$capture_mode" -DVALEO_EXPERIMENTAL_CAPTURE="$capture_mode" -DVALEO_EXPERIMENTAL_AUXILIARY="$capture_mode"
    cmake --build "$slice_dir"
    target="$architecture-apple-macosx$minimum"
    xcrun swiftc -swift-version 5 -O -target "$target" -framework AppKit \
        "$project_dir/app/ServiceProtocol.swift" "$project_dir/app/CameraCalibration.swift" "$project_dir/app/LaunchSplash.swift" "$project_dir/app/main.swift" -o "$slice_dir/ValeoCapture"
    xcrun swiftc -swift-version 5 -O -target "$target" -framework Foundation \
        -import-objc-header "$project_dir/platform/macos/bridge.h" \
        "$project_dir/app/ServiceProtocol.swift" "$project_dir/app/CameraCalibration.swift" "$project_dir/app/service/SessionLifetime.swift" "$project_dir/app/service/main.swift" \
        "$slice_dir/libvaleo_bridge.a" "$slice_dir/libvaleo_session.a" "$slice_dir/libvaleo_host.a" "$slice_dir/libvaleo_legacy.a" "$slice_dir/libvaleo_scsi.a" "$slice_dir/libvaleo_color.a" "$slice_dir/libvaleo_core.a" \
        -lc++ -framework IOKit -framework CoreFoundation -framework CoreGraphics -o "$slice_dir/CaptureService"
done
xcrun lipo -create "$output_dir/x86_64/ValeoCapture" "$output_dir/arm64/ValeoCapture" -output "$app_dir/Contents/MacOS/ValeoCapture"
xcrun lipo -create "$output_dir/x86_64/CaptureService" "$output_dir/arm64/CaptureService" -output "$service_dir/Contents/MacOS/CaptureService"
xcrun lipo -create "$output_dir/x86_64/valeoctl" "$output_dir/arm64/valeoctl" -output "$output_dir/valeoctl"
cp "$project_dir/LICENSE" "$project_dir/NOTICE" "$app_dir/Contents/Resources/"
mkdir -p "$app_dir/Contents/Resources/Licenses"
cp "$project_dir/third_party/apple/APPLE_LICENSE" "$project_dir/third_party/apple/APSL-1.1.txt" \
    "$project_dir/third_party/charls/LICENSE.md" "$app_dir/Contents/Resources/Licenses/"
profiles_output="$app_dir/Contents/Resources/LeafProfiles"
if [ -e "$profiles_output" ] || [ -L "$profiles_output" ]; then
    # Keep the previous generated bank outside the new app, so a default build
    # never silently ships vendor resources left by a private build.
    previous_profiles=$(mktemp -d "$output_dir/leaf-profiles-previous-XXXXXX")
    mv "$profiles_output" "$previous_profiles/LeafProfiles"
fi
if [ -n "$profiles_dir" ]; then
    python3 "$project_dir/tools/bundle_profiles.py" "$profiles_dir" "$profiles_output"
fi
calibration_output="$app_dir/Contents/Resources/LeafCalibration.json"
if [ -e "$calibration_output" ] || [ -L "$calibration_output" ]; then
    previous_calibration=$(mktemp -d "$output_dir/calibration-previous-XXXXXX")
    mv "$calibration_output" "$previous_calibration/LeafCalibration.json"
fi
if [ -n "$calibration_file" ]; then
    python3 "$project_dir/tools/bundle_calibration.py" "$calibration_file" "$calibration_output"
fi
python3 - "$app_dir/Contents/Resources/BuildConfig.json" "$capture_mode" <<'PY'
import json, sys
with open(sys.argv[1], 'w') as out:
    json.dump({'schema': 1, 'captureEnabled': sys.argv[2] == 'ON',
               'intelMinimum': '10.15.7', 'armMinimum': '11.0',
               'isoMode': 'software', 'softwareISOValues': [25, 50, 100, 200],
               'correction': 'paired-dark-development'}, out)
    out.write('\n')
PY
codesign --force --sign - "$service_dir"
codesign --force --sign - "$app_dir"
codesign --verify --deep --strict "$app_dir"
printf 'Built local ad-hoc signed development app: %s\n' "$app_dir"
