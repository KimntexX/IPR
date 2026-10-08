#!/usr/bin/env python3
"""16-bit sRGB audit and repeatable comparisons; never edits source photographs.

The v1 ROI names are retained as historical identifiers, not certified subjects.
CIEDE2000 follows Sharma, Wu, Dalal (2005), doi:10.1002/col.20070.
Only a one-pixel crop is explored; optical/spatial registration is NOT implied.
"""
import argparse
import hashlib
import json
import math
import zipfile
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from analyse_color_mixer import metadata, native_pixels, choose_crop
from analyse_base_colorimetrique import REGIONS_DEFINITION

ROOT = Path(__file__).resolve().parents[1]
WHITE = np.array([0.95047, 1.0, 1.08883])
MATRIX = np.array([[.4124564, .3575761, .1804375],
                   [.2126729, .7151522, .0721750],
                   [.0193339, .1191920, .9503041]])


def sha256(stream):
    digest = hashlib.sha256()
    for block in iter(lambda: stream.read(1024 * 1024), b''):
        digest.update(block)
    return digest.hexdigest()


def file_hash(path):
    with Path(path).open('rb') as stream:
        return sha256(stream)


def srgb_lab(rgb):
    rgb = np.asarray(rgb, dtype=np.float64)
    linear = np.where(rgb <= .04045, rgb / 12.92, ((rgb + .055) / 1.055) ** 2.4)
    xyz = linear @ MATRIX.T
    ratio = xyz / WHITE
    f = np.where(ratio > 216 / 24389, np.cbrt(ratio), (24389 / 27 * ratio + 16) / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]),
                     200 * (f[..., 1] - f[..., 2])], axis=-1), xyz[..., 1]


def delta_e_2000(first, second):
    """CIEDE2000 with kL=kC=kH=1; array last dimension is L*,a*,b*."""
    first, second = np.asarray(first, dtype=np.float64), np.asarray(second, dtype=np.float64)
    l1, a1, b1 = np.moveaxis(first, -1, 0)
    l2, a2, b2 = np.moveaxis(second, -1, 0)
    c1, c2 = np.hypot(a1, b1), np.hypot(a2, b2)
    cbar = (c1 + c2) / 2
    g = .5 * (1 - np.sqrt(cbar ** 7 / (cbar ** 7 + 25. ** 7)))
    ap1, ap2 = (1 + g) * a1, (1 + g) * a2
    cp1, cp2 = np.hypot(ap1, b1), np.hypot(ap2, b2)
    hp1 = np.where(cp1 == 0, 0, np.degrees(np.arctan2(b1, ap1)) % 360)
    hp2 = np.where(cp2 == 0, 0, np.degrees(np.arctan2(b2, ap2)) % 360)
    achromatic = cp1 * cp2 == 0
    dh = hp2 - hp1
    dh = np.where(dh > 180, dh - 360, np.where(dh < -180, dh + 360, dh))
    dh = np.where(achromatic, 0, dh)
    d_h = 2 * np.sqrt(cp1 * cp2) * np.sin(np.radians(dh / 2))
    d_l, d_c = l2 - l1, cp2 - cp1
    lbar, cpbar = (l1 + l2) / 2, (cp1 + cp2) / 2
    hsum = hp1 + hp2
    hbar = np.where(np.abs(hp1 - hp2) <= 180, hsum / 2,
                    np.where(hsum < 360, (hsum + 360) / 2, (hsum - 360) / 2))
    hbar = np.where(achromatic, hsum, hbar)
    t = (1 - .17 * np.cos(np.radians(hbar - 30)) + .24 * np.cos(np.radians(2 * hbar))
         + .32 * np.cos(np.radians(3 * hbar + 6)) - .20 * np.cos(np.radians(4 * hbar - 63)))
    sl = 1 + .015 * (lbar - 50) ** 2 / np.sqrt(20 + (lbar - 50) ** 2)
    sc, sh = 1 + .045 * cpbar, 1 + .015 * cpbar * t
    theta = 30 * np.exp(-((hbar - 275) / 25) ** 2)
    rt = -2 * np.sqrt(cpbar ** 7 / (cpbar ** 7 + 25. ** 7)) * np.sin(np.radians(2 * theta))
    return np.sqrt(np.maximum(0, (d_l / sl) ** 2 + (d_c / sc) ** 2
                              + (d_h / sh) ** 2 + rt * (d_c / sc) * (d_h / sh)))


