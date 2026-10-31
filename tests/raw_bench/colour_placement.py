"""Frozen full-frame A/B colour-placement study, no photographic truth claim.

Runs one native server at a time. Per request watchdog kills a stuck server;
immutable per-scene JSON is preserved on failure. No TIFF or RAW is written.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests'))
from analysis.metrics import haar_sigma
from analysis.native import embedded_metadata, inspect, sha256, signature, write_immutable
from analysis.render import RenderServer, available_memory

TILE = 64
LEVELS = (0., .0003, .003, .03, .3, .8)
COLOURS = (25, 50, 100)
PLACEMENTS = ('post', 'pre', 'hybrid')
Y_WEIGHTS = np.array([.2126, .7152, .0722], dtype=np.float32)


def now():
    return datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')


def linear(pixels):
    p = np.asarray(pixels, dtype=np.float32) / 65535.
    return np.where(p <= .04045, p / 12.92, ((p + .055)/1.055)**2.4).astype(np.float32)


def bounds_check(rgb):
    if rgb.ndim != 3 or rgb.shape[2] != 3 or not np.isfinite(rgb).all():
        raise ValueError('Expected finite HWC RGB')


def choose_tiles(rgb):
    """Fixed neutral coordinates: up to 64 flat and 32 structured tiles/bin."""
    bounds_check(rgb)
    candidates = [[] for _ in LEVELS[:-1]]
    for y in range(0, rgb.shape[0]-TILE+1, TILE):
        for x in range(0, rgb.shape[1]-TILE+1, TILE):
            tile = rgb[y:y+TILE, x:x+TILE]
            level = float((tile @ Y_WEIGHTS).mean())
            index = int(np.searchsorted(LEVELS, level, side='right')-1)
            if index < 0 or index >= len(candidates) or tile.max() > .98:
                continue
            smooth = cv2.GaussianBlur(tile @ Y_WEIGHTS, (0, 0), 4.)
            score = float(smooth.std()) / max(level, .001)
            candidates[index].append((score, y, x))
    result = []
    for group, entries in enumerate(candidates):
        entries.sort()
        # No positive-level exclusion: zero black tiles remain eligible.
        n = len(entries)
        nflat = max(1, int(np.ceil(n*.2))) if n else 0
        nedge = max(1, int(np.ceil(n*.2))) if n else 0
        def sample(items, cap):
            if not items:
                return []
            indices = np.linspace(0, len(items)-1, min(cap, len(items))).astype(int)
            return [[int(items[i][1]), int(items[i][2])] for i in indices]
        result.append(dict(bin=group, low=LEVELS[group], high=LEVELS[group+1],
                           candidates=n, flat=sample(entries[:nflat], 64),
                           structured=sample(entries[n-nedge:], 32)))
    return result


def tile_metrics(tile):
    y = tile @ Y_WEIGHTS
    co = tile[..., 0]-tile[..., 2]
    cg = tile[..., 1]-.5*(tile[..., 0]+tile[..., 2])
    inner = np.s_[12:-12, 12:-12]
    def mid(p):
        band = (cv2.GaussianBlur(p, (0, 0), 1.)-cv2.GaussianBlur(p, (0, 0), 4.))[inner]
        return float(np.median(np.abs(band-np.median(band)))/.6744897501960817)
    def edge(p):
        p = cv2.GaussianBlur(p, (0, 0), 1.)
        dx = cv2.Sobel(p, cv2.CV_32F, 1, 0)[inner]
        dy = cv2.Sobel(p, cv2.CV_32F, 0, 1)[inner]
        return float(np.mean(dx*dx+dy*dy))
    center = tile[inner]
    spread = center.max(2)-center.min(2)
    # Absolute display speckles, not an automatic classification as sensor noise.
    is_coloured = spread > .003
    r, g, b = (center[..., c] for c in range(3))
    return dict(Y=float(y[inner].mean()), R=float(r.mean()), G=float(g.mean()), B=float(b.mean()),
                Co=float(co[inner].mean()), Cg=float(cg[inner].mean()),
                luma_HH=haar_sigma(y[inner]),
                chroma_HH=float(np.hypot(haar_sigma(co[inner]), haar_sigma(cg[inner]))/np.sqrt(2)),
                chroma_mid=float(np.hypot(mid(co), mid(cg))/np.sqrt(2)),
                colour_edge=float(np.sqrt(edge(co)+edge(cg))),
                zero_channel_fraction=float(np.mean(center <= 0)),
                coloured_zero_pixel_fraction=float(np.mean((center.min(2) <= 0) & is_coloured)),
                magenta_fraction=float(np.mean(((r+b)*.5-g > .003) & is_coloured)),
                green_fraction=float(np.mean((g-(r+b)*.5 > .003) & is_coloured)))


def measure(rgb, masks):
    bounds_check(rgb)
    rows = []
    for group in masks:
        for kind in ('flat', 'structured'):
            for y, x in group[kind]:
                if y+TILE > rgb.shape[0] or x+TILE > rgb.shape[1]:
                    raise ValueError('Fixed ROI outside state extent')
                rows.append(dict(bin=group['bin'], kind=kind, y=y, x=x,
                                 metrics=tile_metrics(rgb[y:y+TILE, x:x+TILE])))
    return rows


def aggregate(rows, neutral):
    if len(rows) != len(neutral):
        raise ValueError('Unmatched fixed masks')
    if any(tuple(r[k] for k in ('bin', 'kind', 'y', 'x')) != tuple(n[k] for k in ('bin', 'kind', 'y', 'x'))
           for r, n in zip(rows, neutral)):
        raise ValueError('Fixed ROI identities changed')
    result = []
    for group in range(len(LEVELS)-1):
        for kind in ('flat', 'structured'):
            pairs = [(r['metrics'], n['metrics']) for r, n in zip(rows, neutral)
                     if r['bin'] == group and r['kind'] == kind]
            if not pairs:
                result.append(dict(bin=group, kind=kind, tiles=0, metrics=None))
                continue
            metrics = {}
            for name in pairs[0][0]:
                state = np.array([a[name] for a, b in pairs])
                base = np.array([b[name] for a, b in pairs])
                valid = base > 1e-12
                metrics[name] = dict(median=float(np.median(state)),
                                     median_delta=float(np.median(state-base)),
                                     median_ratio=float(np.median(state[valid]/base[valid])) if valid.any() else None,
                                     ratio_tiles=int(valid.sum()))
            result.append(dict(bin=group, kind=kind, tiles=len(pairs), metrics=metrics))
    return result


def number(settings, key):
    return float(settings.get(key) or 0)


def audit(base, wanted=None):
    raws = sorted(p for p in base.iterdir() if p.suffix.lower() in ('.cr2', '.cr3'))
    if len(raws) != 28:
        raise ValueError(f'Expected frozen 28 RAWs, found {len(raws)}')
    all_records = []
    for raw in raws:
        neutral = base/'neutre'/f'{raw.stem}.tif'
        files = [neutral]
        found = {}
        for path in sorted((base/'bruit').glob(raw.stem+'*.tif')):
            if path.stem.split('-')[0] != raw.stem:
                continue
            _, settings = embedded_metadata(path)
            if number(settings, 'LuminanceSmoothing') != 0 or number(settings, 'Sharpness') != 0:
                continue
            colour = int(number(settings, 'ColorNoiseReduction'))
            if colour not in COLOURS:
                continue
            if colour in found:
                raise ValueError(f'Duplicate colour state {raw.stem} {colour}')
            found[colour] = path
        if set(found) != set(COLOURS):
            raise ValueError(f'Missing LR colour state {raw.stem}: {sorted(found)}')
        files.extend(found[c] for c in COLOURS)
        references = []
        neutral_settings = None
        neutral_dimensions = None
        for index, path in enumerate(files):
            if not path.is_file():
                raise ValueError(f'Missing LR neutral {path}')
            pixels, meta, before = inspect(path)
            del pixels
            settings = meta['xmp_settings']
            if settings.get('CameraProfile') != 'Camera Neutral':
                raise ValueError(f'Wrong LR profile {path}')
            for key in ('LuminanceSmoothing', 'Sharpness', 'LensProfileEnable', 'AutoLateralCA',
                        'Exposure2012', 'Contrast2012', 'Highlights2012', 'Shadows2012',
                        'Whites2012', 'Blacks2012', 'Vibrance', 'Saturation', 'Texture', 'Clarity2012', 'Dehaze'):
                if number(settings, key) != 0:
                    raise ValueError(f'Unexpected LR {key} in {path}')
            if index == 0:
                if number(settings, 'ColorNoiseReduction') != 0:
                    raise ValueError(f'LR neutral has colour denoise {path}')
                neutral_settings = settings
                neutral_dimensions = meta['dimensions']
            if meta['dimensions'] != neutral_dimensions:
                raise ValueError(f'LR state dimensions differ: {path}')
            for key in ('WhiteBalance', 'Temperature', 'Tint', 'CameraProfile', 'ProcessVersion'):
                if settings.get(key) != neutral_settings.get(key):
                    raise ValueError(f'LR state differs from neutral for {key}: {path}')
            references.append(dict(path=str(path), metadata=meta, signature=list(before),
                                   colour=0 if index == 0 else COLOURS[index-1]))
        all_records.append(dict(scene=raw.stem, source=str(raw), source_sha256=sha256(raw),
                                references=references))
    if wanted:
        missing = set(wanted)-{r['scene'] for r in all_records}
        if missing:
            raise ValueError(f'Scenes missing: {missing}')
    return all_records


def request(server, raw, signed, placement, strength, identity=False):
    # RenderServer's blocking pipe reader needs an external per-request watchdog.
    watchdog = threading.Timer(900, server.process.kill)
    watchdog.daemon = True
    watchdog.start()
    try:
        header = server._request(dict(op='render', source=raw, signed=signed,
                                      placement=placement, strength=strength, identityRerender=identity))
        h, w = header['height'], header['width']
        if header['dtype'] != '<u2' or header['bytes'] != h*w*6:
            raise ValueError('Unexpected native layout')
        payload = server._read(header['bytes'])
        for resource in ('profile', 'manifest'):
            if sha256(header[resource+'_path']) != header[resource+'_sha256']:
                raise ValueError(f'Changed {resource} resource')
        return np.frombuffer(payload, dtype='<u2').reshape(h, w, 3), header
    finally:
        watchdog.cancel()
        watchdog.join()


def read_reference(reference):
    path = Path(reference['path'])
    if list(signature(path)) != reference['signature']:
        raise ValueError(f'LR reference changed: {path}')
    pixels, meta, _ = inspect(path)
    if meta['sha256'] != reference['metadata']['sha256']:
        raise ValueError(f'LR reference hash changed: {path}')
    return linear(pixels[..., :3])


def production_hashes(tool, raw):
    """Only retain hashes: never keep two full-resolution masters concurrently."""
    hashes = {}
    with RenderServer(tool, timeout=10) as reference:
        for signed in (True, False):
            for strength in (0, 50):
                watchdog = threading.Timer(900, reference.process.kill)
                watchdog.daemon = True; watchdog.start()
                try:
                    pixels, _ = reference.render(raw, 'neutral', 0,
                        **({'camera_denoise':dict(luminance=0, chrominance=strength)} if signed else
                           {'denoise':dict(luminance=0, chrominance=strength)}))
                    hashes[(signed, strength)] = hashlib.sha256(pixels).hexdigest()
                    del pixels
                finally:
                    watchdog.cancel(); watchdog.join()
    return hashes


def scene_campaign(record, tool, production, outdir):
    start = time.monotonic()
    raw = record['source']
    if sha256(raw) != record['source_sha256']:
        raise ValueError('RAW source changed')
    evidence = dict(schema_version=1, created_utc=now(), scene=record['scene'], inventory=record,
                    masks={}, rows=[], checks=[])
    expected_hashes = production_hashes(production, raw)
    with RenderServer(tool, timeout=10) as server:
        # One shared signed-neutral selection for both A/B and all placements.
        pixels, header = request(server, raw, True, 'post', 0)
        masks = choose_tiles(linear(pixels)); del pixels
        evidence['masks']['ipr'] = masks
        resources = {name: (header[name+'_path'], header[name+'_sha256']) for name in ('profile', 'manifest')}
        for signed in (True, False):
            pixels, header = request(server, raw, signed, 'post', 0)
            if any((header[name+'_path'], header[name+'_sha256']) != value for name, value in resources.items()):
                raise ValueError('Profile/calibration differs between development policies')
            neutral_hash = hashlib.sha256(pixels).hexdigest()
            neutral = measure(linear(pixels), masks)
            # Force an actual identity pre-profile rerender to detect a changed
            # source WB/profile path, not just a skipped zero-strength branch.
            identity, identity_header = request(server, raw, signed, 'pre', 0, True)
            if not np.array_equal(identity, pixels):
                raise ValueError(f'Identity camera rerender differs: {record["scene"]} signed={signed}')
            del identity
            for placement in PLACEMENTS:
                zero, _ = request(server, raw, signed, placement, 0)
                if not np.array_equal(zero, pixels):
                    raise ValueError('Zero strength differs by placement')
                del zero
            del pixels
            for strength in (0, 50):
                actual, _ = request(server, raw, signed, 'post', strength)
                if hashlib.sha256(actual).hexdigest() != expected_hashes[(signed, strength)]:
                    raise ValueError(f'Production post parity failed: {record["scene"]} {signed} {strength}')
                evidence['checks'].append(dict(signed=signed, strength=strength, production_exact=True,
                                              expected_sha256=expected_hashes[(signed, strength)]))
                del actual
            evidence['rows'].append(dict(engine='ipr', signed=signed, placement='post', colour=0,
                                         header=header, pixels_sha256=neutral_hash,
                                         tiles=neutral, summary=aggregate(neutral, neutral)))
            for strength in COLOURS:
                for placement in PLACEMENTS:
                    pixels, header = request(server, raw, signed, placement, strength)
                    values = measure(linear(pixels), masks)
                    evidence['rows'].append(dict(engine='ipr', signed=signed, placement=placement, colour=strength,
                        header=header, pixels_sha256=hashlib.sha256(pixels).hexdigest(),
                        tiles=values, summary=aggregate(values, neutral)))
                    del pixels
                    print(record['scene'], signed, placement, strength, header['elapsed_ms'], flush=True)
            unchanged, _ = request(server, raw, signed, 'post', 0)
            if hashlib.sha256(unchanged).hexdigest() != neutral_hash:
                raise ValueError('Retained neutral source was mutated by a candidate')
            del unchanged
    lr0 = read_reference(record['references'][0])
    lr_masks = choose_tiles(lr0)
    lr_neutral = measure(lr0, lr_masks); del lr0
    evidence['masks']['lr'] = lr_masks
    for ref in record['references']:
        values = measure(read_reference(ref), lr_masks)
        evidence['rows'].append(dict(engine='lr', colour=ref['colour'], tiles=values,
                                     summary=aggregate(values, lr_neutral)))
    evidence['duration_seconds'] = time.monotonic()-start
    evidence['verdict'] = 'measured_no_quality_pass_without_clean_truth'
    output = outdir/f'colour-placement-scene-v001-{record["scene"]}-{now()}.json'
    write_immutable(output, evidence)
    return dict(scene=record['scene'], path=str(output), sha256=sha256(output),
                seconds=evidence['duration_seconds'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--tool', type=Path, required=True)
    parser.add_argument('--production', type=Path, required=True)
    parser.add_argument('--engine-source', type=Path, required=True)
    parser.add_argument('--scene', nargs='+')
    parser.add_argument('--audit-only', action='store_true')
    parser.add_argument('--output-dir', type=Path, default=ROOT/'docs/validation/raw')
    a = parser.parse_args()
    cv2.setNumThreads(1)
    os.environ['OMP_NUM_THREADS'] = '8'
    a.output_dir.mkdir(parents=True, exist_ok=True)
    stamp = now()
    status_path = ROOT/'build-patch_raw_bench/colour-placement/status.json'
    def status(**fields):
        payload = dict(updated_utc=now(), **fields)
        with status_path.open('w', encoding='utf-8') as stream:
            json.dump(payload, stream, ensure_ascii=False, allow_nan=False, indent=2)
    completed = []
    try:
        status(phase='auditing', campaign=stamp)
        inventory = audit(a.base, a.scene)
        paths = [Path(__file__), Path(__file__).with_name('colour_placement_server.cpp'),
                 ROOT/'tests/raw_bench/production/CMakeLists.txt']
        engine_files = sorted(p for p in (a.engine_source/'src').rglob('*') if p.suffix in ('.cpp', '.h'))
        freeze = dict(schema_version=1, created_utc=stamp, inventory=inventory,
            engine_revision=subprocess.check_output(['git', '-C', str(a.engine_source), 'rev-parse', 'HEAD'], text=True).strip(),
            engine_sources={str(p.relative_to(a.engine_source)):sha256(p) for p in engine_files},
            bench_revision=subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
            sources={str(p.relative_to(ROOT)):dict(sha256=sha256(p), text=p.read_text(encoding='utf-8')) for p in paths},
            tools={str(p):sha256(p) for p in (a.tool, a.production)},
            protocol=dict(tile=TILE, levels=LEVELS, colours=COLOURS, luminance=0, sharpening=0, optics=False,
                flat_fraction=.2, max_flat=64, max_structured=32, interior=40, pixel_scale=1,
                hybrid='pre chroma weighted from smoothed camera I: full below .002, zero above .02; then full post chroma',
                masks='IPR signed neutral for both modes/placements; LR own neutral; mean tile bins; include zero blacks',
                limitations=['No clean photographic truth; flat/textured metrics contain real structure.',
                             'Camera and post filters differ; slider equality is not perceptual strength equality.',
                             'Hybrid cascades filters and is one frozen exploratory design, not an optimum.',
                             'One session, existing repeatedly examined corpus; not a sealed holdout.',
                             'Cross-engine ratios are within-engine, not aligned pixel comparisons.',
                             'Luminance=0 isolates chroma: combined luminance interactions not qualified.']))
        freeze_path = a.output_dir/f'colour-placement-freeze-v001-{stamp}.json'
        write_immutable(freeze_path, freeze)
        print('FROZEN', freeze_path, flush=True)
        if a.audit_only:
            status(phase='audit_complete', freeze=str(freeze_path)); return
        selected = [r for r in inventory if not a.scene or r['scene'] in a.scene]
        # Priority scenes are processed first, but all 28 are retained in a full run.
        priority = {'7Y6A7599':0, '6V1A9539':1, 'IMG_7958':2}
        selected.sort(key=lambda r:(priority.get(r['scene'], 3), r['scene']))
        for record in selected:
            memory = available_memory()
            if memory is not None and memory < 5*1024**3:
                raise RuntimeError(f'Only {memory/1024**3:.3f} GiB available, minimum 5: campaign stopped')
            status(phase='measuring', scene=record['scene'], completed=completed, freeze=str(freeze_path))
            completed.append(scene_campaign(record, a.tool, a.production, a.output_dir))
            # No source/config drift accepted between scenes.
            if any(sha256(p) != freeze['sources'][str(p.relative_to(ROOT))]['sha256'] for p in paths):
                raise ValueError('Bench source changed during campaign')
            if any(sha256(a.engine_source/p) != digest for p, digest in freeze['engine_sources'].items()):
                raise ValueError('Frozen engine source changed during campaign')
            if any(sha256(p) != digest for p, digest in freeze['tools'].items()):
                raise ValueError('Native tool changed during campaign')
        output = a.output_dir/f'colour-placement-index-v001-{stamp}.json'
        write_immutable(output, dict(schema_version=1, created_utc=now(), freeze=str(freeze_path),
                                    freeze_sha256=sha256(freeze_path), scenes=completed,
                                    verdict='measured_no_automatic_promotion'))
        status(phase='complete', index=str(output), completed=completed)
    except BaseException as error:
        status(phase='failed', error=str(error), completed=completed)
        write_immutable(a.output_dir/f'colour-placement-failure-v001-{stamp}.json',
                        dict(schema_version=1, created_utc=now(), error=str(error), scenes=completed,
                             campaign=stamp, verdict='incomplete_not_a_quality_result'))
        raise


if __name__ == '__main__':
    main()
