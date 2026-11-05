"""Describe fixed real ROIs without treating their fine texture as noise truth."""
from datetime import datetime,timezone
import html
import json
import os
from pathlib import Path

import cv2
import numpy as np
from PIL import Image
from campaign_artifacts import sha256


def pixels(path,expected):
    if sha256(path)!=expected:raise ValueError('Display image changed')
    with Image.open(path) as image:
        if image.size!=(384,384):raise ValueError('Fixed ROI required')
        return np.asarray(image.convert('RGB'),dtype=np.float64)/255


def describe(reference,value):
    luma=np.dot(value,[.2126,.7152,.0722])
    parent_luma=np.dot(reference,[.2126,.7152,.0722])
    high=luma-cv2.GaussianBlur(luma,(0,0),1.)
    parent_high=parent_luma-cv2.GaussianBlur(parent_luma,(0,0),1.)
    inner=np.s_[8:-8,8:-8]
    return dict(encoded_luma_mean=float(luma.mean()),
        encoded_luma_mean_delta=float((luma-parent_luma).mean()),
        encoded_rgb_mae=float(np.abs(reference-value).mean()),
        fine_luma_std_ratio=float(high[inner].std()/max(parent_high[inner].std(),1e-12)),
        interpretation='Descriptive rendered texture-plus-noise; not noise reduction accuracy, clean truth, PSNR or detail preservation.')


def run():
    root=Path(__file__).resolve().parents[2]
    build=root/'build-patch_neural_restoration'
    display=build/'real-photo-display-v002'
    external=build/'drunet-photo-v001'
    parents=json.loads((display/'result.json').read_text(encoding='utf-8'))
    official=json.loads((external/'result.json').read_text(encoding='utf-8'))
    if not parents['status'].startswith('completed_') or not official['status'].startswith('completed_'):
        raise ValueError('Complete input proofs required')
    result=dict(schema_version=1,measured_at_utc=datetime.now(timezone.utc).isoformat(),
        display_proof_sha256=sha256(display/'result.json'),official_proof_sha256=sha256(external/'result.json'),
        scope='All fixed ROIs, no exclusions or winner selection; encoded sRGB descriptors do not measure true noise or lost information.',records=[])
    pages=[]
    for scene in parents['scenes']:
        variants=[]
        for variant in scene['variants']:
            if variant['halo']:continue
            proof=json.loads(Path(variant['display']).read_text(encoding='utf-8'))
            variants.append((variant['label'],proof['rois']))
        for roi in range(9):
            parent=variants[0][1][roi]
            reference=pixels(Path(parent['path']),parent['sha256'])
            figures=[]
            choices=[(name,rows[roi]) for name,rows in variants]
            choices.extend((f"DRUNet-sRGB-sigma{r['sigma_8bit']}",r) for r in official['records']
                           if r['scene']==scene['name'] and r['roi']==roi)
            for name,row in choices:
                path=Path(row['path']).resolve()
                value=pixels(path,row['sha256'])
                result['records'].append(dict(scene=scene['name'],roi=roi,variant=name,path=str(path),
                    sha256=row['sha256'],**describe(reference,value)))
                relative=Path(os.path.relpath(path,display)).as_posix()
                figures.append(f'<figure><figcaption>{html.escape(name)}</figcaption><img width="384" height="384" src="{relative}"></figure>')
            pages.append(f'<h2>{scene["name"]} ISO {scene["iso"]} — ROI {roi}</h2><div>{"".join(figures)}</div>')
    diagnostic_path=build/'learned-photo-diagnostic-v001/result.json'
    diagnostic=json.loads(diagnostic_path.read_text(encoding='utf-8'))
    if diagnostic['status']!='completed_separate_learned_reconstruction_not_qualification':
        raise ValueError('Learned RGB diagnostic incomplete')
    result['separate_learned_diagnostic_sha256']=sha256(diagnostic_path)
    pages.append('<h1>Diagnostic séparé : reconstruction RGB apprise, sigma CFA 0,03</h1>'
                 '<p>Ces régions ne passent pas par RCD. Elles ne qualifient pas un remplacement du dématriçage.</p>')
    for scene in parents['scenes']:
        for roi in range(9):
            figures=[]
            for record in diagnostic['records']:
                if record['scene']!=scene['name']:continue
                row=record['display']['rois'][roi]
                path=Path(row['path']).resolve()
                if sha256(path)!=row['sha256']:raise ValueError('Learned diagnostic changed')
                relative=Path(os.path.relpath(path,display)).as_posix()
                figures.append(f'<figure><figcaption>RGB appris graine {record["seed"]}</figcaption>'
                               f'<img width="384" height="384" src="{relative}"></figure>')
            pages.append(f'<h2>{scene["name"]} — ROI {roi}</h2><div>{"".join(figures)}</div>')
    result['summary']={name:dict(regions=len(rows),
        median_fine_luma_std_ratio=float(np.median([r['fine_luma_std_ratio'] for r in rows])),
        maximum_absolute_mean_luma_delta=float(max(abs(r['encoded_luma_mean_delta']) for r in rows)))
        for name in sorted({r['variant'] for r in result['records']})
        for rows in [[r for r in result['records'] if r['variant']==name]]}
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    target=root/'docs/validation/raw'/f'neural-real-photo-descriptors-v001-{stamp}.json'
    target.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    (display/'comparison.html').write_text('<!doctype html><meta charset="utf-8"><title>Comparatif photo</title>'
        '<style>body{background:#24272b;color:#eee;font:16px sans-serif}div{display:flex;overflow:auto}'
        'figure{margin:8px;flex:none}figcaption{padding:8px}</style>'
        '<h1>Comparaison exploratoire — pixels à 100 %</h1><p>RCD identique, même profil et affichage. '
        'Classique : luminance seulement, couleur après les tons exclue. Sigma CFA fourni, non calibré par ISO. '
        'DRUNet : modèle officiel sRGB, sigma 15/255 ou 25/255 ; valeurs non équivalentes aux curseurs IPR. '
        'Deux graines conservées sans choisir un gagnant. Aucun rendu ne constitue une vérité propre.</p>'
        +''.join(pages),encoding='utf-8')
    print(json.dumps(result['summary'],indent=2),flush=True)


if __name__=='__main__':run()
