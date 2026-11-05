"""Training-content diagnosis of single-projection supervision, no fitting."""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import time

import torch
from torch.nn import functional as F

from bayer_canonical import canonicalize
from campaign_artifacts import campaign_models
from context_data import extend_texture, extend_observation
from diagnose_train_learnability import digest, save
from edge_protected_noise import EdgeProtectedNoisyUNet, edge_confidence
from fine_texture_training import texture
from joint_bayer_unet import sample_rgb
from memory_budget import MemoryGuard
from noisy_moment_unet import NoisyMomentUNet, FORWARD_TOOLS
from run_noisy_moment_pilot import observation, NOISE
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS
from texture_sampling import texture_identity


def select_events(events):
    chosen = []
    for family in ('weak_achromatic_wave', 'chromatic_wave'):
        for low, high in ((2, 6), (6, 12), (12, 20), (20, 29)):
            candidates = [e for e in events if e['family'] == family and e['amplitude'] >= .04
                and low <= e['frequency'] < high]
            if len(candidates) < 2:
                raise ValueError('Training strata lack two declared strong examples')
            chosen.extend(sorted(candidates, key=lambda e:e['serial'])[:2])
    return chosen


def select_domains(previous_events,current_events):
    """Freeze both known training domains instead of pretending disjoint data are shared."""
    if previous_events==current_events:
        return [dict(source_domain='shared',event=e) for e in select_events(current_events)]
    return [dict(source_domain=domain,event=e) for domain,events in
        (('previous',previous_events),('current',current_events)) for e in select_events(events)]


def bases(metadata, tags):
    axis = torch.arange(-64, 192, dtype=torch.float32)
    y, x = torch.meshgrid(axis, axis, indexing='ij')
    waves = []
    for prefix in ('', 'other_'):
        wave = ((x*math.cos(metadata[prefix+'direction'])+y*math.sin(metadata[prefix+'direction']))
            *(2*math.pi*metadata[prefix+'frequency']/128)+metadata[prefix+'phase_radians']).sin()
        packed = F.pixel_unshuffle(wave[None, None], 2)
        values, _, _, _ = canonicalize(packed, torch.zeros_like(packed), tags)
        waves.append(values[:, :, 56:-56, 56:-56])
    return waves


