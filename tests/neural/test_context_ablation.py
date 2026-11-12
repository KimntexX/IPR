import json
from pathlib import Path
import tempfile
import unittest

import numpy as np
import torch

from evaluate_context_ablation import crop_context,read_tile
from diagnose_train_learnability import digest


class ContextAblationTests(unittest.TestCase):
    def test_crop_keeps_exact_source_values_and_phase(self):
        tile=torch.arange(3*384*384,dtype=torch.float32).reshape(1,3,384,384)
        torch.testing.assert_close(crop_context(tile,[24,144]),tile[:,:,144:240,24:120],rtol=0,atol=0)
        for xy in ([25,24],[-4,24],[292,0],[0.,0]):
            with self.assertRaises(ValueError):crop_context(tile,xy)
        with self.assertRaises(ValueError):crop_context(tile[:,:,:96,:96],[0,0])

    def fixture(self,p):
        tile=dict(dtype='<f4',shape=[384,384,3],offset_bytes=0)
        array=np.full((384,384,3),-.02,dtype='<f4')
        pixels=p/'pixels.bin';pixels.write_bytes(array.tobytes())
        descriptor=p/'capture.json';descriptor.write_text(json.dumps(dict(tiles=[tile])),encoding='utf-8')
        return dict(descriptor=str(descriptor),descriptor_sha256=digest(descriptor),pixels=pixels.name,
            pixels_sha256=digest(pixels),tiles=[tile])

    def test_signed_tile_is_loaded_without_clamp(self):
        with tempfile.TemporaryDirectory() as t:
            c=self.fixture(Path(t));tile=read_tile(c,0)
            self.assertEqual(tuple(tile.shape),(1,3,384,384))
            self.assertTrue((tile<0).all())

    def test_changed_pixels_are_refused(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t);c=self.fixture(p);(p/'pixels.bin').write_bytes(b'changed')
            with self.assertRaises(ValueError):read_tile(c,0)

    def test_changed_descriptor_is_refused(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t);c=self.fixture(p);Path(c['descriptor']).write_text('{}',encoding='utf-8')
            with self.assertRaises(ValueError):read_tile(c,0)

    def test_truncated_pixels_with_new_hash_are_refused(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t);c=self.fixture(p);(p/'pixels.bin').write_bytes(b'changed')
            c['pixels_sha256']=digest(p/'pixels.bin')
            with self.assertRaises(ValueError):read_tile(c,0)


if __name__=='__main__':unittest.main()
