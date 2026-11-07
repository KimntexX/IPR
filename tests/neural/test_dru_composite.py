import unittest
import torch
from context_data import extend_texture
from dru_composite_data import training_texture,reserved_signal,background_case
from dru_composite_controls import reserve,response
from joint_bayer_unet import sample_rgb
from dru_training_data import example
from evaluate_dru_composites import assess


class CompositeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):torch.set_num_threads(4)

    def test_flat_slot_is_exact_reference_and_added_background_is_achromatic(self):
        for serial in (0,1,8):
            family=('weak_achromatic_wave','chromatic_wave')[serial%8];identity=287100000+serial
            old=extend_texture(family,identity,256);new=training_texture(family,identity,256,serial)
            if serial==0:self.assertTrue(torch.equal(old,new))
            else:
                self.assertFalse(torch.equal(old,new))
                delta=new-old;torch.testing.assert_close(delta[:,0],delta[:,1],atol=1e-7,rtol=0.)

    def test_prospective_reserve_is_complete_unique_and_uses_disjoint_seeds(self):
        data=reserve();self.assertEqual(len(data['fields']),384)
        self.assertTrue(all(c['identity']>=870000000 for c in data['cases']))
        self.assertEqual(len({f['noise_seed'] for f in data['fields']}),384)

    def test_joint_fit_keeps_weak_gain_when_background_gain_changes(self):
        case=dict(frequency=19.,amplitude=.006,angle=.37,phase=1.7,colours=[1.,-.7,.4],background=background_case(870000001,'edge',96.))
        truth,wave,cosine,unit=reserved_signal(case)
        tags=torch.tensor([[0,1,3,2]]);clean=sample_rgb(truth,tags)
        altered=sample_rgb(truth+.1*unit[None,None],tags)
        rows=response(altered,clean,wave,cosine,unit,True)
        for row in rows:self.assertAlmostEqual(row['sine_gain'],1.,places=5)

    def test_non_texture_training_slots_remain_bit_exact(self):
        scenario=dict(parameters=[dict(colour=c,a=.001,b=.00001,gain=1.,divisor=1.) for c in range(4)])
        for serial in range(2,8):
            original=example(serial,71,[scenario]);composite=example(serial,71,[scenario],arm='composite')
            for a,b in zip(original,composite):self.assertTrue(torch.equal(a,b))

    def test_reserved_assessment_retains_all_strata_and_refuses_missing_field(self):
        held=reserve();rows=[]
        for field in held['fields']:
            rows.append(dict(**field,site_colours=[0,1,3,2],
                corrected=[dict(sine_gain=.9,mse=1.) for _ in range(4)],observed=[dict(mse=2.) for _ in range(4)]))
        value=assess(rows,held)
        self.assertEqual(len(value['groups']),48);self.assertAlmostEqual(value['median_gain_error'],.1)
        self.assertEqual(value['median_mse_ratio'],.5)
        with self.assertRaises(ValueError):assess(rows[:-1],held)


if __name__=='__main__':unittest.main()
