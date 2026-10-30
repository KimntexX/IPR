"""Audit frozen CFA signal/variance characterization against signed captures, without refitting."""
import argparse
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path, PureWindowsPath
import subprocess

os.environ['OPENBLAS_NUM_THREADS'] = '1'
import numpy as np
from audit_dark_mean import sha256
from capture import load_capture
from study_structure_prediction import authenticated_selection


PHASES = dict(R=(0,0,0), G1=(0,1,1), G2=(1,0,3), B=(1,1,2))
FILES = dict(law='loi-variance-noir-fige-bruit-v001-20261028T101443Z.json',
    blacks='qualification-noirs-repetes-bruit-v001-20261028T100350Z.json',
    scenes='qualification-rafales-carte-bruit-v001-20261028T094754Z.json',
    transfer='transfert-loi-noir-fige-bruit-v001-20261028T101957Z.json')


def normalize_coefficients(slope, dark_variance, reference_black, runtime_black, gain, divisor):
    values = (slope,dark_variance,reference_black,runtime_black,gain,divisor)
    if not all(math.isfinite(v) for v in values) or slope < 0 or dark_variance <= 0 or gain <= 0 or divisor <= 0:
        raise ValueError('Invalid CFA normalization contract')
    return slope*gain/divisor, (dark_variance+slope*(runtime_black-reference_black))*gain**2/divisor**2


def verify_parents(document, history):
    verified = []
    for parent in document.get('parents',[]):
        path = history/PureWindowsPath(parent['path']).name
        if sha256(path) != parent['sha256']:
            raise ValueError(f'Changed historical parent: {path.name}')
        verified.append(dict(path=str(path),sha256=parent['sha256']))
    return verified


