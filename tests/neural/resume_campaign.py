"""Resume a stopped training campaign up to its planned length, in a new campaign folder.

The child reuses the parent's arrays (hard links), restores its last complete update
(resume.pt: weights, Adam moments, data/CPU/CUDA random states) and continues the same
cosine schedule; controls and checkpoints keep their global steps, shifted to the child.
"""
import argparse
import os
from datetime import datetime, timezone
from pathlib import Path

from physical_natural_training import ROOT, digest, read, write


def resumed_steps(global_steps, completed, total):
    """The parent's global steps after `completed`, as child steps; the end always included."""
    return sorted({s-completed for s in global_steps if completed < s <= total} | {total-completed})


def prepare(parent, output, seed, hours):
    plan = read(parent/'plan.json')
    progress = parent/f'seed{seed}'/'progress.json'
    checkpoint = parent/f'seed{seed}'/'resume.pt'
    state = read(progress)
    if state['status'] != 'stopped':
        raise ValueError('Only a stopped campaign is resumed')
    if digest(checkpoint) != state['final_checkpoint_sha256']:
        raise ValueError('Parent checkpoint differs from its progress')
    if plan.get('continuation'):
        raise ValueError('Resuming a continuation is not supported')
    completed, total = state['completed_steps'], plan['steps']
    if not 0 < completed < total:
        raise ValueError('Nothing to resume')
    data = read(parent/'data.json')
    output.mkdir()
    for name in data['files']:
        os.link(parent/name, output/name)
    write(output/'data.json', data)
    tools = {name: digest(ROOT/'tests/neural'/name) for name in (*plan['tools'], 'resume_campaign.py')}
    child = {**plan, 'status': 'frozen_resumed_campaign', 'created_at_utc': datetime.now(timezone.utc).isoformat(),
        'steps': total-completed, 'campaign_seconds': int(hours*3600), 'seeds': [seed], 'tools': tools,
        'controls_at': [0, *resumed_steps(plan['controls_at'], completed, total)],
        'checkpoint_at': resumed_steps(plan['checkpoint_at'], completed, total),
        'continuation': dict(checkpoint=str(checkpoint.resolve()), checkpoint_sha256=digest(checkpoint),
            completed_steps=completed, plan_sha256=digest(parent/'plan.json'), progress=str(progress.resolve()),
            progress_sha256=digest(progress), continue_schedule=True,
            optimizer='Restore identical Adam moments and step counter',
            random_state='Restore identical data generator, CPU and CUDA RNG state',
            reason=state['error']),
        'limits': plan['limits']+[f'Resumed at {completed} of {total} updates after: {state["error"]}']}
    write(output/'plan.json', child)
    return child


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--parent', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seed', type=int, default=71)
    parser.add_argument('--hours', type=float, required=True)
    args = parser.parse_args()
    plan = prepare(args.parent.resolve(), args.output.resolve(), args.seed, args.hours)
    print(plan['steps'], plan['controls_at'], plan['checkpoint_at'])
