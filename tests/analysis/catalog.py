"""One group per current photographic protocol; no invented controls."""

GROUPS = {
    "raw": ("neutral", "auto-wb"),
    "lumiere": ("exposure", "contrast", "shadows", "blacks", "highlights", "whites"),
    "courbes": tuple("curve-" + c for c in ("luminance", "red", "green", "blue")),
    "couleur": ("temperature", "tint", "vibrance", "saturation", "grayscale"),
    "melangeur": tuple("mixer-" + b + "-" + c
                       for b in ("red", "orange", "yellow", "green", "cyan", "blue", "violet", "magenta")
                       for c in ("hue", "saturation", "luminance")),
    "grading": tuple("grading-" + z + "-" + c
                     for z in ("shadows", "midtones", "highlights") for c in ("hue", "saturation")),
    "effets": ("texture", "clarity", "dehaze", "grain", "blur"),
    "bruit": ("denoise-luminance", "denoise-chrominance"),
    "nettete": ("sharpening-amount", "sharpening-radius", "sharpening-masking"),
    "optique": ("lens-correction", "chromatic-aberration"),
}
PROTOCOLS = {g: "docs/validation/protocols/protocole-" + p + ".md" for g, p in
             {"raw": "raw-profils", "lumiere": "lumieres", "courbes": "courbes",
              "couleur": "couleur", "melangeur": "melangeur", "grading": "grading",
              "effets": "effets", "bruit": "bruit", "nettete": "nettete", "optique": "optique"}.items()}
SPATIAL = {"effets", "bruit", "nettete"}
ALIASES = {"color-mixer": "melangeur", "lumieres": "lumiere", "color": "couleur"}


def group_for(feature):
    for group, controls in GROUPS.items():
        if feature in controls:
            return group
    raise ValueError("Unknown photographic feature: " + str(feature))


def bounds_for(feature):
    if feature == "neutral":
        return 0, 0
    if feature == "temperature":
        return 2000, 50000
    if feature == "tint":
        return -150, 150
    if feature == "exposure":
        return -5, 5
    if feature in ("auto-wb", "grayscale", "lens-correction", "chromatic-aberration"):
        return 0, 1
    if feature == "blur":
        return 0, 20
    if feature == "sharpening-amount":
        return 0, 200
    if feature == "sharpening-radius":
        return .3, 3
    if feature.startswith("grading-") and feature.endswith("-hue"):
        return 0, 359
    if feature.startswith(("denoise-", "sharpening-", "grading-")) or feature == "grain":
        return 0, 100
    return -100, 100
