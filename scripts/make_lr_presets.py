"""Write self-contained Lightroom develop presets for the reference exports of a panel.

Each preset sets the whole IPR neutral baseline, then the tested control:
Camera Neutral profile, white balance As Shot, lens profile, chromatic
aberration, vignetting and defringe off, sharpening and noise reduction at 0,
presence, colour, HSL, grading, grain and effects at 0, linear tone curve, no
HDR, and the Lumieres sliders at 0, every slider at 0 except the tested one. "IPR Neutre" is
the baseline alone. A preset applied to any RAW gives the same state, whatever
its previous settings: no neutral needs to be prepared first.
Import the .zip from Lightroom's Presets panel; never overwrites a file.

Usage: python scripts/make_lr_presets.py [--controls highlights shadows ...]
           [--out tests/image/lr-presets]
       python scripts/make_lr_presets.py --controls vibrance saturation
       python scripts/make_lr_presets.py --controls mixer   (24 mixer controls)
       python scripts/make_lr_presets.py --controls texture clarity dehaze grain
           (grain at Lightroom's default size 25 and roughness 50)
"""
import argparse
import sys
import uuid
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIELDS = {"exposure": "Exposure2012", "contrast": "Contrast2012", "highlights": "Highlights2012",
          "shadows": "Shadows2012", "whites": "Whites2012", "blacks": "Blacks2012",
          "vibrance": "Vibrance", "saturation": "Saturation",
          "texture": "Texture", "clarity": "Clarity2012", "dehaze": "Dehaze", "grain": "GrainAmount"}
LABELS = {"exposure": "Exposition", "contrast": "Contraste", "highlights": "HL", "shadows": "Ombres",
          "whites": "Blancs", "blacks": "Noirs", "vibrance": "Vibrance", "saturation": "Saturation",
          "texture": "Texture", "clarity": "Clarte", "dehaze": "Voile", "grain": "Grain"}
# Same states as iprGenerateTests (src/tools/TestRecipes.cpp), neutral excepted.
VALUES = {"exposure": (-1.0, -0.5, 0.5, 1.0), "vibrance": (-100, -50, -25, 25, 50, 100),
          "saturation": (-100, -50, -25, 25, 50, 100), "texture": (-100, -50, -25, 25, 50, 100),
          "clarity": (-100, -50, -25, 25, 50, 100), "dehaze": (-100, -50, -25, 25, 50, 100),
          "grain": (25, 50, 100)}
DEFAULT_VALUES = (-100, -75, -50, -25, 25, 50, 75, 100)
# Lightroom preset group and archive per panel.
PANELS = {"vibrance": ("IPR Couleur", "IPR-Couleur-presets.zip"),
          "saturation": ("IPR Couleur", "IPR-Couleur-presets.zip"),
          **{c: ("IPR Effets", "IPR-Effets-presets.zip") for c in ("texture", "clarity", "dehaze", "grain")}}
LUMIERES = ("IPR Lumières complet", "IPR-Lumieres-complet-presets.zip")
BANDS = ("Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple", "Magenta")
# Colour mixer: IPR band and component names (TestRecipes), Lightroom field parts, labels.
MIXER_BANDS = (("red", "Red", "Rouge"), ("orange", "Orange", "Orange"), ("yellow", "Yellow", "Jaune"),
               ("green", "Green", "Vert"), ("cyan", "Aqua", "Cyan"), ("blue", "Blue", "Bleu"),
               ("violet", "Purple", "Violet"), ("magenta", "Magenta", "Magenta"))
MIXER_COMPONENTS = (("hue", "Hue", "Teinte"), ("saturation", "Saturation", "Saturation"),
                    ("luminance", "Luminance", "Luminance"))
MIXER = []
for band, lr_band, band_label in MIXER_BANDS:
    for component, lr_component, component_label in MIXER_COMPONENTS:
        control = f"mixer-{band}-{component}"
        MIXER.append(control)
        FIELDS[control] = f"{lr_component}Adjustment{lr_band}"
        LABELS[control] = f"Mélangeur {band_label} {component_label}"
        VALUES[control] = (-100, -50, -25, 25, 50, 100)
        PANELS[control] = ("IPR Mélangeur", "IPR-Melangeur-presets.zip")
