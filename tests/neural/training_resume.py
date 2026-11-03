"""CPU-only atomic technical checkpoints; never select a model by its score."""
import math
import os
from pathlib import Path
import tempfile

import torch

from diagnose_train_learnability import digest


def validate_state(state, context):
    if state.get('schema_version') != 1 or state.get('context') != context:
        raise ValueError('Training checkpoint context differs')
    step, losses, counts = (state[k] for k in ('completed_step', 'losses', 'exposure_counts'))
    if (isinstance(step, bool) or not isinstance(step, int) or not 0 <= step <= context['config']['steps']
            or not isinstance(losses, list) or len(losses) != step
            or any(not isinstance(v, (int, float)) or not math.isfinite(v) or v < 0 for v in losses)
            or not isinstance(counts, dict)
            or any(not isinstance(k, str) or isinstance(v, bool) or not isinstance(v, int) or v <= 0 for k, v in counts.items())
            or sum(counts.values()) != step*context['config']['accumulation']):
        raise ValueError('Inconsistent completed-step training counters')
    rng = state['torch_cpu_rng']
    if (not isinstance(rng, torch.Tensor) or rng.dtype != torch.uint8 or rng.device.type != 'cpu'
            or rng.shape != torch.get_rng_state().shape):
        raise ValueError('Valid CPU RNG state required')
    def finite(value):
        if isinstance(value, torch.Tensor):
            return value.device.type == 'cpu' and bool(torch.isfinite(value).all())
        if isinstance(value, dict):
            return all(finite(v) for v in value.values())
        if isinstance(value, (list, tuple)):
            return all(finite(v) for v in value)
        if isinstance(value, float):
            return math.isfinite(value)
        return True
    if not finite(state['model_state']) or not finite(state['optimizer_state']):
        raise ValueError('Finite CPU model and optimizer state required')


def metadata(path, step):
    return dict(path=str(path.resolve()), sha256=digest(path), bytes=path.stat().st_size,
        completed_step=step)


def save_checkpoint(path, model, optimizer, context, step, losses, counts):
    """Call only after a full optimizer update; atomic replacement keeps old file."""
    path = Path(path)
    state = dict(schema_version=1, context=context, completed_step=step,
        losses=losses, exposure_counts=counts, model_state=model.state_dict(),
        optimizer_state=optimizer.state_dict(), torch_cpu_rng=torch.get_rng_state())
    validate_state(state, context)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.training-', suffix='.tmp', delete=False) as handle:
            temporary = Path(handle.name)
            torch.save(state, handle)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return metadata(path, step)


def restore_checkpoint(path, model, optimizer, context):
    state = torch.load(path, map_location='cpu', weights_only=True)
    validate_state(state, context)
    model.load_state_dict(state['model_state'], strict=True)
    optimizer.load_state_dict(state['optimizer_state'])
    torch.set_rng_state(state['torch_cpu_rng'])
    return state['completed_step'], state['losses'], state['exposure_counts'], metadata(Path(path), state['completed_step'])
