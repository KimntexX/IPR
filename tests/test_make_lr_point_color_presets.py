import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from make_lr_point_color_presets import CRS, RDF, STATES, main, read_models  # noqa: E402

# An item in an encoding of our own: the generator must not depend on Lightroom's.
ITEM = ("Src = 0.402000, 0.551000, 0.613000, Hue = 0.110000, Sat = +0.220000, Lum = 0.330000, "
        "Var = 0.440000, Range = 0.660000, Extra = 0.250000, 0.750000")


def model_preset(path, name="PC Orange", item=ITEM):
    path.write_text(
        '<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="' + RDF + '">'
        '<rdf:Description rdf:about="" xmlns:crs="' + CRS + '">'
        f'<crs:Name><rdf:Alt><rdf:li xml:lang="x-default">{name}</rdf:li></rdf:Alt></crs:Name>'
        f'<crs:PointColors><rdf:Seq><rdf:li>{item}</rdf:li></rdf:Seq></crs:PointColors>'
        '</rdf:Description></rdf:RDF></x:xmpmeta>', encoding="utf-8")
    return path


class PointColorPresetTests(unittest.TestCase):
    def test_markers_are_replaced_and_the_source_kept(self):
        with tempfile.TemporaryDirectory() as directory:
            model = read_models(model_preset(Path(directory) / "m.xmp"))
            self.assertEqual(model.label, "PC-Orange")
            self.assertEqual(model.item_for({"hue": -50, "range": 25}),
                             "Src = 0.402000, 0.551000, 0.613000, Hue = -0.500000, Sat = +0.000000, "
                             "Lum = 0.000000, Var = 0.000000, Range = 0.250000, Extra = 0.250000, 0.750000")

    def test_markers_on_the_slider_scale(self):
        with tempfile.TemporaryDirectory() as directory:
            item = "S 0.4 0.5, H 11, S 22, L 33, V 44, R 66"
            model = read_models(model_preset(Path(directory) / "m.xmp", item=item))
            self.assertEqual(model.item_for({"saturation": -100}), "S 0.4 0.5, H 0, S -100, L 0, V 0, R 50")

    def test_a_missing_or_ambiguous_marker_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            for item in ("Hue = 0.11, Sat = 0.22, Lum = 0.33, Var = 0.44",
                         "Hue = 0.11, Sat = 0.22, Lum = 0.33, Var = 0.44, Range = 0.66, Src = 0.11"):
                with self.assertRaises(ValueError):
                    read_models(model_preset(Path(directory) / "m.xmp", item=item))

    def test_archive_of_valid_presets(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            samples = [str(model_preset(folder / "a.xmp")), str(model_preset(folder / "b.xmp", "PC Ciel"))]
            main(["--samples", *samples, "--states", "reduced", "--out", str(folder / "out")])
            archive = folder / "out" / "IPR-Couleur-point-presets.zip"
            with zipfile.ZipFile(archive) as z:
                names = z.namelist()
                self.assertEqual(len(names), 2 * len(STATES["reduced"]))
                self.assertIn("IPR Couleur point/IPR_Point_PC-Ciel_S-100_G000.xmp", names)
                root = ET.fromstring(z.read("IPR Couleur point/IPR_Point_PC-Ciel_S-100_G000.xmp"))
            items = [li.text for e in root.iter(f"{{{CRS}}}PointColors") for li in e.iter(f"{{{RDF}}}li")]
            self.assertEqual(len(items), 1)
            self.assertIn("Sat = -1.000000", items[0])
            self.assertIn("Range = 0.000000", items[0])
            description = next(root.iter(f"{{{RDF}}}Description"))
            self.assertEqual(description.get(f"{{{CRS}}}CameraProfile"), "Camera Neutral")
            with self.assertRaises(SystemExit):   # nothing overwritten
                main(["--samples", samples[0], "--out", str(folder / "out")])

    def test_complete_states(self):
        self.assertEqual(len(STATES["complete"]), 26)


if __name__ == "__main__":
    unittest.main()
