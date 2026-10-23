"""Read-only audit of user-supplied prototype exports; no inference from saved sidecars to export state."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess

from analyse_color_mixer import metadata
from analyse_noise import photographic_metadata
from analysis.native import sha256, signature
from inspect_saved_raw_state import inspect


def audit(base):
    scenes = []
    for scene in ('6V1A9608', '6V1A9890'):
        entries = []
        for name, path in (
            ('LR_proto', base/'bruit/proto'/f'{scene}.tif'),
            ('IPR_proto', base/'bruit/proto'/f'{scene}-IPR.tiff'),
            ('LR_existing_neutral', base/'neutre'/f'{scene}.tif')):
            if not path.is_file():
                entries.append(dict(role=name,path=str(path),status='missing'))
                continue
            before=signature(path)
            entry=dict(role=name, **metadata(path), sha256=sha256(path),
                       photographic=photographic_metadata(path), file_mtime_ns=before[1])
            if before != signature(path):
                raise ValueError('Source changed during audit: '+str(path))
            settings=entry['settings']
            if name=='IPR_proto':
                entry.update(status='user_declared_prototype_not_neutral',
                             export_amount_unknown=True, export_settings_embedded=False,
                             user_declared_optical_corrections=True)
            else:
                entry.update(status='neutral_candidate' if float(settings.get('Sharpness','nan'))==0
                             else 'not_a_neutral_sharpening_active',
                             noise_amounts={key:settings.get(key) for key in ('LuminanceSmoothing','ColorNoiseReduction')})
            entries.append(entry)
        sidecar=base/f'{scene}.CR3.ipr'
        state=inspect(sidecar) if sidecar.is_file() else None
        scenes.append(dict(scene=scene,entries=entries,current_sidecar=state,
            limitations=['Current sidecar is not proof of settings at export time',
                         'LR existing neutral disables optics, proto LR enables them: not matched conditions',
                         'IPR exports are one pixel smaller on both axes: no direct pixel MAE without registration'],
            verdict='paused_before_noise_measurement_pending_matched_LR_states_and_IPR_amount'))
    return scenes


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    now=datetime.now(timezone.utc)
    result=dict(campaign_id='noise-prototype-audit-v001-'+now.strftime('%Y%m%dT%H%M%SZ'),
        schema_version=1,data_version=1,measured_at_utc=now.isoformat(),
        revision=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
        source_sha256=sha256(Path(__file__)),
        configuration=dict(scope='metadata and provenance audit only, no pixel noise measurement',
                           ipr_tiff_use='Explicit user request to compare prototype exports; not a generator measurement'),
        audit=dict(production_modified=False,photographs_modified=False,noise_difference_quantified=False),
        scenes=audit(args.base))
    with args.output.open('x',encoding='utf8') as stream:
        json.dump(result,stream,ensure_ascii=False,allow_nan=False,indent=2)
    print(args.output)
    for scene in result['scenes']:
        print(scene['scene'],scene['verdict'])


if __name__=='__main__':
    main()
