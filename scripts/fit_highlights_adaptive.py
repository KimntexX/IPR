"""Can each image's own Highlights curve be predicted from its histogram?

On the Lightroom pairs neutral -> Highlights variant of tests/image/base, each
scene has its own curve: median EV change of Lightroom per 0.25 EV bin of the
neutral luminance. A model predicts that curve from the image statistics,
trained on the other scenes (leave-one-scene-out), and is scored with the
user criterion: displayed brightness (mean sRGB-encoded Y) of the 10 %
brightest neutral pixels, prediction / LR - 1, within +-3 %.

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
from analysis import inputs, render  # noqa: E402
from analysis.native import sha256, write_immutable  # noqa: E402
from analyse_local_tone import BASE, EDGES, FLOOR, MIN_COUNT, TOLERANCE, block_mean, encode, medians  # noqa: E402

FEATURES = ("mean", "Q1", "Q5", "Q10", "Q25", "Q50", "Q75", "Q90", "Q95", "Q99", "Q999", "above_0.5", "above_0.9")
QUANTILES = {"Q1": .01, "Q5": .05, "Q10": .10, "Q25": .25, "Q50": .5, "Q75": .75, "Q90": .9, "Q95": .95,
             "Q99": .99, "Q999": .999}
# Unclipped IPR master (iprRenderServer): headroom above the output white that
# the clipped LR neutral cannot show.
MASTER = ("master_Q99", "master_Q999", "master_max", "master_above_1", "master_above_2", "master_above_4")
LAMBDAS = (.01, .1, 1, 10, 100)
BINS = len(EDGES) - 1


def statistics(y):
    x = np.log2(np.maximum(y, FLOOR))
    result = {"mean": float(x.mean()), **{k: float(np.quantile(x, q)) for k, q in QUANTILES.items()}}
    result.update({"above_0.5": float((y > .5).mean()), "above_0.9": float((y > .9).mean())})
    return result


def fill(curve):
    """Nearest valid bin for empty ones (curves are compared on the bins a scene uses)."""
    valid = np.flatnonzero(np.isfinite(curve))
    if not valid.size:
        return np.zeros_like(curve)
    return curve[valid[np.abs(np.arange(curve.size)[:, None] - valid[None, :]).argmin(axis=1)]]


def ridge(X, Y, lam):
    """Ridge regression with an unpenalised intercept; returns (coef, mean_x, mean_y, scale)."""
    mx, sx, my = X.mean(0), X.std(0) + 1e-9, Y.mean(0)
    Z = (X - mx) / sx
    coef = np.linalg.solve(Z.T @ Z + lam * np.eye(Z.shape[1]), Z.T @ (Y - my))
    return lambda x: ((x - mx) / sx) @ coef + my


def choose_lambda(X, Y, score):
    """Inner leave-one-out choice of the ridge penalty on the training scenes."""
    best = None
    for lam in LAMBDAS:
        errors = []
        for i in range(len(X)):
            keep = np.arange(len(X)) != i
            errors.append(score(ridge(X[keep], Y[keep], lam)(X[i:i + 1])[0], i))
        total = float(np.mean(errors))
        if best is None or total < best[0]:
            best = (total, lam)
    return best[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args()
    records = [r for r in inputs.base_records(BASE, ["lumiere"]) if r["feature"] == "highlights" and r.get("reference")]
    scenes = sorted({r["scene"] for r in records})
    values = sorted({r["value"] for r in records})
    sources = {p.stem: p for p in BASE.iterdir() if p.is_file() and p.suffix.lower() in (".cr3", ".cr2")}
    master = {}
    with render.RenderServer() as server:
        for scene in scenes:
            m = server.statistics(sources[scene])
            q = m["log2_quantiles"]
            master[scene] = {"master_Q99": q["0.99"], "master_Q999": q["0.999"], "master_max": q["1"],
                             "master_above_1": m["above_1"], "master_above_2": m["above_2"],
                             "master_above_4": m["above_4"]}
            print("master", scene, {k: round(v, 4) for k, v in master[scene].items()}, flush=True)
    data, provenance = {}, {}
    for scene in scenes:
        neutral = next(r["reference"] for r in records if r["scene"] == scene)
        y0, meta = block_mean(neutral, True)
        x = np.log2(np.maximum(y0, FLOOR))
        index = np.clip(np.digitize(x, EDGES) - 1, 0, BINS - 1).astype(np.int16)
        valid, top = y0 > FLOOR, y0 >= np.quantile(y0, .9)
        scene_data = {"stats": {**statistics(y0), **master[scene]}, "top_y": y0[top], "top_index": index[top], "curves": {},
                      "reference": {}, "counts": np.bincount(index[valid], minlength=BINS)}
        provenance[scene] = {"neutral": str(Path(neutral).relative_to(BASE)), "neutral_sha256": meta["sha256"]}
        for r in (r for r in records if r["scene"] == scene):
            y1, _ = block_mean(r["file"], True)
            e = np.log2(np.maximum(y1, FLOOR / 4) / np.maximum(y0, FLOOR)).astype(np.float32)
            scene_data["curves"][r["value"]] = medians(index[valid], e[valid], BINS)
            scene_data["reference"][r["value"]] = float(encode(y1[top]).mean())
        data[scene] = scene_data
        print("loaded", scene, flush=True)

    feature_sets = {"lr": FEATURES, "lr_master": FEATURES + MASTER}
    criterion = lambda s, curve, value: float(
        encode(np.minimum(data[s]["top_y"] * 2.0 ** curve[data[s]["top_index"]], 1)).mean()
        / data[s]["reference"][value] - 1)
    results, correlations = {}, {}
    for (set_name, names), value in ((fs, v) for fs in feature_sets.items() for v in values):
        X = np.array([[data[s]["stats"][f] for f in names] for s in scenes])
        C = np.array([fill(data[s]["curves"][value]) for s in scenes])
        # Bins used by each scene weigh the curve error (pixel counts).
        W = np.array([data[s]["counts"] for s in scenes], dtype=float)
        models = {"shared": [], "shared_gain_Q5_Q95": [], "pca1": [], "pca2": [], "pca3": [], "knn3": [], "knn5": []}
        for i, s in enumerate(scenes):
            keep = np.arange(len(scenes)) != i
            Ct, Xt, Wt = C[keep], X[keep], W[keep]
            weight = Wt.sum(0)
            shared = np.where(weight > 0, (Wt * Ct).sum(0) / np.maximum(weight, 1), Ct.mean(0))
            models["shared"].append(criterion(s, shared, value))
            # Production form: gain on the shared curve, regressed on Q5 and Q95.
            gains = np.array([np.sum(w * c * shared) / max(np.sum(w * shared * shared), 1e-12) for c, w in zip(Ct, Wt)])
            q = [names.index("Q5"), names.index("Q95")]
            A = np.c_[np.ones(len(Xt)), Xt[:, q]]
            coef, *_ = np.linalg.lstsq(A, gains, rcond=None)
            models["shared_gain_Q5_Q95"].append(criterion(s, shared * float(np.r_[1, X[i, q]] @ coef), value))
            # Principal shapes around the mean, scores ridge-regressed on all statistics.
            mean = Ct.mean(0)
            _, _, Vt = np.linalg.svd(Ct - mean, full_matrices=False)
            for k in (1, 2, 3):
                basis = Vt[:k]
                scores = (Ct - mean) @ basis.T
                score = lambda pred, j: float(np.average((mean + pred @ basis - Ct[j]) ** 2, weights=Wt[j] + 1e-9))
                lam = choose_lambda(Xt, scores, score)
                predicted = mean + ridge(Xt, scores, lam)(X[i:i + 1])[0] @ basis
                models[f"pca{k}"].append(criterion(s, predicted, value))
            # Nearest scenes in standardised statistics.
            Z = (Xt - Xt.mean(0)) / (Xt.std(0) + 1e-9)
            z = (X[i] - Xt.mean(0)) / (Xt.std(0) + 1e-9)
            order = np.argsort(np.linalg.norm(Z - z, axis=1))
            for k in (3, 5):
                models[f"knn{k}"].append(criterion(s, Ct[order[:k]].mean(0), value))
        own = [criterion(s, C[i], value) for i, s in enumerate(scenes)]
        # Ideal gain of each scene on the all-scene shared curve, against each statistic.
        weight = W.sum(0)
        shared = np.where(weight > 0, (W * C).sum(0) / np.maximum(weight, 1), C.mean(0))
        gains = np.array([np.sum(w * c * shared) / max(np.sum(w * shared * shared), 1e-12) for c, w in zip(C, W)])
        rank = lambda v: np.argsort(np.argsort(v))
        correlations[str(value)] = {f: float(np.corrcoef(rank(gains), rank(X[:, j]))[0, 1]) for j, f in enumerate(names)}
        results.setdefault(set_name, {})[str(value)] = {name: {"scenes": dict(zip(scenes, errors)),
                                      "within": int(sum(abs(e) <= TOLERANCE for e in errors)),
                                      "mean_abs": float(np.mean(np.abs(errors)))}
                               for name, errors in [("own_curve_ceiling", own), *models.items()]}
        print(f"[{set_name}] HL {value:+.0f}: " + ", ".join(
            f"{n} {r['within']}/{len(scenes)} ({r['mean_abs']:.1%})" for n, r in results[set_name][str(value)].items()),
            flush=True)
    for value in ("-100.0", "-50.0", "100.0"):
        best = sorted(correlations[value].items(), key=lambda kv: -abs(kv[1]))[:5]
        print(f"HL {value} gain vs statistic (Spearman): " + ", ".join(f"{k} {v:+.2f}" for k, v in best), flush=True)

    now = datetime.now(timezone.utc)
    result = {"campaign_id": f"fit-hl-adaptatif-v002-{now.strftime('%Y%m%dT%H%M%S%fZ')}", "schema_version": 1,
              "measured_at_utc": now.isoformat(), "group": "lumiere", "feature": "highlights",
              "question": "Can each image's own Highlights curve be predicted from its histogram statistics?",
              "method": {"curve": "median log2(Y1/Y0) per 0.25 EV bin of the neutral luminance, block mean 8x8",
                         "min_count_per_bin": MIN_COUNT,
                         "feature_sets": {k: list(v) for k, v in feature_sets.items()},
                         "master_statistics": "iprRenderServer statistics of the default master (linear Rec.2020, "
                                              "not clipped): log2 luminance quantiles, maximum, shares above 1, 2, 4",
                         "validation": "leave one scene out; ridge penalty chosen by inner leave-one-out",
                         "criterion": "mean sRGB-encoded Y of the 10 % brightest neutral pixels, prediction / LR - 1",
                         "tolerance": TOLERANCE, "lambdas": list(LAMBDAS)},
              "limits": ["Simulated on the LR neutral: isolates the operator, not IPR's development",
                         "Luminance only; colour handling not modelled",
                         "own_curve_ceiling is the scene's own curve: an upper bound"],
              "software": {"python": sys.version, "numpy": np.__version__, "platform": platform.platform(),
                           "script_sha256": sha256(Path(__file__))},
              "scenes": scenes, "values": values, "provenance": provenance,
              "supersedes_campaign_id": "fit-hl-adaptatif-v001-20261020T122057922451Z",
              "supersedes_reason": "adds the unclipped master headroom statistics and gain correlations",
              "statistics": {s: data[s]["stats"] for s in scenes}, "results": results,
              "gain_rank_correlations": correlations}
    output = args.output_dir / (result["campaign_id"] + ".json")
    write_immutable(output, result)
    print(output)


if __name__ == "__main__":
    main()
