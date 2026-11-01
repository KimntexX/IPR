"""Training-only row selection, gradient flow and reproducibility contracts."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np
import torch

from diagnose_train_learnability import training_data,fit_case,residual_statistics,digest


class LearnabilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):torch.set_num_threads(2)

    def fixture(self,folder):
        rng=np.random.default_rng(25)
        target=rng.uniform(-.1,1.2,(3,3,8,8)).astype(np.float32)
        signal=target+rng.normal(0,.02,target.shape).astype(np.float32)
        # A deliberate invalid reserved tensor proves it cannot enter diagnosis.
        signal[2]=np.nan;target[2]=np.nan
        np.savez(folder/'dataset.npz',signal=signal,target=target,
                 valid=np.ones((3,1,8,8),bool),sigma=np.full((3,3),.02,np.float32))
        (folder/'freeze.json').write_text('{}',encoding='utf-8')
        rows=[]
        for i in range(3):
            rows.append(dict(id=str(i),scene_group='train-scene' if i<2 else 'reserved',
                split='train' if i<2 else 'validation',iso=12800,
                content_sha256=hashlib.sha256(signal[i].tobytes()).hexdigest(),
                target_sha256=hashlib.sha256(target[i].tobytes()).hexdigest()))
        pairs=dict(freeze_sha256=digest(folder/'freeze.json'),
                   dataset=dict(sha256=digest(folder/'dataset.npz')),examples=rows)
        (folder/'pairs.json').write_text(json.dumps(pairs),encoding='utf-8')
        return pairs

    def test_only_training_rows_are_tensors_and_keep_signed_values(self):
        with tempfile.TemporaryDirectory() as name:
            folder=Path(name);self.fixture(folder)
            _,records,arrays=training_data(folder)
            self.assertEqual(len(records),2)
            self.assertTrue(all(r['split']=='train' for r in records))
            self.assertTrue(torch.isfinite(arrays['signal']).all())
            self.assertLess(float(arrays['target'].min()),0)
            self.assertGreater(float(arrays['target'].max()),1)

    def test_changed_parent_or_training_identity_is_refused(self):
        with tempfile.TemporaryDirectory() as name:
            folder=Path(name);pairs=self.fixture(folder)
            pairs['examples'][0]['content_sha256']='a'*64
            (folder/'pairs.json').write_text(json.dumps(pairs),encoding='utf-8')
            with self.assertRaisesRegex(ValueError,'identity'):training_data(folder)
            (folder/'freeze.json').write_text('{"changed":true}',encoding='utf-8')
            with self.assertRaisesRegex(ValueError,'Changed'):training_data(folder)

    def test_no_training_rows_is_not_a_fallback_to_validation(self):
        with tempfile.TemporaryDirectory() as name:
            folder=Path(name);pairs=self.fixture(folder)
            for r in pairs['examples']:r['split']='validation'
            (folder/'pairs.json').write_text(json.dumps(pairs),encoding='utf-8')
            with self.assertRaisesRegex(ValueError,'No training'):training_data(folder)

    def test_tiny_case_preserves_initial_identity_and_gradient_flow(self):
        with tempfile.TemporaryDirectory() as name:
            folder=Path(name);self.fixture(folder);_,_,arrays=training_data(folder)
            config=dict(seed=71,width=4,batch=2,learning_rate=.001,scale=2.)
            result=fit_case('fixture',arrays,config,2,[2],folder)
            self.assertAlmostEqual(result['checkpoint_metrics'][0]['mse_ratio'],1.,places=7)
            self.assertEqual(result['first_gradients'][0]['body'],0.)
            self.assertGreater(result['first_gradients'][0]['head'],0.)
            self.assertGreater(result['first_gradients'][1]['body'],0.)
            self.assertEqual(len(result['losses']),2)
            self.assertTrue((folder/'fixture-diagnostic-weights.pt').is_file())

    def test_fixed_seed_repeats_the_same_training_not_a_model_search(self):
        with tempfile.TemporaryDirectory() as name:
            folder=Path(name);self.fixture(folder);_,_,arrays=training_data(folder)
            config=dict(seed=71,width=4,batch=2,learning_rate=.001,scale=2.)
            a=fit_case('a',arrays,config,2,[2],folder)
            b=fit_case('b',arrays,config,2,[2],folder)
            self.assertEqual(a['losses'],b['losses'])
            self.assertEqual(a['checkpoint_metrics'],b['checkpoint_metrics'])

    def test_constant_residual_has_constant_low_frequency_energy(self):
        signal=torch.full((2,3,12,12),.03);target=torch.full_like(signal,.01)
        rows=residual_statistics([dict(iso=12800)]*2,
            dict(signal=signal,target=target,valid=torch.ones((2,1,12,12),dtype=torch.bool)))
        self.assertAlmostEqual(rows[0]['local_mean_9x9_energy_fraction'],1.,places=5)
        self.assertAlmostEqual(rows[0]['residual_mse'],.0004,places=7)


if __name__=='__main__':unittest.main()
