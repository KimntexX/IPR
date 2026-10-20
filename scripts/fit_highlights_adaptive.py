"""Can each image's own Highlights curve be predicted from its histogram?

On the Lightroom pairs neutral -> Highlights variant of tests/image/base, each
scene has its own curve: median EV change of Lightroom per 0.25 EV bin of the
neutral luminance. A model predicts that curve from the image statistics,
trained on the other scenes (leave-one-scene-out), and is scored with the
perceptual criterion of the protocol: |delta L*| of the mean CIE lightness of
the 10 % brightest neutral pixels, prediction against LR, at most 2 (the user's
+-3 % on bright scenes) and never above 4. Scenes are weighted in fitting by
their share of real highlights (L* >= 70): HL matters little on dark scenes.

Models: shared curve; shared curve with a gain regressed on two quantiles
(the production form); principal curve shapes with ridge-regressed scores;
mean curve of the k most similar scenes. Read-only; one immutable JSON.
Usage: python scripts/fit_highlights_adaptive.py [--output-dir docs/validation/raw]
"""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import platform
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
sys.path.insert(0, str(ROOT / "scripts"))
from analysis import inputs  # noqa: E402
from analysis.native import sha256, write_immutable  # noqa: E402
from analyse_local_tone import BASE, EDGES, FLOOR, MIN_COUNT, block_mean, medians  # noqa: E402

FEATURES = ("mean", "Q1", "Q5", "Q10", "Q25", "Q50", "Q75", "Q90", "Q95", "Q99", "Q999", "above_0.5", "above_0.9")
QUANTILES = {"Q1": .01, "Q5": .05, "Q10": .10, "Q25": .25, "Q50": .5, "Q75": .75, "Q90": .9, "Q95": .95,
             "Q99": .99, "Q999": .999}
DELTA_L, DELTA_L_MAX = 2.0, 4.0
PASS_SHARE, PASS_OUTLIERS = .8, 2          # success: 80 % of scenes within, at most 2 beyond the maximum
HIGHLIGHT_Y, HIGHLIGHT_SHARE = .41, .05    # L* >= 70; full weight from 5 % of the image
LAMBDAS = (.01, .1, 1, 10, 100)
BINS = len(EDGES) - 1


def statistics(y):
    x = np.log2(np.maximum(y, FLOOR))
    result = {"mean": float(x.mean()), **{k: float(np.quantile(x, q)) for k, q in QUANTILES.items()}}
    result.update({"above_0.5": float((y > .5).mean()), "above_0.9": float((y > .9).mean())})
    return result


def lightness(y):
    """CIE L* of a relative linear luminance (clipped at the output white)."""
    y = np.clip(y, 0, 1)
    return np.where(y > 216 / 24389, 116 * np.cbrt(y) - 16, y * 24389 / 27)


def fill(curve):
    """Nearest valid bin for empty ones (curves are compared on the bins a scene uses)."""
    valid = np.flatnonzero(np.isfinite(curve))
    if not valid.size:
        return np.zeros_like(curve)
    return curve[valid[np.abs(np.arange(curve.size)[:, None] - valid[None, :]).argmin(axis=1)]]


def ridge(X, Y, lam, w):
    """Weighted ridge regression with an unpenalised intercept; returns the predictor."""
    w = w / w.sum()
    mx, my = w @ X, w @ Y
    sx = np.sqrt(w @ (X - mx) ** 2) + 1e-9
    Z = (X - mx) / sx
    coef = np.linalg.solve(Z.T @ (w[:, None] * Z) + lam / len(X) * np.eye(Z.shape[1]), Z.T @ (w[:, None] * (Y - my)))
    return lambda x: ((x - mx) / sx) @ coef + my


def choose_lambda(X, Y, w, score):
    """Inner leave-one-out choice of the ridge penalty on the training scenes."""
    best = None
    for lam in LAMBDAS:
        errors = []
        for i in range(len(X)):
            keep = np.arange(len(X)) != i
            errors.append(score(ridge(X[keep], Y[keep], lam, w[keep])(X[i:i + 1])[0], i))
        total = float(np.average(errors, weights=w))
        if best is None or total < best[0]:
            best = (total, lam)
    return best[1]


