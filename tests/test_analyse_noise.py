"""Synthetic tests of photographic noise measurements; no private photos required."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import numpy as np

import analyse_noise as noise


class NoiseAnalysisTests(unittest.TestCase):
    def test_extra_activation_state_keeps_declared_value_and_mapping(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            neutral = root / "neutral.tiff"
            activation = root / "X-IPR-L005.tiff"
            neutral.write_bytes(b"neutral")
            activation.write_bytes(b"activation")
            def fake_metadata(path):
                return {"path": str(path), "bytes": path.stat().st_size, "icc": "sRGB",
                        "bits": [16]*3, "samples": 3, "orientation": 1, "settings": {}}
            extras = [{"image_id": "X", "engine": "IPR", "component": "luminance",
                       "value": 5, "path": activation}]
            with patch.object(noise, "metadata", side_effect=fake_metadata), \
                 patch.object(noise, "photographic_metadata", return_value={}):
                result = noise.inventory(root, ["X"], neutral_mappings={("X", "IPR"): neutral}, extra_states=extras)
            extra = result["scenes"]["X"]["variants"][-1]
            self.assertEqual(extra["observed_value"], 5)
            self.assertTrue(extra["extra_state"])
            self.assertTrue(extra["protocol_valid"])
            self.assertEqual(result["extra_state_mappings"][0]["path"], str(activation))

    def test_cli_rejects_extra_state_that_duplicates_standard_value(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.tiff"
            path.write_bytes(b"must not be read")
            output = Path(directory) / "result.json"
            run = subprocess.run([sys.executable, noise.__file__, "--image-id", "X", "--output", str(output),
                                  "--extra-state", "X", "luminance", "IPR", "25", str(path)], capture_output=True)
            self.assertNotEqual(run.returncode, 0)
            self.assertIn(b"use --variant-file", run.stderr)
            self.assertFalse(output.exists())

    def test_file_numbering(self):
        root = Path("corpus")
        self.assertEqual(noise.variant_path(root, "X", "couleur", "IPR", 1), root / "bruit/couleur/X-IPR.tiff")
        self.assertEqual(noise.variant_path(root, "X", "luminance", "LR", 4), root / "bruit/luminance/X-4.tif")
        self.assertEqual(noise.neutral_path(root, "X", "LR"), root / "neutre/X.tif")
        self.assertEqual(noise.VALUES, (25, 50, 75, 100))

    def test_audit_separates_state_from_sharpening_and_ai(self):
        entry = {"settings": {"LuminanceSmoothing": "25", "ColorNoiseReduction": "0", "Sharpness": "0"}}
        self.assertEqual(noise.audit_lr(entry, "luminance", 25), [])
        entry["settings"]["Sharpness"] = "40"
        self.assertEqual(noise.audit_lr(entry, "luminance", 25)[0]["type"], "nonzero_sharpening")
        entry["settings"]["ColorNoiseReduction"] = "25"
        self.assertIn("wrong_noise_state", {p["type"] for p in noise.audit_lr(entry, "luminance", 25)})
        entry["settings"]["DenoiseApplied"] = "true"
        self.assertIn("ai_settings_present", {p["type"] for p in noise.audit_lr(entry, "luminance", 25)})
        self.assertEqual(noise.ai_active({"DenoiseApplied": "false", "EnhanceAmount": "0"}), {})

    def test_settings_differences_ignore_control_but_not_exposure(self):
        first = {"settings": {"Sharpness": "0", "Exposure2012": "0", "ColorNoiseReduction": "0"}}
        second = {"settings": {"Sharpness": "0", "Exposure2012": "1", "ColorNoiseReduction": "100"}}
        self.assertEqual(set(noise.settings_differences(first, second)), {"Exposure2012"})

    def test_haar_mad_normalization_on_white_noise(self):
        rng = np.random.default_rng(913)
        plane = rng.normal(.3, .02, (1024, 1024))
        self.assertAlmostEqual(noise.noise_sigma(plane), .02, delta=.0002)
        self.assertAlmostEqual(noise.noise_sigma(plane * 3 + 10), .06, delta=.0006)
        self.assertEqual(noise.noise_sigma(np.full((16, 16), .4)), 0.)
        with self.assertRaises(ValueError):
            noise.noise_sigma(np.ones((1, 1)))

    def test_blur_preserves_constants_and_reduces_noise(self):
        flat = np.full((32, 32), .3)
        np.testing.assert_allclose(noise.blur(flat), flat)
        rng = np.random.default_rng(9)
        plane = rng.normal(.3, .02, (128, 128))
        self.assertLess(noise.noise_sigma(noise.blur(plane)), noise.noise_sigma(plane))

    def test_native_read_keeps_adjacent_uint16_pixels(self):
        pixels = np.arange(8 * 8 * 4, dtype=np.uint16).reshape(8, 8, 4)
        values = noise.read_patch(pixels, {"top": 1, "left": 2, "size": 4})
        np.testing.assert_allclose(values, pixels[1:5, 2:6, :3] / 65535., atol=1e-9)
        self.assertEqual(values.shape, (4, 4, 3))

    def test_identical_neutral_is_no_change_not_quality_score(self):
        rng = np.random.default_rng(7)
        image = np.clip(rng.normal(.3, .01, (64, 64, 3)), 0, 1).astype(np.float32)
        result = noise.compare_patch(image, image)
        self.assertTrue(result["identical_on_region"])
        self.assertEqual(result["MAE_linear_to_own_reference"], 0.)
        for value in result["ratios"].values():
            self.assertAlmostEqual(value, 1.)
        self.assertIsNone(noise.safe_ratio(0, 0))

    def test_luminance_and_chroma_proxies_respond_to_expected_noise(self):
        rng = np.random.default_rng(5)
        monochrome = np.repeat(rng.normal(.3, .01, (128, 128, 1)), 3, axis=2).astype(np.float32)
        metrics = noise.plane_metrics(monochrome)
        self.assertGreater(metrics["sigma_Y"], 0)
        self.assertEqual(metrics["sigma_chroma"], 0.)
        colored = monochrome.copy()
        colored[..., 0] += rng.normal(0, .02, (128, 128))
        self.assertGreater(noise.plane_metrics(colored)["sigma_chroma"], 0.)

    def test_region_selection_avoids_overlap_and_clipped_regions(self):
        candidates = [{"top": 0, "left": x, "size": 16, "mean_Y": .1,
                       "structure": x, "clipping": 0} for x in (0, 4, 16, 32)]
        candidates.append({"top": 0, "left": 48, "size": 16, "mean_Y": 1., "structure": 99, "clipping": 1.})
        selected = noise.select_regions(candidates, "flat", 3)
        self.assertEqual([r["left"] for r in selected], [0, 16, 32])

    def test_summary_does_not_fabricate_zero_noise_ratios(self):
        rng = np.random.default_rng(8)
        image = np.repeat(rng.normal(.3, .01, (32, 32, 1)), 3, axis=2).astype(np.float32)
        measurement = noise.compare_patch(image, image)
        summary = noise.summarize([dict(roi={"kind": "flat"}, **measurement)])
        self.assertEqual(summary["flat_sigma_Y_ratio"], 1.)
        self.assertIsNone(summary["flat_sigma_chroma_ratio"])
        self.assertIsNone(summary["detail_edge_gradient_ratio"])

    def test_missing_inputs_are_audited_not_synthesized(self):
        with tempfile.TemporaryDirectory() as directory:
            result = noise.inventory(Path(directory), ["X"])
        self.assertEqual(len(result["issues"]), 18)  # 2 neutrals + 16 states.
        self.assertEqual(result["scenes"]["X"]["neutrals"], {})

    def test_explicit_neutral_mapping_does_not_rename_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "X-IPR-5.tiff"
            source.write_bytes(b"operator declared neutral")
            entry = {"path": str(source), "bytes": source.stat().st_size,
                     "icc": "sRGB", "bits": [16, 16, 16], "samples": 3,
                     "orientation": 1, "settings": {}}
            with patch.object(noise, "metadata", return_value=entry):
                result = noise.inventory(root, ["X"], neutral_mappings={("X", "IPR"): source})
            self.assertEqual(result["scenes"]["X"]["neutrals"]["IPR"]["path"], str(source))
            self.assertTrue(result["scenes"]["X"]["neutrals"]["IPR"]["protocol_valid"])
            self.assertEqual(len(result["neutral_mapping_overrides"]), 1)
            self.assertTrue(source.exists())
            self.assertFalse(noise.neutral_path(root, "X", "IPR").exists())

    def test_cli_refuses_to_overwrite_measurements(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "measurement.json"
            output.write_text(json.dumps({"preserve": True}), encoding="utf-8")
            run = subprocess.run([sys.executable, noise.__file__, "--output", str(output)], capture_output=True)
            self.assertNotEqual(run.returncode, 0)
            self.assertEqual(json.loads(output.read_text(encoding="utf-8")), {"preserve": True})

    def test_missing_neutral_keeps_actual_nonzero_anchor(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "variant.tif"
            path.write_bytes(b"synthetic fixture, reader mocked")
            entry = {"path": str(path), "engine": "LR", "component": "luminance",
                     "value": 25, "observed_value": 50, "state_valid": False,
                     "protocol_valid": False, "bytes": path.stat().st_size,
                     "file_mtime_ns": path.stat().st_mtime_ns}
            result = {"scenes": {"X": {"neutrals": {}, "variants": [entry]}}}
            rng = np.random.default_rng(44)
            pixels = rng.integers(12000, 14000, (64, 64, 3), dtype=np.uint16)
            with patch.object(noise, "native_pixels", return_value=pixels):
                noise.analyse(result, 16)
            group = result["results"]["X"]["LR-luminance"]
            self.assertEqual(group["baseline_value"], 50)
            self.assertFalse(group["baseline_protocol_valid"])
            self.assertEqual(group["variants"][0]["value"], 50)
            self.assertEqual(group["variants"][0]["declared_value"], 25)

    def test_changed_input_since_audit_aborts_before_reading_pixels(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "variant.tif"
            path.write_bytes(b"replacement")
            entry = {"path": str(path), "engine": "IPR", "component": "couleur",
                     "observed_value": 25, "bytes": 0, "file_mtime_ns": 0}
            result = {"scenes": {"X": {"neutrals": {}, "variants": [entry]}}}
            with patch.object(noise, "native_pixels") as reader:
                with self.assertRaisesRegex(ValueError, "changed since metadata audit"):
                    noise.analyse(result, 16)
                reader.assert_not_called()


if __name__ == "__main__":
    unittest.main()