def compare(ipr, lr):
    """Descriptive LR minus IPR differences, without attributing their cause."""
    if ipr.shape != lr.shape or ipr.size == 0:
        raise ValueError('Comparison requires equal, nonempty RGB arrays')
    lab_ipr, y_ipr = srgb_lab(ipr)
    lab_lr, y_lr = srgb_lab(lr)
    delta = lab_lr - lab_ipr
    de00 = delta_e_2000(lab_ipr, lab_lr)
    ci, cl = np.hypot(lab_ipr[..., 1], lab_ipr[..., 2]), np.hypot(lab_lr[..., 1], lab_lr[..., 2])
    hi = np.degrees(np.arctan2(lab_ipr[..., 2], lab_ipr[..., 1]))
    hl = np.degrees(np.arctan2(lab_lr[..., 2], lab_lr[..., 1]))
    chromatic = (ci >= 5) & (cl >= 5)
    # Avoid ambiguous angles on near-neutral pixels; shortest circular distance.
    hue = np.abs((hl - hi + 180) % 360 - 180)
    neutral_lr = (cl < 5) & (lab_lr[..., 0] >= 20) & (lab_lr[..., 0] <= 90)
    dc = np.hypot(delta[..., 1], delta[..., 2])
    return {'samples': int(de00.size), 'delta_e00_mean': float(de00.mean()),
            'delta_e00_median': float(np.median(de00)), 'delta_e00_p95': float(np.percentile(de00, 95)),
            'delta_e76_mean': float(np.linalg.norm(delta, axis=-1).mean()),
            'delta_lab_mean_lr_minus_ipr': delta.reshape(-1, 3).mean(axis=0).tolist(),
            'rgb_mae_normalized': float(np.abs(lr - ipr).mean()),
            'ipr_lab_mean': lab_ipr.reshape(-1, 3).mean(axis=0).tolist(),
            'lr_lab_mean': lab_lr.reshape(-1, 3).mean(axis=0).tolist(),
            'ipr_y_mean': float(y_ipr.mean()), 'lr_y_mean': float(y_lr.mean()),
            'delta_chroma_mean': float((cl - ci).mean()),
            'hue_abs_circular_mean_degrees': float(hue[chromatic].mean()) if chromatic.any() else None,
            'hue_usable_fraction': float(chromatic.mean()),
            'lr_neutral_fraction': float(neutral_lr.mean()),
            'ab_distance_on_lr_neutral_mean': float(dc[neutral_lr].mean()) if neutral_lr.any() else None,
            'ipr_any_channel_low_fraction': float(np.any(ipr <= 1 / 65535, axis=-1).mean()),
            'lr_any_channel_low_fraction': float(np.any(lr <= 1 / 65535, axis=-1).mean()),
            'ipr_any_channel_high_fraction': float(np.any(ipr >= 65534 / 65535, axis=-1).mean()),
            'lr_any_channel_high_fraction': float(np.any(lr >= 65534 / 65535, axis=-1).mean())}


def strict_region(pixels, coords, step, top=0, left=0):
    x1, y1, x2, y2 = coords
    if not (step >= 1 and 0 <= x1 < x2 <= pixels.shape[1] - left
            and 0 <= y1 < y2 <= pixels.shape[0] - top):
        raise ValueError('ROI outside image or invalid; no silent clamping')
    return np.asarray(pixels[y1 + top:y2 + top:step, x1 + left:x2 + left:step, :3],
                      dtype=np.float64) / 65535.


def audit_tiff(path):
    result = metadata(path)
    if result['bits'] != [16] * result['samples'] or 'srgb' not in result['icc'].lower():
        raise ValueError(f'Expected 16-bit sRGB TIFF: {path}')
    result['sha256'] = file_hash(path)
    result['mtime_utc'] = datetime.fromtimestamp(path.stat().st_mtime, timezone.utc).isoformat()
    return result


def archive_identity(archive, name, current_hash):
    matches = [info for info in archive.infolist() if Path(info.filename).name == name and not info.is_dir()]
    if len(matches) != 1:
        raise ValueError(f'Expected one archive member named {name}, found {len(matches)}')
    member = matches[0]
    with archive.open(member) as stream:
        old_hash = sha256(stream)
    return {'member': member.filename, 'archive_timestamp_local': list(member.date_time),
            'archive_bytes': member.file_size, 'sha256': old_hash,
            'identical_bytes': old_hash == current_hash}


