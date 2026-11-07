from pathlib import Path
import os
import tempfile
import unittest
from dru_checkpoint import link_identical_checkpoint


class CheckpointLinksTests(unittest.TestCase):
    def test_equal_contents_share_storage_and_atomic_new_save_keeps_old_state(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);a=root/'checkpoint.pt';b=root/'weights.pt'
            a.write_bytes(b'model-and-adam');b.write_bytes(a.read_bytes())
            self.assertTrue(link_identical_checkpoint(a,b));self.assertTrue(os.path.samefile(a,b))
            self.assertFalse(link_identical_checkpoint(a,b))
            temporary=root/'new.tmp';temporary.write_bytes(b'new-state');os.replace(temporary,b)
            self.assertEqual(a.read_bytes(),b'model-and-adam');self.assertEqual(b.read_bytes(),b'new-state')

    def test_different_contents_and_outside_directory_are_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);a=root/'checkpoint.pt';b=root/'weights.pt'
            a.write_bytes(b'original');b.write_bytes(b'different')
            self.assertFalse(link_identical_checkpoint(a,b));self.assertEqual(b.read_bytes(),b'different')
            folder=root/'other';folder.mkdir();c=folder/'weights.pt';c.write_bytes(a.read_bytes())
            with self.assertRaises(ValueError):link_identical_checkpoint(a,c)


if __name__=='__main__':unittest.main()
