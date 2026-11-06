#!/usr/bin/env python3
# Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
"""Test the untrusted resource-manifest/filesystem boundary, not ICC color math."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('bundle_profiles', Path(__file__).resolve().parents[1] / 'tools/bundle_profiles.py')
module = importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

class ResourceBoundary(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory();self.root = Path(self.temporary.name)
        self.source = self.root / 'source';self.source.mkdir()
        self.output = self.root / 'new-bank'
        data = bytearray(132);struct.pack_into('>I', data, 0, len(data));data[16:20] = b'RGB ';data[36:40] = b'acsp'
        self.data = bytes(data);(self.source / 'Preset.ICC').write_bytes(self.data)
        self.row = {'file': 'Preset.ICC', 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
        self.manifest([self.row])
    def tearDown(self): self.temporary.cleanup()
    def manifest(self, rows): (self.source / 'manifest.json').write_text(json.dumps(rows))
    def reject(self):
        with self.assertRaises((ValueError, OSError)): module.bundle(self.source, self.output)
        self.assertFalse(self.output.exists())
    def test_owned_bytes_and_private_output(self):
        self.assertEqual(module.bundle(self.source, self.output), 1)
        (self.source / 'Preset.ICC').write_bytes(b'changed')
        self.assertEqual((self.output / 'Preset.ICC').read_bytes(), self.data)
        self.assertEqual((self.output / 'Preset.ICC').stat().st_mode & 0o777, 0o600)
        self.assertEqual(json.loads((self.output / 'manifest.json').read_text())['schema'], 1)
    def test_traversal_and_duplicate_case(self):
        for name in ('../escape.ICC', '/escape.ICC', 'nested/escape.icc'):
            with self.subTest(name=name): self.manifest([{**self.row, 'file': name}]);self.reject()
        self.manifest([self.row, {**self.row, 'file': 'PRESET.icc'}]);self.reject()
    def test_changed_source_and_invalid_header(self):
        (self.source / 'Preset.ICC').write_bytes(b'x' * 132);self.reject()
        data = bytearray(self.data);data[16:20] = b'CMYK'
        (self.source / 'Preset.ICC').write_bytes(data)
        self.manifest([{**self.row, 'sha256': hashlib.sha256(data).hexdigest()}]);self.reject()
    def test_symlink_and_special_file(self):
        path = self.source / 'Preset.ICC';path.unlink();path.symlink_to('../external')
        (self.root / 'external').write_bytes(self.data);self.reject()
        path.unlink();os.mkfifo(path);self.reject()
    def test_existing_output_not_modified(self):
        self.output.mkdir();(self.output / 'sentinel').write_text('keep')
        with self.assertRaises(FileExistsError): module.bundle(self.source, self.output)
        self.assertEqual((self.output / 'sentinel').read_text(), 'keep')

if __name__ == '__main__': unittest.main()
