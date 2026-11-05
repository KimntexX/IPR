"""Seal local-sigma photo trial, no new fitting, training or reserve opening."""
from datetime import datetime,timezone
import json
from pathlib import Path
import numpy as np
import rawpy
from camera_noise_sigma import interpolate_noise,balanced_sigma
from campaign_artifacts import sha256
from memory_budget import MemoryGuard


def run():
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    output=build/'local-sigma-plan-v001';output.mkdir(exist_ok=False)
    parent_path=build/'real-photo-baselines-v001/plan.json'
    parent=json.loads(parent_path.read_text(encoding='utf-8'))
    law_path=build/'sensor-noise-saturation-v001/result.json'
    law=json.loads(law_path.read_text(encoding='utf-8'))
    if law['status']!='completed_saturation_sensitivity_not_calibration':raise ValueError('Noise study incomplete')
    fits=law['fits'];pattern=[[0,1],[3,2]]
    plan=dict(schema_version=1,status='preparing',parent_sha256=sha256(parent_path),law_sha256=sha256(law_path),
        created_at_utc=datetime.now(timezone.utc).isoformat(),models=parent['models'],native=parent['native'],scenes=[],
        protocol='Same frozen both-seed weights, whole sensor CFA, fixed core256/halo128/batch4. Compare .01/.03 existing constant states against plug-in per-pixel sigma. No strength fitting or training.',
        limits=['Exploratory noise law, pooled sessions; no camera calibration or clean truth.',
                'Raw noisy signal plug-in max(signal,0), no smoothing, no demosaic covariance model.',
                'Interpolated ISO parameters, no extrapolation; a12800 and a/b4000 not directly characterized.'],
        tools={n:sha256(Path(__file__).with_name(n)) for n in ('prepare_sensor_sigma.py','camera_noise_sigma.py')})
    guard=MemoryGuard(hard_bytes=2*1024**3,soft_bytes=1280*1024**2,system_fraction=.92)
    try:
        with guard:
            for scene in parent['scenes']:
                guard.check()
                capture=json.loads(Path(scene['capture']).read_text(encoding='utf-8'))
                source=Path(capture['source'])
                if sha256(source)!=scene['source_sha256']:raise ValueError('RAW changed')
                shape=scene['sensor_shape'][:2]
                target=output/(scene['name']+'-sigma.npy')
                sigma=np.lib.format.open_memmap(target,mode='w+',dtype='<f4',shape=tuple(shape))
                parameters=[];quantiles=[]
                with rawpy.imread(str(source)) as raw:
                    if list(raw.raw_image_visible.shape)!=shape or raw.raw_pattern.tolist()!=pattern:raise ValueError('CFA geometry changed')
                    for y in range(2):
                        for x in range(2):
                            guard.check();plane=y*2+x;colour=pattern[y][x]
                            a,b=interpolate_noise(fits,scene['iso'],plane)
                            signal=raw.raw_image_visible[y::2,x::2].astype(np.float32)-capture['black'][colour]
                            value=balanced_sigma(signal,a,b,capture['gains'][colour],capture['divisor'])
                            sigma[y::2,x::2]=value
                            parameters.append(dict(plane=plane,colour=colour,a=a,b=b,gain=capture['gains'][colour],divisor=capture['divisor']))
                            quantiles.append(np.quantile(value[::8,::8],[.05,.5,.95]).tolist())
                sigma.flush();del sigma
                if sha256(source)!=scene['source_sha256']:raise ValueError('RAW changed during read')
                plan['scenes'].append(dict(**scene,sigma_path=str(target.resolve()),sigma_sha256=sha256(target),
                    parameters=parameters,plane_sampled_sigma_q05_q50_q95=quantiles))
                print('SIGMA MAP',scene['name'],flush=True)
            plan['status']='sealed_local_sigma_photo_exploration'
    except Exception as error:
        plan.update(status='stopped_on_error',error=str(error));raise
    finally:
        plan['memory']=guard.stats()
        with (output/'plan.json').open('x',encoding='utf-8') as stream:json.dump(plan,stream,indent=2,allow_nan=False)


if __name__=='__main__':run()
