# Third-party material

ValeoCapture's own code is CC BY-NC-SA 4.0, © 2026 Oakman Industries (see `../LICENSE`). The
material below keeps its own license.

## Apple FireWire ABI headers (`apple/`)

Unchanged public header declarations, needed to build the macOS 10.15–15 FireWire route. The macOS
SDK no longer ships `IOFireWireSBP2Lib.h`.

| File | Source commit |
| --- | --- |
| apple/IOKit/firewire/IOFireWireSBP2Lib.h | [IOFireWireSBP2, 5820f1c956e0bdfd148af1ad4f482bf1c932c579](https://github.com/apple-oss-distributions/IOFireWireSBP2/tree/5820f1c956e0bdfd148af1ad4f482bf1c932c579) |
| apple/IOKit/firewire/IOFireWireFamilyCommon.h | [IOFireWireFamily, 1f20e7a1366497e1cd9094890eb67c4a4c05fe87](https://github.com/apple-oss-distributions/IOFireWireFamily/tree/1f20e7a1366497e1cd9094890eb67c4a4c05fe87) |
| apple/IOKit/firewire/IOFireWireLib.h | Same IOFireWireFamily commit; SHA256 19ce967ac76016eda9fdfec49e99697d4e412c0de78ce08620f87db4a672c866 |

The header notices refer to the Apple Public Source License 1.1; `apple/APSL-1.1.txt` is that
license text. `apple/APPLE_LICENSE` is the license file supplied with the source repository. Only
declarations are used, to call the operating system's plug-in interfaces; no implementation code
is copied. If later work copies implementation code, reassess its license first.

## CharLS 2.4.5 (`charls/`)

Optional JPEG-LS component, fetched as source at build time and verified by CMake:

```text
https://codeload.github.com/team-charls/charls/tar.gz/refs/tags/2.4.5
SHA256 b55c742ef96b5e9d63f813a75d0ba29944302abb5a6ab59756ad61fa017b1dd3
```

Its unchanged BSD-3-Clause license is `charls/LICENSE.md`. Do not downgrade below 2.4.5: an earlier
version failed on a truncated test stream. The wrapper additionally rejects oversized,
wrong-geometry, multi-component, unsupported-depth and near-lossless data.

## Supplied by macOS

CoreGraphics, IOKit, CoreFoundation, Foundation and AppKit. libtiff is used only as an independent
reader in host tests and is not part of the app.
