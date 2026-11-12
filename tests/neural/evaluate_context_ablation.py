"""Paired 96/384px inference, fixed pixels, sigma, weights and metric thresholds."""
import argparse
from collections import defaultdict
from datetime import datetime,timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

import numpy as np
import torch

from camera_data import normalize_pair,validate_splits
from camera_unet import apply
from diagnose_train_learnability import digest,save
from evaluate_enriched_transfer import identities,load_pilot
from neural_transfer_metrics import core_mask,error_sums,gradient_sums,summarize


def crop_context(tile,xy):
    if tile.ndim!=4 or tuple(tile.shape)!=(1,3,384,384):raise ValueError('Expected 384px NCHW camera context')
    x,y=xy
    if any(not isinstance(v,int) or v<0 or v>288 or v%4 for v in (x,y)):
        raise ValueError('Crop must fit and preserve two-pool alignment phase')
    return tile[:,:,y:y+96,x:x+96]


def read_tile(capture,roi):
    descriptor=Path(capture['descriptor'])
    if digest(descriptor)!=capture['descriptor_sha256']:raise ValueError('Changed camera descriptor')
    native=json.loads(descriptor.read_text(encoding='utf-8'))
    pixels=descriptor.parent/capture['pixels']
    if pixels.resolve().parent!=descriptor.resolve().parent:raise ValueError('Invalid camera pixels path')
    if digest(pixels)!=capture['pixels_sha256']:raise ValueError('Changed camera pixels')
    tile=capture['tiles'][roi]
    if native['tiles'][roi]!=tile or tile['dtype']!='<f4' or tile['shape']!=[384,384,3]:
        raise ValueError('Camera tile contract mismatch')
    if tile['offset_bytes']<0:raise ValueError('Negative data offset')
    with pixels.open('rb') as handle:
        handle.seek(tile['offset_bytes']);data=handle.read(384*384*3*4)
    if len(data)!=384*384*3*4:raise ValueError('Truncated camera context')
    array=np.frombuffer(data,dtype='<f4').reshape(384,384,3).transpose(2,0,1).copy()
    if not np.isfinite(array).all():raise ValueError('Nonfinite camera context')
    return torch.from_numpy(array)[None]


def score(record,source,target,predicted,valid,thresholds):
    return dict(**record,full=error_sums(source,target,predicted,valid),
        core24=error_sums(source,target,predicted,core_mask(valid)),
        structure=gradient_sums(source,target,predicted,valid,thresholds),
        output_sha256=hashlib.sha256(predicted.contiguous().numpy().tobytes()).hexdigest())


