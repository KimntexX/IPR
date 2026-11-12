import unittest
import numpy as np
from audit_natural_bench import group_sessions, border_samples


class NaturalBenchAuditTests(unittest.TestCase):
    def test_day_group_detects_cross_role_leak(self):
        rows = [dict(role=role, metadata=dict(model='camera', datetime_original='2026:11:01 12:00:00'))
                for role in ('training', 'development')]
        self.assertTrue(group_sessions(rows)[0]['cross_role_leak'])

    def test_missing_day_cannot_silently_pass_split(self):
        with self.assertRaises(ValueError):
            group_sessions([dict(role='training', metadata=dict(model='camera'))])

    def test_border_sampling_ignores_active_pixels_and_keeps_phase(self):
        image = np.full((12, 12), 100, dtype=np.uint16)
        image[2:10,2:10] = 900
        rows = border_samples(image, np.array([[0,1],[3,2]]), 2, 2, 8, 8)
        self.assertEqual(len(rows), 4)
        self.assertTrue(all(row['sampled_count']>0 and row['quantiles_dn']==[100,100,100] for row in rows))

    def test_no_crop_margins_does_not_invent_black_measurement(self):
        rows = border_samples(np.ones((8,8), dtype=np.uint16), np.array([[0,1],[3,2]]), 0, 0, 8, 8)
        self.assertTrue(all(row['sampled_count']==0 and row['quantiles_dn'] is None for row in rows))


if __name__ == '__main__':
    unittest.main()
