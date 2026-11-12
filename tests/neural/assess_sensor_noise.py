"""Session-separated exploratory controls and camera-domain sigma diagnostics."""
from datetime import datetime,timezone
import json
from pathlib import Path

import numpy as np
from campaign_artifacts import sha256
from sensor_noise_math import fit_signal_variance


def session_control(proof,saturation_key='at_reported_white_fraction'):
    rows=proof['measurements'];pairs=proof['pairs'];frames=proof['frames'];results=[]
    def day(row):return frames[pairs[row['pair']]['frames'][0]]['metadata']['datetime_original'][:10]
    for iso in sorted({r['iso'] for r in rows}):
        for plane in range(4):
            dark=[r for r in rows if r['iso']==iso and r['plane']==plane and r['kind']=='dark' and day(r)=='2026:10:28']
            if not dark:
                results.append(dict(iso=iso,plane=plane,status='no_training_dark_session'));continue
            black=float(np.median([r['mean_dn'] for r in dark]))
            read=float(np.median([r['temporal_variance_dn2'] for r in dark]))
            training=[];held=[];decisions=[]
            for row in rows:
                if row['iso']!=iso or row['plane']!=plane or row['kind']!='light':continue
                signal=row['mean_dn']-black
                admissible=(signal>0 and abs(row['mean_drift_dn'])<=.01*signal
                    and row['block_span_dn']<=.05*signal and row[saturation_key]==0
                    and row['temporal_variance_dn2']>0)
                role={'2026:10:16':'training','2026:10:22':'heldout'}.get(day(row),'unused')
                decisions.append(dict(pair=row['pair'],roi=row['roi'],eligible=admissible,role=role))
                if admissible and role!='unused':
                    (training if role=='training' else held).append(dict(pair=row['pair'],roi=row['roi'],
                        signal_dn=signal,variance_dn2=row['temporal_variance_dn2']))
            fit=fit_signal_variance(training,held,read)
            results.append(dict(iso=iso,plane=plane,training_black_dn=black,training_read_variance_dn2=read,
                decisions=decisions,fit=fit))
    return dict(protocol='Light16October+dark28October fit; light22October check, dark22October excluded from fit. Same fixed gates recomputed with training black.',
        scope='Exploratory postmeasurement control, not unseen confirmation; calendar sessions do not prove independent scene identity or temperature.',
        results=results)


def camera_sigma(proof,build):
    fits=proof['fits'];calibration_pattern=proof['frames'][0]['pattern']
    if any(f['pattern']!=calibration_pattern for f in proof['frames']):raise ValueError('Mixed calibration Bayer phase')
    colour_to_plane={int(c):i for i,c in enumerate(np.array(calibration_pattern).reshape(-1))}
    def parameters(iso,plane):
        candidates=sorted((r['iso'],r['fit']['shot_slope_dn']) for r in fits if r['plane']==plane and 'shot_slope_dn' in r.get('fit',{}))
        xx=np.array([p[0] for p in candidates]);yy=np.array([p[1] for p in candidates])
        if not xx[0]<=iso<=xx[-1] or (yy<=0).any():raise ValueError('No positive interpolation support')
        a=float(np.exp(np.interp(np.log(iso),np.log(xx),np.log(yy))))
        dark=sorted((r['iso'],r['reference_dark_variance_dn2']) for r in fits if r['plane']==plane and r.get('reference_dark_variance_dn2',0)>0)
        b=float(np.exp(np.interp(np.log(iso),np.log([p[0] for p in dark]),np.log([p[1] for p in dark]))))
        return a,b
    records=[]
    for name in ('6V1A0181','6V1A0263','6V1A9819'):
        path=build/'photo-comparison-v001/full-cfa-sensor-v002'/(name+'.json')
        context=json.loads(path.read_text(encoding='utf-8'));binary=path.parent/context['pixels']
        if sha256(binary)!=context['pixels_sha256']:raise ValueError('Native capture changed')
        with binary.open('rb') as stream:
            for tile in context['stages']:
                if tile['stage']!='normalized_cfa_before_wb_unclipped':continue
                stream.seek(tile['offset_bytes'])
                value=np.frombuffer(stream.read(384*384*4),dtype='<f4').reshape(384,384).astype(np.float64)
                for y in range(2):
                    for x in range(2):
                        colour=tile['pattern'][y][x];plane=colour_to_plane[colour]
                        a,b=parameters(context['iso'],plane)
                        signal=value[y::2,x::2]*context['divisor']
                        sigma=np.sqrt(a*np.maximum(signal,0)+b)*context['gains'][colour]/context['divisor']
                        records.append(dict(scene=name,iso=context['iso'],roi=tile['roi'],colour_index=colour,
                            source_capture_sha256=sha256(path),shot_slope_dn=a,read_variance_dn2=b,
                            camera_sigma_q05_q50_q95=np.quantile(sigma,[.05,.5,.95]).tolist(),
                            under_sigma01_fraction=float((sigma<.01).mean()),over_sigma03_fraction=float((sigma>.03).mean()),
                            negative_signal_fraction=float((signal<0).mean())))
    return dict(scope='Illustrative unqualified plug-in sigma, not measured photo noise or new training input. Nonnegative signal estimate; independent sensor law, no RCD covariance propagation.',
        interpolation='Positive log-log interpolation across existing ISO fits, no extrapolation. At12800 shot slope interpolated, dark variance directly observed; at4000 both interpolated.',
        records=records)


