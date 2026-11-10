"""Audit of the multicoloured sheet series of 10 November before it is used for evaluation.

For every burst (one ISO and one shutter time): split-half stability against
the burst's own temporal noise, light drift inside the burst, integer packed
shift and per-plane gain against the mean of the brightest ISO100 burst, and
the ratio of that gain to the nominal exposure ratio (ISO x time), which
tracks the light over the 23 minutes of capture. Also the share of sites at
or above white. Read only on the card; the ISO100 reference means are cached
in the build folder for the later evaluation.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path

from prepare_next_training import digest, write
from real_noise_bench import CARD, burst_statistics, files, gain_offset, phase_shift, split_half_ratio


ROOT = Path(__file__).resolve().parents[2]
# (ISO, shutter seconds, first, last) as listed by the card metadata.
BURSTS = ((100, .25, 588, 591), (100, .125, 592, 595), (100, 1/15, 596, 599), (100, 1/30, 600, 603),
          (1600, 1/80, 604, 607), (1600, 1/160, 608, 611), (1600, 1/320, 612, 615), (1600, 1/640, 616, 619),
          (3200, 1/160, 620, 623), (3200, 1/320, 624, 627), (3200, 1/640, 628, 632), (3200, 1/1250, 633, 637),
          (6400, 1/320, 638, 642), (6400, 1/640, 643, 647), (6400, 1/1250, 648, 652), (6400, 1/2500, 653, 658),
          (12800, 1/1000, 659, 663), (12800, 1/2000, 664, 669), (12800, 1/4000, 670, 675), (12800, 1/8000, 676, 681),
          (16000, 1/1000, 682, 685), (16000, 1/2000, 686, 689), (16000, 1/4000, 690, 693), (16000, 1/8000, 694, 697),
          (25600, 1/1600, 698, 701), (25600, 1/3200, 702, 706), (25600, 1/5000, 707, 710), (25600, 1/8000, 711, 715),
          (51200, 1/1600, 716, 719), (51200, 1/3200, 720, 723), (51200, 1/5000, 724, 728), (51200, 1/8000, 729, 734))


def run(output):
    import numpy as np
    import rawpy
    output.mkdir(exist_ok=False)
    reference, reference_exposure = None, None
    rows = []
    for iso, shutter, first, last in BURSTS:
        paths = files(first, last, CARD)
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
    result = dict(schema_version=1, status='audit_of_colour_sheet_series', measured_at_utc=datetime.now(timezone.utc).isoformat(),
        rows=rows, tool_sha256=digest(Path(__file__)),
        limits=['Card files read in place; capture order and tripod/phone release as stated by the user',
                'No ISO100 burst after the high-ISO bursts: light stability is inferred from gain over nominal exposure',
                'ISO100 and high ISO use different black and white normalizations; gain_over_nominal compares bursts, not absolute levels'])
    write(output/'result.json', result)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    with (ROOT/'docs/validation/raw'/f'neural-{output.name}-{stamp}.json').open('x', encoding='utf-8') as stream:
        json.dump(result, stream, allow_nan=False, separators=(',', ':'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    run(parser.parse_args().output.resolve())
