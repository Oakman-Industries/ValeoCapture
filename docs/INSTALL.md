# Installing a ValeoCapture development build

Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0.

Drag **ValeoCapture** into **Applications**. The app contains its capture service, transport,
licenses and — in an owner-built functional package — that owner's own Leaf presets and
per-back calibration. No Java runtime or vendor software is required.

This is a locally ad-hoc signed development build, not Developer ID-signed or notarized. If macOS
asks for approval, use its normal per-app Open / Privacy & Security controls. Do not disable
Gatekeeper to run it.

## FireWire on your Mac

- **macOS 10.15.7–15:** ValeoCapture uses macOS's own FireWire stack. There is nothing extra to
  install. Hardware-tested on Intel macOS 15.7.9; other versions are build targets only.
- **macOS 26 and later:** Apple's FireWire stack is gone. ValeoCapture uses the SCSI device
  published by the ASFW DriverKit driver. The Valeo 11 needs ASFW changes that are under review
  ([ASFW #254–#257](https://github.com/mrmidi/ASFireWire/pulls?q=is%3Apr+author%3AOakman-Dev)) and
  not yet in a released ASFW build; until they are, this route needs a development ASFW build,
  which requires System Integrity Protection to be disabled. Hardware-tested on Apple Silicon
  macOS 26.7.1.

The colour metadata in a functional build is matched to one specific Valeo 11; another back needs
its own calibration. No firmware update is included or performed.

## Using it

Close other camera software. Connect and power the Valeo 11, open ValeoCapture and press **Check
Connection**. Choose the capture folder, software gain (1×/2×/4×/8×, equivalent to ISO
25/50/100/200), orientation and Leaf preset. Press **Start Listening**, then fire the camera's
shutter. The tested Hasselblad 501CM uses the lens-to-back sync cable; the app does not move a
mechanical shutter.

On the Valeo 11 the higher ISO settings are digital amplification of the base ISO 25 signal. The
app does not program analogue gain or add noise. A gain change during a transfer applies to the
next image; Stop is not needed.

Each exposure keeps the original light frame and a fresh dark frame with their status records,
exports an editable DNG and optionally a 16-bit TIFF rendered through the selected preset. The DNG
keeps the unamplified corrected mosaic and records the gain as BaselineExposure (0/+1/+2/+3 EV);
the TIFF applies the gain before the preset and embeds the actual output sRGB profile. Keep the raw
planes: factory gain/defect equivalence is still being verified. Timing details are saved beside
each capture.

**Stop** finishes an in-flight image and its export before closing; wait for the stopped state
before disconnecting. A frame already pending when you connect is preserved rather than
overwritten. Files are never overwritten.

## Uninstalling

Stop, quit, and delete ValeoCapture.app. Photographs, capture folders and imported presets and
preferences stay where they are. ValeoCapture installs no system driver, startup item or
privileged helper. (An ASFW driver, if you installed one for macOS 26, is removed with ASFW's own
app.)
