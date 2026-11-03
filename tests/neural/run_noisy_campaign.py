"""Resource-aware sequential launcher. Execute only after the user's start signal."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

from memory_budget import snapshot


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as handle:
        for chunk in iter(lambda: handle.read(1024*1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def save(path, value):
    with Path(path).open('x', encoding='utf-8') as handle:
        json.dump(value, handle, indent=2, allow_nan=False)


def capacity(until, admission_fraction, poll_seconds):
    """Two stable samples; lightweight polling never loads PyTorch."""
    consecutive = 0
    while datetime.now(timezone.utc) < until:
        sample = snapshot()
        consecutive = consecutive+1 if sample['system_used_fraction'] < admission_fraction else 0
        if consecutive >= 2:
            return sample
        remaining = (until-datetime.now(timezone.utc)).total_seconds()
        if remaining <= 0:
            break
        time.sleep(min(poll_seconds, remaining))
    return None


def resume_from(result, folder):
    meta = result.get('training_progress', {}).get('checkpoint')
    if meta is None:
        return None
    path = Path(meta['path']).resolve()
    if (not path.is_relative_to(folder.resolve()) or path.name != 'training-latest.pt'
            or not path.is_file() or sha256(path) != meta['sha256']):
        raise ValueError('Resume file identity or campaign confinement differs')
    return path


def run(root, plan_path, output, until, max_attempts=8, poll_seconds=15):
    base = (root/'build-patch_neural_restoration').resolve()
    plan_path, output = plan_path.resolve(), output.resolve()
    if not all(p.is_relative_to(base) for p in (plan_path, output)):
        raise ValueError('Dedicated neural build required')
    if (until.tzinfo is None or isinstance(max_attempts, bool) or not isinstance(max_attempts, int)
            or not 1 <= max_attempts <= 16 or not 0 < poll_seconds <= 60):
        raise ValueError('Timezone-aware latest start,1..16 attempts, polling within60seconds required')
    plan = json.loads(plan_path.read_text(encoding='utf-8'))
    if plan.get('schema_version') != 3 or plan.get('checkpoint_policy', {}).get('interval_steps') != 64:
        raise ValueError('Current resumable64-step plan required')
    if plan['memory_policy']['system_fraction'] != .92:
        raise ValueError('Authorized92percent memory policy required')
    executable = root/'.venv-neural/Scripts/python.exe'
    if not executable.is_file():
        raise ValueError('Existing neural Python environment required')
    output.mkdir(parents=True, exist_ok=False)
    now = datetime.now(timezone.utc)
    identity = 'neural-campaign-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    report = dict(schema_version=1, data_version=1, campaign_id=identity,
        started_at_utc=now.isoformat(), until_latest_start=until.isoformat(), status='incomplete',
        plan_path=str(plan_path), plan_sha256=sha256(plan_path), launcher_sha256=sha256(__file__),
        admission_fraction=.90, poll_seconds=poll_seconds, max_attempts_per_seed=max_attempts,
        attempt_timeout_seconds=1800, attempts=[],
        policy='Both seeds evaluated even after quality failure. Retry only memory stops; latest technical checkpoint, no score selection. No new attempt after deadline; active attempt may finish normally.')
    try:
        for seed in (71, 73):
            resume = None
            for number in range(1, max_attempts+1):
                sample = capacity(until, report['admission_fraction'], poll_seconds)
                if sample is None:
                    report['status'] = 'latest_start_reached'
                    return report
                if sha256(plan_path) != report['plan_sha256']:
                    raise ValueError('Plan changed during campaign')
                attempt = output/f'seed{seed}-attempt{number:02d}'
                command = [str(executable), str(root/'tests/neural/run_noisy_moment_pilot.py'),
                    '--plan', str(plan_path), '--seed', str(seed), '--output', str(attempt)]
                if resume is not None:
                    command.extend(('--resume', str(resume)))
                event = dict(seed=seed, number=number, output=str(attempt),
                    admission_memory=sample, resumed_from=str(resume) if resume else None)
                report['attempts'].append(event)
                try:
                    with (output/f'seed{seed}-attempt{number:02d}.log').open('x', encoding='utf-8') as log:
                        process = subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT,
                            timeout=report['attempt_timeout_seconds'], check=False)
                except subprocess.TimeoutExpired:
                    event['status'] = 'attempt_timeout'
                    report['status'] = 'stopped_on_attempt_timeout'
                    return report
                result_path = attempt/'result.json'
                event['returncode'] = process.returncode
                if not result_path.is_file():
                    event['status'] = 'missing_result'
                    report['status'] = 'stopped_on_missing_result'
                    return report
                result = json.loads(result_path.read_text(encoding='utf-8'))
                event.update(result_path=str(result_path), result_sha256=sha256(result_path),
                    status=result['status'], pilot_gates_passed=result.get('pilot_gates_passed'),
                    training_progress=result.get('training_progress'), campaign_id=result['campaign_id'])
                if process.returncode == 0 and result['status'] == 'completed_synthetic_noise_pilot_not_photographic_qualification':
                    break
                if result['status'] != 'aborted_for_memory_budget':
                    report['status'] = 'stopped_on_calculation_error'
                    return report
                resume = resume_from(result, output)
            else:
                report['status'] = 'memory_retry_limit_reached'
                return report
        report['status'] = 'completed_both_seeds'
        report['pilot_gates_passed'] = all(e['pilot_gates_passed'] for e in report['attempts']
            if e['status'] == 'completed_synthetic_noise_pilot_not_photographic_qualification')
        return report
    except Exception as error:
        report['status'] = 'stopped_on_launcher_error'
        report['error'] = str(error)
        raise
    finally:
        report['finished_at_utc'] = datetime.now(timezone.utc).isoformat()
        save(output/'campaign.json', report)
        save(root/'docs/validation/raw'/f'{identity}.json', report)
        print(report['status']+' '+str(output/'campaign.json'), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--until', type=datetime.fromisoformat, required=True,
        help='Latest start for a new attempt, including timezone; active attempt finishes normally')
    parser.add_argument('--max-attempts', type=int, default=8)
    parser.add_argument('--poll-seconds', type=float, default=15)
    args = parser.parse_args()
    report = run(Path(__file__).resolve().parents[2], args.plan, args.output, args.until, args.max_attempts, args.poll_seconds)
    if report['status'] not in ('completed_both_seeds', 'latest_start_reached'):
        raise SystemExit(1)
