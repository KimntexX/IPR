from unittest.mock import patch
import copy
import unittest
import numpy as np
from float_qualification import compare_module_inlet, qualify_inlet


class FloatInletTest(unittest.TestCase):
    def fixture(self, negative=True):
        values = np.full((2, 2, 3), -.01 if negative else .01, dtype='<f4')
        summary = dict(shape=[10, 10, 3], dtype='f32', finite=True,
                       unit='linear_camera_white_1', orientation='display',
                       negative_fraction=[1. if negative else 0.]*3, minimum=values[0, 0].tolist())
        desc = dict(dtype='f32', layout='rgb', unit=summary['unit'], orientation=summary['orientation'])
        return (dict(full_camera=summary, source=dict(sha256='fixture'), engine_revision='fixture'),
                {('pre_dcp_camera_rgb', (0, x)): (copy.deepcopy(desc), values.copy()) for x in (0, 2, 4)})

    def run_check(self, capture, require_negatives=False):
        with patch('float_qualification.load_capture', return_value=capture):
            return qualify_inlet('fixture.json', require_negatives)

    def test_signed_black_passes(self):
        self.assertEqual(self.run_check(self.fixture(), True)['verdict'], 'pass_scoped_float_inlet')

    def test_no_negative_on_lit_image_is_not_failure(self):
        self.run_check(self.fixture(False))

    def test_clipped_black_fails(self):
        with self.assertRaises(ValueError):
            self.run_check(self.fixture(False), True)

    def test_absent_context_not_reconstructed(self):
        value = self.fixture(); del value[0]['full_camera']
        with self.assertRaises(ValueError):
            self.run_check(value)

    def test_nonfinite_roi_fails(self):
        value = self.fixture(); value[1][('pre_dcp_camera_rgb', (0, 0))][1][0, 0, 0] = np.nan
        with self.assertRaises(ValueError):
            self.run_check(value)

    def test_summary_sign_inconsistent_fails(self):
        value = self.fixture(); value[0]['full_camera']['negative_fraction'] = [0.]*3
        with self.assertRaises(ValueError):
            self.run_check(value)

    def module_fixture(self, orientation=0):
        decoder, ds = self.fixture()
        decoder['mode'] = 'signed'
        module = dict(source=copy.deepcopy(decoder['source']), engine_revision='fixture', mode='signed',
                      metadata=dict(orientation_flip=orientation))
        ms = {('pre_output_camera_rgb', key[1]): value for key, value in copy.deepcopy(ds).items()}
        for desc, values in ds.values():
            desc.update(shape=list(values.shape))
        for desc, values in ms.values():
            desc.update(shape=list(values.shape))
        return (decoder, ds), (module, ms)

    def test_exact_signed_transfer(self):
        with patch('float_qualification.load_capture', side_effect=self.module_fixture()):
            self.assertEqual(compare_module_inlet('decoder', 'module')['verdict'], 'pass_exact_module_to_decoder_inlet')

    def test_quantized_or_clipped_transfer_fails(self):
        decoder, module = self.module_fixture()
        decoder[1][('pre_dcp_camera_rgb', (0, 0))][1][0, 0, 0] = 0
        with patch('float_qualification.load_capture', side_effect=[decoder, module]):
            with self.assertRaises(ValueError):
                compare_module_inlet('decoder', 'module')

    def test_rotated_sparse_rois_not_falsely_compared(self):
        with patch('float_qualification.load_capture', side_effect=self.module_fixture(5)):
            self.assertEqual(compare_module_inlet('decoder', 'module')['verdict'], 'not_compared_rotated_sparse_ROIs')
