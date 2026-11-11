import tempfile
import unittest
from pathlib import Path
from unittest import mock

import paired_scene_audit


class LocateSeriesTests(unittest.TestCase):
    def test_protocol_folders_win_over_the_card_and_folder_names_do_not_matter(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            protocol, card = root/'protocole', root/'card'
            for folder in (protocol/'100EOSR6', protocol/'escalade', card/'100EOSR6'):
                folder.mkdir(parents=True)
            (protocol/'100EOSR6'/'6V1A0007.CR3').write_bytes(b'old series')
            (card/'100EOSR6'/'6V1A0758.CR3').write_bytes(b'gym')
            with mock.patch.object(paired_scene_audit, 'PROTOCOL', protocol):
                self.assertEqual(paired_scene_audit.locate_series('6V1A0758.CR3', card), card/'100EOSR6')
                (protocol/'escalade'/'6V1A0758.CR3').write_bytes(b'gym')
                self.assertEqual(paired_scene_audit.locate_series('6V1A0758.CR3', card), protocol/'escalade')
                with self.assertRaises(FileNotFoundError):
                    paired_scene_audit.locate_series('6V1A9999.CR3', card)


if __name__ == '__main__':
    unittest.main()
