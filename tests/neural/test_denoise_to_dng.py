import math
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

import numpy as np

from denoise_to_dng import CORE, law_for_iso, tiles, write_cfa_dng


def bank():
    fits = []
    for iso, slope, read in ((1600, 3., 40.), (6400, 12., 500.), (12800, None, 2000.), (51200, 90., 32000.)):
        for plane in range(4):
            fits.append(dict(iso=iso, plane=plane, fit={'shot_slope_dn': slope}, reference_dark_variance_dn2=read))
    return dict(fits=fits)


class DenoiseToDngTests(unittest.TestCase):
    def test_law_is_measured_when_possible_and_log_interpolated_otherwise(self):
        rows, provenance = law_for_iso(bank(), 6400)
        self.assertEqual(rows[0], (12., 500.))
        self.assertEqual(provenance, 'measured')
        rows, provenance = law_for_iso(bank(), 12800)
        t = math.log(2)/math.log(8)
        self.assertAlmostEqual(rows[1][0], math.exp((1-t)*math.log(12)+t*math.log(90)), places=9)
        self.assertEqual(rows[1][1], 2000.)
        self.assertIn('6400 and 51200', provenance)
        for outside in (800, 102400):
            with self.assertRaises(ValueError):
                law_for_iso(bank(), outside)

    def test_tiles_cover_every_cell_once(self):
        covered = np.zeros((2319, 3479), int)
        for y0, y1, x0, x1 in tiles(*covered.shape):
            self.assertLessEqual(y1-y0, CORE)
            covered[y0:y1, x0:x1] += 1
        self.assertTrue((covered == 1).all())

    def test_written_dng_keeps_the_mosaic_and_the_tags(self):
        import tifffile
        raw = SimpleNamespace(rgb_xyz_matrix=np.array([[.7, -.1, -.05], [-.4, 1.2, .2], [-.05, .1, .6], [0, 0, 0]]),
                              camera_whitebalance=[2., 1., 1.6, 0.], sizes=SimpleNamespace(flip=0))
        mosaic = (np.arange(64*96, dtype=np.uint16).reshape(64, 96) % 4000)+2048
        meta = dict(make='Canon', model='Canon EOS R6 Mark III', iso=6400)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'trial.dng'
            write_cfa_dng(path, mosaic, raw, meta, [2048.]*4, 16383.)
            with tifffile.TiffFile(path) as dng:
                page = dng.pages[0]
                np.testing.assert_array_equal(page.asarray(), mosaic)
                tags = page.tags
                self.assertEqual(tags['PhotometricInterpretation'].value, 32803)
                self.assertEqual(tuple(tags[33422].value), (0, 1, 1, 2))
                self.assertEqual(tags[50717].value, 16383)
                self.assertEqual(tags[34855].value, 6400)
                neutral = tags[50728].value
                self.assertAlmostEqual(neutral[0]/neutral[1], .5, places=4)


if __name__ == '__main__':
    unittest.main()
