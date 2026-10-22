#!/usr/bin/env python3
"""Read DNG scalar calibration/metadata, not image data or proprietary LUTs."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from PIL import Image
from PIL.TiffImagePlugin import ImageFileDirectory_v2

from analyse_base_colorimetrique_v2 import file_hash
from investigate_tonal_base import read_dcp

TAGS = {271: 'make', 272: 'model', 305: 'software', 50708: 'unique_camera_model',
        50723: 'camera_calibration_1', 50724: 'camera_calibration_2',
        50727: 'analog_balance', 50728: 'as_shot_neutral',
        50721: 'color_matrix_1', 50722: 'color_matrix_2',
        50713: 'black_level_repeat_dim', 50715: 'black_level_delta_h',
        50716: 'black_level_delta_v', 50778: 'calibration_illuminant_1',
        50779: 'calibration_illuminant_2',
        33421: 'cfa_repeat_pattern_dim', 33422: 'cfa_pattern',
        50730: 'baseline_exposure', 50731: 'baseline_noise',
        50936: 'profile_name', 50964: 'forward_matrix_1', 50965: 'forward_matrix_2',
        51109: 'baseline_exposure_offset', 51110: 'default_black_render',
        256: 'width', 257: 'height', 262: 'photometric',
        50714: 'black_level', 50717: 'white_level', 50829: 'active_area'}


def plain(value):
    if value is None or isinstance(value, (str, int)):
        return value
    if isinstance(value, (tuple, list, bytes)):
        return [plain(x) for x in value]
    return float(value)


def audit(path, profile_tags=None):
    with Image.open(path) as image:
        root = {name: plain(image.tag_v2.get(tag)) for tag, name in TAGS.items()}
        offsets = image.tag_v2.get(330, ())
        checks = {}
        if profile_tags is not None:
            for tag in (50964, 50965, 50981, 50982, 51108, 50940, 51109):
                actual = image.tag_v2.get(tag)
                actual = actual if isinstance(actual, tuple) else (actual,)
                expected = profile_tags.get(tag)
                if expected is None or any(x is None for x in actual):
                    raise ValueError('Missing expected embedded profile tag')
                encode_values = lambda values: struct.pack('<'+'d'*len(values), *map(float, values))
                first, second = encode_values(actual), encode_values(expected)
                checks[str(tag)] = {'count': len(actual), 'equal_numeric_values': first == second,
                    'dng_numeric_sha256': hashlib.sha256(first).hexdigest(),
                    'dcp_numeric_sha256': hashlib.sha256(second).hexdigest()}
    if len(offsets) > 32:
        raise ValueError('Unexpected SubIFD count')
    children = []
    with path.open('rb') as stream:
        header = stream.read(8)
        for offset in offsets:
            if not 8 <= offset < path.stat().st_size:
                raise ValueError('SubIFD outside file')
            stream.seek(offset)
            directory = ImageFileDirectory_v2(ifh=header)
            directory.load(stream)
            children.append({name: plain(directory.get(tag)) for tag, name in TAGS.items()
                             if directory.get(tag) is not None})
    return {'path': str(path), 'sha256': file_hash(path), 'ifd0': root, 'sub_ifds': children,
            'embedded_profile_checks': checks}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--raw-folder', type=Path, required=True)
    parser.add_argument('--converter', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--profile', type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Refuse to overwrite evidence')
    records = []
    paths = sorted(args.input.glob('*.dng'))
    if not paths:
        parser.error('No DNG files')
    for path in paths:
        raw = args.raw_folder/(path.stem+'.CR3')
        record = audit(path, read_dcp(args.profile) if args.profile else None)
        record.update(scene=path.stem, raw_path=str(raw), raw_sha256=file_hash(raw))
        records.append(record)
        print(path.stem, record['ifd0']['baseline_exposure'], record['ifd0']['profile_name'])
    result = {'schema': 1, 'diagnostic_only': True, 'analyser_sha256': file_hash(Path(__file__)),
              'converter_path': str(args.converter), 'converter_sha256': file_hash(args.converter),
              'conversion_options': ['-c', '-p0', '-d', str(args.input)],
              'limitations': 'Adobe converter interpretation, not a physical sensor calibration. Root plus direct SubIFDs; LUT contents not exported. DNG is not a Lightroom TIFF.',
              'scenes': records}
    if args.profile:
        result['profile_path'] = str(args.profile)
        result['profile_sha256'] = file_hash(args.profile)
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2, allow_nan=False)
        stream.write('\n')


if __name__ == '__main__':
    main()
