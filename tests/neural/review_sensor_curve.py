"""Read-only closure of completed curve: matched seed differences and epoch tails."""
from datetime import datetime,timezone
import json
from pathlib import Path
import numpy as np
from campaign_artifacts import sha256
from summarize_photo_comparison import pixels


def run():
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    campaign=build/'sensor-learning-campaign-v001'
    progress=json.loads((campaign/'progress.json').read_text(encoding='utf-8'))
    if progress['status']!='completed_bounded_sensor_curve_known_only':raise ValueError('Campaign incomplete')
    source=campaign/'assessment.json';assessment=json.loads(source.read_text(encoding='utf-8'))
    if assessment['status']!='completed_bounded_curve_known_photo_diagnostics_not_qualification':raise ValueError('Missing complete assessment')
    result=dict(schema_version=1,status='completed_read_only_curve_review_not_qualification',
        measured_at_utc=datetime.now(timezone.utc).isoformat(),assessment_sha256=sha256(source),
        progress_sha256=sha256(campaign/'progress.json'),script_sha256=sha256(Path(__file__)),
        seed_difference=[],loss_epoch_tails={},limits=assessment['limits'])
    plan_path=build/'sensor-learning-plan-v001/plan.json'
    plan=json.loads(plan_path.read_text(encoding='utf-8'))
    witness_path=build/'repeatability-control-campaign-v001/seed71-attempt01/result.json'
    witness=json.loads(witness_path.read_text(encoding='utf-8'))
    source_path=Path(witness['plan']['source'])/'result.json'
    if (sha256(plan_path)!=progress['plan_sha256'] or sha256(witness_path)!=plan['training_witness_sha256']
            or sha256(source_path)!=plan['source_sha256']):raise ValueError('Training reproduction source changed')
    result['reproduction']=dict(sealed_plan=plan,target_witness_plan=witness['plan'],
        source_freeze=json.loads(source_path.read_text(encoding='utf-8'))['freeze'],
        limits='Recipe and identities only. JSON is not a backup of RAW/PNG pixels, target tensors or weight/checkpoint files; retain these locally for exact replay.')
    for seed,training in assessment['training'].items():
        losses=training['losses']
        result['loss_epoch_tails'][seed]=[dict(step=step,mean_last64=float(np.mean(losses[step-64:step]))) for step in (512,1024,1536,2048)]
    for step in (0,256,1024,2048):
        path=build/'local-sigma-display-v001/result.json' if step==0 else campaign/f'photo-display-{step}/result.json'
        display=json.loads(path.read_text(encoding='utf-8'))
        if sha256(path)!=assessment['display_proofs'][str(step)]:raise ValueError('Display proof changed')
        rows=[]
        for scene in ('6V1A0181','6V1A0263','6V1A9819'):
            records={r['seed']:r for r in display['records'] if r['scene']==scene}
            for roi in range(9):
                images=[]
                for seed in (71,73):
                    row=records[seed]['display']['rois'][roi]
                    images.append(pixels(Path(row['path']),row['sha256']))
                rows.append(dict(scene=scene,roi=roi,rgb_mae=float(np.abs(images[0]-images[1]).mean())))
        summary=[]
        for seed in (71,73):
            rr=[r for r in assessment['records'] if r['seed']==seed and r['additional_steps']==step]
            summary.append(dict(seed=seed,regions=len(rr),
                median_fine_luma_std_ratio=float(np.median([r['fine_luma_std_ratio'] for r in rr])),
                median_fine_chroma_std_ratio=float(np.median([r['fine_chroma_std_ratio'] for r in rr])),
                max_absolute_mean_luma_delta=float(max(abs(r['encoded_luma_mean_delta']) for r in rr))))
        result['seed_difference'].append(dict(step=step,regions=rows,median_rgb_mae=float(np.median([r['rgb_mae'] for r in rows])),summary=summary))
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    with (root/'docs/validation/raw'/f'neural-sensor-curve-review-v001-{stamp}.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)
    print('SEED RGB MAE',[(r['step'],r['median_rgb_mae']) for r in result['seed_difference']],flush=True)


if __name__=='__main__':run()
