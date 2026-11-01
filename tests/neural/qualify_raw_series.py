"""Read-only CFA temporal qualification; not a noise calibration or training set.

Fixed 3x3 grid, 128x128 samples per Bayer plane. No black subtraction, gains,
registration, interpolation, rejection or learned thresholds. Temporal dispersion
also contains lighting changes and motion; fixed-pattern noise is not removed.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import time

import numpy as np
import rawpy

from audit_moon import digest


GROUPS = ((2, 'indoor', 134, 151), (3, 'indoor', 152, 180),
          (4, 'indoor', 181, 194), (5, 'indoor', 195, 209),
          (7, 'vegetation', 211, 243), (8, 'vegetation', 244, 262),
          (9, 'vegetation', 263, 280), (10, 'vegetation', 281, 300))


def grid_rois(shape, side=128):
    """Coordinates in Bayer cells, common extent of all four planes."""
    height, width = shape[0]//2, shape[1]//2
    if side < 2 or min(height, width) < 2*side:
        raise ValueError('Insufficient common Bayer extent')
    return [dict(x=int(x*width)-side//2, y=int(y*height)-side//2,
                 width=side, height=side)
            for y in (.2, .5, .8) for x in (.2, .5, .8)]


def extract_rois(mosaic, rois):
    if mosaic.ndim != 2 or mosaic.dtype != np.uint16:
        raise ValueError('Expected a two-dimensional uint16 CFA')
    planes = [mosaic[y::2, x::2] for y in range(2) for x in range(2)]
    tiles = []
    for roi in rois:
        x, y, w, h = (roi[k] for k in ('x', 'y', 'width', 'height'))
        if min(x, y) < 0 or w != h or w < 2 or any(
                y+h > p.shape[0] or x+w > p.shape[1] for p in planes):
            raise ValueError('Invalid CFA-plane ROI')
        tiles.append(np.stack([p[y:y+h, x:x+w] for p in planes]))
    return np.stack(tiles)


def temporal_summary(stack):
    """Input T,R,C,H,W in raw DN, without resampling or radiometric correction."""
    if stack.ndim != 5 or stack.shape[0] < 4 or not np.isfinite(stack).all():
        raise ValueError('Expected at least four finite temporal samples')
    data = stack.astype(np.float64)
    temporal_var = data.var(axis=0, ddof=1)
    split_delta = data[::2].mean(axis=0)-data[1::2].mean(axis=0)
    frame_means = data.mean(axis=(-2, -1))
    rows = []
    for roi in range(stack.shape[1]):
        for plane in range(stack.shape[2]):
            means = frame_means[:, roi, plane]
            # Consecutive pair difference is divided by sqrt(2); it is not
            # declared independent sensor noise in a moving/flickering scene.
            differences = np.diff(data[:, roi, plane], axis=0)
            rows.append(dict(roi=roi, plane=plane,
                mean_dn=float(means.mean()),
                frame_mean_min_dn=float(means.min()), frame_mean_max_dn=float(means.max()),
                frame_mean_range_dn=float(np.ptp(means)),
                temporal_rms_dn=float(np.sqrt(temporal_var[roi, plane].mean())),
                adjacent_difference_rms_over_sqrt2_dn=float(np.sqrt(np.mean(differences**2)/2)),
                interleaved_half_mean_rmse_dn=float(np.sqrt(np.mean(split_delta[roi, plane]**2)))))
    return rows


def run(inventory_path, output):
    root = Path(__file__).resolve().parents[2]
    output = output.resolve()
    if not output.is_relative_to((root/'build-patch_neural_restoration').resolve()):
        raise ValueError('Output must be in the dedicated neural build')
    inventory = json.loads(inventory_path.read_text(encoding='utf-8'))
    records = {Path(r['path']).stem: r for r in inventory['records']}
    selected = [records[f'6V1A{number:04d}'] for _, _, first, last in GROUPS
                for number in range(first, last+1)]
    for record in selected:
        path = Path(record['path'])
        stat = path.stat()
        if stat.st_size != record['bytes'] or stat.st_mtime_ns != record['mtime_ns']:
            raise ValueError('Source changed since inventory: '+str(path))
    output.mkdir(parents=True, exist_ok=False)
    start = time.monotonic()
    result = dict(schema='ipr.neural.raw-series-qualification.v1',
        data_version=1, tool_sha256=digest(Path(__file__)),
        created_utc=datetime.now(timezone.utc).isoformat(),
        git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        inventory=dict(path=str(inventory_path), sha256=digest(inventory_path)),
        protocol=dict(domain='unprocessed visible CFA codes', units='DN',
            grid='3x3 centers at 20/50/80 percent, 128x128 samples per Bayer plane',
            plane_order='row-major visible 2x2 CFA pattern, recorded for every source',
            resampling=False, black_subtraction=False, frame_exclusions=[],
            reference_split='even/odd frame indices within each burst, diagnostic only',
            limitations=['Motion and illumination changes contribute to temporal dispersion.',
                'No production black/white calibration; reported white is informational only.',
                'No clean target, local-motion validation, training or model selection.',
                'All ISO variants of each composition must remain in the same neural partition.']),
        versions=dict(numpy=np.__version__,rawpy=rawpy.__version__,libraw=rawpy.libraw_version), groups=[])
    completed = 0
    for group_id, scene, first, last in GROUPS:
        tiles, frames, expected_shape, expected_pattern = [], [], None, None
        for number in range(first, last+1):
            record = records[f'6V1A{number:04d}']
            path = Path(record['path'])
            before = path.stat()
            sha = digest(path)
            if record.get('source_full_sha256') and sha != record['source_full_sha256']:
                raise ValueError('Source hash changed: '+str(path))
            with rawpy.imread(str(path)) as raw:
                mosaic = raw.raw_image_visible
                shape = list(mosaic.shape)
                pattern = raw.raw_pattern.tolist()
                if expected_shape is None:
                    expected_shape, expected_pattern = shape, pattern
                    rois = grid_rois(shape)
                if shape != expected_shape or pattern != expected_pattern:
                    raise ValueError('Inconsistent CFA geometry in group')
                tile = extract_rois(mosaic, rois)
                tiles.append(tile)
                frames.append(dict(path=str(path), sha256=sha, size_bytes=before.st_size,
                    mtime_ns=before.st_mtime_ns, metadata=record['metadata'],
                    visible_shape_hw=shape, pattern=pattern,
                    colour_description=raw.color_desc.decode('ascii'),
                    reported_black_unvalidated=raw.black_level_per_channel,
                    reported_white_unvalidated=int(raw.white_level),
                    roi_plane_mean_dn=tile.mean(axis=(-2,-1)).tolist(),
                    roi_plane_at_reported_white_fraction=(tile >= raw.white_level).mean(axis=(-2,-1)).tolist()))
            after = path.stat()
            if (before.st_size,before.st_mtime_ns) != (after.st_size,after.st_mtime_ns):
                raise ValueError('Source modified during read: '+str(path))
            completed += 1
            print(f'{completed}/{len(selected)} {path.name}', flush=True)
        result['groups'].append(dict(id=group_id,scene=scene,frames=frames,
            rois_bayer_cells=rois, temporal=temporal_summary(np.stack(tiles))))
        # Preserve completed-group progress if the campaign is interrupted.
        with (output/f'group-{group_id}.json').open('x',encoding='utf-8') as handle:
            json.dump(result['groups'][-1],handle,indent=2,allow_nan=False)
    result['elapsed_seconds'] = time.monotonic()-start
    result['source_count'] = completed
    with (output/'audit.json').open('x',encoding='utf-8') as handle:
        json.dump(result,handle,indent=2,allow_nan=False)
    print('COMPLETE '+str(output/'audit.json'),flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inventory',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args = parser.parse_args()
    run(args.inventory,args.output)
