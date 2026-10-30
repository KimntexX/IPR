"""Negative control: a perfect denoiser can raise means inside noise-selected dark masks."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import numpy as np


def selected_mean_change(noisy, truth, mask):
    if noisy.shape != truth.shape or mask.shape != truth.shape or not mask.any():
        raise ValueError('Matching arrays and nonempty mask required')
    before = noisy[mask]
    after = truth[mask]  # Oracle: exact known clean signal, no image algorithm.
    delta = after-before
    return dict(pixels=int(mask.sum()), mean_before=float(before.mean()),
                oracle_mean_after=float(after.mean()), oracle_mean_change=float(delta.mean()),
                change_standard_error=float(delta.std(ddof=1)/np.sqrt(delta.size)) if delta.size > 1 else 0.)


def main():
    root = Path(__file__).resolve().parents[2]
    rng = np.random.default_rng(20261030)
    half = rng.normal(0., .01, size=(2, 131072))
    noise = np.stack((half[0], -half[0]), axis=1).ravel()
    independent = np.stack((half[1], -half[1]), axis=1).ravel()
    truth = np.full(noise.shape, .01)
    noisy = truth+noise
    rows = []
    for rho in (0., .5, 1.):
        reference = truth+rho*noise+np.sqrt(1-rho*rho)*independent
        row = selected_mean_change(noisy, truth, reference < .01)
        row.update(reference_noise_correlation=rho, mask_definition='reference < truth mean')
        rows.append(row)
    control = selected_mean_change(noisy, truth, truth < .015)
    assert abs(control['oracle_mean_change']) < 1e-15
    assert abs(rows[0]['oracle_mean_change']) < 5*rows[0]['change_standard_error']
    assert rows[2]['oracle_mean_change'] > rows[1]['oracle_mean_change'] > rows[0]['oracle_mean_change']
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    source = Path(__file__)
    proof = dict(schema_version=1, created_utc=stamp, study='dark_mask_selection_bias_negative_control',
                 revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
                 source=dict(sha256=hashlib.sha256(source.read_bytes()).hexdigest(), text=source.read_text(encoding='utf-8')),
                 samples=int(noise.size), seed=20261030, truth_mean=.01, sigma=.01,
                 correlated_masks=rows, clean_signal_mask=control,
                 verdict='mechanism_demonstrated_not_real_scene_attribution',
                 limits=['Scalar independent Gaussian model, not a Canon CFA or Lightroom simulation.',
                         'Oracle is perfect denoising to known truth, not an implementable filter.',
                         'No DCP, clipping, quantization or mean balancing in this experiment.',
                         'Does not quantify how much of the real mask shift is selection bias.',
                         'Frozen masks remain useful for same-pixel comparison but not proof of photometric bias.'])
    output = root/'docs/validation/raw'/f'noise-dark-selection-v001-{stamp}.json'
    with output.open('x', encoding='utf-8') as handle:
        json.dump(proof, handle, ensure_ascii=False, indent=2, allow_nan=False)
    print(output)
    print(json.dumps(rows))
    print(json.dumps(control))


if __name__ == '__main__':
    main()
