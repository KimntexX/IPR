#!/usr/bin/env python3
"""Prepare schema-4 model/mode rules with explicit proposed mode bounds and evidence."""
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
    seen = set()
    rules = measure.get('model_rules')
    if not isinstance(rules, list) or not rules:
        raise ValueError('Explicit model_rules/mode bounds required; never infer a universal camera policy from one file')
    for scene in measure['scenes']:
        # Do not enable an unmeasured scene simply because its metadata agree.
        if not scene.get('comparison_available'):
            continue
        if scene['raw_sha256'] in seen: raise ValueError('Duplicate source evidence')
        seen.add(scene['raw_sha256'])
        row = next(x for x in manifest['images'] if Path(x['raw']).stem == scene['scene'] and x['variant'] == 'baseline')
        dng = next(x for x in audit['scenes'] if x['scene'] == scene['scene'])
        if (file_hash(local_path(row['raw'])) != scene['raw_sha256'] or
                row['raw_sha256'] != scene['raw_sha256'] or dng['raw_sha256'] != scene['raw_sha256'] or
                file_hash(local_path(dng['path'])) != scene['dng_sha256'] or dng['sha256'] != scene['dng_sha256']):
            raise ValueError('RAW/DNG evidence mismatch')
        matches = [r for r in rules if r['camera_model'] == row['camera'] and
                   r['white_level'] == scene['dng_white'] and r['baseline_exposure_ev'] == scene['dng_baseline_exposure_ev']]
        if len(matches) != 1: raise ValueError('Missing/ambiguous proposed model rule')
        rule = dict(matches[0], camera_make=dng['ifd0']['make'], profile_sha256=digest,
                    format_extension=Path(row['raw']).suffix[1:].lower(),
                    decoder_id='libraw', decoder_version=measure['libraw_version'])
        for key in ('black_min', 'black_max', 'native_white_min', 'native_white_max', 'provenance'):
            if key not in rule: raise ValueError('Missing mode bound/provenance')
        if rule not in entries: entries.append(rule)
    if not entries: raise ValueError('Missing measured scenes')
    return {'schema': 4, 'entries': entries}


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
    print(len(result['entries']), 'proposed model/mode rules written:', args.output)


if __name__ == '__main__':
    main()
