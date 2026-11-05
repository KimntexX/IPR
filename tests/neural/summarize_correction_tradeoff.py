"""Descriptive fixed-grid summaries; never select a deployable strength."""
import argparse
from collections import defaultdict
from datetime import datetime,timezone
import json
from pathlib import Path
from statistics import mean

from diagnose_train_learnability import digest,save


def summarize_group(rows,flats,parents,scale):
    if not rows or not flats:
        raise ValueError('Both fine and constant observations required')
    def variant(row):
        found=[v for v in row['variants'] if v['scale']==scale]
        if len(found)!=1:
            raise ValueError('Exactly one fixed-grid variant required')
        return found[0]
    groups=defaultdict(list)
    for row in rows:
        if row['case']['amplitude']==.06:
            groups[row['case']['frequency'],row['case']['chromatic']].append(row)
    if not groups:
        raise ValueError('Strong fine structures required')
    transfers=[]
    for group in groups.values():
        for channel in range(3):
            gain=mean(variant(r)['rgb']['sine_gain'][channel] for r in group)
            parent=mean(parents[r['field']['case_index']]['rgb']['sine_gain'][channel] for r in group)
            if parent<=0:
                raise ValueError('Positive parent transfer reference required')
            transfers.append(gain/parent)
    return dict(scale=scale,fine_observations=len(rows),constant_observations=len(flats),
        mean_rgb_mse=mean(variant(r)['rgb']['rgb_mse'] for r in rows),
        mean_strong_rgb_mse=mean(variant(r)['rgb']['rgb_mse'] for r in rows if r['case']['amplitude']==.06),
        minimum_strong_transfer_over_parent=min(transfers),
        mean_constant_cfa_mse_over_noisy=mean(variant(r)['cfa']['mse_over_noisy'] for r in flats),
        mean_constant_cfa_spatial_variance=mean(variant(r)['cfa']['spatial_variance'] for r in flats))


def run(root,source,output):
    base=root/'build-patch_neural_restoration'
    if not all(p.resolve().is_relative_to(base.resolve()) for p in (source,output)):
        raise ValueError('Dedicated neural paths required')
    data=json.loads(source.read_text(encoding='utf-8'))
    if data['status']!='completed_known_correction_tradeoff_not_qualification':
        raise ValueError('Complete fixed-grid measurement required')
    result=dict(schema_version=1,status='completed_descriptive_correction_tradeoff_summary_not_qualification',
        measured_at_utc=datetime.now(timezone.utc).isoformat(),source_sha256=digest(source),
        script_sha256=digest(Path(__file__)),groups=[],
        limits=['Means/minima over the declared phase0 draw0 subset; not the full fine qualification.',
            'No fitted/interpolated strength, no selected checkpoint, no changed threshold.',
            'Constant CFA error and transfer are separate from total fine RGB error; no claim of universal dominance.'])
    for seed in (71,73):
        for level in (1,2):
            parent_rows=[r for r in data['records'] if r['arm']=='control' and r['seed']==seed and r['field']['noise_index']==level]
            parents={r['field']['case_index']:next(v for v in r['variants'] if v['scale']==0.) for r in parent_rows}
            group=dict(seed=seed,noise_index=level,control=[],stratified=[])
            for arm,scales in (('control',data['control_scales']),('stratified',data['candidate_scales'])):
                rows=[r for r in data['records'] if r['arm']==arm and r['seed']==seed and r['field']['noise_index']==level]
                flats=[r for r in data['flat_records'] if r['arm']==arm and r['seed']==seed and r['noise_index']==level]
                group[arm]=[summarize_group(rows,flats,parents,s) for s in scales]
            result['groups'].append(group)
    output.mkdir(parents=True,exist_ok=False)
    save(output/'result.json',result)
    identity='neural-correction-tradeoff-summary-v001-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    save(root/'docs/validation/raw'/f'{identity}.json',result)
    print(identity)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    run(Path(__file__).resolve().parents[2],args.source,args.output)
