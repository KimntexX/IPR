"""Tests for derived documentation, independent of the private photographs."""
import importlib.util
import tempfile
import unittest
from pathlib import Path
from xml.etree import ElementTree
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    'documentation_metrics', Path(__file__).resolve().parents[1] / 'scripts/export_documentation_metrics.py')
metrics = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(metrics)


class MetricsTests(unittest.TestCase):
    def test_source_digest_is_portable_but_detects_content_change(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'source.json'
            source.write_bytes(b'{"value": 1}\n')
            expected = metrics.digest(source)
            source.write_bytes(b'{"value": 1}\r\n')
            self.assertEqual(metrics.digest(source), expected)
            source.write_bytes(b'{"value": 2}\r\n')
            self.assertNotEqual(metrics.digest(source), expected)

    def test_provenance_and_exact_large_integer_survive_crlf(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source.json'
            source.write_bytes(b'{"value": 1234567890123456789}\n')
            output = root / 'metrics'
            metrics.write_csv(output / 'values.csv', [{
                'source': 'source.json', 'source_sha256': metrics.digest(source),
                'locator': '/value', 'context': '', 'value': 1234567890123456789}],
                ['source', 'source_sha256', 'locator', 'context', 'value'])
            source.write_bytes(b'{"value": 1234567890123456789}\r\n')
            with patch.object(metrics, 'ROOT', root):
                metrics.validate(output)
                source.write_bytes(b'{"value": 1234567890123456788}\r\n')
                with self.assertRaises(ValueError):
                    metrics.validate(output)

    def test_obsolete_derived_hash_contract_requires_regeneration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source.json'
            source.write_bytes(b'{}\n')
            output = root / 'metrics'
            output.mkdir()
            (output / 'values.csv').write_text('source,source_sha256\nsource.json,' + metrics.digest(source) + '\n', encoding='utf8')
            with patch.object(metrics, 'ROOT', root), self.assertRaisesRegex(ValueError, 'Regenerate'):
                metrics.validate(output)

    def test_pointer_escaping_and_no_boolean_or_nonfinite_metric(self):
        rows = list(metrics.numeric_rows({'a/b~': [1, True, float('nan')], 'scene': 'RAW'}))
        self.assertEqual(rows, [('/a~1b~0/0', 1, 'RAW')])

    def test_bulk_tile_samples_stay_in_json(self):
        rows = list(metrics.numeric_rows({'records': [{'iso': 100, 'tiles': [{'x': 1, 'LR_Y': 0.5}]}]}))
        self.assertEqual(rows, [('/records/0/iso', 100, '')])

    def test_csv_round_trip(self):
        import csv
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'metrics.csv'
            metrics.write_csv(path, [{'source': 'é,preuve', 'value': 0.25}], ['source', 'value'])
            with path.open(encoding='utf-8', newline='') as stream:
                self.assertEqual(list(csv.DictReader(stream)), [{'source': 'é,preuve', 'value': '0.25'}])

    def test_missing_svg_point_is_not_interpolated(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'curve.svg'
            metrics.chart(path, 'A & B', ['v1', 'v2', 'v3'], [('IPR', [1, None, 3])], 'ratio', 'preuve')
            root = ElementTree.parse(path).getroot()
            self.assertEqual(len(root.findall('{http://www.w3.org/2000/svg}circle')), 2)
            self.assertEqual(len(root.findall('{http://www.w3.org/2000/svg}polyline')), 0)

    def test_family_classification(self):
        self.assertEqual(metrics.family('mesure-color-mixer-v2.json'), 'melangeur')
        self.assertEqual(metrics.family('mesure-bruit-v2.json'), 'bruit')
        self.assertEqual(metrics.family('latence-ouverture-v001.json'), 'performance-ouverture')


if __name__ == '__main__':
    unittest.main()
