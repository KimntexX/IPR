import json
from pathlib import Path
import unittest
import torch
from dru_training_data import example
from dru_comparison_policy import texture_parameters,scenario_for
from run_synthetic_joint_pilot import tensor_hash


class ComparisonPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(4)
        cls.root=Path(__file__).resolve().parents[2]
        path=cls.root/'build-patch_neural_restoration/local-sigma-plan-v001/plan.json'
        cls.scenes=json.loads(path.read_text(encoding='utf-8'))['scenes'] if path.exists() else [dict(parameters=[dict(colour=c,a=.001,b=.00001,gain=1.,divisor=1.) for c in range(4)])]*3

    @unittest.skipUnless((Path(__file__).resolve().parents[2]/'build-patch_neural_restoration/dru-training-audit-v001/result.json').exists(),'Local archived audit required')
    def test_reference_replays_archived_observation_target_and_sigma(self):
        audit=json.loads((self.root/'build-patch_neural_restoration/dru-training-audit-v001/result.json').read_text(encoding='utf-8'))
        for serial in (0,1,2,3,4,5,6,7,32,33,127):
            old=next(r for r in audit['training'] if r['seed']==71 and r['serial']==serial)
            packed,sigma,_,target=example(serial,71,self.scenes)
            for value,key in ((packed,'observed_core_sha256'),(sigma,'sigma_core_sha256'),(target,'target_core_sha256')):
                self.assertEqual(tensor_hash(value[:,:,192:320,192:320]),old[key])

    def test_noise_arm_changes_only_noise_target_remains_identical(self):
        base=example(33,71,self.scenes);candidate=example(33,71,self.scenes,arm='noise')
        self.assertTrue(torch.equal(base[3],candidate[3]));self.assertTrue(torch.equal(base[2],candidate[2]))
        self.assertFalse(torch.equal(base[0],candidate[0]));self.assertFalse(torch.equal(base[1],candidate[1]))
        identity=280000000+71*100000
        self.assertIs(scenario_for(1,identity+1,self.scenes,'noise'),scenario_for(1,identity+1,self.scenes,'reference'))
        self.assertIs(scenario_for(33,identity+33,self.scenes,'details'),scenario_for(33,identity+33,self.scenes,'reference'))

    def test_detail_arm_changes_only_texture_slots_and_preserves_spatial_phase(self):
        base=example(1,71,self.scenes);candidate=example(1,71,self.scenes,arm='details')
        self.assertTrue(torch.equal(base[2],candidate[2]));self.assertFalse(torch.equal(base[3],candidate[3]))
        for serial in (2,3,4,5,6,7):
            for a,b in zip(example(serial,71,self.scenes),example(serial,71,self.scenes,arm='details')):
                self.assertTrue(torch.equal(a,b))

    def test_invalid_arm_refused(self):
        with self.assertRaises(ValueError):example(1,71,self.scenes,arm='combined')

    def test_detail_quota_covers_nine_continuous_bins_without_test_targets(self):
        counts={}
        for serial in range(1024):
            if serial%8 not in (0,1):continue
            p=texture_parameters(('weak_achromatic_wave','chromatic_wave')[serial%8],280000000+71*100000+serial,serial)
            key=(p['frequency_bin'],p['amplitude_bin']);counts[key]=counts.get(key,0)+1
            self.assertTrue(2<=p['frequency']<=28)
            self.assertTrue(.003/1.35<=p['amplitude']/1.35<=.12/1.35)
        self.assertEqual(len(counts),9);self.assertLessEqual(max(counts.values())-min(counts.values()),1)


if __name__=='__main__':unittest.main()
