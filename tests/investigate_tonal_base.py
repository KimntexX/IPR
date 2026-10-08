#!/usr/bin/env python3
"""Read-only stage audit of tonalBaseProbe; source photographs/profiles untouched.

This is a diagnostic, not a DNG renderer. It records profile structure/scalars,
not proprietary lookup tables. Intermediate images stay in the local build dir.
"""
import argparse
import json
import struct
from pathlib import Path

import numpy as np
from analyse_base_colorimetrique_v2 import file_hash, compare, srgb_lab
from analyse_color_mixer import native_pixels


def local_path(path):
    if len(path) > 2 and path[1] == ':' and Path('/mnt/c').exists():
        return Path('/mnt') / path[0].lower() / path[3:].replace('\\', '/')
    return Path(path)


def read_dcp(path):
    """Decode numeric IFD0 values; fail explicitly on malformed input."""
    data = Path(path).read_bytes()
    endian = {'II': '<', 'MM': '>'}.get(data[:2].decode('ascii'))
    if endian is None or struct.unpack_from(endian + 'H', data, 2)[0] != 0x4352:
        raise ValueError('Not a DCP TIFF header')
    offset = struct.unpack_from(endian + 'I', data, 4)[0]
    count = struct.unpack_from(endian + 'H', data, offset)[0]
    formats = {1: ('B', 1), 2: ('B', 1), 3: ('H', 2), 4: ('I', 4),
               5: ('II', 8), 7: ('B', 1), 9: ('i', 4), 10: ('ii', 8),
               11: ('f', 4), 12: ('d', 8)}
    tags = {}
    for i in range(count):
        entry = offset + 2 + 12 * i
        tag, kind, size, location = struct.unpack_from(endian + 'HHII', data, entry)
        if kind not in formats:
            continue
        fmt, width = formats[kind]
        position = entry + 8 if size * width <= 4 else location
        if position + size * width > len(data):
            raise ValueError('DCP value outside file')
        values = struct.unpack_from(endian + fmt * size, data, position)
        if kind == 2:
            values = bytes(values).rstrip(b'\0').decode('utf-8', errors='replace')
        elif kind in (5, 10):
            values = tuple(values[j] / values[j+1] for j in range(0, len(values), 2))
        tags[tag] = values
    return tags


def profile_summary(path):
    tags = read_dcp(path)
    return {'path': str(path), 'sha256': file_hash(path),
            'name': tags.get(50936), 'tags_present': sorted(tags),
            'forward_matrices': [tag in tags for tag in (50964, 50965)],
            'illuminants': [tags.get(tag) for tag in (50778, 50779)],
            'hue_sat_dimensions': tags.get(50937), 'look_dimensions': tags.get(50981),
            'hue_sat_encoding': tags.get(51107, (0,)),
            'look_encoding': tags.get(51108, (0,)),
            'baseline_exposure_offset_ev': tags.get(51109, (0,)),
            'tone_curve_points': len(tags.get(50940, ())) // 2,
            'default_black_render': tags.get(51110),
            'dynamic_range': tags.get(52551)}


def encode(rgb):
    rgb = np.clip(rgb, 0, 1)
    return np.where(rgb <= .0031308, rgb * 12.92,
                    1.055 * rgb ** (1/2.4) - .055)


def summary(rgb, srgb):
    result = {'mean_rgb': rgb.reshape(-1, 3).mean(axis=0).tolist(),
              'fraction_any_channel_negative': float(np.any(rgb < 0, axis=-1).mean()),
              'fraction_any_channel_above_one': float(np.any(rgb > 1, axis=-1).mean())}
    if srgb:
        y = np.clip(rgb, 0, 1) @ np.array([.2126729, .7151522, .0721750])
        result['clipped_linear_srgb_y_mean'] = float(y.mean())
        result['y_percentiles_5_50_95'] = np.percentile(y, [5, 50, 95]).tolist()
    return result


# Same D50->D65 Bradford and XYZ->sRGB matrices as the production engine.
D50_TO_SRGB = np.array([[3.2404542, -1.5371385, -.4986108],
                        [-.9692660, 1.8760108, .0415560],
                        [.0556434, -.2040259, 1.0572252]]) @ np.array([
                            [.9555766, -.0230393, .0631636],
                            [-.0282895, 1.0099416, .0210077],
                            [.0122982, -.0204830, 1.3299098]])
