"""Engineering-only crop/whole-view agreement on cached signed CFA and noise."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time

import numpy as np
import torch

from bayer_canonical import predict
from compare_reconstruction_objectives import confined
from diagnose_train_learnability import digest, save
from edge_protected_noise import EdgeProtectedNoisyUNet
from memory_budget import MemoryGuard
from mosaic_data import pack_mosaic, read_stage
from noisy_moment_unet import NoisyMomentUNet, FORWARD_TOOLS
from run_noisy_moment_pilot import runtime_versions
from synthetic_bayer_data import PATTERNS


def centered_origin(whole_side, tile_side):
    if (not isinstance(whole_side, int) or not isinstance(tile_side, int)
            or tile_side < 128 or tile_side >= whole_side or (whole_side-tile_side) % 2):
        raise ValueError('Even centred tile within whole view required')
    origin = (whole_side-tile_side)//2
    if origin % 8:
        raise ValueError('Tile origin must preserve Bayer and two pooling-grid phases')
    return origin


def compare_core(whole, tile):
    if (whole.ndim != 4 or whole.shape[:2] != (1, 3) or tile.shape[:2] != (1, 3)
            or whole.shape[-1] != whole.shape[-2] or tile.shape[-1] != tile.shape[-2]
            or not torch.isfinite(whole).all() or not torch.isfinite(tile).all()):
        raise ValueError('Finite square N1 RGB views required')
    centered_origin(whole.shape[-1], tile.shape[-1])
    a, b = whole.shape[-1]//2, tile.shape[-1]//2
    reference, candidate = whole[:, :, a-16:a+16, a-16:a+16], tile[:, :, b-16:b+16, b-16:b+16]
    delta = candidate-reference
    return dict(components=delta.numel(), max_absolute_difference=float(delta.abs().max()),
        mean_squared_difference=float(delta.square().mean()), bit_exact=bool(torch.equal(reference, candidate)))


def validate_capture_coverage(sources):
    if len(sources) != 3 or len({c['source_sha256'] for c in sources}) != 3:
        raise ValueError('Three distinct qualified photographic sources required')
    for capture in sources:
        stages = capture['stages']
        selected = [s for s in stages if s['stage'] == 'normalized_cfa_before_wb_unclipped']
        if (capture.get('rgb_replay_bit_exact') is not True
                or capture.get('rgb_regions_compared') != 9 or len(stages) != 18
                or len(selected) != 9 or {s['roi'] for s in selected} != set(range(9))):
            raise ValueError('All nine qualified photographic ROIs required per source')


def run(root, data_path, folders, capture_path, kind, output):
    confined(root, [data_path, capture_path, output]+folders)
    data = json.loads(data_path.read_text(encoding='utf-8'))
    captures = json.loads(capture_path.read_text(encoding='utf-8'))
    if captures['status'] != 'qualified_signed_mosaic_inlet_not_joint_model' or data.get('runtime') != runtime_versions():
        raise ValueError('Qualified cached inlet and pinned runtime required')
    references = []
    for folder in folders:
        p = json.loads((folder/'result.json').read_text(encoding='utf-8'))
        if (p['status'] != 'completed_synthetic_noise_pilot_not_photographic_qualification'
                or p['plan_sha256'] != digest(data_path) or digest(folder/'weights.pt') != p['weights_sha256']
                or any(p['plan']['tools'].get(n) != digest(Path(__file__).with_name(n)) for n in FORWARD_TOOLS)):
            raise ValueError('Completed current model required; quality verdict remains separate')
        references.append(dict(seed=p['seed'], folder=str(folder.resolve()), result_sha256=digest(folder/'result.json'),
            weights_sha256=p['weights_sha256'], prior_quality_passed=p['pilot_gates_passed']))
    if len(references) != 2 or {r['seed'] for r in references} != {71, 73}:
        raise ValueError('Both model seeds required')
    sources = [r['capture'] for r in captures['records']]
    validate_capture_coverage(sources)
    for c in sources:
        descriptor = Path(c['descriptor'])
        pixels = descriptor.parent/c['pixels']
        confined(root, [descriptor, pixels])
        if digest(descriptor) != c['descriptor_sha256'] or digest(pixels) != c['pixels_sha256']:
            raise ValueError('Cached mosaic changed')
    now = datetime.now(timezone.utc)
    campaign = 'neural-tiling-audit-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    output.mkdir(parents=True, exist_ok=False)
    freeze = dict(data_plan_sha256=digest(data_path), captures_sha256=digest(capture_path), models=references,
        kind=kind, sources=sources, tolerance=2e-6, full_side=384, core_side=32,
        tile_sides=[128, 160, 192], declared_required_halos=[64, 80], sigma_scenarios=[0., .01, .03],
        runtime=runtime_versions(), tools={n: digest(Path(__file__).with_name(n)) for n in
            FORWARD_TOOLS+('audit_neural_tiles.py', 'edge_protected_noise.py', 'mosaic_data.py')},
        limits=['Engineering crop/whole-view comparison, no clean photographic target or denoising quality claim.',
            'Constant supplied sigma is a stress scenario, not a noise estimate for these cameras/ISO.',
            'Three cached R6III sources, nine ROIs each; no new RAW decode or source modification.',
            'Halo48 is diagnostic; halo64 and80 must agree with the full384 view for every case.'])
    save(output/'freeze.json', freeze)
    result = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(),
        freeze=freeze, freeze_sha256=digest(output/'freeze.json'), status='incomplete', records=[])
    guard, started = MemoryGuard(**data['memory_policy']), time.monotonic()
    try:
        with guard, torch.inference_mode():
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            for ref in references:
                factory = EdgeProtectedNoisyUNet if kind == 'protected' else NoisyMomentUNet
                model = factory(data['config']['width']).eval().requires_grad_(False)
                model.load_state_dict(torch.load(Path(ref['folder'])/'weights.pt', weights_only=True, map_location='cpu')['state_dict'], strict=True)
                def measure(packed, labels, identity):
                    before = packed.clone()
                    for level in freeze['sigma_scenarios']:
                        guard.check()
                        sigma = torch.full_like(packed, level)
                        full = predict(model, packed/2, sigma/2, labels, False, True)*2
                        for side in freeze['tile_sides']:
                            origin = centered_origin(384, side)//2
                            part = packed[:, :, origin:origin+side//2, origin:origin+side//2]
                            deviations = sigma[:, :, origin:origin+side//2, origin:origin+side//2]
                            tile = predict(model, part/2, deviations/2, labels, False, True)*2
                            result['records'].append(dict(seed=ref['seed'], source=identity, sigma=level,
                                side=side, halo=(side-32)//2, **compare_core(full, tile)))
                    if not torch.equal(before, packed):
                        raise ValueError('Source mosaic changed')
                for c in sources:
                    for stage in c['stages']:
                        if stage['stage'] != 'normalized_cfa_before_wb_unclipped':
                            continue
                        guard.check()
                        packed = torch.from_numpy(pack_mosaic(read_stage(c, stage)))[None]
                        labels = torch.tensor(np.array(stage['pattern']).reshape(1, 4), dtype=torch.int64)
                        measure(packed, labels, dict(source_sha256=c['source_sha256'], file=Path(c['source']).name,
                            iso=c['iso'], roi=stage['roi'], stage=stage['stage']))
                for phase, p in enumerate(PATTERNS):
                    packed = torch.randn(1, 4, 192, 192, generator=torch.Generator().manual_seed(99100000+phase))*.02+.3
                    measure(packed, torch.tensor([p]), dict(synthetic_noise_stress=True, phase_index=phase, seed=99100000+phase))
                print('AUDITED seed'+str(ref['seed']), flush=True)
                del model
            result['by_halo'] = {str(halo): dict(cases=sum(r['halo'] == halo for r in result['records']),
                maximum=max(r['max_absolute_difference'] for r in result['records'] if r['halo'] == halo),
                passed=all(r['max_absolute_difference'] <= freeze['tolerance'] for r in result['records'] if r['halo'] == halo))
                for halo in (48, 64, 80)}
            result['engineering_passed'] = all(result['by_halo'][str(h)]['passed'] for h in freeze['declared_required_halos'])
            guard.check()
            if any(digest(Path(__file__).with_name(n)) != h for n, h in freeze['tools'].items()):
                raise ValueError('Audit implementation changed')
            result['status'] = 'completed_tiling_engineering_audit_not_photographic_qualification'
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        result['memory'] = guard.stats()
        result['elapsed_seconds'] = time.monotonic()-started
        save(output/'result.json', result)
        save(root/'docs/validation/raw'/f'{campaign}.json', result)
    print('COMPLETE '+campaign+' engineering='+str(result['engineering_passed']), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-plan', type=Path, required=True)
    parser.add_argument('--models', nargs=2, type=Path, required=True)
    parser.add_argument('--captures', type=Path, required=True)
    parser.add_argument('--kind', choices=('plain', 'protected'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(Path(__file__).resolve().parents[2], args.data_plan, args.models, args.captures, args.kind, args.output)
