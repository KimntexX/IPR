"""Audit of a paired real-noise scene series before it is used for evaluation.

Scenes: the multicoloured sheet of 10 November (ISO100 bursts before only)
and the climbing gym of 11 November (ISO100 bursts before and after). For
every burst (one ISO and one shutter time): split-half stability against the
burst's own temporal noise, light drift inside the burst, integer packed
shift and per-plane gain against the mean of the first, brightest ISO100
burst, and the ratio of that gain to the nominal exposure ratio (ISO x time),
which tracks the light over the capture. Final ISO100 bursts, when present,
check the scene and the light directly. Also the share of sites at or above
white. Read only; the reference means are cached in the build folder for the
later evaluation.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path

from prepare_next_training import digest, write
from real_noise_bench import CARD, burst_statistics, files, gain_offset, phase_shift, split_half_ratio


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT/'build-patch_neural_restoration'
PROTOCOL = ROOT.parent.parent/'tests'/'image'/'colorimétrie'/'protocole'


def locate_series(first_file, card=Path('D:/DCIM')):
    """Folder holding a series, found by its first file: the camera reuses folder names (100EOSR6, 101EOSR6),
    so the protocol folders are searched first, then the card; the first match wins."""
    for root in (PROTOCOL, card):
        if root.is_dir():
            for folder in sorted(p for p in root.iterdir() if p.is_dir()):
                if (folder/first_file).is_file():
                    return folder
    raise FileNotFoundError(f'{first_file} found neither in {PROTOCOL} nor on the card')


# (ISO, shutter seconds, first, last) as listed by the card metadata.
SHEET_BURSTS = ((100, .25, 588, 591), (100, .125, 592, 595), (100, 1/15, 596, 599), (100, 1/30, 600, 603),
          (1600, 1/80, 604, 607), (1600, 1/160, 608, 611), (1600, 1/320, 612, 615), (1600, 1/640, 616, 619),
          (3200, 1/160, 620, 623), (3200, 1/320, 624, 627), (3200, 1/640, 628, 632), (3200, 1/1250, 633, 637),
          (6400, 1/320, 638, 642), (6400, 1/640, 643, 647), (6400, 1/1250, 648, 652), (6400, 1/2500, 653, 658),
          (12800, 1/1000, 659, 663), (12800, 1/2000, 664, 669), (12800, 1/4000, 670, 675), (12800, 1/8000, 676, 681),
          (16000, 1/1000, 682, 685), (16000, 1/2000, 686, 689), (16000, 1/4000, 690, 693), (16000, 1/8000, 694, 697),
          (25600, 1/1600, 698, 701), (25600, 1/3200, 702, 706), (25600, 1/5000, 707, 710), (25600, 1/8000, 711, 715),
          (51200, 1/1600, 716, 719), (51200, 1/3200, 720, 723), (51200, 1/5000, 724, 728), (51200, 1/8000, 729, 734))
GYM_BURSTS = ((100, .25, 758, 762), (100, .125, 763, 767), (100, 1/15, 768, 772), (100, 1/30, 773, 777),
              (1600, 1/60, 778, 782), (1600, 1/125, 783, 788), (1600, 1/250, 789, 793), (1600, 1/500, 794, 800),
              (3200, 1/100, 801, 805), (3200, 1/200, 806, 810), (3200, 1/400, 811, 816), (3200, 1/800, 817, 821),
              (6400, 1/200, 822, 826), (6400, 1/400, 827, 831), (6400, 1/800, 832, 836), (6400, 1/1250, 837, 841),
              (12800, 1/500, 842, 846), (12800, 1/1000, 847, 851), (12800, 1/2000, 852, 857), (12800, 1/4000, 858, 863),
              (16000, 1/640, 864, 867), (16000, 1/1250, 868, 875), (16000, 1/2500, 876, 881),
              (25600, 1/800, 882, 887), (25600, 1/1600, 888, 893), (25600, 1/3200, 894, 899), (25600, 1/6400, 900, 905),
              (51200, 1/1000, 906, 911), (51200, 1/2000, 912, 917), (51200, 1/4000, 918, 924), (51200, 1/8000, 925, 931),
              (100, .25, 932, 936), (100, .125, 937, 942), (100, 1/15, 943, 950), (100, 1/30, 951, 957))
SCENES = {'color_sheet': dict(folder=CARD, audit=BUILD/'color-sheet-audit-v001', bursts=SHEET_BURSTS),
          'climbing_gym': dict(folder=None, first_file='6V1A0758.CR3', audit=BUILD/'climbing-gym-audit-v001', bursts=GYM_BURSTS)}


def scene_folder(scene):
    table = SCENES[scene]
    return table['folder'] if table['folder'] is not None else locate_series(table['first_file'])


def run(scene, output):
    import numpy as np
    import rawpy
    output.mkdir(exist_ok=False)
    reference, reference_exposure = None, None
    rows = []
    folder = scene_folder(scene)
    for iso, shutter, first, last in SCENES[scene]['bursts']:
        paths = files(first, last, folder)
        missing = [p.name for p in paths if not p.exists()]
        if missing:
            raise FileNotFoundError(f'ISO {iso} {shutter:g} s: missing {missing}')
        total, half, means, temporal = burst_statistics(paths, iso)
        count = len(paths)
        mean = (total/count).astype(np.float32)
        with rawpy.imread(str(paths[0])) as raw:
            saturated = float((raw.raw_image_visible >= raw.white_level-64).mean())
        plane_means = np.array(means)
        row = dict(iso=iso, shutter_s=shutter, first=paths[0].name, last=paths[-1].name, frames=count,
                   saturated_fraction=saturated,
                   light_drift_first_to_last=((plane_means[-1]-plane_means[0])/np.maximum(plane_means.mean(axis=0), 1e-12)).tolist(),
                   split_half_ratio=split_half_ratio(half, count//2, total-half, count-count//2, temporal) if count >= 4 else None,
                   temporal_sigma_by_plane=np.sqrt(temporal).tolist())
        if reference is None:
            reference, reference_exposure = mean, iso*shutter
            np.save(output/'iso100-brightest-mean.npy', mean, allow_pickle=False)
            row['reference_mean_sha256'] = digest(output/'iso100-brightest-mean.npy')
        else:
            green_ref, green = reference[1:3].mean(axis=0), mean[1:3].mean(axis=0)
            h, w = green_ref.shape
            crop = (slice(h//2-512, h//2+512), slice(w//2-768, w//2+768))
            shift, sharpness = phase_shift(green_ref[crop], green[crop])
            fitted = gain_offset(reference[:, crop[0], crop[1]], mean[:, crop[0], crop[1]])
            # ISO100 is normalized with black 512, the others with 2048: gains compare exposures up to that scale.
            nominal = reference_exposure/(iso*shutter)
            row.update(shift_packed_cells=shift, correlation_peak_sharpness=sharpness, gain_offset_to_reference=fitted,
                       nominal_exposure_ratio=nominal, gain_over_nominal=[g['gain']/nominal for g in fitted])
        rows.append(row)
        write(output/'progress.json', dict(status='running', rows=rows))
    result = dict(schema_version=2, status='audit_of_paired_scene_series', scene=scene, folder=str(folder),
        measured_at_utc=datetime.now(timezone.utc).isoformat(), rows=rows, tool_sha256=digest(Path(__file__)),
        limits=['Card files read in place; capture order and tripod/phone release as stated by the user',
                'Without ISO100 bursts after the high-ISO bursts, light stability is only inferred from gain over nominal exposure',
                'ISO100 and high ISO use different black and white normalizations; gain_over_nominal compares bursts, not absolute levels'])
    write(output/'result.json', result)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    with (ROOT/'docs/validation/raw'/f'neural-{output.name}-{stamp}.json').open('x', encoding='utf-8') as stream:
        json.dump(result, stream, allow_nan=False, separators=(',', ':'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene', choices=sorted(SCENES), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(args.scene, args.output.resolve())