# Linear primaries only, no ROMM transfer curve. ICC ROMMRGB.pdf matrix.
_PRIMARY_XY = np.array([[.7347, .2653], [.1596, .8404], [.0366, .0001]])
_PRIMARY_XYZ = np.column_stack((_PRIMARY_XY[:, 0] / _PRIMARY_XY[:, 1],
                               np.ones(3), (1-_PRIMARY_XY.sum(axis=1))/_PRIMARY_XY[:, 1])).T
PROPHOTO_TO_XYZ = _PRIMARY_XYZ @ np.diag(np.linalg.solve(_PRIMARY_XYZ, [.9642, 1, .8249]))


def rgb_to_hsv(rgb):
    r, g, b = np.moveaxis(rgb, -1, 0)
    maximum, minimum = rgb.max(axis=-1), rgb.min(axis=-1)
    delta = maximum-minimum
    safe_delta = np.where(delta > 0, delta, 1)
    hue = np.where(maximum == r, ((g-b)/safe_delta) % 6,
                   np.where(maximum == g, (b-r)/safe_delta+2, (r-g)/safe_delta+4))/6
    saturation = delta / np.where(maximum > 0, maximum, 1)
    return np.stack([np.where(delta > 0, hue, 0), saturation, maximum], axis=-1)


def hsv_to_rgb(hsv):
    h, s, v = np.moveaxis(hsv, -1, 0)
    sector = h*6
    index = np.floor(sector).astype(int) % 6
    f = sector-np.floor(sector)
    p, q, t = v*(1-s), v*(1-f*s), v*(1-(1-f)*s)
    return np.stack([np.choose(index, [v, q, p, p, t, v]),
                     np.choose(index, [t, v, v, q, p, p]),
                     np.choose(index, [p, p, t, v, v, q])], axis=-1)


def apply_linear_table(rgb, dimensions, values):
    """SDR encoding=0 ablation, trilinear HSV table, same clipping as IPR."""
    h_count, s_count, v_count = dimensions
    table = np.array(values).reshape(v_count, h_count, s_count, 3)
    hsv = rgb_to_hsv(np.clip(rgb, 0, 1))
    coords = hsv * [h_count, s_count-1, v_count-1]
    low = np.floor(coords).astype(int)
    frac = coords - low
    modify = np.zeros_like(hsv)
    for dh in (0, 1):
        for ds in (0, 1):
            for dv in (0, 1):
                weight = ((frac[..., 0] if dh else 1-frac[..., 0]) *
                          (frac[..., 1] if ds else 1-frac[..., 1]) *
                          (frac[..., 2] if dv else 1-frac[..., 2]))
                entries = table[np.minimum(low[..., 2]+dv, v_count-1),
                                (low[..., 0]+dh) % h_count,
                                np.minimum(low[..., 1]+ds, s_count-1)]
                modify += entries * weight[..., None]
    hsv[..., 0] = (hsv[..., 0] + modify[..., 0]/360) % 1
    hsv[..., 1:] = np.clip(hsv[..., 1:] * modify[..., 1:], 0, 1)
    return hsv_to_rgb(hsv)


def dcp_ablation(camera, tags, kelvin):
    """Restricted to the actual A/D65, SDR, linear-table, curve-free corpus.

    This changes only the LUT working primaries, not WB/exposure/tone mapping.
    It is NOT a complete DNG implementation or a certified reference renderer.
    """
    if (tags.get(50778) != (17,) or tags.get(50779) != (21,) or 50940 in tags or
            tags.get(51107, (0,)) != (0,) or tags.get(51108, (0,)) != (0,)):
        raise ValueError('Profile unsupported by restricted diagnostic ablation')
    weight = np.clip((1/kelvin-1/2856) / (1/6500-1/2856), 0, 1)
    forward = (np.array(tags[50964])*(1-weight) + np.array(tags[50965])*weight).reshape(3, 3)
    xyz = camera @ forward.T * 2**tags.get(51109, (0,))[0]
    hue_map = np.array(tags[50938])*(1-weight) + np.array(tags[50939])*weight
    matrix_only = xyz @ D50_TO_SRGB.T
    outputs = {'matrix_only': matrix_only}
    for name, initial in [('srgb_tables', matrix_only),
                          ('prophoto_tables', xyz @ np.linalg.inv(PROPHOTO_TO_XYZ).T)]:
        mapped = apply_linear_table(initial, tags[50937], hue_map)
        mapped = apply_linear_table(mapped, tags[50981], tags[50982])
        if name == 'prophoto_tables':
            mapped = mapped @ PROPHOTO_TO_XYZ.T @ D50_TO_SRGB.T
        outputs[name] = mapped
    return outputs


