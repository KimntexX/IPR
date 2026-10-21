"""Tests for corrected measurement semantics, independent of private photographs."""
import io
import unittest
import zipfile
import struct
import tempfile
from unittest import mock
from pathlib import Path

import numpy as np

import analyse_base_colorimetrique_v2 as analysis
import investigate_tonal_base as investigation
import audit_canon_metadata as canon_metadata
import investigate_canon_calibration as canon_calibration
import analyse_canon_white_levels as white_levels
import analyse_tonal_rendering as tonal_rendering
import investigate_dcp_curve as dcp_curve
import investigate_dcp_order as dcp_order
import audit_dng_baseline as dng_baseline
import investigate_dcp_normalization as dcp_normalization
import investigate_standard_normalization as standard_normalization
import prepare_raw_normalization as normalization_manifest
import verify_raw_normalization as normalization_verification


class BaseColorimetryTests(unittest.TestCase):
    def test_guarded_hue_curve_falls_back_only_on_inverted_extrema(self):
        rgb=np.array([[.4,.6,.8],[.1,.2,.3],[.4,.4,.4]])
        points=[0,0,.5,1,1,.1]
        actual,mask=normalization_verification.guarded_curve(rgb,points)
        np.testing.assert_array_equal(mask,[True,False,False])
        np.testing.assert_allclose(actual[0],dcp_curve.curve(rgb[0],points))
        np.testing.assert_allclose(actual[1:],dcp_curve.hue_stable(rgb[1:],points))

    def test_standard_renderer_rejects_unverified_profile(self):
        with self.assertRaises(ValueError):
            standard_normalization.render(np.zeros((1,1,3)),5200,{})

    def test_manifest_builder_rejects_unverified_profile(self):
        with mock.patch.object(normalization_manifest, 'read_dcp', return_value={}):
            with self.assertRaises(ValueError):
                normalization_manifest.build({}, {}, {}, Path('unused'))

    def test_manifest_builder_requires_explicit_model_rules_and_source_evidence(self):
        tags = {50778:(17,),50779:(21,),50964:(1,),50965:(1,),50940:(0,0,1,1),
                50981:(1,2,1),50982:(0,),51108:(1,),51109:(-.5,)}
        sha='a'*64
        scene={'scene':'A','comparison_available':True,'raw_sha256':sha,'dng_sha256':sha,
               'dng_white':13995,'black_rgbg':[512]*4,'dng_baseline_exposure_ev':.26}
        measure={'diagnostic_only':True,'inputs_sha256':{'profile':sha},
                 'libraw_version':'0.22.2-Release','scenes':[scene,{'comparison_available':False}],
                 'model_rules':[dict(camera_model='EOS R6 Mark III',white_level=13995,
                    baseline_exposure_ev=.26,black_min=400,black_max=640,
                    native_white_min=13000,native_white_max=17000,provenance='explicit proposal')]}
        manifest={'images':[{'raw':'A.CR3','variant':'baseline','raw_sha256':sha,'camera':'EOS R6 Mark III','iso':100}]}
        audit={'scenes':[{'scene':'A','path':'A.dng','raw_sha256':sha,'sha256':sha,'ifd0':{'make':'Canon'}}]}
        with mock.patch.object(normalization_manifest,'read_dcp',return_value=tags), \
             mock.patch.object(normalization_manifest,'file_hash',return_value=sha):
            output=normalization_manifest.build(measure,manifest,audit,Path('profile'))
            self.assertEqual(len(output['entries']),1)
            self.assertEqual(output['entries'][0]['camera_model'],'EOS R6 Mark III')
            self.assertEqual(output['entries'][0]['baseline_exposure_ev'],.26)
            self.assertEqual(output['schema'],4)
            self.assertEqual(output['entries'][0]['decoder_id'],'libraw')
            self.assertEqual(output['entries'][0]['format_extension'],'cr3')
            self.assertNotIn('raw_sha256',output['entries'][0])
            measure['scenes'].append(scene)
            with self.assertRaises(ValueError):
                normalization_manifest.build(measure,manifest,audit,Path('profile'))

    def test_white_span_gain_uses_black_subtracted_units(self):
        self.assertAlmostEqual(dcp_normalization.white_scale_gain(15871,13995,[512]*4),15871/13483)
        self.assertAlmostEqual(dcp_normalization.white_scale_gain(14335,14351,[2048]*4),14335/12303)
        for args in ((0,13995,[512]*4),(100,512,[512]*4),(100,13995,[512,513,512,512]),
                     (100,float('nan'),[512]*4)):
            with self.assertRaises(ValueError):
                dcp_normalization.white_scale_gain(*args)

    def test_diagonal_camera_calibration_cancels_with_reference_neutral(self):
        neutral = np.array([.53,1,.59])
        expected = np.diag(1/neutral)
        for gains in ([1,1,1.0017],[.9,1.1,1.2]):
            actual = dcp_normalization.camera_white_operator(neutral,[1.02,.98,1.1],np.diag(gains))
            np.testing.assert_allclose(actual,expected,atol=1e-14)
            # Already balanced samples must not be balanced a second time.
            np.testing.assert_allclose(actual @ np.diag(neutral),np.eye(3),atol=1e-14)

    def test_nondiagonal_calibration_maps_neutral_but_does_not_cancel(self):
        neutral=np.array([.53,1,.59])
        calibration=np.array([[1,.01,0],[0,1,.01],[.01,0,1]])
        actual=dcp_normalization.camera_white_operator(neutral,[1,1,1],calibration)
        np.testing.assert_allclose(actual @ neutral,np.ones(3),atol=1e-14)
        self.assertGreater(np.max(np.abs(actual-np.diag(1/neutral))),.001)

    def test_camera_white_operator_rejects_invalid_neutral_or_calibration(self):
        for neutral in ([0,1,1],[float('nan'),1,1],[-1,1,1]):
            with self.assertRaises(ValueError):
                dcp_normalization.camera_white_operator(neutral,[1,1,1],np.eye(3))
        with self.assertRaises(np.linalg.LinAlgError):
            dcp_normalization.camera_white_operator([1,1,1],[1,1,1],np.zeros((3,3)))

    def test_camera_variant_selection_rejects_missing_and_ambiguous(self):
        row={'raw':'tests/image/base/SCENE.CR3','variant':'specular_fixed'}
        self.assertEqual(dcp_normalization.select_unique({'images':[row]},'SCENE','specular_fixed'),row)
        for images in ([],[row,row]):
            with self.assertRaises(ValueError):
                dcp_normalization.select_unique({'images':images},'SCENE','specular_fixed')

    def test_dng_audit_reads_metadata_without_decoding_or_writing_pixels(self):
        from PIL import Image, TiffImagePlugin
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'synthetic.dng'
            tags = TiffImagePlugin.ImageFileDirectory_v2()
            tags[50730] = TiffImagePlugin.IFDRational(28, 100)
            tags[51109] = TiffImagePlugin.IFDRational(-1, 2)
            tags[50936] = 'Synthetic Neutral'
            Image.new('RGB', (2, 1), (5, 6, 7)).save(path, format='TIFF', tiffinfo=tags)
            before = path.read_bytes()
            with mock.patch.object(Image.Image, 'load', side_effect=AssertionError('No pixel decode')):
                result = dng_baseline.audit(path)
            self.assertAlmostEqual(result['ifd0']['baseline_exposure'], .28)
            self.assertAlmostEqual(result['ifd0']['baseline_exposure_offset'], -.5)
            self.assertEqual(result['ifd0']['profile_name'], 'Synthetic Neutral')
            self.assertEqual(result['sub_ifds'], [])
            self.assertEqual(path.read_bytes(), before)

    def test_negative_exposure_shoulder_endpoints_continuity_and_bounds(self):
        x = np.linspace(0, 1, 10001)
        for ev in (0, -.5, -2, -8):
            y = dcp_order.negative_exposure(x, ev)
            self.assertEqual(y[0], 0)
            self.assertAlmostEqual(y[-1], 1)
            self.assertTrue(np.all(np.diff(y) > 0))
            np.testing.assert_allclose(y[x <= .25], x[x <= .25]*2**ev)
            self.assertTrue(np.all((y >= 0) & (y <= 1)))
            eps = 1e-7
            values = dcp_order.negative_exposure(np.array([.25-eps, .25, .25+eps]), ev)
            np.testing.assert_allclose(np.diff(values)/eps, 2**ev, atol=2e-7)
        for ev in (1, float('nan'), -17):
            with self.assertRaises(ValueError):
                dcp_order.negative_exposure(x, ev)

    def test_encoded_look_identity_and_value_axis_only(self):
        rgb = np.array([[[.4, .2, .1], [.7, .7, .7], [0, 0, 0]]])
        table = np.tile([0., 1., 1.], (3, 4, 2, 1))
        np.testing.assert_allclose(dcp_order.look_table(rgb, (4, 2, 3), table.ravel(), 1), rgb, atol=1e-14)
        table[..., 2] = .5
        out = dcp_order.look_table(rgb, (4, 2, 3), table.ravel(), 1)
        expected_value = dcp_order.linearize(dcp_order.encode(rgb.max(axis=-1))*.5)
        np.testing.assert_allclose(out.max(axis=-1), expected_value, atol=1e-14)
        np.testing.assert_allclose(out[0, 0, 1]/out[0, 0, 0], .5, atol=1e-14)
        np.testing.assert_allclose(out[0, 0, 2]/out[0, 0, 0], .25, atol=1e-14)

    def test_single_value_division_ignores_look_encoding(self):
        rgb = np.random.default_rng(5).random((5, 6, 3))
        table = np.tile([0., .9, .7], (1, 4, 2, 1))
        np.testing.assert_allclose(dcp_order.look_table(rgb, (4, 2, 1), table.ravel(), 1),
                                   dcp_order.look_table(rgb, (4, 2, 1), table.ravel(), 0))

    def test_signed_gamut_bounded_identity_and_luminance(self):
        rgb = np.array([[-.1, .3, .8], [1.2, .5, -.2], [.2, .5, .8], [0, 0, 0]])
        out = dcp_order.signed_gamut(rgb)
        self.assertTrue(np.all((out >= 0) & (out <= 1)))
        weights = np.array([.2126, .7152, .0722])
        np.testing.assert_allclose(out @ weights, rgb @ weights, atol=1e-14)
        np.testing.assert_allclose(out[2:], rgb[2:], atol=1e-14)
        extreme = dcp_order.signed_gamut(np.array([[2., 3., 4.], [-1., -2., -3.]]))
        np.testing.assert_allclose(extreme, [[1, 1, 1], [0, 0, 0]], atol=1e-14)

    def test_order_ablation_is_observable_and_profile_is_restricted(self):
        rgb = np.array([[[.7, .3, .1]]])
        table = np.tile([0., 1., 1.], (3, 2, 2, 1))
        table[0, ..., 2], table[1, ..., 2], table[2, ..., 2] = .7, .9, 1.2
        lut = lambda x: dcp_order.look_table(x, (2, 2, 3), table.ravel(), 1)
        self.assertGreater(np.max(np.abs(lut(rgb*.5)-lut(rgb)*.5)), .001)
        with self.assertRaises(ValueError):
            dcp_order.require_profile({})

    def test_hue_stable_curve_preserves_relative_middle_for_all_permutations(self):
        import itertools
        rgb = np.array(list(itertools.permutations([.05, .2, .8])))
        points = [0, 0, .1, .3, .5, .8, 1, 1]
        out = dcp_curve.hue_stable(rgb, points)
        fraction = lambda a: (a-a.min(axis=-1, keepdims=True))/(a.max(axis=-1, keepdims=True)-a.min(axis=-1, keepdims=True))
        np.testing.assert_allclose(fraction(out), fraction(rgb), atol=1e-14)
        self.assertGreater(np.max(np.abs(out-dcp_curve.curve(rgb, points))), .01)

    def test_dcp_curve_gray_endpoints_and_identity(self):
        rgb = np.array([[0, 0, 0], [.2, .2, .2], [1, 1, 1], [.1, .3, .8]])
        for cubic in (False, True):
            np.testing.assert_allclose(dcp_curve.hue_stable(rgb, [0, 0, 1, 1], cubic), rgb)
            out = dcp_curve.hue_stable(rgb, [0, 0, .5, .7, 1, 1], cubic)
            np.testing.assert_allclose(out[:3, 0], out[:3, 1])
            np.testing.assert_allclose(out[[0, 2]], rgb[[0, 2]])

    def test_dcp_cubic_is_bounded_monotone_and_interpolates_knots(self):
        points = [0, 0, .1, .25, .3, .25, .8, .95, 1, 1]
        out = dcp_curve.curve(np.linspace(0, 1, 10001), points, True)
        self.assertTrue(np.all(np.diff(out) >= -1e-14))
        self.assertTrue(np.all((out >= 0) & (out <= 1)))
        np.testing.assert_allclose(dcp_curve.curve(np.array(points[::2]), points, True), points[1::2])
        with self.assertRaises(ValueError):
            dcp_curve.curve([.1], [0, 0, .5, 1.1, 1, 1])

    def test_dcp_cubic_accepts_local_decrease_without_overshoot(self):
        points = [0, 0, .5, .8, .6, .7999, 1, 1]
        out = dcp_curve.curve(np.linspace(.5, .6, 101), points, True)
        self.assertTrue(np.all((out >= .7999-1e-14) & (out <= .8+1e-14)))

    def test_hidden_dcp_changes_only_tag_identifier_both_endian(self):
        for endian, marker in (('<', b'II'), ('>', b'MM')):
            data = marker + struct.pack(endian+'HIH', 0x4352, 8, 1)
            data += struct.pack(endian+'HHII', 50940, 11, 4, 26) + b'\0'*4
            data += struct.pack(endian+'ffff', 0, 0, 1, 1)
            changed = dcp_curve.hide_curve(data)
            self.assertEqual(changed[:10], data[:10])
            self.assertEqual(changed[12:], data[12:])
            self.assertEqual(struct.unpack_from(endian+'H', changed, 10)[0], 65000)
            with self.assertRaises(ValueError):
                dcp_curve.hide_curve(changed)

    def test_diagnostic_gamut_is_bounded_and_keeps_in_gamut_values(self):
        rgb = np.array([[0, 0, 0], [1, 1, 1], [.2, .4, .8], [-.3, 2, .2], [3, 3, 3]])
        out = dcp_curve.gamut(rgb)
        self.assertTrue(np.isfinite(out).all())
        self.assertTrue(np.all((out >= -1e-7) & (out <= 1+1e-7)))
        np.testing.assert_allclose(out[:3], rgb[:3], atol=1e-7)

    def test_harmonized_audit_selects_requested_camera_profile(self):
        settings = {name: '0' for name in ('Exposure2012', 'Contrast2012', 'Highlights2012',
            'Shadows2012', 'Whites2012', 'Blacks2012', 'Texture', 'Clarity2012', 'Dehaze',
            'Vibrance', 'Saturation', 'Sharpness', 'LuminanceSmoothing', 'ColorNoiseReduction',
            'LensProfileEnable', 'AutoLateralCA')}
        settings.update(WhiteBalance='As Shot', CameraProfile='Camera Neutral', ToneCurveName2012='Linear')
        self.assertEqual(tonal_rendering.harmonized_warnings(settings, 'Camera Neutral'), [])
        self.assertEqual(len(tonal_rendering.harmonized_warnings(settings)), 1)

    def test_harmonized_audit_reports_missing_and_wrong_controls(self):
        settings = {name: '0' for name in ('Exposure2012', 'Contrast2012', 'Highlights2012',
            'Shadows2012', 'Whites2012', 'Blacks2012', 'Texture', 'Clarity2012', 'Dehaze',
            'Vibrance', 'Saturation', 'Sharpness', 'LuminanceSmoothing', 'ColorNoiseReduction',
            'LensProfileEnable', 'AutoLateralCA')}
        settings.update(WhiteBalance='As Shot', CameraProfile='Adobe Standard', ToneCurveName2012='Linear')
        self.assertEqual(tonal_rendering.harmonized_warnings(settings), [])
        settings['LensProfileEnable'] = '1'
        self.assertEqual(len(tonal_rendering.harmonized_warnings(settings)), 1)
        del settings['WhiteBalance']
        self.assertEqual(len(tonal_rendering.harmonized_warnings(settings)), 2)

    def test_harmonized_crop_rejects_unexpected_geometry(self):
        with self.assertRaises(ValueError):
            tonal_rendering.grid_crop_candidates(np.zeros((12, 10, 3), dtype=np.uint16),
                                                np.zeros((2, 2, 3)), 10, 10)

    def test_tonal_fit_recovers_known_gain_and_power(self):
        y = np.linspace(.005, .15, 100).reshape(10, 10)
        rgb = investigation.encode(np.repeat(y[..., None], 3, axis=-1))
        target = investigation.encode(np.repeat((1.5*y**.8)[..., None], 3, axis=-1))
        model = tonal_rendering.fit_model([(rgb, target)], True)
        self.assertAlmostEqual(model['gain'], 1.5, places=6)
        self.assertAlmostEqual(model['gamma'], .8, places=6)

    def test_tonal_identity_black_and_overshoot(self):
        rgb = np.array([[[0., 0., 0.], [.5, .5, .5], [1., 1., 1.]]])
        output, overshoot = tonal_rendering.apply_model(rgb, {'gain': 1., 'gamma': 1.})
        np.testing.assert_allclose(output, rgb, atol=1/65535)
        self.assertEqual(overshoot, 0)
        _, overshoot = tonal_rendering.apply_model(rgb, {'gain': 2., 'gamma': 1.})
        self.assertGreater(overshoot, 0)
    def test_white_normalization_gain_and_invalid_levels(self):
        self.assertAlmostEqual(white_levels.normalization_gain(16383, 13200, 512), 15871 / 12688)
        for values in ((512, 13200, 512), (16383, 512, 512), (float('nan'), 13200, 512)):
            with self.assertRaises(ValueError):
                white_levels.normalization_gain(*values)

    def test_fixed_highlight_mask_excludes_changed_dark_pixels(self):
        pixels = np.array([[[1., 1., 1.], [.1, .1, .1]]])
        mask = np.array([[True, False]])
        result = white_levels.highlight_metrics(pixels, mask)
        self.assertEqual(result['samples'], 1)
        self.assertEqual(result['all_channels_high_fraction'], 1)
        with self.assertRaises(ValueError):
            white_levels.highlight_metrics(pixels, np.zeros((1, 2), dtype=bool))

    def test_white_probe_float_transport_restores_integer_thresholds(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            path = folder / 'codes.f32'
            codes = np.array([[[1, 65534, 65535]]], dtype=np.float64) / 65535
            codes.astype('<f4').tofile(path)
            item = {'directory': '.', 'stages': {'encoded_srgb': {
                'file': path.name, 'sha256': analysis.file_hash(path), 'rows': 1, 'cols': 1}}}
            np.testing.assert_array_equal(white_levels.load_stage(folder, item, 'encoded_srgb'), codes)

    def test_canon_color_data_both_byte_orders(self):
        for endian, magic in [('<', b'II'), ('>', b'MM')]:
            words = [0] * 3778
            words[0] = 66
            words[0x69:0x6e] = [1938, 1024, 1024, 1733, 5200]
            header = magic + struct.pack(endian + 'HIHHHI', 42, 8, 1, 0x4001, 3, len(words))
            data = header + struct.pack(endian + 'II', 26, 0) + struct.pack(endian + str(len(words)) + 'H', *words)
            result = canon_metadata.tiff_color_data(data)
            self.assertEqual(result['version_word'], 66)
            self.assertEqual(result['candidate_words_not_certified']['0x69'], words[0x69:0x6e])
            with self.assertRaises(ValueError):
                canon_metadata.tiff_color_data(data[:-1])

    def test_canon_bmff_extents(self):
        payload = struct.pack('>I4sQ', 1, b'mdat', 20) + b'abcd'
        self.assertEqual(list(canon_metadata.boxes(payload, 0, len(payload))), [(b'mdat', 16, 20)])
        for bad in (b'abcd', struct.pack('>I4s', 100, b'mdat'), struct.pack('>I4s', 4, b'mdat')):
            with self.assertRaises(ValueError):
                list(canon_metadata.boxes(bad, 0, len(bad)))

    def test_canon_ctmd_sample_mapping(self):
        def box(kind, payload):
            return struct.pack('>I4s', 8 + len(payload), kind) + payload
        tiff = b'II' + struct.pack('<HIHHHII', 42, 8, 1, 0x4001, 3, 4, 26) + struct.pack('<I4H', 0, 66, 1, 2, 3)
        block = struct.pack('<II', 8 + len(tiff), 0x927c) + tiff
        sample = struct.pack('<IH6x', 12 + len(block), 8) + block
        def moov(offset):
            tables = (box(b'stsd', bytes(4) + struct.pack('>I', 1) + box(b'CTMD', b''))
                      + box(b'co64', bytes(4) + struct.pack('>IQ', 1, offset))
                      + box(b'stsz', bytes(4) + struct.pack('>II', len(sample), 1))
                      + box(b'stsc', bytes(4) + struct.pack('>IIII', 1, 1, 1, 1)))
            return box(b'moov', box(b'trak', box(b'mdia', box(b'minf', box(b'stbl', tables)))))
        prefix = moov(0)
        data = moov(len(prefix) + 8) + box(b'mdat', sample)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'test.CR3'
            path.write_bytes(data)
            result = canon_metadata.audit(path)
            self.assertEqual(result['color_data_records'][0]['version_word'], 66)
            self.assertIn('CTMD/sample0/record8', result['color_data_records'][0]['source'])

    def test_float_clipping_is_not_all_black(self):
        rgb = np.array([[[-.1, .2, .3], [0, 0, 0], [.1, .2, 1.1]]])
        result = canon_calibration.clipping(rgb)
        self.assertAlmostEqual(result['any_negative_fraction'], 1/3)
        self.assertAlmostEqual(result['all_nonpositive_fraction'], 1/3)
        self.assertAlmostEqual(result['any_over_one_fraction'], 1/3)
        with self.assertRaises(ValueError):
            canon_calibration.clipping(np.full((1, 1, 3), np.nan))

    def test_revision_suffixes_are_explicit_and_default_is_preserved(self):
        for options, current, previous in [([], '-IPR', None),
                (['--ipr-suffix=-IPR-2', '--previous-ipr-suffix=-IPR'], '-IPR-2', '-IPR')]:
            with mock.patch('sys.argv', ['analysis', '--archive', 'old.zip',
                                         '--output', 'new.json', *options]), \
                    mock.patch.object(analysis, 'analyse') as run:
                analysis.main()
                args = run.call_args.args[0]
                self.assertEqual(args.ipr_suffix, current)
                self.assertEqual(args.previous_ipr_suffix, previous)

    def test_revision_suffix_rejects_path_separator(self):
        with mock.patch('sys.argv', ['analysis', '--archive', 'old.zip',
                                     '--output', 'new.json', '--ipr-suffix=../bad']), \
                mock.patch.object(analysis, 'analyse') as run, \
                mock.patch('sys.stderr', io.StringIO()):
            with self.assertRaises(SystemExit):
                analysis.main()
            run.assert_not_called()

    def test_dcp_scalar_reader_both_byte_orders(self):
        for endian, magic in [('<', b'II'), ('>', b'MM')]:
            data = (magic + struct.pack(endian + 'HIH', 0x4352, 8, 2)
                    + struct.pack(endian + 'HHII', 51107, 4, 1, 0)
                    + struct.pack(endian + 'HHII', 51109, 10, 1, 38)
                    + struct.pack(endian + 'Iii', 0, -1, 2))
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'synthetic.dcp'
                path.write_bytes(data)
                tags = investigation.read_dcp(path)
                self.assertEqual(tags[51107], (0,))
                self.assertEqual(tags[51109], (-.5,))

    def test_hsv_round_trip_including_achromatic(self):
        values = np.vstack([np.random.default_rng(5).random((100, 3)),
                            np.zeros((1, 3)), np.ones((1, 3)), np.eye(3)])
        np.testing.assert_allclose(investigation.hsv_to_rgb(investigation.rgb_to_hsv(values)),
                                   values, atol=2e-15)

    def test_table_identity_and_hue_wrap(self):
        values = np.random.default_rng(7).random((4, 5, 3))
        table = np.tile([0., 1., 1.], (2, 4, 3, 1))
        actual = investigation.apply_linear_table(values, (4, 3, 2), table.ravel())
        np.testing.assert_allclose(actual, values, atol=2e-15)
        table[..., 0] = 360
        actual = investigation.apply_linear_table(values, (4, 3, 2), table.ravel())
        np.testing.assert_allclose(actual, values, atol=2e-15)

    def test_table_value_scaling(self):
        values = np.array([[[.4, .2, .1], [.1, .1, .1]]])
        table = np.tile([0., 1., .5], (1, 3, 2, 1))
        actual = investigation.apply_linear_table(values, (3, 2, 1), table.ravel())
        np.testing.assert_allclose(actual, values*.5, atol=2e-15)

    def test_prophoto_matrix_white_and_inverse(self):
        matrix = investigation.PROPHOTO_TO_XYZ
        np.testing.assert_allclose(matrix @ np.ones(3), [.9642, 1, .8249], atol=1e-14)
        np.testing.assert_allclose(np.linalg.inv(matrix) @ matrix, np.eye(3), atol=1e-14)

    def test_ciede2000_reference_cases(self):
        # Numerical reference pairs from Sharma/Wu/Dalal supplementary data:
        # https://hajim.rochester.edu/ece/sites/gsharma/ciede2000/dataNprograms/ciede2000testdata.txt
        cases = np.array([
            [50, 2.6772, -79.7751, 50, 0, -82.7485, 2.0425],
            [50, 3.1571, -77.2803, 50, 0, -82.7485, 2.8615],
            [50, 2.8361, -74.0200, 50, 0, -82.7485, 3.4412],
            [50, -1.3802, -84.2814, 50, 0, -82.7485, 1.0000],
            [50, 0, 0, 50, -1, 2, 2.3669],
            [50, -1, 2, 50, 0, 0, 2.3669],
            [50, 2.49, -.001, 50, -2.49, .0009, 7.1792],
            [50, 2.49, -.001, 50, -2.49, .001, 7.1792],
            [50, 2.49, -.001, 50, -2.49, .0011, 7.2195],
            [50, -.001, 2.49, 50, .0009, -2.49, 4.8045],
            [50, -.001, 2.49, 50, .001, -2.49, 4.8045],
            [50, -.001, 2.49, 50, .0011, -2.49, 4.7461],
            [50, 2.5, 0, 73, 25, -18, 27.1492],
            [60.2574, -34.0099, 36.2677, 60.4626, -34.1751, 39.4387, 1.2644],
            [6.7747, -.2908, -2.4247, 5.8714, -.0985, -2.2286, .6377],
            [2.0776, .0795, -1.135, .9033, -.0636, -.5514, .9082],
        ])
        actual = analysis.delta_e_2000(cases[:, :3], cases[:, 3:6])
        np.testing.assert_allclose(actual, cases[:, 6], atol=5e-5, rtol=0)
        np.testing.assert_allclose(actual, analysis.delta_e_2000(cases[:, 3:6], cases[:, :3]), atol=1e-10)

    def test_identity_and_srgb_white_black(self):
        rgb = np.array([[0, 0, 0], [1, 1, 1], [.25, .25, .25]])
        lab, _ = analysis.srgb_lab(rgb)
        np.testing.assert_allclose(lab[0], 0, atol=1e-10)
        np.testing.assert_allclose(lab[1], [100, 0, 0], atol=2e-5)
        np.testing.assert_allclose(analysis.delta_e_2000(lab, lab), 0)
        result = analysis.compare(rgb, rgb)
        self.assertEqual(result['delta_e00_mean'], 0)
        self.assertIsNone(result['hue_abs_circular_mean_degrees'])

    def test_single_16_bit_increment_is_not_lost(self):
        pixels = np.full((2, 3, 4), 32768, dtype=np.uint16)
        a = analysis.strict_region(pixels, [0, 0, 3, 2], 1)
        pixels[..., 0] += 1
        b = analysis.strict_region(pixels, [0, 0, 3, 2], 1)
        result = analysis.compare(a, b)
        self.assertGreater(result['delta_e00_mean'], 0)
        self.assertAlmostEqual(result['rgb_mae_normalized'], 1 / 65535 / 3, places=14)

    def test_roi_outside_image_is_rejected_instead_of_clamped(self):
        pixels = np.zeros((10, 10, 3), dtype=np.uint16)
        for coords in ([11, 0, 20, 5], [-1, 0, 5, 5], [0, 0, 11, 10], [4, 4, 4, 6]):
            with self.assertRaises(ValueError):
                analysis.strict_region(pixels, coords, 1)

    def test_comparison_sign_and_shape(self):
        darker = np.full((2, 2, 3), .25)
        lighter = np.full((2, 2, 3), .5)
        result = analysis.compare(darker, lighter)
        self.assertGreater(result['delta_lab_mean_lr_minus_ipr'][0], 0)
        self.assertGreater(result['lr_y_mean'], result['ipr_y_mean'])
        with self.assertRaises(ValueError):
            analysis.compare(darker, lighter[:1])

    def test_archive_identity_includes_entire_member(self):
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, 'w') as archive:
            archive.writestr('folder/X-IPR.tiff', b'full file with metadata and pixels')
        stream.seek(0)
        with zipfile.ZipFile(stream) as archive:
            same = analysis.archive_identity(archive, 'X-IPR.tiff', analysis.sha256(io.BytesIO(b'full file with metadata and pixels')))
            self.assertTrue(same['identical_bytes'])
            changed = analysis.archive_identity(archive, 'X-IPR.tiff', analysis.sha256(io.BytesIO(b'changed')))
            self.assertFalse(changed['identical_bytes'])
            with self.assertRaises(ValueError):
                analysis.archive_identity(archive, 'missing.tiff', '')


if __name__ == '__main__':
    unittest.main()
