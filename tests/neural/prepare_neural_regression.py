"""Replay historical data with current weights; never label it independent."""
import argparse
import copy
from datetime import datetime, timezone
import json
from pathlib import Path

from audit_spectral_noise import source_contract
from compare_reconstruction_objectives import confined
from confirm_fine_noise import TOOLS as FINE_TOOLS
from confirm_noisy_model import TOOLS as CLASSIC_TOOLS
from diagnose_train_learnability import digest, save
from neural_inference_contract import check_inference


def replay_plan(source, historical, kind):
    if len(source['models']) != 2 or {r['seed'] for r in source['models']} != {71, 73}:
        raise ValueError('Both current seeds required')
    old = historical['plan']
    if kind == 'classic':
        if (historical['status'] != 'completed_independent_noisy_confirmation_not_photographic_qualification'
                or len(historical['groups']) != 6):
            raise ValueError('Complete historical classical cohort required, including failures')
        data = copy.deepcopy(source['data'])
        data.pop('observations', None)
        for key in ('edges', 'mixed'):
            data[key] = copy.deepcopy(old['data'][key])
        return dict(schema_version=1, role='regression', kind='protected', data=data,
            models=copy.deepcopy(source['models']), replicas=copy.deepcopy(old['replicas']),
            tools={n: digest(Path(__file__).with_name(n)) for n in CLASSIC_TOOLS})
    if kind == 'fine':
        if (historical['status'] != 'completed_fine_signal_confirmation_not_photographic_qualification'
                or len(historical['records']) != 4032):
            raise ValueError('Complete historical fine cohort required, including failures')
        plan = copy.deepcopy(old)
        plan.update(role='regression', source=copy.deepcopy(source),
            tools={n: digest(Path(__file__).with_name(n)) for n in FINE_TOOLS})
        return plan
    raise ValueError('Known current cohort contract required')


def prepare(root, source_path, historical_path, kind, output):
    confined(root, [source_path, historical_path, output])
    proof = json.loads(source_path.read_text(encoding='utf-8'))
    if proof.get('pilot_gates_passed') is not True:
        raise ValueError('Current two-seed development must pass before full regressions')
    source = source_contract(root, proof)
    check_inference(source['data'], source['tools'])
    historical = json.loads(historical_path.read_text(encoding='utf-8'))
    plan = replay_plan(source, historical, kind)
    plan.update(frozen_at_utc=datetime.now(timezone.utc).isoformat(),
        source_proof_path=str(source_path.resolve()), source_proof_sha256=digest(source_path),
        historical_result_sha256=digest(historical_path), historical_campaign_id=historical['campaign_id'],
        preparation_tool_sha256=digest(__file__),
        policy='Current arithmetic and both current weights on immutable previously observed inputs/criteria; all prior failures retained. Regression, not independent confirmation.')
    output.mkdir(parents=True, exist_ok=False)
    save(output/'plan.json', plan)
    print('PREPARED regression '+str(output/'plan.json'), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--historical', type=Path, required=True)
    parser.add_argument('--kind', choices=('classic', 'fine'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prepare(Path(__file__).resolve().parents[2], args.source, args.historical, args.kind, args.output)
