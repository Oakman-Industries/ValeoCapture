#!/usr/bin/env python3
# Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
import importlib.util
import json
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('bundle_calibration', Path(__file__).resolve().parents[1] / 'tools/bundle_calibration.py')
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)


class CalibrationBoundary(unittest.TestCase):
    def setUp(self):
        self.value = {'schema': 1, 'cameraType': 'VA11A/5/C', 'correction': 'paired-dark-development',
                      'bodySerial': 'SYNTHETIC-TEST', 'whiteLevel': 15000,
                      'xyzToCamera': [1, 0, 0, 0, 1, 0, 0, 0, 1], 'asShotNeutral': [1, 1, 1]}
    def validate(self): module.validate(json.dumps(self.value).encode())
    def test_explicit_bounded_metadata(self): self.validate()
    def test_missing_and_unknown_source(self):
        for key, value in [('schema', True), ('cameraType', 'other'), ('bodySerial', ''), ('correction', 'unknown')]:
            with self.subTest(key=key):
                original = self.value[key]; self.value[key] = value
                with self.assertRaises(ValueError): self.validate()
                self.value[key] = original
    def test_color_values_rejected_before_capture(self):
        for key, value in [('whiteLevel', True), ('whiteLevel', 65535), ('asShotNeutral', [0, 1, 1]),
                           ('xyzToCamera', [0] * 9), ('xyzToCamera', [1, 0, 0, 0, 1, 0, 0, 0, 0.0000001]),
                           ('xyzToCamera', [float('nan')] * 9), ('asShotNeutral', [True, 1, 1])]:
            with self.subTest(key=key, value=value):
                original = self.value[key]; self.value[key] = value
                with self.assertRaises(ValueError): self.validate()
                self.value[key] = original
    def test_oversize(self):
        self.value['extra'] = 'x' * 32769
        with self.assertRaises(ValueError): self.validate()


if __name__ == '__main__': unittest.main()
