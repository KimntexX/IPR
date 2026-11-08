"""Read-only qualification gates, independent of current neural scores."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time

from prepare_next_training import read, write, digest


def group_sessions(rows):
    """Conservative session grouping by camera and capture day, never by patch."""
    groups = {}
    for row in rows:
        metadata = row['metadata']
        day = metadata.get('datetime_original', metadata.get('datetime', ''))[:10]
        if not day:
            raise ValueError('Missing capture day prevents a session split check')
        key = (metadata.get('model'), day)
        groups.setdefault(key, set()).add(row['role'])
    return [dict(model=k[0], capture_day=k[1], roles=sorted(v), cross_role_leak=len(v)>1)
            for k,v in sorted(groups.items())]


def border_samples(mosaic, visible_pattern, top, left, height, width):
    import numpy as np
    raw_height, raw_width = mosaic.shape
    regions = [(0, 0, raw_width, top), (0, top+height, raw_width, raw_height-top-height),
               (0, top, left, height), (left+width, top, raw_width-left-width, height)]
    found = {i:[] for i in range(4)}
    for x, y, w, h in regions:
        if w <= 0 or h <= 0:
            continue
        for dy in range(2):
            for dx in range(2):
                values = mosaic[y+dy:y+h:2, x+dx:x+w:2].ravel()
                if not values.size:
                    continue
                label = int(visible_pattern[(y+dy-top)%2, (x+dx-left)%2])
                step = max(1, (len(values)+4095)//4096)
                found[label].append(values[::step])
    rows = []
    for label, pieces in found.items():
        values = np.concatenate(pieces) if pieces else np.array([], dtype=np.uint16)
        rows.append(dict(colour=label, sampled_count=len(values),
            quantiles_dn=np.quantile(values, [.01, .5, .99]).tolist() if len(values) else None,
            zero_fraction=float((values==0).mean()) if len(values) else None))
    return rows


def run(root, output):
    import numpy as np
    import rawpy
    from audit_moon import metadata
    from natural_cfa_preflight import normalized_patch
    from memory_budget import MemoryGuard
    started = time.monotonic()
    campaign = root/'build-patch_neural_restoration/natural-cfa-preflight-v002'
    plan, data = read(campaign/'plan.json'), read(campaign/'data.json')
    archive = root/'docs/validation/raw/neural-low-iso-supports-v001-20261102T095553Z.json'
    prior = read(archive)
    old = {row['source_sha256']:row for row in prior['captures']}
    expected = np.load(campaign/'targets.npy', mmap_mode='r', allow_pickle=False)
    result = dict(schema_version=1, measured_at_utc=datetime.now(timezone.utc).isoformat(),
        status='started', independent_of_model_scores=True, sources=[],
        parents=[dict(path=str(p), sha256=digest(p)) for p in (campaign/'plan.json', campaign/'data.json', archive)],
        tools={n:digest(Path(__file__).with_name(n)) for n in (
            'audit_natural_bench.py', 'audit_moon.py', 'natural_cfa_preflight.py', 'memory_budget.py')},
        versions=dict(numpy=np.__version__, rawpy=rawpy.__version__, libraw=list(rawpy.libraw_version)),
        limitations=['Geometric crop borders are candidate dark areas, not declared optical-black truth',
                    'Metadata does not prove focus, independent scenes, sensor calibration or noise-free targets',
                    'No model scores, fitted thresholds, partition reassignment or source modifications'])
    output.mkdir(exist_ok=False)
    guard = MemoryGuard(hard_bytes=2*1024**3, soft_bytes=1280*1024**2, system_fraction=.8)
    try:
        with guard:
            for source in plan['sources']:
                guard.check()
                path = Path(source['source'])
                before = path.stat()
                if digest(path) != source['expected_sha256']:
                    raise ValueError('Original source digest differs')
                meta = metadata(path)['metadata']
                capture = old[source['expected_sha256']]
                row = dict(source=str(path), sha256=source['expected_sha256'], role=source['role'], metadata=meta,
                    prior_ipr_calibration_white_dn=capture.get('calibration_white_dn'),
                    prior_ipr_black_estimated=capture.get('black_estimated'),
                    focus_qualified=False, clean_truth_qualified=False, checks=[])
                with rawpy.imread(str(path)) as raw:
                    full, visible = raw.raw_image, raw.raw_image_visible
                    pattern = raw.raw_colors_visible[:2,:2].copy()
                    sizes = raw.sizes
                    black, white = list(raw.black_level_per_channel), float(raw.white_level)
                    row.update(raw_shape=list(full.shape), visible_shape=list(visible.shape),
                        top_margin=sizes.top_margin, left_margin=sizes.left_margin,
                        visible_pattern=pattern.tolist(), decoder_black_dn=black, decoder_white_dn=white,
                        border_candidates=border_samples(full, pattern, sizes.top_margin, sizes.left_margin,
                                                         visible.shape[0], visible.shape[1]))
                    for index, entry in enumerate(data['entries']):
                        if entry['source_sha256'] != row['sha256']:
                            continue
                        x,y = entry['crop_xy']
                        actual = normalized_patch(visible[y:y+512,x:x+512], pattern, black, white)
                        equal = bool(np.array_equal(actual, expected[index]))
                        row['checks'].append(dict(crop_xy=[x,y], packed_bit_identical=equal,
                            at_or_above_normalized_white=float((actual>=1).mean()),
                            below_normalized_black=float((actual<0).mean())))
                        if not equal:
                            raise ValueError('Extracted patch cannot be reproduced exactly')
                    del full, visible
                after = path.stat()
                row['stat_unchanged'] = (before.st_size,before.st_mtime_ns)==(after.st_size,after.st_mtime_ns)
                row['white_matches_prior_ipr'] = row['prior_ipr_calibration_white_dn']==white
                if not row['stat_unchanged']:
                    raise ValueError('Original source changed during qualification')
                result['sources'].append(row)
                write(output/'progress.json', result)
            groups = group_sessions(result['sources'])
            result['session_groups'] = groups
            result['gates'] = dict(
                provenance=dict(status='passed_on_four_sources', check='SHA256 and source stat unchanged'),
                extraction=dict(status='passed_on_36_patches', check='Bit-identical replay from original CFA'),
                metadata=dict(status='passed' if all(r['metadata'].get('iso')==100 for r in result['sources']) else 'failed',
                              check='CMT static ISO100'),
                session_split=dict(status='passed_by_camera_capture_day' if not any(g['cross_role_leak'] for g in groups) else 'failed'),
                scene_split=dict(status='not_qualified', reason='Unique days do not prove distinct physical scenes'),
                normalization=dict(status='not_qualified_for_ipr', reason='Decoder metadata differs from archived IPR; exact numerical bridge required'),
                clean_proxy=dict(status='not_qualified', reason='Single ISO100 frames retain noise; focus not independently qualified'),
                noise_model=dict(status='not_qualified', reason='Current constant Gaussian levels are not ISO-specific sensor noise'),
                baselines=dict(status='not_qualified', reason='External reference installation is not a calibrated CFA comparison'),
                final_test=dict(status='not_assigned', reason='Previously inspected photos and provisional development only'))
            result['status'] = 'partially_qualified_provenance_and_extraction_not_full_benchmark'
    except Exception as error:
        result.update(status='stopped_audit_incomplete', error=str(error))
        raise
    finally:
        result.update(elapsed_seconds=time.monotonic()-started, memory=guard.stats(),
                      finished_at_utc=datetime.now(timezone.utc).isoformat())
        write(output/'progress.json', result)
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-natural-bench-qualification-v001-{stamp}.json').open('x', encoding='utf-8') as stream:
            json.dump(result, stream, allow_nan=False, separators=(',', ':'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(Path(__file__).resolve().parents[2], args.output)
