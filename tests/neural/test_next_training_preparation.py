from pathlib import Path
import tempfile
import unittest

from prepare_next_training import configurations, upstream_finished, inventory, prepare, write, read


class PreparationTests(unittest.TestCase):
    def test_terminal_status_requires_finished_archive_boundary(self):
        completed = 'completed_composite_experiment_not_production_validation'
        self.assertFalse(upstream_finished(dict(status=completed)))
        self.assertFalse(upstream_finished(dict(status='composite-73', finished_at_utc='now')))
        self.assertTrue(upstream_finished(dict(status=completed, finished_at_utc='now')))
        self.assertTrue(upstream_finished(dict(status='stopped_on_error_or_load', finished_at_utc='now')))

    def test_scope_and_batches_are_comparable(self):
        configs = configurations()
        gpu = [c for c in configs if c['device']=='cuda']
        self.assertEqual(len(gpu), 8)
        self.assertEqual({c['batch'] for c in gpu}, {8})
        for width in (16, 32):
            for side in (128, 256):
                self.assertEqual({c['precision'] for c in gpu if c['width']==width and c['side']==side},
                                 {'fp32', 'amp-fp16'})
        self.assertTrue(all(c['precision']=='fp32' for c in configs if c['device']=='cpu'))

    def test_rgb_metadata_never_becomes_clean_cfa_or_assigned_partition(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'memory.json'
            write(path, dict(captures=[dict(source=str(Path(directory)/'absent.CR3'), source_sha256='abc',
                model='camera', iso=100, domain='balanced RGB after demosaic')]))
            result = inventory(dict(natural_inventory=str(path)))
            source = result['sources'][0]
            self.assertFalse(source['accessible'])
            self.assertFalse(source['clean_target_qualified'])
            self.assertIsNone(source['partition'])
            self.assertEqual(source['prior_domain'], 'balanced RGB after demosaic')
            self.assertIn('not_a_partition', result['status'])

    def test_timezone_required_without_creating_partial_plan(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)/'output'
            with self.assertRaises(ValueError):
                prepare(output, '2026-11-08T08:00:00')
            self.assertFalse(output.exists())

    def test_atomic_json_rejects_nan_preserving_previous_measurement(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'progress.json'
            write(path, dict(status='waiting'))
            with self.assertRaises(ValueError):
                write(path, dict(value=float('nan')))
            self.assertEqual(read(path), dict(status='waiting'))


if __name__ == '__main__':
    unittest.main()
