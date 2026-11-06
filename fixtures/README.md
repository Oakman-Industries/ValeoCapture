# Synthetic test fixtures

Four 64 × 64, unsigned 16-bit single-plane patterns generated from arithmetic and then JPEG-LS
encoded by an independent encoder. They are not camera payloads or photographs. `manifest.json`
records the little-endian source-array and encoded hashes. The C++ tests compute every expected
sample independently and wrap each stream in both four-byte and eight-byte length envelopes.

To regenerate into a **new** directory (the script uses the `ffmpeg` command-line encoder):

```sh
python3 tools/make-fixtures.py build/regenerated-fixtures
```

Never overwrite the checked-in fixtures just to make a test pass; review hashes and decoding and
record encoder-version differences when changing them. These synthetic streams do not prove real
gain-map packing, scale, dimensions or calibration order.
