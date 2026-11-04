"""Predeclared autonomous diagnosis or held-out confirmation, without retraining."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

from run_stability_qualification import write


def next_action(progress):
    status = progress.get('status')
    if status == 'completed_known_regressions_passed':
        rows = progress.get('regressions', [])
        if (len(rows) != 3 or {r['name'] for r in rows} != {'classic_first', 'classic_second', 'fine'}
                or not all(r['passed'] is True for r in rows) or progress.get('development_passed') is not True):
            raise ValueError('Inconsistent qualification verdict')
        return 'confirm'
    if status in ('completed_known_regressions_failed_reserved_unopened', 'stopped_on_development_failure'):
        return 'diagnose'
    if status == 'stopped_on_error':
        return 'stop'
    return 'wait'


def execute(command, log):
    """Own child only; terminate its process tree on Windows timeout/cancellation."""
    with subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT) as process:
        try:
            return process.wait(timeout=7200)
        except BaseException:
            if os.name == 'nt':
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30, check=False)
            else:
                process.kill()
            process.wait(timeout=30)
            raise


def run(root, output, qualification, data_plan, campaign, prefix, previous_fine, previous_campaign):
    base = root/'build-patch_neural_restoration'
    if (not re.fullmatch('[a-z][a-z0-9_-]*', prefix)
            or not all(p.resolve().is_relative_to(base.resolve())
                for p in (output, qualification, data_plan, campaign, previous_fine, previous_campaign))):
        raise ValueError('Dedicated build paths and simple prefix required')
    output.mkdir(parents=True, exist_ok=False)
    state = dict(schema_version=1, started_at_utc=datetime.now(timezone.utc).isoformat(),
        status='waiting_known_qualification', qualification=str(qualification), data_plan=str(data_plan),
        campaign=str(campaign), prefix=prefix, script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        previous_fine=str(previous_fine), previous_campaign=str(previous_campaign),
        policy='No retraining or tuning. Known failure -> paired diagnostic only. All known pass -> frozen new classic cohort then fine cohort2; failure stops opening further reserved data.', stages=[])
    write(output/'progress.json', state)
    def command(name, script, *arguments):
        entry = dict(name=name, started_at_utc=datetime.now(timezone.utc).isoformat(),
            command=[sys.executable, str(root/'tests/neural'/script), *map(str, arguments)])
        state['stages'].append(entry)
        state['status'] = name
        write(output/'progress.json', state)
        with (output/(name+'.log')).open('x', encoding='utf-8') as log:
            returncode = execute(entry['command'], log)
        entry.update(returncode=returncode, finished_at_utc=datetime.now(timezone.utc).isoformat())
        write(output/'progress.json', state)
        if returncode:
            raise RuntimeError('Execution failed: '+name)
    def measured_pass(path):
        result = json.loads(path.read_text(encoding='utf-8'))
        return result.get('pilot_gates_passed') is True and result['status'].startswith('completed_')
    try:
        deadline = time.monotonic()+7200
        while True:
            if qualification.exists():
                progress = json.loads(qualification.read_text(encoding='utf-8'))
                action = next_action(progress)
                if action != 'wait':
                    break
            if time.monotonic() >= deadline:
                raise TimeoutError('Known qualification not closed within two hours')
            time.sleep(30)
        state['qualification_sha256'] = hashlib.sha256(qualification.read_bytes()).hexdigest()
        if action == 'stop':
            state['status'] = 'stopped_after_qualification_error'
            return state
        protected = base/(prefix+'-protected-v001/result.json')
        fine = base/(prefix+'-fine-v001/result.json')
        if fine.exists():
            command('paired_fine_comparison', 'compare_fine_candidates.py',
                '--previous', previous_fine, '--current', fine,
                '--output', output/'paired-fine.json')
        if action == 'diagnose':
            if fine.exists():
                command('fine_diagnosis', 'trace_fine_attenuation.py', '--proof', protected,
                    '--historical', fine, '--output', base/(prefix+'-fine-trace-v001'))
                command('multitone_diagnosis', 'diagnose_multitone_training.py', '--data-plan', data_plan,
                    '--previous-campaign', previous_campaign, '--campaign', campaign,
                    '--output', base/(prefix+'-multitone-diagnostic-v001'))
            else:
                command('learning_diagnosis', 'diagnose_context_learning.py', '--data-plan', data_plan,
                    '--campaign', campaign, '--output', base/(prefix+'-learning-diagnostic-v001/result.json'))
            state['status'] = 'completed_known_failure_diagnosis_reserved_unopened'
            return state
        # Inputs, thresholds and both final weights remain frozen before opening this cohort.
        models = [campaign/f'seed{seed}-attempt01' for seed in (71, 73)]
        classic_plan = base/(prefix+'-independent-plan-v001')
        classic = base/(prefix+'-independent-v001')
        command('prepare_independent_classic', 'confirm_noisy_model.py', '--prepare', '--data-plan', data_plan,
            '--eligibility', protected, '--kind', 'protected', '--models', *models,
            '--prior', base/'noisy-confirmation-plan-v001/plan.json',
            base/'fixed-point-independent-plan-v001/plan.json', '--edge-start', 352,
            '--mixed-seed', 3800000, '--output', classic_plan)
        command('independent_classic', 'confirm_noisy_model.py', '--plan', classic_plan/'plan.json', '--output', classic)
        if not measured_pass(classic/'result.json'):
            state['status'] = 'stopped_on_new_classic_failure_fine2_unopened'
            return state
        fine_plan = base/(prefix+'-independent-fine-plan-v001')
        fine_new = base/(prefix+'-independent-fine-v001')
        command('prepare_independent_fine', 'confirm_fine_noise.py', '--prepare', '--proof', classic/'result.json',
            '--cohort', 2, '--prior', base/'fixed-point-fine-plan-v001/plan.json', '--output', fine_plan)
        command('independent_fine', 'confirm_fine_noise.py', '--plan', fine_plan/'plan.json', '--output', fine_new)
        if not measured_pass(fine_new/'result.json'):
            state['status'] = 'stopped_on_new_fine_failure'
            return state
        captures = base/'mosaic-inlet-v001/result.json'
        command('tiling', 'audit_neural_tiles.py', '--data-plan', data_plan, '--models', *models,
            '--captures', captures, '--kind', 'protected', '--output', base/(prefix+'-tiling-v001'))
        tiling = json.loads((base/(prefix+'-tiling-v001/result.json')).read_text(encoding='utf-8'))
        if tiling.get('engineering_passed') is not True:
            state['status'] = 'stopped_on_tiling_failure'
            return state
        command('camera_preview', 'preview_camera_neural.py', '--proof', classic/'result.json',
            '--captures', captures, '--output', base/(prefix+'-camera-v001'))
        state['status'] = 'completed_synthetic_confirmation_and_visual_material_not_photographic_validation'
        return state
    except Exception as error:
        state.update(status='stopped_on_error', error=str(error))
        raise
    finally:
        state['finished_at_utc'] = datetime.now(timezone.utc).isoformat()
        write(output/'progress.json', state)
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        target = root/'docs/validation/raw'/('neural-followup-v001-'+stamp+'.json')
        if target.exists():
            raise FileExistsError(target)
        write(target, state)
        print(state['status'], flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('output', 'qualification', 'data-plan', 'campaign', 'previous-fine', 'previous-campaign'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--prefix', required=True)
    args = parser.parse_args()
    run(Path(__file__).resolve().parents[2], args.output, args.qualification, args.data_plan,
        args.campaign, args.prefix, args.previous_fine, args.previous_campaign)
