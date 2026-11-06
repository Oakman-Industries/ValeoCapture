#!/bin/bash
# Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
set -euo pipefail
project_dir=$(cd "$(dirname "$0")/.." && pwd)
source_app=${1:-"$project_dir/build-functional-app/ValeoCapture.app"}
test_dir=$(mktemp -d "$project_dir/build-functional-app/service-smoke-XXXXXX")
test_app="$test_dir/ServiceSmoke.app"
ditto "$source_app" "$test_app"
mv "$test_app/Contents/MacOS/ValeoCapture" "$test_dir/original-app-executable"
/usr/libexec/PlistBuddy -c 'Set :CFBundleIdentifier org.valeocapture.ServiceSmoke' "$test_app/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleExecutable ServiceSmoke' "$test_app/Contents/Info.plist"
xcrun swiftc -swift-version 5 -O -framework Foundation \
    "$project_dir/app/ServiceProtocol.swift" "$project_dir/app/CameraCalibration.swift" \
    "$project_dir/tools/xpc_capture_smoke.swift" -o "$test_app/Contents/MacOS/ServiceSmoke"
codesign --force --sign - "$test_app"
codesign --verify --deep --strict "$test_app"
"$test_app/Contents/MacOS/ServiceSmoke"
