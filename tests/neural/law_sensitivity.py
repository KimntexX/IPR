"""How much the shipped U-Net loses when its noise law is wrong, on the real-noise benches.

Runs the final beta-1 model on the prepared crops of the three real-noise
evaluations, with the calibrated law, with the law scaled, and with the law
estimated from each RAW alone (est_law.py), and scores like real_noise_evaluation.
"""
import json, sys, time
from pathlib import Path

import numpy as np
import torch

NEURAL = Path(__file__).resolve().parent
sys.path.insert(0, str(NEURAL))
from real_noise_evaluation import BUILD, DIVISOR, fit_mapping, read, scene_folder, score_method  # noqa: E402
from input_modes import load_trained_model  # noqa: E402
from physical_natural_training import CANONICAL_TAGS, sigma_map  # noqa: E402
from estimate_single_raw_law import estimate_law  # noqa: E402

CAMPAIGN = BUILD/'final-beta1-training-v002'
SETS = ('final-real-noise-evaluation', 'final-color-sheet-evaluation', 'final-climbing-gym-evaluation')
SCALED = [('a', f) for f in (0.8, 0.9, 1.1, 1.2)] + [('b', f) for f in (0.8, 0.9, 1.1, 1.2)] + [('ab', f) for f in (0.8, 1.2)]
MARGINS = (1.1, 1.15, 1.2)   # safety factors on the estimated slope


def main(output):
    output.mkdir(exist_ok=False)
    plan = read(CAMPAIGN/'plan.json')
    model, sha, step, _ = load_trained_model(CAMPAIGN, plan, plan['seeds'][0], None)
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    tags = torch.tensor([CANONICAL_TAGS], device='cuda')
    estimated_laws, rows = {}, []
    started = time.time()
    for name in SETS:
        folder = BUILD/name
        crops = read(folder/'crops.json')
        arrays = np.load(folder/'crops.npz')
        for entry in crops['entries']:
            key = entry['key']
            reference, valid, noisy = arrays[key+'-reference'], arrays[key+'-valid'], arrays[key+'-noisy']
            calibrated = [(r['shot_slope_dn'], r['read_variance_dn2']) for r in entry['noise_law']]
            files = [scene_folder(entry['scene'])/f for f in entry['files']]
            conditions = {'calibrated': [calibrated]*len(noisy)}
            for which, f in SCALED:
                conditions[f'{which}x{f}'] = [[(a*(f if 'a' in which else 1), b*(f if 'b' in which else 1)) for a, b in calibrated]]*len(noisy)
            per_frame = []
            for path in files:
                if str(path) not in estimated_laws:
                    estimated_laws[str(path)] = estimate_law(path)
                per_frame.append(estimated_laws[str(path)])
            conditions['estimated'] = per_frame
            for margin in MARGINS:
                conditions[f'estimated_a_x{margin}'] = [[(a*margin, b) for a, b in law] for law in per_frame]
            result = dict(set=name, key=key, iso=entry['iso'], trained_iso=entry['trained_iso'], calibrated=calibrated,
                          estimated=per_frame, gains={})
            for cname, laws in conditions.items():
                gains = []
                for index, frame in enumerate(noisy):
                    law = laws[index]
                    slope = torch.tensor([a for a, _ in law], device='cuda')[None, :, None, None]
                    readv = torch.tensor([b for _, b in law], device='cuda')[None, :, None, None]
                    with torch.inference_mode():
                        x = torch.from_numpy(frame).cuda()[None]
                        sigma = sigma_map(x, slope, readv, DIVISOR)
                        out = model.correct(x, sigma, tags, slope/DIVISOR, readv/DIVISOR**2)['corrected'][0].cpu().numpy()
                    mapping = fit_mapping(frame, reference, valid)
                    gains.append(score_method(out, frame, reference, valid, mapping)['psnr_gain_db'])
                result['gains'][cname] = float(np.median(gains))
            rows.append(result)
            base = result['gains']['calibrated']
            print(name, key, 'calibrated %.2f' % base, ' '.join('%s %+.3f' % (c, g-base) for c, g in result['gains'].items() if c != 'calibrated'), flush=True)
    summary = {}
    for cname in rows[0]['gains']:
        deltas = [r['gains'][cname]-r['gains']['calibrated'] for r in rows]
        summary[cname] = dict(median_delta_db=float(np.median(deltas)), min_delta_db=float(np.min(deltas)), max_delta_db=float(np.max(deltas)))
    est_ratio = {'a': [], 'b': []}
    for r in rows:
        for law in r['estimated']:
            for (a, b), (a0, b0) in zip(law, r['calibrated']):
                est_ratio['a'].append(a/a0); est_ratio['b'].append(b/b0)
    json.dump(dict(schema_version=1, status='law_sensitivity_exploration', model_step=step, checkpoint_sha256=sha,
                   campaign=str(CAMPAIGN), sets=SETS, summary=summary,
                   estimated_over_calibrated={k: dict(median=float(np.median(v)), p10=float(np.quantile(v, .1)),
                                                      p90=float(np.quantile(v, .9))) for k, v in est_ratio.items()},
                   seconds=time.time()-started, rows=rows), open(output/'result.json', 'w'), indent=1)
    print(json.dumps(summary, indent=1))


if __name__ == '__main__':
    main(Path(sys.argv[1]))
