"""End-to-end synthetic training I/O test, without card data or photo tuning."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

from train_photo_pilot import run,digest


class PhotoTrainingTests(unittest.TestCase):
    def test_signed_dataset_trains_once_and_preserves_inputs(self):
        root=Path(__file__).resolve().parents[2]/'build-patch_neural_restoration'
        root.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='training-contract-',dir=root) as folder:
            work=Path(folder)
            rng=np.random.default_rng(75)
            targets=rng.uniform(-.05,1.2,(4,3,8,8)).astype(np.float32)
            signals=(targets+rng.normal(0,.02,targets.shape)).astype(np.float32)
            examples=[]
            for i in range(4):
                examples.append(dict(id=f'fixture-{i}',source=f'fixture-{i}.synthetic',
                    scene_group='indoor' if i<2 else 'vegetation',split='train' if i<2 else 'validation',
                    iso=3200,roi=i,content_sha256=hashlib.sha256(signals[i].tobytes()).hexdigest(),
                    target_sha256=hashlib.sha256(targets[i].tobytes()).hexdigest()))
            freeze=dict(campaign_id='synthetic-training-contract',training=dict(seed=71,width=4,
                batch=2,steps=2,learning_rate=.001,scale=2.))
            (work/'freeze.json').write_text(json.dumps(freeze),encoding='utf-8')
            np.savez(work/'dataset.npz',signal=signals,target=targets,
                valid=np.ones((4,1,8,8),bool),sigma=np.full((4,3),.02,np.float32))
            original=digest(work/'dataset.npz')
            pairs=dict(status='qualified_for_exploratory_pilot',freeze_sha256=digest(work/'freeze.json'),
                dataset=dict(sha256=original),examples=examples,limits=['Synthetic test fixture, not a photograph.'])
            (work/'pairs.json').write_text(json.dumps(pairs),encoding='utf-8')
            run(work)
            result=json.loads((work/'training.json').read_text(encoding='utf-8'))
            self.assertEqual(result['split_counts'],dict(train=2,validation=2,test=0))
            self.assertEqual(len(result['records']),4)
            self.assertEqual(len(result['losses']),2)
            self.assertEqual(digest(work/'dataset.npz'),original)
            self.assertTrue((work/'pilot-weights.pt').is_file())
            self.assertLess(min(r['output_min'] for r in result['records']),0)
            self.assertGreater(max(r['output_max'] for r in result['records']),1)
            with self.assertRaises(ValueError):run(work)


if __name__=='__main__':
    unittest.main()
