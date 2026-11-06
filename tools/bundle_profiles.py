#!/usr/bin/env python3
# Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
"""Modern-host-only, bounded import of operator-supplied ICCs into a local build.

Vendor bytes remain in ignored private/build output. No installer is executed.
The caller supplies an extraction manifest containing file/bytes/sha256 entries.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct

def read_regular(path, maximum):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(fd, 'rb') as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or not 0 <= info.st_size <= maximum:
            raise ValueError('Resource file type/size outside bounds')
        data = stream.read(maximum + 1)
        if len(data) != info.st_size: raise ValueError('Resource changed while reading')
        return data

def bundle(source, destination):
    source, destination = Path(source), Path(destination)
    rows = json.loads(read_regular(source / 'manifest.json', 128 * 1024))
    if not isinstance(rows, list) or not 1 <= len(rows) <= 128:
        raise ValueError('Expected a bounded resource manifest list')
    prepared, names, total = [], set(), 0
    for row in rows:
        if not isinstance(row, dict) or not isinstance(row.get('file'), str):
            raise ValueError('Malformed resource entry')
        name = row['file']
        if not name.lower().endswith(('.icc', '.icm')): continue
        if not re.fullmatch(r'[A-Za-z0-9 _().+\-]{1,124}\.(?:icc|icm)', name, re.IGNORECASE) or name.casefold() in names:
            raise ValueError('Unsafe or duplicate ICC name')
        if type(row.get('bytes')) is not int or not 128 <= row['bytes'] <= 16 * 1024 * 1024 or not re.fullmatch(r'[0-9a-f]{64}', row.get('sha256', '')):
            raise ValueError('Invalid resource length/hash')
        data = read_regular(source / name, 16 * 1024 * 1024)
        total += len(data)
        if total > 64 * 1024 * 1024 or len(data) != row['bytes'] or hashlib.sha256(data).hexdigest() != row['sha256']:
            raise ValueError('Resource size/hash mismatch')
        if struct.unpack_from('>I', data)[0] != len(data) or data[36:40] != b'acsp' or data[16:20] != b'RGB ':
            raise ValueError('Expected a bounded RGB ICC resource')
        names.add(name.casefold());prepared.append((name, data, row['sha256']))
    if not prepared: raise ValueError('No ICC resources selected')
    # All inputs are validated and owned before any bundle file is created.
    destination.mkdir(mode=0o700, exist_ok=False)
    manifest = {'schema': 1, 'profiles': []}
    for name, data, digest in sorted(prepared):
        fd = os.open(destination / name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
        with os.fdopen(fd, 'wb') as stream: stream.write(data)
        manifest['profiles'].append({'file': name, 'name': Path(name).stem, 'sha256': digest})
    fd = os.open(destination / 'manifest.json', os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'w') as stream: json.dump(manifest, stream, indent=2);stream.write('\n')
    return len(prepared)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('private_resource_directory');parser.add_argument('new_bundle_directory')
    args = parser.parse_args()
    print('Bundled %d verified local ICC resources' % bundle(args.private_resource_directory, args.new_bundle_directory))
