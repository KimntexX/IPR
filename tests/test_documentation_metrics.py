"""Tests for derived documentation, independent of the private photographs."""
import importlib.util
import tempfile
import unittest
from pathlib import Path
from xml.etree import ElementTree

SPEC = importlib.util.spec_from_file_location(
    'documentation_metrics', Path(__file__).resolve().parents[1] / 'scripts/export_documentation_metrics.py')
metrics = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(metrics)


class MetricsTests(unittest.TestCase):
    def test_pointer_escaping_and_no_boolean_or_nonfinite_metric(self):
        rows = list(metrics.numeric_rows({'a/b~': [1, True, float('nan')], 'scene': 'RAW'}))
        self.assertEqual(rows, [('/a~1b~0/0', 1, 'RAW')])

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


if __name__ == '__main__':
    unittest.main()
