"""Protocol-group dispatch. Numerical observations, never automatic visual acceptance."""
import numpy as np

from . import metrics as m

CENTERS = dict(zip(("red", "orange", "yellow", "green", "cyan", "blue", "violet", "magenta"),
                   (0, 30, 60, 120, 180, 240, 270, 300)))


def couleur(record, base, changed, y0, y1):
    valid = (base.min(axis=-1) > 1e-5) & (changed.min(axis=-1) > 1e-5)
    valid &= (base.max(axis=-1) < .98) & (changed.max(axis=-1) < .98)
    r0, g0, b0 = (base[..., i] for i in range(3))
    r1, g1, b1 = (changed[..., i] for i in range(3))
    eps = 1e-12
    return {"axis_samples": int(valid.sum()),
            "delta_log_R_B": m.mean(np.log(np.maximum(r1, eps) / np.maximum(b1, eps)) -
                                    np.log(np.maximum(r0, eps) / np.maximum(b0, eps)), valid),
            "delta_log_magenta_green": m.mean(
                np.log(np.sqrt(np.maximum(r1 * b1, eps)) / np.maximum(g1, eps)) -
                np.log(np.sqrt(np.maximum(r0 * b0, eps)) / np.maximum(g0, eps)), valid),
            "chroma_groups": m.chroma_groups(base, changed),
            "achromatic_max_linear_channel_spread": float(np.ptp(changed, axis=-1).max()),
            "grayscale_test_applicable": record["feature"] == "grayscale" and record["value"] == 1}


def lumiere(record, base, changed, y0, y1):
    result = {"tonal_zones": m.zones(y0, y1)}
    if record["feature"] == "exposure":
        mask = (base.min(axis=-1) > .005) & (base.max(axis=-1) < .45) & (changed.max(axis=-1) < .98)
        ratios = y1[mask] / y0[mask]
        result["exposure"] = {"samples": int(mask.sum()), "expected_gain": 2 ** record["value"],
                              "median_observed_gain": float(np.median(ratios)) if ratios.size else None,
                              "gain_quantiles": np.quantile(ratios, [.05, .5, .95]).tolist() if ratios.size else None}
    return result


def melangeur(record, base, changed, y0, y1):
    band, component = record["feature"].split("-")[1:]
    h0, s0, _ = m.hsv(base)
    h1, s1, _ = m.hsv(changed)
    mask = (np.abs(m.circular_delta(h0, CENTERS[band])) <= 15) & (s0 >= .15) & (y0 > .01) & (y0 < .9)
    hue_mask = mask & (s1 >= .02)
    return {"band": band, "component": component, "center_degrees": CENTERS[band],
            "mask_source": "own-engine neutral (not internal rendering supports)",
            "core_samples": int(mask.sum()), "hue_samples": int(hue_mask.sum()),
            "core_hue_delta_degrees": m.circular_mean(m.circular_delta(h1, h0), hue_mask),
            "core_saturation_delta": m.mean(s1 - s0, mask),
            "core_Y_ratio": m.ratio(y1[mask].sum(), y0[mask].sum()),
            "outside_core_mean_Y_delta": m.mean(y1 - y0, ~mask),
            "low_chroma_samples": int((s0 < .02).sum()),
            "low_chroma_MAE_linear": m.mean(np.abs(changed - base).mean(axis=-1), s0 < .02)}


def grading(record, base, changed, y0, y1):
    result = {"tonal_zones": m.zones(y0, y1), "zones_are_diagnostic_quantiles": True}
    h0, s0, _ = m.hsv(base)
    h1, s1, _ = m.hsv(changed)
    result["hue_samples"] = int(((s0 >= .02) & (s1 >= .02)).sum())
    result["hue_delta_degrees"] = m.circular_mean(m.circular_delta(h1, h0), (s0 >= .02) & (s1 >= .02))
    result["mean_chroma_delta"] = m.mean(s1 - s0)
    return result


def courbes(record, base, changed, y0, y1):
    # Numeric bins fixed in the reference domain; no assertion of HDR support.
    bins = []
    for low in np.arange(0, 1, .1):
        mask = (y0 >= low) & (y0 < low + .1 if low < .9 else y0 <= 1)
        bins.append({"low": float(low), "high": float(low + .1), "samples": int(mask.sum()),
                     "mean_Y_delta": m.mean(y1 - y0, mask)})
    return {"reference_Y_bins": bins, "native_curve_points": (record.get("settings") or {}).get("toneCurves"),
            "HDR_not_measured": True}


def raw(record, base, changed, y0, y1):
    return {"tonal_zones": m.zones(y0, y1), "chroma_groups": m.chroma_groups(base, changed),
            "physical_color_accuracy_not_measured": True}


def optique(record, base, changed, y0, y1):
    return {"tonal_zones": m.zones(y0, y1),
            "geometric_and_fringe_validation": "requires annotated lines/edges; not inferred from MAE"}


GLOBAL = {"raw": raw, "lumiere": lumiere, "courbes": courbes, "couleur": couleur,
          "melangeur": melangeur, "grading": grading, "optique": optique}


def measure(group, record, reference, variant):
    result, base, changed, y0, y1 = m.common(reference, variant)
    if group in GLOBAL:
        result.update(GLOBAL[group](record, base, changed, y0, y1))
    if group == "effets":
        result["interpercentile_contrast_ratio"] = m.ratio(np.quantile(y1, .95) - np.quantile(y1, .05),
                                                         np.quantile(y0, .95) - np.quantile(y0, .05))
    return result


def measure_roi(group, record, reference, variant):
    result = measure(group, record, reference, variant)
    base, changed = m.detail(m.linearize(reference)), m.detail(m.linearize(variant))
    result["detail_reference"] = base
    result["detail_variant"] = changed
    result["detail_ratios"] = {key: m.ratio(changed[key], base[key]) for key in
                              ("sigma_Y_Haar", "sigma_chroma_Haar", "gradient_RMS",
                               "micro_residual_RMS", "local_residual_RMS")}
    result["proxies_not_reconstruction_error"] = True
    return result
