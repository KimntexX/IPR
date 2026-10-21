"""Diagnostic DCP copy safety, no private files."""
import struct
import unittest
from run_raw_opening_trials import without_curve_bytes


class TrialTests(unittest.TestCase):
    def fixture(self, tag=50940):
        return b'II'+struct.pack('<HIH',0x4352,8,1)+struct.pack('<HHII',tag,11,4,26)+struct.pack('<I4f',0,0,0,1,1)

    def test_only_tone_tag_identifier_changes(self):
        before=self.fixture()
        after=without_curve_bytes(before)
        self.assertEqual(after[:10],before[:10])
        self.assertEqual(after[12:],before[12:])
        self.assertEqual(struct.unpack_from('<H',after,10)[0],65000)
        self.assertEqual(struct.unpack_from('<H',before,10)[0],50940)

    def test_missing_curve_refused(self):
        with self.assertRaisesRegex(ValueError,'Expected one'):
            without_curve_bytes(self.fixture(50000))

    def test_reserved_tag_refused(self):
        with self.assertRaisesRegex(ValueError,'Reserved'):
            without_curve_bytes(self.fixture(65000))


if __name__ == '__main__':
    unittest.main()
