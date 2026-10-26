"""Protocol usability is separate from computing diagnostic metrics."""
from copy import deepcopy
import math

FIELDS = {"exposure": "Exposure2012", "contrast": "Contrast2012", "shadows": "Shadows2012",
          "blacks": "Blacks2012", "highlights": "Highlights2012", "whites": "Whites2012",
          "temperature": "Temperature", "tint": "Tint", "vibrance": "Vibrance",
          "saturation": "Saturation", "texture": "Texture", "clarity": "Clarity2012",
          "dehaze": "Dehaze", "grain": "GrainAmount", "denoise-luminance": "LuminanceSmoothing",
          "denoise-chrominance": "ColorNoiseReduction", "sharpening-amount": "Sharpness",
          "sharpening-radius": "SharpenRadius", "sharpening-masking": "SharpenEdgeMasking"}
IPR_FIELDS = {**{f: f for f in FIELDS}, "denoise-luminance": "denoiseLuminance",
              "denoise-chrominance": "denoiseChrominance", "sharpening-amount": "sharpeningAmount",
              "sharpening-radius": "sharpeningRadius", "sharpening-masking": "sharpeningMasking",
              "auto-wb": "automaticWhiteBalance", "grayscale": "grayscale", "blur": "blur"}
ZERO_IPR = ("exposure", "contrast", "shadows", "blacks", "highlights", "whites", "tint",
            "vibrance", "saturation", "texture", "clarity", "dehaze", "grain", "blur",
            "denoiseLuminance", "denoiseChrominance", "sharpeningAmount", "grayscale",
            "automaticWhiteBalance")


def neutral_ipr(settings, support=False):
    required = set(ZERO_IPR) | {"temperature", "mixerHue", "mixerSaturation", "mixerLuminance", "grading", "toneCurves"}
    missing = required - settings.keys()
    problems = ["incomplete_reference_settings:" + ",".join(sorted(missing))] if missing else []
    if support:
        return problems
    nonzero = [key for key in ZERO_IPR if key in settings and finite_number(settings[key]) != 0]
    if nonzero:
        problems.append("nonneutral_reference:" + ",".join(nonzero))
    for key in ("mixerHue", "mixerSaturation", "mixerLuminance"):
        if key in settings and (len(settings[key]) != 8 or any(v != 0 for v in settings[key])):
            problems.append("nonneutral_or_legacy_reference:" + key)
    # Shadows, midtones, highlights and the global wheel; colour and luminance at zero.
    if "grading" in settings and (len(settings["grading"]) != 4 or any(
            z.get("saturation") != 0 or z.get("luminance", 0) != 0 for z in settings["grading"])):
        problems.append("nonneutral_reference:grading")
    if "toneCurves" in settings and (len(settings["toneCurves"]) != 4 or
            any(curve not in ([], [[0, 0], [1, 1]]) for curve in settings["toneCurves"])):
        problems.append("nonneutral_reference:curves")
    return problems


def finite_number(value):
    try:
        number = float(value)
        return number if math.isfinite(number) else None
    except (ValueError, TypeError):
        return None


def field_for(feature):
    if feature.startswith("mixer-"):
        _, band, component = feature.split("-")
        band = {"cyan": "Aqua", "violet": "Purple"}.get(band, band.capitalize())
        return component.capitalize() + "Adjustment" + band
    return FIELDS.get(feature)


