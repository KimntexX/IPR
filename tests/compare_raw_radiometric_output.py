"""Compare radiometric trials with fresh LR highlight/ROI distributions, not pixel alignment."""
import argparse
from datetime import datetime,timezone
from pathlib import Path
import numpy as np
from analysis import native


def distribution(pixels):
    rgb=np.asarray(pixels[::8,::8,:3])
    linear=native.linearize(rgb.astype(float)/65535.)
    y=linear @ np.array([.2126,.7152,.0722])
    height,width=y.shape
    iy,ix=np.indices(y.shape)
    roi=(ix>=width*.25)&(ix<width*.75)&(iy>=height*.48)&(iy<height*.76)
    red=linear[...,0][roi].sum();green=linear[...,1][roi].sum()
    return dict(mean_Y=float(y.mean()),Y_p05_p50_p95=np.percentile(y,[5,50,95],method='lower').tolist(),
                black_percent=float(100*np.mean(y<=.001)),
                encoded_clipped_percent=float(100*np.mean(np.any(rgb>=65534,axis=-1))),
                all_channels_high_percent=float(100*np.mean(np.all(rgb>=65534,axis=-1))),
                central_ROI_G_over_R=float(green/red) if red>0 else None,
                samples=int(y.size))


def select_exports(paths, selections):
    candidates={}
    for path in paths:
        candidates.setdefault(path.name.split('-')[0],[]).append(path)
    for scene,name in selections.items():
        if scene not in candidates or name not in {p.name for p in candidates[scene]}:
            raise ValueError('Unknown explicit IPR reference: '+scene+'='+name)
    chosen={}
    for scene,options in candidates.items():
        if scene in selections:
            chosen[scene]=next(p for p in options if p.name==selections[scene])
        elif len(options)==1:chosen[scene]=options[0]
        else:raise ValueError('Ambiguous IPR export: '+scene+'; select --ipr-reference SCENE=FILENAME')
    return chosen,candidates


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('trials',type=Path)
    parser.add_argument('--ipr-audit-exports',type=Path,help='Compare current manual *_RAW_Audit TIFFs with LR and numerical witness')
    parser.add_argument('--ipr-reference',action='append',default=[],metavar='SCENE=FILENAME',help='Explicit selection when several audit exports exist for a scene')
    args=parser.parse_args()
    memory=native.read_json(args.trials);rows=[];inputs=[dict(file=str(args.trials),sha256=native.sha256(args.trials))]
    records=memory['records'];export_paths={};selection_audit={}
    if args.ipr_audit_exports:
        selections={}
        for item in args.ipr_reference:
            scene,separator,name=item.partition('=')
            if not separator or scene in selections:raise ValueError('Invalid or repeated --ipr-reference')
            selections[scene]=name
        paths=[p for p in sorted(args.ipr_audit_exports.iterdir()) if p.suffix.lower() in ('.tif','.tiff') and 'raw_audit' in p.name.lower()]
        export_paths,candidates=select_exports(paths,selections)
        selection_audit={scene:dict(selected=str(export_paths[scene]),explicit=scene in selections,
                                   candidates=[dict(file=str(p),sha256=native.sha256(p)) for p in options])
                         for scene,options in candidates.items()}
        for scene in export_paths:
            if scene not in {row['scene'] for row in records}:raise ValueError('No LR witness for '+scene)
        if not export_paths:raise ValueError('No manual RAW_Audit TIFF exports')
        records=[row for row in records if row['scene'] in export_paths]
    elif args.ipr_reference:raise ValueError('--ipr-reference requires --ipr-audit-exports')
    for row in records:
        ref=row['LR_reference'];path=Path(ref['LR_file']);print('Checking LR highlights',row['scene'],flush=True)
        pixels,metadata,before=native.inspect(path)
        if metadata['sha256']!=ref['LR_sha256']:raise ValueError('LR changed since trial')
        stats=distribution(pixels);native.verify_unchanged(path,before,metadata['sha256']);del pixels
        inputs.append(dict(file=str(path),sha256=metadata['sha256']))
        record=dict(scene=row['scene'],camera=row['camera'],LR=stats,LR_metadata=metadata,
                         model_deltas={name:dict(clipping_percentage_points=value['encoded_clipped_percent']-stats['encoded_clipped_percent'],
                                                mean_Y_relative_percent=100*(value['mean_Y']/stats['mean_Y']-1),
                                                central_ROI_G_over_R_difference=value['central_ROI_G_over_R']-stats['central_ROI_G_over_R'] if stats['central_ROI_G_over_R'] is not None else None)
                                       for name,value in row['models'].items()})
        if export_paths:
            path=export_paths[row['scene']];print('Checking IPR audit export',path.name,flush=True)
            pixels,metadata,before=native.inspect(path);ipr=distribution(pixels)
            declaration=native.embedded_metadata(path)[0]
            native.verify_unchanged(path,before,metadata['sha256']);del pixels
            inputs.append(dict(file=str(path),sha256=metadata['sha256']))
            witness=row['models']['white_exposure_hue']
            record.update(IPR=ipr,IPR_metadata=metadata,IPR_declaration=declaration,
                          export_deltas=dict(mean_Y_relative_to_LR_percent=100*(ipr['mean_Y']/stats['mean_Y']-1),
                                             mean_Y_relative_to_witness_percent=100*(ipr['mean_Y']/witness['mean_Y']-1),
                                             clipping_to_LR_percentage_points=ipr['encoded_clipped_percent']-stats['encoded_clipped_percent'],
                                             all_channels_high_to_LR_percentage_points=ipr['all_channels_high_percent']-stats['all_channels_high_percent']))
        rows.append(record)
    if native.sha256(args.trials)!=inputs[0]['sha256']:raise ValueError('Trial memory changed')
    now=datetime.now(timezone.utc);prefix='analyse-exports-raw-audit' if export_paths else 'comparaison-radiometrie-lr'
    previous=sorted(Path('docs/validation/raw').glob(prefix+'-v[0-9][0-9][0-9]-*.json'))
    version=int(previous[-1].stem.split('-v')[-1][:3])+1 if previous else 1
    campaign=f'{prefix}-v{version:03d}-'+now.strftime('%Y%m%dT%H%M%S%fZ')
    result=dict(campaign_id=campaign,schema_version=1,data_version=version,group='base-colorimetrique',
                supersedes_campaign_id=previous[-1].stem if previous else None,
                measured_at_utc=now.isoformat(),inputs=inputs,analyser_sha256=native.sha256(Path(__file__)),
                native_reader_sha256=native.sha256(Path('tests/analysis/native.py')),records=rows,IPR_selection=selection_audit,
                units=dict(Y='relative linear sRGB luminance',clipping='percent of samples with at least one code >=65534',
                           differences='percentage points for clipping; relative percent for mean Y'),
                method=dict(sample_step=8,quantiles='lower interpolation',ROI='normalized fixed central rectangle, not semantic'),
                limitations=['Independent image geometries; similar clipping rates do not prove same pixels or lost detail',
                             'No pixel MAE or DeltaE, no semantic ROI correspondence guaranteed',
                             'No physical colour calibration or IHM validation',
                             'Manual IPR TIFFs may not declare settings/calibration; filename is not proof',
                             'Only current supplied file bytes analysed; prior recipes not inferred' if export_paths else 'No manual IPR export used'])
    target=Path('docs/validation/raw')/(campaign+'.json');native.write_immutable(target,result);print(target)


if __name__=='__main__':main()
