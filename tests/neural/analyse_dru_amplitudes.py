"""Summarize paired transfer and replay fixed blur witnesses without fitting a model."""
import argparse
from collections import defaultdict
from datetime import datetime,timezone
import json
import math
from pathlib import Path
import statistics

from campaign_artifacts import sha256


def summarize(records):
    buckets=defaultdict(list)
    for row in records:
        if sorted(row['site_colours'])!=list(range(4)):raise ValueError('Invalid physical colour mapping')
        for colour in range(4):
            site=row['site_colours'].index(colour)
            corrected=row['corrected'][site];observed=row['observed'][site]
            values=(corrected['sine_gain'],observed['sine_gain'],corrected['mse'],observed['mse'])
            if not all(math.isfinite(v) for v in values) or values[2]<0 or values[3]<=0:raise ValueError('Invalid transfer or MSE')
            key=tuple(row[k] for k in ('arm','seed','step','amplitude','frequency','chromatic','noise_index'))+(colour,)
            buckets[key].append(values)
    names=('arm','seed','step','amplitude','frequency','chromatic','noise_index','colour')
    return [dict(zip(names,key),fields=len(rows),mean_gain=statistics.mean(v[0] for v in rows),
        observed_mean_gain=statistics.mean(v[1] for v in rows),
        mean_gain_change_from_observed=statistics.mean(v[0]-v[1] for v in rows),
        mse_ratio=sum(v[2] for v in rows)/sum(v[3] for v in rows)) for key,rows in sorted(buckets.items())]


def run(source=None,output=None):
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    source=build/'dru-amplitude-diagnosis-v001/result.json' if source is None else source.resolve()
    output=build/'dru-amplitude-analysis-v001/result.json' if output is None else output.resolve()
    if not all(p.resolve().is_relative_to(build.resolve()) for p in (source,output)):raise ValueError('Owned analysis paths required')
    measured=json.loads(source.read_text(encoding='utf-8'));plan=measured['plan']
    if measured['status']!='completed_amplitude_diagnostic_not_qualification':raise ValueError('Completed sweep required')
    def identity(r):return tuple(r[k] for k in ('arm','seed','step','amplitude','case_index','noise_index'))
    expected={(m['arm'],m['seed'],m['step'],a,f['case_index'],f['noise_index'])
        for m in plan['models'] for a in plan['amplitudes'] for f in plan['base_fields']}
    actual=[identity(r) for r in measured['records']]
    if not expected or len(actual)!=len(expected) or len(set(actual))!=len(expected) or set(actual)!=expected:raise ValueError('Missing or duplicated observations')
    paired={}
    for row in measured['records']:
        k=tuple(row[n] for n in ('amplitude','case_index','noise_index'))
        hashes=tuple(row[n] for n in ('truth_sha256','observation_sha256','sigma_sha256'))
        if k in paired and paired[k]!=hashes:raise ValueError('Unpaired observations across models')
        paired[k]=hashes
    result=dict(schema_version=1,status='started',measured_at_utc=datetime.now(timezone.utc).isoformat(),
        source_sha256=sha256(source),groups=summarize(measured['records']),witnesses=[],
        tools={n:sha256(Path(__file__).with_name(n)) for n in ('analyse_dru_amplitudes.py','dru_audit_math.py','confirm_fine_noise.py','sensor_transfer_math.py')},
        limits=plan['limits']+['Gain change is corrected minus observed on identical noise, not a ratio of noisy estimates.',
            'Fixed 3x3 CFA-plane blur witnesses at blends .25/.5/1 reuse the previous audit; no coefficient selected.',
            'Witness loss uses the training equation on the same 32x32 packed core as transfer measurement, not the full training dataset.',
            'A lower MSE with attenuation does not establish an objective implementation bug or prove the cause of the learned response.'])
    import torch
    from memory_budget import MemoryGuard
    from confirm_fine_noise import fine_signal,observation
    from synthetic_bayer_data import PATTERNS
    from dru_audit_math import loss_probe,fixed_blurs
    from sensor_transfer_math import site_response
    from run_synthetic_joint_pilot import tensor_hash
    guard=MemoryGuard(hard_bytes=2*1024**3,soft_bytes=1280*1024**2,system_fraction=.92)
    with guard,torch.inference_mode():
        torch.set_num_threads(4);torch.set_num_interop_threads(1)
        original=build/'curve-known-fine-v001/result.json'
        if sha256(original)!=plan['source_sha256']:raise ValueError('Known source changed')
        cases=json.loads(original.read_text(encoding='utf-8'))['plan']['cases']
        for amplitude in plan['amplitudes']:
            for field in plan['base_fields']:
                guard.check();case=dict(cases[field['case_index']],amplitude=amplitude)
                truth,wave,cosine=fine_signal(case);tags=torch.tensor([PATTERNS[field['phase_index']]])
                clean,values,sigma=observation(truth,tags,field)
                if paired[amplitude,field['case_index'],field['noise_index']]!=(tensor_hash(truth),tensor_hash(values),tensor_hash(sigma)):
                    raise ValueError('Witness observations changed')
                probe=loss_probe(values/2,clean/2,sigma/2,32)
                transfer={name:site_response(v,clean,wave,cosine) for name,v in fixed_blurs(values).items()}
                result['witnesses'].append(dict(amplitude=amplitude,frequency=case['frequency'],chromatic=case['chromatic'],
                    angle=case['angle'],noise_index=field['noise_index'],case_index=field['case_index'],
                    loss=probe,blurred_transfer=transfer))
    result.update(status='completed_amplitude_summary_and_objective_witness',memory=guard.stats())
    output.parent.mkdir(exist_ok=False)
    output.write_text(json.dumps(result,indent=2,allow_nan=False),encoding='utf-8')
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    canonical=root/'docs/validation/raw'/f'neural-dru-amplitude-analysis-v001-{stamp}.json'
    with canonical.open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)
    print(canonical,flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path);parser.add_argument('--output',type=Path)
    args=parser.parse_args();run(args.source,args.output)
