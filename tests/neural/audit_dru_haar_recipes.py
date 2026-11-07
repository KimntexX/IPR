"""Check all 4096 planned recipe exposures before a matched 1024-update study."""
from datetime import datetime,timezone
import json
from pathlib import Path
import time
import torch
from campaign_artifacts import sha256
from memory_budget import MemoryGuard
from load_control import YieldController
from dru_training_data import example,core_loss
from dru_comparison_policy import scenario_for
from dru_clean_variance import clean_sigma
from dru_haar_objective import noise_aware_haar_penalty


def gradient_stats(left,right):
    a=float(left.square().sum().sqrt());b=float(right.square().sum().sqrt())
    return dict(mse_norm=a,haar_norm=b,haar_to_mse=b/a if a else None,
        cosine=float((left*right).sum())/(a*b) if a*b else None)


def run():
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    output=build/'dru-haar-recipe-audit-v001';output.mkdir(exist_ok=False)
    original=build/'dru-data-comparison-plan-v001/reference.json';parent=json.loads(original.read_text(encoding='utf-8'))
    noise_path=Path(parent['noise_source'])
    if sha256(noise_path)!=parent['noise_sha256']:raise ValueError('Noise laws changed')
    scenes=json.loads(noise_path.read_text(encoding='utf-8'))['scenes']
    tools=('dru_training_data.py','dru_comparison_policy.py','sensor_curve_noise.py','bayer_canonical.py','context_data.py')
    for name in tools:
        if sha256(Path(__file__).with_name(name))!=parent['tools'][name]:raise ValueError('Original recipe changed: '+name)
    plan=dict(schema_version=1,created_at_utc=datetime.now(timezone.utc).isoformat(),serials=list(range(4096)),
        data_seed=71,data_arm='reference',source_sha256=sha256(original),noise_source=str(noise_path),noise_sha256=sha256(noise_path),
        tools={n:sha256(Path(__file__).with_name(n)) for n in (*tools,'audit_dru_haar_recipes.py','dru_clean_variance.py','dru_haar_objective.py')},
        policy='All four micro-exposures for each of 1024 updates; exact full-context generation, loss/gradients on the same128x128 packed core. No model weights.',
        limits=['Synthetic development recipes, no independent photographic validation.',
            'Clean-law sigma is loss-only privileged synthetic information, not a replacement inference input.',
            'Observed-signal sigma is statistically dependent on the same noise; compare rather than assume independence.',
            'Gradient norms are for output tensors, not network parameters or actual clipped Adam updates.'])
    (output/'plan.json').write_text(json.dumps(plan,indent=2)+'\n',encoding='utf-8')
    result=dict(status='started',plan=plan,records=[])
    guard=MemoryGuard(hard_bytes=2*1024**3,soft_bytes=1280*1024**2,system_fraction=.92)
    controller=YieldController(dict(yield_memory_fraction=.85,yield_external_cpu_fraction=.8,pressure_seconds=15))
    started=time.monotonic()
    try:
        with guard:
            torch.set_num_threads(4);torch.set_num_interop_threads(1);torch.use_deterministic_algorithms(True)
            for serial in plan['serials']:
                guard.check()
                if (output/'STOP').exists():raise InterruptedError('User recipe-audit stop')
                if controller.check():raise InterruptedError('External load pressure')
                if time.monotonic()-started>900:raise TimeoutError('15-minute recipe audit budget')
                packed,sigma,labels,target=example(serial,71,scenes)
                # Isolated contiguous core retains exactly the training loss samples.
                p=packed[:,:,192:320,192:320].clone().detach().requires_grad_()
                estimated=sigma[:,:,192:320,192:320].clone();clean=target[:,:,192:320,192:320].clone()
                identity=280000000+71*100000+serial
                scenario=scenario_for(serial,identity,scenes,'reference');known=clean_sigma(clean,labels,scenario)
                del packed,sigma,target
                mse=core_loss(p,clean,estimated,0);gm=torch.autograd.grad(mse,p)[0]
                losses={};gradients={}
                for name,sd in [('estimated',estimated),('known',known)]:
                    loss=noise_aware_haar_penalty(p,clean,sd,0);g=torch.autograd.grad(loss,p)[0]
                    if not torch.isfinite(loss) or not torch.isfinite(g).all():raise ValueError('Nonfinite Haar loss/gradient')
                    losses[name]=float(loss.detach());gradients[name]=gradient_stats(gm,g)
                centre=clean.mean(dim=(-2,-1),keepdim=True)
                gain=torch.tensor(.8,requires_grad=True)
                attenuation=noise_aware_haar_penalty(centre+gain*(clean-centre),clean,known,0)
                slope=float(torch.autograd.grad(attenuation,gain)[0])
                if not torch.isfinite(gm).all() or not torch.isfinite(mse) or not torch.isfinite(attenuation) or not torch.isfinite(torch.tensor(slope)):
                    raise ValueError('Nonfinite reference or attenuation gradient')
                variance=known.square();measured=estimated.square()
                positive=variance>0
                ratio=(measured[positive]/variance[positive]) if positive.any() else None
                result['records'].append(dict(serial=serial,family_index=serial%8,phase=labels[0].tolist(),
                    noise_scene=None if scenario is None else scenario.get('name'),
                    negative_target_fraction=float((clean<0).float().mean()),
                    known_variance_mean_by_plane=variance.mean(dim=(-2,-1)).flatten().tolist(),
                    estimated_variance_mean_by_plane=measured.mean(dim=(-2,-1)).flatten().tolist(),
                    variance_ratio_mean=float(ratio.mean()) if ratio is not None else None,
                    standardized_noise_mse=float(((p.detach()-clean).square()[positive]/variance[positive]).mean()) if positive.any() else None,
                    mse=float(mse.detach()),haar_losses=losses,gradients=gradients,
                    attenuation_loss=float(attenuation.detach()),attenuation_gain_slope=slope))
                if (serial+1)%128==0:
                    (output/'progress.json').write_text(json.dumps(dict(status='running',completed=serial+1,total=4096,elapsed_seconds=time.monotonic()-started)),encoding='utf-8')
                    print('RECIPES',serial+1,'/4096',flush=True)
            result['checks']=dict(all_exposures_complete=len(result['records'])==4096,
                attenuation_direction_nonpositive=all(r['attenuation_gain_slope']<=0 for r in result['records']),
                zero_noise_identity_exact=all(r['mse']==0 and r['haar_losses']==dict(estimated=0.,known=0.) for r in result['records'] if r['noise_scene'] is None))
            result['status']='completed_actual_recipe_haar_audit'
    except Exception as error:
        result.update(status='stopped_on_error_or_load',error=str(error));raise
    finally:
        result.update(elapsed_seconds=time.monotonic()-started,memory=guard.stats())
        (output/'result.json').write_text(json.dumps(result,indent=2,allow_nan=False),encoding='utf-8')
        (output/'progress.json').write_text(json.dumps(dict(status=result['status'],completed=len(result['records']),total=4096,error=result.get('error'))),encoding='utf-8')
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        canonical=root/'docs/validation/raw'/f'neural-dru-haar-recipe-audit-v001-{stamp}.json'
        with canonical.open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)
        print(canonical,flush=True)


if __name__=='__main__':run()
