#!/usr/bin/env python3
"""Prepare opt-in per-file Camera Neutral calibration from controlled evidence.

Not a general camera calibration database. No DCP payloads are copied.
"""
import argparse
import json
from pathlib import Path

from analyse_base_colorimetrique_v2 import file_hash
from investigate_dcp_order import require_profile
from investigate_tonal_base import local_path, read_dcp


def build(measure, manifest, audit, profile):
    require_profile(read_dcp(profile))
    if not measure.get('diagnostic_only'):
        raise ValueError('Expected controlled diagnostic evidence')
    digest = file_hash(profile)
    if digest not in measure['inputs_sha256'].values():
        raise ValueError('Profile differs from measured profile')
    entries = []
    for scene in measure['scenes']:
        # Do not enable an unmeasured scene simply because its metadata agree.
        if not scene.get('comparison_available'):
            continue
        row = next(x for x in manifest['images'] if Path(x['raw']).stem == scene['scene'] and x['variant'] == 'baseline')
        dng = next(x for x in audit['scenes'] if x['scene'] == scene['scene'])
        if (file_hash(local_path(row['raw'])) != scene['raw_sha256'] or
                row['raw_sha256'] != scene['raw_sha256'] or dng['raw_sha256'] != scene['raw_sha256'] or
                file_hash(local_path(dng['path'])) != scene['dng_sha256'] or dng['sha256'] != scene['dng_sha256']):
            raise ValueError('RAW/DNG evidence mismatch')
        entries.append({'camera_make': dng['ifd0']['make'], 'camera_model': row['camera'],
                        'iso': row['iso'], 'raw_sha256': scene['raw_sha256'],
                        'profile_sha256': digest, 'white_level': scene['dng_white'],
                        'black_rgbg': scene['black_rgbg'],
                        'baseline_exposure_ev': scene['dng_baseline_exposure_ev'],
                        'dng_sha256': scene['dng_sha256'],
                        'provenance': 'Adobe DNG Converter 18.6; controlled Neutral experiment 2026-10-08; '+scene['scene']})
    if len({x['raw_sha256'] for x in entries}) != len(entries) or not entries:
        raise ValueError('Missing or duplicate measured scenes')
    return {'schema': 1, 'libraw_version': measure['libraw_version'], 'entries': entries}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('measure', 'probe', 'audit', 'profile', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Refuse to overwrite calibration')
    paths = [args.measure, args.probe/'manifest.json', args.audit]
    measure, manifest, audit = [json.loads(x.read_text(encoding='utf-8')) for x in paths]
    for path in paths[1:]:
        if measure['inputs_sha256'].get(str(path)) != file_hash(path):
            raise ValueError('Controlled dependency changed: '+str(path))
    result = build(measure, manifest, audit, args.profile)
    result['evidence_sha256'] = {str(path):file_hash(path) for path in paths}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2, allow_nan=False)
        stream.write('\n')
    print(len(result['entries']), 'audited RAW/profile pairs written:', args.output)


if __name__ == '__main__':
    main()
