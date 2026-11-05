"""Separate training exposure identities from fresh procedural texture identities."""
from collections import Counter,defaultdict
import math

import torch

from audit_texture_coverage import FREQUENCY,AMPLITUDE,bin_index
from fine_texture_training import texture


def texture_identity(event):
    identity = event.get('texture_serial',event['serial'])
    if isinstance(identity,bool) or not isinstance(identity,int) or identity < 0:
        raise ValueError('Nonnegative texture identity required')
    return identity


def joint_quotas(count):
    """At least one per cell, then nearest generator-probability allocation."""
    if isinstance(count,bool) or not isinstance(count,int) or count < 12:
        raise ValueError('At least12 exposures required per conditional group')
    frequency = (2.,6.,12.,20.,28.)
    amplitude = (.003,.01,.04,.12)
    cells = [(f,a) for f in range(4) for a in range(3)]
    probability = {(f,a):math.log(frequency[f+1]/frequency[f])/math.log(14.)
        *math.log(amplitude[a+1]/amplitude[a])/math.log(40.) for f,a in cells}
    quota = {cell:1 for cell in cells}
    for _ in range(count-12):
        chosen = max(cells,key=lambda cell:(count*probability[cell]-quota[cell],-cells.index(cell)))
        quota[chosen] += 1
    return quota


def strata(events):
    groups = defaultdict(list)
    for event in events:
        groups[event['family'],event['phase'],event['serial']%3].append(event['serial'])
    result = {}
    for index,(group,serials) in enumerate(sorted(groups.items())):
        quota = joint_quotas(len(serials))
        cells = [cell for cell,n in sorted(quota.items()) for _ in range(n)]
        order = torch.randperm(len(cells),generator=torch.Generator().manual_seed(330000000+index)).tolist()
        result.update({serial:cells[position] for serial,position in zip(sorted(serials),order)})
    return result


def sample_events(templates,arm,excluded,guard,hash_tensor):
    if arm not in ('control','stratified'):
        raise ValueError('Declared fresh-random or stratified arm required')
    assignments = strata(templates) if arm == 'stratified' else {}
    next_identity = 4000000 if arm == 'control' else 8000000
    rows,seen,attempts = [],set(excluded),0
    for template in templates:
        for _ in range(10000):
            if attempts%32 == 0:
                guard.check()
            identity = next_identity
            next_identity += 1
            attempts += 1
            truth,metadata = texture(template['family'],identity,128)
            cell = (bin_index(metadata['frequency'],FREQUENCY),bin_index(metadata['amplitude'],AMPLITUDE))
            if arm == 'stratified' and cell != assignments[template['serial']]:
                continue
            fingerprint = hash_tensor(truth)
            if fingerprint in seen:
                continue
            seen.add(fingerprint)
            rows.append(dict(serial=template['serial'],texture_serial=identity,family=template['family'],
                phase=template['phase'],target_sha256=fingerprint,**metadata))
            break
        else:
            raise ValueError('Bounded fresh-texture selection exhausted')
    return rows,dict(candidate_draws=attempts,accepted=len(rows),
        selection='Fresh independent ID stream; rejection on declared frequency/amplitude strata only in candidate. No validation pixels/parameters used.',
        conditional_counts=[dict(family=f,phase=p,noise_index=n,exposures=k)
            for (f,p,n),k in sorted(Counter((r['family'],r['phase'],r['serial']%3) for r in rows).items())])
