"""Postmeasurement sensitivity to saturation below the advertised rawpy white."""
from copy import deepcopy
from datetime import datetime,timezone
import json
from pathlib import Path
import numpy as np
from campaign_artifacts import sha256
from sensor_noise_math import near_white_fraction
from study_sensor_noise import fit_all
from assess_sensor_noise import session_control,camera_sigma,plot


def run():
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    parent_path=build/'sensor-noise-v001/result.json'
    parent=json.loads(parent_path.read_text(encoding='utf-8'))
    output=build/'sensor-noise-saturation-v001';output.mkdir(exist_ok=False)
    protocol=dict(scope='Postmeasurement sensitivity, not fresh confirmation. Original105pairs and split unchanged; no repeated RAW decoding.',
        trigger='60ROI/plane differences were zero near16382DN, below advertised white16383.',
        guard='Reject fitting when either cached frame has any sample>=reported_white-16DN. Fixed conservative16DN margin, not a fitted sensor white.',
        parent_sha256=sha256(parent_path),tools={name:sha256(Path(__file__).with_name(name)) for name in
            ('check_sensor_saturation.py','study_sensor_noise.py','sensor_noise_math.py','assess_sensor_noise.py')})
    with (output/'plan.json').open('x',encoding='utf-8') as stream:json.dump(protocol,stream,indent=2)
    proof=deepcopy(parent)
    for pair in proof['pairs']:
        fractions=[]
        for index in pair['frames']:
            frame=proof['frames'][index];path=Path(frame['cached_tiles'])
            if sha256(path)!=frame['tiles_sha256']:raise ValueError('Cached sensor tiles changed')
            tiles=np.load(path,allow_pickle=False)
            fractions.append(near_white_fraction(tiles,frame['rawpy_reported_white_unvalidated']))
        for row in proof['measurements'][pair['index']*36:(pair['index']+1)*36]:
            row['at_saturation_guard_fraction']=float(max(f[row['roi'],row['plane']] for f in fractions))
    old_eligible={(r['pair'],r['roi'],r['plane']) for r in parent['measurements'] if r.get('fit_gate',{}).get('eligible')}
    proof['fits']=fit_all(proof['measurements'],'at_saturation_guard_fraction')
    new_eligible={(r['pair'],r['roi'],r['plane']) for r in proof['measurements'] if r.get('fit_gate',{}).get('eligible')}
    result=dict(schema_version=1,status='completed_saturation_sensitivity_not_calibration',
        measured_at_utc=datetime.now(timezone.utc).isoformat(),protocol=protocol,
        originally_eligible=len(old_eligible),eligible_with_saturation_guard=len(new_eligible),
        newly_rejected=sorted(old_eligible-new_eligible),fits=proof['fits'],measurements=proof['measurements'],
        session_control=session_control(proof,'at_saturation_guard_fraction'),camera_sigma=camera_sigma(proof,build))
    plot(proof,output/'signal-variance.svg')
    result['figure_sha256']=sha256(output/'signal-variance.svg')
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    for path in (output/'result.json',root/'docs/validation/raw'/f'neural-sensor-saturation-v001-{stamp}.json'):
        with path.open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)
    print('eligible',len(old_eligible),'->',len(new_eligible),flush=True)
    for r in result['fits']:
        f=r.get('fit',{})
        if 'shot_slope_dn' in f:print('FIT',r['iso'],r['plane'],round(f['median_relative_variance_error'],4),round(f['median_constant_relative_variance_error'],4))


if __name__=='__main__':run()
