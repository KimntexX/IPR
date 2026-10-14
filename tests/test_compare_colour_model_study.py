"""Diagnostic summary contracts, using synthetic memory rather than private images."""
import copy
import unittest

from compare_colour_model_study import summarize


def fixture():
    probe = {"sources": [], "records": []}
    reference = {"group": "couleur", "coverage": {"measured": 196, "diagnostic": 0, "excluded": 0}, "records": []}
    for scene in range(7):
        source = dict(scene=str(scene), sha256="raw", dcp="neutral", lcp_sha256="lens",
                      lens_geometry=True, lens_vignetting=True, CA=False, normalization=False)
        probe["sources"].append(source)
        declaration = dict(source_sha256="raw", dcp_profile="neutral", lcp_sha256="lens",
                           lens_geometry_applied=True, lens_vignetting_applied=True,
                           chromatic_aberration_applied=False, raw_normalization=False)
        for feature in ("vibrance", "saturation"):
            for value in (-100, -50, -25, 0, 25, 50, 100):
                zones = {z: dict(mean_chroma_delta=value / 1000,
                                mean_relative_chroma_delta=value / 1000, samples=20)
                         for z in ("low_chroma", "high_chroma")}
                metrics = dict(Y_ratio=1., mean_Y_neutral=.3, mean_Y_variant=.3,
                               pre_output_Y_ratio=1., pre_output_outside_srgb_percent=0.,
                               newly_clipped_samples=0, chroma_groups=zones)
                for engine in ("IPR", "LR"):
                    reference["records"].append(dict(scene=str(scene), feature=feature, value=value,
                        engine=engine, metrics=copy.deepcopy(metrics), provenance={"declaration": declaration.copy()}))
                for model in ("current", "A1_endpoint_rgb", "A2_adaptive_oklch", "B_pr598_prophoto"):
                    probe["records"].append(dict(scene=str(scene), feature=feature, value=value,
                                                model=model, metrics=copy.deepcopy(metrics)))
    return probe, reference


class StudyTests(unittest.TestCase):
    def test_complete_diagnostic_not_visual_validation(self):
        result = summarize(*fixture())
        self.assertEqual(len(result["pairs"]), 392)
        self.assertEqual(len(result["aggregates"]), 56)
        self.assertEqual(len(result["progressions"]), 56)
        self.assertTrue(all(r["monotonic_observed_means"] for r in result["progressions"]))
        self.assertTrue(all(not r["pixel_alignment"] for r in result["pairs"]))
        self.assertFalse(result["decision"]["production_modified"])
        self.assertFalse(result["decision"]["cleanup_eligible"])
        self.assertIsNone(result["decision"]["recommended_model"])

    def test_source_context_mismatch_rejected(self):
        for key, value in (("sha256", "other"), ("normalization", True), ("CA", True)):
            with self.subTest(key=key):
                probe, reference = fixture()
                probe["sources"][0][key] = value
                with self.assertRaisesRegex(ValueError, "Incompatible study context"):
                    summarize(probe, reference)

    def test_witness_mismatch_rejected(self):
        probe, reference = fixture()
        probe["records"][0]["metrics"]["Y_ratio"] += .01
        with self.assertRaisesRegex(ValueError, "probe/reference mismatch"):
            summarize(probe, reference)

    def test_incomplete_reference_rejected(self):
        probe, reference = fixture()
        reference["coverage"]["excluded"] = 1
        with self.assertRaisesRegex(ValueError, "complete audited"):
            summarize(probe, reference)

    def test_missing_or_duplicate_probe_rejected(self):
        for duplicate in (False, True):
            probe, reference = fixture()
            probe["records"].pop()
            if duplicate:
                probe["records"].append(copy.deepcopy(probe["records"][0]))
            with self.assertRaisesRegex(ValueError, "Incomplete or duplicated"):
                summarize(probe, reference)


if __name__ == "__main__":
    unittest.main()
