"""Separate trained-weight provenance from current inference arithmetic."""
from pathlib import Path

from diagnose_train_learnability import digest
from noisy_moment_unet import FORWARD_TOOLS
from run_noisy_moment_pilot import NOISE, runtime_versions


INFERENCE_TOOLS = FORWARD_TOOLS+('edge_protected_noise.py', 'prepare_closed_inference.py')


def check_inference(data, source_tools):
    if (data.get('runtime') != runtime_versions()
            or data.get('noise_levels') != [dict(a=a, b=b) for a, b in NOISE]):
        raise ValueError('Inference runtime or noise convention changed')
    if any(source_tools.get(n) != digest(Path(__file__).with_name(n)) for n in INFERENCE_TOOLS):
        raise ValueError('Qualified forward arithmetic changed; use its archived revision')
