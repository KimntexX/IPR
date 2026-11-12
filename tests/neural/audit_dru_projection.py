"""Read-only attribution audit: paired controls and explicit objective blind spots."""
from datetime import datetime,timezone
import json
import math
from pathlib import Path
import statistics
import time
import torch
from torch.nn import functional as F
from campaign_artifacts import sha256
from memory_budget import MemoryGuard
from dru_structure_objective import structure_penalty


def gradients(image):
    return image[:,:,:,1:]-image[:,:,:,:-1],image[:,:,1:,:]-image[:,:,:-1,:]


def projection_rows(prediction,target):
    rows=[]
    for scale in (1,2,4):
        p=prediction if scale==1 else F.avg_pool2d(prediction,scale)
        t=target if scale==1 else F.avg_pool2d(target,scale)
        dx,dy=gradients(t);ex,ey=gradients(p-t)
        energy=dx.square().mean()+dy.square().mean()
        value=((ex*dx).mean()+(ey*dy).mean())/(energy+1e-8)
        rows.append(dict(scale=scale,energy=float(energy),epsilon_retention=float(energy/(energy+1e-8)),signed_projection=float(value)))
    return rows


def waves():
    y,x=torch.meshgrid(torch.arange(128,dtype=torch.float64),torch.arange(128,dtype=torch.float64),indexing='ij')
    specs=[(6.,.21,.03),(18.,.64,.015),(34.,1.02,.006),(52.,.37,.004)]
    return [a*torch.sin(2*math.pi*f*(x*math.cos(angle)+y*math.sin(angle))/128+.71*j)[None,None].expand(1,4,128,128).clone()
        for j,(f,angle,a) in enumerate(specs)],specs


def cancellation_witness():
    components,specs=waves();truth=.15+sum(components)
    matrix=torch.tensor([[projection_rows(component,truth)[s]['signed_projection']-
        projection_rows(torch.zeros_like(component),truth)[s]['signed_projection'] for component in components] for s in range(3)],dtype=torch.float64)
    # Four basis errors, three global constraints: exhibit a null direction, not a fitted denoiser.
    _,singular,vh=torch.linalg.svd(matrix,full_matrices=True)
    vector=vh[-1]
    if abs(float(vector[-1]))<1e-10:raise ValueError('Declared last-component normalization is degenerate')
    vector=vector*(-.2/vector[-1])
    error=sum(v*c for v,c in zip(vector,components));predicted=truth+error
    return dict(specifications=specs,coefficient_changes=vector.tolist(),singular_values=singular.tolist(),
        null_residual=(matrix@vector).tolist(),loss=float(structure_penalty(predicted,truth,0)),
        raw_mse=float(error.square().mean()),float32_loss=float(structure_penalty(predicted.float(),truth.float(),0)),
        scope='Constructed counterexample to sufficiency of three global projections; not evidence the learned network produces this error.')