def analyse(folder):
    manifest = json.loads((folder / 'manifest.json').read_text(encoding='utf-8'))
    result = {'probe': manifest, 'profiles': {}, 'images': [],
              'limitations': 'No physical calibration, no optical registration; scalar EV ratios are descriptive.'}
    for item in manifest['images']:
        profile = local_path(item['dcp'])
        result['profiles'][item['dcp']] = profile_summary(profile)
        stem = Path(item['raw']).stem
        stages = {}
        arrays = {}
        for stage, entry in item['stages'].items():
            path = folder / entry['file']
            if file_hash(path) != entry['sha256']:
                raise ValueError('Stage hash mismatch: ' + str(path))
            rgb = np.fromfile(path, dtype='<f4').reshape(entry['rows'], entry['cols'], 3).astype(float)
            arrays[stage] = rgb
            stages[stage] = summary(rgb, stage.startswith('dcp'))
        ipr_path = Path('tests/image/base/neutre') / (stem + '-IPR.tiff')
        lr_path = Path('tests/image/base/neutre') / (stem + '.tif')
        ipr = native_pixels(ipr_path)[::8, ::8, :3].astype(float) / 65535
        lr = native_pixels(lr_path)[::8, ::8, :3].astype(float) / 65535
        y_lr = srgb_lab(lr)[1]
        y_ipr = srgb_lab(ipr)[1]
        image = {'image': stem, 'stages': stages,
                 'tiff_ipr_sha256': file_hash(ipr_path), 'tiff_lr_sha256': file_hash(lr_path),
                 'tiff_y_mean_ipr': float(y_ipr.mean()), 'tiff_y_mean_lr': float(y_lr.mean()),
                 'lr_to_ipr_mean_y_ratio_ev': float(np.log2(y_lr.mean()/y_ipr.mean()))}
        predicted = encode(arrays['dcp_with_lcp'])
        if predicted.shape == ipr.shape:
            image['probe_vs_existing_ipr'] = compare(predicted, ipr)
            image['probe_vs_existing_ipr_max_abs_16bit_codes'] = float(np.max(np.abs(predicted-ipr))*65535)
        else:
            image['probe_vs_existing_ipr'] = {'error': 'different dimensions'}
        image['with_vs_without_lcp'] = compare(encode(arrays['dcp_without_lcp']), predicted)
        variants = dcp_ablation(arrays['camera_rgb_after_lcp'], read_dcp(profile), item['wb_kelvin'])
        image['table_space_ablation'] = {}
        for name, rgb in variants.items():
            image['table_space_ablation'][name] = summary(rgb, True)
        image['table_space_ablation']['reimplementation_max_abs_vs_cpp'] = float(
            np.max(np.abs(variants['srgb_tables']-arrays['dcp_with_lcp'])))
        image['table_space_ablation']['prophoto_vs_srgb'] = compare(
            encode(variants['srgb_tables']), encode(variants['prophoto_tables']))
        # Equal-sized native sample grids, no optical registration; no validation claim.
        if lr.shape == predicted.shape:
            image['table_space_ablation']['srgb_vs_lr_uncalibrated'] = compare(encode(variants['srgb_tables']), lr)
            image['table_space_ablation']['prophoto_vs_lr_uncalibrated'] = compare(encode(variants['prophoto_tables']), lr)
        result['images'].append(image)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', type=Path, help='Only print DCP structure, without tables')
    parser.add_argument('--probe', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.profile:
        print(json.dumps(profile_summary(args.profile), indent=2))
        return
    if not args.probe or not args.output:
        parser.error('--probe and --output are required')
    result = analyse(args.probe)
    with args.output.open('x', encoding='utf-8') as out:
        json.dump(result, out, indent=2, ensure_ascii=False, allow_nan=False)
        out.write('\n')
    for item in result['images']:
        print(item['image'], 'mean-Y ratio EV:', round(item['lr_to_ipr_mean_y_ratio_ev'], 3),
              'probe/TIFF:', item['probe_vs_existing_ipr'].get('delta_e00_mean'))


if __name__ == '__main__':
    main()
