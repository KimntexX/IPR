"""Native, immutable group campaigns; no private corpus needed."""
import copy
import json
from pathlib import Path
import struct
import tempfile
import unittest
import os
import subprocess
import sys
from unittest.mock import patch

import numpy as np
from PIL import ImageCms

from analysis import audit, campaign, catalog, groups, inputs, metrics, native, render
from analysis.cli import main


def write_tiff(path, array, endian="<", xmp=None, with_icc=True, orientation=1, declaration=None):
    """Minimal native TIFF fixture, intentionally not Pillow RGB 8-bit saving."""
    array = np.asarray(array)
    height, width, channels = array.shape
    icc = ImageCms.ImageCmsProfile(ImageCms.createProfile("sRGB")).tobytes()
    tags = [(256, 4, 1, width), (257, 4, 1, height), (258, 3, channels, (16,) * channels),
            (259, 3, 1, 1), (262, 3, 1, 2), (273, 4, 1, 0), (274, 3, 1, orientation),
            (277, 3, 1, channels), (278, 4, 1, height), (279, 4, 1, array.size * 2),
            (284, 3, 1, 1)]
    if with_icc:
        tags.append((34675, 7, len(icc), icc))
    if declaration:
        payload = json.dumps(declaration).encode("utf-8") + b"\0"
        tags.append((270, 2, len(payload), payload))
    if xmp:
        tags.append((700, 1, len(xmp), xmp))
    tags.sort()
    start = 8 + 2 + 12 * len(tags) + 4
    extra = bytearray()
    entries = []
    for tag, kind, count, value in tags:
        if tag == 273:
            payload = b"OFFSET"
        elif isinstance(value, bytes):
            payload = value
        elif isinstance(value, tuple):
            payload = struct.pack(endian + "H" * len(value), *value)
        else:
            payload = struct.pack(endian + ("H" if kind == 3 else "I"), value)
        if tag == 273:
            entries.append((tag, kind, count, None))
        elif len(payload) <= 4:
            entries.append((tag, kind, count, payload.ljust(4, b"\0")))
        else:
            entries.append((tag, kind, count, struct.pack(endian + "I", start + len(extra))))
            extra.extend(payload)
    offset = start + len(extra)
    header = (b"II" if endian == "<" else b"MM") + struct.pack(endian + "HIH", 42, 8, len(tags))
    for tag, kind, count, payload in entries:
        header += struct.pack(endian + "HHI", tag, kind, count) + (struct.pack(endian + "I", offset) if payload is None else payload)
    path.write_bytes(header + b"\0" * 4 + extra + array.astype(endian + "u2").tobytes())


def fixture_record(root, feature="saturation", value=50):
    base = root / "neutral.tiff"
    changed = root / "variant.tiff"
    pixels = np.full((32, 32, 3), [28000, 25000, 22000], dtype=np.uint16)
    write_tiff(base, pixels)
    write_tiff(changed, pixels + np.array([500, 0, -500]))
    settings = {key: 0 for key in audit.ZERO_IPR}
    settings.update(temperature=5200, mixerHue=[0] * 8, mixerSaturation=[0] * 8,
                    mixerLuminance=[0] * 8, grading=[{"hue": 0, "saturation": 0} for _ in range(3)],
                    toneCurves=[[], [], [], []], sharpeningRadius=1, sharpeningMasking=0)
    variant = copy.deepcopy(settings)
    variant[feature] = value
    return {"scene": "S", "engine": "IPR", "feature": feature, "value": value,
            "file": str(changed), "reference": str(base), "expected_sha256": native.sha256(changed),
            "reference_sha256": native.sha256(base), "settings": variant, "reference_settings": settings}