# Settings of the current Lightroom neutral references (6V1A0037, 11 October 2026).
BASELINE = {
    "WhiteBalance": "As Shot", "CameraProfile": "Camera Neutral", "ConvertToGrayscale": "False",
    "HDREditMode": "0", "Texture": "0", "Clarity2012": "0", "Dehaze": "0", "Vibrance": "0", "Saturation": "0",
    "ParametricShadows": "0", "ParametricDarks": "0", "ParametricLights": "0", "ParametricHighlights": "0",
    "ParametricShadowSplit": "25", "ParametricMidtoneSplit": "50", "ParametricHighlightSplit": "75",
    "ToneCurveName2012": "Linear", "Sharpness": "0", "LuminanceSmoothing": "0", "ColorNoiseReduction": "0",
    "LensProfileEnable": "0", "AutoLateralCA": "0", "VignetteAmount": "0", "LensManualDistortionAmount": "0",
    "DefringePurpleAmount": "0", "DefringeGreenAmount": "0", "PerspectiveUpright": "0",
    "PostCropVignetteAmount": "0", "GrainAmount": "0", "ShadowTint": "0",
    "RedHue": "0", "RedSaturation": "0", "GreenHue": "0", "GreenSaturation": "0", "BlueHue": "0", "BlueSaturation": "0",
    "ColorGradeMidtoneHue": "0", "ColorGradeMidtoneSat": "0", "ColorGradeMidtoneLum": "0",
    "ColorGradeShadowLum": "0", "ColorGradeHighlightLum": "0", "ColorGradeGlobalHue": "0",
    "ColorGradeGlobalSat": "0", "ColorGradeGlobalLum": "0", "ColorGradeBlending": "50",
    "SplitToningShadowHue": "0", "SplitToningShadowSaturation": "0", "SplitToningHighlightHue": "0",
    "SplitToningHighlightSaturation": "0", "SplitToningBalance": "0",
    **{f"{kind}Adjustment{band}": "0" for kind in ("Hue", "Saturation", "Luminance") for band in BANDS},
    **{field: ("0.00" if field == "Exposure2012" else "0") for field in FIELDS.values()},
}


# Companion settings of the grain amount; the analysis ignores them when it pairs a
# grain state with its neutral (tests/analysis/inputs.py).
GRAIN_COMPANIONS = {"GrainSize": "25", "GrainFrequency": "50"}


def alt(tag, text):
    return f'   <crs:{tag}>\n    <rdf:Alt>\n     <rdf:li xml:lang="x-default">{text}</rdf:li>\n    </rdf:Alt>\n   </crs:{tag}>\n'


def preset(group, control=None, value=None):
    settings = dict(BASELINE)
    if control is None:
        name = "IPR Neutre"
    else:
        settings[FIELDS[control]] = f"{value:+.2f}" if control == "exposure" else f"{value:+d}"
        if control == "grain":   # Lightroom's defaults, set so that every grain state is explicit
            settings.update(GRAIN_COMPANIONS)
        name = f"IPR {LABELS[control]} {value:+g}"
    attributes = "".join(f'   crs:{k}="{v}"\n' for k, v in settings.items())
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
        + alt("Name", name) + alt("ShortName", "") + alt("SortName", "") + alt("Group", group) + alt("Description", "")
        + '  </rdf:Description>\n </rdf:RDF>\n</x:xmpmeta>\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--controls", nargs="+", default=["highlights"], choices=sorted(LABELS) + ["mixer"])
    parser.add_argument("--out", type=Path, default=ROOT / "tests/image/lr-presets")
    args = parser.parse_args()
    args.controls = [c for control in args.controls for c in (MIXER if control == "mixer" else [control])]
    panels = {PANELS.get(c, LUMIERES) for c in args.controls}
    if len(panels) > 1:
        sys.exit("Un seul panneau par archive : " + ", ".join(sorted(g for g, _ in panels)))
    group, archive_name = panels.pop()
    folder, archive = args.out / group, args.out / archive_name
    if archive.exists() or folder.exists():
        sys.exit(f"Existe déjà, rien n'est écrasé : {archive if archive.exists() else folder}")
    folder.mkdir(parents=True)
    presets = [preset(group)] + [preset(group, c, v) for c in args.controls for v in VALUES.get(c, DEFAULT_VALUES)]
    files = []
    for name, text in presets:
        path = folder / f"{name.replace(' ', '_')}.xmp"
        path.write_text(text, encoding="utf-8")
        files.append(path)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for path in files:
            z.write(path, f"{group}/{path.name}")
    print(f"{len(files)} préréglages dans {folder}\nArchive à importer dans Lightroom : {archive}")


if __name__ == "__main__":
    main()