def ipr_state(record):
    settings, baseline = deepcopy(record.get("settings")), deepcopy(record.get("reference_settings"))
    feature, value = record["feature"], record["value"]
    if not isinstance(settings, dict) or not isinstance(baseline, dict):
        return ["missing_declared_settings"], []
    warnings, supports = neutral_ipr(baseline, record.get("reference_role") == "support"), []
    if feature in IPR_FIELDS:
        key = IPR_FIELDS[feature]
        observed = finite_number(settings.get(key))
        matches = (observed is not None and math.isclose(observed, value, rel_tol=0, abs_tol=1e-7)
                   if feature == "sharpening-radius" else observed == value)
        if not matches:
            warnings.append("declared_value_mismatch")
        settings.pop(key, None)
        baseline.pop(key, None)
        if feature in ("sharpening-radius", "sharpening-masking"):
            amount = finite_number(settings.get("sharpeningAmount"))
            if amount is None or amount <= 0:
                warnings.append("inactive_sharpening_support")
            supports.append({"sharpeningAmount": amount})
            settings.pop("sharpeningAmount", None)
            baseline.pop("sharpeningAmount", None)
    elif feature.startswith("mixer-"):
        _, band, component = feature.split("-")
        index = ("red", "orange", "yellow", "green", "cyan", "blue", "violet", "magenta").index(band)
        key = "mixer" + component.capitalize()
        if len(settings.get(key, [])) != 8 or len(baseline.get(key, [])) != 8:
            return ["missing_eight_band_settings"], []
        if settings[key][index] != value:
            warnings.append("declared_value_mismatch")
        settings[key][index] = baseline[key][index]
    elif feature.startswith("grading-"):
        _, zone, component = feature.split("-")
        index = ("shadows", "midtones", "highlights").index(zone)
        if len(settings.get("grading", [])) != 4 or len(baseline.get("grading", [])) != 4:
            return ["missing_grading_settings"], []
        if settings["grading"][index].get(component) != value:
            warnings.append("declared_value_mismatch")
        supports.append(dict(settings["grading"][index]))
        if component == "hue" and settings["grading"][index].get("saturation", 0) <= 0:
            warnings.append("inactive_grading_support")
        settings["grading"][index] = baseline["grading"][index]
    elif feature.startswith("curve-"):
        index = ("luminance", "red", "green", "blue").index(feature[6:])
        if len(settings.get("toneCurves", [])) != 4 or len(baseline.get("toneCurves", [])) != 4:
            return ["missing_curve_points"], []
        expected = [[0, 0], [.5, .5 + .004 * value], [1, 1]]
        actual = settings["toneCurves"][index]
        if (len(actual) != 3 or any(len(p) != 2 for p in actual) or
                any(not math.isclose(float(a), float(b), rel_tol=0, abs_tol=1e-7)
                    for point, target in zip(actual, expected) for a, b in zip(point, target))):
            warnings.append("diagnostic_curve_points_mismatch")
        settings["toneCurves"][index] = baseline["toneCurves"][index]
    elif feature != "neutral":
        warnings.append("state_audit_not_implemented")
    if settings != baseline:
        warnings.append("other_settings_changed")
    if not record.get("expected_sha256"):
        warnings.append("declared_ipr_state_without_pixel_hash")
    if record.get("reference_role") == "support":
        supports.append({"explicit_reference_role": "support", "neutrality_not_asserted": True})
    return warnings, supports


def audit(record, metadata, reference):
    if record["engine"] == "IPR":
        return ipr_state(record)
    settings, baseline = metadata["xmp_settings"], reference["xmp_settings"]
    warnings = []
    field = field_for(record["feature"])
    if not settings or not baseline:
        warnings.append("missing_lr_xmp")
    if record.get("reference_role") != "support":
        zero_fields = ("Exposure2012", "Contrast2012", "Shadows2012", "Blacks2012", "Highlights2012", "Whites2012",
                       "Vibrance", "Saturation", "LuminanceSmoothing", "ColorNoiseReduction", "Sharpness")
        unknown = [key for key in zero_fields if finite_number(baseline.get(key)) is None]
        nonzero = [key for key in zero_fields if finite_number(baseline.get(key)) not in (None, 0)]
        if unknown:
            warnings.append("unknown_lr_reference_controls:" + ",".join(unknown))
        if nonzero:
            warnings.append("nonneutral_lr_reference:" + ",".join(nonzero))
    if field:
        observed = finite_number(settings.get(field))
        expected = record["value"]
        if record.get("value_mode") == "delta_from_reference":
            origin = finite_number(baseline.get(field))
            expected = None if origin is None else origin + expected
        if observed is None or expected is None or abs(observed - expected) > 1e-6:
            warnings.append("lr_state_mismatch_or_unknown")
    elif record["feature"] != "neutral":
        warnings.append("lr_state_audit_not_implemented")
    ignored = {field, "HasSettings", "Version", "UUID", "Digest"}
    differences = [key for key in settings.keys() | baseline.keys()
                   if key not in ignored and settings.get(key) != baseline.get(key)]
    if differences:
        warnings.append("other_lr_settings_changed:" + ",".join(sorted(differences)))
    for key in ("DenoiseApplied", "EnhanceApplied", "AutoTone", "AutoWhiteVersion"):
        if settings.get(key, "false").lower() not in ("false", "0", ""):
            warnings.append("automatic_or_ai_processing:" + key)
    return warnings, []
