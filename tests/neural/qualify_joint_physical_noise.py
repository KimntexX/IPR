"""Check all six native Bayer-plane correlations on the original known controls."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import platform
import time
import numpy as np
from fit_photon_covariance import dark_sources
from joint_physical_noise import correlations, PLANE_PAIRS
from physical_measurement import simultaneous, classify, binomial_upper_tail
from prepare_next_training import digest, read, write
from qualify_physical_spectrum import simulate, aggregate


def run(root, output):
    from memory_budget import MemoryGuard
    build=root/'build-patch_neural_restoration'
    coefficient_path=build/'photon-covariance-fit-v001/result.json'
    coefficients=read(coefficient_path)
    gamma={(r['iso'],r['plane']):r for r in coefficients['coefficients']}
    bank,sources=dark_sources(root)
    fits={(r['iso'],r['plane']):r['fit'] for r in bank['fits']}
    eligible={}
    for row in bank['measurements']:
        gate=row.get('fit_gate',{})
        if row['iso'] in sources and gate.get('eligible') and gate.get('split')=='heldout':
            eligible.setdefault((row['pair'],row['roi']),{})[row['plane']]=gate['signal_dn']
    # Eligibility depends only on pre-existing physical gates, never on joint correlation.
    fields={k:v for k,v in eligible.items() if len(v)==4}
    output.mkdir(exist_ok=False)
    now=datetime.now(timezone.utc)
    result=dict(schema_version=1,data_version='v001',campaign_id='physical-joint-'+now.strftime('%Y%m%dT%H%M%SZ'),
        measured_at_utc=now.isoformat(),status='started',rows=[],
        provenance=dict(coefficients_path=str(coefficient_path),coefficients_sha256=digest(coefficient_path),
            bank_sha256=digest(root/'docs/validation/raw/neural-sensor-noise-v001-20261105T173608Z.json')),
        software=dict(python=platform.python_version(),numpy=np.__version__),
        tools={name:digest(Path(__file__).with_name(name)) for name in ('qualify_joint_physical_noise.py',
            'joint_physical_noise.py','qualify_physical_spectrum.py','fit_photon_covariance.py',
            'physical_measurement.py','memory_budget.py')},
        criteria=dict(absolute_correlation_effect=.02,simultaneous_quantile=.95,replicates=128,
            independent_validation_replicates=64,positive_detection_min=.9,null_binomial_p_min=.01,
            positive_control='Plane1 := .1*plane0 + sqrt(.99)*plane1',max_wall_seconds=900),
        limits=['Zero-lag correlation between packed Bayer positions only; not all spatial cross-spectra',
            'Shared empirical dark identities, independent photon innovations per plane',
            'Known original heldout exposures, not a fresh final reserve',
            'Fixed pattern cancels in differences; fitted-parameter uncertainty not included'])
    write(output/'freeze.json',{k:v for k,v in result.items() if k!='rows'})
    guard=MemoryGuard(hard_bytes=2*1024**3,soft_bytes=1536*1024**2,system_fraction=.8)
    started=time.monotonic()
    try:
        with guard:
            for pair in bank['pairs']:
                selected=[(roi,signals) for (index,roi),signals in fields.items() if index==pair['index']]
                if not selected:
                    continue
                iso=pair['iso']
                rng=np.random.default_rng(921000+pair['index']*100+iso)
                count=len(sources[iso]);first=rng.integers(count,size=128)
                indices=first,(first+rng.integers(1,count,size=128))%count
                observed=[]
                for index in pair['frames']:
                    frame=bank['frames'][index];path=root/frame['cached_tiles']
                    if digest(path)!=frame['tiles_sha256']:
                        raise ValueError('Original control cache changed')
                    observed.append(np.load(path,allow_pickle=False).astype(np.float64))
                for roi,signals in selected:
                    guard.check()
                    if time.monotonic()-started>900:
                        raise TimeoutError('Joint physical campaign wall budget reached')
                    generated=[]
                    for plane in range(4):
                        g=gamma[(iso,plane)]
                        generated.append(simulate(sources[iso][:,roi,plane],signals[plane],
                            fits[(iso,plane)]['shot_slope_dn'],g['gamma_x'],g['gamma_y'],
                            931000+pair['index']*100+roi*4+plane,indices))
                    delta=np.stack(generated,axis=1)
                    simulated=correlations(delta)
                    altered=delta[64:].copy()
                    altered[:,1]=.1*altered[:,0]+np.sqrt(.99)*altered[:,1]
                    result['rows'].append(dict(iso=iso,plane=0,pair=pair['index'],roi=roi,
                        observed=correlations(observed[0][roi]-observed[1][roi])[0].tolist(),
                        simulated=simulated.tolist(),positive_control=correlations(altered).tolist()))
                    write(output/'progress.json',dict(status='running',fields=len(result['rows']),iso=iso,
                        elapsed_seconds=time.monotonic()-started,memory=guard.stats()))
            observed,groups=aggregate(result['rows'],'observed')
            simulated,_=aggregate(result['rows'],'simulated')
            positive,_=aggregate(result['rows'],'positive_control')
            measurement=simultaneous(simulated[:64],observed,bounds=np.full(6,.02),
                names=[f'plane{i}_plane{j}' for i,j in PLANE_PAIRS])
            null=sum(classify(value,measurement)=='outside_bounds' for value in simulated[64:])
            detection=sum(classify(value,measurement)=='outside_bounds' for value in positive)/64
            instrument=dict(null_rejections=null,null_binomial_upper_tail=binomial_upper_tail(null,64),
                positive_detection=detection)
            instrument['passed']=instrument['null_binomial_upper_tail']>=.01 and detection>=.9
            result.update(groups=groups,measurement=measurement,instrument=instrument,
                status=('joint_'+measurement['status']+'_not_full_bench' if instrument['passed']
                        else 'joint_measurement_instrument_not_qualified'))
    except Exception as error:
        result.update(status='stopped',error=str(error))
        raise
    finally:
        result.update(finished_at_utc=datetime.now(timezone.utc).isoformat(),memory=guard.stats(),
                      elapsed_seconds=time.monotonic()-started)
        write(output/'result.json',result)
        write(output/'progress.json',{k:v for k,v in result.items() if k!='rows'})
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-joint-physical-noise-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,allow_nan=False,separators=(',',':'))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    run(Path(__file__).resolve().parents[2],args.output)