def run(parent,baseline,output):
    root=Path(__file__).resolve().parents[2]
    parent,baseline,output=(p.resolve() for p in (parent,baseline,output))
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in (parent,output)):
        raise ValueError('Use dedicated neural build')
    parent_result=json.loads((parent/'result.json').read_text(encoding='utf-8'))
    previous=json.loads(baseline.read_text(encoding='utf-8'))
    if previous['status']!='completed_fixed_enriched_transfer':raise ValueError('Expected completed enriched evaluation')
    if digest(parent/'result.json')!=previous['freeze']['parent']['result_sha256']:
        raise ValueError('Different pilot result')
    for n in ('neural_transfer_metrics.py','camera_unet.py','camera_data.py'):
        if digest(Path(__file__).with_name(n))!=previous['freeze']['tools'][n]:raise ValueError('Changed metric/model code')
    pairs=json.loads((parent/'pairs.json').read_text(encoding='utf-8'))
    validate_splits(pairs['examples'])
    indices=[i for i,r in enumerate(pairs['examples']) if r['split']=='validation']
    records=[pairs['examples'][i] for i in indices]
    if len(records)!=186 or identities(records)!=identities(previous['records']):raise ValueError('Changed control identities')
    threads=min(4,max(1,(os.cpu_count() or 1)-2))
    torch.set_num_threads(threads);torch.set_num_interop_threads(1);torch.use_deterministic_algorithms(True)
    model,weights=load_pilot(parent,parent_result)
    with np.load(parent/'dataset.npz',allow_pickle=False) as data:
        arrays={n:torch.from_numpy(data[n][indices].copy()) for n in ('signal','target','valid','sigma')}
    captures={c['source_sha256']:c for c in parent_result['freeze']['sources']}
    groups=defaultdict(list)
    for i,r in enumerate(records):groups[(r['source_sha256'],r['roi'])].append(i)
    now=datetime.now(timezone.utc);campaign='neural-context-ablation-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        parent=dict(path=str(parent),result_sha256=digest(parent/'result.json')),
        baseline=dict(path=str(baseline),sha256=digest(baseline)),weights_sha256=digest(weights),
        amount=100,scale=previous['freeze']['scale'],arms=[96,384],
        thresholds=previous['freeze']['structure_thresholds_from_original_training_only'],
        sigma_policy='Same frozen per-tile Haar proxy for both arms, no re-estimation at larger context.',
        sampling_policy='Same 96px source/target/mask, no resampling; crop origins preserve modulo4 pooling phase.',
        tools={n:digest(Path(__file__).with_name(n)) for n in ('evaluate_context_ablation.py',
            'evaluate_enriched_transfer.py','neural_transfer_metrics.py','camera_unet.py','camera_data.py')})
    output.mkdir(parents=True,exist_ok=False);save(output/'freeze.json',freeze)
    arms={'patch96':[],'context384':[]};started=time.monotonic()
    result=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        freeze=freeze,freeze_sha256=digest(output/'freeze.json'),arms=arms,status='incomplete',
        contexts=[],threads_intra=threads,versions=dict(torch=torch.__version__,numpy=np.__version__),
        limits=['Previously consulted single composition, no independent final test or visual validation.',
            '384px input retains its own boundaries; some 96px crops have only 24px outer margin.',
            'Size/context ablation does not by itself identify a unique padding or learned-bias cause.',
            'Noisy reference means and correlation affect all detail and colour proxies.',
            'Camera opponents/green gradients are not perceptual colour or display luminance.',
            'No retraining, blending search, production changes or new raw decoding.'])
    try:
        with torch.inference_mode():
            for (source_hash,roi),members in groups.items():
                capture=captures[source_hash];tile=read_tile(capture,roi)
                sigma=arrays['sigma'][members[0]:members[0]+1,:,None,None]
                if any(not torch.equal(arrays['sigma'][i],arrays['sigma'][members[0]]) for i in members):
                    raise ValueError('Different conditioning within context')
                tile_hash=hashlib.sha256(tile.numpy().tobytes()).hexdigest()
                x,_,s=normalize_pair(tile,tile,sigma.expand_as(tile),freeze['scale'])
                large=apply(model,x,s,100)*freeze['scale']
                if not torch.equal(apply(model,x,s,0),x):raise ValueError('Zero not identity')
                result['contexts'].append(dict(source_sha256=source_hash,roi=roi,source=str(capture['source']),
                    descriptor_sha256=capture['descriptor_sha256'],pixels_sha256=capture['pixels_sha256'],
                    sigma=sigma.reshape(3).tolist(),context_chw_sha256=tile_hash))
                for i in members:
                    record=records[i]
                    source,target,valid=(arrays[n][i:i+1] for n in ('signal','target','valid'))
                    if not torch.equal(crop_context(tile,record['patch_xy']),source):raise ValueError('Context source pixels differ')
                    for array,key in ((source,'content_sha256'),(target,'target_sha256')):
                        if hashlib.sha256(array.numpy().tobytes()).hexdigest()!=record[key]:raise ValueError('Changed control pixels')
                    small_x,_,small_s=normalize_pair(source,target,sigma.expand_as(source),freeze['scale'])
                    small=apply(model,small_x,small_s,100)*freeze['scale']
                    arms['patch96'].append(score(record,source,target,small,valid,freeze['thresholds']))
                    arms['context384'].append(score(record,source,target,crop_context(large,record['patch_xy']),valid,freeze['thresholds']))
                if hashlib.sha256(tile.numpy().tobytes()).hexdigest()!=tile_hash:raise ValueError('Source context modified')
        result['summary']={n:summarize(rows) for n,rows in arms.items()}
        result['by_iso']={n:{str(iso):summarize([r for r in rows if r['iso']==iso])
            for iso in sorted({r['iso'] for r in rows})} for n,rows in arms.items()}
        for region in ('full','core24'):
            for metric in ('mse_ratio','opponent_mse_ratio'):
                if not np.isclose(result['summary']['patch96'][region][metric],previous['summary'][region][metric],rtol=1e-6,atol=1e-9):
                    raise ValueError('Replayed 96px arm differs from previous evaluation')
        if digest(weights)!=freeze['weights_sha256']:raise ValueError('Model changed')
        result['status']='completed_fixed_context_ablation'
    except Exception as error:
        result['error']=str(error);raise
    finally:
        result['elapsed_seconds']=time.monotonic()-started
        save(output/'result.json',result);save(root/'docs/validation/raw'/f'{campaign}.json',result)
    print(json.dumps(result['summary'],indent=2),flush=True)
    print('COMPLETE '+campaign,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('parent','baseline','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();run(a.parent,a.baseline,a.output)
