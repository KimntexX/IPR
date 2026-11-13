"""The beta-1 U-Net on real bursts of one camera: qualified noise law against the law estimated
from each frame, scored against the mean of the other frames of the burst.

Usage: python burst_estimated_against_qualified.py <campaign folder> <qualification.json>
       <burst inventory.json> <output.json>
The burst inventory lists the frames in shooting order: file path, ISO, exposure; consecutive
frames of one ISO and exposure form a burst, kept from three frames. The camera must not move.

For frame i of a burst of n: reference = mean of the n - 1 others. The squared error to the
reference, minus the reference's own noise (qualified law at the reference's signal, over
n - 1), estimates the error to the clean scene without bias. Gain = 10 log10 of the noisy
input's error over the output's, both estimated the same way. The correction is the same for
every law, so the differences between laws do not depend on it. Crops: three 512 x 512 per
plane (centre, top left, lower right). Needs torch with CUDA and rawpy; not a unit test.
"""
import json
import sys
import time
from pathlib import Path

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from denoise_estimated_against_qualified import (CROP, DIVISOR, blacks, canonical_law, canonical_planes,  # noqa: E402
                                                 denoise, estimate, law_at, read_raw)
from input_modes import load_trained_model  # noqa: E402
from physical_natural_training import CANONICAL_TAGS  # noqa: E402


def crops_of(shape):
    h, w = shape
    return [(h // 2 - CROP // 2, w // 2 - CROP // 2), (h // 8, w // 8), (h - h // 8 - CROP, w - w // 8 - CROP)]


def bursts(inventory):
    groups, current = [], None
    for frame in inventory:
        key = (frame['iso'], frame['exposure_s'])
        if current is not None and current['key'] == key:
            current['frames'].append(frame['path'])
        else:
            current = dict(key=key, frames=[frame['path']])
            groups.append(current)
    return [g for g in groups if len(g['frames']) >= 3]


def error(estimate_, reference, reference_variance):
    return float(np.mean((estimate_ - reference) ** 2) - np.mean(reference_variance))


def main(campaign, qualified_path, inventory_path, output):
    if Path(output).exists():
        raise SystemExit(f'{output} exists: a new version gets a new name')
    started = time.time()
    campaign = Path(campaign)
    qualified = json.load(open(qualified_path, encoding='utf-8'))
    inventory = json.load(open(inventory_path, encoding='utf-8'))
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    plan = json.load(open(campaign/'plan.json'))
    model, sha, step, _ = load_trained_model(campaign, plan, plan['seeds'][0], None)
    tags = torch.tensor([CANONICAL_TAGS], device='cuda')
    rows = []
    for burst in bursts(inventory):
        iso, exposure = burst['key']
        law = law_at(qualified, iso)
        if law is None:
            continue
        cal = canonical_law(law)
        frames, estimates = [], []
        for path in burst['frames']:
            full, top, left, white, height, width = read_raw(path)
            frames.append(canonical_planes(full, top, left, height, width, blacks(full, top, left, height)))
            estimates.append(estimate(full, height, top, left, white, width))
        stack = np.stack(frames)
        n = len(frames)
        for i in range(n):
            reference = (stack.sum(axis=0) - stack[i]) / (n - 1)
            est = canonical_law(estimates[i])
            laws = dict(qualified=cal, estimated=est, estimated_slope_x1_2=[(a * 1.2, b) for a, b in est])
            gains = {k: [] for k in laws}
            for y, x in crops_of(stack.shape[2:]):
                ref = reference[:, y:y + CROP, x:x + CROP]
                inp = stack[i][:, y:y + CROP, x:x + CROP]
                # The reference's own noise, per plane, from the qualified law at its signal.
                ref_var = np.stack([(a * np.maximum(ref[p] * DIVISOR, 0) + b) / DIVISOR ** 2 / (n - 1)
                                    for p, (a, b) in enumerate(cal)])
                noisy = error(inp, ref, ref_var)
                for name, chosen in laws.items():
                    gains[name].append(10 * np.log10(noisy / max(error(denoise(model, tags, inp, chosen), ref, ref_var), 1e-20)))
            row = dict(iso=iso, exposure_s=exposure, frames=n, frame=Path(burst['frames'][i]).name,
                       signal_dn=float(np.mean(stack[i]) * DIVISOR),
                       estimated_slope_over_qualified=[e[0] / c[0] for e, c in zip(est, cal)],
                       gain_db={k: float(np.median(v)) for k, v in gains.items()},
                       gain_db_by_crop={k: [float(g) for g in v] for k, v in gains.items()})
            rows.append(row)
        last = [r for r in rows if r['iso'] == iso and r['exposure_s'] == exposure]
        print(iso, exposure, n, 'a/a0 %.2f' % np.median([np.median(r['estimated_slope_over_qualified']) for r in last]),
              ' '.join('%s %.2f' % (k, np.median([r['gain_db'][k] for r in last])) for k in last[0]['gain_db']), flush=True)
    summary = {}
    for iso in sorted({r['iso'] for r in rows}):
        sel = [r for r in rows if r['iso'] == iso]
        d = [r['gain_db']['estimated'] - r['gain_db']['qualified'] for r in sel]
        d12 = [r['gain_db']['estimated_slope_x1_2'] - r['gain_db']['qualified'] for r in sel]
        summary[str(iso)] = dict(frames=len(sel), qualified_gain_median_db=float(np.median([r['gain_db']['qualified'] for r in sel])),
                                 estimated_minus_qualified_median_db=float(np.median(d)), worst_db=float(min(d)), best_db=float(max(d)),
                                 estimated_x1_2_minus_qualified_median_db=float(np.median(d12)), worst_x1_2_db=float(min(d12)),
                                 slope_ratio_median=float(np.median([np.median(r['estimated_slope_over_qualified']) for r in sel])))
    result = dict(schema_version=1, status='exploratory_burst_estimated_against_qualified_law', camera=qualified['model'],
                  method=__doc__.strip(), model_step=step, checkpoint_sha256=sha, campaign=campaign.name, divisor=DIVISOR,
                  qualified_law=Path(qualified_path).name, crop_per_plane=CROP, summary=summary, rows=rows,
                  seconds=time.time() - started)
    Path(output).write_text(json.dumps(result, indent=1), encoding='utf-8')
    print(json.dumps(summary, indent=1))


if __name__ == '__main__':
    if len(sys.argv) != 5:
        raise SystemExit(__doc__)
    main(*sys.argv[1:])
