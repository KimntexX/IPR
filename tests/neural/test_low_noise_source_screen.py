"""Bounded day diversity never overrides protected or separate-camera roles."""
import unittest
from screen_low_noise_sources import select


class SingleScreenTests(unittest.TestCase):
    def row(self,index,role='unqualified_single_or_sequence'):
        return dict(path=f'source/{index}.CR3',role=role,metadata=dict(iso=100,focal_mm=24.,
            exposure_s=.001,datetime_original=f'2026:10:{index+1:02d} 12:00:00'))

    def test_at_most_six_days_and_no_duplicate_copy_day(self):
        records=[self.row(i) for i in range(10)]
        duplicate=self.row(0);duplicate['path']='other/copy.CR3'
        candidates,chosen=select(records+[duplicate])
        self.assertEqual(len(candidates),11);self.assertEqual(len(chosen),6)
        self.assertEqual(len({r['metadata']['datetime_original'][:10] for r in chosen}),6)

    def test_protected_other_camera_and_calibration_are_never_selected(self):
        for role in ('reserved_evaluated_control','existing_training_composition',
                     'separate_camera_qualification_needed','calibration_only_candidate'):
            self.assertEqual(select([self.row(0,role)])[1],[])

    def test_priority_bounds_do_not_admit_high_iso_or_long_exposure(self):
        row=self.row(0);row['metadata']['iso']=12800
        self.assertEqual(select([row])[1],[])
        row=self.row(0);row['metadata']['exposure_s']=.1
        self.assertEqual(select([row])[1],[])


if __name__=='__main__':unittest.main()
