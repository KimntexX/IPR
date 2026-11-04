"""Validate an immutable learned initializer before loading its owned model."""
import json
from pathlib import Path

import torch

from compare_reconstruction_objectives import confined
from diagnose_train_learnability import digest


def initialize_continuation(root, model, clean_state, ref):
    folder = Path(ref['folder'])
    confined(root, [folder])
    proof_path, weights_path = folder/'result.json', folder/'weights.pt'
    proof = json.loads(proof_path.read_text(encoding='utf-8'))
    if (digest(weights_path) != ref['weights_sha256'] or digest(proof_path) != ref['result_sha256']
            or proof['status'] != 'completed_synthetic_noise_pilot_not_photographic_qualification'
            or proof['training_progress']['completed_optimizer_steps'] != 2048
            or proof['pilot_gates_passed'] is not True or proof['seed'] != ref['seed']
            or proof['weights_sha256'] != ref['weights_sha256']):
        raise ValueError('Warm initialization provenance differs')
    checkpoint = torch.load(weights_path, weights_only=True, map_location='cpu')
    state = checkpoint['state_dict']
    if checkpoint['plan_sha256'] != proof['plan_sha256']:
        raise ValueError('Warm checkpoint plan differs')
    if any(n not in state or not torch.equal(state[n], value) for n, value in clean_state.items()):
        raise ValueError('Warm initialization changed frozen reconstruction')
    model.load_state_dict(state, strict=True)
