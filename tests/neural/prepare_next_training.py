"""Queue bounded engineering probes after the frozen composite campaign.

No quality training, image exports, package installation or publication.
Random patches measure cost only. Natural sources remain unqualified proxies.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build-patch_neural_restoration'


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b''):
            value.update(chunk)
    return value.hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def write(path, data):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(data, indent=2, allow_nan=False), encoding='utf-8')
    temporary.replace(path)


def configurations():
    return [dict(width=w, side=s, precision=p, device='cuda', batch=8, updates=16)
            for w in (16, 32) for s in (128, 256) for p in ('fp32', 'amp-fp16')] + [
        dict(width=w, side=512, precision='fp32', device='cpu', batch=1, updates=4)
        for w in (16, 32)]


def upstream_finished(progress):
    return bool(progress.get('finished_at_utc')) and progress.get('status') in (
        'completed_composite_experiment_not_production_validation', 'stopped_on_error_or_load')


def prepare(output, deadline):
    if datetime.fromisoformat(deadline).utcoffset() is None:
        raise ValueError('Timezone-aware deadline required')
    output.mkdir(parents=True, exist_ok=False)
    files = [Path(__file__).resolve(), ROOT/'tests/neural/training_cost_probe.py',
             ROOT/'tests/neural/dru_cfa.py', ROOT/'tests/neural/memory_budget.py',
             ROOT/'tests/neural/load_control.py', ROOT/'tests/neural/campaign_artifacts.py']
    external = BUILD/'third-party-drunet-v001'
    files += [external/'manifest.json'] + [external/f['path'] for f in read(external/'manifest.json')['files']]
    source = ROOT/'docs/validation/raw/neural-low-iso-supports-v001-20261102T095553Z.json'
    files.append(source)
    plan = dict(schema_version=1, created_at_utc=datetime.now(timezone.utc).isoformat(),
        deadline_utc=deadline, upstream=str(BUILD/'dru-composite-study-v001/progress.json'),
        python_gpu=str(ROOT.parent/'venv-neural-cuda/Scripts/python.exe'),
        python_cpu=str(ROOT/'.venv-neural/Scripts/python.exe'),
        configurations=configurations(), files=[dict(path=str(p), sha256=digest(p)) for p in files],
        natural_inventory=str(source),
        limits=dict(worker_seconds=900, admission_seconds=600, admission_fraction=.70,
                    global_fraction=.92, gpu_fraction=.50, gpu_free_gib=4),
        scope='Engineering only; random weights and patches, no photographic quality conclusion',
        long_training='Deferred: natural CFA partitions, noise realism and calibrated comparators required',
        bm3d=dict(version='4.0.3', isolated_external_environment_required=True,
                  installation='Not performed by this queue; package/license/provenance qualification pending'))
    write(output/'plan.json', plan)
    return plan


def inventory(plan):
    source = read(Path(plan['natural_inventory']))
    rows = []
    for capture in source['captures']:
        path = Path(capture['source'])
        rows.append(dict(source=str(path), expected_sha256=capture['source_sha256'],
            accessible=path.is_file(), model=capture['model'], iso=capture['iso'],
            prior_domain=capture['domain'], scene=None, session=None, partition=None,
            clean_target_qualified=False))
    return dict(source_memory=plan['natural_inventory'], source_sha256=digest(Path(plan['natural_inventory'])),
        sources=rows, status='draft_inventory_not_a_partition_or_clean_target_qualification',
        required=['Assign distinct scene/session groups before sampling patches',
                  'Extract sensor CFA; previous balanced RGB captures are not sensor CFA',
                  'Check focus, saturation and original hashes before extracting',
                  'Measure black-bank realism and freeze noisy-only sigma estimator',
                  'Calibrate CFA baselines before long training; freeze test split before scores'])


def run(output):
    from memory_budget import snapshot
    plan = read(output/'plan.json')
    deadline = datetime.fromisoformat(plan['deadline_utc']).timestamp()
    result = dict(schema_version=1, started_at_utc=datetime.now(timezone.utc).isoformat(),
                  pid=__import__('os').getpid(), status='waiting_for_composite', stages=[], plan=plan)

    def check():
        if time.time() >= deadline or (output/'STOP').exists():
            raise RuntimeError('Deadline or user STOP reached')
        for item in plan['files']:
            if digest(Path(item['path'])) != item['sha256']:
                raise RuntimeError('Frozen preparation source changed: '+item['path'])

    def report():
        write(output/'progress.json', result)

    try:
        report()
        while True:
            check()
            upstream = read(Path(plan['upstream']))
            if upstream_finished(upstream):
                result['upstream'] = {k: upstream.get(k) for k in (
                    'status', 'finished_at_utc', 'reserved_summary', 'joint_median_improvement_both_seeds', 'error')}
                break
            time.sleep(30)
        result['natural_inventory'] = inventory(plan)
        write(output/'natural-inventory.json', result['natural_inventory'])
        if upstream['status'] != 'completed_composite_experiment_not_production_validation':
            raise RuntimeError('Upstream incomplete: preparation retained, no automatic retry or GPU probe')
        for number, config in enumerate(plan['configurations']):
            check()
            stable = None
            admitted_at = time.monotonic()
            result['status'] = 'waiting_for_memory'
            report()
            while True:
                check()
                if snapshot()['system_used_fraction'] < plan['limits']['admission_fraction']:
                    stable = stable or time.monotonic()
                    if time.monotonic()-stable >= 60:
                        break
                else:
                    stable = None
                if time.monotonic()-admitted_at > plan['limits']['admission_seconds']:
                    raise RuntimeError('Memory admission timed out; no quota increase')
                time.sleep(5)
            path = output/f'probe-{number:02d}.json'
            command = [plan['python_gpu' if config['device']=='cuda' else 'python_cpu'],
                       str(ROOT/'tests/neural/training_cost_probe.py'), '--config', json.dumps(config),
                       '--output', str(path)]
            row = dict(config=config, result=str(path))
            result['stages'].append(row)
            result['status'] = f'probe-{number:02d}'
            with (output/f'probe-{number:02d}.log').open('x', encoding='utf-8') as log:
                child = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                    creationflags=subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS)
                row['pid'] = child.pid
                report()
                started = time.monotonic()
                try:
                    while child.poll() is None:
                        check()
                        if time.monotonic()-started > plan['limits']['worker_seconds']:
                            raise RuntimeError('Bounded engineering probe timed out')
                        try:
                            child.wait(timeout=10)
                        except subprocess.TimeoutExpired:
                            pass
                finally:
                    if child.poll() is None:
                        child.kill()
                    child.wait(timeout=15)
                row['returncode'] = child.returncode
            if path.exists():
                row['measurement'] = read(path)
            if child.returncode and not path.exists():
                raise RuntimeError('Probe failed without measurement; inspect its log')
            if row.get('measurement', {}).get('status') in ('resource_stop', 'invalid_numeric'):
                raise RuntimeError('Probe stopped on pressure or invalid numerics; no automatic continuation')
            report()
        result['status'] = 'completed_cost_preparation_not_quality_validation'
    except Exception as error:
        result.update(status='stopped', error=str(error))
    finally:
        result['finished_at_utc'] = datetime.now(timezone.utc).isoformat()
        report()
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (ROOT/'docs/validation/raw'/f'neural-training-preparation-v001-{stamp}.json').open('x', encoding='utf-8') as stream:
            json.dump(result, stream, allow_nan=False, separators=(',', ':'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--deadline')
    args = parser.parse_args()
    if args.prepare:
        if not args.deadline:
            parser.error('--deadline required for preparation')
        prepare(args.output.resolve(), args.deadline)
    else:
        run(args.output.resolve())
