"""Balanced relative RGB signs with matched procedural carrier and magnitudes."""
from collections import Counter,defaultdict
import itertools
import math

import torch

from audit_texture_coverage import FREQUENCY,AMPLITUDE,bin_index
from texture_sampling import sample_events
from training_texture_data import training_texture


def colour_class(colours):
    if len(colours)!=3 or any(not math.isfinite(x) or x==0 for x in colours):
        raise ValueError('Three finite nonzero chromatic coefficients required')
    red,green,blue=colours
    return 2*int(red*green<0)+int(red*blue<0)


def colour_group(event):
    return (bin_index(event['frequency'],FREQUENCY),bin_index(event['amplitude'],AMPLITUDE),event['serial']%3)


def colour_coverage(events):
    counts=Counter((*colour_group(e),colour_class(e['colours'])) for e in events if e['family']=='chromatic_wave')
    cells=[dict(frequency_bin=f,amplitude_bin=a,noise_index=n,relative_sign_class=c,exposures=counts[f,a,n,c])
        for f,a,n,c in itertools.product(range(4),range(3),(1,2),range(4))]
    return dict(positive_joint_cells=len(cells),missing_cells=sum(r['exposures']==0 for r in cells),cells=cells,
        scope='Colour signs conditional on frequency/amplitude/noise, pooled across Bayer phases. Not full phase-by-sign coverage.')


def balanced_signs(events):
    groups=defaultdict(list)
    for event in events:
        if event['family']=='chromatic_wave':
            groups[colour_group(event)].append(event)
    assignments={}
    for index,(group,rows) in enumerate(sorted(groups.items())):
        if len(rows)<4:
            raise ValueError('Four chromatic exposures required per conditional sign group')
        labels=[(j+index)%4 for j in range(len(rows))]
        order=torch.randperm(len(labels),generator=torch.Generator().manual_seed(340000000+index)).tolist()
        for row,position in zip(sorted(rows,key=lambda r:r['serial']),order):
            desired=labels[position]
            actual=colour_class(row['colours'])
            assignments[row['serial']]=[1,-1 if actual//2!=desired//2 else 1,-1 if actual%2!=desired%2 else 1]
    return assignments


def sample_colour_pair(templates,excluded,guard,hash_tensor):
    baseline,selection=sample_events(templates,'stratified',excluded,guard,hash_tensor,identity_start=12000000)
    assignments=balanced_signs(baseline)
    control,candidate=[],[]
    for event in baseline:
        guard.check()
        plain=dict(event,colour_flips=[1,1,1])
        changed=dict(event,colour_flips=assignments.get(event['serial'],[1,1,1]))
        image,metadata=training_texture(changed)
        fingerprint=hash_tensor(image)
        if fingerprint in excluded:
            raise ValueError('Transformed content collided with known excluded target; do not silently resample')
        changed.update(metadata,target_sha256=fingerprint)
        control.append(plain)
        candidate.append(changed)
    if len({e['target_sha256'] for e in candidate})!=len(candidate):
        raise ValueError('Duplicate transformed target')
    return {'control':control,'colour_balanced':candidate},dict(base_selection=selection,
        shared_targets=sum(a['target_sha256']==b['target_sha256'] for a,b in zip(control,candidate)),
        transformed_targets=sum(a['target_sha256']!=b['target_sha256'] for a,b in zip(control,candidate)),
        prior_colour_coverage=colour_coverage(templates),control_colour_coverage=colour_coverage(control),
        candidate_colour_coverage=colour_coverage(candidate),
        policy='Same fresh stratified carriers, channel magnitudes, phase/noise/exposure schedule; only G/B contrast signs about DC can change. Four relative sign classes balanced per frequency/amplitude/noise, pooled across phases. No validation output selection.')
