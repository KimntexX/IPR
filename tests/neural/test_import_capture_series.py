"""Exclusive corpus import, integrity and stale-inventory contracts."""
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch

from import_capture_series import copy_verified


class ImportTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.source = Path(self.folder.name)/'source.CR3'
        self.source.write_bytes(b'raw-source-data')
        self.destination = Path(self.folder.name)/'copy.CR3'
        self.stat = self.source.stat()

    def copy(self):
        return copy_verified(self.source,self.destination,self.stat.st_size,self.stat.st_mtime_ns)

    def test_copy_matches_and_preserves_source(self):
        result = self.copy()
        self.assertEqual(result['source_sha256'],result['destination_sha256'])
        self.assertEqual(self.source.read_bytes(),self.destination.read_bytes())
        self.assertEqual(self.source.stat().st_mtime_ns,self.stat.st_mtime_ns)

    def test_existing_destination_is_untouched(self):
        self.destination.write_bytes(b'user-reference')
        with self.assertRaises(FileExistsError): self.copy()
        self.assertEqual(self.destination.read_bytes(),b'user-reference')

    def test_stale_inventory_does_not_create_copy(self):
        with self.assertRaises(ValueError):
            copy_verified(self.source,self.destination,self.stat.st_size+1,self.stat.st_mtime_ns)
        self.assertFalse(self.destination.exists())

    def test_hash_mismatch_is_failure_not_success(self):
        with patch('import_capture_series.digest',side_effect=['source','different']):
            with self.assertRaises(ValueError): self.copy()
        self.assertTrue(self.source.exists())
        self.assertTrue(self.destination.exists())


if __name__ == '__main__': unittest.main()
