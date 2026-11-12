import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('neural_assets', Path(__file__).resolve().parents[1] / 'scripts/neural_assets.py')
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)


class AssetTests(unittest.TestCase):
    def test_unpublished_model_is_refused(self):
        with self.assertRaises(ValueError):
            assets.validate_source({'url': None, 'sha256': None})

    def test_http_is_refused(self):
        with self.assertRaises(ValueError):
            assets.validate_source({'url': 'http://example.test/model', 'sha256': '0'*64})

    def test_cached_digest_is_checked(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'model'
            path.write_bytes(b'fixture')
            item = {'url': 'https://example.test/model', 'sha256': hashlib.sha256(b'fixture').hexdigest()}
            with patch.object(assets.urllib.request, 'urlopen', side_effect=AssertionError('Unexpected network')):
                assets.fetch(item, path)

    def test_corrupt_download_keeps_previous_file(self):
        class Response:
            url = 'https://example.test/model'
            def __enter__(self):
                self.blocks = iter([b'corrupt', b''])
                return self
            def __exit__(self, *args):
                return False
            def read(self, length):
                return next(self.blocks)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'model'
            path.write_bytes(b'old')
            with patch.object(assets.urllib.request, 'urlopen', return_value=Response()):
                with self.assertRaises(ValueError):
                    assets.fetch({'url': Response.url, 'sha256': '0'*64}, path)
            self.assertEqual(path.read_bytes(), b'old')
            self.assertEqual(list(Path(folder).iterdir()), [path])

    def test_zip_traversal_is_refused_before_extraction(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            archive = root / 'archive.zip'
            with zipfile.ZipFile(archive, 'w') as output:
                output.writestr('valid.txt', 'valid')
                output.writestr('../outside.txt', 'invalid')
            with self.assertRaises(ValueError):
                assets.unpack(archive, root/'payload')
            self.assertFalse((root/'outside.txt').exists())
            self.assertFalse((root/'payload').exists())


if __name__ == '__main__':
    unittest.main()
