"""Noise law of a camera from a qualification shoot: one JSON of evidence.

Usage: python tests/qualify_camera_noise.py <folder of RAW files> <output.json>

Reads every RAW of the folder (not recursive), never writes into it. The shoot follows the
noise protocol (docs/validation/protocols/protocole-bruit.md): darks with the lens cap at
1/200 s, and pairs of identical exposures of a defocused uniform surface at several levels,
full ISO stops, at a fixed aperture. A hand-held shoot is accepted: the two frames of a pair
may move by a few pixels.

Darks: read variance per ISO and CFA position, half the variance of the difference of two
consecutive frames (the fixed pattern cancels), on the central 80 %.
Flats: per 64-pixel tile of each CFA plane, the difference of the two frames after matching
their signal and removing a plane (illumination slope, small move), divided by 1 + k^2;
tiles near saturation, with texture (the mean keeps more than a quarter of the difference's
variance, twice over) or with a signal mismatch above 15 % are dropped. Then
variance = a * signal + b per ISO and position, b fixed to the darks' (and fitted free for
comparison), weighted by the inverse variance on 20 quantile bins of signal.
Black: median of the masked left columns of each frame. Units: raw DN of the visible mosaic.

Needs rawpy and Pillow (not installed by the CI: this script is not a unit test).
"""
import hashlib
import json
import sys
import time
from collections import defaultdict
from fractions import Fraction
from pathlib import Path

import numpy as np
import rawpy
from PIL import Image

TILE = 64
POSITIONS = ((0, 0), (0, 1), (1, 0), (1, 1))
RAW_SUFFIXES = {'.cr2', '.cr3', '.nef', '.arw', '.dng', '.raf', '.orf', '.rw2'}
DARK_SIGNAL_DN = 20         # a frame this close to its black is a dark
SATURATION = 0.95           # of the white level, in either frame of a tile
TEXTURE_RATIO = 2.0         # mean variance over its pure-noise expectation
SIGNAL_MISMATCH = 0.15      # |k - 1| between the two frames of a pair


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def exif(path):
    with Image.open(path) as im:
        base = im.getexif()
        ifd = base.get_ifd(0x8769)
        iso = ifd.get(0x8827)
        exposure, aperture = ifd.get(0x829A), ifd.get(0x829D)
        return dict(make=str(base.get(0x010F) or '').strip(), model=str(base.get(0x0110) or '').strip(),
                    iso=int(iso[0] if isinstance(iso, tuple) else iso),
                    exposure_s=float(Fraction(str(exposure))) if exposure is not None else None,
                    aperture=float(aperture) if aperture is not None else None,
                    datetime=ifd.get(0x9003) or base.get(0x0132))


def planes(path):
    """The four CFA planes of the visible mosaic, each with the black of its masked columns."""
    with rawpy.imread(str(path)) as raw:
        full = raw.raw_image.astype(np.float64)
        visible = raw.raw_image_visible.astype(np.float64)
        top, left, white = raw.sizes.top_margin, raw.sizes.left_margin, float(raw.white_level)
    if left <= 24:
        raise ValueError(f'{path.name}: no masked columns to read the black from')
    out = []
    for dy, dx in POSITIONS:
        masked = full[top + dy:top + visible.shape[0]:2, 8 + dx:left - 8:2]
        out.append((visible[dy::2, dx::2], float(np.median(masked))))
    return out, white


def residual(tile, design):
    coefficients, *_ = np.linalg.lstsq(design, tile.ravel(), rcond=None)
    return tile.ravel() - design @ coefficients


