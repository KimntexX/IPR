import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from confirm_closed_structure import prepare


class StructureConfirmationTests(unittest.TestCase):
    def test_prepare_freezes_declared_disjoint_seed_ranges_and_complete_families(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root/'build-patch_neural_restoration'/'confirmation'
            with patch('confirm_closed_structure.subprocess.check_output', return_value='test-sha'):
                prepare(root, output)
            plan = json.loads((output/'plan.json').read_text(encoding='utf-8'))
            self.assertEqual({r['seed'] for r in plan['edges']}, set(range(2000064, 2000096)))
            self.assertEqual(len({r['target_sha256'] for r in plan['edges']}), 32)
            self.assertEqual({r['normal_bin'] for r in plan['edges']}, set(range(32)))
            self.assertEqual(len(plan['mixed']), 20)
            self.assertEqual(len({r['target_sha256'] for r in plan['mixed']}), 20)
            self.assertTrue(all(len(r['packed_sha256_by_phase']) == 4 for r in plan['edges']))
            self.assertEqual({r['original_phase_index'] for r in plan['mixed']}, {0, 1, 2, 3})
            with self.assertRaises(FileExistsError):
                prepare(root, output)

    def test_prepare_refuses_output_outside_dedicated_build(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(ValueError):
                prepare(root, root/'outside')


if __name__ == '__main__':
    unittest.main()
