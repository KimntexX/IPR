"""Variants of the single-RAW slope estimator against known laws (60D qualified, R6 III catalogue).

Each RAW is read once; every variant runs on the same planes. Output: per photo and variant,
estimated slope over the known slope, per CFA position.
"""
import json
import math
import struct
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np
import rawpy
from PIL import Image

POSITIONS = ((0, 0), (0, 1), (1, 0), (1, 1))
# name: (block, quantile, aggregation, texture gate)
VARIANTS = {
    'baseline_b16_q10_half': (16, 0.10, 'half', None),
    'b16_q05_half': (16, 0.05, 'half', None),
    'b16_q02_half': (16, 0.02, 'half', None),
    'b8_q10_half': (8, 0.10, 'half', None),
    'b8_q05_half': (8, 0.05, 'half', None),
    'b16_q10_min': (16, 0.10, 'min', None),
    'b16_q10_quarter': (16, 0.10, 'quarter', None),
    'b16_q10_half_gate2': (16, 0.10, 'half', 2.0),
    'b16_q10_half_gate1.5': (16, 0.10, 'half', 1.5),
    'b16_q05_half_gate1.5': (16, 0.05, 'half', 1.5),
}
_bias = {}


def bias(block, q):
    if (block, q) not in _bias:
        rng = np.random.default_rng(1)
        x = rng.standard_normal((4000, block + 2, block + 2))
        r = x[:, 1:-1, 1:-1] - 0.25 * (x[:, :-2, 1:-1] + x[:, 2:, 1:-1] + x[:, 1:-1, :-2] + x[:, 1:-1, 2:])
        v = (np.median(np.abs(r - np.median(r, axis=(1, 2), keepdims=True)), axis=(1, 2)) / 0.6745) ** 2 / 1.25
        _bias[(block, q)] = float(np.quantile(v, q))
    return _bias[(block, q)]


