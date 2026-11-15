"""Compare calibrated C2 captures with preserved, pre-C2 noise opening buffers."""
from datetime import datetime,timezone
import argparse
import json
from pathlib import Path

import numpy as np

from capture import load_capture,sha256


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--parity',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);a=p.parse_args()
    campaign=json.loads(a.parity.read_text(encoding='utf-8'));old=json.loads(a.baseline.read_text(encoding='utf-8'));records=[]
    for r in campaign['records']:
        if not r['explicit_levels']:continue
        source=r['source']['path'];item=old['native'][source];binary=Path(item['pixels']['path'])
        if item['record']['source_sha256']!=r['source']['sha256'] or sha256(binary)!=item['pixels']['sha256']:raise ValueError('Historical input changed')
        q=r['captures']['candidate']
        if sha256(Path(q['path']))!=q['sha256']:raise ValueError('Candidate manifest changed')
        manifest,stages=load_capture(q['path']);pixels=np.fromfile(binary,dtype='<f4')
        for current,historical in (('pre_demosaic_cfa','pre_rcd_scaled_cfa'),('post_median_camera_rgb','post_rcd_median_camera_rgb'),('pre_dcp_camera_rgb','pre_dcp_camera_rgb')):
            for stage in item['record']['stages']:
                if stage['stage']!=historical:continue
                x,y,w,h=stage['rect'];count=int(np.prod(stage['shape']));offset=stage['offset_bytes']//4
                if offset<0 or offset+count>pixels.size:raise ValueError('Invalid historical extent')
                before=pixels[offset:offset+count].reshape(stage['shape'])
                after=stages[(current,(y,x))][1]
                if before.shape!=after.shape:raise ValueError('Historical geometry differs')
                delta=before.astype(float)-after.astype(float)
                records.append(dict(source=source,current_stage=current,previous_stage=historical,origin_yx=[y,x],max_abs=float(abs(delta).max()),unequal_values=int(np.count_nonzero(delta)),
                    recovered_u16_unequal=int(np.count_nonzero(np.rint(before.astype(float)*65535)!=np.rint(after.astype(float)*65535)))))
    if len(records)!=18:raise ValueError('Need two calibrated sources and three stages x three ROIs')
    now=datetime.now(timezone.utc);root=Path(__file__).resolve().parents[2];out=root/'docs/validation/raw'/f'banc-raw-reference-bruit-v001-{now.strftime("%Y%m%dT%H%M%SZ")}.json'
    exact=[r for r in records if r['current_stage']!='pre_dcp_camera_rgb']
    result=dict(schema_version=1,measured_at_utc=now.isoformat(),parents=[dict(path=str(p.resolve()),sha256=sha256(p)) for p in (a.parity,a.baseline)],records=records,
        verdict='pass_integer_boundaries' if all(r['max_abs']==0 for r in exact) and all(r['recovered_u16_unequal']==0 for r in records) else 'fail',
        limitations='Two calibrated black RAWs only. Historical pre-DCP may use different floating representation; deviations recorded and recovered integer values must remain equal. No claim of exact f32 DCP-input parity, signed-mode correctness, full-frame parity or quality improvement.')
    with out.open('x',encoding='utf-8') as f:json.dump(result,f,ensure_ascii=False,indent=2,allow_nan=False)
    print(result['verdict'],'max preRCD/postMedian',max(r['max_abs'] for r in exact),'max final camera',max(r['max_abs'] for r in records),'RESULT',out,flush=True)
    return 0 if result['verdict']=='pass_integer_boundaries' else 1


if __name__=='__main__':raise SystemExit(main())
