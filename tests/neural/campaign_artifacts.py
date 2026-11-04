"""Resolve complete model lineages after technical memory retries, never by score."""
import hashlib
import json
from pathlib import Path

from continuation_policy import completed_training


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as handle:
        for block in iter(lambda:handle.read(1024*1024), b''):
            digest.update(block)
    return digest.hexdigest()


def campaign_models(campaign):
    campaign = Path(campaign).resolve()
    report = json.loads((campaign/'campaign.json').read_text(encoding='utf-8'))
    if report['status'] != 'completed_both_seeds':
        raise ValueError('Complete two-seed campaign required')
    if any(row['seed'] not in (71, 73) for row in report['attempts']):
        raise ValueError('Undeclared seed')
    models = []
    for seed in (71, 73):
        attempts = [r for r in report['attempts'] if r['seed'] == seed]
        if not attempts:
            raise ValueError('Missing seed lineage')
        previous_checkpoint = None
        for number, row in enumerate(attempts, 1):
            folder = Path(row['output']).resolve()
            if row['number'] != number or folder != campaign/f'seed{seed}-attempt{number:02d}':
                raise ValueError('Attempt order or confinement differs')
            result_path = folder/'result.json'
            if sha256(result_path) != row['result_sha256']:
                raise ValueError('Attempt result identity differs')
            proof = json.loads(result_path.read_text(encoding='utf-8'))
            if (proof['seed'] != seed or proof['status'] != row['status']
                    or proof['plan_sha256'] != report['plan_sha256']):
                raise ValueError('Attempt provenance differs')
            resumed = row.get('resumed_from')
            restored = proof['training_progress'].get('restored_from')
            if previous_checkpoint is None:
                if resumed is not None or restored is not None:
                    raise ValueError('Unexplained checkpoint restoration')
            else:
                if (resumed is None or Path(resumed).resolve() != Path(previous_checkpoint['path']).resolve()
                        or restored is None or restored['sha256'] != previous_checkpoint['sha256']
                        or restored['completed_step'] != previous_checkpoint['completed_step']
                        or Path(restored['path']).resolve() != Path(previous_checkpoint['path']).resolve()):
                    raise ValueError('Memory retry did not continue the declared checkpoint')
            if number < len(attempts):
                if proof['status'] != 'aborted_for_memory_budget':
                    raise ValueError('Only technical memory stops permit another attempt')
                previous_checkpoint = proof['training_progress'].get('checkpoint')
                if previous_checkpoint is not None:
                    checkpoint = Path(previous_checkpoint['path']).resolve()
                    if (checkpoint != folder/'training-latest.pt'
                            or sha256(checkpoint) != previous_checkpoint['sha256']):
                        raise ValueError('Interrupted checkpoint identity differs')
            else:
                if proof['status'] != 'completed_synthetic_noise_pilot_not_photographic_qualification' or not completed_training(proof):
                    raise ValueError('Final declared training stage incomplete')
                if sha256(folder/'weights.pt') != proof['weights_sha256']:
                    raise ValueError('Final weights identity differs')
                models.append(folder)
    return models
