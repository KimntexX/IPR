"""Sequential matched continuation, fine-first qualification and paired verdict."""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import sys

from run_neural_followup import execute
from run_stability_qualification import write


def run(root, prepared, output):
    base = root/'build-patch_neural_restoration'
    if not all(p.resolve().is_relative_to(base.resolve()) for p in (prepared, output)):
        raise ValueError('Dedicated neural build paths required')
    output.mkdir(parents=True, exist_ok=False)
    state = dict(schema_version=1, started_at_utc=datetime.now(timezone.utc).isoformat(),
        script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), status='started', stages=[],
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
        prefix = 'continuation-'+arm
        return prepared/arm/'plan.json', base/(prefix+'-campaign-v001'), base/(prefix+'-qualification-v001'), prefix
    try:
        until = datetime.now(timezone.utc)+timedelta(hours=2)
        state['plans_sha256'] = {arm:hashlib.sha256((prepared/arm/'plan.json').read_bytes()).hexdigest()
            for arm in ('control', 'progressive')}
        for arm in ('control', 'progressive'):
            plan, campaign, qualification, prefix = paths(arm)
            command('train_'+arm, 'run_noisy_campaign.py', '--plan', plan, '--output', campaign, '--until', until.isoformat())
            report = json.loads((campaign/'campaign.json').read_text(encoding='utf-8'))
            if report['status'] != 'completed_both_seeds':
                raise RuntimeError('Training incomplete: '+arm)
        state['qualification'] = {}
        for arm in ('control', 'progressive'):
            plan, campaign, qualification, prefix = paths(arm)
            if hashlib.sha256(plan.read_bytes()).hexdigest() != state['plans_sha256'][arm]:
                raise ValueError('Declared paired plan changed')
            command('qualify_'+arm, 'run_stability_qualification.py', '--campaign', campaign,
                '--data-plan', plan, '--output', qualification, '--prefix', prefix)
            report = json.loads((qualification/'progress.json').read_text(encoding='utf-8'))
            state['qualification'][arm] = dict(status=report['status'], regressions=report.get('regressions', []))
            if report['status'] == 'stopped_on_error':
                raise RuntimeError('Qualification execution error: '+arm)
        control_fine = base/'continuation-control-fine-v001/result.json'
        if not control_fine.exists():
            for arm in ('control', 'progressive'):
                plan, campaign, qualification, prefix = paths(arm)
                command('diagnose_'+arm, 'diagnose_context_learning.py', '--data-plan', plan,
                    '--campaign', campaign, '--output', base/(prefix+'-learning-diagnostic-v001/result.json'))
            state['status'] = 'inconclusive_control_development_failed_reserved_unopened'
            return state
        plan, campaign, qualification, prefix = paths('progressive')
        command('progressive_followup', 'run_neural_followup.py', '--qualification', qualification/'progress.json',
            '--data-plan', plan, '--campaign', campaign, '--prefix', prefix,
            '--previous-fine', control_fine, '--previous-campaign', paths('control')[1],
            '--output', base/(prefix+'-followup-v001'))
        followup = json.loads((base/(prefix+'-followup-v001/progress.json')).read_text(encoding='utf-8'))
        state['status'] = 'completed_paired_continuation'
        state['candidate_followup_status'] = followup['status']
        comparison = base/(prefix+'-followup-v001/paired-fine.json')
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
    args = parser.parse_args()
    run(Path(__file__).resolve().parents[2], args.prepared, args.output)
