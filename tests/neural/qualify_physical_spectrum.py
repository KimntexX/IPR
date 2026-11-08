"""Validate frozen shot covariance and the measurement instrument on known controls.

Only marginal temporal noise is tested here. No training or whole-bank promotion.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import platform
import time
import numpy as np
from fit_photon_covariance import colour_innovations, dark_sources
from physical_measurement import vectors, simultaneous, classify, binomial_upper_tail
from prepare_next_training import digest, read, write


def simulate(read_frames, signal, gain, gamma_x, gamma_y, seed, indices):
    """Shared read-frame identities preserve covariance across fields of a capture."""
    first, second = indices
    rng = np.random.default_rng(seed)
    shape = (len(first), *read_frames.shape[-2:])
    if gain <= 0 or signal < 0:
        raise ValueError('Positive photon gain and nonnegative signal required')
    photons = []
    for _ in range(2):
        innovations = gain * rng.poisson(signal / gain, size=shape) - signal
        photons.append(np.rint(signal + colour_innovations(innovations, gamma_x, gamma_y)))
    return photons[0] - photons[1] + read_frames[first].astype(np.float64) - read_frames[second].astype(np.float64)


def aggregate(rows, key):
    """Each capture pair has equal weight; its eligible ROIs share that weight."""
    groups = sorted({(r['iso'], r['plane']) for r in rows})
    values, identities = [], []
    for iso, plane in groups:
        selected = [r for r in rows if (r['iso'], r['plane']) == (iso, plane)]
        pairs = sorted({r['pair'] for r in selected})
        by_pair = [np.mean([np.asarray(r[key]) for r in selected if r['pair'] == pair], axis=0) for pair in pairs]
        values.append(np.mean(by_pair, axis=0))
        identities.append(dict(iso=iso, plane=plane, pairs=pairs, fields=len(selected)))
    return np.stack(values, axis=-2), identities


def run(root, output, repetitions=128):
    from memory_budget import MemoryGuard
    if repetitions < 128 or repetitions % 2:
        raise ValueError('Even replicate count >=128 required')
    build = root / 'build-patch_neural_restoration'
    coefficient_path = build / 'photon-covariance-fit-v001/result.json'
    coefficients = read(coefficient_path)
    bank, sources = dark_sources(root)
    fits = {(r['iso'], r['plane']): r['fit'] for r in bank['fits']}
    gammas = {(r['iso'], r['plane']): r for r in coefficients['coefficients']}
    pairs = {r['index']: r for r in bank['pairs']}
    output.mkdir(exist_ok=False)
    now = datetime.now(timezone.utc)
    result = dict(schema_version=1, data_version='v001', campaign_id='physical-spectrum-' + now.strftime('%Y%m%dT%H%M%SZ'),
        started_at_utc=now.isoformat(), status='started', rows=[],
        provenance=dict(coefficients_path=str(coefficient_path), coefficients_sha256=digest(coefficient_path),
            bank_sha256=digest(root/'docs/validation/raw/neural-sensor-noise-v001-20261105T173608Z.json')),
        software=dict(python=platform.python_version(), numpy=np.__version__),
        tools={name: digest(Path(__file__).with_name(name)) for name in
            ('qualify_physical_spectrum.py', 'physical_measurement.py', 'fit_photon_covariance.py', 'memory_budget.py')},
        criteria=dict(repetitions=repetitions, practical_variance_ratio=1.05, practical_lag=.02,
            practical_psd_ratio=1.10, simultaneous_quantile=.95, null_binomial_p_min=.01,
            positive_control_detection_min=.90, max_wall_seconds=1200),
        limits=['Original known heldout exposures, not a fresh final reserve',
            'Nearest-neighbour covariance fitted on original training exposures only',
            'Pair differences cancel fixed pattern; no joint colour or natural-reference qualification',
            'Empirical read pairs plus separately rounded correlated photon innovations, no ADC clipping',
            'Monte Carlo calibration conditions on finite read library and fixed fitted parameters',
            'Practical systematic-effect bounds replace an invalid pointwise absolute-periodogram criterion'])
    write(output/'freeze.json', {k:v for k,v in result.items() if k != 'rows'})
    guard = MemoryGuard(hard_bytes=2*1024**3, soft_bytes=1536*1024**2, system_fraction=.8)
    started = time.monotonic()
    try:
        with guard:
            read_indices = {}
            for (iso, plane), fit in sorted(fits.items()):
                if iso not in sources:
                    continue
                gamma = gammas[(iso, plane)]
                for field in fit.get('heldout', []):
                    guard.check()
                    if time.monotonic()-started > 1200:
                        raise TimeoutError('Physical spectrum wall budget reached')
                    pair, roi = field['pair'], field['roi']
                    key = (iso, pair)
                    if key not in read_indices:
                        rng = np.random.default_rng(821000 + pair*100 + iso)
                        n = len(sources[iso])
                        first = rng.integers(n, size=repetitions)
                        read_indices[key] = first, (first+rng.integers(1,n,size=repetitions)) % n
                    arrays=[]
                    for index in pairs[pair]['frames']:
                        frame=bank['frames'][index]; path=root/frame['cached_tiles']
                        if digest(path)!=frame['tiles_sha256']:
                            raise ValueError('Light source cache changed')
                        arrays.append(np.load(path,allow_pickle=False)[roi,plane].astype(np.float64))
                    delta=simulate(sources[iso][:,roi,plane],field['signal_dn'],fit['shot_slope_dn'],
                        gamma['gamma_x'],gamma['gamma_y'],831000+pair*100+roi*4+plane,read_indices[key])
                    simulated=vectors(delta)
                    # Independent validation replicates are not used to set the uncertainty.
                    correlation_control=vectors(colour_innovations(delta[repetitions//2:],.04,.04))
                    result['rows'].append(dict(iso=iso,plane=plane,pair=pair,roi=roi,
                        signal_dn=field['signal_dn'],observed=vectors(arrays[0]-arrays[1])[0].tolist(),
                        simulated=simulated.tolist(),correlation_control=correlation_control.tolist()))
                    write(output/'progress.json',dict(status='running',fields=len(result['rows']),iso=iso,plane=plane,
                        elapsed_seconds=time.monotonic()-started,memory=guard.stats()))
            observed,identities=aggregate(result['rows'],'observed')
            simulated,_=aggregate(result['rows'],'simulated')
            correlated,_=aggregate(result['rows'],'correlation_control')
            middle=repetitions//2
            measurement=simultaneous(simulated[:middle],observed)
            null=[classify(value,measurement) for value in simulated[middle:]]
            variance_control=simulated[middle:].copy();variance_control[:,:,0]+=np.log(1.2)
            positives=dict(variance=[classify(value,measurement) for value in variance_control],
                           correlation=[classify(value,measurement) for value in correlated])
            null_rejections=null.count('outside_bounds')
            instrument=dict(null_rejections=null_rejections,validation_replicates=middle,
                null_binomial_upper_tail=binomial_upper_tail(null_rejections,middle),
                positive_detection={k:v.count('outside_bounds')/middle for k,v in positives.items()})
            instrument['passed']=(instrument['null_binomial_upper_tail']>=.01
                and all(v>=.9 for v in instrument['positive_detection'].values()))
            result.update(groups=identities,measurement=measurement,instrument=instrument,
                status=('marginal_'+measurement['status']+'_not_full_bench' if instrument['passed']
                        else 'measurement_instrument_not_qualified'))
    except Exception as error:
        result.update(status='stopped',error=str(error))
        raise
    finally:
        result.update(finished_at_utc=datetime.now(timezone.utc).isoformat(),memory=guard.stats(),
                      elapsed_seconds=time.monotonic()-started)
        write(output/'result.json',result)
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-physical-spectrum-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,allow_nan=False,separators=(',',':'))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    run(Path(__file__).resolve().parents[2],args.output)
