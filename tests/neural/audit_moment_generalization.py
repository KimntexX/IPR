"""Frozen-model train/control and edge-support diagnosis, without fitting."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import time

import torch
from torch.nn import functional as F

from audit_bayer_phases import PHASE_NAMES
from bayer_canonical import predict
from diagnose_train_learnability import digest, save
from joint_bayer_unet import bilinear, sample_rgb
from moment_bayer_unet import model_from_config
from run_synthetic_joint_pilot import metrics, aggregate, tensor_hash
from synthetic_bayer_data import PATTERNS, FAMILIES, signal


def edge_regions(target, border=48, radius=3):
    """Mark both transition endpoints, dilate in full coordinates, then crop."""
    if (target.ndim != 4 or target.shape[:2] != (1, 3)
            or not torch.isfinite(target).all() or min(target.shape[-2:]) <= 2*border
            or border < 1 or radius < 0):
        raise ValueError('Finite single RGB field and valid core/margin required')
    h, w = target.shape[-2:]
    transition = torch.zeros(1, 1, h, w, dtype=torch.bool, device=target.device)
    horizontal = (target[:, :, :, 1:] != target[:, :, :, :-1]).any(dim=1, keepdim=True)
    vertical = (target[:, :, 1:, :] != target[:, :, :-1, :]).any(dim=1, keepdim=True)
    transition[:, :, :, 1:] |= horizontal
    transition[:, :, :, :-1] |= horizontal
    transition[:, :, 1:, :] |= vertical
    transition[:, :, :-1, :] |= vertical
    band = F.max_pool2d(transition.float(), 2*radius+1, stride=1, padding=radius) > 0
    band = band[:, :, border:-border, border:-border]
    return dict(near_edge=band, away_from_edge=~band)


def regional_metrics(target, base, output, mask, border=48):
    fields = [v[:, :, border:-border, border:-border] for v in (target, base, output)]
    if (any(v.shape != fields[0].shape or not torch.isfinite(v).all() for v in fields)
            or mask.dtype != torch.bool or mask.shape != fields[0][:, :1].shape):
        raise ValueError('Matching finite RGB cores and Boolean region required')
    before, after = fields[1]-fields[0], fields[2]-fields[0]
    opponent = lambda v: torch.stack((v[:, 0]-v[:, 1], v[:, 2]-v[:, 1]), dim=1)
    return dict(pixels=int(mask.sum()),
        baseline_sse=float((before.square()*mask).sum()), output_sse=float((after.square()*mask).sum()),
        baseline_opponent_sse=float((opponent(before).square()*mask).sum()),
        output_opponent_sse=float((opponent(after).square()*mask).sum()))


def summarize_regions(records, region):
    values = [r['regions'][region] for r in records]
    pixels = sum(v['pixels'] for v in values)
    sums = {k: sum(v[k] for v in values) for k in
            ('baseline_sse', 'output_sse', 'baseline_opponent_sse', 'output_opponent_sse')}
    base = sums['baseline_sse']/(3*pixels) if pixels else None
    opposite = sums['baseline_opponent_sse']/(2*pixels) if pixels else None
    return dict(cases=len(values), nonempty_cases=sum(v['pixels'] > 0 for v in values), pixels=pixels,
        **sums, baseline_mse=base, output_mse=sums['output_sse']/(3*pixels) if pixels else None,
        mse_ratio=sums['output_sse']/sums['baseline_sse'] if base is not None and base > 1e-12 else None,
        opponent_mse_ratio=sums['output_opponent_sse']/sums['baseline_opponent_sse']
            if opposite is not None and opposite > 1e-12 else None)


def summarize(records):
    if not records:
        return None
    result = dict(all=aggregate(records),
        distinct_contents=len({(r['family'], r['seed']) for r in records}),
        by_family={f: aggregate([r for r in records if r['family'] == f])
            for f in FAMILIES if any(r['family'] == f for r in records)})
    edges = [r for r in records if r['family'] == 'edge']
    result['edge_regions'] = {name: summarize_regions(edges, name)
        for name in ('near_edge', 'away_from_edge')} if edges else None
    return result


def edge_orientation(seed):
    # Exact normal-angle rule of the frozen procedural generator, not inferred
    # from a reconstructed image. Bins are descriptive and fixed before replay.
    angle = float(torch.rand(8, generator=torch.Generator().manual_seed(seed))[0])*180
    return angle, min(3, int(angle//45))


def run(parent, phase_audit, output):
    root = Path(__file__).resolve().parents[2]
    parent, phase_audit, output = [p.resolve() for p in (parent, phase_audit, output)]
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in (parent, phase_audit, output)):
        raise ValueError('Use dedicated neural build')
    proof = json.loads((parent/'result.json').read_text(encoding='utf-8'))
    phase = json.loads((phase_audit/'result.json').read_text(encoding='utf-8'))
    config = proof['freeze']['config']
    weights = parent/'synthetic-joint-weights.pt'
    if (proof['status'] != 'completed_synthetic_reconstruction_stage_not_photographic_qualification'
            or config['model_kind'] != 'moment' or not config['canonical_input'] or not config['exact_observations']
            or phase['status'] != 'completed_crossed_bayer_phase_audit_no_training'
            or phase['freeze']['parent']['result_sha256'] != digest(parent/'result.json')
            or digest(weights) != proof['weights_sha256']):
        raise ValueError('Expected fixed completed moment pilot and phase audit')
    for name, expected in proof['freeze']['tools'].items():
        if digest(Path(__file__).with_name(name)) != expected:
            raise ValueError('Changed pilot dependency: '+name)
    train, control = proof['freeze']['train_cases'], proof['freeze']['evaluation_cases']
    if (len(train) != 40 or len(control) != 20
            or len({(c['family'], c['seed']) for c in train+control}) != 60):
        raise ValueError('Expected 40 training and 20 disjoint control contents')
    now = datetime.now(timezone.utc)
    campaign = 'neural-moment-generalization-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    tools = {**proof['freeze']['tools'], Path(__file__).name: digest(Path(__file__))}
    freeze = dict(config=config, parent_result_sha256=digest(parent/'result.json'),
        phase_result_sha256=digest(phase_audit/'result.json'), weights_sha256=digest(weights), tools=tools,
        git_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        train_cases=train, control_cases=control, patterns=PATTERNS,
        edge_band='Both exact truth-transition endpoints dilated by Chebyshev radius3 before core48 crop.',
        normal_angle_bins_degrees=[0, 45, 90, 135, 180], relative_error_mse_floor=1e-12,
        policy='Same final weights, all four phases per content; training-phase exposure tracked separately.')
    output.mkdir(parents=True, exist_ok=False)
    save(output/'freeze.json', freeze)
    torch.set_num_threads(4)
    torch.set_num_interop_threads(1)
    torch.use_deterministic_algorithms(True)
    model = model_from_config(config)
    checkpoint = torch.load(weights, map_location='cpu', weights_only=True)
    if checkpoint['freeze_sha256'] != proof['freeze_sha256']:
        raise ValueError('Checkpoint provenance differs')
    model.load_state_dict(checkpoint['state_dict'], strict=True)
    model.requires_grad_(False)
    model.eval()
    old = {(r['family'], r['seed'], tuple(r['pattern'])): r for r in phase['records']}
    if len(old) != 80:
        raise ValueError('Expected 80 distinct prior control records')
    rows = []
    result = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(),
        freeze=freeze, records=rows, status='incomplete', versions=dict(torch=torch.__version__),
        limits=['Already consulted procedural families; not independent photographic validation.',
            '40 train and 20 control contents, not 240 independent scenes; no fit or new settings.',
            'Only one of four phases was trained for each training content.',
            'Orientation groups contain few unequal contents; descriptive only.',
            'Truth-based edge masks for diagnosis only; RGB camera units, no noise or sensor model.'])
    started = time.monotonic()
    try:
        with torch.inference_mode():
            for split, cases in (('train', train), ('control', control)):
                for case in cases:
                    target = signal(case['family'], case['seed'], config['side'])[None]
                    target_hash = tensor_hash(target)
                    for i, values in enumerate(PATTERNS):
                        pattern = torch.tensor([values], dtype=torch.int64)
                        packed = sample_rgb(target, pattern)
                        packed_hash = tensor_hash(packed)
                        output_image = predict(model, packed/config['scale'], torch.zeros_like(packed), pattern,
                            exact_observations=True, canonical_input=True)*config['scale']
                        if not torch.equal(sample_rgb(output_image, pattern), packed):
                            raise ValueError('Observed values changed')
                        if tensor_hash(target) != target_hash or tensor_hash(packed) != packed_hash:
                            raise ValueError('Input changed')
                        output_hash = tensor_hash(output_image)
                        if split == 'control':
                            reference = old[(case['family'], case['seed'], tuple(values))]
                            if (reference['target_sha256'] != target_hash or reference['packed_sha256'] != packed_hash
                                    or reference['output_sha256'] != output_hash):
                                raise ValueError('Control replay differs')
                        base = bilinear(packed, pattern)
                        row = dict(split=split, family=case['family'], seed=case['seed'], phase_name=PHASE_NAMES[i],
                            pattern=list(values), trained_phase=split == 'train' and list(values) == case['pattern'],
                            target_sha256=target_hash, packed_sha256=packed_hash, output_sha256=output_hash,
                            observed_site_exact=True, metrics=metrics(target, base, output_image))
                        if case['family'] == 'edge':
                            row['normal_angle_degrees'], row['normal_bin'] = edge_orientation(case['seed'])
                            row['regions'] = {name: regional_metrics(target, base, output_image, mask)
                                for name, mask in edge_regions(target).items()}
                            for key in ('pixels', 'baseline_sse', 'output_sse', 'baseline_opponent_sse', 'output_opponent_sse'):
                                total = sum(v[key] for v in row['regions'].values())
                                if abs(total-row['metrics'][key]) > 1e-6*max(1., abs(total)):
                                    raise ValueError('Regional partition lost metric mass')
                        rows.append(row)
        result['by_split'] = {s: summarize([r for r in rows if r['split'] == s]) for s in ('train', 'control')}
        result['train_exposed_phase'] = summarize([r for r in rows if r['trained_phase']])
        result['train_unexposed_phases'] = summarize([r for r in rows if r['split'] == 'train' and not r['trained_phase']])
        result['edge_by_split_normal'] = {s: {str(b): summarize([r for r in rows if r['split'] == s
            and r.get('normal_bin') == b]) for b in range(4)} for s in ('train', 'control')}
        result['by_split_phase'] = {s: {p: summarize([r for r in rows if r['split'] == s and r['phase_name'] == p])
            for p in PHASE_NAMES} for s in ('train', 'control')}
        if digest(weights) != freeze['weights_sha256'] or any(digest(Path(__file__).with_name(n)) != h for n, h in tools.items()):
            raise ValueError('Model or implementation changed during replay')
        result['status'] = 'completed_frozen_train_control_edge_diagnosis_no_training'
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        result['elapsed_seconds'] = time.monotonic()-started
        save(output/'result.json', result)
        save(root/'docs/validation/raw'/f'{campaign}.json', result)
    print(json.dumps({s: result['by_split'][s]['all'] for s in ('train', 'control')}, indent=2), flush=True)
    print('COMPLETE '+campaign, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--parent', type=Path, required=True)
    parser.add_argument('--phase-audit', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(args.parent, args.phase_audit, args.output)
