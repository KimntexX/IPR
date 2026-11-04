"""Declared training transition and exact completion budgets, no score selection."""
import math


def secondary_weight(config, step):
    schedule = config.get('structure_schedule')
    if schedule is None:
        return 1.
    start, end, ramp = (schedule[k] for k in ('start', 'end', 'ramp_steps'))
    if (isinstance(step, bool) or not isinstance(step, int) or not 1 <= step <= config['steps']
            or isinstance(ramp, bool) or not isinstance(ramp, int) or ramp < 2
            or any(isinstance(v, bool) or not isinstance(v, (int, float))
                or not math.isfinite(v) or not 0 <= v <= 1 for v in (start, end))):
        raise ValueError('Declared continuous structure transition required')
    fraction = min(1., (step-1)/(ramp-1))
    return start+(end-start)*fraction


def completed_training(proof):
    plan = proof['plan']
    steps = plan['config']['steps']
    progress = proof['training_progress']
    if progress.get('completed_optimizer_steps') != steps:
        return False
    warm = plan.get('warm_start')
    if warm is None:
        return steps == 2048 and 'warm_start' not in proof and progress.get('total_optimizer_steps', steps) == steps
    refs = [r for r in warm['models'] if r['seed'] == proof['seed']]
    return (steps == 512 and warm['completed_optimizer_steps'] == 2048 and len(refs) == 1
        and proof.get('warm_start') == refs[0] and progress.get('total_optimizer_steps') == 2560)
