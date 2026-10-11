"""Small deterministic tests for the TIFF corpus analyser, not the render engine."""

import tempfile
import unittest
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import analyse_color_mixer as mixer


class MixerAnalysisTests(unittest.TestCase):
    def test_neutral_path_supports_live_neutre_and_archive_layouts(self):
        from unittest.mock import patch
        with patch.object(Path, "exists", return_value=False):
            self.assertEqual(mixer.neutral_path("X", "LR"), mixer.ROOT / "neutre" / "X.tif")
        with patch.object(Path, "exists", return_value=True):
            self.assertEqual(mixer.neutral_path("X", "IPR"), mixer.ROOT / "X-IPR.tiff")

    def test_intermediate_numbering_and_metadata_audit(self):
        for index, component in enumerate(mixer.COMPONENTS):
            for offset, value in enumerate((-50, -25, 25, 50), 1):
                number = index * 4 + offset
                self.assertEqual(mixer.variant_state(number, "intermediate"), (component, value))
                self.assertEqual(mixer.expected_setting("cyan", number, "intermediate"),
                                 {component.capitalize() + "AdjustmentAqua": value})
        with self.assertRaises(ValueError):
            mixer.variant_state(13, "intermediate")
        entry = {"engine": "LR", "number": 12, "path": "X-12.tif", "icc": "sRGB",
                 "active_mixer": {"LuminanceAdjustmentPurple": "50"}, "settings": {}}
        result = {"reference": {"path": "X.tif", "active_mixer": {}, "icc": "sRGB", "settings": {}},
                  "bands": {"violet": {"entries": [entry]}}, "validation_mode": "intermediate",
                  "ipr_mixer_version": "eight"}
        self.assertEqual(mixer.audit(result), [])
        entry["active_mixer"]["LuminanceAdjustmentPurple"] = "100"
        self.assertEqual(mixer.audit(result)[0]["type"], "wrong_variant_settings")

    def test_intermediate_inventory_uses_twelve_states(self):
        from unittest.mock import patch
        with patch.object(mixer, "metadata", return_value={"icc": "sRGB"}), \
             patch.object(Path, "exists", return_value=True), \
             patch.object(Path, "glob", return_value=[]):
            result = mixer.inventory(["red"], "X", "intermediate")
        entries = result["bands"]["red"]["entries"]
        self.assertEqual(len(entries), 24)
        self.assertEqual([e["number"] for e in entries[:12]], list(range(1, 13)))
        self.assertEqual(result["validation_mode"], "intermediate")

    def test_mean_progression_includes_neutral_and_detects_reversal(self):
        records = [{"component": "hue", "engine": "IPR", "value": value,
                    "core_hue_delta": value / 10.} for value in (-50, -25, 25, 50)]
        result = mixer.progression(records, "hue", "IPR")
        self.assertTrue(result["usable"])
        self.assertTrue(result["strictly_increasing_mean"])
        self.assertEqual([p["value"] for p in result["points"]], [-50, -25, 0, 25, 50])
        records[1]["core_hue_delta"] = 1.
        self.assertFalse(mixer.progression(records, "hue", "IPR")["monotonic_mean"])
        records[1]["metadata_valid"] = False
        self.assertIsNone(mixer.progression(records, "hue", "IPR")["monotonic_mean"])

    def test_progression_missing_neutral_or_empty_core_is_not_validated(self):
        records = [{"component": "luminance", "engine": "IPR", "value": value}
                   for value in (-50, -25, 25, 50)]
        result = mixer.progression(records, "luminance", "IPR")
        self.assertFalse(result["usable"])
        self.assertIsNone(result["monotonic_mean"])
        self.assertEqual(len(result["points"]), 4)

    def test_intermediate_analysis_measures_all_states_and_rejects_flat_response(self):
        from argparse import Namespace
        from unittest.mock import patch
        pixels = np.full((3, 4, 3), (180, 20, 20), dtype=np.uint8)
        reference, ipr_reference = mixer.ROOT / "X.tif", mixer.ROOT / "X-IPR.tiff"
        existing = {reference, ipr_reference} | {
            mixer.variant_path("red", n, "IPR", "X") for n in range(1, 13)}
        fake_stat = Namespace(st_size=36, st_mtime_ns=1)
        args = Namespace(image_id="X", bands=["red"], sample_long_edge=100,
                         without_ipr_reference=False)
        result = {"reference": {"path": str(reference), "active_mixer": {},
                                "icc": "sRGB", "settings": {}},
                  "bands": {"red": {"entries": []}}, "ipr_mixer_version": "eight",
                  "validation_mode": "intermediate"}
        with patch.object(Path, "exists", lambda path: path in existing), \
             patch.object(Path, "stat", lambda path: fake_stat), \
             patch.object(mixer, "metadata", return_value={"path": str(ipr_reference), "icc": "sRGB"}), \
             patch.object(mixer, "native_pixels", return_value=pixels):
            mixer.analyse(args, result)
        group = result["results"]["red"]
        self.assertEqual(len(group["variants"]), 12)
        self.assertEqual([(r["component"], r["value"]) for r in group["variants"]],
                         [mixer.variant_state(n, "intermediate") for n in range(1, 13)])
        for component in mixer.COMPONENTS:
            self.assertTrue(group["pairs"][component]["IPR"]["endpoints_identical_on_sample"])
            self.assertTrue(group["progression"][component]["IPR"]["monotonic_mean"])
            self.assertFalse(group["progression"][component]["IPR"]["strictly_increasing_mean"])

    def test_file_mapping(self):
        self.assertEqual(mixer.variant_path("red", 1, "LR", "X").name, "X.tif")
        self.assertEqual(mixer.variant_path("red", 6, "IPR", "X").name, "X-IPR-6.tiff")
        self.assertEqual(mixer.expected_setting("green", 3), {"SaturationAdjustmentGreen": -100})
        self.assertEqual(mixer.expected_setting("yellow", 6), {"LuminanceAdjustmentYellow": 100})
        self.assertEqual(mixer.expected_setting("violet", 1), {"HueAdjustmentPurple": -100})
        self.assertEqual(mixer.ANALYSIS_CENTERS["violet"], 270.)
        self.assertIn("violet", mixer.CENTERS)
        self.assertNotIn("violet", mixer.LEGACY_CENTERS)
        key = ("blue", 6, "LR")
        try:
            mixer.VARIANT_OVERRIDES[key] = Path("elsewhere.tif")
            self.assertEqual(mixer.variant_path("blue", 6, "LR", "X"), Path("elsewhere.tif"))
        finally:
            mixer.VARIANT_OVERRIDES.pop(key, None)

    def test_cli_inspection_without_colour_argument(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sample.tif"
            Image.fromarray(np.zeros((2, 2, 3), dtype=np.uint8)).save(path)
            completed = subprocess.run([sys.executable, mixer.__file__, "--inspect-file", str(path)],
                                       check=True, text=True, capture_output=True)
            result = json.loads(completed.stdout)
            self.assertEqual(result["size"], [2, 2])
            self.assertEqual(result["active_mixer"], {})

    def test_audit_marks_incomplete_or_incompatible_states(self):
        neutral = {"path": "neutral.tif", "active_mixer": {}, "icc": "sRGB", "settings": {"Exposure2012": "0"}}
        wrong = {"engine": "LR", "number": 6, "path": "wrong.tif", "icc": "sRGB",
                 "active_mixer": {"LuminanceAdjustmentGreen": "-100"},
                 "settings": {"Exposure2012": "1"}}
        result = {"reference": neutral, "bands": {"green": {"entries": [wrong, {"missing": True, "path": "missing.tif"}]}}}
        kinds = {issue["type"] for issue in mixer.audit(result)}
        self.assertEqual(kinds, {"wrong_variant_settings", "non_mixer_settings_differ", "missing_variant"})
        wrong["icc"] = "Adobe RGB"
        with self.assertRaises(ValueError):
            mixer.audit(result)

    def test_native_sampling_and_alignment(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sample.tif"
            pixels = np.arange(5 * 6 * 3, dtype=np.uint8).reshape(5, 6, 3)
            Image.fromarray(pixels).save(path, compression="raw")
            mapped = mixer.native_pixels(path)
            try:
                np.testing.assert_array_equal(mapped, pixels)
                np.testing.assert_allclose(mixer.sample(mapped, 2, 4, 5, 1, 1),
                                           pixels[1:5:2, 1:6:2] / 255., atol=1e-7)
            finally:
                # Windows cannot remove a TIFF while its memory map is open.
                mapped._mmap.close()
            with self.assertRaises(ValueError):
                mixer.choose_crop(pixels, pixels[:2, :2], 1)

    def test_colour_math(self):
        primaries = np.array([[[1., 0., 0.], [0., 1., 0.], [0., 0., 1.],
                              [1., 1., 0.], [.5, .5, .5]]], dtype=np.float32)
        hue, saturation, _ = mixer.hsv(primaries)
        np.testing.assert_allclose(hue, [[0., 120., 240., 60., 0.]])
        np.testing.assert_allclose(saturation, [[1., 1., 1., 1., 0.]])
        self.assertAlmostEqual(float(mixer.circular_delta(5., 355.)), 10.)
        self.assertAlmostEqual(float(mixer.circular_delta(355., 5.)), -10.)
        np.testing.assert_allclose(mixer.lab(np.ones((1, 1, 3), dtype=np.float32)),
                                   [[[100., 0., 0.]]], atol=.0001)
        np.testing.assert_allclose(mixer.linearize(np.array([0., .04045, 1.])),
                                   [0., .003130805, 1.], atol=1e-7)

    def test_analysis_can_explicitly_exclude_incompatible_ipr_neutral(self):
        from argparse import Namespace
        from unittest.mock import patch
        neutral = np.full((3, 4, 3), 128, dtype=np.uint8)
        variants = {mixer.variant_path("red", n, "IPR", "X") for n in range(1, 7)}
        reference = mixer.ROOT / "X.tif"
        default_ipr = mixer.ROOT / "X-IPR.tiff"
        existing = variants | {reference, default_ipr}
        fake_stat = Namespace(st_size=36, st_mtime_ns=1)
        args = Namespace(image_id="X", bands=["red"], sample_long_edge=100,
                         without_ipr_reference=True)
        result = {"reference": {"path": str(reference), "active_mixer": {},
                                "icc": "sRGB", "settings": {}},
                  "bands": {"red": {"entries": []}}, "ipr_mixer_version": "eight"}

        def pixels(path):
            self.assertNotEqual(path, default_ipr, "Excluded neutral must never be read")
            return neutral

        with patch.object(Path, "exists", lambda path: path in existing), \
             patch.object(Path, "stat", lambda path: fake_stat), \
             patch.object(mixer, "native_pixels", side_effect=pixels):
            mixer.analyse(args, result)
        self.assertFalse(result["measurement"]["ipr_neutral_available"])
        self.assertTrue(result["measurement"]["ipr_neutral_explicitly_excluded"])
        self.assertNotIn("ipr_reference", result)
        for record in result["results"]["red"]["variants"]:
            self.assertNotIn("core_Y_ratio", record)
            self.assertNotIn("core_hue_delta", record)
        self.assertIn("IPR", result["results"]["red"]["pairs"]["hue"])


    def test_legacy_and_new_band_identity_and_supports(self):
        self.assertEqual(mixer.support_widths("blue", "legacy-seven"), (60., 60.))
        self.assertEqual(mixer.support_widths("blue", "eight"), (60., 30.))
        self.assertEqual(mixer.support_widths("magenta", "legacy-seven"), (60., 60.))
        self.assertEqual(mixer.support_widths("magenta", "eight"), (30., 60.))
        self.assertEqual(mixer.support_widths("violet", "eight"), (30., 30.))
        result = {"reference": {"path": "neutral.tif", "active_mixer": {},
                                "icc": "sRGB", "settings": {}},
                  "bands": {"violet": {"entries": []}}}
        self.assertFalse(mixer.band_identified("violet", result))
        self.assertEqual(mixer.audit(result)[0]["type"], "band_identity_mismatch")
        result["ipr_mixer_version"] = "eight"
        self.assertTrue(mixer.band_identified("violet", result))
        self.assertEqual(mixer.audit(result), [])


if __name__ == "__main__":
    unittest.main()