def gate_bias(block):
    """Expected texture ratio of pure noise (sub-means variance x 16 over the block's noise)."""
    key = ('gate', block)
    if key not in _bias:
        rng = np.random.default_rng(2)
        x = rng.standard_normal((4000, block, block))
        sub = x.reshape(4000, block // 4, 4, block // 4, 4).mean(axis=(2, 4))
        _bias[key] = float(np.median(sub.var(axis=(1, 2)) * 16))
    return _bias[key]


def block_stats(plane, block):
    r = plane[1:-1, 1:-1] - 0.25 * (plane[:-2, 1:-1] + plane[2:, 1:-1] + plane[1:-1, :-2] + plane[1:-1, 2:])
    h, w = (r.shape[0] // block) * block, (r.shape[1] // block) * block
    cut = lambda x: x[:h, :w].reshape(h // block, block, w // block, block).swapaxes(1, 2).reshape(-1, block, block)
    rb, mb = cut(r).reshape(-1, block * block), cut(plane[1:1 + h, 1:1 + w])
    var = (np.median(np.abs(rb - np.median(rb, axis=1, keepdims=True)), axis=1) / 0.6745) ** 2 / 1.25
    sub = mb.reshape(-1, block // 4, 4, block // 4, 4).mean(axis=(2, 4))
    texture = sub.var(axis=(1, 2)) * 16
    flat = mb.reshape(len(mb), -1)
    return flat.mean(axis=1), flat.max(axis=1), var, texture


def slope(mean, peak, var, texture, b, limit, block, q, aggregation, gate):
    keep = (peak < limit) & (mean > 0)
    if gate is not None:
        keep &= texture < gate * gate_bias(block) * np.maximum(var, 1e-9)
    if keep.sum() < 200:
        return float('nan')
    edges = np.unique(np.quantile(mean[keep], np.linspace(0, 1, 25)))
    est = []
    for lo, hi in zip(edges[:-1], edges[1:]):
        sel = keep & (mean >= lo) & (mean < hi)
        if sel.sum() >= 40:
            est.append((float(np.quantile(var[sel], q)) / bias(block, q) - b) / float(np.median(mean[sel])))
    if not est:
        return float('nan')
    est = np.sort(est)
    if aggregation == 'min':
        return float(est[0])
    share = 2 if aggregation == 'half' else 4
    return float(np.mean(est[:max(1, len(est) // share)]))


def run(args):
    path, known = args
    with rawpy.imread(path) as raw:
        full = raw.raw_image.astype(np.float64)
        rows, width = raw.raw_image_visible.shape
        top, left, white = raw.sizes.top_margin, raw.sizes.left_margin, float(raw.white_level)
    out = {name: [] for name in VARIANTS}
    for p, (dy, dx) in enumerate(POSITIONS):
        masked = full[top + dy:top + rows:2, 8 + dx:left - 8:2]
        black = float(np.median(masked))
        cols = masked - np.median(masked, axis=0, keepdims=True)
        b = float((np.median(np.abs(cols - np.median(cols))) / 0.6745) ** 2)
        plane = full[top + dy:top + rows:2, left + dx:left + width:2] - black
        stats = {blk: block_stats(plane, blk) for blk in {v[0] for v in VARIANTS.values()}}
        for name, (blk, q, aggregation, gate) in VARIANTS.items():
            a = slope(*stats[blk], b, 0.95 * (white - black), blk, q, aggregation, gate)
            out[name].append(a / known[p][0])
    return path, out


def exif_iso(path):
    p = Path(path)
    if p.suffix.lower() == '.cr2':
        with Image.open(p) as im:
            base = im.getexif()
            iso = base.get_ifd(0x8769).get(0x8827)
            return str(base.get(0x0110) or ''), int(iso[0] if isinstance(iso, tuple) else iso)
    data = p.read_bytes()[:4 << 20]
    model, iso = '', None
    for marker in (b'CMT1', b'CMT2'):
        i = data.find(marker)
        if i >= 4:
            size = struct.unpack('>I', data[i - 4:i])[0]
            ex = Image.Exif()
            ex.load(b'Exif\x00\x00' + data[i + 4:i - 4 + size])
            model = model or str(ex.get(0x0110) or '')
            v = ex.get(0x8827) or ex.get_ifd(0x8769).get(0x8827)
            if v:
                iso = int(v[0] if isinstance(v, tuple) else v)
    return model, iso


def known_law(table, iso):
    isos = sorted(table)
    if iso in table:
        return table[iso]
    if iso < isos[0] or iso > isos[-1]:
        return None
    lo, hi = max(i for i in isos if i < iso), min(i for i in isos if i > iso)
    t = (math.log(iso) - math.log(lo)) / (math.log(hi) - math.log(lo))
    return [(math.exp((1 - t) * math.log(a0) + t * math.log(a1)), b0) for (a0, b0), (a1, _) in zip(table[lo], table[hi])]


def main(qualified_60d, catalogue, image_root, output, per_iso=12):
    q = json.load(open(qualified_60d, encoding='utf-8'))
    law60 = {int(k): [(p['fit_dark_read']['shot_slope_dn'], p['fit_dark_read']['read_variance_dn2']) for p in v['positions']]
             for k, v in q['isos'].items()}
    cat = json.load(open(catalogue, encoding='utf-8'))['cameras'][0]
    lawr6 = {e['iso']: [(p['shot_slope_dn'], p['read_variance_dn2']) for p in e['positions']]
             for e in cat['isos'] if all(p['shot_slope_dn'] for p in e['positions'])}
    jobs, meta = [], {}
    counts = {}
    for p in sorted(Path(image_root).rglob('*')):
        if p.suffix.lower() not in ('.cr2', '.cr3') or any(s in p.parts for s in ('protocole', 'rafale', 'blacks', 'eos-60d')):
            continue
        model, iso = exif_iso(p)
        if iso is None:
            continue
        camera = '60D' if '60D' in model else 'R6III' if 'R6 Mark III' in model else None
        if camera is None:
            continue
        law = known_law(law60 if camera == '60D' else lawr6, iso)
        if law is None:
            continue
        key = (camera, iso)
        counts[key] = counts.get(key, 0) + 1
        if counts[key] > per_iso:
            continue
        jobs.append((p.as_posix(), law))
        meta[p.as_posix()] = dict(camera=camera, iso=iso, law='measured' if iso in (law60 if camera == '60D' else lawr6) else 'interpolated')
    print(len(jobs), 'photos', flush=True)
    rows = []
    with ProcessPoolExecutor(max_workers=8) as pool:
        for path, out in pool.map(run, jobs):
            rows.append(dict(file=Path(path).name, **meta[path], ratios=out))
            print(Path(path).name, meta[path]['iso'], ' '.join('%s %.2f' % (k[:12], np.nanmedian(v)) for k, v in out.items()), flush=True)
    json.dump(dict(variants={k: list(v) for k, v in VARIANTS.items()}, rows=rows), open(output, 'w'), indent=1)


if __name__ == '__main__':
    main(*sys.argv[1:5])
