"""Does the trained network read its sigma map? Response of the correction to a scaled sigma input.

CUDA environment. Reuses the exported comparator observations (same noisy
tiles and sigma maps as the development controls) and the final v005
checkpoint. For each factor lambda the network receives lambda*sigma; its
correction is c = lambda*sigma*F(x, lambda*sigma). If the network relies on
the sigma channel, F should fall roughly as 1/lambda so that the correction
stays matched to the actual noise; if F barely moves, the correction is set by
what the network infers from the noisy input itself. Diagnosis only.
"""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path

from physical_natural_comparators import BORDER
from prepare_next_training import digest, read, write


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT/'build-patch_neural_restoration'
FACTORS = (.5, 1., 2., 4.)


def fit_alpha(correction, noise):
    """Least-squares alpha of noise ~ alpha*correction, and the gain over the noisy input with and without it."""
    import numpy as np
    d, y = correction.astype(np.float64).ravel(), noise.astype(np.float64).ravel()
    alpha = float(d@y/max(d@d, 1e-30))
    gain = 10*math.log10((y@y)/max(((y-d)@(y-d)), 1e-30))
    oracle = 10*math.log10((y@y)/max(((y-alpha*d)@(y-alpha*d)), 1e-30))
    return alpha, gain, oracle


def run(comparators, campaign, output):
    import numpy as np
    import sys
    import torch
    from dru_cfa import DruCfaCorrector
    from physical_natural_training import CANONICAL_TAGS
    exported = read(comparators/'export.json')
    if digest(comparators/'arrays.npz') != exported['arrays_sha256']:
        raise ValueError('Exported arrays changed')
    state_path = campaign/'seed71/resume.pt'
    progress = read(campaign/'seed71/progress.json')
    if digest(state_path) != progress['final_checkpoint_sha256']:
        raise ValueError('Final checkpoint changed')
    output.mkdir(exist_ok=False)
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    sys.path.insert(0, str(BUILD/'third-party-drunet-v001'))
    from models.network_unet import UNetRes
    arrays = np.load(comparators/'arrays.npz')
    clean, noisy, sigma, logged = arrays['clean'], arrays['noisy'], arrays['sigma'], arrays['network_seed71']
    core = UNetRes(in_nc=8, out_nc=4, nc=[32, 64, 128, 256], nb=4, act_mode='R',
                   downsample_mode='strideconv', upsample_mode='convtranspose')
    model = DruCfaCorrector(core).cuda()
    state = torch.load(state_path, map_location='cuda', weights_only=False)
    model.load_state_dict(state['model_state'])
    del state
    model.eval()
    tags = torch.tensor([CANONICAL_TAGS], device='cuda')
    inner = (Ellipsis, slice(BORDER, -BORDER), slice(BORDER, -BORDER))
    rows, reproduction = [], 0.
    with torch.inference_mode():
        for index in range(noisy.shape[0]):
            for iso_index, iso in enumerate(exported['isos']):
                x = torch.from_numpy(noisy[index, iso_index:iso_index+1]).cuda()
                s = torch.from_numpy(sigma[index, iso_index:iso_index+1]).cuda()
                noise = (noisy[index, iso_index]-clean[index])[inner]
                row = dict(target_index=index, source=0 if index < 9 else 1, iso=iso, factors={})
                reference = None
                for factor in FACTORS:
                    out = model.correct_observations(x, s*factor, tags)[0].cpu().numpy()
                    if factor == 1.:
                        reproduction = max(reproduction, float(np.abs(out-logged[index, iso_index]).max()))
                    correction = (noisy[index, iso_index]-out)[inner]
                    residual = correction/np.maximum(factor*sigma[index, iso_index][inner], 1e-12)
                    if factor == 1.:
                        reference = residual
                    alpha, gain, oracle = fit_alpha(correction, noise)
                    row['factors'][str(factor)] = dict(alpha=alpha, gain_db=gain, oracle_gain_db=oracle,
                                                       correction_rms=float(np.sqrt(np.square(correction).mean())))
                    row['factors'][str(factor)]['residual_rms'] = float(np.sqrt(np.square(residual).mean()))
                ref_rms = row['factors']['1.0']['residual_rms']
                for factor in FACTORS:
                    row['factors'][str(factor)]['residual_rms_over_unit'] = row['factors'][str(factor)]['residual_rms']/max(ref_rms, 1e-30)
                rows.append(row)
    if reproduction > 1e-5:
        raise ValueError(f'Network does not reproduce the exported outputs ({reproduction:.2e})')
    summary = []
    for source in (0, 1):
        for iso in exported['isos']:
            picked = [r for r in rows if r['source'] == source and r['iso'] == iso]
            summary.append(dict(source=source, iso=iso, factors={str(f): {key: float(np.median([r['factors'][str(f)][key] for r in picked]))
                for key in ('alpha', 'gain_db', 'oracle_gain_db', 'residual_rms_over_unit')} for f in FACTORS}))
    result = dict(schema_version=1, status='sigma_response_diagnosis_not_qualification',
        measured_at_utc=datetime.now(timezone.utc).isoformat(), comparators=str(comparators.resolve()),
        arrays_sha256=exported['arrays_sha256'], checkpoint_sha256=progress['final_checkpoint_sha256'],
        tool_sha256=digest(Path(__file__)), factors=list(FACTORS), reproduction_max_abs=reproduction,
        summary=summary, rows=rows,
        limits=['18 development tiles of two sources; diagnosis only',
                'A sigma factor is never a usable correction: the real sigma map is measured and correct',
                'residual_rms_over_unit = rms of F at lambda*sigma over rms of F at sigma; 1/lambda means F fully follows sigma'])
    write(output/'result.json', result)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    with (ROOT/'docs/validation/raw'/f'neural-{output.name}-{stamp}.json').open('x', encoding='utf-8') as stream:
        json.dump(result, stream, allow_nan=False, separators=(',', ':'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--comparators', type=Path, default=BUILD/'physical-natural-comparators-v003')
    parser.add_argument('--campaign', type=Path, default=BUILD/'physical-natural-training-v005')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(args.comparators.resolve(), args.campaign.resolve(), args.output.resolve())
