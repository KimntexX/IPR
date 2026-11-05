"""Atomic streaming CUDA checkpoints; save only between complete optimizer updates."""
import hashlib
import json
import os
from pathlib import Path
import tempfile
import torch


def fingerprint(value):
    digest=hashlib.sha256()
    def visit(item):
        if isinstance(item,torch.Tensor):
            cpu=item.detach().cpu().contiguous()
            digest.update(str((cpu.dtype,tuple(cpu.shape))).encode());digest.update(cpu.numpy().tobytes())
        elif isinstance(item,dict):
            for key in sorted(item,key=str):digest.update(repr(key).encode());visit(item[key])
        elif isinstance(item,(tuple,list)):
            for element in item:visit(element)
        else:digest.update(json.dumps(item,sort_keys=True).encode())
    visit(value);return digest.hexdigest()


def save(path,model,optimizer,context,step,losses):
    path=Path(path)
    if any(p.grad is not None for p in model.parameters()):raise ValueError('Checkpoint only after complete update and gradient clearing')
    if not isinstance(step,int) or isinstance(step,bool) or step<0 or len(losses)!=step:raise ValueError('Invalid checkpoint position')
    torch.cuda.synchronize()
    state=dict(schema_version=1,context=context,completed_step=step,noise_serial=step*4,losses=losses,
        model_state=model.state_dict(),optimizer_state=optimizer.state_dict(),
        cpu_rng=torch.get_rng_state(),cuda_rng=torch.cuda.get_rng_state_all())
    temporary=None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent,suffix='.tmp',delete=False) as stream:
            temporary=Path(stream.name)
            # torch.save streams non-CPU storages through CPU individually; do not clone the entire Adam state.
            torch.save(state,stream);stream.flush();os.fsync(stream.fileno())
        os.replace(temporary,path)
    finally:
        if temporary is not None:temporary.unlink(missing_ok=True)


def restore(path,model,optimizer,context):
    state=torch.load(path,weights_only=True,map_location='cpu')
    step=state.get('completed_step')
    if (state.get('schema_version')!=1 or state.get('context')!=context or not isinstance(step,int)
            or isinstance(step,bool) or step<0 or len(state['losses'])!=step or state['noise_serial']!=step*4):
        raise ValueError('Different checkpoint contract or position')
    model.load_state_dict(state['model_state'],strict=True);optimizer.load_state_dict(state['optimizer_state'])
    torch.set_rng_state(state['cpu_rng']);torch.cuda.set_rng_state_all(state['cuda_rng'])
    return step,state['losses']