def run():
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration';output=build/'dru-projection-audit-v001'
    output.mkdir(exist_ok=False)
    source=build/'dru-structure-amplitude-analysis-v001/result.json';measured=json.loads(source.read_text(encoding='utf-8'))
    if measured['status']!='completed_amplitude_summary_and_objective_witness':raise ValueError('Complete source required')
    result=dict(schema_version=1,status='started',measured_at_utc=datetime.now(timezone.utc).isoformat(),source_sha256=sha256(source),
        tools={n:sha256(Path(__file__).with_name(n)) for n in ('audit_dru_projection.py','dru_structure_objective.py')},
        effects=[],slices=[],masked_weak_feature=[],pure_wave_sensitivity=[],
        limits=['All measured controls are known development controls, not independent qualification.',
            'Constructed analytic witnesses can refute sufficiency, not establish the learned error mechanism.',
            'One noise replicate/phase and two initializations do not establish population confidence.',
            'No coefficient sweep, fitting, weights loading or production change.'])
    names=('seed','step','amplitude','frequency','chromatic','noise_index','colour')
    indexed={}
    for row in measured['groups']:
        k=(row['arm'],*(row[n] for n in names))
        if k in indexed:raise ValueError('Duplicate group')
        indexed[k]=row
    if len(indexed)!=1152:raise ValueError('Full two-arm grid required')
    for row in measured['groups']:
        if row['arm']!='structured':continue
        reference=indexed['reference',*(row[n] for n in names)]
        if row['fields']!=reference['fields'] or row['observed_mean_gain']!=reference['observed_mean_gain']:raise ValueError('Unpaired group')
        result['effects'].append(dict(**{n:row[n] for n in names},gain_delta=row['mean_gain']-reference['mean_gain'],
            absolute_gain_bias_delta=abs(row['mean_gain']-1)-abs(reference['mean_gain']-1),
            mse_ratio_delta=row['mse_ratio']-reference['mse_ratio']))
    for dimension in ('frequency','amplitude','chromatic','noise_index','colour'):
        for value in sorted({r[dimension] for r in result['effects']}):
            for seed in (71,73):
                rows=[r for r in result['effects'] if r[dimension]==value and r['seed']==seed and r['step']==256]
                result['slices'].append(dict(dimension=dimension,value=value,seed=seed,groups=len(rows),
                    mean_gain_bias_delta=statistics.mean(r['absolute_gain_bias_delta'] for r in rows),
                    mean_mse_ratio_delta=statistics.mean(r['mse_ratio_delta'] for r in rows),
                    groups_improving_both=sum(r['absolute_gain_bias_delta']<0 and r['mse_ratio_delta']<0 for r in rows),
                    groups_worsening_both=sum(r['absolute_gain_bias_delta']>0 and r['mse_ratio_delta']>0 for r in rows)))
    guard=MemoryGuard(hard_bytes=2*1024**3,soft_bytes=1280*1024**2,system_fraction=.92);started=time.monotonic()
    with guard,torch.inference_mode():
        torch.set_num_threads(4);torch.set_num_interop_threads(1)
        components,_=waves();weak=components[-1];truth=.15+weak
        y,x=torch.meshgrid(torch.arange(128,dtype=torch.float64),torch.arange(128,dtype=torch.float64),indexing='ij')
        backgrounds={'none':torch.zeros_like(weak),'coarse_wave':components[0],
            'edge':(.15*(x>63))[None,None].expand_as(weak),'three_waves':sum(components[:-1])}
        for name,background in backgrounds.items():
            guard.check();target=truth+background;predicted=target-.2*weak
            result['masked_weak_feature'].append(dict(background=name,weak_component_gain=.8,
                loss=float(structure_penalty(predicted,target,0)),by_scale=projection_rows(predicted,target)))
        for frequency in (6.,14.,26.):
            for amplitude in (.004,.015,.03):
                for angle in (.17,.63,1.07):
                    guard.check();signal=amplitude*torch.sin(2*math.pi*frequency*(2*x*math.cos(angle)+2*y*math.sin(angle))/128+1.17)
                    signal=signal[None,None].expand_as(weak);target=.15+signal;predicted=.15+.8*signal
                    result['pure_wave_sensitivity'].append(dict(frequency_raw_cycles_per128=frequency,amplitude_packed=amplitude,
                        angle=angle,loss=float(structure_penalty(predicted,target,0)),by_scale=projection_rows(predicted,target)))
        result['cancellation']=cancellation_witness()
        if time.monotonic()-started>120:raise TimeoutError('Two-minute CPU audit budget')
    result.update(status='completed_projection_attribution_audit',memory=guard.stats())
    (output/'result.json').write_text(json.dumps(result,indent=2,allow_nan=False),encoding='utf-8')
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    canonical=root/'docs/validation/raw'/f'neural-dru-projection-audit-v001-{stamp}.json'
    with canonical.open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)
    print(canonical,flush=True)


if __name__=='__main__':run()
