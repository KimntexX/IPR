#!/usr/bin/env python3
"""Immutable, audited intermediate shadows/highlights campaign (native TIFF)."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path

from analyse_color_mixer import metadata
from analyse_exposition import neutral_file, pixels
from analyse_lumieres import FIELDS, measure, setting_warnings

SCENES = ('6V1A0007', '6V1A0037', '6V1A0043', '6V1A0327', '6V1A9421', '6V1A9950')


def fingerprint(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(4 * 1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path('tests/image/base'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Published JSON already exists; choose a new timestamp')
    now = datetime.now(timezone.utc).isoformat()
    result = dict(campaign_id=args.output.stem, schema_version=1, data_version=1,
                  measured_at_utc=now, analyser_sha256=fingerprint(Path(__file__)),
                  protocol='native-srgb-linear-Y-quantiles-v1', step=8,
                  software_version='manual exports: unknown build; no IPR settings manifest',
                  scenes=list(SCENES), records=[], neutrals=[], missing=[],
                  units={'Y': 'linear sRGB relative luminance, normalized 0..1',
                         'percent': '100 * qualifying samples / sampled pixel count'},
                  thresholds={'changed_Y': 2e-5, 'black_Y': .001, 'clip_encoded': 1-1/65535},
                  zones='per-engine neutral Y: bottom20, middle40-60, top20; ties included',
                  geometry='within-engine only; no cross-engine pixel registration/MAE',
                  ipr_provenance='suffix paired with LR XMP; not a certified IPR settings audit')
    def info(path):
        data = metadata(path)
        data.update(sha256=fingerprint(path), modified_at_utc=datetime.fromtimestamp(
            path.stat().st_mtime, timezone.utc).isoformat())
        return data
    for scene in SCENES:
        baselines = {}
        for engine in ('LR', 'IPR'):
            path = neutral_file(args.root, scene, engine)
            baselines[engine] = (pixels(path, 8), info(path))
            result['neutrals'].append(dict(scene=scene, engine=engine, metadata=baselines[engine][1]))
        for feature in ('ombre', 'hl'):
            for number in range(1, 9):
                suffix = '' if number == 1 else f'-{number}'
                lr = args.root/feature/f'{scene}{suffix}.tif'
                if not lr.exists():
                    result['missing'].append(str(lr))
                    continue
                lr_info = info(lr)
                value = float(lr_info['settings'][FIELDS[feature]])
                for engine in ('LR', 'IPR'):
                    path = lr if engine == 'LR' else args.root/feature/f'{scene}-IPR{suffix}.tiff'
                    if not path.exists():
                        result['missing'].append(str(path))
                        continue
                    variant_info = lr_info if engine == 'LR' else info(path)
                    base, base_info = baselines[engine]
                    warnings = setting_warnings(variant_info, base_info, FIELDS[feature], value)
                    changed = pixels(path, 8)
                    record = dict(scene=scene, feature=feature, engine=engine, number=number,
                                  value=value, metadata=variant_info, warnings=warnings,
                                  sampled_pixels=int(base.shape[0]*base.shape[1]),
                                  settings_verified=engine == 'LR' and not warnings,
                                  measurement=measure(base, changed, value, feature))
                    result['records'].append(record)
                    print(scene, feature, engine, number, value, warnings, flush=True)
    # Never overwrite an immutable published campaign, including concurrent runs.
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x', encoding='utf-8') as target:
        json.dump(result, target, ensure_ascii=False, indent=2, allow_nan=False)
        target.write('\n')
    print('Published', args.output, len(result['records']), 'records', flush=True)


if __name__ == '__main__':
    main()
