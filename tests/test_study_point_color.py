import unittest

import numpy as np

from study_point_color import (axes, change_of, crossing, desaturation_weight, estimate_source, neighbourhood,
                               point_state, profile, single_slider, slope)

CRS = "http://ns.adobe.com/camera-raw-settings/1.0/"
RDF = "http://www.w3.org/1999/02/22-rdf-syntax-ns#"


def xmp(point, variance="0.440000"):
    return ('<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="' + RDF + '">'
            '<rdf:Description rdf:about="" xmlns:crs="' + CRS + '">'
            f'<crs:PointColors><rdf:Seq><rdf:li>{point}</rdf:li></rdf:Seq></crs:PointColors>'
            f'<crs:ColorVariance><rdf:Seq><rdf:li>{variance}</rdf:li></rdf:Seq></crs:ColorVariance>'
            '</rdf:Description></rdf:RDF></x:xmpmeta>').encode()


def lab(L, C, h):
    return np.stack([L, C * np.cos(np.radians(h)), C * np.sin(np.radians(h))], axis=-1)


SOURCE = lab(np.array(0.65), np.array(0.12), np.array(50.0))


def synthetic(n=60000, half=20.0, seed=1):
    """Colours around the source and their weight under a known hue falloff (flat on the other axes)."""
    rng = np.random.default_rng(seed)
    L = rng.uniform(0.55, 0.75, n)
    C = 0.12 * np.exp(rng.uniform(-0.2, 0.2, n))
    h = 50.0 + rng.uniform(-80, 80, n)
    lab0 = lab(L, C, h)
    w = np.clip(1 - np.abs(h - 50.0) / (2 * half), 0, 1)   # 0.5 at +/- half
    return lab0, w


class PointColorStudyTests(unittest.TestCase):
    def test_state_of_an_export(self):
        point = ("0.548111, 0.502314, 0.161747, 0.000000, -1.000000, 0.000000, 0.250000, 0.000000, 0.333333, "
                 "0.666667, 1.000000, 0.000000, 0.322314, 0.682314, 1.000000, 0.000000, 0.258857, 0.618857, 1.000000")
        state = point_state(xmp(point, "0.000000"))
        self.assertEqual(state, {"source": (0.548111, 0.502314, 0.161747), "hue": 0, "saturation": -100,
                                 "luminance": 0, "range": 25, "variance": 0})
        self.assertIsNone(point_state(xmp(", ".join(["-1.000000"] * 19))))
        self.assertIsNone(single_slider(state))
        self.assertEqual(single_slider({**state, "range": 50}), ("saturation", -100))

    def test_weight_source_and_half_width(self):
        lab0, w = synthetic()
        lab1 = lab0.copy()
        lab1[:, 1:] *= (1 - w)[:, None]
        measured = desaturation_weight(lab0, lab1)
        self.assertTrue(np.allclose(measured, w, atol=1e-9))
        source = estimate_source(lab0, measured)
        self.assertLess(np.linalg.norm(source - SOURCE), 0.01)
        report = neighbourhood(lab0, measured, source)
        low, high = report["hue"]["half_width_0_5"]
        self.assertAlmostEqual(low, 20.0, delta=2.5)
        self.assertAlmostEqual(high, 20.0, delta=2.5)
        self.assertAlmostEqual(report["hue"]["reach_0_05"][1], 38.0, delta=3.0)

    def test_greys_and_clipped_pixels_have_no_weight(self):
        lab0 = np.array([[0.5, 0.001, 0.0], [0.5, 0.1, 0.0]])
        lab1 = np.array([[0.5, 0.0, 0.0], [np.nan, np.nan, np.nan]])
        self.assertTrue(np.all(np.isnan(desaturation_weight(lab0, lab1))))

    def test_crossing_without_a_fall(self):
        centres = np.array([-10.0, -5.0, 0.0, 5.0, 10.0])
        self.assertEqual(crossing(centres, np.array([1.0, 1.0, 1.0, 1.0, 1.0]), 0.5), [None, None])
        centres, mean, _ = profile(np.zeros(100), np.ones(100), np.array([-1.0, 1.0]))
        self.assertEqual(mean.tolist(), [1.0])

    def test_responses_at_weight_one(self):
        lab0, w = synthetic()
        L, C, h = lab0[:, 0], np.hypot(lab0[:, 1], lab0[:, 2]), np.degrees(np.arctan2(lab0[:, 2], lab0[:, 1]))
        turned = lab(L, C, h + 30.0 * w)
        self.assertAlmostEqual(slope(w, change_of("hue", lab0, turned, SOURCE)), 30.0, places=6)
        lighter = lab(L * np.exp(0.2 * w), C, h)
        self.assertAlmostEqual(slope(w, change_of("luminance", lab0, lighter, SOURCE)), 0.2, places=6)
        d = axes(lab0, SOURCE)["hue"]
        spread = lab(L, C, 50.0 + d * (1 + 0.6 * w))
        gains = change_of("variance", lab0, spread, SOURCE)
        self.assertAlmostEqual(slope(w, gains[:, 0]), 0.6, places=4)
        self.assertIsNone(slope(np.zeros(10), np.ones(10)))


if __name__ == "__main__":
    unittest.main()
