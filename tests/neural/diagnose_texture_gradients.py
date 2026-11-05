"""Loss-gradient conflicts on preselected known training textures, no updates."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time

import torch

from audit_spectral_noise import source_contract
from bayer_canonical import canonicalize, spatial_reflect
from campaign_artifacts import campaign_models
from context_data import extend_texture, extend_observation
from diagnose_context_learning import probe
from diagnose_multitone_training import select_events
from diagnose_train_learnability import digest, save
from fine_texture_training import texture
from memory_budget import MemoryGuard
from neural_inference_contract import check_inference
from noisy_moment_unet import NoisyMomentUNet
from noisy_training_policy import configure_trainable
from run_noisy_moment_pilot import observation, NOISE
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS


def run(root, output, prepared, base):
    build = root/'build-patch_neural_restoration'
    if not all(p.resolve().is_relative_to(build.resolve()) for p in (output, prepared, base)):
        raise ValueError('Dedicated neural paths required')
    plans = {arm:json.loads((prepared/arm/'plan.json').read_text(encoding='utf-8')) for arm in ('control','correlation')}
    selected = select_events(plans['control']['texture_augmentation']['events'])
    if selected != select_events(plans['correlation']['texture_augmentation']['events']):
        raise ValueError('Both arms must share selected training contents')
    now = datetime.now(timezone.utc)
    identity = 'neural-texture-gradients-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    result = dict(schema_version=1, campaign_id=identity, measured_at_utc=now.isoformat(), status='incomplete',
        script_sha256=digest(Path(__file__)), probe_sha256=digest(Path(__file__).with_name('diagnose_context_learning.py')),
        selection='First two strong training textures per family and frequency bin [2,6,12,20,29), both positive noises, seeds71/73 and arms; one fixed fresh simulated draw.',
        selected_events=selected, records=[],
        limits=['Descriptive gradients, not an optimizer update or evidence that projecting gradients improves quality.',
            'Known training contents, not reserved validation; losses and criteria unchanged.'])
    output.mkdir(parents=True, exist_ok=False)
    guard, started = MemoryGuard(**plans['control']['memory_policy']), time.monotonic()
    try:
        with guard:
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            for arm, plan in plans.items():
                qualified = json.loads((base/f'input-cues-{arm}-protected-v001/result.json').read_text(encoding='utf-8'))
                source = source_contract(root, qualified)
                check_inference(plan, source['tools'])
                folders = campaign_models(base/f'input-cues-{arm}-campaign-v001')
                for seed, folder in zip((71,73), folders):
                    proof = json.loads((folder/'result.json').read_text(encoding='utf-8'))
                    weights = folder/'weights.pt'
                    if proof['weights_sha256'] != digest(weights) or proof['plan_sha256'] != digest(prepared/arm/'plan.json'):
                        raise ValueError('Final paired weights changed')
                    model = NoisyMomentUNet(8).eval()
                    model.load_state_dict(torch.load(weights, weights_only=True)['state_dict'], strict=True)
                    configure_trainable(model, 'cfa_front', plan['config']['statistics_trainable'])
                    for event in selected:
                        centre, _ = texture(event['family'],event['serial'],128)
                        if tensor_hash(centre) != event['target_sha256']:
                            raise ValueError('Training target changed')
                        expanded = extend_texture(event['family'], event['serial'], 256)
                        tags = torch.tensor([PATTERNS[event['phase']]])
                        for level in (1,2):
                            guard.check()
                            noise_seed = 270000000+event['serial']*3+level
                            values, sigma = observation(centre, tags, level, noise_seed)
                            values, sigma = extend_observation(expanded, tags, values, sigma, *NOISE[level], 280000000+noise_seed)
                            values, sigma, labels, flags = canonicalize(values/2,sigma/2,tags)
                            # Apply the same canonical spatial transform to the clean RGB target.
                            truth = spatial_reflect(expanded/2, flags)
                            row = probe(model,values,sigma,labels,truth,112,0.)
                            result['records'].append(dict(arm=arm, seed=seed, serial=event['serial'], family=event['family'],
                                frequency=event['frequency'], amplitude=event['amplitude'], noise_index=level,
                                weights_sha256=digest(weights), probe=row))
                    del model
                    print('GRADIENTS',arm,seed,flush=True)
            if len(result['records']) != 128:
                raise ValueError('Expected128fixed probes')
            result['status'] = 'completed_known_texture_gradient_diagnosis_not_qualification'
    except Exception as error:
        result.update(status='failed_diagnostic_execution', error=str(error))
        raise
    finally:
        result['elapsed_seconds'] = time.monotonic()-started
        result['memory'] = guard.stats()
        save(output/'result.json',result)
        save(root/'docs/validation/raw'/f'{identity}.json',result)
    print(result['status'],flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('output','prepared','base'):
        parser.add_argument('--'+name,type=Path,required=True)
    args = parser.parse_args()
    run(Path(__file__).resolve().parents[2],args.output,args.prepared,args.base)
