from pathlib import Path
import json
import tempfile
import unittest
from unittest.mock import MagicMock,patch
import numpy as np
from physical_light_audit import run
from prepare_next_training import digest


class PhysicalLightAuditTests(unittest.TestCase):
    def fixture(self,root,index):
        folder=root/'docs/validation/raw';folder.mkdir(parents=True)
        frames=[]
        for seed in (41,42):
            p=root/f'{seed}.npy';np.save(p,np.random.default_rng(seed).integers(500,530,(1,4,32,32),dtype=np.uint16))
            frames.append(dict(cached_tiles=p.name,tiles_sha256=digest(p)))
        parent=dict(frames=frames,pairs=[dict(index=0,iso=3200,frames=[index,1]),dict(index=1,iso=3200,frames=[999,999])],
            fits=[dict(iso=3200,plane=0,fit=dict(heldout=[dict(pair=0,roi=0,signal_dn=100,predicted_variance_dn2=25)]))])
        (folder/'neural-sensor-noise-v001-20261105T173608Z.json').write_text(json.dumps(parent),encoding='utf-8')

    def test_frozen_integer_indices_and_only_previously_eligible_pairs(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root,0)
            guard=MagicMock();guard.stats.return_value={}
            with patch('memory_budget.MemoryGuard',return_value=guard):run(root,root/'output')
            result=json.loads((root/'output/progress.json').read_text(encoding='utf-8'))
            self.assertEqual(result['status'],'completed_known_light_spectral_diagnostics_not_full_physical_qualification')
            self.assertEqual(len(result['pairs']),1)
            self.assertEqual(len(result['pairs'][0]['rows']),1)

    def test_invalid_frame_index_stops_instead_of_reading_another_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root,-1)
            guard=MagicMock();guard.stats.return_value={}
            with patch('memory_budget.MemoryGuard',return_value=guard):
                with self.assertRaises(ValueError):run(root,root/'output')
            self.assertEqual(json.loads((root/'output/progress.json').read_text(encoding='utf-8'))['status'],'stopped')


if __name__=='__main__':unittest.main()
