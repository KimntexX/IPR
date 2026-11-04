"""Bounded sequential follow-up; failed development never opens reserved data."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time
from campaign_artifacts import campaign_models


def eligible(proof):
    return proof.get('pilot_gates_passed') is True and proof.get('status', '').startswith('completed_')


def write(path, data):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(data, indent=2)+'\n', encoding='utf-8')
    temporary.replace(path)


def run(root, output, campaign, plan, prefix):
    base = root/'build-patch_neural_restoration'
    if (not re.fullmatch(r'[a-z][a-z0-9_-]*', prefix)
            or not all(p.resolve().is_relative_to(base.resolve()) for p in (output, campaign, plan))):
        raise ValueError('Dedicated neural build paths and simple output prefix required')
    output.mkdir(parents=True, exist_ok=False)
    state = dict(schema_version=1, started_at_utc=datetime.now(timezone.utc).isoformat(),
        status='waiting_training', tools_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        campaign=str(campaign), data_plan=str(plan), prefix=prefix,
        policy='Both seeds, fixed gates, sequential jobs. Fine regression first; failure skips classics and forbids overall qualification. Reserved cohorts remain unopened.', stages=[])
    def command(name, script, *arguments):
        entry = dict(name=name, started_at_utc=datetime.now(timezone.utc).isoformat(),
            command=[sys.executable, str(root/'tests/neural'/script), *map(str, arguments)])
        state['stages'].append(entry)
        state['status'] = name
        write(output/'progress.json', state)
        with (output/(name+'.log')).open('x', encoding='utf-8') as log:
            completed = subprocess.run(entry['command'], cwd=root, stdout=log, stderr=subprocess.STDOUT,
                timeout=7200, check=False)
        entry.update(returncode=completed.returncode, finished_at_utc=datetime.now(timezone.utc).isoformat())
        write(output/'progress.json', state)
        if completed.returncode:
            raise RuntimeError('Stage failed: '+name+'; see log')
    try:
        deadline = time.monotonic()+3600
        while not (campaign/'campaign.json').exists():
            if time.monotonic() >= deadline:
                raise TimeoutError('Training campaign did not finish within one hour')
            time.sleep(30)
        trained = json.loads((campaign/'campaign.json').read_text(encoding='utf-8'))
        if trained.get('status') != 'completed_both_seeds':
            raise RuntimeError('Training campaign incomplete: '+trained.get('status', 'missing'))
        models = campaign_models(campaign)
        state['resolved_model_folders'] = [str(p) for p in models]
        protected = base/(prefix+'-protected-v001')
        command('prepare_development', 'run_edge_protected_probe.py', '--prepare', '--data-plan', plan,
            '--models', *models, '--output', base/(prefix+'-protected-plan-v001'))
        command('development', 'run_edge_protected_probe.py', '--plan',
            base/(prefix+'-protected-plan-v001/plan.json'), '--output', protected)
        proof = json.loads((protected/'result.json').read_text(encoding='utf-8'))
        state['development_passed'] = eligible(proof)
        if not eligible(proof):
            state['status'] = 'stopped_on_development_failure'
            return state
        regressions = (
            ('fine', 'fine', 'fixed-point-fine-v001'),
            ('classic_first', 'classic', 'noisy-confirmation-attempt02-v001'),
            ('classic_second', 'classic', 'fixed-point-independent-v001'))
        state['regressions'] = []
        for name, kind, historical in regressions:
            data = base/(prefix+'-'+name+'-plan-v001')
            destination = base/(prefix+'-'+name+'-v001')
            command('prepare_'+name, 'prepare_neural_regression.py', '--source', protected/'result.json',
                '--historical', base/historical/'result.json', '--kind', kind, '--output', data)
            command(name, 'confirm_noisy_model.py' if kind == 'classic' else 'confirm_fine_noise.py',
                '--plan', data/'plan.json', '--output', destination)
            result = json.loads((destination/'result.json').read_text(encoding='utf-8'))
            state['regressions'].append(dict(name=name, passed=eligible(result), result=str(destination/'result.json'),
                sha256=hashlib.sha256((destination/'result.json').read_bytes()).hexdigest()))
            if kind == 'fine' and not eligible(result):
                state['skipped_classic_regressions'] = 'Fine prerequisite failed; no overall qualification claim.'
                break
        state['status'] = ('completed_known_regressions_passed' if all(r['passed'] for r in state['regressions'])
            else 'completed_known_regressions_failed_reserved_unopened')
        return state
    except Exception as error:
        state.update(status='stopped_on_error', error=str(error))
        raise
    finally:
        state['finished_at_utc'] = datetime.now(timezone.utc).isoformat()
        write(output/'progress.json', state)
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        target = root/'docs/validation/raw'/('neural-stability-qualification-v001-'+stamp+'.json')
        if target.exists():
            raise FileExistsError(target)
        write(target, state)
        print(state['status'], flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--campaign', type=Path, required=True)
    parser.add_argument('--data-plan', type=Path, required=True)
    parser.add_argument('--prefix', required=True)
    args = parser.parse_args()
    run(Path(__file__).resolve().parents[2], args.output, args.campaign, args.data_plan, args.prefix)
