"""Small native fixtures; no private image corpus or output rendering."""
import copy
from pathlib import Path
import tempfile
import unittest

import numpy as np

from analysis import audit, native
from compare_vibrance_saturation import associations, run
from test_analyse_groupes import write_tiff


def fixture(base):
    folder = base / "couleur/saturation"
    folder.mkdir(parents=True)
    (base / "couleur/vibrance").mkdir()
    (base / "neutre").mkdir()
    pixels = np.full((16, 16, 3), [28000, 25000, 22000], dtype=np.uint16)
    settings = dict.fromkeys(audit.ZERO_IPR, 0)
    settings.update(temperature=5200, mixerHue=[0] * 8, mixerSaturation=[0] * 8,
                    mixerLuminance=[0] * 8, grading=[{"hue": 0, "saturation": 0}] * 3,
                    toneCurves=[[], [], [], []])
    decl = {"ipr_test_schema": 1, "scene": "S", "feature": "neutral", "value": 0,
            "reference": "neutre/S-IPR.tiff", "settings": settings,
            "dcp_profile": "Canon Camera Neutral", "raw_normalization": False,
            "lens_geometry_applied": False, "lens_vignetting_applied": False,
            "chromatic_aberration_applied": False}
    write_tiff(base / "neutre/S-IPR.tiff", pixels, declaration=decl)
    for value in (0, 100):
        changed = copy.deepcopy(decl)
        changed.update(feature="saturation", value=value)
        changed["settings"]["saturation"] = value
        target = pixels if value == 0 else pixels + np.array([1000, 0, -1000])
        write_tiff(folder / ("S-IPR-saturation-" + str(value) + ".tiff"), target, declaration=changed)
        fields = {key: "0" for key in ("Exposure2012", "Contrast2012", "Shadows2012", "Blacks2012",
                                      "Highlights2012", "Whites2012", "Vibrance", "Saturation",
                                      "LuminanceSmoothing", "ColorNoiseReduction", "Sharpness")}
        fields.update(Saturation=str(value), RawFileName="S.CR3", CameraProfile="Camera Neutral",
                      WhiteBalance="Custom", Temperature="5200", Tint="5", LensProfileEnable="0", AutoLateralCA="0")
        packet = ('<root xmlns:crs="' + native.CRS + '" ' + ' '.join('crs:' + k + '="' + v + '"' for k, v in fields.items()) + '/>').encode()
        write_tiff(folder / ("S-LR-" + str(value) + ".tif"), target, xmp=packet)
    return folder


class GlobalColourComparisonTests(unittest.TestCase):
    def test_own_zero_references_audit_pairings_and_no_pixel_alignment(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            fixture(base)
            result = run(base, step=1, data_version=2, supersedes="previous-colour-campaign")
            self.assertEqual(result["supersedes_campaign_id"], "previous-colour-campaign")
            self.assertEqual(result["data_version"], 2)
            self.assertEqual(result["coverage"], {"measured": 4, "diagnostic": 0, "excluded": 0})
            self.assertEqual(len(result["comparison"]["pairs"]), 2)
            self.assertTrue(all(not p["cross_engine_pixel_alignment"] for p in result["comparison"]["pairs"]))
            self.assertTrue(all(not p["context_issues"] for p in result["comparison"]["pairs"]))
            self.assertEqual(result["decision"]["visual"], "not_evaluated")

    def test_duplicate_lr_states_are_all_diagnostic_not_first_selected(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            folder = fixture(base)
            (folder / "S-copy.tif").write_bytes((folder / "S-LR-100.tif").read_bytes())
            result = run(base, step=1)
            duplicated = [r for r in result["records"] if r["engine"] == "LR" and r["value"] == 100]
            self.assertEqual(len(duplicated), 2)
            self.assertTrue(all("ambiguous_duplicate_state" in r["issues"] for r in duplicated))
            self.assertEqual(len(result["comparison"]["pairs"]), 1)

    def test_missing_ipr_declaration_and_outside_reference_not_guessed(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            folder = fixture(base)
            write_tiff(folder / "S-IPR-unknown.tiff", np.full((16, 16, 3), 25000, dtype=np.uint16))
            _, inventory, _ = associations(base)
            unknown = next(e for e in inventory if e["file"].endswith("S-IPR-unknown.tiff"))
            self.assertTrue(unknown["issues"])
            path = folder / "S-IPR-saturation-100.tiff"
            decl, _ = native.embedded_metadata(path)
            decl["reference"] = "../outside.tiff"
            write_tiff(path, np.full((16, 16, 3), 25000, dtype=np.uint16), declaration=decl)
            with self.assertRaises(ValueError):
                associations(base)

    def test_missing_one_neutral_excludes_its_states_not_entire_campaign(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            fixture(base)
            (base / "neutre/S-IPR.tiff").unlink()
            result = run(base, step=1)
            self.assertEqual(result["coverage"]["excluded"], 2)
            self.assertEqual(result["coverage"]["measured"], 2)
            self.assertEqual(result["comparison"]["pairs"], [])

    def test_wrong_control_export_does_not_poison_actual_zero(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            folder = fixture(base)
            from xml.etree import ElementTree as ET
            source = folder / "S-LR-0.tif"
            _, settings = native.embedded_metadata(source)
            for name, amount in (("S-real-zero.tif", "0"), ("S-wrong-control.tif", "-100")):
                fields = {**settings, "Saturation": amount}
                element = ET.Element("root", {"{" + native.CRS + "}" + k: v for k, v in fields.items()})
                write_tiff(base / "couleur/vibrance" / name, np.full((16, 16, 3), 25000, dtype=np.uint16), xmp=ET.tostring(element))
            records, _, _ = associations(base)
            genuine = next(r for r in records if r["file"].endswith("S-real-zero.tif"))
            misplaced = next(r for r in records if r["file"].endswith("S-wrong-control.tif"))
            self.assertEqual(genuine["association_issues"], [])
            self.assertEqual(genuine["file"], misplaced["reference"])
            self.assertTrue(misplaced["association_issues"])


if __name__ == "__main__":
    unittest.main()
