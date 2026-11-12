"""Metadata-only joint coverage audit; no generated or opened validation data."""
import argparse
from collections import Counter
from datetime import datetime,timezone
import itertools
import json
import math
from pathlib import Path

from diagnose_train_learnability import digest,save


FAMILIES = ('weak_achromatic_wave','chromatic_wave')
FREQUENCY = (2.,6.,12.,20.,29.)
AMPLITUDE = (.003,.01,.04,.121)


def bin_index(value,bounds):
    if isinstance(value,bool) or not isinstance(value,(int,float)) or not math.isfinite(value):
        raise ValueError('Finite numeric metadata required')
    for i in range(len(bounds)-1):
        if bounds[i] <= value < bounds[i+1]:
            return i
    raise ValueError('Metadata outside declared bins')


def coverage(events):
    count = Counter()
    serials = set()
    for e in events:
        if (e['family'] not in FAMILIES or isinstance(e['serial'],bool) or not isinstance(e['serial'],int)
                or e['serial'] < 0 or e['serial'] in serials or isinstance(e['phase'],bool)
                or not isinstance(e['phase'],int) or e['phase'] not in range(4)):
            raise ValueError('Unique valid texture exposure and phase required')
        serials.add(e['serial'])
        key = (e['family'],e['phase'],e['serial']%3,bin_index(e['frequency'],FREQUENCY),bin_index(e['amplitude'],AMPLITUDE))
        count[key] += 1
    cells = []
    for family,phase,noise,frequency,amplitude in itertools.product(FAMILIES,range(4),(1,2),range(4),range(3)):
        cells.append(dict(family=family,phase=phase,noise_index=noise,frequency_bin=frequency,
            amplitude_bin=amplitude,exposures=count[family,phase,noise,frequency,amplitude]))
    return dict(texture_exposures=len(events),positive_joint_cells=len(cells),
        missing_cells=sum(r['exposures']==0 for r in cells),singleton_cells=sum(r['exposures']==1 for r in cells),
        cells=cells)


def run(root,source,output):
    base = root/'build-patch_neural_restoration'
    if not all(p.resolve().is_relative_to(base.resolve()) for p in (source,output)):
        raise ValueError('Dedicated neural paths required')
    plan = json.loads(source.read_text(encoding='utf-8'))
    events = plan['texture_augmentation']['events']
    now = datetime.now(timezone.utc)
    identity = 'neural-texture-coverage-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    result = dict(schema_version=1,campaign_id=identity,measured_at_utc=now.isoformat(),
        status='completed_known_training_coverage_audit_not_qualification',source_sha256=digest(source),
        script_sha256=digest(Path(__file__)),frequency_bin_edges=FREQUENCY,amplitude_bin_edges=AMPLITUDE,
        noise_recipe='Exposure serial modulo3, unchanged actual training policy.',
        scopes={'initializer2048':coverage(events),'continuation512':coverage([e for e in events if e['serial']<2048])},
        limits=['Exploratory post-result bins; no statistical significance or causal proof.',
            'Model seeds do not duplicate exposure coverage; replay does not add distinct contents.',
            'Coarse cells ignore direction, DC, second wave and channel-specific amplitude; nonempty does not mean sufficient.',
            'No new training, noise draw or reserved image generated.'])
    output.mkdir(parents=True,exist_ok=False)
    save(output/'result.json',result)
    save(root/'docs/validation/raw'/f'{identity}.json',result)
    for name,scope in result['scopes'].items():
        print(name,scope['texture_exposures'],scope['missing_cells'],scope['singleton_cells'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args = parser.parse_args()
    run(Path(__file__).resolve().parents[2],args.source,args.output)
