#!/bin/bash
# Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
# Modern host only. Creates a new read-only DMG; never installs a driver/kext.
set -euo pipefail
project_dir=$(cd "$(dirname "$0")/.." && pwd)
app_dir="$project_dir/build-functional-app/ValeoCapture.app"
if [ "$#" -gt 0 ]; then
    if [ "$#" -ne 2 ] || [ "$1" != "--app" ]; then echo "Usage: scripts/package-app.sh [--app FUNCTIONAL_APP]" >&2; exit 2; fi
    app_dir=$2
fi
codesign --verify --deep --strict "$app_dir"
xcrun lipo "$app_dir/Contents/MacOS/ValeoCapture" -verify_arch x86_64 arm64
xcrun lipo "$app_dir/Contents/XPCServices/CaptureService.xpc/Contents/MacOS/CaptureService" -verify_arch x86_64 arm64
python3 - "$app_dir" "$project_dir" <<'PY'
import importlib.util, json, pathlib, sys
app, root = map(pathlib.Path, sys.argv[1:])
resources = app / 'Contents/Resources'
config = json.loads((resources / 'BuildConfig.json').read_bytes())
if config.get('captureEnabled') is not True:
    raise SystemExit('Refusing to package the disabled development preview as a functional app')
if config.get('isoMode') != 'software':
    raise SystemExit('Rebuild the current software-gain app before packaging')
spec = importlib.util.spec_from_file_location('calibration', root / 'tools/bundle_calibration.py')
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
module.validate((resources / 'LeafCalibration.json').read_bytes())
bank = json.loads((resources / 'LeafProfiles/manifest.json').read_bytes())
if bank.get('schema') != 1 or not bank.get('profiles'):
    raise SystemExit('Missing bundled Leaf profiles')
PY
package_dir="$project_dir/build-package"
mkdir -p "$package_dir"
staging=$(mktemp -d "$package_dir/staging-XXXXXX")
ditto "$app_dir" "$staging/ValeoCapture.app"
python3 - "$staging/ValeoCapture.app/Contents/Resources" <<'PY'
import json, pathlib, sys
r = pathlib.Path(sys.argv[1]); bank = r / 'LeafProfiles'
manifest = json.loads((bank / 'manifest.json').read_bytes())
bank.chmod(0o755)
for row in manifest['profiles']:
    name = row['file']
    if not isinstance(name, str) or pathlib.Path(name).name != name:
        raise SystemExit('Unsafe packaged preset filename')
    f = bank / name
    if f.is_symlink() or not f.is_file():
        raise SystemExit('Packaged preset is not a regular file')
    f.chmod(0o644)
for f in [bank / 'manifest.json', r / 'LeafCalibration.json']:
    if f.is_symlink(): raise SystemExit('Unsafe packaged metadata symlink')
    f.chmod(0o644)
PY
ln -s /Applications "$staging/Applications"
cp "$project_dir/docs/INSTALL.md" "$staging/Read Me.md"
codesign --verify --deep --strict "$staging/ValeoCapture.app"
version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app_dir/Contents/Info.plist")
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then echo "Invalid package version" >&2; exit 2; fi
output="$package_dir/ValeoCapture-$version-development-$(date -u +%Y%m%dT%H%M%SZ).dmg"
if [ -e "$output" ]; then echo "Preserve existing package; choose a new output time" >&2; exit 1; fi
hdiutil create -volname ValeoCapture -srcfolder "$staging" -fs HFS+ -format UDZO "$output"
hdiutil verify "$output"
shasum -a 256 "$output" > "$output.sha256"
printf 'Created installable development package: %s\n' "$output"
