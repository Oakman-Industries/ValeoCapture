#!/usr/bin/env python3
# Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
"""Copy reviewed, private camera metadata as owned bytes; no vendor fixture here."""
import json
import math
import os
from pathlib import Path
import stat
import sys


def validate(data):
    if len(data) > 32768:
        raise ValueError('Calibration exceeds 32 KiB')
    v = json.loads(data)
    if not isinstance(v, dict) or type(v.get('schema')) is not int or v['schema'] != 1:
        raise ValueError('Unknown calibration schema')
    if v.get('cameraType') != 'VA11A/5/C' or v.get('correction') != 'paired-dark-development':
        raise ValueError('Unreviewed camera/correction kind')
    serial = v.get('bodySerial')
    if not isinstance(serial, str) or not 1 <= len(serial) <= 64 or any(ord(c) < 32 or ord(c) > 126 for c in serial):
        raise ValueError('A specific back serial is required')
    if type(v.get('whiteLevel')) is not int or not 0 < v['whiteLevel'] <= 16383:
        raise ValueError('Invalid white level')
    for key, count in [('xyzToCamera', 9), ('asShotNeutral', 3)]:
        a = v.get(key)
        if not isinstance(a, list) or len(a) != count or any(type(x) not in (int, float) or not math.isfinite(x) or abs(x) > 1000 for x in a):
            raise ValueError('Invalid ' + key)
    if any(not 0 < x <= 1 for x in v['asShotNeutral']):
        raise ValueError('Invalid neutral')
    c = [round(x * 1000000) / 1000000 for x in v['xyzToCamera']]
    det = c[0] * (c[4] * c[8] - c[5] * c[7]) - c[1] * (c[3] * c[8] - c[5] * c[6]) + c[2] * (c[3] * c[7] - c[4] * c[6])
    if abs(det) < 1e-10:
        raise ValueError('Missing or singular color matrix')


def main():
    source, target = map(Path, sys.argv[1:])
    fd = os.open(source, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(fd, 'rb') as stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise ValueError('Calibration source must be a regular file')
        data = stream.read(32769)
    validate(data)
    fd = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as out:
        out.write(data)
    print('Bundled reviewed private calibration (serial not printed)')


if __name__ == '__main__':
    main()
