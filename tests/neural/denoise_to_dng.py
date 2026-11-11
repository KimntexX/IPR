"""Denoise R6 III CR3 files with a trained network and write Bayer DNG files for real-world trials.

CUDA environment (torch, rawpy, tifffile). For each CR3: visible mosaic,
black level measured on the masked left columns (median per plane, as IPR's
production estimator does), noise law of the file's ISO (measured, or
interpolated in log-ISO between the measured ISOs of the sensor bank), sigma
map, network on the packed planes in tiles with the exact receptive halo,
then the mosaic is written back as an uncompressed CFA DNG with the camera's
colour matrix and as-shot neutral. The same writer also produces the
untouched mosaic, so IPR or Lightroom compare both DNG with identical
metadata. Trial tool only: not a production path, not a qualification.
"""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import struct

from prepare_next_training import digest, read, write


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT/'build-patch_neural_restoration'
BANK = ROOT/'docs/validation/raw/neural-sensor-noise-v001-20261105T173608Z.json'
DIVISOR = 12303.
CORE, HALO = 1024, 192
POSITIONS = ((0, 0), (0, 1), (1, 0), (1, 1))


def cr3_exif(path):
    """Make, model and ISO from the CMT1 (IFD0) and CMT2 (EXIF) boxes of a CR3, without decoding pixels."""
    def ifd(data):
        order = '<' if data[:2] == b'II' else '>'
        offset = struct.unpack(order+'I', data[4:8])[0]
        count = struct.unpack(order+'H', data[offset:offset+2])[0]
        tags = {}
        for index in range(count):
            entry = data[offset+2+12*index:offset+14+12*index]
            tag, kind, number = struct.unpack(order+'HHI', entry[:8])
            value = entry[8:12]
            if kind == 2:
                start = struct.unpack(order+'I', value)[0] if number > 4 else None
                raw = data[start:start+number] if start is not None else value[:number]
                tags[tag] = raw.split(b'\0')[0].decode('ascii', 'replace')
            elif kind == 3:
                tags[tag] = struct.unpack(order+'H', value[:2])[0]
            elif kind == 4:
                tags[tag] = struct.unpack(order+'I', value)[0]
        return tags
    boxes = {}
    with Path(path).open('rb') as stream:
        data = stream.read(4*1024*1024)
    for name in (b'CMT1', b'CMT2'):
        at = data.find(name)
        if at < 4:
            raise ValueError(f'{name.decode()} box missing in {path}')
        size = struct.unpack('>I', data[at-4:at])[0]
        boxes[name] = ifd(data[at+4:at-4+size])
    iso = boxes[b'CMT2'].get(34855)
    if not iso:
        raise ValueError(f'ISO missing in {path}')
    return dict(make=boxes[b'CMT1'].get(271, ''), model=boxes[b'CMT1'].get(272, ''), iso=int(iso))


def law_for_iso(bank, iso):
    """Per-plane (shot slope, read variance) in DN for any ISO inside the bank's measured range, log-log interpolation."""
    fits = {(r['iso'], r['plane']): r for r in bank['fits']}
    rows, sources = [], set()
    for plane in range(4):
        slopes = {i: r['fit']['shot_slope_dn'] for (i, p), r in fits.items() if p == plane and r['fit'].get('shot_slope_dn')}
        reads = {i: r['reference_dark_variance_dn2'] for (i, p), r in fits.items() if p == plane}
        values = []
        for table in (slopes, reads):
            if iso in table:
                values.append(float(table[iso]))
                continue
            lower = [i for i in table if i < iso]
            upper = [i for i in table if i > iso]
            if not lower or not upper:
                raise ValueError(f'ISO {iso} outside the measured noise laws ({min(table)}-{max(table)})')
            low, high = max(lower), min(upper)
            t = math.log(iso/low)/math.log(high/low)
            values.append(math.exp((1-t)*math.log(table[low])+t*math.log(table[high])))
            sources.add(f'interpolated between ISO {low} and {high}')
        rows.append(tuple(values))
    return rows, ('measured' if not sources else '; '.join(sorted(sources)))


