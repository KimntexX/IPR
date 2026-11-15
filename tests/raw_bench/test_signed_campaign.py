import copy
from pathlib import Path
import tempfile
import unittest
from capture import sha256
from signed_campaign import select_inputs


class SignedCampaignTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.data = dict(records=[], native={})
        for iso in (1600, 51200):
            for role in ('black', 'near_black_scene', 'illuminated_scene'):
                paths = []
                for shot in range(2):
                    source = Path(self.temp.name)/f'{iso}-{role}-{shot}.raw'
                    source.write_bytes(b'fixture')
                    paths.append(str(source))
                    self.data['native'][str(source)] = dict(record=dict(
                        source_sha256=sha256(source), applied_black_dn=[2048]*4, white_before_process=14351))
                self.data['records'].append(dict(iso=iso, role=role, paths=paths))

    def test_select_does_not_mutate_baseline(self):
        before = copy.deepcopy(self.data)
        result = select_inputs(self.data)
        self.assertEqual(len(result), 6)
        self.assertEqual(self.data, before)
        self.assertEqual(result[0]['shots'][0]['black'], [2048]*4)

    def test_incomplete_corpus_rejected(self):
        self.data['records'].pop()
        with self.assertRaises(ValueError):
            select_inputs(self.data)

    def test_changed_source_rejected(self):
        Path(self.data['records'][0]['paths'][0]).write_bytes(b'changed')
        with self.assertRaises(ValueError):
            select_inputs(self.data)


if __name__ == '__main__':
    unittest.main()
