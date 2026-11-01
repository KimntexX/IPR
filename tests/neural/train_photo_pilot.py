"""Fixed first photographic U-Net pilot; no tuning on the validation scene.

An exploratory two-composition benchmark, not a production model or a test of
generalization to arbitrary photos. Targets are qualified burst averages.
"""
import argparse
from datetime import datetime,timezone
import hashlib
import json
import os
from pathlib import Path
import time

import numpy as np
import torch

from camera_data import normalize_pair,masked_mse,validate_splits
from camera_unet import CameraUNet,apply


def digest(path):
    value=hashlib.sha256()
    with Path(path).open('rb') as handle:
        for part in iter(lambda:handle.read(1024*1024),b''):value.update(part)
    return value.hexdigest()


def run(work):
    root=Path(__file__).resolve().parents[2]
    work=work.resolve()
    if not work.is_relative_to((root/'build-patch_neural_restoration').resolve()):
        raise ValueError('Use the dedicated neural build')
    freeze=json.loads((work/'freeze.json').read_text(encoding='utf-8'))
    pairs=json.loads((work/'pairs.json').read_text(encoding='utf-8'))
    if pairs['status']!='qualified_for_exploratory_pilot':
        raise ValueError('Unqualified data')
    if digest(work/'freeze.json')!=pairs['freeze_sha256'] or digest(work/'dataset.npz')!=pairs['dataset']['sha256']:
        raise ValueError('Changed protocol or dataset')
    if (work/'training.json').exists() or (work/'pilot-weights.pt').exists():
        raise ValueError('Training output already exists')
    counts=validate_splits(pairs['examples'])
    config=freeze['training']
    threads=min(4,max(1,(os.cpu_count() or 1)-2))
    torch.set_num_threads(threads);torch.set_num_interop_threads(1)
    torch.manual_seed(config['seed']);torch.use_deterministic_algorithms(True)
    with np.load(work/'dataset.npz',allow_pickle=False) as data:
        signal=torch.from_numpy(data['signal'])
        target=torch.from_numpy(data['target'])
        valid=torch.from_numpy(data['valid'])
        sigmas=torch.from_numpy(data['sigma'])
    if len(signal)!=len(pairs['examples']) or valid.dtype!=torch.bool:
        raise ValueError('Invalid example layout')
    for index,example in enumerate(pairs['examples']):
        if hashlib.sha256(signal[index].numpy().tobytes()).hexdigest()!=example['content_sha256']:
            raise ValueError('Input example identity mismatch')
        if hashlib.sha256(target[index].numpy().tobytes()).hexdigest()!=example['target_sha256']:
            raise ValueError('Reference example identity mismatch')
    training=torch.tensor([i for i,r in enumerate(pairs['examples']) if r['split']=='train'])
    if not len(training) or not counts['validation'] or counts['test']!=0:
        raise ValueError('This pilot requires separate train/validation compositions and no invented test set')
    model=CameraUNet(width=config['width'])
    optimizer=torch.optim.Adam(model.parameters(),lr=config['learning_rate'])
    started=time.monotonic();losses=[]
    for step in range(config['steps']):
        indices=training[torch.randint(len(training),(config['batch'],))]
        x,y,sigma=normalize_pair(signal[indices],target[indices],
            sigmas[indices,:,None,None].expand_as(signal[indices]),config['scale'])
        model.train();optimizer.zero_grad(set_to_none=True)
        loss=masked_mse(model(x,sigma),y,valid[indices])
        loss.backward()
        if not all(p.grad is None or torch.isfinite(p.grad).all() for p in model.parameters()):
            raise ValueError('Nonfinite gradient')
        optimizer.step();losses.append(float(loss.item()))
        if (step+1)%32==0:print(f'TRAIN {step+1}/{config["steps"]} loss={loss.item():.8g}',flush=True)
    model.eval();metrics=[]
    for i,example in enumerate(pairs['examples']):
        source=signal[i:i+1];reference=target[i:i+1];mask=valid[i:i+1]
        sigma=sigmas[i:i+1,:,None,None].expand_as(source)
        x,y,s=normalize_pair(source,reference,sigma,config['scale'])
        output=apply(model,x,s,100)*config['scale']
        if not torch.equal(apply(model,x,s,0),x):raise ValueError('Zero is not identity')
        baseline=masked_mse(source,reference,mask).item()
        error=masked_mse(output,reference,mask).item()
        bias=[float((output-reference)[:,c][mask[:,0]].mean()) for c in range(3)]
        metrics.append(dict(id=example['id'],split=example['split'],source=example['source'],
            iso=example['iso'],roi=example['roi'],valid_channel_samples=int(mask.sum())*3,
            input_mse=baseline,output_mse=error,mse_ratio=error/baseline if baseline else None,
            channel_bias=bias,output_min=float(output.min()),output_max=float(output.max())))
    summaries={}
    for split in ('train','validation'):
        rows=[r for r in metrics if r['split']==split]
        count=sum(r['valid_channel_samples'] for r in rows)
        baseline=sum(r['input_mse']*r['valid_channel_samples'] for r in rows)/count
        error=sum(r['output_mse']*r['valid_channel_samples'] for r in rows)/count
        summaries[split]=dict(patches=len(rows),input_mse=baseline,output_mse=error,
            mse_ratio=error/baseline,worst_patch_mse_ratio=max(r['mse_ratio'] for r in rows))
    torch.save(dict(state_dict=model.state_dict(),width=config['width'],
        domain='signed balanced camera RGB, scale2, single-image Haar sigma proxy',
        freeze_sha256=digest(work/'freeze.json'),pairs_sha256=digest(work/'pairs.json')),
        work/'pilot-weights.pt')
    result=dict(schema_version=1,data_version=1,campaign_id=freeze['campaign_id']+'-training',
        measured_at_utc=datetime.now(timezone.utc).isoformat(),freeze_sha256=digest(work/'freeze.json'),
        pairs_sha256=digest(work/'pairs.json'),config=config,split_counts=counts,
        sources={p.name:digest(p) for p in (Path(__file__),Path(__file__).with_name('camera_unet.py'),
                 Path(__file__).with_name('camera_data.py'))},
        versions=dict(numpy=np.__version__,torch=torch.__version__),device='cpu',
        cpu_logical_available=os.cpu_count(),threads_intra=threads,threads_inter=1,
        elapsed_seconds=time.monotonic()-started,losses=losses,records=metrics,summary=summaries,
        weights_sha256=digest(work/'pilot-weights.pt'),
        verdict='exploratory_validation_mse_gain' if summaries['validation']['mse_ratio']<1 else 'no_validation_mse_gain',
        limits=pairs['limits']+['MSE is measured against a noisy aligned burst reference, not an absolute clean truth.',
            'No independent third composition, Lightroom equivalence, visual validation or production integration.',
            '256 fixed steps; no validation-driven checkpoint or parameter selection.',
            'CPU-only training; no GPU, full-frame inference, peak-memory or deployment qualification.'])
    with (work/'training.json').open('x',encoding='utf-8') as handle:
        json.dump(result,handle,indent=2,allow_nan=False)
    print(json.dumps(summaries,indent=2),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work',type=Path,required=True)
    run(parser.parse_args().work)