def masked_black(raw):
    """Median of the masked left columns per canonical plane."""
    import numpy as np
    sizes = raw.sizes
    if sizes.top_margin % 2 or sizes.left_margin % 2 or sizes.left_margin < 32:
        raise ValueError('Even margins and a masked left area are required')
    masked = raw.raw_image[sizes.top_margin:sizes.top_margin+sizes.height//2*2, 8:sizes.left_margin-8]
    return [float(np.median(masked[y::2, x::2])) for y, x in POSITIONS]


def tiles(height, width, core=CORE):
    """Core windows covering the packed planes; each is processed with a halo of HALO cells around it."""
    for y in range(0, height, core):
        for x in range(0, width, core):
            yield y, min(y+core, height), x, min(x+core, width)


def denoise_planes(model, planes, law, device='cuda'):
    """Network output of normalized packed planes [4, H, W] with the exact receptive halo around every tile."""
    import numpy as np
    import torch
    import torch.nn.functional as functional
    from physical_natural_training import CANONICAL_TAGS, sigma_map
    slope = torch.tensor([s for s, _ in law], device=device)[None, :, None, None]
    readv = torch.tensor([r for _, r in law], device=device)[None, :, None, None]
    a, b = slope/DIVISOR, readv/DIVISOR**2
    tags = torch.tensor([CANONICAL_TAGS], device=device)
    _, height, width = planes.shape
    full = torch.from_numpy(np.ascontiguousarray(planes, dtype=np.float32))[None]
    # Reflect padding gives every tile its full context; the extra rounds the size to the network's 8-cell grid.
    padded = functional.pad(full, (HALO, HALO+(-width) % 8, HALO, HALO+(-height) % 8), mode='reflect')
    out = np.empty_like(planes, dtype=np.float32)
    with torch.inference_mode():
        for y0, y1, x0, x1 in tiles(height, width):
            ty, tx = (y1-y0)+(-(y1-y0)) % 8, (x1-x0)+(-(x1-x0)) % 8
            window = padded[..., y0:y0+ty+2*HALO, x0:x0+tx+2*HALO].to(device)
            sigma = sigma_map(window, slope, readv, DIVISOR)
            corrected = model.correct(window, sigma, tags, a, b)['corrected']
            out[:, y0:y1, x0:x1] = corrected[0, :, HALO:HALO+(y1-y0), HALO:HALO+(x1-x0)].cpu().numpy()
    return out


def write_cfa_dng(path, mosaic, raw, meta, black, white):
    """Uncompressed RGGB CFA DNG 1.4 with black and white levels, colour matrix (D65) and as-shot neutral."""
    import numpy as np
    import tifffile
    from fractions import Fraction

    def rational(values, signed=False, denominator=10000):
        flat = []
        for value in values:
            fraction = Fraction(float(value)).limit_denominator(denominator)
            flat += [fraction.numerator, fraction.denominator]
        return flat
    camera_from_xyz = np.asarray(raw.rgb_xyz_matrix[:3], dtype=np.float64)
    if not np.isfinite(camera_from_xyz).all() or not camera_from_xyz.any():
        raise ValueError('Camera colour matrix missing')
    multipliers = np.asarray(raw.camera_whitebalance[:3], dtype=np.float64)
    neutral = multipliers[1]/multipliers
    orientation = {0: 1, 3: 3, 5: 8, 6: 6}.get(raw.sizes.flip, 1)
    unique = f'{meta["make"]} {meta["model"]}'.strip()
    extratags = [
        (50706, 'B', 4, (1, 4, 0, 0), True), (50707, 'B', 4, (1, 1, 0, 0), True),
        (50708, 's', 0, unique, True), (271, 's', 0, meta['make'], True), (272, 's', 0, meta['model'], True),
        (274, 'H', 1, orientation, True),
        (33421, 'H', 2, (2, 2), True), (33422, 'B', 4, (0, 1, 1, 2), True),
        (50710, 'B', 3, (0, 1, 2), True), (50711, 'H', 1, 1, True),
        (50713, 'H', 2, (2, 2), True), (50714, 5, 4, tuple(rational(black)), True), (50717, 'I', 1, int(white), True),
        (50721, 10, 9, tuple(rational(camera_from_xyz.ravel(), signed=True)), True), (50778, 'H', 1, 21, True),
        (50728, 5, 3, tuple(rational(neutral)), True), (34855, 'H', 1, meta['iso'], True)]
    tifffile.imwrite(path, mosaic, photometric=32803, compression=None, subfiletype=0, metadata=None,
                     software='IPR neural denoise trial', extratags=extratags)


def run(inputs, campaign, step, output, strength, write_noisy):
    import numpy as np
    import rawpy
    from input_modes import load_trained_model
    if not 0 <= strength <= 1:
        raise ValueError('Strength in [0, 1] required')
    plan = read(campaign/'plan.json')
    seed = plan['seeds'][0]
    model, checkpoint_sha256, checkpoint_step, _ = load_trained_model(campaign, plan, seed, step)
    bank = read(BANK)
    output.mkdir(parents=True, exist_ok=True)
    records = []
    for source in inputs:
        meta = cr3_exif(source)
        if 'R6 Mark III' not in meta['model']:
            raise ValueError(f'{source.name}: only the Canon EOS R6 Mark III has measured noise laws ({meta["model"]})')
        law, provenance = law_for_iso(bank, meta['iso'])
        with rawpy.imread(str(source)) as raw:
            if raw.raw_colors_visible[:2, :2].tolist() != [[0, 1], [3, 2]]:
                raise ValueError('RGGB R6 III phase required')
            black = masked_black(raw)
            white = float(raw.white_level)
            mosaic = raw.raw_image_visible[:raw.sizes.height//2*2, :raw.sizes.width//2*2].astype(np.float32)
            planes = np.stack([(mosaic[y::2, x::2]-black[i])/DIVISOR for i, (y, x) in enumerate(POSITIONS)])
            denoised = denoise_planes(model, planes, law)
            blended = planes+strength*(denoised-planes)
            out = np.empty_like(mosaic)
            for i, (y, x) in enumerate(POSITIONS):
                out[y::2, x::2] = blended[i]*DIVISOR+black[i]
            out = np.clip(np.rint(out), 0, white).astype(np.uint16)
            stem = source.stem
            denoised_path = output/f'{stem}-neural.dng'
            write_cfa_dng(denoised_path, out, raw, meta, black, white)
            record = dict(source=str(source), source_sha256=digest(source), iso=meta['iso'], noise_law_provenance=provenance,
                          black_by_plane=black, white=white, strength=strength, denoised=denoised_path.name,
                          denoised_sha256=digest(denoised_path))
            if write_noisy:
                noisy_path = output/f'{stem}-noisy.dng'
                original = np.clip(mosaic, 0, white).astype(np.uint16)
                write_cfa_dng(noisy_path, original, raw, meta, black, white)
                record.update(noisy=noisy_path.name, noisy_sha256=digest(noisy_path))
        records.append(record)
        print(f'{source.name}: ISO {meta["iso"]}, {provenance} -> {record["denoised"]}', flush=True)
    write(output/f'denoise-to-dng-{datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")}.json', dict(
        schema_version=1, status='trial_dng_not_qualification', campaign=str(campaign), checkpoint_step=checkpoint_step,
        checkpoint_sha256=checkpoint_sha256, seed=seed, tool_sha256=digest(Path(__file__)), files=records))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', type=Path, nargs='+', help='CR3 files of the Canon EOS R6 Mark III')
    parser.add_argument('--campaign', type=Path, default=BUILD/'sigma-input-color-training-v001')
    parser.add_argument('--checkpoint-step', type=int, default=75000)
    parser.add_argument('--output', type=Path, default=BUILD/'dng-trials')
    parser.add_argument('--strength', type=float, default=1.)
    parser.add_argument('--noisy', action='store_true', help='also write the untouched mosaic through the same DNG writer')
    args = parser.parse_args()
    run([p.resolve() for p in args.inputs], args.campaign.resolve(), args.checkpoint_step, args.output.resolve(),
        args.strength, args.noisy)
