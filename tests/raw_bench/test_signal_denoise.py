"""A B-V1 baseline must refer to the exact frozen LR/mask campaign."""
import importlib.util
import json
from pathlib import Path
import unittest
from unittest.mock import Mock, patch


spec = importlib.util.spec_from_file_location(
    'qualify_signal_denoise', Path(__file__).with_name('qualify_signal_denoise.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class VariantBaselineTests(unittest.TestCase):
    def variant(self, schema='ipr-noise-strength-law-variant-v001', digest='expected'):
        path = Mock()
        path.read_text.return_value = json.dumps(dict(
            schema=schema, reference=dict(sha256=digest),
            measures=dict(B_V1={'scene': {'0/0/0': {'dark': 12}}})))
        return path

    def test_matching_reference_returns_variant_not_old_B(self):
        with patch.object(module, 'sha256', return_value='expected'):
            result = module.variant_baseline(Path('reference.json'), self.variant())
        self.assertEqual(result['scene']['0/0/0']['dark'], 12)

    def test_different_reference_is_rejected(self):
        with patch.object(module, 'sha256', return_value='actual'):
            with self.assertRaisesRegex(ValueError, 'different frozen reference'):
                module.variant_baseline(Path('reference.json'), self.variant())

    def test_unknown_schema_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Unknown variant schema'):
            module.variant_baseline(Path('reference.json'), self.variant(schema='unknown'))


if __name__ == '__main__':
    unittest.main()
