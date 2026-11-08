"""Prepare new input proxies in the verified archived IPR sensor domain."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import numpy as np

from prepare_next_training import read, write, digest


def native_patch(dn, pattern, black, divisor):
    if dn.ndim != 2 or min(dn.shape)<2 or any(n%2 for n in dn.shape) or dn.dtype!=np.uint16:
        raise ValueError('Even uint16 sensor CFA required')
    pattern=np.asarray(pattern)
    black=np.asarray(black,dtype=np.float64)
    if (pattern.shape!=(2,2) or sorted(pattern.ravel().tolist())!=[0,1,2,3]
            or not np.isfinite(divisor) or divisor<=0 or black.shape!=(4,) or not np.isfinite(black).all()):
        raise ValueError('Distinct physical colours and positive divisor required')
    red=np.argwhere(pattern==0)[0]
    blue=np.argwhere(pattern==2)[0]
    if not np.array_equal(blue,1-red):raise ValueError('Opposite red and blue required')
    values=dn.astype(np.float32)
    for y in range(2):
        for x in range(2):
            values[y::2,x::2]-=np.float32(black[int(pattern[y,x])])
    values*=np.float32(1/divisor)
    labels=pattern.copy()
    if red[0]:values=values[::-1,:];labels=labels[::-1,:]
    if red[1]:values=values[:,::-1];labels=labels[:,::-1]
    return np.stack([values[y::2,x::2] for y,x in ((0,0),(0,1),(1,0),(1,1))]),labels.ravel().tolist()


def run(root,output):
    import rawpy
    from memory_budget import MemoryGuard
    build=root/'build-patch_neural_restoration'
    original=build/'natural-cfa-preflight-v002/data.json'
    bridge_path=build/'natural-bench-native-bridge-v001/bridge-progress.json'
    data,bridge=read(original),read(bridge_path)
    if bridge['status']!='qualified_archived_ROI_CFA_bridge':raise ValueError('Exact native bridge required')
    by_hash={row['sha256']:row for row in bridge['sources']}
    output.mkdir(exist_ok=False)
    result=dict(schema_version=1,measured_at_utc=datetime.now(timezone.utc).isoformat(),
        status='preparing',entries=[],tools={n:digest(Path(__file__).with_name(n)) for n in (
            'prepare_native_normalized_cfa.py','memory_budget.py','prepare_next_training.py')},
        parents=[dict(path=str(p),sha256=digest(p)) for p in (original,bridge_path)],
        scope='Numerical input proxies only: archived IPR normalized signed CFA, prior roles unchanged',
        limits=['Single ISO100 images are not noise-free truth',
                'Focus, sensor-noise realism, final scene/session split and calibrated baselines pending',
                'Different domain from the active rawpy-metadata preflight; no cross-domain score comparison',
                'Physical green labels retained in metadata; tensor planes are canonical spatial positions'])
    arrays=[]
    with MemoryGuard(hard_bytes=2*1024**3,soft_bytes=1280*1024**2,system_fraction=.8) as guard:
        for source_hash,source in by_hash.items():
            guard.check()
            path=Path(source['source'])
            if digest(path)!=source_hash:raise ValueError('Original source changed')
            with rawpy.imread(str(path)) as raw:
                visible=raw.raw_image_visible
                pattern=raw.raw_colors_visible[:2,:2].copy()
                for entry in data['entries']:
                    if entry['source_sha256']!=source_hash:continue
                    x,y=entry['crop_xy']
                    packed,labels=native_patch(visible[y:y+512,x:x+512],pattern,source['black'],source['divisor'])
                    arrays.append(packed)
                    result['entries'].append(dict(source=str(path),source_sha256=source_hash,role=entry['role'],
                        crop_xy=[x,y],black=source['black'],white=source['white'],divisor=source['divisor'],
                        physical_labels=labels,packed_sha256=hashlib.sha256(packed.tobytes()).hexdigest(),
                        below_zero_fraction=float((packed<0).mean()),at_or_above_one_fraction=float((packed>=1).mean()),
                        focus_qualified=False,clean_truth_qualified=False))
                del visible
            if digest(path)!=source_hash:raise ValueError('Source changed during extraction')
        np.save(output/'targets.npy',np.stack(arrays),allow_pickle=False)
    result.update(status='qualified_numeric_input_proxies_not_full_benchmark',
        dataset_sha256=digest(output/'targets.npy'),shape=[len(arrays),*arrays[0].shape],memory=guard.stats())
    write(output/'result.json',result)
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    with (root/'docs/validation/raw'/f'neural-native-normalized-proxies-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
        json.dump(result,stream,allow_nan=False,separators=(',',':'))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    run(Path(__file__).resolve().parents[2],args.output)
