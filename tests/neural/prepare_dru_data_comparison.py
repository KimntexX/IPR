"""Seal matched reference/noise/details arms before any new training."""
from datetime import datetime,timezone,timedelta
import json
from pathlib import Path
from campaign_artifacts import sha256
from dru_comparison_policy import ARMS,texture_parameters,scenario_for


def run():
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    output=build/'dru-data-comparison-plan-v001';output.mkdir(exist_ok=False)
    original=build/'dru-memory-plan-v001/plan.json'
    old=json.loads(original.read_text(encoding='utf-8'))
    scenes=json.loads(Path(old['noise_source']).read_text(encoding='utf-8'))['scenes']
    tools=list(old['tools'])+['dru_comparison_policy.py']
    hashes={name:sha256(root/'tests/neural'/name) for name in tools}
    started=datetime.now(timezone.utc);training_deadline=(started+timedelta(hours=3)).isoformat()
    deadline=(started+timedelta(hours=3,minutes=15)).isoformat()
    manifest=dict(schema_version=1,status='sealed_matched_dru_data_comparison',created_at_utc=started.isoformat(),
        source_plan_sha256=sha256(original),data_seed=71,model_seeds=[71,73],steps_per_seed=256,
        arms=[],campaign_deadline_utc=deadline,
        fixed=['Architecture32643840parameters','Core variance-normalized MSE','Adam1e-4','Four microbatches per update',
            'Source scene noise laws and Gaussian seeds','All existing known strong controls','Targets shared across model initializations'],
        policy='All arms and seeds retained; no best checkpoint/seed selection, no new reserve, no photo rendering, no publication.',
        texture_preview=[],noise_preview=[],
        supervisor_tools={name:sha256(root/'tests/neural'/name) for name in ('run_dru_data_comparison.py','summarize_dru_data_comparison.py','run_dru_overnight.py')})
    for arm in ARMS:
        plan=dict(old,data_arm=arm,data_seed=71,tools=hashes,created_at_utc=started.isoformat(),
            training_deadline_utc=training_deadline,campaign_deadline_utc=deadline,
            purpose='Matched single-factor data comparison, not photographic qualification',
            reason='User authorizes reference/noise/details comparison after read-only audit; day campaign, not former overnight deadline.',
            revision_reason='Fixed common data_seed independent of model initialization; single declared factor per arm.')
        path=output/f'{arm}.json';path.write_text(json.dumps(plan,indent=2)+'\n',encoding='utf-8')
        manifest['arms'].append(dict(name=arm,path=str(path.resolve()),sha256=sha256(path)))
    for serial in range(1024):
        identity=280000000+71*100000+serial
        if serial%8 in (0,1):
            p=texture_parameters(('weak_achromatic_wave','chromatic_wave')[serial%8],identity,serial)
            manifest['texture_preview'].append(dict(serial=serial,parameters=p))
        scenario=scenario_for(serial,identity,scenes,'noise')
        reference=scenario_for(serial,identity,scenes,'reference')
        manifest['noise_preview'].append(dict(serial=serial,changed=scenario is not reference,
            parameters=None if scenario is None else scenario['parameters']))
    path=output/'plan.json';path.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    stamp=started.strftime('%Y%m%dT%H%M%SZ')
    with (root/'docs/validation/raw'/f'neural-dru-data-comparison-plan-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
        json.dump(manifest,stream,indent=2,allow_nan=False)
    print(path)


if __name__=='__main__':run()
