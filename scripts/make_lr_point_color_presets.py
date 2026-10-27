"""Write Lightroom presets for the point colour reference exports, from model presets saved in Lightroom.

Lightroom's point colour stores each picked colour as one text item of
crs:PointColors, in an encoding IPR does not assume. Save one model preset per
source colour in Lightroom (only "Point Color" ticked), the colour picked in a
photo and the sliders at the marker values: hue shift +11, saturation shift +22,
luminance shift +33, variance +44, range 66. The generator finds each marker in
the item, keeps the rest (the source colour) verbatim, and writes the states over
the IPR neutral baseline of make_lr_presets.py:

- complete: hue, saturation and luminance shifts at -100, -50, -25, +25, +50, +100;
  variance at -100, -50, +50, +100; saturation -100 at range 0, 25, 75, 100 (the
  desaturation maps the neighbourhood). 26 states per source.
- reduced: saturation -100 at range 0, 25, 50, 75, 100; hue, saturation and
  luminance +50. 8 states per source.

Import the .zip from Lightroom's Presets panel; never overwrites a file.

Usage: python scripts/make_lr_point_color_presets.py --samples model1.xmp [model2.xmp ...]
           [--states complete|reduced] [--out tests/image/lr-presets]
"""
import argparse
import re
import sys
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_lr_presets import ROOT, alt, preset  # noqa: E402

CRS = "http://ns.adobe.com/camera-raw-settings/1.0/"
RDF = "http://www.w3.org/1999/02/22-rdf-syntax-ns#"
MARKERS = {"hue": 11, "saturation": 22, "luminance": 33, "variance": 44, "range": 66}
NEUTRAL_SLIDERS = {"hue": 0, "saturation": 0, "luminance": 0, "variance": 0, "range": 50}
SHIFTS = (-100, -50, -25, 25, 50, 100)
LETTERS = {"hue": "T", "saturation": "S", "luminance": "L", "variance": "V"}
STATES = {
    "complete": ([{component: v} for component in ("hue", "saturation", "luminance") for v in SHIFTS]
                 + [{"variance": v} for v in (-100, -50, 50, 100)]
                 + [{"saturation": -100, "range": r} for r in (0, 25, 75, 100)]),
    "reduced": ([{"saturation": -100, "range": r} for r in (0, 25, 50, 75, 100)]
                + [{component: 50} for component in ("hue", "saturation", "luminance")]),
}
GROUP = ("IPR Couleur point", "IPR-Couleur-point-presets.zip")
NUMBER = re.compile(r"[-+]?\d+(?:\.\d+)?")


class Model:
    """The item of a model preset, with the place and scale of each marker."""

    def __init__(self, label, item):
        self.label, self.item, self.slots = label, item, {}
        tokens = [(m.start(), m.end(), m.group()) for m in NUMBER.finditer(item)]
        for slider, marker in MARKERS.items():
            found = [(start, end, text, scale) for start, end, text in tokens for scale in (1.0, 0.01)
                     if abs(abs(float(text)) - marker * scale) < 1e-6]
            if len(found) != 1:
                raise ValueError(f"{label}: marker {slider} {marker} found {len(found)} times in: {item}")
            self.slots[slider] = found[0]
        spans = sorted((s[0], s[1]) for s in self.slots.values())
        if any(a[1] > b[0] for a, b in zip(spans, spans[1:])):
            raise ValueError(f"{label}: overlapping markers in: {item}")

    def item_for(self, sliders):
        """The item with each slider at its value (neutral value when not given)."""
        values = {**NEUTRAL_SLIDERS, **sliders}
        text, offset = self.item, 0
        for slider, (start, end, original, scale) in sorted(self.slots.items(), key=lambda s: s[1][0]):
            decimals = len(original.split(".")[1]) if "." in original else 0
            value = values[slider] * scale
            sign = "+" if original.startswith("+") and value >= 0 else ""
            replacement = f"{sign}{value:.{decimals}f}"
            text = text[:start + offset] + replacement + text[end + offset:]
            offset += len(replacement) - (end - start)
        return text


def read_models(path):
    """Models of a Lightroom preset: one per crs:PointColors item, labelled by the preset name."""
    root = ET.parse(path).getroot()
    names = [li.text for e in root.iter(f"{{{CRS}}}Name") for li in e.iter(f"{{{RDF}}}li") if li.text]
    label = re.sub(r"[^\w-]+", "-", names[0] if names else Path(path).stem).strip("-")
    items = [li.text.strip() for e in root.iter(f"{{{CRS}}}PointColors") for li in e.iter(f"{{{RDF}}}li")
             if li.text and li.text.strip()]
    if len(items) != 1:
        raise ValueError(f"{path}: expected one point colour, found {len(items)}")
    return Model(label, items[0])


def state_name(label, sliders):
    parts = [f"{LETTERS[s]}{v:+04d}" for s, v in sliders.items() if s in LETTERS]
    if "range" in sliders:
        parts.append(f"G{sliders['range']:03d}")
    return f"IPR Point {label} {' '.join(parts)}"


def point_preset(group, model, sliders):
    name = state_name(model.label, sliders)
    _, text = preset(group)
    element = (f"   <crs:PointColors>\n    <rdf:Seq>\n     <rdf:li>{model.item_for(sliders)}</rdf:li>\n"
               "    </rdf:Seq>\n   </crs:PointColors>\n")
    neutral_name = alt("Name", "IPR Neutre")
    assert text.count(neutral_name) == 1
    text = text.replace(neutral_name, alt("Name", name)).replace("  </rdf:Description>", element + "  </rdf:Description>")
    return name, text


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--samples", type=Path, nargs="+", required=True)
    parser.add_argument("--states", choices=sorted(STATES), default="complete")
    parser.add_argument("--out", type=Path, default=ROOT / "tests/image/lr-presets")
    args = parser.parse_args(argv)
    models = [read_models(path) for path in args.samples]
    if len({m.label for m in models}) != len(models):
        sys.exit("Deux modèles portent le même nom : renommez un préréglage modèle")
    group, archive_name = GROUP
    folder, archive = args.out / group, args.out / archive_name
    if archive.exists() or folder.exists():
        sys.exit(f"Existe déjà, rien n'est écrasé : {archive if archive.exists() else folder}")
    folder.mkdir(parents=True)
    files = []
    for model in models:
        for sliders in STATES[args.states]:
            name, text = point_preset(group, model, sliders)
            path = folder / f"{name.replace(' ', '_')}.xmp"
            path.write_text(text, encoding="utf-8")
            files.append(path)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for path in files:
            z.write(path, f"{group}/{path.name}")
    print(f"{len(files)} préréglages dans {folder}\nArchive à importer dans Lightroom : {archive}")


if __name__ == "__main__":
    main()