def plot(proof,target):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig,axes=plt.subplots(2,3,figsize=(15,8),layout='constrained')
    for ax,iso in zip(axes.flat,(100,1600,3200,6400,16000,51200)):
        for plane,colour in enumerate(('tab:red','tab:green','olive','tab:blue')):
            row=next(r for r in proof['fits'] if r['iso']==iso and r['plane']==plane)
            values=[r for r in proof['measurements'] if r['iso']==iso and r['plane']==plane and r['kind']=='light' and r.get('fit_gate',{}).get('eligible')]
            if not values:continue
            xx=np.array([r['fit_gate']['signal_dn'] for r in values]);yy=np.array([r['temporal_variance_dn2'] for r in values])
            ax.scatter(xx,yy,s=7,alpha=.35,color=colour)
            fit=row.get('fit',{})
            if 'shot_slope_dn' in fit:
                line=np.geomspace(max(1.,xx.min()),xx.max(),80)
                ax.plot(line,fit['shot_slope_dn']*line+fit['read_variance_dn2'],color=colour,label=f'plane{plane}')
        ax.set(xscale='log',yscale='log',title=f'ISO{iso}',xlabel='Signal above pooled dark (DN)',ylabel='Temporal variance (DN²)')
        ax.grid(alpha=.2);ax.legend(fontsize=8)
    fig.suptitle('Exploratory CFA sensor law — eligible measured points, not a qualified calibration')
    fig.savefig(target);plt.close(fig)


def run():
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    source=build/'sensor-noise-v001/result.json'
    proof=json.loads(source.read_text(encoding='utf-8'))
    if proof['status']!='completed_exploratory_sensor_dn_law_not_calibration':raise ValueError('Complete measurement required')
    output=build/'sensor-noise-assessment-v001';output.mkdir(exist_ok=False)
    result=dict(schema_version=1,status='completed_exploratory_assessment_not_calibration',
        measured_at_utc=datetime.now(timezone.utc).isoformat(),parent_sha256=sha256(source),script_sha256=sha256(Path(__file__)),
        session_control=session_control(proof),camera_sigma=camera_sigma(proof,build))
    plot(proof,output/'signal-variance.svg')
    result['figure']=dict(path=str(output/'signal-variance.svg'),sha256=sha256(output/'signal-variance.svg'))
    for path in (output/'result.json',root/'docs/validation/raw'/('neural-sensor-assessment-v001-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'.json')):
        with path.open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)
    for iso in (100,1600,3200,6400,12800,16000,25600,51200):
        fits=[r['fit'] for r in result['session_control']['results'] if r['iso']==iso and 'median_relative_variance_error' in r.get('fit',{})]
        print('SESSION',iso,[(round(f['median_relative_variance_error'],3),round(f['median_constant_relative_variance_error'],3)) for f in fits])
    for name in ('6V1A0181','6V1A0263','6V1A9819'):
        rr=[r for r in result['camera_sigma']['records'] if r['scene']==name]
        print('SIGMA',name,np.min([r['camera_sigma_q05_q50_q95'][0] for r in rr]),np.max([r['camera_sigma_q05_q50_q95'][2] for r in rr]))


if __name__=='__main__':run()
