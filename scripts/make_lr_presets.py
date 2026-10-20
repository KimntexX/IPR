"""Write Lightroom develop presets for the Lumieres reference exports.

One preset per control and value (group "IPR Lumières"). Each preset sets all
six Lumieres sliders: the tested one to its value, the others to 0, and
nothing else, so it can be applied in turn to RAW files that already carry
their Lightroom neutral (Camera Neutral, optics off, no sharpening or noise
reduction) without accumulating. Import the .zip from Lightroom's Presets
panel; never overwrites a file.

Usage: python scripts/make_lr_presets.py [--controls highlights shadows whites]
           [--out tests/image/lr-presets]
"""
import argparse
import sys
import uuid
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIELDS = {"exposure": "Exposure2012", "contrast": "Contrast2012", "highlights": "Highlights2012",
          "shadows": "Shadows2012", "whites": "Whites2012", "blacks": "Blacks2012"}
LABELS = {"contrast": "Contraste", "highlights": "HL", "shadows": "Ombres", "whites": "Blancs", "blacks": "Noirs"}
VALUES = (-100, -75, -50, -25, 25, 50, 75, 100)
GROUP = "IPR Lumières"


def alt(tag, text):
    return f'   <crs:{tag}>\n    <rdf:Alt>\n     <rdf:li xml:lang="x-default">{text}</rdf:li>\n    </rdf:Alt>\n   </crs:{tag}>\n'


def preset(control, value):
    sliders = {field: ("0.00" if field == "Exposure2012" else "0") for field in FIELDS.values()}
    sliders[FIELDS[control]] = f"{value:+d}"
    name = f"IPR {LABELS[control]} {value:+d}"
    attributes = "".join(f'   crs:{k}="{v}"\n' for k, v in sliders.items())
    return name, (
        '<x:xmpmeta xmlns:x="adobe:ns:meta/" x:xmptk="Adobe XMP Core 7.0-c000 1.000000, 0000/00/00-00:00:00        ">\n'
        ' <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">\n'
        '  <rdf:Description rdf:about=""\n'
        '    xmlns:crs="http://ns.adobe.com/camera-raw-settings/1.0/"\n'
        '   crs:PresetType="Normal"\n   crs:Cluster=""\n'
        f'   crs:UUID="{uuid.uuid4().hex.upper()}"\n'
        '   crs:SupportsAmount="False"\n   crs:SupportsColor="True"\n   crs:SupportsMonochrome="True"\n'
        '   crs:SupportsHighDynamicRange="True"\n   crs:SupportsNormalDynamicRange="True"\n'
        '   crs:SupportsSceneReferred="True"\n   crs:SupportsOutputReferred="True"\n'
        '   crs:RequiresRGBTables="False"\n   crs:CameraModelRestriction=""\n'
        '   crs:Copyright=""\n   crs:ContactInfo=""\n'
        '   crs:Version="18.7"\n   crs:ProcessVersion="15.4"\n'
        + attributes +
        '   crs:HasSettings="True">\n'
        + alt("Name", name) + alt("ShortName", "") + alt("SortName", "") + alt("Group", GROUP) + alt("Description", "")
        + '  </rdf:Description>\n </rdf:RDF>\n</x:xmpmeta>\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--controls", nargs="+", default=["highlights", "shadows", "whites"], choices=sorted(LABELS))
    parser.add_argument("--out", type=Path, default=ROOT / "tests/image/lr-presets")
    args = parser.parse_args()
    folder = args.out / GROUP
    folder.mkdir(parents=True, exist_ok=True)
    archive = args.out / "IPR-Lumieres-presets.zip"
    if archive.exists():
        sys.exit(f"Existe déjà, rien n'est écrasé : {archive}")
    files = []
    for control in args.controls:
        for value in VALUES:
            name, text = preset(control, value)
            path = folder / f"{name.replace(' ', '_')}.xmp"
            if path.exists():
                sys.exit(f"Existe déjà, rien n'est écrasé : {path}")
            path.write_text(text, encoding="utf-8")
            files.append(path)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for path in files:
            z.write(path, f"{GROUP}/{path.name}")
    print(f"{len(files)} préréglages dans {folder}\nArchive à importer dans Lightroom : {archive}")


if __name__ == "__main__":
    main()
