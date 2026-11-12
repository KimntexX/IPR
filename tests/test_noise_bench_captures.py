import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import numpy as np
from noise_bench.captures import load_field


class CaptureAdapterTests(unittest.TestCase):
    def test_current_capture_contract_and_phase(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pixels = np.arange(16, dtype='<f4').reshape(4, 4, 1)-8
            binary = root/'pixels.bin'
            binary.write_bytes(pixels.tobytes())
            descriptor = dict(stage='black_subtracted_cfa', dtype='f32', shape=[4, 4, 1],
                offset_bytes=0, stride_bytes=16, unit='DN', layout='cfa',
                orientation='sensor', origin_yx=[1, 0], context=dict(cfa=[[0, 1], [3, 2]]))
            manifest = dict(schema_version=1, source=dict(sha256='synthetic-source'),
                pixels=dict(path='pixels.bin', sha256=hashlib.sha256(binary.read_bytes()).hexdigest()),
                stages=[descriptor])
            path = root/'capture.json'
            path.write_text(json.dumps(manifest), encoding='utf-8')
            args = dict(stage='black_subtracted_cfa', origin_yx=[1, 0], domain='cfa', expected_unit='DN')
            a, metadata = load_field(path, **args)
            np.testing.assert_array_equal(a[0], pixels[::2, ::2, 0])
            self.assertEqual(metadata['plane_colours'], [3, 2, 0, 1])
            self.assertLess(a.min(), 0)
            with self.assertRaises(ValueError):
                load_field(path, **dict(args, expected_unit='normalized'))
            with self.assertRaises(ValueError):
                load_field(path, **dict(args, domain='rendered'))
            binary.write_bytes(b'changed')
            with self.assertRaises(ValueError):
                load_field(path, **args)

    def test_rendered_capture_and_odd_cfa_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pixels = np.arange(27, dtype='<f4').reshape(3, 3, 3)
            binary = root/'pixels.bin'
            binary.write_bytes(pixels.tobytes())
            descriptor = dict(stage='working_rec2020', dtype='f32', shape=[3, 3, 3],
                offset_bytes=0, stride_bytes=36, unit='linear', layout='rgb',
                orientation='sensor', origin_yx=[0, 0], context={})
            manifest = dict(schema_version=1, source=dict(sha256='synthetic-source'),
                pixels=dict(path='pixels.bin', sha256=hashlib.sha256(binary.read_bytes()).hexdigest()),
                stages=[descriptor])
            path = root/'capture.json'
            path.write_text(json.dumps(manifest), encoding='utf-8')
            a, _ = load_field(path, stage='working_rec2020', origin_yx=[0, 0], domain='rendered', expected_unit='linear')
            np.testing.assert_array_equal(a, pixels.transpose(2, 0, 1))
            descriptor.update(shape=[3, 3, 1], stride_bytes=12, layout='cfa')
            path.write_text(json.dumps(manifest), encoding='utf-8')
            with self.assertRaises(ValueError):
                load_field(path, stage='working_rec2020', origin_yx=[0, 0], domain='cfa', expected_unit='linear')


if __name__ == '__main__':
    unittest.main()
