"""Discarded full-context CUDA checkpoint/replay resource qualification."""
from datetime import datetime,timezone
import gc
import json
import os
from pathlib import Path
import time
os.environ.setdefault('CUBLAS_WORKSPACE_CONFIG',':4096:8')
from campaign_artifacts import sha256
from memory_budget import MemoryGuard


def run():
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    output=build/'dru-cfa-checkpoint-v001';output.mkdir(exist_ok=False)
    result=dict(schema_version=1,status='started',measured_at_utc=datetime.now(timezone.utc).isoformat(),
        scope='Engineering checkpoint only, synthetic fixture, no trained candidate or photographic benefit claimed.',
        tools={n:sha256(Path(__file__).with_name(n)) for n in ('dru_checkpoint.py','dru_cfa.py','probe_dru_checkpoint.py')})
    guard=MemoryGuard(hard_bytes=6*1024**3,soft_bytes=4608*1024**2,system_fraction=.92);started=time.monotonic()
    try:
        with guard:
            import torch
            from dru_cfa import create
            from dru_checkpoint import save,restore,fingerprint
            torch.set_num_threads(4);torch.set_num_interop_threads(1)
            if not torch.cuda.is_available() or torch.cuda.mem_get_info()[0]<4*1024**3:raise RuntimeError('GPU busy or absent')
            torch.cuda.set_per_process_memory_fraction(.5);torch.use_deterministic_algorithms(True)
            torch.backends.cuda.matmul.allow_tf32=False;torch.backends.cudnn.allow_tf32=False
            torch.backends.cudnn.benchmark=False;torch.backends.cudnn.deterministic=True
            context=dict(seed=9101,torch_version=str(torch.__version__),packed_side=512,accumulation=4,learning_rate=.0001,
                tools=result['tools'])
            model,provenance=create(root,9101);model=model.cuda();result['architecture']=provenance
            optimizer=torch.optim.Adam(model.parameters(),lr=.0001)
            target=torch.full((1,4,512,512),.15,device='cuda');sigma=torch.full_like(target,.01);tags=torch.tensor([[0,1,3,2]],device='cuda')
            values=target+torch.randn(target.shape,generator=torch.Generator().manual_seed(901)).cuda()*.01
            def update(model,optimizer):
                guard.check();optimizer.zero_grad(set_to_none=True);total=0.
                for _ in range(4):
                    prediction=model.correct_observations(values,sigma,tags)
                    loss=(prediction[:,:,192:320,192:320]-target[:,:,192:320,192:320]).square().mean()/4
                    loss.backward();total+=float(loss.detach());del prediction,loss
                torch.nn.utils.clip_grad_norm_(model.parameters(),1.,error_if_nonfinite=True);optimizer.step();optimizer.zero_grad(set_to_none=True)
                torch.cuda.synchronize();return total
            first=update(model,optimizer);checkpoint=output/'resume.pt'
            save(checkpoint,model,optimizer,context,1,[first]);result['checkpoint_bytes']=checkpoint.stat().st_size
            result['checkpoint_sha256']=sha256(checkpoint);print('CHECKPOINT SAVED',flush=True)
            reference_loss=update(model,optimizer)
            reference=fingerprint(dict(model=model.state_dict(),optimizer=optimizer.state_dict()))
            del model,optimizer;gc.collect();torch.cuda.empty_cache();guard.check()
            model,_=create(root,9101);model=model.cuda();optimizer=torch.optim.Adam(model.parameters(),lr=.0001)
            step,losses=restore(checkpoint,model,optimizer,context);gc.collect();torch.cuda.empty_cache();guard.check()
            resumed_loss=update(model,optimizer)
            actual=fingerprint(dict(model=model.state_dict(),optimizer=optimizer.state_dict()))
            result.update(resumed_from_step=step,model_and_adam_bit_exact=actual==reference,
                next_loss_bit_exact=reference_loss==resumed_loss,reference_loss=reference_loss,resumed_loss=resumed_loss,
                peak_cuda_allocated_bytes=torch.cuda.max_memory_allocated())
            if actual!=reference or reference_loss!=resumed_loss:raise ValueError('Full architecture resume changed model/Adam trajectory')
            result['status']='completed_dru_checkpoint_replay_not_quality_qualification'
            del model,optimizer;gc.collect();torch.cuda.empty_cache()
    except Exception as error:
        result.update(status='stopped_on_error',error=str(error));raise
    finally:
        result.update(elapsed_seconds=time.monotonic()-started,memory=guard.stats())
        with (output/'result.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-dru-checkpoint-v001-{stamp}.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)


if __name__=='__main__':run()
