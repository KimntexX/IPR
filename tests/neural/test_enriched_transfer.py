import json
from pathlib import Path
import tempfile
import unittest

import torch

from camera_unet import CameraUNet
from evaluate_enriched_transfer import identities,load_pilot
from diagnose_train_learnability import digest


class EnrichedTransferTests(unittest.TestCase):
    def fixture(self,p):
        for name in ('freeze.json','pairs.json','dataset.npz'):(p/name).write_bytes(b'fixture')
        training=dict(config=dict(steps=2048,width=4),freeze_sha256=digest(p/'freeze.json'),pairs_sha256=digest(p/'pairs.json'))
        torch.save(dict(width=4,state_dict=CameraUNet(4).state_dict(),freeze_sha256=training['freeze_sha256'],
            pairs_sha256=training['pairs_sha256']),p/'pilot-weights.pt')
        training['weights_sha256']=digest(p/'pilot-weights.pt')
        (p/'training.json').write_text(json.dumps(training),encoding='utf-8')
        return dict(training=training,training_sha256=digest(p/'training.json'),freeze=dict(training=training['config']),
            qualification=dict(dataset=dict(sha256=digest(p/'dataset.npz'))))

    def test_loads_frozen_model_without_trainable_parameters(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t);result=self.fixture(p);model,_=load_pilot(p,result)
            self.assertFalse(model.training)
            self.assertTrue(all(not v.requires_grad for v in model.parameters()))

    def test_changed_data_proof_and_weights_are_refused(self):
        for name in ('dataset.npz','training.json','pilot-weights.pt'):
            with self.subTest(name=name),tempfile.TemporaryDirectory() as t:
                p=Path(t);result=self.fixture(p);(p/name).write_bytes(b'changed')
                with self.assertRaises(ValueError):load_pilot(p,result)

    def test_wrong_budget_is_refused(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t);result=self.fixture(p);result['training']['config']['steps']=4096
            with self.assertRaises(ValueError):load_pilot(p,result)

    def test_identity_is_order_independent_not_target_independent(self):
        a=dict(id='a',source_sha256='s',content_sha256='x',target_sha256='y')
        b=dict(a,id='b')
        self.assertEqual(identities([a,b]),identities([b,a]))
        self.assertNotEqual(identities([a]),identities([dict(a,target_sha256='z')]))


if __name__=='__main__':unittest.main()
