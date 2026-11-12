"""All declared curve milestones on known photos; no best-checkpoint selection."""
from datetime import datetime,timezone
import html
import json
import os
from pathlib import Path
import numpy as np
from campaign_artifacts import sha256
from summarize_photo_comparison import pixels,describe
from summarize_local_sigma import fine_chroma


def run():
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    campaign=build/'sensor-learning-campaign-v001'
    baseline_path=build/'local-sigma-display-v001/result.json';baseline=json.loads(baseline_path.read_text(encoding='utf-8'))
    neutral=json.loads((build/'real-photo-display-v002/result.json').read_text(encoding='utf-8'))
    displays={0:baseline};proofs={0:sha256(baseline_path)}
    for step in (256,1024,2048):
        path=campaign/f'photo-display-{step}'/'result.json';value=json.loads(path.read_text(encoding='utf-8'))
        if value['status']!='completed_local_sigma_display_not_qualification':raise ValueError('Missing declared photo milestone')
        displays[step]=value;proofs[step]=sha256(path)
    result=dict(schema_version=1,measured_at_utc=datetime.now(timezone.utc).isoformat(),
        status='completed_bounded_curve_known_photo_diagnostics_not_qualification',display_proofs=proofs,records=[],training={},
        limits=['All known development photos, no clean reference or independent confirmation.',
            'Luma/chroma fine descriptors contain texture and noise; lower is not a quality verdict.',
            'Noise law and continuation both change, no attribution to duration alone.',
            'Known fine/classical regression gates have not been replayed for these checkpoints; no reserve may open.',
            'All milestones retained, no best score or seed selection.'])
    for seed in (71,73):
        path=campaign/f'seed{seed}'/'result.json';value=json.loads(path.read_text(encoding='utf-8'))
        if value['status']!='completed_sensor_curve_continuation_not_quality_qualification':raise ValueError('Incomplete training')
        milestones=[dict(step=step,path=str(campaign/f'seed{seed}'/f'checkpoint-{step:06d}.pt'),
            sha256=sha256(campaign/f'seed{seed}'/f'checkpoint-{step:06d}.pt')) for step in (0,256,1024,2048)]
        result['training'][seed]=dict(source_sha256=sha256(path),milestones=milestones,
            losses=value['losses'],median_seconds_per_update=value['median_seconds_per_update'],
            training_loss_tail64=float(np.mean(value['losses'][-64:])))
    panels=[]
    for scene in neutral['scenes']:
        zero=next(v for v in scene['variants'] if v['label']=='RCD-neutral')
        references=json.loads(Path(zero['display']).read_text(encoding='utf-8'))['rois']
        classic=next(v for v in scene['variants'] if v['label']=='classic-intensity-60')
        classics=json.loads(Path(classic['display']).read_text(encoding='utf-8'))['rois']
        for roi in range(9):
            reference=pixels(Path(references[roi]['path']),references[roi]['sha256']);figures=[]
            choices=[('RCD neutral',references[roi]),('Classic luminance60',classics[roi])]
            for seed in (71,73):
                for step in (0,256,1024,2048):
                    record=next(r for r in displays[step]['records'] if r['scene']==scene['name'] and r['seed']==seed)
                    row=record['display']['rois'][roi];path=Path(row['path']).resolve()
                    value=pixels(path,row['sha256'])
                    result['records'].append(dict(scene=scene['name'],roi=roi,seed=seed,additional_steps=step,
                        sha256=row['sha256'],fine_chroma_std_ratio=fine_chroma(value)/max(fine_chroma(reference),1e-12),**describe(reference,value)))
                    choices.append((f'Seed{seed}, +{step}',row))
            for label,row in choices:
                relative=Path(os.path.relpath(Path(row['path']).resolve(),campaign)).as_posix()
                figures.append(f'<figure><figcaption>{html.escape(label)}</figcaption><img width="384" height="384" src="{relative}"></figure>')
            panels.append(f'<h2>{scene["name"]} — ROI{roi}</h2><div>{"".join(figures)}</div>')
    (campaign/'comparison.html').write_text('<!doctype html><meta charset="utf-8"><title>Bounded learning curve</title>'
        '<style>body{background:#24272b;color:#eee;font:16px sans-serif}div{display:flex;overflow:auto}figure{margin:8px;flex:none}figcaption{padding:8px}</style>'
        '<h1>Continuation à bruit expérimental — tous les jalons, pixels à100%</h1><p>'
        +html.escape(' '.join(result['limits']))+'</p><img width="1000" src="learning-curve.svg">'+''.join(panels),encoding='utf-8')
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    figure,axes=plt.subplots(1,3,figsize=(15,4),layout='constrained')
    for seed in (71,73):
        losses=np.asarray(result['training'][seed]['losses'])
        axes[0].plot(np.arange(64,len(losses)+1),np.convolve(losses,np.ones(64)/64,mode='valid'),label=f'seed{seed}')
        for axis,key in ((axes[1],'fine_luma_std_ratio'),(axes[2],'fine_chroma_std_ratio')):
            values=[float(np.median([r[key] for r in result['records'] if r['seed']==seed and r['additional_steps']==step])) for step in (0,256,1024,2048)]
            axis.plot((0,256,1024,2048),values,'o-',label=f'seed{seed}')
    axes[0].set(title='Training loss, rolling64 (not validation)',xlabel='Additional updates',ylabel='Loss')
    axes[1].set(title='Known photos: fine luma',xlabel='Additional updates',ylabel='Texture+noise ratio to neutral')
    axes[2].set(title='Known photos: fine chroma',xlabel='Additional updates',ylabel='Texture+noise ratio to neutral')
    for axis in axes:axis.grid(alpha=.2);axis.legend()
    figure.savefig(campaign/'learning-curve.svg');plt.close(figure)
    result['figure_sha256']=sha256(campaign/'learning-curve.svg')
    with (campaign/'assessment.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    with (root/'docs/validation/raw'/f'neural-sensor-curve-assessment-v001-{stamp}.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)


if __name__=='__main__':run()
