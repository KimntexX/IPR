"""Report pair-level nuisance effects, retaining all raw diagnostics and splits."""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import numpy as np
from prepare_next_training import digest,read,write
from physical_pair_drift import project
from physical_replay_probe import metrics


def run(root,output):
    from memory_budget import MemoryGuard
    parent=root/'docs/validation/raw/neural-sensor-noise-v001-20261105T173608Z.json'
    probe=root/'build-patch_neural_restoration/physical-replay-probe-v001/progress.json'
    bank,previous=read(parent),read(probe)
    previous_rows={(r['pair'],r['roi'],r['plane']):r for r in previous['rows']}
    output.mkdir(exist_ok=False)
    result=dict(schema_version=1,status='started',started_at_utc=datetime.now(timezone.utc).isoformat(),
        parents=[dict(path=str(p),sha256=digest(p)) for p in (parent,probe)],pairs=[],
        tools={n:digest(Path(__file__).with_name(n)) for n in ('diagnose_physical_pair_drift.py','physical_pair_drift.py','physical_replay_probe.py')},
        protocol='One shared displacement and four plane gains/offsets per pair; all original gates/splits retained; no camera coefficient refit',
        limits=['Diagnostic only, no reference replacement or RAW correction',
                'Projection may remove real low-dimensional sensor variation; requires null/power validation before promotion',
                'First-order displacement model is not exact image registration'])
    write(output/'freeze.json',result)
    guard=MemoryGuard(hard_bytes=2*1024**3,soft_bytes=1536*1024**2,system_fraction=.8)
    try:
        with guard:
            for pair in bank['pairs']:
                if pair['kind']!='light' or pair['iso'] not in (100,3200,6400,16000,51200):continue
                fields=[r for r in bank['measurements'] if r['pair']==pair['index'] and r.get('fit_gate',{}).get('eligible')]
                if not fields:continue
                guard.check();arrays=[]
                for index in pair['frames']:
                    frame=bank['frames'][index];path=root/frame['cached_tiles']
                    if digest(path)!=frame['tiles_sha256']:raise ValueError('Frozen CFA tiles changed')
                    arrays.append(np.load(path,allow_pickle=False).astype(np.float64))
                mask=np.zeros(arrays[0].shape[:2],bool)
                for field in fields:mask[field['roi'],field['plane']]=True
                delta=arrays[0]-arrays[1]
                residual,info=project((arrays[0]+arrays[1])*.5,delta,mask)
                rows=[]
                for field in fields:
                    roi,plane=field['roi'],field['plane']
                    before=metrics(delta[roi,plane]);after=metrics(residual[roi,plane])
                    row=dict(roi=roi,plane=plane,signal_dn=field['fit_gate']['signal_dn'],
                             split=field['fit_gate']['split'],before=before,after=after)
                    prior=previous_rows.get((pair['index'],roi,plane))
                    if prior is not None:
                        model=prior['simulated'];row['unchanged_model']=model
                        row['raw_lag_error']=max(abs(model[k]-before[k]) for k in ('lag_x1','lag_y1'))
                        row['residual_lag_error']=max(abs(model[k]-after[k]) for k in ('lag_x1','lag_y1'))
                        row['raw_psd_error']=abs(model['low_high_psd_ratio']/before['low_high_psd_ratio']-1)
                        row['residual_psd_error']=abs(model['low_high_psd_ratio']/after['low_high_psd_ratio']-1)
                    rows.append(row)
                result['pairs'].append(dict(pair=pair['index'],iso=pair['iso'],projection=info,rows=rows))
                write(output/'progress.json',result)
            result['status']='completed_pair_drift_diagnostic_not_promoted_measurement'
    except Exception as error:result.update(status='stopped',error=str(error));raise
    finally:
        result.update(finished_at_utc=datetime.now(timezone.utc).isoformat(),memory=guard.stats())
        write(output/'progress.json',result)
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-physical-pair-drift-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,allow_nan=False,separators=(',',':'))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();run(Path(__file__).resolve().parents[2],a.output)
