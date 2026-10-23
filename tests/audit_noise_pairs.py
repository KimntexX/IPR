"""Read-only CR3 exposure-pair audit; diagnostic DN, no physical calibration."""
import argparse
from functools import lru_cache
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct
import subprocess

import numpy as np
import rawpy

from audit_canon_metadata import boxes, CANON_UUID

TAGS = {0x10f: 'make', 0x110: 'model', 0x132: 'datetime', 0x829a: 'exposure_s',
        0x829d: 'aperture', 0x8827: 'iso', 0x8832: 'recommended_iso',
        0x920a: 'focal_mm', 0xa434: 'lens'}


def tiff_fields(data):
    if len(data) < 8 or data[:2] not in (b'II', b'MM'):
        raise ValueError('Invalid TIFF header')
    e = '<' if data[:2] == b'II' else '>'
    if struct.unpack_from(e + 'H', data, 2)[0] != 42:
        raise ValueError('Not classic TIFF')
    offset = struct.unpack_from(e + 'I', data, 4)[0]
    if offset + 2 > len(data):
        raise ValueError('IFD outside buffer')
    count = struct.unpack_from(e + 'H', data, offset)[0]
    if offset + 2 + 12 * count + 4 > len(data):
        raise ValueError('Truncated IFD')
    result = {}
    for i in range(count):
        entry = offset + 2 + 12 * i
        tag, kind, n, pointer = struct.unpack_from(e + 'HHII', data, entry)
        if tag not in TAGS or kind not in (2, 3, 4, 5) or n > 1024:
            continue
        size = {2: 1, 3: 2, 4: 4, 5: 8}[kind] * n
        start = entry + 8 if size <= 4 else pointer
        if start + size > len(data) or n == 0:
            raise ValueError('Invalid field extent')
        payload = data[start:start + size]
        if kind == 2:
            value = payload.split(b'\0')[0].decode('ascii', 'replace')
        elif kind in (3, 4):
            value = struct.unpack_from(e + ('H' if kind == 3 else 'I'), payload)[0]
        else:
            a, b = struct.unpack_from(e + 'II', payload)
            value = a / b if b else None
        result[TAGS[tag]] = value
    return result


def metadata(data):
    result = {}
    def walk(a, b):
        for kind, first, last in boxes(data, a, b):
            if kind in (b'CMT1', b'CMT2'):
                result.update(tiff_fields(data[first:last]))
            elif kind in (b'moov', b'trak', b'mdia', b'minf', b'stbl'):
                walk(first, last)
            elif kind == b'uuid' and data[first:first + 16] == CANON_UUID:
                walk(first + 16, last)
    walk(0, len(data))
    return result


def matching(a, b):
    required = ('model', 'iso', 'exposure_s', 'aperture', 'focal_mm', 'lens')
    return all(a.get(k) is not None and a.get(k) == b.get(k) for k in required)


def pair_stats(a, b):
    difference = (a - b) / np.sqrt(2)
    variance = float(np.var(difference, ddof=1))
    means = [float(a.mean()), float(b.mean())]
    relative_drift = abs(means[0] - means[1]) / max(abs(np.mean(means)), 1)
    # Compare observed large-scale difference to expected white-noise block mean variance.
    block_means = difference.reshape(8, 16, 8, 16).mean(axis=(1, 3))
    structure_ratio = float(np.var(block_means, ddof=1) * 256 / max(variance, 1e-20))
    lag = float(np.mean((difference[:, 1:] - difference.mean()) *
                        (difference[:, :-1] - difference.mean())) / max(variance, 1e-20))
    return dict(means_dn=means, signal_mean_dn=float(np.mean(means)),
                noise_variance_dn2=variance, mean_difference_dn=means[0] - means[1],
                relative_drift=relative_drift, difference_block_structure_ratio=structure_ratio,
                difference_horizontal_lag1=lag,
                status='candidate_stable' if relative_drift <= .01 and structure_ratio <= 4 else 'reject_drift_or_structure')


