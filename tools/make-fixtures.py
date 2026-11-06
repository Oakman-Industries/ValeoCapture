#!/usr/bin/env python3
# Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
"""Independent synthetic JPEG-LS fixtures. Modern host only; requires FFmpeg."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

if len(sys.argv) != 2:
    raise SystemExit('Usage: make-fixtures.py NEW_OUTPUT_DIRECTORY')
directory = Path(sys.argv[1])
directory.mkdir(exist_ok=False)
manifest = {}
for name in ['zero', 'maximum', 'ramp', 'random16']:
    values = []
    state = 0x12345678
    for i in range(4096):
        state = (1664525 * state + 1013904223) & 0xffffffff
        value = 0 if name == 'zero' else 65535 if name == 'maximum' else state >> 16 if name == 'random16' else (((i % 64 + i // 64) & 31) * 64)
        values.append(value)
    raw = struct.pack('<4096H', *values)
    encoded = subprocess.check_output(['ffmpeg', '-v', 'error', '-f', 'rawvideo',
        '-pixel_format', 'gray16le', '-video_size', '64x64', '-i', 'pipe:0',
        '-frames:v', '1', '-c:v', 'jpegls', '-f', 'image2pipe', 'pipe:1'], input=raw)
    if not encoded.startswith(bytes.fromhex('ffd8fff7')):
        raise SystemExit('FFmpeg did not emit expected JPEG-LS SOI/SOF55')
    (directory / (name + '.jls')).write_bytes(encoded)
    manifest[name] = {'raw_sha256': hashlib.sha256(raw).hexdigest(),
                      'jls_sha256': hashlib.sha256(encoded).hexdigest(), 'jls_bytes': len(encoded)}
(directory / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps(manifest, indent=2))