def thumbnail(pixels, rois):
    step = max(1, math.ceil(max(pixels.shape[:2]) / 500))
    # Display only; measurements above operate on native uint16 samples.
    reduced = (np.asarray(pixels[::step, ::step, :3], dtype=np.uint32) * 255 // 65535).astype(np.uint8)
    image = Image.fromarray(reduced)
    draw = ImageDraw.Draw(image)
    for index, roi in enumerate(rois, 1):
        x1, y1, x2, y2 = roi['coords']
        if x2 <= pixels.shape[1] and y2 <= pixels.shape[0]:
            box = (x1 // step, y1 // step, x2 // step, y2 // step)
            draw.rectangle(box, outline='white', width=1)
            draw.text(box[:2], str(index), fill='yellow')
    return image


def analyse(args):
    if args.output.exists() or (args.contact_sheet and args.contact_sheet.exists()):
        raise ValueError('Output already exists; keep previous measurement files immutable')
    result = {'schema': 2, 'timestamp_utc': datetime.now(timezone.utc).isoformat(),
              'analyser_sha256': file_hash(Path(__file__)), 'archive': str(args.archive),
              'archive_sha256': file_hash(args.archive),
              'method': {'rgb': 'native 16-bit sRGB SDR', 'lab_white': 'D65',
                         'global_grid_step': args.step, 'roi_grid_step': args.roi_step,
                         'delta_e': 'CIEDE2000 kL=kC=kH=1 (also separately CIE76)',
                         'sign': 'Lightroom minus IPR',
                         'geometry': '0/1-pixel crop only; no optical registration',
                         'roi_labels': 'historical v1 labels, not verified physical targets'},
              'scenes': []}
    thumbnails = []
    with zipfile.ZipFile(args.archive) as archive:
        for scene in args.scenes:
            ipr_path, lr_path = args.input / f'{scene}{args.ipr_suffix}.tiff', args.input / f'{scene}.tif'
            ipr_meta, lr_meta = audit_tiff(ipr_path), audit_tiff(lr_path)
            previous = archive_identity(archive, f'{scene}-IPR.tiff', ipr_meta['sha256'])
            ipr, lr = native_pixels(ipr_path), native_pixels(lr_path)
            candidates = choose_crop(lr, ipr, args.step)
            crop = candidates[0]
            height, width = ipr.shape[:2]
            global_roi = [0, 0, width, height]
            ipr_sample = strict_region(ipr, global_roi, args.step)
            lr_sample = strict_region(lr, global_roi, args.step, crop['top'], crop['left'])
            record = {'scene': scene, 'ipr': ipr_meta, 'lr': lr_meta, 'old_export': previous,
                      'crop_candidates': candidates, 'global': compare(ipr_sample, lr_sample), 'regions': []}
            if args.previous_ipr_suffix:
                old_path = args.input / f'{scene}{args.previous_ipr_suffix}.tiff'
                old_meta = audit_tiff(old_path)
                old_pixels = native_pixels(old_path)
                if old_pixels.shape != ipr.shape:
                    raise ValueError('Previous/new IPR geometry differs; do not silently register')
                old_sample = strict_region(old_pixels, global_roi, args.step)
                record['previous_ipr'] = old_meta
                record['previous_vs_lr_same_crop'] = compare(old_sample, lr_sample)
                # compare labels are generic legacy names: ipr=old, lr=new here.
                record['previous_vs_current'] = compare(old_sample, ipr_sample)
                record['previous_vs_current_sign'] = 'current minus previous'
                del old_pixels
            for index, roi in enumerate(REGIONS_DEFINITION[scene], 1):
                entry = {'number': index, **roi}
                try:
                    first = strict_region(ipr, roi['coords'], args.roi_step)
                    second = strict_region(lr, roi['coords'], args.roi_step, crop['top'], crop['left'])
                except ValueError as error:
                    entry.update(valid=False, error=str(error))
                else:
                    entry.update(valid=True, measurement=compare(first, second))
                record['regions'].append(entry)
            if args.contact_sheet:
                thumbnails.append((scene, thumbnail(ipr, REGIONS_DEFINITION[scene]),
                                    thumbnail(lr, REGIONS_DEFINITION[scene])))
            result['scenes'].append(record)
            print(scene, 'old/new identical:', previous['identical_bytes'],
                  'DE00 global:', round(record['global']['delta_e00_mean'], 4), flush=True)
            del ipr, lr
    result['all_old_new_identical_bytes'] = all(row['old_export']['identical_bytes'] for row in result['scenes'])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2, ensure_ascii=False, allow_nan=False)
    if args.contact_sheet:
        sheet = Image.new('RGB', (1100, 540 * len(thumbnails)), '#262a30')
        draw = ImageDraw.Draw(sheet)
        for row, (scene, first, second) in enumerate(thumbnails):
            for col, (label, image) in enumerate([('IPR nouveau', first), ('Lightroom', second)]):
                x, y = 550 * col, 540 * row
                draw.text((x + 15, y + 8), scene + ' / ' + label, fill='white')
                sheet.paste(image, (x + (550 - image.width) // 2, y + 30))
        args.contact_sheet.parent.mkdir(parents=True, exist_ok=True)
        with args.contact_sheet.open('xb') as stream:
            sheet.save(stream, format='PNG')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=ROOT / 'tests/image/base/neutre')
    parser.add_argument('--ipr-suffix', default='-IPR', help='Use --ipr-suffix=-IPR-2 for revision exports')
    parser.add_argument('--previous-ipr-suffix', help='Optional previous export, e.g. --previous-ipr-suffix=-IPR')
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--contact-sheet', type=Path)
    parser.add_argument('--step', type=int, default=8)
    parser.add_argument('--roi-step', type=int, default=4)
    parser.add_argument('--scenes', nargs='+', choices=sorted(REGIONS_DEFINITION), default=sorted(REGIONS_DEFINITION))
    args = parser.parse_args()
    if args.step < 1 or args.roi_step < 1:
        parser.error('Sampling steps must be positive')
    for suffix in (args.ipr_suffix, args.previous_ipr_suffix):
        if suffix is not None and (not suffix or any(c in suffix for c in '/\\')):
            parser.error('Export suffix must be nonempty and contain no path separators')
    analyse(args)


if __name__ == '__main__':
    main()