def read_frame(path, root, declared_dark=False):
    before = path.stat()
    data = path.read_bytes()
    item = dict(path=str(path.relative_to(root)), sha256=hashlib.sha256(data).hexdigest(),
                metadata=metadata(data), declared_dark=declared_dark)
    del data
    with rawpy.imread(str(path)) as raw:
        if raw.raw_pattern is None or raw.raw_pattern.shape != (2, 2):
            raise ValueError('Only Bayer 2x2 supported')
        m = raw.raw_image_visible
        h, w = m.shape
        if h < 256 or w < 256:
            raise ValueError('Insufficient native dimensions for audit')
        desc = raw.color_desc.decode('ascii')
        pattern = raw.raw_pattern.copy()
        item.update(shape=[h, w], black=list(raw.black_level_per_channel),
                    white=int(raw.white_level), cfa=pattern.tolist(), rois=[])
        crops = []
        for fx in (.2, .5, .8):
            y, x = (h // 2 - 128) // 2 * 2, (int(w * fx) - 128) // 2 * 2
            crop = m[y:y + 256, x:x + 256].copy().astype(float)
            crops.append(crop)
            item['rois'].append(dict(mosaic_yx=[y, x], size=[256, 256],
                planes=[dict(name=desc[int(pattern[dy, dx])], offset=[dy, dx],
                             mean_dn=float(crop[dy::2, dx::2].mean()),
                             sd_dn=float(crop[dy::2, dx::2].std()),
                             saturation_fraction=float((crop[dy::2, dx::2] >= .98 * raw.white_level).mean()))
                        for dy, dx in ((0, 0), (0, 1), (1, 0), (1, 1))]))
    after = path.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ValueError('Source changed during audit: ' + str(path))
    means = [p['mean_dn'] for roi in item['rois'] for p in roi['planes']]
    tolerance = max(.01 * float(np.median(means)), 8)
    item['dark_like_not_confirmed'] = max(means) - min(means) <= tolerance
    return item, crops


@lru_cache(maxsize=512)
def capture_metadata(path, size, modified_ns):
    return metadata(path.read_bytes())


def input_paths(root, directory):
    iso = int(directory.name.split('-')[1])
    paths = []
    # Group logically by actual ISO without moving incorrectly stored sources.
    for folder in sorted((root / 'protocole').glob('iso-*')):
        for path in sorted(folder.glob('*.CR3')):
            stat = path.stat()
            if capture_metadata(path, stat.st_size, stat.st_mtime_ns).get('iso') == iso:
                paths.append((path, False))
    for path in sorted((root / 'protocole' / 'blacks').glob('*.CR3')):
        stat = path.stat()
        if capture_metadata(path, stat.st_size, stat.st_mtime_ns).get('iso') == iso:
            paths.append((path, True))
    return paths


def audit(root):
    groups = []
    for directory in sorted((root / 'protocole').glob('iso-*')):
        if not directory.is_dir():
            continue
        frames, pixels = [], {}
        for path, declared_dark in input_paths(root, directory):
            item, crops = read_frame(path, root, declared_dark)
            item['folder_iso_matches'] = item['metadata'].get('iso') == int(directory.name.split('-')[1])
            item['storage_folder_iso_matches'] = (None if declared_dark else
                item['metadata'].get('iso') == int(path.parent.name.split('-')[1]))
            pixels[item['path']] = crops
            frames.append(item)
        # Neighboring pairs plus all matching-exposure alternatives (including distant darks).
        pairs = []
        candidates = [(a, b) for i, a in enumerate(frames) for j, b in enumerate(frames)
                      if j > i and (j == i + 1 or matching(a['metadata'], b['metadata']))]
        for left, right in candidates:
            same = matching(left['metadata'], right['metadata'])
            pair = dict(left=left['path'], right=right['path'], matching_exposure_metadata=same,
                        matching_geometry=left['shape'] == right['shape'] and left['cfa'] == right['cfa'],
                        matching_decoder_levels=left['black'] == right['black'] and left['white'] == right['white'])
            pair['measurements'] = []
            pair['declared_dark_pair'] = left['declared_dark'] and right['declared_dark']
            pair['mixed_capture_roles'] = left['declared_dark'] != right['declared_dark']
            pair['both_dark_like_not_confirmed'] = left['dark_like_not_confirmed'] and right['dark_like_not_confirmed']
            if same and pair['matching_geometry'] and left['white'] == right['white']:
                for roi, (a, b) in enumerate(zip(pixels[left['path']], pixels[right['path']])):
                    for dy, dx in ((0, 0), (0, 1), (1, 0), (1, 1)):
                        stats = pair_stats(a[dy::2, dx::2], b[dy::2, dx::2])
                        sat = max(left['rois'][roi]['planes'][2*dy+dx]['saturation_fraction'],
                                  right['rois'][roi]['planes'][2*dy+dx]['saturation_fraction'])
                        if sat > .001:
                            stats['status'] = 'reject_saturation'
                        pair['measurements'].append(dict(roi=roi, offset=[dy, dx], **stats))
            pair['status'] = ('candidate_not_certified_dark_or_flat' if pair['measurements'] and
                              any(s['status'] == 'candidate_stable' for s in pair['measurements'])
                              else 'rejected')
            pairs.append(pair)
        groups.append(dict(folder=directory.name, frames=frames, pair_candidates=pairs))
        print(directory.name, len(frames), 'frames;', sum(p['status'] != 'rejected' for p in pairs), 'candidate pairs', flush=True)
    return groups


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--previous', type=Path, help='Previous immutable audit for relocation tracking')
    args = parser.parse_args()
    if not args.corpus.is_dir() or args.output.exists():
        parser.error('Missing corpus or output already exists')
    now = datetime.now(timezone.utc)
    previous = json.loads(args.previous.read_text(encoding='utf8')) if args.previous else None
    result = dict(campaign_id='noise-pairs-v005-' + now.strftime('%Y%m%dT%H%M%SZ'),
                  schema_version=1, data_version=5, measured_at_utc=now.isoformat(),
                  supersedes_campaign_id=previous['campaign_id'] if previous else None,
                  revision_reason='Group captures by actual metadata ISO and include shared declared blacks; no physical moves',
                  revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
                  source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  metadata_reader_sha256=hashlib.sha256(Path(__file__).with_name('audit_canon_metadata.py').read_bytes()).hexdigest(),
                  versions=dict(numpy=np.__version__, rawpy=rawpy.__version__, libraw=list(rawpy.libraw_version)),
                  configuration=dict(iso_assignment='CR3 metadata, not storage folder; mismatches recorded without moving sources',
                                     shared_black_folder='protocole/blacks; user-declared cap-on, ISO from CR3 metadata; declaration is not physical certification',
                                     pairing='adjacent plus non-adjacent exact-exposure metadata matches within each folder; alternatives overlap, not independent',
                                     crops='three 256x256 central-height mosaic squares at x=20/50/80%',
                                     noise_variance='sample variance of (A-B)/sqrt(2), no bias subtraction needed for this statistic',
                                     drift_limit=.01, structure_ratio_limit=4, saturation_fraction_limit=.001,
                                     saturation_reference='>=98% of rawpy.white_level; conservative near-white exclusion, not a certified clipping threshold',
                                     dark_like='all 12 ROI/plane means differ by <=max(1% median,8DN); not proof of cap-on capture'),
                  audit=dict(production_modified=False, physical_coefficients_fitted=False,
                             dark_frame_identity_confirmed=False, flat_field_identity_confirmed=False,
                             actual_saturation_threshold_validated=False, images_modified=False),
                  groups=audit(args.corpus))
    result['relocations_since_previous'] = []
    if previous:
        old_by_hash = {f['sha256']: f['path'] for g in previous['groups'] for f in g['frames']}
        for group in result['groups']:
            for frame in group['frames']:
                old_path = old_by_hash.get(frame['sha256'])
                if old_path and old_path != frame['path']:
                    result['relocations_since_previous'].append(dict(source=old_path, destination=frame['path'],
                                                                     sha256=frame['sha256']))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x', encoding='utf8') as stream:
        json.dump(result, stream, indent=2, allow_nan=False, ensure_ascii=False)
    print(args.output)


if __name__ == '__main__':
    main()