def match_moments(record, historical_frame):
    manifest,stages = load_capture(record['path'])
    if manifest['source']['sha256'] != historical_frame['sha256'] or manifest['mode'] != 'signed':
        raise ValueError('Current/historical source mismatch')
    unpacked = sorted((key for key in stages if key[0]=='unpacked_cfa'),key=lambda k:k[1][1])
    if len(unpacked) != 3:
        raise ValueError('Expected same three fixed CFA crops')
    rows = []
    for roi,key in enumerate(unpacked):
        descriptor,pixels = stages[key]
        old = historical_frame['rois'][roi]
        context = descriptor['context']
        if (list(key[1]) != old['mosaic_yx'] or descriptor['unit'] != 'DN'
            or list(pixels.shape) != [256,256,1] or context['visible_size'] != historical_frame['shape']
            or context['cfa'] != historical_frame['cfa'] or context['input_white'] != historical_frame['white']):
            raise ValueError('Historical/current geometry, CFA, white or units differ')
        if not np.all(pixels == np.rint(pixels)):
            raise ValueError('Unpacked DN no longer exact integers')
        for phase,(dy,dx,colour) in PHASES.items():
            if context['cfa'][dy][dx] != colour:
                raise ValueError('This characterization needs its explicit RG/G2B phase geometry')
            plane = pixels[dy::2,dx::2,0].astype(float)
            prior = next(p for p in old['planes'] if p['offset']==[dy,dx])
            mean,sd = float(plane.mean()),float(plane.std())
            if abs(mean-prior['mean_dn']) > 1e-12 or abs(sd-prior['sd_dn']) > 1e-12:
                raise ValueError('Current raw moments differ from historical calibration domain')
            rows.append(dict(source_sha256=historical_frame['sha256'],roi=roi,phase=phase,
                origin_yx=list(key[1]),mean_dn=mean,sd_dn=sd,
                historical_mean_dn=prior['mean_dn'],historical_sd_dn=prior['sd_dn']))
    return rows,manifest['applied']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--history-root',type=Path,required=True)
    parser.add_argument('--pilot-proof',type=Path,required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    history = args.history_root/'docs/validation/raw'
    parents = {key:json.loads((history/name).read_text(encoding='utf-8')) for key,name in FILES.items()}
    if parents['law']['verdict'] != 'exploratory_fixed_dark_cfa_law_not_physical_noise_profile_validation':
        raise ValueError('Unexpected law status')
    verified = {key:verify_parents(parents[key],history) for key in ('law','transfer')}
    law_hash = sha256(history/FILES['law'])
    if not any(p['sha256']==law_hash for p in verified['transfer']):
        raise ValueError('Transfer does not reference this frozen law')
    if parents['blacks']['parent']['sha256'] != sha256(history/FILES['scenes']):
        raise ValueError('Dark/scene historical lineage mismatch')
    selection = authenticated_selection(root,args.pilot_proof)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    freeze = dict(schema_version=2,created_utc=stamp,study='signed_CFA_signal_noise_bridge',
        supersedes='signal-noise-bridge-v001-20261030T143139Z.json',
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        parents={key:dict(path=str(history/name),sha256=sha256(history/name)) for key,name in FILES.items()},
        verified_lineage=verified,pilot_sha256=sha256(args.pilot_proof),current_selection=selection,
        sources={s:dict(sha256=sha256(root/s),text=(root/s).read_text(encoding='utf-8')) for s in
                 ('tests/raw_bench/audit_signal_noise_bridge.py','tests/raw_bench/capture.py')},
        protocol='No new fitting or RAW decoding. Audit same-source unpacked moments, frozen CFA coefficients, local dark anchors, supported domains and normalization equations.',
        limits=['Historical scenes/controls were already measured, not new independent validation.',
                'Moment identity is not full historical pixel parity.',
                'b and black anchors remain local to ROI, camera and session; no universal intercept or ISO interpolation.',
                'Normalized coefficients are PRE-RCD CFA, never CameraDenoise RGB/B3 sigma.',
                'Transfer extrema do not constitute a continuously validated signal domain.'])
    frozen = root/'docs/validation/raw'/f'signal-noise-bridge-freeze-v002-{stamp}.json'
    with frozen.open('x',encoding='utf-8') as handle:
        json.dump(freeze,handle,ensure_ascii=False,indent=2,allow_nan=False)
    result = dict(schema_version=2,created_utc=stamp,freeze_path=str(frozen.relative_to(root)),
        freeze_sha256=sha256(frozen),runtime_registration=False,models=[],moment_checks=[],transfer_points=[],
        normalization_witnesses=[],status='research_only_not_runtime_characterization')
    try:
        historical_development = {}
        for group in parents['blacks']['records']:
            for index in group['development_indices']:
                frame = group['decoded_frames'][index]
                historical_development[frame['sha256']] = frame
        contexts = {}
        for pair in selection:
            iso = int(pair['label'].split('ISO')[1])
            for capture in pair['captures']:
                identity = capture['source']['sha256']
                if identity not in historical_development:
                    raise ValueError('Pilot capture not in frozen dark development partition')
                checks,context = match_moments(capture,historical_development[identity])
                result['moment_checks'].extend(checks)
                if iso in contexts and contexts[iso] != context:
                    raise ValueError('Normalization witness contexts differ')
                contexts[iso] = context
        refs = {(g['iso'],r['roi'],r['plane']):r for g in parents['blacks']['records'] for r in g['measurements']}
        scenes = {g['group_index']:g for g in parents['scenes']['records']}
        cameras = {g['metadata']['model'] for g in scenes.values()}
        cameras.update(frame['metadata']['model'] for frame in historical_development.values())
        if cameras != {'Canon EOS R6 Mark III'}:
            raise ValueError('Unqualified camera or mixed camera corpus')
        expected = {(iso,phase) for iso in (1600,3200,6400,12800,25600,51200) for phase in PHASES}
        if {(r['iso'],r['plane']) for r in parents['law']['records']} != expected:
            raise ValueError('Unexpected characterized ISO/phase scope')
        for model in parents['law']['records']:
            iso,phase = model['iso'],model['plane']
            slope = model['model']['nonnegative_slope_dn']
            groups = sorted({p['group_index'] for p in model['points']})
            row = dict(camera='Canon EOS R6 Mark III',iso=iso,phase=phase,
                slope_dn=slope,unit='variance DN^2, signal DN above local reference black; slope DN',
                fit_group_count=len(groups),fit_signal_range_dn=model['model']['signal_range_dn'],
                leave_group_out_status=model['leave_one_group_out']['status'],
                old_held_relative_mae=model['leave_one_group_out'].get('held_relative_mae'),
                regions=[],frozen_measurements=model['points'])
            for roi in range(3):
                ref = refs[(iso,roi,phase)]
                b = ref['development']['centered_temporal_variance_dn2']
                black = ref['development_black_dn']
                row['regions'].append(dict(roi=roi,reference_black_dn=black,temporal_dark_variance_dn2=b,
                    reference_offset=ref['offset'],black_population='development only'))
            for point in model['points']:
                ref = refs[(iso,point['roi'],phase)]
                scene = scenes[point['group_index']]
                measured = next(r for r in scene['planes'] if r['roi']==point['roi'] and r['plane']==phase)
                s = float(np.mean(measured['frame_means_dn']))-ref['development_black_dn']
                if (not np.isclose(s,point['signal_dn'],rtol=0,atol=1e-12)
                    or measured['centered_temporal_variance_dn2'] != point['variance_dn2']
                    or ref['development']['centered_temporal_variance_dn2'] != point['dark_variance_dn2']):
                    raise ValueError('Frozen point/scene/dark mismatch')
            matches = [r for r in parents['transfer']['records'] if r['iso']==iso and r['plane']==phase]
            row['historical_transfer'] = dict(measurements=len(matches),
                exact_exposure_measurements=sum(r['matches_1_over_200'] for r in matches),
                signal_extrema_dn=[min(r['frozen']['signal_dn'] for r in matches),max(r['frozen']['signal_dn'] for r in matches)] if matches else None,
                note='Previously inspected extrapolation evidence, not refitted or continuous validation')
            if iso in contexts:
                context = contexts[iso]
                _,_,colour = PHASES[phase]
                gain,divisor = context['gains'][colour],context['divisor']
                runtime_black = context['black'][colour]
                for region in row['regions']:
                    a,b = normalize_coefficients(slope,region['temporal_dark_variance_dn2'],region['reference_black_dn'],runtime_black,gain,divisor)
                    witness = dict(iso=iso,phase=phase,roi=region['roi'],gain=gain,divisor=divisor,
                        runtime_black_dn=runtime_black,reference_black_dn=region['reference_black_dn'],
                        normalized_slope=a,normalized_intercept=b,
                        formula='Var(X)=A*E[X]+B, X=g*(DN-runtimeBlack)/D, CFA before RCD',checks=[])
                    for s in (0.,*model['model']['signal_range_dn']):
                        x = gain*(s+region['reference_black_dn']-runtime_black)/divisor
                        direct = (region['temporal_dark_variance_dn2']+slope*s)*gain**2/divisor**2
                        normalized = a*x+b
                        if not np.isclose(normalized,direct,rtol=1e-12,atol=1e-16):
                            raise ValueError('Normalized variance transport failed')
                        witness['checks'].append(dict(signal_above_reference_dn=s,expected_normalized_signal=x,
                            direct_variance=direct,coefficient_variance=normalized))
                    example_x = .03
                    example_s = divisor*example_x/gain+runtime_black-region['reference_black_dn']
                    witness['example_at_camera_CFA_point03'] = dict(signal_dn=example_s,
                        inside_fit_domain=model['model']['signal_range_dn'][0] <= example_s <= model['model']['signal_range_dn'][1],
                        total_sigma_over_dark_sigma=math.sqrt(1+slope*example_s/region['temporal_dark_variance_dn2']),
                        note='Frozen-law evaluation, not a measured RGB/B3 noise ratio; may extrapolate')
                    result['normalization_witnesses'].append(witness)
            result['models'].append(row)
        for record in parents['transfer']['records']:
            result['transfer_points'].append({key:record[key] for key in
                ('iso','plane','roi','pair_id','source_sha256','exposure_s','matches_1_over_200',
                 'frozen_slope_dn','frozen_black_dn','frozen_dark_variance_dn2','frozen',
                 'training_signal_range_dn','outside_training_signal_domain')})
        if len(result['moment_checks']) != 48 or len(result['models']) != 24:
            raise ValueError('Incomplete bridge contract')
        result['coverage'] = dict(camera='Canon EOS R6 Mark III',isos=sorted({r['iso'] for r in result['models']}),
            fit_groups=len(scenes),fit_frames=sum(g['frames'] for g in scenes.values()),
            fit_moments=sum(len(r['frozen_measurements']) for r in result['models']),
            temporal_dark_frames=sum(len(g['development_indices']) for g in parents['blacks']['records']),
            fit_exposures_s=sorted({g['metadata']['exposure_s'] for g in scenes.values()}),
            transfer_measurements=len(result['transfer_points']),
            transfer_same_exposure_measurements=sum(r['matches_1_over_200'] for r in result['transfer_points']),
            prediction_ready_for_CameraDenoise=False)
        result['verdict'] = 'audited_frozen_CFA_law_and_signed_domain_not_production_noise_profile'
    except Exception as exc:
        result['verdict'] = 'fail'
        result['error'] = f'{type(exc).__name__}: {exc}'
    output = root/'docs/validation/raw'/f'signal-noise-bridge-v002-{stamp}.json'
    with output.open('x',encoding='utf-8') as handle:
        json.dump(result,handle,ensure_ascii=False,indent=2,allow_nan=False)
    print(output,result['verdict'],result.get('error',''),flush=True)
    print(json.dumps(result.get('coverage',{})),flush=True)
    return int(result['verdict']=='fail')


if __name__ == '__main__':
    raise SystemExit(main())
