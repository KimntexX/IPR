import unittest

import numpy as np

from study_noise_profile import MIN_TILES, PLANES, compatible, estimate, pooled_models
from study_noise_spectral import FEATURE_GROUPS

NAMES = FEATURE_GROUPS["combined"]


def d_tile(value, plane=0, kind="pair-difference"):
    return dict(plane=plane, bin=10, kind=kind,
                signature=dict(valid=True, features={name: value for name in NAMES}))


def parent(values, split="train"):
    tiles = [d_tile(v, plane) for v in values for plane in range(3)]
    return {"pairs": [dict(split=split, tiles=tiles)]}


def grid(n, bin_a=10, feature=0.0, hh=0.01, quiet=1, valid=1):
    """Probe table of n tiles: every plane with the same values."""
    t = {"x": np.zeros(n), "y": np.zeros(n), "bin_a": np.full(n, float(bin_a)), "bin_m": np.full(n, float(bin_a)),
         "quiet": np.full(n, float(quiet))}
    for plane in PLANES:
        t[f"{plane}_hh_a"] = np.full(n, hh)
        t[f"{plane}_hh_d"] = np.full(n, hh)
        t[f"{plane}_mid_a"] = np.full(n, 2 * hh)
        t[f"{plane}_mid_d"] = np.full(n, 2 * hh)
        t[f"{plane}_valid"] = np.full(n, float(valid))
        for name in NAMES:
            t[f"{plane}_{name}"] = np.full(n, feature)
    return t


def join(*tables):
    return {k: np.concatenate([t[k] for t in tables]) for k in tables[0]}


class NoiseProfileStudyTests(unittest.TestCase):
    def test_pooled_envelope_uses_training_differences_only(self):
        base = parent(np.linspace(-.05, .05, 50))
        reference = pooled_models(base)
        noisy = {"pairs": base["pairs"] + parent([10.0] * 50, "heldout")["pairs"]
                 + [dict(split="train", tiles=[d_tile(10.0, plane, "single-a") for plane in range(3) for _ in range(50)])]}
        self.assertEqual(pooled_models(noisy), reference)

    def test_texture_outside_the_envelope_is_excluded(self):
        pooled = pooled_models(parent(np.linspace(-.05, .05, 50)))
        t = join(grid(20, feature=0.0), grid(20, feature=5.0, hh=0.05))
        mask = compatible(t, "y", {}, pooled)
        self.assertTrue(mask[:20].all())
        self.assertFalse(mask[20:].any())

    def test_degenerate_signature_is_never_compatible(self):
        pooled = pooled_models(parent(np.linspace(-.05, .05, 50)))
        self.assertFalse(compatible(grid(20, valid=0), "y", {}, pooled).any())

    def test_profile_abstains_without_enough_compatible_tiles(self):
        pooled = pooled_models(parent(np.linspace(-.05, .05, 50)))
        textured = join(grid(MIN_TILES - 1, bin_a=9), grid(40, bin_a=9, feature=5.0, hh=0.05))
        rows = estimate(join(textured, grid(MIN_TILES, bin_a=10)), {}, pooled)["y"]
        self.assertIsNone(rows[9]["p_hh_raw"])
        self.assertAlmostEqual(rows[10]["p_hh_raw"], 0.01)

    def test_e_copies_another_bin_and_says_so(self):
        pooled = pooled_models(parent(np.linspace(-.05, .05, 50)))
        t = join(grid(20, bin_a=10), grid(3, bin_a=12, quiet=1))
        rows = estimate(t, {}, pooled)["y"]
        self.assertFalse(rows[10]["e_filled_from_other_bin"])
        self.assertTrue(rows[12]["e_filled_from_other_bin"])
        self.assertEqual(rows[12]["e_hh"], rows[10]["e_hh"])

    def test_truth_needs_quiet_tiles(self):
        pooled = pooled_models(parent(np.linspace(-.05, .05, 50)))
        rows = estimate(grid(40, quiet=0), {}, pooled)["y"]
        self.assertIsNone(rows[10]["truth_hh"])


if __name__ == "__main__":
    unittest.main()