def run(root, output, plan_path, previous_campaign, campaign):
    base = root/'build-patch_neural_restoration'
    if not all(p.resolve().is_relative_to(base.resolve()) for p in (output, plan_path, previous_campaign, campaign)):
        raise ValueError('Dedicated neural build paths required')
    plan = json.loads(plan_path.read_text(encoding='utf-8'))
    for name in FORWARD_TOOLS:
        if digest(Path(__file__).with_name(name)) != plan['tools'][name]:
            raise ValueError('Forward arithmetic changed')
    model_folders = {variant:dict(zip((71,73),campaign_models(model_campaign)))
        for variant,model_campaign in (('previous',previous_campaign),('current',campaign))}
    domain_events = {}
    for variant,folders in model_folders.items():
        first=json.loads((folders[71]/'result.json').read_text(encoding='utf-8'))
        domain_events[variant]=first['plan']['texture_augmentation']['events']
        if variant=='current' and first['plan_sha256']!=digest(plan_path):
            raise ValueError('Current diagnosis plan differs from trained plan')
    selected = select_domains(domain_events['previous'],domain_events['current'])
    now = datetime.now(timezone.utc)
    identity = 'neural-multitone-diagnostic-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    result = dict(schema_version=1, campaign_id=identity, measured_at_utc=now.isoformat(),
        status='incomplete', plan_sha256=digest(plan_path), script_sha256=digest(Path(__file__)),
        selection='First2training events by serial in each family x frequency bin [2,6,12,20,29), amplitude>=.04, from each distinct training domain; no output selection.',
        selected_events=selected, records=[], models=[],
        limits=['Known training contents from either or both arms; seen-in-model-training declared per record. Three fresh simulated noise draws at high noise; descriptive, not independent qualification.',
            'Component fits marked ill-conditioned above1e4; channels with abs(colour)<.25 omitted from relative-amplitude summaries.'])
    output.mkdir(parents=True, exist_ok=False)
    guard, started = MemoryGuard(**plan['memory_policy']), time.monotonic()
    try:
        with guard, torch.inference_mode():
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            for variant, model_campaign in (('previous', previous_campaign), ('current', campaign)):
                folders = model_folders[variant]
                for seed in (71, 73):
                    folder = folders[seed]
                    proof = json.loads((folder/'result.json').read_text(encoding='utf-8'))
                    weights = folder/'weights.pt'
                    if digest(weights) != proof['weights_sha256']:
                        raise ValueError('Final weights changed')
                    trained_events = {e['serial']:e for e in proof['plan']['texture_augmentation']['events']}
                    if proof['plan']['texture_augmentation']['events']!=domain_events[variant]:
                        raise ValueError('Seed models differ in their declared training domain')
                    if any(digest(Path(__file__).with_name(n))!=proof['plan']['tools'][n] for n in FORWARD_TOOLS):
                        raise ValueError('Compared model forward arithmetic changed')
                    result['models'].append(dict(variant=variant, seed=seed,
                        weights_sha256=digest(weights), result_sha256=digest(folder/'result.json')))
                    model = EdgeProtectedNoisyUNet(8).eval().requires_grad_(False)
                    model.load_state_dict(torch.load(weights, weights_only=True)['state_dict'], strict=True)
                    for selection in selected:
                        event=selection['event']
                        seen_in_training=trained_events.get(event['serial'])==event
                        if selection['source_domain'] in ('shared',variant) and not seen_in_training:
                            raise ValueError('Declared own training domain is absent from model')
                        guard.check()
                        centre, meta = texture(event['family'], texture_identity(event), 128)
                        if tensor_hash(centre) != event['target_sha256']:
                            raise ValueError('Frozen training target changed')
                        truth = extend_texture(event['family'], texture_identity(event), 256)
                        tags = torch.tensor([PATTERNS[event['phase']]])
                        clean, _, labels, _ = canonicalize(sample_rgb(truth, tags)/2,
                            torch.zeros_like(sample_rgb(truth, tags)), tags)
                        clean_core = clean[:, :, 56:-56, 56:-56]
                        wave1, wave2 = bases(meta, tags)
                        collected = []
                        for replicate in range(3):
                            noise_seed = 240000000+event['serial']*10+replicate
                            old, sigma = observation(centre, tags, 2, noise_seed)
                            values, sigma = extend_observation(truth, tags, old, sigma, *NOISE[2], 250000000+noise_seed)
                            values, sigma, labels, _ = canonicalize(values/2, sigma/2, tags)
                            raw = NoisyMomentUNet.correct_observations(model, values, sigma, labels)
                            protected = values+edge_confidence(values, sigma)*(raw-values)
                            if not torch.equal(protected, model.correct_observations(values, sigma, labels)):
                                raise ValueError('Protected diagnostic mirror differs')
                            outputs = dict(noisy=values, raw=raw, protected=protected)
                            for plane in range(4):
                                colour = int(labels[0, plane])
                                colour = 1 if colour == 3 else colour
                                if abs(meta['colours'][colour]) < .25:
                                    continue
                                design = torch.stack((torch.ones(256), wave1[0, plane].flatten(),
                                    wave2[0, plane].flatten()), dim=1).double()
                                condition = float(torch.linalg.cond(design))
                                inverse = torch.linalg.pinv(design)
                                target = clean_core[0, plane].double().flatten()
                                ideal = inverse@target
                                detail = target-target.mean()
                                measured = {}
                                for name, image in outputs.items():
                                    prediction = image[0, plane, 56:-56, 56:-56].double().flatten()
                                    fitted = inverse@prediction
                                    measured[name] = dict(component_gain=(fitted[1:]/ideal[1:]).tolist(),
                                        coherent_gain=float(1+((prediction-target)*detail).sum()/detail.square().sum()))
                                collected.append(dict(replicate=replicate, plane=plane, colour=colour,
                                    condition_number=condition, stable=condition <= 1e4, measured=measured))
                        result['records'].append(dict(variant=variant, seed=seed, serial=event['serial'],
                            source_domain=selection['source_domain'],seen_in_model_training=seen_in_training,
                            texture_serial=texture_identity(event),
                            family=event['family'], frequency=meta['frequency'], other_frequency=meta['other_frequency'],
                            target_sha256=event['target_sha256'], fits=collected))
                    del model
                    print('DIAGNOSED', variant, seed, flush=True)
            result['status'] = 'completed_training_multitone_diagnosis_not_qualification'
    except Exception as error:
        result.update(status='failed_diagnostic_execution',error=str(error))
        raise
    finally:
        result['memory'] = guard.stats()
        result['elapsed_seconds'] = time.monotonic()-started
        save(output/'result.json', result)
        save(root/'docs/validation/raw'/f'{identity}.json', result)
    print('COMPLETE', identity, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--data-plan', type=Path, required=True)
    parser.add_argument('--previous-campaign', type=Path, required=True)
    parser.add_argument('--campaign', type=Path, required=True)
    args = parser.parse_args()
    run(Path(__file__).resolve().parents[2], args.output, args.data_plan, args.previous_campaign, args.campaign)