class GroupAnalysisTests(unittest.TestCase):
    def test_base_ignores_ipr_review_tiffs(self):
        """IPR TIFFs in base serve visual review; IPR states are rendered in memory."""
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory).resolve()
            (base / "neutre").mkdir()
            folder = base / "couleur/saturation"
            folder.mkdir(parents=True)
            record = fixture_record(base)
            declaration = {"ipr_test_schema": 1, "scene": "S", "group": "couleur", "feature": "neutral",
                           "value": 0, "reference": "neutre/S-IPR-neutre-couleur.tiff",
                           "settings": record["reference_settings"], "source_sha256": "test"}
            pixels = np.full((32, 32, 3), 25000, dtype=np.uint16)
            write_tiff(base / "neutre/S-IPR-neutre-couleur.tiff", pixels, declaration=declaration)
            write_tiff(folder / "S-IPR-saturation-50.tiff", pixels + 100,
                       declaration={**declaration, "feature": "saturation", "value": 50})
            write_tiff(folder / "S-IPR-saturation-25.tiff", pixels + 50)  # no declaration
            self.assertEqual(inputs.base_records(base, ["couleur"]), [])

    def test_group_entrypoints_share_cli(self):
        for group in catalog.GROUPS:
            wrapper = Path(__file__).parent / ("analyse_" + group + ".py")
            result = subprocess.run([sys.executable, str(wrapper), "--help"], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("--base", result.stdout)
            result = subprocess.run([sys.executable, str(wrapper), "--group", "raw"], capture_output=True)
            self.assertNotEqual(result.returncode, 0)

    def test_lr_manual_tint_uses_zero_support_not_as_shot_plus_five(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory).resolve()
            folder = base / "couleur/nuance"
            folder.mkdir(parents=True)
            (base / "neutre").mkdir()
            pixels = np.full((8, 8, 3), 25000, dtype=np.uint16)
            def xmp(tint, wb="Custom"):
                fields = {key: "0" for key in ("Exposure2012", "Contrast2012", "Shadows2012", "Blacks2012", "Highlights2012", "Whites2012", "Vibrance", "Saturation", "LuminanceSmoothing", "ColorNoiseReduction", "Sharpness")}
                fields.update(Temperature="5200", Tint=str(tint), WhiteBalance=wb, CameraProfile="Camera Neutral")
                attrs = " ".join('crs:' + key + '=\"' + value + '\"' for key, value in fields.items())
                return ('<root xmlns:crs=\"' + native.CRS + '\" ' + attrs + '/>').encode()
            write_tiff(base / "neutre/S-LR-AsShot.tif", pixels, xmp=xmp(5, "As Shot"))
            write_tiff(folder / "S-5.tif", pixels, xmp=xmp(0))
            write_tiff(folder / "S-6.tif", pixels + 50, xmp=xmp(25))
            records = inputs.base_records(base, ["couleur"])
            state = next(r for r in records if r["value"] == 25)
            self.assertEqual(Path(state["reference"]).name, "S-5.tif")
            result = campaign.analyse("couleur", records, step=1)
            self.assertEqual(result["coverage"]["measured"], 2)
            write_tiff(folder / "S-duplicate.tif", pixels + 1, xmp=xmp(0))
            with self.assertRaises(ValueError):
                inputs.base_records(base, ["couleur"])

    def test_mixer_components_share_the_band_folder(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory).resolve()
            folder = base / "color-mixer/red"
            folder.mkdir(parents=True)
            (base / "neutre").mkdir()
            pixels = np.full((8, 8, 3), 25000, dtype=np.uint16)
            def xmp(**changed):
                fields = {key: "0" for key in ("HueAdjustmentRed", "SaturationAdjustmentRed", "LuminanceAdjustmentRed")}
                fields.update(changed, WhiteBalance="As Shot", CameraProfile="Camera Neutral")
                attrs = " ".join('crs:' + key + '=\"' + value + '\"' for key, value in fields.items())
                return ('<root xmlns:crs=\"' + native.CRS + '\" ' + attrs + '/>').encode()
            write_tiff(base / "neutre/S.tif", pixels, xmp=xmp())
            write_tiff(folder / "S-2.tif", pixels + 50, xmp=xmp(HueAdjustmentRed="+50"))
            write_tiff(folder / "S-3.tif", pixels + 25, xmp=xmp(SaturationAdjustmentRed="-25"))
            records = inputs.base_records(base, ["melangeur"])
            self.assertEqual(sorted((r["feature"], r["value"]) for r in records),
                             [("mixer-red-hue", 50), ("mixer-red-saturation", -25)])
            self.assertTrue(all(Path(r["reference"]).name == "S.tif" for r in records))

    def test_grain_size_and_roughness_do_not_break_the_neutral_pairing(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory).resolve()
            folder = base / "effets/grain"
            folder.mkdir(parents=True)
            (base / "neutre").mkdir()
            pixels = np.full((8, 8, 3), 25000, dtype=np.uint16)
            def xmp(**changed):
                fields = {"GrainAmount": "0", "Texture": "0", "WhiteBalance": "As Shot", "CameraProfile": "Camera Neutral"}
                fields.update(changed)
                attrs = " ".join('crs:' + key + '=\"' + value + '\"' for key, value in fields.items())
                return ('<root xmlns:crs=\"' + native.CRS + '\" ' + attrs + '/>').encode()
            write_tiff(base / "neutre/S.tif", pixels, xmp=xmp())
            write_tiff(folder / "S-2.tif", pixels + 50, xmp=xmp(GrainAmount="+50", GrainSize="25", GrainFrequency="50"))
            records = inputs.base_records(base, ["effets"])
            self.assertEqual([(r["feature"], r["value"], Path(r["reference"]).name) for r in records], [("grain", 50, "S.tif")])

    def test_float_radius_declaration_tolerance_does_not_hide_wrong_state(self):
        record = {"feature": "sharpening-radius", "value": .3,
                  "settings": {"sharpeningRadius": .30000001192092896, "sharpeningAmount": 50},
                  "reference_settings": {"sharpeningRadius": 1, "sharpeningAmount": 50}, "expected_sha256": "x"}
        self.assertNotIn("declared_value_mismatch", audit.ipr_state(record)[0])
        record["settings"]["sharpeningRadius"] = .31
        self.assertIn("declared_value_mismatch", audit.ipr_state(record)[0])


    @unittest.skipUnless(os.environ.get("IPR_RENDER_SERVER_BIN") and os.environ.get("IPR_GENERATOR_TEST_BIN"),
                         "Native render integration uses explicit rebuilt binaries")
    def test_in_memory_render_equals_generator_tiff(self):
        """Same pixels and declarations as the TIFF export, for every state of a group."""
        from PIL import Image
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            base = root / "tests/image/base"
            base.mkdir(parents=True)
            Image.new("RGB", (48, 32), (100, 120, 130)).save(base / "tiny.png")
            result = subprocess.run([os.environ["IPR_GENERATOR_TEST_BIN"], "lumiere"], cwd=root, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            with render.RenderServer(os.environ["IPR_RENDER_SERVER_BIN"]) as server:
                states = server.states(base, "lumiere")
                self.assertEqual(len(states), 5 + 5 * 9)
                for state in states + [{"source": str(base / "tiny.png"), "feature": "neutral", "value": 0}]:
                    feature, value = state["feature"], state["value"]
                    name = ("neutre/tiny-IPR-neutre-lumiere.tiff" if feature == "neutral" else
                            "lumiere/" + feature + "/tiny-IPR-" + feature + "-" + format(value, "g") + ".tiff")
                    stored, stored_metadata, _ = native.inspect(base / name)
                    try:
                        pixels, metadata = server.render(state["source"], feature, value)
                        np.testing.assert_array_equal(pixels, stored[:, :, :3], name)
                        self.assertEqual(metadata["icc_sha256"], stored_metadata["icc_sha256"])
                        declared, _ = native.embedded_metadata(base / name)
                        self.assertEqual(metadata["declaration"]["settings"], declared["settings"], name)
                    finally:
                        stored._mmap.close()
                with self.assertRaises(ValueError):
                    server.render(base / "tiny.png", "invented", 0)
                self.assertEqual(server.render(base / "tiny.png", "neutral", 0)[0].shape, (32, 48, 3))

    @unittest.skipUnless(os.environ.get("IPR_RENDER_SERVER_BIN"), "Native render integration uses an explicit rebuilt binary")
    def test_in_memory_campaigns_on_tiny_fixture_write_no_image(self):
        from PIL import Image
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            base = root / "tests/image/base"
            base.mkdir(parents=True)
            Image.new("RGB", (48, 32), (100, 120, 130)).save(base / "tiny.png")
            Image.new("RGB", (40, 24), (140, 90, 60)).save(base / "tiny2.png")
            output = root / "measure.json"
            self.assertEqual(main(["--base", str(base), "--group", "couleur", "--output", str(output), "--strict",
                                   "--render-server", os.environ["IPR_RENDER_SERVER_BIN"]]), 0)
            result = native.read_json(output)
            self.assertEqual(result["coverage"]["diagnostic"], 0)
            self.assertEqual(result["coverage"]["excluded"], 0)
            self.assertEqual(result["ipr_pixels"]["kind"], "in_memory_render")
            self.assertEqual(main(["--base", str(base), "--group", "lumiere", "--output", str(root / "two.json"),
                                   "--strict", "--render-workers", "2",
                                   "--render-server", os.environ["IPR_RENDER_SERVER_BIN"]]), 0)
            self.assertEqual(native.read_json(root / "two.json")["ipr_pixels"]["parallel_servers"], 2)
            wb = [r for r in result["records"] if r["feature"] in ("temperature", "tint")]
            self.assertTrue(wb and all(r["reference_role"] == "support" for r in wb))
            self.assertEqual(sorted(p.name for p in base.rglob("*") if p.is_file()), ["tiny.png", "tiny2.png"])
            with render.RenderServer(os.environ["IPR_RENDER_SERVER_BIN"]) as server:
                records = inputs.render_records(base, list(catalog.GROUPS), server)
                for group in ("lumiere", "courbes", "couleur", "melangeur", "grading", "effets", "bruit", "nettete"):
                    measured = campaign.analyse(group, records, step=1, patch_size=16, servers=[server])
                    self.assertEqual(measured["coverage"]["diagnostic"], 0, (group, [(r["feature"], r["issues"]) for r in measured["records"]]))
                    self.assertEqual(measured["coverage"]["excluded"], 0, group)
            self.assertEqual(sorted(p.name for p in base.rglob("*") if p.is_file()), ["tiny.png", "tiny2.png"])

    def test_render_worker_count_follows_free_memory(self):
        gib = 1024 ** 3
        self.assertEqual(render.worker_count(available=1 * gib), 1)
        self.assertEqual(render.worker_count(available=2 * gib + render.PER_WORKER_BYTES), 1)
        self.assertLessEqual(render.worker_count(available=64 * gib), 4)
        self.assertEqual(render.worker_count(available=64 * gib, limit=1), 1)

    def test_catalog_matches_photographic_groups_and_orange_violet_magenta(self):
        self.assertEqual(len(catalog.GROUPS), 10)
        for feature in ("mixer-orange-hue", "mixer-violet-hue", "mixer-magenta-hue"):
            self.assertEqual(catalog.group_for(feature), "melangeur")
        with self.assertRaises(ValueError):
            catalog.group_for("invented-mask")

    def test_read_native_16bit_both_endians_preserves_adjacent_codes(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "image.tiff"
            values = np.arange(32 * 32 * 3, dtype=np.uint16).reshape(32, 32, 3) + 25000
            for order in ("<", ">"):
                write_tiff(path, values, order)
                pixels, metadata, _ = native.inspect(path)
                try:
                    np.testing.assert_array_equal(pixels, values)
                    np.testing.assert_allclose(native.encoded(pixels), values / 65535, rtol=0, atol=0)
                    self.assertEqual(metadata["bits"], [16, 16, 16])
                finally:
                    # Release the mapping before rewriting/removing the fixture on Windows.
                    pixels._mmap.close()

    def test_reader_rejects_missing_icc_orientation_alpha_and_truncation(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.tiff"
            values = np.full((8, 8, 3), 25000, dtype=np.uint16)
            for kwargs in ({"with_icc": False}, {"orientation": 6}):
                write_tiff(path, values, **kwargs)
                with self.assertRaises(ValueError):
                    native.inspect(path)
            write_tiff(path, np.concatenate((values, np.zeros((8, 8, 1), dtype=np.uint16)), axis=2))
            with self.assertRaises(ValueError):
                native.inspect(path)
            write_tiff(path, values)
            path.write_bytes(path.read_bytes()[:-2])
            with self.assertRaises(ValueError):
                native.inspect(path)

    def test_srgb_known_values_and_haar_gaussian_normalization(self):
        np.testing.assert_allclose(native.linearize(np.array([0, .04045, 1])), [0, .00313080495356, 1])
        rng = np.random.default_rng(9)
        self.assertAlmostEqual(metrics.haar_sigma(rng.normal(0, .02, (1024, 1024))), .02, delta=.0002)
        self.assertEqual(metrics.haar_sigma(np.full((16, 16), .3)), 0)
        self.assertIsNone(metrics.haar_sigma(np.ones((1, 1))))

    def test_empty_masks_and_black_denominators_produce_null_not_nan(self):
        p = np.zeros((8, 8, 3))
        r = groups.measure("couleur", {"feature": "saturation", "value": 0}, p, p)
        self.assertIsNone(r["Y_ratio"])
        self.assertIsNone(r["delta_log_R_B"])
        json.dumps(r, allow_nan=False)

    def test_color_direction_and_chroma_adaptivity_metrics(self):
        base = np.full((32, 32, 3), [.4, .4, .4])
        warmer = base * [1.1, 1, .9]
        result = groups.measure("couleur", {"feature": "temperature", "value": 6500}, base, warmer)
        self.assertGreater(result["delta_log_R_B"], 0)
        magenta = base * [1.05, .9, 1.05]
        result = groups.measure("couleur", {"feature": "tint", "value": 25}, base, magenta)
        self.assertGreater(result["delta_log_magenta_green"], 0)

    def test_mixer_gray_excluded_and_circular_wrap(self):
        self.assertAlmostEqual(float(metrics.circular_delta(1, 359)), 2)
        p = np.full((16, 16, 3), .5)
        r = groups.measure("melangeur", {"feature": "mixer-red-hue", "value": 25}, p, p)
        self.assertEqual(r["core_samples"], 0)
        self.assertIsNone(r["core_hue_delta_degrees"])

    def test_audit_other_settings_and_inactive_support(self):
        record = {"feature": "saturation", "value": 25,
                  "settings": {"saturation": 25, "exposure": 1},
                  "reference_settings": {"saturation": 0, "exposure": 0}, "expected_sha256": "x"}
        self.assertIn("other_settings_changed", audit.ipr_state(record)[0])
        record.update(feature="sharpening-radius", value=1,
                      settings={"sharpeningRadius": 1, "sharpeningAmount": 0},
                      reference_settings={"sharpeningRadius": 1, "sharpeningAmount": 0})
        self.assertIn("inactive_sharpening_support", audit.ipr_state(record)[0])

    def test_lr_tint_offset_and_wrong_secondary_control(self):
        r = {"engine": "LR", "feature": "tint", "value": 25, "value_mode": "delta_from_reference"}
        fields = ("Exposure2012", "Contrast2012", "Shadows2012", "Blacks2012", "Highlights2012", "Whites2012",
                  "Vibrance", "Saturation", "LuminanceSmoothing", "ColorNoiseReduction", "Sharpness")
        base = {"xmp_settings": {**dict.fromkeys(fields, "0"), "Tint": "5"}}
        variant = {"xmp_settings": {**dict.fromkeys(fields, "0"), "Tint": "30"}}
        self.assertEqual(audit.audit(r, variant, base)[0], [])
        variant["xmp_settings"]["Exposure2012"] = "1"
        self.assertTrue(audit.audit(r, variant, base)[0])

    def test_campaign_json_provenance_and_immutable_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = fixture_record(root)
            result = campaign.analyse("couleur", [record], step=1)
            self.assertEqual(result["coverage"]["measured"], 1)
            self.assertFalse(result["decision"]["cleanup_eligible"])
            output = root / "measure.json"
            native.write_immutable(output, result)
            self.assertEqual(native.read_json(output)["schema_version"], 1)
            with self.assertRaises(FileExistsError):
                native.write_immutable(output, result)

    def test_hash_mismatch_missing_reference_and_geometry_are_excluded(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = fixture_record(root)
            record["expected_sha256"] = "0" * 64
            result = campaign.analyse("couleur", [record])
            self.assertEqual(result["coverage"]["excluded"], 1)
            record.pop("reference")
            self.assertEqual(campaign.analyse("couleur", [record])["coverage"]["excluded"], 1)
            record = fixture_record(root)
            write_tiff(root / "variant.tiff", np.full((8, 8, 3), 22000, dtype=np.uint16))
            self.assertEqual(campaign.analyse("couleur", [record])["coverage"]["excluded"], 1)

    def test_mid_read_mutation_aborts_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = fixture_record(root)
            with patch.object(campaign, "verify_unchanged", side_effect=ValueError("changed")):
                with self.assertRaises(ValueError):
                    campaign.analyse("couleur", [record])

    def test_noise_roi_native_stride_and_no_invalid_quality_verdict(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = fixture_record(root, "denoiseLuminance", 25)
            record["feature"] = "denoise-luminance"
            result = campaign.analyse("bruit", [record], step=8, patch_size=16)
            state = result["records"][0]
            self.assertTrue(state["rois"])
            self.assertTrue(all(r["sampling_step"] == 1 for r in state["rois"]))
            self.assertEqual(result["decision"]["visual"], "not_evaluated")

    def test_strict_json_rejects_duplicate_keys_and_nonfinite(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.json"
            for contents in ('{"x":NaN}', '{"x":1,"x":2}'):
                path.write_text(contents)
                with self.assertRaises(ValueError):
                    native.read_json(path)

    def test_bounds_and_unknown_features_rejected(self):
        record = {"scene": "S", "engine": "IPR", "feature": "temperature", "value": 0, "file": "x"}
        with self.assertRaises(ValueError):
            inputs.validate_record(record)
        record.update(feature="invented", value=0)
        with self.assertRaises(ValueError):
            inputs.validate_record(record)


    def test_all_dispatch_handlers_return_strict_json(self):
        p = np.full((8, 8, 3), [.4, .3, .2])
        for group, controls in catalog.GROUPS.items():
            record = {"feature": controls[0], "value": 0, "settings": {}}
            json.dumps(groups.measure(group, record, p, p), allow_nan=False)

    def test_explicit_mapping_resolution_and_duplicate_neutrals(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = fixture_record(root)
            path = root / "mapping.json"
            data = {"schema_version": 1, "records": [
                {"scene": "S", "engine": "IPR", "feature": "neutral", "value": 0, "file": "neutral.tiff",
                 "settings": record["reference_settings"], "expected_sha256": record["reference_sha256"]},
                {"scene": "S", "engine": "IPR", "feature": "saturation", "value": 50, "file": "variant.tiff",
                 "settings": record["settings"], "expected_sha256": record["expected_sha256"]}]}
            path.write_text(json.dumps(data))
            mapped = inputs.mapping_records(path)
            self.assertEqual(mapped[1]["reference"], str((root / "neutral.tiff").resolve()))
            data["records"].append(data["records"][0])
            path.write_text(json.dumps(data))
            with self.assertRaises(ValueError):
                inputs.mapping_records(path)

    def test_strict_diagnostics_return_two_not_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = fixture_record(root)
            record.pop("expected_sha256")
            mapping = root / "mapping.json"
            mapping.write_text(json.dumps({"schema_version": 1, "records": [record]}))
            output = root / "diagnostic.json"
            self.assertEqual(main(["--mapping", str(mapping), "--group", "couleur", "--strict",
                                   "--output", str(output)]), 2)
            self.assertEqual(native.read_json(output)["coverage"]["diagnostic"], 1)

    def test_within_engine_icc_change_not_silently_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = fixture_record(root)
            actual = campaign.inspect
            def changed_icc(path):
                pixels, metadata, before = actual(path)
                if path.name == "variant.tiff":
                    metadata["icc_sha256"] = "different"
                return pixels, metadata, before
            with patch.object(campaign, "inspect", side_effect=changed_icc):
                result = campaign.analyse("couleur", [record])
            self.assertEqual(result["coverage"]["excluded"], 1)

    def test_spatial_roi_out_of_bounds_rejected(self):
        with self.assertRaises(ValueError):
            campaign.explicit_rois([{"box": [0, 0, 99, 99]}], (32, 32, 3))
        with self.assertRaises(ValueError):
            campaign.explicit_rois([{"box": [-1, 0, 2, 2]}], (32, 32, 3))

    def test_progression_excludes_unknown_state_and_no_fake_zero(self):
        records = []
        for value, response in ((25, .1), (50, .2)):
            records.append({"scene": "S", "engine": "IPR", "feature": "tint", "value": value,
                            "status": "measured", "reference_metadata": {"sha256": "a"},
                            "protocol_usable": True, "metrics": {"delta_log_magenta_green": response}})
        progression = campaign.progressions("couleur", records)[0]
        self.assertEqual(len(progression["points"]), 2)
        self.assertTrue(progression["monotonic_observed_means"])
        records[1]["protocol_usable"] = False
        self.assertIsNone(campaign.progressions("couleur", records)[0]["monotonic_observed_means"])


if __name__ == "__main__":
    unittest.main()
