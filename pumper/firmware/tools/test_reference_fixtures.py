"""Keep the captured compatibility corpus intact without compiling C."""
import hashlib
import json
from pathlib import Path
import unittest


class ReferenceFixturesTest(unittest.TestCase):
    def test_captured_outputs_match_their_manifest(self):
        root = Path(__file__).resolve().parents[1] / 'reference/golden'
        manifest = json.loads((root / 'manifest.json').read_text())
        self.assertEqual(set(manifest['fixtures']), {'compat.bin', 'dsp.bin', 'spectrum.bin', 'storage.bin'})
        for name, expected in manifest['fixtures'].items():
            with self.subTest(fixture=name):
                data = (root / name).read_bytes()
                self.assertEqual(len(data), expected['bytes'])
                self.assertEqual(hashlib.sha256(data).hexdigest(), expected['sha256'])


if __name__ == '__main__':
    unittest.main()
