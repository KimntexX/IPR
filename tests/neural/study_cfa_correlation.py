"""Known-content feature audit: texture versus matched flat noise; no network fit."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import statistics
import time

import torch

from cfa_correlation import correlation_features
from confirm_fine_noise import fine_signal, observation
from diagnose_train_learnability import digest, save
from edge_protected_noise import edge_confidence
from memory_budget import MemoryGuard
from run_noisy_moment_pilot import NOISE, observation as original_observation
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS, signal


def auc(positive, negative):
    if not positive or not negative:
        raise ValueError('Both paired classes required')
    ordered = sorted([(v, 1) for v in positive]+[(v, 0) for v in negative])
    rank_sum, index = 0., 0
    while index < len(ordered):
        end = index+1
        while end < len(ordered) and ordered[end][0] == ordered[index][0]:
            end += 1
        rank_sum += ((index+1+end)/2)*sum(label for _,label in ordered[index:end])
        index = end
    return (rank_sum-len(positive)*(len(positive)+1)/2)/(len(positive)*len(negative))


def scores(values, sigma):
    offset = (values.shape[-1]-32)//2
    crop = (slice(None), slice(None), slice(offset, -offset), slice(offset, -offset))
    result = dict(edge=float((1-edge_confidence(values, sigma))[crop].mean()))
    for window in (9, 17):
        features = correlation_features(values, sigma, window).abs().amax(dim=2)
        result['correlation'+str(window)] = features[crop].mean(dim=(-2, -1))[0].tolist()
    return result


def run(root, output):
    base = root/'build-patch_neural_restoration'
    if not output.resolve().is_relative_to(base.resolve()):
        raise ValueError('Dedicated neural build required')
    source_path = base/'fixed-point-fine-plan-v001/plan.json'
    source = json.loads(source_path.read_text(encoding='utf-8'))
    original_path = base/'noisy-structure-plan-v001/plan.json'
    original = json.loads(original_path.read_text(encoding='utf-8'))
    now = datetime.now(timezone.utc)
    identity = 'neural-correlation-cue-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    result = dict(schema_version=1, data_version=1, campaign_id=identity, measured_at_utc=now.isoformat(),
        status='incomplete', source_plan_sha256=digest(source_path), original_plan_sha256=digest(original_path),
        tools={n:digest(Path(__file__).with_name(n)) for n in ('study_cfa_correlation.py', 'cfa_correlation.py',
            'confirm_fine_noise.py', 'synthetic_bayer_data.py', 'edge_protected_noise.py')},
        policy='All known fine positive-noise fields once, not duplicated by model seed. Same random draws for each texture and per-plane-mean flat placebo. Features see only observed CFA and estimated sigma.',
        limits=['Descriptive simulation separation, not denoising or calibrated probability.',
            'Placebos are generated using truth only for audit labels, never for feature computation.',
            'Correlated fields and repeated procedural contents; no independent confidence interval.',
            'Known independent Gaussian simulation law, not real sensor noise calibration.'], records=[], flat_controls=[])
    output.mkdir(parents=True, exist_ok=False)
    guard, started = MemoryGuard(**original['memory_policy']), time.monotonic()
    try:
        with guard, torch.inference_mode():
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            cached = None
            for field in source['fields']:
                if field['noise_index'] == 0:
                    continue
                guard.check()
                case = source['cases'][field['case_index']]
                if cached != field['case_index']:
                    truth, _, _ = fine_signal(case)
                    if tensor_hash(truth) != case['target_sha256']:
                        raise ValueError('Known fine target changed')
                    cached = field['case_index']
                tags = torch.tensor([PATTERNS[field['phase_index']]])
                clean, observed, sigma = observation(truth, tags, field)
                if tensor_hash(observed) != field['observation_sha256'] or tensor_hash(sigma) != field['sigma_sha256']:
                    raise ValueError('Known noisy field changed')
                flat = clean.mean(dim=(-2, -1), keepdim=True).expand_as(clean)
                a,b = NOISE[field['noise_index']]
                noise = torch.randn(flat.shape, generator=torch.Generator().manual_seed(field['noise_seed']))
                placebo = flat+noise*(a*flat.clamp_min(0)+b).sqrt()
                placebo_sigma = (a*placebo.clamp_min(0)+b).sqrt()
                result['records'].append(dict(field, case=case, target_sha256=case['target_sha256'],
                    placebo_sha256=tensor_hash(placebo), texture=scores(observed, sigma), flat=scores(placebo, placebo_sigma)))
            lookup = {(r['case_index'],r['phase'],r['noise_index']):r for r in original['observations']}
            for index, case in enumerate(original['mixed']):
                if case['family'] != 'constant':
                    continue
                truth = signal('constant', case['seed'], 128)[None]
                for phase, pattern in enumerate(PATTERNS):
                    tags = torch.tensor([pattern])
                    for level in (1,2):
                        guard.check()
                        field = lookup[index,phase,level]
                        values, sigma = original_observation(truth, tags, level, field['noise_seed'])
                        if tensor_hash(values) != field['noisy_sha256']:
                            raise ValueError('Known constant noise changed')
                        result['flat_controls'].append(dict(case_index=index, phase=phase, noise_index=level,
                            mean_by_plane=values.mean(dim=(-2,-1))[0].tolist(), scores=scores(values,sigma)))
            summaries = []
            for level in (1,2):
                for amplitude in (.008,.06):
                    for chromatic in (False,True):
                        rows=[r for r in result['records'] if r['noise_index']==level and r['case']['amplitude']==amplitude
                            and r['case']['chromatic']==chromatic]
                        for colour in (0,1,2):
                            selected=[(r,p) for r in rows for p,c in enumerate(PATTERNS[r['phase_index']]) if (1 if c==3 else c)==colour]
                            entry=dict(noise_index=level, amplitude=amplitude, chromatic=chromatic, colour=colour, pairs=len(selected), features={})
                            for feature in ('edge','correlation9','correlation17'):
                                positive=[r['texture'][feature] if feature=='edge' else r['texture'][feature][p] for r,p in selected]
                                negative=[r['flat'][feature] if feature=='edge' else r['flat'][feature][p] for r,p in selected]
                                entry['features'][feature]=dict(auc=auc(positive,negative),
                                    paired_superiority=statistics.mean(float(a>b)+.5*float(a==b) for a,b in zip(positive,negative)),
                                    texture_median=statistics.median(positive), flat_median=statistics.median(negative))
                            summaries.append(entry)
            if len(result['records']) != 1728 or len(result['flat_controls']) != 32:
                raise ValueError('Missing declared observations')
            result.update(status='completed_known_cfa_correlation_cue_audit_not_qualification', summaries=summaries)
    finally:
        result['memory']=guard.stats()
        result['elapsed_seconds']=time.monotonic()-started
        save(output/'result.json', result)
        save(root/'docs/validation/raw'/f'{identity}.json', result)
    print('COMPLETE', identity, flush=True)


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    run(Path(__file__).resolve().parents[2],args.output)
