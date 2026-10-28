"""Write Lightroom presets for the point colour reference exports, from model points saved in Lightroom.

Lightroom writes each picked colour as one text item of crs:PointColors (its
source, shifts, range and sub-ranges) and its variance as one item of
crs:ColorVariance; IPR does not decode the source. Give one model per source
colour: a Lightroom preset (only "Point Color" ticked) or the photo's sidecar,
the colour picked and the sliders at the marker values: hue shift +11, saturation
shift +22, luminance shift +33, variance +44, range 66. The generator finds each
marker once in these items, keeps the rest (source, sub-ranges) verbatim, and
writes the states over the IPR neutral baseline of make_lr_presets.py:

- complete: hue, saturation and luminance shifts at -100, -50, -25, +25, +50, +100;
  variance at -100, -50, +50, +100; saturation -100 at range 0, 25, 75, 100 (the
  desaturation maps the neighbourhood). 26 states per source.
- reduced: saturation -100 at range 0, 25, 50, 75, 100; hue, saturation and
  luminance +50. 8 states per source.

Import the .zip from Lightroom's Presets panel; never overwrites a file.

Usage: python scripts/make_lr_point_color_presets.py --samples model1.xmp [model2.xmp ...]
           [--labels Orange ...] [--states complete|reduced ...] [--out tests/image/lr-presets]
       (label: --labels, else the preset name, else the file name; --states: one for all
       models or one per model)
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
# Items of a point, in the order they are written.
ELEMENTS = ("PointColors", "ColorVariance")
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
    """The items of a model point ({element: text}), with the place and scale of each marker."""

    def __init__(self, label, items):
        self.label, self.items, self.slots = label, items, {}
        for slider, marker in MARKERS.items():
            found = [(element, m.start(), m.end(), m.group(), scale)
                     for element, item in items.items() for m in NUMBER.finditer(item) for scale in (1.0, 0.01)
                     if abs(abs(float(m.group())) - marker * scale) < 1e-6]
            if len(found) != 1:
                raise ValueError(f"{label}: marker {slider} {marker} found {len(found)} times in: {items}")
            self.slots[slider] = found[0]

    def items_for(self, sliders):
        """The items with each slider at its value (neutral value when not given)."""
        values = {**NEUTRAL_SLIDERS, **sliders}
        items = dict(self.items)
        # From the end of each item, so that the earlier places stay valid.
        for slider, (element, start, end, original, scale) in sorted(
                self.slots.items(), key=lambda s: (s[1][0], s[1][1]), reverse=True):
            decimals = len(original.split(".")[1]) if "." in original else 0
            value = values[slider] * scale
            sign = "+" if original.startswith("+") and value >= 0 else ""
            items[element] = items[element][:start] + f"{sign}{value:.{decimals}f}" + items[element][end:]
        return items


def read_model(path, label=None):
    """The model point of a Lightroom preset or sidecar, which must hold exactly one point."""
    root = ET.parse(path).getroot()
    if label is None:
        names = [li.text for e in root.iter(f"{{{CRS}}}Name") for li in e.iter(f"{{{RDF}}}li") if li.text]
        label = names[0] if names else Path(path).stem
    label = re.sub(r"[^\w-]+", "-", label).strip("-")
    items = {}
    for element in ELEMENTS:
        found = [li.text.strip() for e in root.iter(f"{{{CRS}}}{element}") for li in e.iter(f"{{{RDF}}}li")
                 if li.text and li.text.strip()]
        if len(found) > 1 or (element == "PointColors" and len(found) != 1):
            raise ValueError(f"{path}: expected one point colour, found {len(found)} {element} items")
        if found:
            items[element] = found[0]
    return Model(label, items)


def state_name(label, sliders):
    parts = [f"{LETTERS[s]}{v:+04d}" for s, v in sliders.items() if s in LETTERS]
    if "range" in sliders:
        parts.append(f"G{sliders['range']:03d}")
    return f"IPR Point {label} {' '.join(parts)}"


def point_preset(group, model, sliders):
    name = state_name(model.label, sliders)
    _, text = preset(group)
    elements = "".join(f"   <crs:{tag}>\n    <rdf:Seq>\n     <rdf:li>{item}</rdf:li>\n    </rdf:Seq>\n   </crs:{tag}>\n"
                       for tag, item in model.items_for(sliders).items())
    neutral_name = alt("Name", "IPR Neutre")
    assert text.count(neutral_name) == 1
    text = text.replace(neutral_name, alt("Name", name)).replace("  </rdf:Description>", elements + "  </rdf:Description>")
    return name, text


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--samples", type=Path, nargs="+", required=True)
    parser.add_argument("--labels", nargs="+")
    parser.add_argument("--states", choices=sorted(STATES), nargs="+", default=["complete"])
    parser.add_argument("--out", type=Path, default=ROOT / "tests/image/lr-presets")
    args = parser.parse_args(argv)
    if args.labels and len(args.labels) != len(args.samples):
        sys.exit("Un nom par modèle : --labels doit suivre --samples")
    if len(args.states) not in (1, len(args.samples)):
        sys.exit("--states : une série pour tous les modèles ou une par modèle")
    states = args.states * len(args.samples) if len(args.states) == 1 else args.states
    models = [read_model(path, args.labels[i] if args.labels else None) for i, path in enumerate(args.samples)]
    if len({m.label for m in models}) != len(models):
        sys.exit("Deux modèles portent le même nom : renommez un préréglage modèle")
    group, archive_name = GROUP
    folder, archive = args.out / group, args.out / archive_name
    if archive.exists() or folder.exists():
        sys.exit(f"Existe déjà, rien n'est écrasé : {archive if archive.exists() else folder}")
    folder.mkdir(parents=True)
    files = []
    for model, series in zip(models, states):
        for sliders in STATES[series]:
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
