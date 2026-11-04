"""Sequential matched continuation, fine-first qualification and paired verdict."""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import re
import sys

from run_neural_followup import execute
from run_stability_qualification import write


def run(root, prepared, output, prefix, candidate):
    base = root/'build-patch_neural_restoration'
    if (not all(p.resolve().is_relative_to(base.resolve()) for p in (prepared, output))
            or not all(re.fullmatch('[a-z][a-z0-9_-]*', label) for label in (prefix, candidate)) or candidate == 'control'):
        raise ValueError('Dedicated neural build paths required')
    output.mkdir(parents=True, exist_ok=False)
    state = dict(schema_version=1, started_at_utc=datetime.now(timezone.utc).isoformat(),
        script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), status='started', stages=[],
        prefix=prefix, candidate=candidate,
        policy='Both arms and seeds predeclared, both trainings before evaluation.512additional steps, same data, no selection. Fine-first qualification; control failure remains visible.')
    write(output/'progress.json', state)
    def command(name, script, *arguments):
        row = dict(name=name, started_at_utc=datetime.now(timezone.utc).isoformat(),
            command=[sys.executable, str(root/'tests/neural'/script), *map(str, arguments)])
        state['stages'].append(row)
        state['status'] = name
        write(output/'progress.json', state)
        with (output/(name+'.log')).open('x', encoding='utf-8') as log:
            code = execute(row['command'], log)
        row.update(returncode=code, finished_at_utc=datetime.now(timezone.utc).isoformat())
        write(output/'progress.json', state)
        if code:
            raise RuntimeError('Failed execution: '+name)
    def paths(arm):
        arm_prefix = prefix+'-'+arm
        return prepared/arm/'plan.json', base/(arm_prefix+'-campaign-v001'), base/(arm_prefix+'-qualification-v001'), arm_prefix
    try:
        until = datetime.now(timezone.utc)+timedelta(hours=2)
        state['plans_sha256'] = {arm:hashlib.sha256((prepared/arm/'plan.json').read_bytes()).hexdigest()
            for arm in ('control', candidate)}
        for arm in ('control', candidate):
            plan, campaign, qualification, arm_prefix = paths(arm)
            command('train_'+arm, 'run_noisy_campaign.py', '--plan', plan, '--output', campaign, '--until', until.isoformat())
            report = json.loads((campaign/'campaign.json').read_text(encoding='utf-8'))
            if report['status'] != 'completed_both_seeds':
                raise RuntimeError('Training incomplete: '+arm)
        state['qualification'] = {}
        for arm in ('control', candidate):
            plan, campaign, qualification, arm_prefix = paths(arm)
            if hashlib.sha256(plan.read_bytes()).hexdigest() != state['plans_sha256'][arm]:
                raise ValueError('Declared paired plan changed')
            command('qualify_'+arm, 'run_stability_qualification.py', '--campaign', campaign,
                '--data-plan', plan, '--output', qualification, '--prefix', arm_prefix)
            report = json.loads((qualification/'progress.json').read_text(encoding='utf-8'))
            state['qualification'][arm] = dict(status=report['status'], regressions=report.get('regressions', []))
            if report['status'] == 'stopped_on_error':
                raise RuntimeError('Qualification execution error: '+arm)
        control_fine = base/(prefix+'-control-fine-v001/result.json')
        if not control_fine.exists():
            for arm in ('control', candidate):
                plan, campaign, qualification, arm_prefix = paths(arm)
                command('diagnose_'+arm, 'diagnose_context_learning.py', '--data-plan', plan,
                    '--campaign', campaign, '--output', base/(arm_prefix+'-learning-diagnostic-v001/result.json'))
            state['status'] = 'inconclusive_control_development_failed_reserved_unopened'
            return state
        plan, campaign, qualification, arm_prefix = paths(candidate)
        command('candidate_followup', 'run_neural_followup.py', '--qualification', qualification/'progress.json',
            '--data-plan', plan, '--campaign', campaign, '--prefix', arm_prefix,
            '--previous-fine', control_fine, '--previous-campaign', paths('control')[1],
            '--output', base/(arm_prefix+'-followup-v001'))
        followup = json.loads((base/(arm_prefix+'-followup-v001/progress.json')).read_text(encoding='utf-8'))
        state['status'] = 'completed_paired_continuation'
        state['candidate_followup_status'] = followup['status']
        comparison = base/(arm_prefix+'-followup-v001/paired-fine.json')
        if comparison.exists():
            state['paired_fine'] = json.loads(comparison.read_text(encoding='utf-8'))
        return state
    except Exception as error:
        state.update(status='stopped_on_error', error=str(error))
        raise
    finally:
        state['finished_at_utc'] = datetime.now(timezone.utc).isoformat()
        write(output/'progress.json', state)
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        target = root/'docs/validation/raw'/('neural-paired-continuation-v001-'+stamp+'.json')
        if target.exists():
            raise FileExistsError(target)
        write(target, state)
        print(state['status'], flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--prefix', required=True)
    parser.add_argument('--candidate', required=True)
    args = parser.parse_args()
    run(Path(__file__).resolve().parents[2], args.prepared, args.output, args.prefix, args.candidate)
