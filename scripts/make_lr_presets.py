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
       python scripts/make_lr_presets.py --controls grading   (61 colour grading states)
       python scripts/make_lr_presets.py --controls grading-blend   (38 blending and balance states)
       python scripts/make_lr_presets.py --controls grading-luminance   (58 luminance and global states)
       python scripts/make_lr_presets.py --controls curves   (36 point curve states)
       python scripts/make_lr_presets.py --controls exposure-contrast   (20 intermediate states)
       python scripts/make_lr_presets.py --controls white-balance   (15 temperature and tint states)
       python scripts/make_lr_presets.py --controls sharpening   (16 states on a full-resolution centre crop)
       python scripts/make_lr_presets.py --controls noise   (11 states on a full-resolution centre crop)
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
# Colour grading: per zone, Lightroom's hue and saturation fields (shadows and highlights
# keep their split-toning names) and labels. States, wide by design (more comparison
# points to place the breakpoints): a hue turn every 30 degrees at saturation 50, and
# saturation 10 to 100 at an orange and a blue hue.
GRADING_ZONES = (("shadows", "SplitToningShadowHue", "SplitToningShadowSaturation", "Ombres"),
                 ("midtones", "ColorGradeMidtoneHue", "ColorGradeMidtoneSat", "Tons-moyens"),
                 ("highlights", "SplitToningHighlightHue", "SplitToningHighlightSaturation", "Hautes-lumieres"))
GRADING_STATES = ([(hue, 50) for hue in range(0, 360, 30)]
                  + [(hue, saturation) for hue in (30, 210) for saturation in (10, 25, 50, 75, 100) if saturation != 50])
GRADING = ("IPR Grading", "IPR-Grading-presets.zip")
# Blending and balance (Lightroom defaults 50 and 0): each zone alone at 30 degrees and
# saturation 50 across both sliders, then shadows 210 + highlights 30 across blending
# and at balance -50 / +50, and the three zones together (additivity). Each state:
# (label, {zone: (hue, saturation)}, blending, balance).
SINGLE = (30, 50)
GRADING_BLEND_STATES = (
    [(f"{label} F{b:03d}", {zone: SINGLE}, b, 0) for zone, _, _, label in GRADING_ZONES for b in (0, 25, 75, 100)]
    + [(f"{label} B{v:+04d}", {zone: SINGLE}, 50, v) for zone, _, _, label in GRADING_ZONES
       for v in (-100, -50, -25, 25, 50, 100)]
    + [(f"Ombres-HL F{b:03d}", {"shadows": (210, 50), "highlights": (30, 50)}, b, 0) for b in (0, 25, 50, 75, 100)]
    + [(f"Ombres-HL B{v:+04d}", {"shadows": (210, 50), "highlights": (30, 50)}, 50, v) for v in (-50, 50)]
    + [("Trois-zones F050", {"shadows": (210, 50), "midtones": (120, 50), "highlights": (30, 50)}, 50, 0)])
GRADING_BLEND = ("IPR Grading fusion", "IPR-Grading-fusion-presets.zip")
# Luminance of each zone and of the global wheel, alone at intermediate values, then at
# -50 / +50 with a colour (30 degrees, saturation 50) for their interaction; the global
# wheel's colour: a hue turn at saturation 50, saturation 25 to 100 at 30 and 210 degrees.
# Each state: (label, {Lightroom field: value}).
LUMINANCE_FIELDS = (("Ombres", "ColorGradeShadowLum", "SplitToningShadowHue", "SplitToningShadowSaturation"),
                    ("Tons-moyens", "ColorGradeMidtoneLum", "ColorGradeMidtoneHue", "ColorGradeMidtoneSat"),
                    ("Hautes-lumieres", "ColorGradeHighlightLum", "SplitToningHighlightHue",
                     "SplitToningHighlightSaturation"),
                    ("Global", "ColorGradeGlobalLum", "ColorGradeGlobalHue", "ColorGradeGlobalSat"))
