"""Immutable synthesis of fresh RAW probes and the audited neutral corpus."""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import statistics
from collections import defaultdict

from analysis import native
from audit_canon_metadata import audit
from investigate_tonal_base import profile_summary


def summarize(probes, inventory, reference_choices=None):
    reference_choices = reference_choices or {}
    records = {p['record']['scene']:p['record'] for p in probes}
    expected = {r['scene'] for r in inventory['RAW']}
    source_hashes = {r['scene']:r['sha256'] for r in inventory['RAW']}
    if set(records)!=expected or len(records)!=len(probes):
        raise ValueError('Missing or duplicate RAW probe')
    comparisons, missing, effects = [], [], []
    for scene in sorted(records):
        record = records[scene]
        if record.get('sha256')!=source_hashes[scene]:
            raise ValueError('RAW source hash mismatch: '+scene)
        if record['status']!='measured' or record.get('sample_replay_max_abs_float_error',float('inf'))>2e-6:
            raise ValueError('Failed RAW / DCP replay: '+scene)
        stages = record['stages']
        for name in ('hue_preserving_curve','display_5200_tint5_diagnostic'):
            current, candidate = stages['current'],stages[name]
            effects.append(dict(scene=scene,model=name,
                output_Y_ratio=candidate['output']['mean_Y']/current['output']['mean_Y'],
                ROI_G_over_R_current=current['flower_ROI']['G_over_R_of_means'],
                ROI_G_over_R_candidate=candidate['flower_ROI']['G_over_R_of_means'],
                new_encoded_clipped_percent=candidate['encoded_clipped_percent']-current['encoded_clipped_percent']))
        references = []
        for r in inventory['neutral_exports']:
            if r.get('scene')!=scene or r['status']!='measured':
                continue
            xmp = r['metadata'].get('xmp_settings',{})
            if xmp.get('CameraProfile')!='Camera Neutral':
                continue
            # Optical-off neutral references only. Keep WB differences explicit.
            if xmp.get('LensProfileEnable','0')!='0':
                continue
            if any(float(xmp.get(k,0))!=0 for k in ('Exposure2012','Vibrance','Saturation','Contrast2012','Shadows2012','Blacks2012','Whites2012','Highlights2012','Texture','Clarity2012','Dehaze','Sharpness','LuminanceSmoothing','ColorNoiseReduction','AutoLateralCA')):
                continue
            references.append(r)
        candidates = references
        if scene in reference_choices:
            references = [r for r in candidates if Path(r['file']).name==reference_choices[scene]]
            if len(references)!=1:
                raise ValueError('Explicit LR reference is not uniquely eligible: '+scene)
        if len(references)!=1:
            missing.append(dict(scene=scene,eligible_references=len(references),reason='No unique compatible stored LR neutral'))
            continue
        ref = references[0]
        lrY = ref['distribution']['whole_region']['mean_Y']
        comparisons.append(dict(scene=scene,LR_file=ref['file'],LR_sha256=ref['metadata']['sha256'],
            camera=record.get('metadata',{}).get('camera','unknown'),
            reference_selection='explicit newly supplied export' if scene in reference_choices else 'unique audited eligible reference',
            reference_candidates=[dict(file=r['file'],sha256=r['metadata']['sha256']) for r in candidates],
            LR_WB=ref['metadata']['xmp_settings'].get('WhiteBalance'),
            LR_T=ref['metadata']['xmp_settings'].get('Temperature'),LR_tint=ref['metadata']['xmp_settings'].get('Tint'),
            IPR_reference_kelvin=record['reference_kelvin'],LR_Y=lrY,
            WB_equivalence_verified=False,geometry='independent whole-image distributions, not pixel aligned',
            models={name:dict(mean_Y=stages[name]['output']['mean_Y'],
                    ratio_to_LR=stages[name]['output']['mean_Y']/lrY,
                    delta_EV=math.log2(stages[name]['output']['mean_Y']/lrY))
                    for name in ('current','hue_preserving_curve','display_5200_tint5_diagnostic')}))
    values = [c['models']['current']['ratio_to_LR'] for c in comparisons]
    hue = [r['output_Y_ratio'] for r in effects if r['model']=='hue_preserving_curve']
    cohorts = defaultdict(list)
    for comparison in comparisons:
        cohorts[comparison['camera']].append(comparison['models']['current'])
    return dict(RAW_count=len(records),comparisons=comparisons,missing_LR=missing,ablation_effects=effects,
                descriptive=dict(current_to_LR_scene_mean_relative_percent=100*(statistics.mean(values)-1) if values else None,
                                 current_to_LR_scene_median_relative_percent=100*(statistics.median(values)-1) if values else None,
                                 hue_curve_Y_ratio_min=min(hue),hue_curve_Y_ratio_max=max(hue)),
                camera_cohorts={camera:dict(scenes=len(rows),
                     mean_relative_percent=100*(statistics.mean(r['ratio_to_LR'] for r in rows)-1),
                     mean_log_ratio_EV=statistics.mean(r['delta_EV'] for r in rows))
                     for camera,rows in cohorts.items()},
                decision=dict(production_modified=False,photographic_validation=False,
                              colour_tuning_suspended=True,automatic_cleanup=False),
                limitations=['Independent distributions, differing geometry: no inter-engine MAE or pixel DeltaE',
                             'WB and exact profile application are not physically equated by equal display numbers',
                             'Custom WB references remain diagnostic, not As Shot validation',
                             'No LR reference for missing scenes: no fidelity claim on them'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inventory',type=Path)
    parser.add_argument('--raw-dir',type=Path,default=Path('docs/validation/raw'))
    parser.add_argument('--lr-reference',action='append',default=[],metavar='SCENE=FILENAME',
                        help='Explicit audited reference choice when a scene has several exports')
    args = parser.parse_args()
    inventory = native.read_json(args.inventory)
    paths = sorted(args.raw_dir.glob('inspection-ouverture-scene-v001-*.json'))
    probes = [native.read_json(p) for p in paths]
    for probe in probes:
        for source,digest in probe.get('source_hashes',{}).items():
            if native.sha256(Path(source)) != digest:
                raise ValueError('Stored RAW probe code changed; new native measurement required: '+source)
    choices = {}
    for choice in args.lr_reference:
        scene, separator, name = choice.partition('=')
        if not separator or not scene or not name or scene in choices:
            parser.error('Expected a unique SCENE=FILENAME per explicit choice')
        choices[scene] = name
    result = summarize(probes,inventory,choices)
    # Independent MakerNotes extraction; offsets are candidates, not certified semantics.
    result['canon_metadata'] = [audit(Path(r['path'])) for r in inventory['RAW']]
    profiles = {}
    for probe in probes:
        record = probe['record']
        path = Path(record['profile_path'])
        if native.sha256(path)!=record['profile_sha256']:
            raise ValueError('Stored RAW probe DCP changed: '+str(path))
        if str(path) not in profiles:
            profiles[str(path)] = profile_summary(path)
    result['DCP_profiles'] = list(profiles.values())
    now = datetime.now(timezone.utc)
    previous = sorted(args.raw_dir.glob('synthese-ouverture-v[0-9][0-9][0-9]-*.json'))
    version = int(previous[-1].name.split('-')[2][1:])+1 if previous else 1
    campaign = f'synthese-ouverture-v{version:03d}-'+now.strftime('%Y%m%dT%H%M%S%fZ')
    result.update(campaign_id=campaign,schema_version=1,data_version=version,group='base-colorimetrique',
                  supersedes_campaign_id=previous[-1].stem if previous else None,
                  protocol='docs/validation/protocols/protocole-raw-profils.md',
                  protocol_sha256=native.sha256(Path('docs/validation/protocols/protocole-raw-profils.md')),
                  units={'Y':'relative linear sRGB luminance','ratio':'dimensionless',
                         'percent':'100*(ratio-1) or delta clipped fraction percentage points',
                         'EV':'log2(IPR mean linear Y / LR mean linear Y)',
                         'G_over_R':'ratio of mean linear channels; not a perceptual error'},
                  workflow={'branch':'patch_raw_opening','base_commit':'7297197',
                            'colour_checkpoint_branch':'patch_color','colour_checkpoint_commit':'8abcd12'},
                  measured_at_utc=now.isoformat(),analyser_sha256=native.sha256(Path(__file__)),
                  canon_auditor_sha256=native.sha256(Path('tests/audit_canon_metadata.py')),
                  DCP_auditor_sha256=native.sha256(Path('tests/investigate_tonal_base.py')),
                  inputs=[dict(file=str(p),sha256=native.sha256(p)) for p in [args.inventory]+paths])
    native.write_immutable(args.raw_dir/(campaign+'.json'),result)
    print(campaign,result['RAW_count'],'RAW',len(result['comparisons']),'LR comparisons',len(result['missing_LR']),'missing')
    for row in result['comparisons']:
        print(row['scene'],row['LR_WB'],{name:round(100*(m['ratio_to_LR']-1),2) for name,m in row['models'].items()})


if __name__=='__main__':
    main()
