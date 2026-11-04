"""Read-only activation/gradient probes on already consulted development data."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path

import torch

from bayer_canonical import canonicalize
from clean_cfa_loss import clean_cfa_loss
from cfa_structure_loss import structure_loss
from context_data import extend_case, extend_observation, crop_centre
from diagnose_train_learnability import digest, save
from joint_bayer_unet import sample_rgb, bilinear
from memory_budget import MemoryGuard
from noisy_moment_unet import NoisyMomentUNet, load_reconstruction, FORWARD_TOOLS
from noisy_training_policy import configure_trainable
from reconstruction_loss import energy_balanced_loss
from run_noisy_moment_pilot import observation, NOISE
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS


def statistics(value):
    value = value.detach().float()
    return dict(rms=float(value.square().mean().sqrt()), mean=float(value.mean()),
        minimum=float(value.min()), maximum=float(value.max()),
        fraction_below_minus10=float((value < -10).float().mean()),
        fraction_abs_above3=float((value.abs() > 3).float().mean()))


def probe(model, values, sigma, labels, truth, border):
    activations, handles = {}, []
    def hook(name):
        def record(module, inputs, output):
            activations[name] = statistics(output)
        return record
    baseline = {n: p.detach().clone() for n, p in model.named_parameters()}
    try:
        for name, module in model.named_modules():
            if name.startswith(('noise_features.', 'noise_head')) and isinstance(module, (torch.nn.Conv2d, torch.nn.SiLU)):
                handles.append(module.register_forward_hook(hook(name)))
        model.zero_grad(set_to_none=True)
        image = model(values, sigma, labels)
        noisy_activations = dict(activations)
        reference = bilinear(values, labels)
        rgb = energy_balanced_loss(image, truth, reference, 2., border, 'tempered')
        clean = sample_rgb(truth, labels)
        anchored = model.correct_observations(clean, sigma, labels)
        anchor = clean_cfa_loss(anchored, clean, reference, truth, 2., border)
        noisy_corrected = model.correct_observations(values, sigma, labels)
        structure = structure_loss(noisy_corrected, clean, sigma, border//2)
        parameters = [p for p in model.parameters() if p.requires_grad]
        components = {}
        for name, term in (('rgb', rgb), ('anchor', anchor), ('structure', structure)):
            gradients = torch.autograd.grad(term, parameters, retain_graph=True, allow_unused=True)
            components[name] = torch.cat([(g if g is not None else torch.zeros_like(p)).detach().flatten()
                for p, g in zip(parameters, gradients)])
        component_norms = {n: float(v.norm()) for n, v in components.items()}
        alignments = {}
        for a, b in (('rgb', 'anchor'), ('rgb', 'structure'), ('anchor', 'structure')):
            denominator = component_norms[a]*component_norms[b]
            alignments[a+'_'+b] = float(torch.dot(components[a], components[b]))/denominator if denominator else None
        (rgb+anchor+structure).backward()
        gradients = {n: statistics(p.grad) for n, p in model.named_parameters()
            if p.requires_grad and p.grad is not None}
        with torch.no_grad():
            corrected = model.correct_observations(values, sigma, labels)
            c = border//2
            delta = (corrected-values)[:, :, c:-c, c:-c]
            error = (values-clean)[:, :, c:-c, c:-c]
            deviation = sigma[:, :, c:-c, c:-c]
            measures = dict(correction_over_sigma=float(delta.square().mean().sqrt()/deviation.square().mean().sqrt()),
                cfa_mse_ratio=float((delta+error).square().mean()/error.square().mean()),
                correction_noise_inner_product=float((delta*error).mean()),
                rgb_loss=float(rgb), clean_anchor_loss=float(anchor), structure_loss=float(structure))
        if any(not torch.equal(p, baseline[n]) for n, p in model.named_parameters()):
            raise ValueError('Diagnostic mutated weights')
        return dict(measures=measures, activations=noisy_activations, gradients=gradients,
            component_gradient_norms=component_norms, component_gradient_cosines=alignments)
    finally:
        for handle in handles:
            handle.remove()
        model.zero_grad(set_to_none=True)


def run(root, output, plan_path, campaign):
    base = root/'build-patch_neural_restoration'
    if not all(p.resolve().is_relative_to(base.resolve()) for p in (output, plan_path, campaign)):
        raise ValueError('Dedicated neural build paths required')
    plan = json.loads(plan_path.read_text(encoding='utf-8'))
    for name in FORWARD_TOOLS:
        if digest(Path(__file__).with_name(name)) != plan['tools'][name]:
            raise ValueError('Frozen forward arithmetic changed')
    result = dict(schema_version=1, measured_at_utc=datetime.now(timezone.utc).isoformat(),
        status='incomplete', plan_sha256=digest(plan_path),
        diagnostic_sha256=digest(Path(__file__)),
        protocol='Both initial/final seeds; first consulted example per mixed family, phase 0, two positive noises; no updates or reserved data.', models=[])
    lookup = {(r['case_index'], r['phase'], r['noise_index']): r for r in plan['observations']}
    cases, families = [], set()
    for index, case in enumerate(plan['mixed']):
        if case['family'] not in families:
            cases.append((index, case))
            families.add(case['family'])
    with MemoryGuard(**plan['memory_policy']) as guard:
        torch.set_num_threads(4)
        torch.set_num_interop_threads(1)
        torch.use_deterministic_algorithms(True)
        for seed in (71, 73):
            folder = campaign/f'seed{seed}-attempt01'
            proof = json.loads((folder/'result.json').read_text(encoding='utf-8'))
            if proof['plan_sha256'] != digest(plan_path) or proof['weights_sha256'] != digest(folder/'weights.pt'):
                raise ValueError('Checkpoint provenance differs')
            parent = next(p for p in plan['parents'] if p['seed'] == seed)
            parent_path = Path(parent['folder'])/'weights.pt'
            if digest(parent_path) != parent['weights_sha256']:
                raise ValueError('Parent changed')
            for stage in ('initial', 'final'):
                torch.manual_seed(seed)
                model = NoisyMomentUNet(8)
                if stage == 'initial':
                    load_reconstruction(model, torch.load(parent_path, weights_only=True)['state_dict'])
                else:
                    model.load_state_dict(torch.load(folder/'weights.pt', weights_only=True)['state_dict'], strict=True)
                configure_trainable(model, 'cfa_front')
                model.eval()
                measured = dict(seed=seed, stage=stage, weights_sha256=digest(folder/'weights.pt'),
                    weights={n: statistics(p) for n, p in model.named_parameters() if p.requires_grad}, rows=[])
                for index, case in cases:
                    expanded = extend_case(case, 320)
                    truth = crop_centre(expanded)
                    if tensor_hash(truth) != case['target_sha256']:
                        raise ValueError('Historical target changed')
                    tags = torch.tensor([PATTERNS[0]])
                    for level in (1, 2):
                        guard.check()
                        frozen = lookup[index, 0, level]
                        values, sigma = observation(truth, tags, level, frozen['noise_seed'])
                        if tensor_hash(values) != frozen['noisy_sha256'] or tensor_hash(sigma) != frozen['sigma_sha256']:
                            raise ValueError('Historical noise changed')
                        values, sigma = extend_observation(expanded, tags, values, sigma, *NOISE[level],
                            plan['context_policy']['validation_halo_seed_base']+frozen['noise_seed'])
                        values, sigma, labels, flags = canonicalize(values/2, sigma/2, tags)
                        # Phase zero is canonical; target therefore has no reflection.
                        if flags.any():
                            raise ValueError('Expected canonical phase')
                        row = probe(model, values, sigma, labels, expanded/2, 144)
                        row.update(case_index=index, family=case['family'], noise_index=level,
                            noisy_sha256=frozen['noisy_sha256'], target_sha256=case['target_sha256'])
                        measured['rows'].append(row)
                losses = proof['losses']
                measured['training_loss_blocks'] = [dict(start=i, mean=sum(losses[i:i+64])/len(losses[i:i+64]))
                    for i in range(0, len(losses), 64)]
                result['models'].append(measured)
                print('DIAGNOSED', seed, stage, len(measured['rows']), flush=True)
                del model
        result['memory'] = guard.stats()
    result['status'] = 'completed_read_only_consulted_development_diagnostic'
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        raise FileExistsError(output)
    save(output, result)
    print('SAVED', output, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--data-plan', type=Path, required=True)
    parser.add_argument('--campaign', type=Path, required=True)
    args = parser.parse_args()
    run(Path(__file__).resolve().parents[2], args.output, args.data_plan, args.campaign)