GRADING_LUMINANCE_STATES = (
    [(f"{label} L{v:+04d}", {lum: v}) for label, lum, _, _ in LUMINANCE_FIELDS
     for v in (-100, -50, -25, -10, 10, 25, 50, 100)]
    + [(f"{label} L{v:+04d} T030 S050", {lum: v, hue: 30, sat: 50}) for label, lum, hue, sat in LUMINANCE_FIELDS
       for v in (-50, 50)]
    + [(f"Global T{h:03d} S{s:03d}", {"ColorGradeGlobalHue": h, "ColorGradeGlobalSat": s})
       for h, s in [(h, 50) for h in range(0, 360, 30)] + [(h, s) for h in (30, 210) for s in (25, 75, 100)]])
GRADING_LUMINANCE = ("IPR Grading luminance", "IPR-Grading-luminance-presets.zip")
# Point curves, in Lightroom's 0-255 coordinates: the composite RGB curve and each
# channel's, one curve changed per state, the others linear. Composite: the midpoint
# at six heights, the quarter points, the ends moved, S and inverse S, a four-point
# curve; each channel: its midpoint at four heights and its quarter points. Each
# state: (label, {curve field: points}).
CURVE_FIELDS = (("RVB", "ToneCurvePV2012"), ("Rouge", "ToneCurvePV2012Red"),
                ("Vert", "ToneCurvePV2012Green"), ("Bleu", "ToneCurvePV2012Blue"))
LINEAR = ((0, 0), (255, 255))


def _through(*points):
    return ((0, 0),) + points + ((255, 255),)


CURVE_STATES = (
    [(f"RVB M{d:+03d}", {"ToneCurvePV2012": _through((128, 128 + d))}) for d in (-64, -32, -16, 16, 32, 64)]
    + [(f"RVB Q{x:03d}{d:+03d}", {"ToneCurvePV2012": _through((x, x + d))}) for x in (64, 192) for d in (-32, 32)]
    + [("RVB noir+32", {"ToneCurvePV2012": ((0, 32), (255, 255))}),
       ("RVB blanc-32", {"ToneCurvePV2012": ((0, 0), (255, 223))}),
       ("RVB coupe-noirs32", {"ToneCurvePV2012": ((32, 0), (255, 255))}),
       ("RVB coupe-blancs32", {"ToneCurvePV2012": ((0, 0), (223, 255))}),
       ("RVB S-doux", {"ToneCurvePV2012": _through((64, 48), (192, 208))}),
       ("RVB S-fort", {"ToneCurvePV2012": _through((64, 32), (192, 224))}),
       ("RVB S-inverse", {"ToneCurvePV2012": _through((64, 80), (192, 176))}),
       ("RVB quatre-points", {"ToneCurvePV2012": _through((32, 40), (96, 128), (160, 168), (224, 240))})]
    + [(f"{label} M{d:+03d}", {field: _through((128, 128 + d))})
       for label, field in CURVE_FIELDS[1:] for d in (-64, -32, 32, 64)]
    + [(f"{label} Q064+32", {field: _through((64, 96))}) for label, field in CURVE_FIELDS[1:]]
    + [(f"{label} Q192-32", {field: _through((192, 160))}) for label, field in CURVE_FIELDS[1:]])
CURVES = ("IPR Courbes", "IPR-Courbes-presets.zip")
# Campaigns of the beta validation: (group, archive, states), each state (label, {field: value}).
# Exposure and contrast at intermediate values; neutral: grading/neutre.
EXPOSURE_CONTRAST = [(f"Exposition {v:+.2f}", {"Exposure2012": f"{v:+.2f}"})
                     for v in (-2.0, -1.5, -1.0, -0.5, -0.25, 0.25, 0.5, 1.0, 1.5, 2.0)] +                     [(f"Contraste {v:+d}", {"Contrast2012": f"{v:+d}"}) for v in (-100, -75, -50, -25, -10, 10, 25, 50, 75, 100)]
# White balance as the colour protocol: manual anchor 5200 K / tint 0 (not Lightroom's As Shot);
# temperature at tint 0, tint at 5200 K.
WHITE_BALANCE = [(f"Temperature {k}", {"WhiteBalance": "Custom", "Temperature": str(k), "Tint": "0"})
                 for k in (2800, 3500, 4300, 5200, 6000, 7500, 10000)] +                 [(f"Nuance {t:+d}", {"WhiteBalance": "Custom", "Temperature": "5200", "Tint": f"{t:+d}"})
                 for t in (-100, -50, -25, -10, 10, 25, 50, 100)]
