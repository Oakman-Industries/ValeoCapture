# Building and testing ValeoCapture

Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0.

Needs a macOS development host with the Xcode command-line tools, CMake and Ninja. Target minimums
are macOS 10.15.7 (Intel) and 11.0 (Apple Silicon); building for a target is not proof that it
works on that OS or hardware.

## Core library, CLI and tests

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

The first build fetches a SHA256-pinned CharLS source archive (see `third_party/README.md`). For
offline rebuilds keep the populated build tree, or point CMake's `FETCHCONTENT_SOURCE_DIR_CHARLS`
at a verified local copy. Python 3 and libtiff enable additional host-only tests; libtiff is an
independent test reader, not an app dependency.

Memory and undefined-behaviour checks:

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DVALEO_SANITIZERS=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

All tests use synthetic fixtures. None needs a camera, a vendor file or private data.

## App

```sh
scripts/build-app.sh
build-app/ValeoCapture.app/Contents/MacOS/ValeoCapture --self-test-xpc
```

This builds a universal (x86_64 + arm64), ad-hoc signed `ValeoCapture.app` with its embedded
capture service, plus a universal `valeoctl`. The default build keeps **all camera commands
disabled** and bundles no vendor resources.

To regenerate the README screenshot (`assets/showcase.png`) from the real window — offscreen, with a
generated test chart instead of a photograph and without touching the camera or preferences:

```sh
build-app/ValeoCapture.app/Contents/MacOS/ValeoCapture --render-showcase /tmp/showcase.png
sips -Z 1800 /tmp/showcase.png --out assets/showcase.png
```

### Functional build with your own resources

```sh
scripts/build-app.sh --enable-capture \
  --leaf-profiles /absolute/path/to/your/presets \
  --leaf-calibration /absolute/path/to/your/calibration.json
```

Output goes to `build-functional-app/`, separate from the default build. Presets are your own
Leaf ICC files; each is validated before it is copied into the build. The calibration file is
validated by `tools/bundle_calibration.py` and must look like:

```json
{
  "schema": 1,
  "cameraType": "VA11A/5/C",
  "correction": "paired-dark-development",
  "bodySerial": "<exactly as your back reports it, including any trailing padding>",
  "whiteLevel": 15000,
  "xyzToCamera": [9 finite numbers, non-singular],
  "asShotNeutral": [3 numbers in (0, 1]]
}
```

The app accepts a back only when its reported serial matches `bodySerial` exactly. There is no
public tool yet to derive the colour matrix and neutral for your back; that is an open problem
(see CONTRIBUTING).

`scripts/package-app.sh` wraps a functional build into a DMG for your own use. Packages that
contain your presets or calibration are personal builds — do not publish them.

## Command-line tool

```sh
build/valeoctl list                         # passive discovery
build/valeoctl fingerprint-check FILE.dng   # verify the provenance mark (offline)
build/valeoctl check-raw FRAME.vraw         # validate a saved raw plane (offline)
```

Camera commands (`probe`, `identity`, `diagnostic-status`) need a build configured with
`-DVALEO_EXPERIMENTAL_READS=ON`, which the functional app build enables. Quit the app before
using them, and never rebuild or replace a running app or capture service.
