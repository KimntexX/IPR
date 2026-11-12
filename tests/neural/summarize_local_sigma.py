"""Fixed-ROI descriptive comparison: local noise map versus constant sigma."""
from datetime import datetime,timezone
import html
import json
import os
from pathlib import Path
import numpy as np
import cv2
from campaign_artifacts import sha256
from summarize_photo_comparison import pixels,describe


def fine_chroma(image):
    value=np.stack((image[:,:,0]-image[:,:,2],image[:,:,1]-(image[:,:,0]+image[:,:,2])*.5),axis=-1)
    high=value-cv2.GaussianBlur(value,(0,0),1.)
    return float(np.sqrt(np.mean(np.var(high[8:-8,8:-8],axis=(0,1)))))


def cfa_effects(build):
    proof=json.loads((build/'local-sigma-inference-v001/result.json').read_text(encoding='utf-8'))
    rows=[]
    def orient(value,bits):
        if bits&2:value=value[::-1]
        if bits&1:value=value[:,::-1]
        return value.T if bits&4 else value
    for record in proof['records']:
        scene=next(r for r in proof['plan']['scenes'] if r['name']==record['scene'])
        if sha256(Path(record['path']))!=record['sha256']:raise ValueError('Corrected CFA changed')
        shape=tuple(scene['sensor_shape'][:2]);bits=scene['orientation']
        source=orient(np.memmap(scene['input'],dtype='<f4',mode='r',shape=shape),bits)
        corrected=orient(np.memmap(record['path'],dtype='<f4',mode='r',shape=shape),bits)
        sigma=orient(np.load(scene['sigma_path'],mmap_mode='r',allow_pickle=False),bits)
        context=json.loads(Path(scene['context']).read_text(encoding='utf-8'))
        for roi,tile in enumerate(context['tiles']):
            x,y=tile['origin_xy'];delta=corrected[y:y+384,x:x+384].astype(np.float64)-source[y:y+384,x:x+384]
            noise=sigma[y:y+384,x:x+384]
            rows.append(dict(scene=scene['name'],seed=record['seed'],roi=roi,changed_fraction=float((delta!=0).mean()),
                rms_correction_in_supplied_sigma=float(np.sqrt(np.mean((delta/np.maximum(noise,1e-12))**2)))))
    return rows


def run():
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    folder=build/'local-sigma-display-v001';local_path=folder/'result.json'
    local=json.loads(local_path.read_text(encoding='utf-8'))
    if local['status']!='completed_local_sigma_display_not_qualification':raise ValueError('Complete display required')
    parent_path=build/'real-photo-display-v002/result.json'
    parent=json.loads(parent_path.read_text(encoding='utf-8'))
    result=dict(schema_version=1,measured_at_utc=datetime.now(timezone.utc).isoformat(),
        local_display_sha256=sha256(local_path),constant_display_sha256=sha256(parent_path),records=[],seed_variation=[],
        script_sha256=sha256(Path(__file__)),
        scope='All27fixedROIs; rendered fine content includes texture and noise. No clean truth, PSNR, model selection or claimed detail preservation.')
    panels=[]
    for scene in parent['scenes']:
        variants={v['label']:json.loads(Path(v['display']).read_text(encoding='utf-8'))['rois'] for v in scene['variants'] if not v['halo']}
        for roi in range(9):
            ref=variants['RCD-neutral'][roi];reference=pixels(Path(ref['path']),ref['sha256'])
            choices=[('RCD-neutral',ref),('classic-intensity-60',variants['classic-intensity-60'][roi])]
            for seed in (71,73):
                for sigma in (.01,.03):
                    label=f'neural-seed{seed}-sigma{sigma}';choices.append((label,variants[label][roi]))
                state=next(r for r in local['records'] if r['scene']==scene['name'] and r['seed']==seed)
                choices.append((f'neural-seed{seed}-local',state['display']['rois'][roi]))
            images={};figures=[]
            for label,row in choices:
                path=Path(row['path']).resolve();value=pixels(path,row['sha256']);images[label]=value
                result['records'].append(dict(scene=scene['name'],roi=roi,variant=label,sha256=row['sha256'],
                    fine_chroma_std_ratio=fine_chroma(value)/max(fine_chroma(reference),1e-12),**describe(reference,value)))
                relative=Path(os.path.relpath(path,folder)).as_posix()
                figures.append(f'<figure><figcaption>{html.escape(label)}</figcaption><img width="384" height="384" src="{relative}"></figure>')
            result['seed_variation'].append(dict(scene=scene['name'],roi=roi,
                local_rgb_mae=float(np.abs(images['neural-seed71-local']-images['neural-seed73-local']).mean()),
                constant01_rgb_mae=float(np.abs(images['neural-seed71-sigma0.01']-images['neural-seed73-sigma0.01']).mean()),
                constant03_rgb_mae=float(np.abs(images['neural-seed71-sigma0.03']-images['neural-seed73-sigma0.03']).mean())))
            panels.append(f'<h2>{scene["name"]} ISO {scene["iso"]} — ROI {roi}</h2><div>{"".join(figures)}</div>')
    result['summary']={name:dict(regions=len(rows),median_fine_luma_std_ratio=float(np.median([r['fine_luma_std_ratio'] for r in rows])),
        median_fine_chroma_std_ratio=float(np.median([r['fine_chroma_std_ratio'] for r in rows])),
        maximum_absolute_mean_luma_delta=float(max(abs(r['encoded_luma_mean_delta']) for r in rows)))
        for name in sorted({r['variant'] for r in result['records']}) for rows in [[r for r in result['records'] if r['variant']==name]]}
    result['seed_rgb_mae_medians']={key:float(np.median([r[key] for r in result['seed_variation']]))
        for key in ('local_rgb_mae','constant01_rgb_mae','constant03_rgb_mae')}
    result['cfa_correction_descriptors']=cfa_effects(build)
    (folder/'comparison.html').write_text('<!doctype html><meta charset="utf-8"><title>Carte sigma locale</title>'
        '<style>body{background:#24272b;color:#eee;font:16px sans-serif}div{display:flex;overflow:auto}figure{margin:8px;flex:none}figcaption{padding:8px}</style>'
        '<h1>Carte sigma locale — poids et RCD inchangés, pixels à100%</h1>'
        '<p>Photos connues, sans vérité propre. Loi de bruit expérimentale ; ISO interpolés là où nécessaire. '
        'Le classique montre seulement la luminance. Comparer détails et artefacts ; le lissage ne prouve pas la qualité.</p>'
        +''.join(panels),encoding='utf-8')
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    with (root/'docs/validation/raw'/f'neural-local-sigma-descriptors-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
        json.dump(result,stream,indent=2,allow_nan=False)
    with (folder/'descriptors-v002.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)
    print(json.dumps(dict(summary=result['summary'],seed_mae=result['seed_rgb_mae_medians']),indent=2),flush=True)


if __name__=='__main__':run()