# Sharpening on the centre sixteenth of the frame, exported at full resolution (a 2048-pixel
# export would wash the sharpening out); the neutral is the same crop at amount 0. Lightroom's
# defaults: radius 1.0, detail 25, masking 0.
SHARPENING_CROP = {"HasCrop": "True", "CropTop": "0.375", "CropLeft": "0.375", "CropBottom": "0.625",
                   "CropRight": "0.625", "CropAngle": "0", "CropConstrainToWarp": "0"}


def _sharpening(amount, radius=1.0, detail=25, masking=0):
    return {**SHARPENING_CROP, "Sharpness": str(amount), "SharpenRadius": f"{radius:+.1f}",
            "SharpenDetail": str(detail), "SharpenEdgeMasking": str(masking)}


SHARPENING = ([("Nettete neutre", _sharpening(0))]
              + [(f"Nettete gain {a}", _sharpening(a)) for a in (25, 50, 75, 100, 150)]
              + [(f"Nettete rayon {r:.1f}", _sharpening(75, radius=r)) for r in (0.5, 1.5, 2.0, 3.0)]
              + [(f"Nettete detail {d}", _sharpening(75, detail=d)) for d in (0, 50, 100)]
              + [(f"Nettete masquage {m}", _sharpening(75, masking=m)) for m in (25, 50, 75)])
# Noise reduction on the centre quarter of the frame (half of each side), exported at full
# resolution like the sharpening; Lightroom's detail, contrast and smoothness at their defaults.
# "Bruit defaut RAW" is what Lightroom applies to a RAW untouched: colour 25 and sharpening 40.
NOISE_CROP = {"HasCrop": "True", "CropTop": "0.25", "CropLeft": "0.25", "CropBottom": "0.75",
              "CropRight": "0.75", "CropAngle": "0", "CropConstrainToWarp": "0"}


def _noise(luminance, colour, sharpness=0):
    return {**NOISE_CROP, "LuminanceSmoothing": str(luminance), "LuminanceNoiseReductionDetail": "50",
            "LuminanceNoiseReductionContrast": "0", "ColorNoiseReduction": str(colour),
            "ColorNoiseReductionDetail": "50", "ColorNoiseReductionSmoothness": "50",
            "Sharpness": str(sharpness), "SharpenRadius": "+1.0", "SharpenDetail": "25", "SharpenEdgeMasking": "0"}


NOISE = ([("Bruit neutre", _noise(0, 0))]
         + [(f"Bruit luminance {v}", _noise(v, 0)) for v in (25, 50, 75, 100)]
         + [(f"Bruit couleur {v}", _noise(0, v)) for v in (25, 50, 100)]
         + [("Bruit luminance 50 couleur 25", _noise(50, 25)), ("Bruit luminance 100 couleur 100", _noise(100, 100)),
            ("Bruit defaut RAW", _noise(0, 25, sharpness=40))])
CAMPAIGNS = {"exposure-contrast": ("IPR Exposition contraste", "IPR-Exposition-contraste-presets.zip", EXPOSURE_CONTRAST),
             "white-balance": ("IPR Balance blancs", "IPR-Balance-blancs-presets.zip", WHITE_BALANCE),
             "sharpening": ("IPR Nettete", "IPR-Nettete-presets.zip", SHARPENING),
             "noise": ("IPR Bruit", "IPR-Bruit-presets.zip", NOISE)}
