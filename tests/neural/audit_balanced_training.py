"""Compare two frozen moment models on the same 160 training content/phase pairs."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import time

import torch

from audit_bayer_phases import PHASE_NAMES
from audit_moment_generalization import edge_regions, regional_metrics, summarize
from bayer_canonical import predict
from diagnose_train_learnability import digest, save
from joint_bayer_unet import bilinear, sample_rgb
from moment_bayer_unet import model_from_config
from run_balanced_moment_pilot import measure
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS, signal


def paired_summary(rows):
    if not rows:
        raise ValueError('Nonempty paired measurements required')
    summaries = {name: summarize([dict(family=r['family'], seed=r['seed'], metrics=r[name],
        **(dict(regions=r[name+'_regions']) if r['family'] == 'edge' else {})) for r in rows])
        for name in ('reference', 'candidate')}
    informative = [r for r in rows if r['reference']['baseline_sse']/(3*r['reference']['pixels']) > 1e-12]
    delta = lambda r: (r['candidate']['output_sse']-r['reference']['output_sse'])/(3*r['reference']['pixels'])
    total_exposures = sum(r['candidate_exposures'] for r in rows)
    if total_exposures <= 0:
        raise ValueError('Positive training exposure count required')
    weighted = {name: sum(r['candidate_exposures']*r[name]['output_sse']/(3*r[name]['pixels'])
        for r in rows)/total_exposures for name in ('reference', 'candidate')}
    return dict(**summaries, informative_cases=len(informative),
        improved=sum(delta(r) < -1e-12 for r in informative),
        worsened=sum(delta(r) > 1e-12 for r in informative),
        tied=sum(abs(delta(r)) <= 1e-12 for r in informative),
        candidate_exposures=total_exposures, candidate_exposure_weighted_raw_mse=weighted,
        candidate_over_reference_mse=summaries['candidate']['all']['output_mse']/summaries['reference']['all']['output_mse']
            if summaries['reference']['all']['output_mse'] > 1e-12 else None)


def run(reference, candidate, previous_audit, output):
    root = Path(__file__).resolve().parents[2]
    reference, candidate, previous_audit, output = [p.resolve() for p in (reference, candidate, previous_audit, output)]
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve())
               for p in (reference, candidate, previous_audit, output)):
        raise ValueError('Use dedicated neural build')
    proofs = {name: json.loads((folder/'result.json').read_text(encoding='utf-8'))
        for name, folder in (('reference', reference), ('candidate', candidate))}
    old = json.loads((previous_audit/'result.json').read_text(encoding='utf-8'))
    if (old['status'] != 'completed_frozen_train_control_edge_diagnosis_no_training'
            or old['freeze']['parent_result_sha256'] != digest(reference/'result.json')
            or proofs['candidate']['freeze']['reference']['result_sha256'] != digest(reference/'result.json')
            or proofs['reference']['freeze']['config'] != proofs['candidate']['freeze']['config']
            or proofs['reference']['freeze']['train_cases'] != proofs['candidate']['freeze']['train_cases']):
        raise ValueError('Inconsistent paired experiment provenance')
    config = proofs['candidate']['freeze']['config']
    cases = proofs['candidate']['freeze']['train_cases']
    counts = proofs['candidate']['freeze']['phase_counts_by_content']
    if (len(cases) != 40 or len(counts) != 40 or any(len(c) != 4 or min(c) < 1 or max(c)-min(c) > 1 for c in counts)
            or config['model_kind'] != 'moment'):
        raise ValueError('Expected fully exposed balanced 40-content experiment')
    tools = {**old['freeze']['tools'], **proofs['candidate']['freeze']['tools'], Path(__file__).name: digest(Path(__file__))}
    for name, expected in tools.items():
        if digest(Path(__file__).with_name(name)) != expected:
            raise ValueError('Changed experiment implementation: '+name)
    torch.set_num_threads(4)
    torch.set_num_interop_threads(1)
    torch.use_deterministic_algorithms(True)
    models = {}
    for name, folder in (('reference', reference), ('candidate', candidate)):
        proof = proofs[name]
        if (proof['status'] != 'completed_synthetic_reconstruction_stage_not_photographic_qualification'
                or digest(folder/'synthetic-joint-weights.pt') != proof['weights_sha256']):
            raise ValueError('Incomplete or modified model')
        checkpoint = torch.load(folder/'synthetic-joint-weights.pt', map_location='cpu', weights_only=True)
        if checkpoint['freeze_sha256'] != proof['freeze_sha256']:
            raise ValueError('Checkpoint provenance differs')
        model = model_from_config(config)
        model.load_state_dict(checkpoint['state_dict'], strict=True)
        model.requires_grad_(False)
        model.eval()
        models[name] = model
    now = datetime.now(timezone.utc)
    campaign = 'neural-balanced-training-audit-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze = dict(config=config, tools=tools, train_cases=cases, phase_counts=counts,
        reference_result_sha256=digest(reference/'result.json'), candidate_result_sha256=digest(candidate/'result.json'),
        previous_audit_sha256=digest(previous_audit/'result.json'),
        weights_sha256={n: p['weights_sha256'] for n, p in proofs.items()},
        git_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        core_border=48, relative_error_mse_floor=1e-12, paired_absolute_mse_tolerance=1e-12,
        policy='Same160 training pairs, uniform pixels and exposure-weighted summaries; no gradient or optimizer step.')
    output.mkdir(parents=True, exist_ok=False)
    save(output/'freeze.json', freeze)
    old_rows = {(r['family'], r['seed'], tuple(r['pattern'])): r for r in old['records'] if r['split'] == 'train'}
    if len(old_rows) != 160:
        raise ValueError('Expected160 prior training records')
    rows = []
    result = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(),
        freeze=freeze, records=rows, status='incomplete', versions=dict(torch=torch.__version__),
        limits=['Training diagnostics, not validation; same40 procedural contents crossed with4 phases.',
            'Reference saw one phase per content; candidate saw all four. Do not call these160 independent scenes.',
            'Noisy inference, photographic benefit and unseen orientations not qualified here.',
            'Final loss only: no proof of convergence, optimization failure or model-capacity limit.'])
    started = time.monotonic()
    try:
        with torch.inference_mode():
            for index, case in enumerate(cases):
                truth = signal(case['family'], case['seed'], config['side'])[None]
                for phase, values in enumerate(PATTERNS):
                    pattern = torch.tensor([values], dtype=torch.int64)
                    source = sample_rgb(truth, pattern)
                    before = tensor_hash(source)
                    measured = {name: measure(model, truth, pattern, config['scale']) for name, model in models.items()}
                    previous = old_rows[(case['family'], case['seed'], tuple(values))]
                    if any(measured['reference'][k] != previous[k] for k in ('target_sha256', 'packed_sha256', 'output_sha256')):
                        raise ValueError('Reference training replay differs')
                    row = dict(family=case['family'], seed=case['seed'], pattern=list(values), phase_name=PHASE_NAMES[phase],
                        reference_exposed_phase=list(values) == case['pattern'], candidate_exposures=counts[index][phase],
                        target_sha256=tensor_hash(truth), packed_sha256=before,
                        output_sha256={name: v['output_sha256'] for name, v in measured.items()},
                        reference=measured['reference']['metrics'], candidate=measured['candidate']['metrics'])
                    if case['family'] == 'edge':
                        # Reuse the fixed masks and exact inference for regional diagnostics.
                        base = bilinear(source, pattern)
                        for name, model in models.items():
                            image = predict(model, source/config['scale'], torch.zeros_like(source), pattern,
                                exact_observations=True, canonical_input=True)*config['scale']
                            if tensor_hash(image) != measured[name]['output_sha256']:
                                raise ValueError('Regional inference differs')
                            row[name+'_regions'] = {n: regional_metrics(truth, base, image, mask)
                                for n, mask in edge_regions(truth).items()}
                    if tensor_hash(source) != before:
                        raise ValueError('Source changed')
                    rows.append(row)
        result['summary'] = paired_summary(rows)
        result['by_phase'] = {p: paired_summary([r for r in rows if r['phase_name'] == p]) for p in PHASE_NAMES}
        result['by_reference_exposure'] = {str(exposed): paired_summary([r for r in rows if r['reference_exposed_phase'] == exposed])
            for exposed in (True, False)}
        if any(digest(Path(__file__).with_name(n)) != h for n, h in tools.items()) or any(
                digest(folder/'synthetic-joint-weights.pt') != proofs[name]['weights_sha256']
                for name, folder in (('reference', reference), ('candidate', candidate))):
            raise ValueError('Model or implementation changed during audit')
        result['status'] = 'completed_paired_balanced_training_audit_no_training'
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        result['elapsed_seconds'] = time.monotonic()-started
        save(output/'result.json', result)
        save(root/'docs/validation/raw'/f'{campaign}.json', result)
    print(json.dumps({n: result['summary'][n]['all'] for n in ('reference', 'candidate')}, indent=2), flush=True)
    print('COMPLETE '+campaign, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('reference', 'candidate', 'previous-audit', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    run(args.reference, args.candidate, args.previous_audit, args.output)