def summary(scenes, errors):
    errors = np.abs(np.asarray(errors))
    within, outliers = int((errors <= DELTA_L).sum()), int((errors > DELTA_L_MAX).sum())
    return {"delta_L": dict(zip(scenes, map(float, errors))), "within": within, "outliers": outliers,
            "mean_abs_delta_L": float(errors.mean()), "max_delta_L": float(errors.max()),
            "passes": bool(within >= PASS_SHARE * len(scenes) and outliers <= PASS_OUTLIERS)}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args()
    records = [r for r in inputs.base_records(BASE, ["lumiere"]) if r["feature"] == "highlights" and r.get("reference")]
    scenes = sorted({r["scene"] for r in records})
    values = sorted({r["value"] for r in records})
    data, provenance = {}, {}
    for scene in scenes:
        neutral = next(r["reference"] for r in records if r["scene"] == scene)
        y0, meta = block_mean(neutral, True)
        x = np.log2(np.maximum(y0, FLOOR))
        index = np.clip(np.digitize(x, EDGES) - 1, 0, BINS - 1).astype(np.int16)
        valid, top = y0 > FLOOR, y0 >= np.quantile(y0, .9)
        scene_data = {"stats": statistics(y0), "top_y": y0[top], "top_index": index[top], "curves": {},
                      "reference": {}, "counts": np.bincount(index[valid], minlength=BINS),
                      "weight": float(np.clip((y0 >= HIGHLIGHT_Y).mean() / HIGHLIGHT_SHARE, .1, 1))}
        provenance[scene] = {"neutral": str(Path(neutral).relative_to(BASE)), "neutral_sha256": meta["sha256"]}
        for r in (r for r in records if r["scene"] == scene):
            y1, _ = block_mean(r["file"], True)
            e = np.log2(np.maximum(y1, FLOOR / 4) / np.maximum(y0, FLOOR)).astype(np.float32)
            scene_data["curves"][r["value"]] = medians(index[valid], e[valid], BINS)
            scene_data["reference"][r["value"]] = float(lightness(y1[top]).mean())
        data[scene] = scene_data
        print("loaded", scene, flush=True)

    # Signed delta L* of the mean lightness of the brightest tenth, prediction - LR.
    def criterion(s, curve, value):
        d = data[s]
        return float(lightness(d["top_y"] * 2.0 ** curve[d["top_index"]]).mean() - d["reference"][value])

    X = np.array([[data[s]["stats"][f] for f in FEATURES] for s in scenes])
    sw = np.array([data[s]["weight"] for s in scenes])
    rank = lambda v: np.argsort(np.argsort(v))
    results, correlations = {}, {}
    for value in values:
        C = np.array([fill(data[s]["curves"][value]) for s in scenes])
        # Bins used by each scene weigh the curve error (pixel counts x scene weight).
        W = np.array([data[s]["counts"] for s in scenes], dtype=float) * sw[:, None]
        models = {"shared": [], "shared_gain_Q5_Q95": [], "pca1": [], "pca2": [], "pca3": [], "knn3": [], "knn5": []}
        for i, s in enumerate(scenes):
            keep = np.arange(len(scenes)) != i
            Ct, Xt, Wt, wt = C[keep], X[keep], W[keep], sw[keep]
            weight = Wt.sum(0)
            shared = np.where(weight > 0, (Wt * Ct).sum(0) / np.maximum(weight, 1e-9), Ct.mean(0))
            models["shared"].append(criterion(s, shared, value))
            # Production form: gain on the shared curve, regressed on Q5 and Q95.
            gains = np.array([np.sum(w * c * shared) / max(np.sum(w * shared * shared), 1e-12) for c, w in zip(Ct, Wt)])
            q = [FEATURES.index("Q5"), FEATURES.index("Q95")]
            A = np.c_[np.ones(len(Xt)), Xt[:, q]] * np.sqrt(wt)[:, None]
            coef, *_ = np.linalg.lstsq(A, gains * np.sqrt(wt), rcond=None)
            models["shared_gain_Q5_Q95"].append(criterion(s, shared * float(np.r_[1, X[i, q]] @ coef), value))
            # Principal shapes around the weighted mean, scores ridge-regressed on all statistics.
            mean = wt @ Ct / wt.sum()
            _, _, Vt = np.linalg.svd(np.sqrt(wt)[:, None] * (Ct - mean), full_matrices=False)
            for k in (1, 2, 3):
                basis = Vt[:k]
                scores = (Ct - mean) @ basis.T
                score = lambda pred, j: float(np.average((mean + pred @ basis - Ct[j]) ** 2, weights=Wt[j] + 1e-9))
                lam = choose_lambda(Xt, scores, wt, score)
                predicted = mean + ridge(Xt, scores, lam, wt)(X[i:i + 1])[0] @ basis
                models[f"pca{k}"].append(criterion(s, predicted, value))
            # Nearest scenes in standardised statistics.
            Z = (Xt - Xt.mean(0)) / (Xt.std(0) + 1e-9)
            z = (X[i] - Xt.mean(0)) / (Xt.std(0) + 1e-9)
            order = np.argsort(np.linalg.norm(Z - z, axis=1))
            for k in (3, 5):
                models[f"knn{k}"].append(criterion(s, Ct[order[:k]].mean(0), value))
        own = [criterion(s, C[i], value) for i, s in enumerate(scenes)]
        weight = W.sum(0)
        shared = np.where(weight > 0, (W * C).sum(0) / np.maximum(weight, 1e-9), C.mean(0))
        gains = np.array([np.sum(w * c * shared) / max(np.sum(w * shared * shared), 1e-12) for c, w in zip(C, W)])
        correlations[str(value)] = {f: float(np.corrcoef(rank(gains), rank(X[:, j]))[0, 1]) for j, f in enumerate(FEATURES)}
        results[str(value)] = {name: summary(scenes, errors) for name, errors in [("own_curve_ceiling", own), *models.items()]}
        print(f"HL {value:+.0f}: " + ", ".join(
            f"{n} {r['within']}/{len(scenes)} >4:{r['outliers']} (mean {r['mean_abs_delta_L']:.2f})"
            + (" PASS" if r["passes"] else "") for n, r in results[str(value)].items()), flush=True)
    for name in results[str(values[0])]:
        print(f"{name}: passes at " + ", ".join(f"{v:+.0f}" for v in values if results[str(v)][name]["passes"]), flush=True)

    now = datetime.now(timezone.utc)
    result = {"campaign_id": f"fit-hl-adaptatif-v003-{now.strftime('%Y%m%dT%H%M%S%fZ')}", "schema_version": 1,
              "measured_at_utc": now.isoformat(), "group": "lumiere", "feature": "highlights",
              "supersedes_campaign_id": "fit-hl-adaptatif-v002-20261020T124115179808Z",
              "supersedes_reason": "perceptual criterion (delta L* <= 2, user decision of 20 October 2026) and "
                                   "scenes weighted by their highlights; master headroom dropped (never above 1)",
              "question": "Can each image's own Highlights curve be predicted from its histogram statistics?",
              "method": {"curve": "median log2(Y1/Y0) per 0.25 EV bin of the neutral luminance, block mean 8x8",
                         "min_count_per_bin": MIN_COUNT, "features": list(FEATURES),
                         "scene_weight": f"clip(share of neutral pixels with Y >= {HIGHLIGHT_Y} / {HIGHLIGHT_SHARE}, 0.1, 1)",
                         "validation": "leave one scene out; ridge penalty chosen by inner leave-one-out",
                         "criterion": "|delta L*| of the mean CIE L* of the 10 % brightest neutral pixels, prediction - LR",
                         "pass": {"delta_L": DELTA_L, "max_delta_L": DELTA_L_MAX, "share_within": PASS_SHARE,
                                  "outliers_allowed": PASS_OUTLIERS},
                         "lambdas": list(LAMBDAS)},
              "limits": ["Simulated on the LR neutral: isolates the operator, not IPR's development",
                         "Luminance only; colour handling not modelled",
                         "own_curve_ceiling is the scene's own curve: an upper bound"],
              "software": {"python": sys.version, "numpy": np.__version__, "platform": platform.platform(),
                           "script_sha256": sha256(Path(__file__))},
              "scenes": scenes, "values": values, "provenance": provenance,
              "statistics": {s: data[s]["stats"] for s in scenes},
              "scene_weights": {s: data[s]["weight"] for s in scenes}, "results": results,
              "gain_rank_correlations": correlations}
    output = args.output_dir / (result["campaign_id"] + ".json")
    write_immutable(output, result)
    print(output)


if __name__ == "__main__":
    main()
