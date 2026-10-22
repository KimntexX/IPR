"""Prepare a Lightroom export batch from XMP sidecars (automation track of the
Lumieres roadmap).

For each scene and each value of the requested controls, writes a hard link to
the RAW (no extra disk space) and a minimal .xmp sidecar holding only the
Camera Raw settings (crs:) of the scene's existing Lightroom neutral, with the
tested control changed. Open the batch folder in Lightroom (Local tab), check
that the settings were read, then export everything as 16-bit sRGB TIFF with
metadata into tests/image/base/lumiere/<control>/. Never overwrites a file.

Usage: python scripts/prepare_lr_xmp_batch.py --raw-dir tests/image/base-hors-protocole
           [--controls highlights shadows whites] [--scenes 6V1A0054 ...]
           [--out tests/image/lr-lots]
"""
import argparse
import copy
import os
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "tests/image/base"
CRS = "http://ns.adobe.com/camera-raw-settings/1.0/"
RDF = "http://www.w3.org/1999/02/22-rdf-syntax-ns#"
X = "adobe:ns:meta/"
FIELDS = {"shadows": "Shadows2012", "blacks": "Blacks2012", "highlights": "Highlights2012", "whites": "Whites2012",
          "contrast": "Contrast2012"}
VALUES = (-100, -75, -50, -25, 25, 50, 75, 100)
LUMIERE = ("Exposure2012", "Contrast2012", "Highlights2012", "Shadows2012", "Whites2012", "Blacks2012")
RAW_SUFFIXES = {".cr3", ".cr2", ".nef", ".arw", ".dng", ".raf", ".orf", ".rw2"}


def embedded_xmp(path):
    from PIL import Image
    with Image.open(path) as image:
        if image.format != "TIFF":
            return None
        xmp = image.tag_v2.get(700, b"")
        if isinstance(xmp, tuple):
            xmp = bytes(xmp)
        return xmp or None


def lightroom_neutral(scene):
    """The scene's LR neutral in neutre/: Camera Raw settings with every
    Lumieres control at 0 (an IPR export carries no crs: settings)."""
    for path in sorted((BASE / "neutre").iterdir()):
        if not path.name.startswith(scene) or path.suffix.lower() not in (".tif", ".tiff") or "-IPR" in path.name:
            continue
        xmp = embedded_xmp(path)
        if not xmp:
            continue
        description = ET.fromstring(xmp).find(f".//{{{RDF}}}Description")
        settings = {k.split("}")[1]: v for k, v in description.attrib.items() if k.startswith("{" + CRS + "}")}
        if settings and all(float(settings.get(f, "0")) == 0 for f in LUMIERE):
            return path, description
    return None, None


def sidecar(description, raw_name, field, value):
    """Minimal xmpmeta: crs: attributes and crs: elements of the neutral only."""
    for prefix, uri in (("x", X), ("rdf", RDF), ("crs", CRS)):
        ET.register_namespace(prefix, uri)
    meta = ET.Element(f"{{{X}}}xmpmeta")
    rdf = ET.SubElement(meta, f"{{{RDF}}}RDF")
    out = ET.SubElement(rdf, f"{{{RDF}}}Description", {f"{{{RDF}}}about": ""})
    for key, val in description.attrib.items():
        if key.startswith("{" + CRS + "}"):
            out.set(key, val)
    for child in description:
        if child.tag.startswith("{" + CRS + "}"):
            out.append(copy.deepcopy(child))
    out.set(f"{{{CRS}}}RawFileName", raw_name)
    out.set(f"{{{CRS}}}HasSettings", "True")
    out.set(f"{{{CRS}}}{field}", f"{value:+d}")
    return ET.tostring(meta, encoding="unicode")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--raw-dir", type=Path, required=True)
    parser.add_argument("--controls", nargs="+", default=["highlights", "shadows", "whites"], choices=sorted(FIELDS))
    parser.add_argument("--scenes", nargs="*")
    parser.add_argument("--out", type=Path, default=ROOT / "tests/image/lr-lots")
    args = parser.parse_args()
    raws = sorted(p for p in args.raw_dir.iterdir() if p.suffix.lower() in RAW_SUFFIXES)
    if args.scenes:
        raws = [p for p in raws if p.stem in args.scenes]
    written, skipped = 0, []
    for raw in raws:
        neutral, description = lightroom_neutral(raw.stem)
        if description is None:
            skipped.append(raw.stem)
            continue
        for control in args.controls:
            folder = args.out / control
            folder.mkdir(parents=True, exist_ok=True)
            for value in VALUES:
                tag = f"{value:+d}".replace("+", "")
                target = folder / f"{raw.stem}-{control}-{tag}{raw.suffix}"
                xmp = target.with_suffix(".xmp")
                if target.exists() or xmp.exists():
                    sys.exit(f"Existe déjà, rien n'est écrasé : {target}")
                os.link(raw, target)
                xmp.write_text('<?xpacket begin="﻿" id="W5M0MpCehiHzreSzNTczkc9d"?>\n'
                               + sidecar(description, target.name, FIELDS[control], value)
                               + '\n<?xpacket end="w"?>\n', encoding="utf-8")
                written += 1
        print(f"{raw.stem} : neutre {neutral.name}")
    print(f"{written} variantes préparées dans {args.out} (liens physiques, aucun espace disque en plus).")
    if skipped:
        print("Sans neutre LR exploitable (ignorées) :", ", ".join(skipped))


if __name__ == "__main__":
    main()