# Settings of the current Lightroom neutral references (6V1A0037, 11 October 2026).
BASELINE = {
    # No crop unless a campaign sets one: the sharpening crop must not stay on the photo.
    "HasCrop": "False",
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


# Companion settings of the grain amount; the analysis ignores them, and the seed
# Lightroom adds on export, when it pairs a grain state with its neutral
# (tests/analysis/inputs.py).
GRAIN_COMPANIONS = {"GrainSize": "25", "GrainFrequency": "50"}


def alt(tag, text):
    return f'   <crs:{tag}>\n    <rdf:Alt>\n     <rdf:li xml:lang="x-default">{text}</rdf:li>\n    </rdf:Alt>\n   </crs:{tag}>\n'


def seq(tag, points):
    items = "".join(f"     <rdf:li>{x}, {y}</rdf:li>\n" for x, y in points)
    return f"   <crs:{tag}>\n    <rdf:Seq>\n{items}    </rdf:Seq>\n   </crs:{tag}>\n"


def preset(group, control=None, value=None, grading=None, blend=None, fields=None, curves=None, campaign=None):
    settings = dict(BASELINE)
    curve_elements = ""
    if campaign is not None:
        label, values = campaign
        settings.update(values)
        name = f"IPR {label}"
    elif curves is not None:
        label, changed = curves
        # Every point curve explicit, so a preset gives the same state whatever came before.
        settings["ToneCurveName2012"] = "Custom"
        curve_elements = "".join(seq(field, changed.get(field, LINEAR)) for _, field in CURVE_FIELDS)
        name = f"IPR Courbes {label}"
    elif fields is not None:
        label, values = fields
        settings.update({field: f"{v:+d}" if field.endswith("Lum") else str(v) for field, v in values.items()})
        name = f"IPR Grading {label}"
    elif blend is not None:
        label, zones, blending, balance = blend
        fields = {zone: (hue_field, saturation_field) for zone, hue_field, saturation_field, _ in GRADING_ZONES}
        for zone, (hue, saturation) in zones.items():
            settings[fields[zone][0]], settings[fields[zone][1]] = str(hue), str(saturation)
        settings["ColorGradeBlending"], settings["SplitToningBalance"] = str(blending), f"{balance:+d}" if balance else "0"
        name = f"IPR Grading {label}"
    elif grading is not None:
        (zone, hue_field, saturation_field, label), hue, saturation = grading
        settings[hue_field], settings[saturation_field] = str(hue), str(saturation)
        name = f"IPR Grading {label} T{hue:03d} S{saturation:03d}"
    elif control is None:
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
        + curve_elements
        + '  </rdf:Description>\n </rdf:RDF>\n</x:xmpmeta>\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--controls", nargs="+", default=["highlights"], choices=sorted(LABELS) + ["mixer", "grading", "grading-blend", "grading-luminance", "curves"] + sorted(CAMPAIGNS))
    parser.add_argument("--out", type=Path, default=ROOT / "tests/image/lr-presets")
    args = parser.parse_args()
    grading = "grading" in args.controls
    blend = "grading-blend" in args.controls
    luminance = "grading-luminance" in args.controls
    curves = "curves" in args.controls
    campaign = next((c for c in args.controls if c in CAMPAIGNS), None)
    if (grading or blend or luminance or curves or campaign) and len(args.controls) > 1:
        sys.exit("Grading et courbes ont leur propre panneau : --controls grading, grading-blend, "
                 "grading-luminance ou curves seul")
    args.controls = [c for control in args.controls for c in (MIXER if control == "mixer" else [control])]
    panels = ({CAMPAIGNS[campaign][:2]} if campaign else {GRADING} if grading else {GRADING_BLEND} if blend else {GRADING_LUMINANCE} if luminance
              else {CURVES} if curves else {PANELS.get(c, LUMIERES) for c in args.controls})
    if len(panels) > 1:
        sys.exit("Un seul panneau par archive : " + ", ".join(sorted(g for g, _ in panels)))
    group, archive_name = panels.pop()
    folder, archive = args.out / group, args.out / archive_name
    if archive.exists() or folder.exists():
        sys.exit(f"Existe déjà, rien n'est écrasé : {archive if archive.exists() else folder}")
    folder.mkdir(parents=True)
    if campaign:
        presets = [preset(group, campaign=state) for state in CAMPAIGNS[campaign][2]]
    elif curves:   # the neutral is the grading campaign's (grading/neutre), same geometry
        presets = [preset(group, curves=state) for state in CURVE_STATES]
    elif luminance:   # the neutral is the grading campaign's (grading/neutre)
        presets = [preset(group, fields=state) for state in GRADING_LUMINANCE_STATES]
    elif blend:   # the neutral is the grading campaign's (grading/neutre)
        presets = [preset(group, blend=state) for state in GRADING_BLEND_STATES]
    elif grading:
        presets = [preset(group)] + [preset(group, grading=(zone, hue, saturation))
                                     for zone in GRADING_ZONES for hue, saturation in GRADING_STATES]
    else:
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
