"""All bootstrap seeds, control milestones and known-photo regions; no best-model selection."""
import argparse
from datetime import datetime,timezone
import html
import json
import os
from pathlib import Path
import numpy as np
from campaign_artifacts import sha256
from summarize_photo_comparison import pixels,describe
from summarize_local_sigma import fine_chroma


def run(campaign):
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    proof=json.loads((campaign/'photo-display/result.json').read_text(encoding='utf-8'))
    if proof['status']!='completed_local_sigma_display_not_qualification':raise ValueError('Complete common RCD/display required')
    neutral=json.loads((build/'real-photo-display-v002/result.json').read_text(encoding='utf-8'))
    inference=json.loads((campaign/'photo-inference/result.json').read_text(encoding='utf-8'))
    result=dict(schema_version=1,status='completed_dru_bootstrap_assessment_not_qualification',
        measured_at_utc=datetime.now(timezone.utc).isoformat(),training={},records=[],
        display_sha256=sha256(campaign/'photo-display/result.json'),
        limits=['Known photos without clean truth; fine descriptors mix texture and noise.',
            'Procedural bootstrap only. Experimental Gaussian law, not a full sensor emulator.',
            'Same unchanged RCD and shared frozen DCP display, no production integration.',
            'Classic comparator is luminance60 only; colour comparator remains incomplete.'])
    for seed in (71,73):
        result['training'][seed]=json.loads((campaign/f'seed{seed}/result.json').read_text(encoding='utf-8'))
    panels=[]
    for scene in neutral['scenes']:
        variants={v['label']:json.loads(Path(v['display']).read_text(encoding='utf-8'))['rois'] for v in scene['variants'] if not v['halo']}
        references=variants['RCD-neutral'];classics=variants['classic-intensity-60']
        for index in range(len(references)):
            reference=pixels(Path(references[index]['path']),references[index]['sha256'])
            choices=[('RCD neutral',references[index]),('Classic luminance60',classics[index])]
            for seed in (71,73):
                record=next(r for r in proof['records'] if r['scene']==scene['name'] and r['seed']==seed)
                row=record['display']['rois'][index];value=pixels(Path(row['path']),row['sha256'])
                result['records'].append(dict(scene=scene['name'],roi=index,seed=seed,sha256=row['sha256'],
                    fine_chroma_std_ratio=fine_chroma(value)/max(fine_chroma(reference),1e-12),**describe(reference,value)))
                steps=next(r['step'] for r in inference['plan']['models'] if r['seed']==seed)
                choices.append((f'DRU CFA seed{seed}, {steps} updates',row))
            figures=[]
            for label,row in choices:
                relative=Path(os.path.relpath(Path(row['path']).resolve(),campaign)).as_posix()
                figures.append(f'<figure><figcaption>{html.escape(label)}</figcaption><img width="384" height="384" src="{relative}"></figure>')
            panels.append(f'<h2>{scene["name"]}, ROI{index}</h2><div>{"".join(figures)}</div>')
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig,ax=plt.subplots(figsize=(10,4),layout='constrained')
    for seed in (71,73):
        values=np.asarray(result['training'][seed]['losses']);window=min(64,len(values))
        ax.plot(np.arange(window,len(values)+1),np.convolve(values,np.ones(window)/window,mode='valid'),label=f'seed{seed}')
    ax.set(xlabel='Optimizer updates',ylabel='Noise-normalized CFA MSE',title='Training only, not photographic validation');ax.grid(alpha=.2);ax.legend()
    fig.savefig(campaign/'learning-curve.svg');plt.close(fig)
    (campaign/'comparison.html').write_text('<!doctype html><meta charset="utf-8"><title>DRU CFA bootstrap</title>'
        '<style>body{background:#24272b;color:#eee;font:16px sans-serif}div{display:flex;overflow:auto}figure{margin:8px;flex:none}</style>'
        '<h1>Premier apprentissage CFA direct, deux graines</h1><p>'+html.escape(' '.join(result['limits']))+
        '</p><img width="1000" src="learning-curve.svg">'+''.join(panels),encoding='utf-8')
    with (campaign/'assessment.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    with (root/'docs/validation/raw'/f'neural-dru-assessment-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
        json.dump(result,stream,indent=2,allow_nan=False)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--campaign',type=Path,required=True);run(p.parse_args().campaign)
