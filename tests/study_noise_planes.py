"""Research only: overlapping noise maps, residual diagnostics, and RAW CFA audit."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import subprocess

import numpy as np
import scipy
from scipy.ndimage import convolve, gaussian_filter

from study_noise_level import haar_all, mad, prototypes, scenes


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def noise_map(plane, tile=32, stride=16):
    """Agreement is a heuristic, never a calibrated probability of correctness."""
    p = np.asarray(plane, dtype=float)
    if p.ndim != 2 or min(p.shape) < tile or not np.isfinite(p).all():
        raise ValueError('Expected a finite 2D plane at least one tile wide')
    if tile < 16 or stride < 1 or stride > tile:
        raise ValueError('Invalid tile/stride')
    numerator = np.zeros_like(p)
    denominator = np.zeros_like(p)
    confidence_sum = np.zeros_like(p)
    rows = []
    # Positive raised-cosine weights avoid uncovered edges; do not resize pixels.
    axis = .1 + .9 * np.sin(np.pi * (np.arange(tile) + .5) / tile) ** 2
    weight = np.outer(axis, axis)
    starts = lambda n: sorted(set(range(0, n - tile + 1, stride)) | {n - tile})
    for y in starts(p.shape[0]):
        for x in starts(p.shape[1]):
            part = p[y:y + tile, x:x + tile]
            estimates, support = prototypes(part)
            # PCA tail is recorded, not averaged: its small-tile bias is uncalibrated.
            candidates = np.array([mad(haar_all(part)), estimates['laplacian_mad'], estimates['weak_dct']])
            sigma = float(np.median(candidates))
            spread = float((candidates.max() - candidates.min()) / max(sigma, 1e-12))
            agree = float(spread <= .5 and sigma > 1e-10)
            sl = np.s_[y:y + tile, x:x + tile]
            numerator[sl] += weight * sigma
            denominator[sl] += weight
            confidence_sum[sl] += weight * agree
            rows.append(dict(y=y, x=x, mean=float(part.mean()), sigma=sigma,
                             relative_spread=spread, agreement=bool(agree),
                             estimates=estimates, support=support))
    return numerator / denominator, confidence_sum / denominator, rows


def residual_diagnostic(plane, sigma, confidence):
    # Center explicitly excluded; white independent equal-variance reference only.
    kernel = np.ones((3, 3)) / 8
    kernel[1, 1] = 0
    predicted = convolve(plane, kernel, mode='mirror')
    z = np.abs(plane - predicted) / np.maximum(sigma * np.sqrt(9 / 8), 1e-10)
    valid = np.zeros(plane.shape, bool)
    valid[2:-2, 2:-2] = True
    eligible = valid & (confidence >= .75) & (sigma > 1e-10)
    return (z > 3) & eligible, eligible, z


def synthetic():
    selected = ['flat_060', 'linear_ramp', 'slanted_edge', 'thin_oblique_grid',
                'periodic_texture', 'stochastic_clean_texture']
    rows = []
    for seed in (1601, 1613):
        rng = np.random.default_rng(seed)
        for name in selected:
            base = scenes()[name]
            for channel, amplitude, clean in [('Y', 1., base), ('C1', .7, .4 * (base - .4)),
                                               ('C2', 1.3, -.3 * (base - .4))]:
                for kind in ('clean', 'white', 'correlated', 'signal_dependent', 'mixed_impulse'):
                    variance = np.zeros_like(clean) if kind == 'clean' else np.full_like(clean, (.025 * amplitude) ** 2)
                    if kind == 'signal_dependent':
                        variance = amplitude ** 2 * (np.maximum(base, 0) / 2500 + .01 ** 2)
                    noise = rng.normal(size=clean.shape) * np.sqrt(variance)
                    if kind == 'correlated':
                        noise = gaussian_filter(noise, .65, mode='mirror')
                        noise *= .025 * amplitude / np.std(noise)
                    observed = clean + noise
                    impulse = rng.random(clean.shape) < .03 if kind == 'mixed_impulse' else np.zeros(clean.shape, bool)
                    observed = np.where(impulse, rng.choice([-.1, 1.1], clean.shape), observed)
                    sigma, confidence, tiles = noise_map(observed)
                    coarse, _, coarse_tiles = noise_map(observed, tile=64, stride=32)
                    # Same fusion on the full plane isolates localization from method changes.
                    global_consensus, _, _ = noise_map(observed, tile=96, stride=96)
                    flagged, eligible, _ = residual_diagnostic(observed, sigma, confidence)
                    interior = np.zeros(clean.shape, bool)
                    interior[2:-2, 2:-2] = True
                    normal = interior & ~impulse
                    target = np.sqrt(variance)
                    global_sigma = mad(haar_all(observed))
                    rows.append(dict(seed=seed, scene=name, channel=channel, noise=kind,
                                     target_sigma_rms=float(np.sqrt(variance.mean())),
                                     local_sigma_mean=float(sigma.mean()), global_haar_sigma=global_sigma,
                                     local_sigma_mae=float(np.abs(sigma - target).mean()),
                                     global_sigma_mae=float(np.abs(global_sigma - target).mean()),
                                     global_consensus_sigma=float(global_consensus[0, 0]),
                                     global_consensus_mae=float(np.abs(global_consensus - target).mean()),
                                     coarse_sigma_mae=float(np.abs(coarse - target).mean()),
                                     eligible_pixels=int(eligible.sum()), interior_pixels=int(interior.sum()),
                                     flagged_pixels=int(flagged.sum()),
                                     impulse_pixels=int((impulse & interior).sum()),
                                     detected_impulses=int((flagged & impulse).sum()),
                                     nonimpulse_pixels=int(normal.sum()),
                                     flagged_nonimpulse_pixels=int((flagged & normal).sum()),
                                     tiles=tiles, coarse_tiles=coarse_tiles))
    return rows


def corpus_audit(root):
    """Read-only RAW crops. No color rendering or physical calibration claimed."""
    import rawpy
    inventory = [dict(path=str(p.relative_to(root)), bytes=p.stat().st_size)
                 for p in sorted(root.rglob('*')) if p.is_file() and p.suffix.lower() == '.cr3']
    results, history = [], []
    for old in sorted((root / 'protocole/resultats_bruit').glob('*.json')):
        data = json.loads(old.read_text(encoding='utf-8-sig'))
        history.append(dict(path=str(old.relative_to(root)), sha256=digest(old),
                            label=data.get('label'), black_source=data.get('source_niveau_noir'),
                            planes=data.get('plans'), status='historical_not_used_for_fitting'))
    for directory in sorted((root / 'protocole').glob('iso-*')):
        files = sorted(directory.glob('*.CR3'))
        if not files:
            continue
        # Deterministic first file: diagnostic, not claimed representative or paired.
        path = files[0]
        record = dict(path=str(path.relative_to(root)), sha256=digest(path),
                      selection='first lexicographic file per ISO folder, diagnostic only')
        try:
            with rawpy.imread(str(path)) as raw:
                pattern = raw.raw_pattern
                if pattern is None or pattern.shape != (2, 2):
                    raise ValueError('Expected a 2x2 CFA pattern')
                # Copy every needed value while decoder-owned memory is alive.
                pattern = pattern.copy()
                black = list(raw.black_level_per_channel)
                white = int(raw.white_level)
                desc = raw.color_desc.decode('ascii')
                mosaic = raw.raw_image_visible
                height, width = mosaic.shape
                y = (height // 2 - 128) // 2 * 2
                x = (width // 2 - 128) // 2 * 2
                crop = mosaic[y:y + 256, x:x + 256].copy().astype(float)
                record.update(shape=[height, width], roi_mosaic=[y, x, 256, 256],
                              black=black, white=white, cfa=pattern.tolist(), descriptor=desc)
            planes = []
            for dy, dx in ((0, 0), (0, 1), (1, 0), (1, 1)):
                part = crop[dy::2, dx::2]
                sigma, confidence, tiles = noise_map(part)
                index = int(pattern[dy, dx])
                planes.append(dict(name=desc[index], offset=[dy, dx], raw_index=index,
                                   mean_dn=float(part.mean()), min_dn=float(part.min()), max_dn=float(part.max()),
                                   metadata_black_dn=black[index],
                                   saturation_fraction=float((part >= white).mean()),
                                   local_sigma_mean_dn=float(sigma.mean()),
                                   agreement_fraction=float((confidence >= .75).mean()), tiles=tiles))
            record.update(status='measured_no_ground_truth', planes=planes,
                          physical_fit_allowed=False,
                          warning='DN as exposed by rawpy; black/white coordinate consistency not established; no paired truth')
        except (rawpy.LibRawError, ValueError) as error:
            record.update(status='excluded', reason=str(error))
        results.append(record)
        print('RAW diagnostic:', path.name, record['status'], flush=True)
    return dict(root=str(root.resolve()), inventory=inventory, selected=results,
                previous_ptc=history, rawpy_version=rawpy.__version__,
                libraw_version=list(rawpy.libraw_version))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not args.corpus.is_dir() or args.output.exists():
        parser.error('Corpus must exist; output must not exist')
    now = datetime.now(timezone.utc)
    source = Path(__file__).resolve()
    result = dict(campaign_id='noise-planes-v002-' + now.strftime('%Y%m%dT%H%M%SZ'),
                  schema_version=1, data_version=2, measured_at_utc=now.isoformat(),
                  supersedes_campaign_id='noise-planes-v001-20261020T210951Z',
                  revision_reason='Add full-plane same-fusion control and 64px scale; v001 filename timestamp was incorrect, embedded UTC remains authoritative',
                  revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
                  sources={p.name: digest(p) for p in (source, source.with_name('study_noise_level.py'))},
                  versions=dict(python=platform.python_version(), numpy=np.__version__, scipy=scipy.__version__),
                  configuration=dict(tile=32, stride=16, coarse_tile=64, coarse_stride=32, seeds=[1601, 1613],
                                     channels='synthetic Y/C1/C2 independently contaminated; not a camera transform',
                                     fusion='median of all-block Haar, Laplacian MAD, weak-DCT; PCA diagnostic only',
                                     agreement='max-min <= 0.5*median; positive sigma; overlap coverage >=0.75',
                                     residual='center minus 8-neighbor mean; denominator sqrt(9/8)*sigma; z>3',
                                     confidence='heuristic, not a probability',
                                     limits='white equal-variance independent residual reference; invalid as calibrated correlated-noise test'),
                  audit=dict(production_modified=False, images_written=False, images_deleted=False,
                             production_renderer_executed=False, dcp_applied=False,
                             real_pixel_noise_truth_available=False),
                  synthetic=synthetic(), corpus=corpus_audit(args.corpus))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2, allow_nan=False, ensure_ascii=False)
    print(len(result['synthetic']), 'synthetic planes:', args.output)


if __name__ == '__main__':
    main()
