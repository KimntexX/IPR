"""Audit spatial noise on already-known held-out flat pairs; no variance refit."""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import numpy as np
from prepare_next_training import read,write,digest
from physical_dark_audit import statistics


def run(root,output):
    from memory_budget import MemoryGuard
    parent=root/'docs/validation/raw/neural-sensor-noise-v001-20261105T173608Z.json'
    data=read(parent)
    eligible={}
    for fit in data['fits']:
        for row in fit['fit'].get('heldout',[]):
            eligible[(row['pair'],row['roi'],fit['plane'])]=dict(iso=fit['iso'],
                signal_dn=row['signal_dn'],predicted_variance_dn2=row['predicted_variance_dn2'])
    frames=data['frames']
    output.mkdir(exist_ok=False)
    result=dict(schema_version=1,status='started',started_at_utc=datetime.now(timezone.utc).isoformat(),
        parent=dict(path=str(parent),sha256=digest(parent)),pairs=[],
        tools={n:digest(Path(__file__).with_name(n)) for n in ('physical_light_audit.py','physical_dark_audit.py')},
        limits=['Old variance-fit heldout observations are now known, not a fresh final reserve',
                'Use exactly prior eligible heldout tuples, no new score-selected ROI filter',
                'Mean drift removed only for covariance/PSD; recorded separately; no image registration',
                'Differences cancel common structure and do not identify a causal sensor/codec mechanism'])
    guard=MemoryGuard(hard_bytes=2*1024**3,soft_bytes=1280*1024**2,system_fraction=.8)
    try:
        with guard:
            for pair in data['pairs']:
                keys=[k for k in eligible if k[0]==pair['index']]
                if not keys:continue
                guard.check()
                arrays=[]
                # Frozen parent pairs index their own frame array.
                for entry in pair['frames']:
                    if isinstance(entry,bool) or not isinstance(entry,int) or not 0<=entry<len(frames):
                        raise ValueError('Invalid frozen frame index')
                    frame=frames[entry]
                    path=root/frame['cached_tiles']
                    if digest(path)!=frame['tiles_sha256']:raise ValueError('Frozen DN pixels changed')
                    arrays.append(np.load(path,allow_pickle=False))
                metrics=statistics(np.stack(arrays))
                selected=[]
                by_location={(r['roi'],r['plane']):r for r in metrics['rows']}
                for _,roi,plane in keys:
                    row=dict(by_location[(roi,plane)],**eligible[(pair['index'],roi,plane)])
                    delta=arrays[0][roi,plane].astype(np.float64)-arrays[1][roi,plane].astype(np.float64)
                    row['centered_pair_variance_dn2']=float(delta.var(ddof=1)/2)
                    selected.append(row)
                result['pairs'].append(dict(index=pair['index'],iso=pair['iso'],rows=selected))
            result['status']='completed_known_light_spectral_diagnostics_not_full_physical_qualification'
    except Exception as error:
        result.update(status='stopped',error=str(error));raise
    finally:
        result.update(finished_at_utc=datetime.now(timezone.utc).isoformat(),memory=guard.stats())
        write(output/'progress.json',result)
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-physical-light-audit-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,allow_nan=False,separators=(',',':'))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();run(Path(__file__).resolve().parents[2],a.output)
