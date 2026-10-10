import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from analyse_lumieres_intermediaires import fingerprint


class IntermediateCampaignTests(unittest.TestCase):
    def test_fingerprint_is_full_sha256(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'sample'
            data = b'abc' * 3000000
            path.write_bytes(data)
            self.assertEqual(fingerprint(path), hashlib.sha256(data).hexdigest())

    def test_published_json_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'published.json'
            path.write_bytes(b'{"immutable":true}')
            result = subprocess.run([sys.executable, str(Path(__file__).with_name(
                'analyse_lumieres_intermediaires.py')), '--output', str(path)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b'already exists', result.stderr)
            self.assertEqual(path.read_bytes(), b'{"immutable":true}')


if __name__ == '__main__':
    unittest.main()