def dark_pair(a, b):
    h, w = a.shape
    return float((a - b)[h // 10:-h // 10, w // 10:-w // 10].var(ddof=1) / 2)


def flat_tiles(a, black_a, b, black_b, white):
    """(signal, noise variance, texture ratio, k) of each usable tile of one CFA plane."""
    h, w = a.shape
    yy, xx = np.mgrid[0:TILE, 0:TILE]
    design = np.column_stack([np.ones(TILE * TILE), yy.ravel(), xx.ravel()])
    rows = []
    for y in range(h // 10, h - h // 10 - TILE + 1, TILE):
        for x in range(w // 10, w - w // 10 - TILE + 1, TILE):
            ta, tb = a[y:y + TILE, x:x + TILE], b[y:y + TILE, x:x + TILE]
            if ta.max() >= SATURATION * white or tb.max() >= SATURATION * white:
                continue
            sa, sb = ta.mean() - black_a, tb.mean() - black_b
            if sa <= 0 or sb <= 0:
                continue
            k = sa / sb
            difference = residual((ta - black_a) - k * (tb - black_b), design).var(ddof=3)
            mean = residual(((ta - black_a) + k * (tb - black_b)) / 2, design).var(ddof=3)
            rows.append((float((sa + sb) / 2), float(difference / (1 + k * k)), float(4 * mean / difference), float(k)))
    return rows


def fit(signal, variance, fixed_read=None):
    edges = np.unique(np.quantile(signal, np.linspace(0, 1, 21)))
    xs, ys, ns = [], [], []
    for lo, hi in zip(edges[:-1], edges[1:]):
        chosen = (signal >= lo) & (signal <= hi)
        if chosen.sum() >= 20:
            xs.append(float(np.median(signal[chosen])))
            ys.append(float(np.median(variance[chosen])))
            ns.append(int(chosen.sum()))
    if len(xs) < 3:
        return None
    xs, ys = np.array(xs), np.array(ys)
    w = 1 / ys
    if fixed_read is None:
        a, b = np.linalg.lstsq(np.column_stack([xs, np.ones_like(xs)]) * w[:, None], ys * w, rcond=None)[0]
    else:
        b = fixed_read
        a = float(np.sum(w * w * xs * (ys - b)) / np.sum(w * w * xs * xs))
    model = a * xs + b
    return dict(shot_slope_dn=float(a), read_variance_dn2=float(b), bins=int(len(xs)),
                relative_rms=float(np.sqrt(np.mean(((ys - model) / model) ** 2))),
                signal_range_dn=[float(xs.min()), float(xs.max())],
                binned=[dict(signal_dn=float(x), variance_dn2=float(y), tiles=n) for x, y, n in zip(xs, ys, ns)])


def main(folder, output):
    started = time.time()
    folder, output = Path(folder), Path(output)
    if output.exists():
        raise SystemExit(f'{output} exists: a new version gets a new name')
    files = sorted(p for p in folder.iterdir() if p.suffix.lower() in RAW_SUFFIXES)
    inventory, loaded_white = [], None
    for path in files:
        frame, white = planes(path)
        visible = frame[1][0]
        h, w = visible.shape
        centre = visible[h // 2 - 128:h // 2 + 128, w // 2 - 128:w // 2 + 128]
        inventory.append(dict(file=path.name, sha256=sha256(path), bytes=path.stat().st_size, **exif(path),
                              black_dn=[p[1] for p in frame], centre_signal_dn=float(centre.mean() - frame[1][1]),
                              white_level=white))
        loaded_white = white
    cameras = {(r['make'], r['model']) for r in inventory}
    if len(cameras) != 1:
        raise SystemExit(f'one camera per folder, found {sorted(cameras)}')
    darks, flats = defaultdict(list), defaultdict(list)
    for r in inventory:
        r['role'] = 'dark' if r['centre_signal_dn'] < DARK_SIGNAL_DN else 'flat'
        (darks[r['iso']] if r['role'] == 'dark' else flats[(r['iso'], r['exposure_s'])]).append(r['file'])

    read = defaultdict(lambda: defaultdict(list))
    for iso, names in darks.items():
        frames = [planes(folder / n)[0] for n in sorted(names)]
        for i in range(0, len(frames) - 1, 2):
            for p in range(4):
                read[iso][p].append(dark_pair(frames[i][p][0], frames[i + 1][p][0]))
    tiles, pairs = defaultdict(lambda: defaultdict(list)), defaultdict(list)
    for (iso, exposure), names in sorted(flats.items()):
        names = sorted(names)
        for i in range(0, len(names) - 1, 2):
            (a, white), (b, _) = planes(folder / names[i]), planes(folder / names[i + 1])
            pairs[iso].append(dict(files=[names[i], names[i + 1]], exposure_s=exposure))
            for p in range(4):
                tiles[iso][p] += flat_tiles(a[p][0], a[p][1], b[p][0], b[p][1], white)

    isos = {}
    for iso in sorted(set(read) | set(tiles)):
        positions = []
        for p in range(4):
            t = np.array(tiles[iso][p]) if tiles[iso][p] else np.zeros((0, 4))
            kept = t[(t[:, 2] < TEXTURE_RATIO) & (np.abs(t[:, 3] - 1) < SIGNAL_MISMATCH)] if len(t) else t
            dark = float(np.median(read[iso][p])) if read[iso][p] else None
            positions.append(dict(
                dark_read_variance_dn2=dark, dark_read_variance_per_pair=read[iso][p],
                tiles_total=int(len(t)), tiles_kept=int(len(kept)),
                fit_dark_read=fit(kept[:, 0], kept[:, 1], dark) if len(kept) and dark is not None else None,
                fit_free=fit(kept[:, 0], kept[:, 1]) if len(kept) else None))
        isos[str(iso)] = dict(dark_pairs=len(read[iso][0]), flat_pairs=pairs[iso], positions=positions)
        print(iso, ['a=%.3f b=%.1f' % (q['fit_dark_read']['shot_slope_dn'], q['fit_dark_read']['read_variance_dn2'])
                    for q in positions if q['fit_dark_read']], flush=True)
    make, model = cameras.pop()
    result = dict(schema_version=1, status='camera_noise_qualification_exploratory', make=make, model=model,
                  positions='raster order of the 2x2 CFA cell: (0,0), (0,1), (1,0), (1,1)',
                  white_level=loaded_white, method=__doc__.strip(),
                  thresholds=dict(tile=TILE, dark_signal_dn=DARK_SIGNAL_DN, saturation=SATURATION,
                                  texture_ratio=TEXTURE_RATIO, signal_mismatch=SIGNAL_MISMATCH),
                  inventory=inventory, isos=isos, seconds=time.time() - started,
                  versions=dict(python=sys.version.split()[0], numpy=np.__version__, rawpy=rawpy.__version__))
    output.write_text(json.dumps(result, indent=1), encoding='utf-8')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2])
