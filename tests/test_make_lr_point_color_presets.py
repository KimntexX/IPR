import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from make_lr_point_color_presets import CRS, RDF, STATES, main, read_model  # noqa: E402

# Layout of a Lightroom sidecar: source, shifts, range, three sub-ranges; variance apart.
POINT = ("0.548111, 0.502314, 0.161747, 0.110000, 0.220000, 0.330000, 0.660000, 0.000000, 0.333333, "
         "0.666667, 1.000000, 0.000000, 0.322314, 0.682314, 1.000000, 0.000000, 0.258857, 0.618857, 1.000000")


def seq(tag, item):
    return f"<crs:{tag}><rdf:Seq><rdf:li>{item}</rdf:li></rdf:Seq></crs:{tag}>"


def model_file(path, name=None, point=POINT, variance="0.440000"):
    named = f'<crs:Name><rdf:Alt><rdf:li xml:lang="x-default">{name}</rdf:li></rdf:Alt></crs:Name>' if name else ""
    path.write_text(
        '<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="' + RDF + '">'
        '<rdf:Description rdf:about="" xmlns:crs="' + CRS + '">' + named + seq("PointColors", point)
        + (seq("ColorVariance", variance) if variance else "") + '</rdf:Description></rdf:RDF></x:xmpmeta>',
        encoding="utf-8")
    return path


class PointColorPresetTests(unittest.TestCase):
    def test_markers_are_replaced_and_the_rest_kept(self):
        with tempfile.TemporaryDirectory() as directory:
            model = read_model(model_file(Path(directory) / "6V1A0327.xmp"))
            self.assertEqual(model.label, "6V1A0327")
            items = model.items_for({"hue": -50, "variance": 100, "range": 25})
            self.assertEqual(items["PointColors"], POINT.replace("0.110000", "-0.500000").replace(
                "0.220000", "0.000000").replace("0.330000", "0.000000").replace("0.660000", "0.250000"))
            self.assertEqual(items["ColorVariance"], "1.000000")

    def test_label_from_the_option_or_the_preset_name(self):
        with tempfile.TemporaryDirectory() as directory:
            path = model_file(Path(directory) / "m.xmp", name="PC Orange")
            self.assertEqual(read_model(path).label, "PC-Orange")
            self.assertEqual(read_model(path, "Peau claire").label, "Peau-claire")

    def test_variance_in_the_point_item_and_slider_scale(self):
        with tempfile.TemporaryDirectory() as directory:
            model = read_model(model_file(Path(directory) / "m.xmp", point="S 0.4 0.5, H 11, S 22, L 33, V 44, R 66",
                                          variance=None))
            self.assertEqual(model.items_for({"saturation": -100}), {"PointColors": "S 0.4 0.5, H 0, S -100, L 0, V 0, R 50"})

    def test_a_missing_or_ambiguous_marker_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            for point, variance in ((POINT, None), (POINT, "0.110000"), (POINT.replace("0.660000", "0.500000"), "0.44")):
                with self.assertRaises(ValueError):
                    read_model(model_file(Path(directory) / "m.xmp", point=point, variance=variance))

    def test_archive_of_valid_presets(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            samples = [str(model_file(folder / "a.xmp")), str(model_file(folder / "b.xmp"))]
            main(["--samples", *samples, "--labels", "Orange", "Ciel", "--states", "reduced", "--out", str(folder / "out")])
            with zipfile.ZipFile(folder / "out" / "IPR-Couleur-point-presets.zip") as z:
                names = z.namelist()
                self.assertEqual(len(names), 2 * len(STATES["reduced"]))
                root = ET.fromstring(z.read("IPR Couleur point/IPR_Point_Ciel_S-100_G000.xmp"))
            point = [li.text for e in root.iter(f"{{{CRS}}}PointColors") for li in e.iter(f"{{{RDF}}}li")]
            variance = [li.text for e in root.iter(f"{{{CRS}}}ColorVariance") for li in e.iter(f"{{{RDF}}}li")]
            self.assertEqual(point[0].split(", ")[3:7], ["0.000000", "-1.000000", "0.000000", "0.000000"])
            self.assertEqual(variance, ["0.000000"])
            description = next(root.iter(f"{{{RDF}}}Description"))
            self.assertEqual(description.get(f"{{{CRS}}}CameraProfile"), "Camera Neutral")
            with self.assertRaises(SystemExit):   # nothing overwritten
                main(["--samples", samples[0], "--out", str(folder / "out")])

    def test_complete_states(self):
        self.assertEqual(len(STATES["complete"]), 26)


if __name__ == "__main__":
    unittest.main()
