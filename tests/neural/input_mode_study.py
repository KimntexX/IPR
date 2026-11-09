"""Short paired comparison of network inputs: sigma map, blind, Anscombe domain, and the loss alone.

Four arms trained from scratch for 4096 updates on the frozen v005 arrays
(hardlinks), same seed, data stream, schedule and development controls:
A sigma map + unweighted L1 (current), B blind + unweighted L1,
C Anscombe input + Anscombe-domain L1, D sigma map + calibrated
sigma-normalized L1 (separates the loss effect from the input effect),
E sigma map as input with a plain x - F output (the map's own contribution).
A later study may train only some arms and pair them with the verified,
unchanged arm A of an earlier study.
Development diagnosis only; no production change.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

from prepare_next_training import digest, read, write


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT/'build-patch_neural_restoration'
PARENT = BUILD/'physical-natural-training-v005'
CALIBRATION = BUILD/'physical-loss-scale-calibration-v001/result.json'
STEPS = 4096
ARMS = (('A_sigma_map', 'sigma_map', 'unweighted L1'),
        ('B_blind', 'blind', 'unweighted L1'),
        ('C_anscombe', 'anscombe', 'anscombe_l1'),
        ('D_sigma_map_normalized_loss', 'sigma_map', 'sigma_normalized_l1'),
        ('E_sigma_input', 'sigma_input', 'unweighted L1'))
REFERENCE = 'A_sigma_map'
TOOLS = ('input_mode_study.py', 'input_modes.py', 'physical_natural_training.py', 'physical_training_objective.py',
         'dru_cfa.py', 'memory_budget.py', 'load_control.py', 'prepare_next_training.py')


def parameter_count(in_nc):
    sys.path.insert(0, str(BUILD/'third-party-drunet-v001'))
    from models.network_unet import UNetRes
    core = UNetRes(in_nc=in_nc, out_nc=4, nc=[32, 64, 128, 256], nb=4, act_mode='R',
                   downsample_mode='strideconv', upsample_mode='convtranspose')
    return sum(p.numel() for p in core.parameters())


def reference_arm(study_path):
    """Arm A of a completed earlier study, pinned by its plan, progress and checkpoint digests."""
    earlier = read(study_path)
    arm = next(a for a in earlier['arms'] if a['name'] == REFERENCE)
    folder = Path(arm['path'])
    progress = read(folder/'seed71/progress.json')
    if (progress['status'] != 'completed_natural_physical_development_training' or progress['completed_steps'] != STEPS
            or digest(folder/'plan.json') != arm['plan_sha256']
            or digest(folder/'seed71/resume.pt') != progress['final_checkpoint_sha256']):
        raise ValueError('Earlier reference arm is incomplete or changed')
    return dict(arm, reuse=dict(progress_sha256=digest(folder/'seed71/progress.json'),
                                checkpoint_sha256=progress['final_checkpoint_sha256'], study=str(study_path.resolve())))


def prepare(output, names, reference_study=None):
    from input_modes import in_channels
    source_plan, data = read(PARENT/'plan.json'), read(PARENT/'data.json')
    for name, expected in data['files'].items():
        if digest(PARENT/name) != expected:
            raise ValueError('Parent array changed: '+name)
    calibration = read(CALIBRATION)
    if (calibration['status'] != 'completed_training_only_loss_scale_calibration'
            or calibration['parent_plan_sha256'] != digest(PARENT/'plan.json')):
        raise ValueError('Completed training-only calibration for the v005 plan required')
    output.mkdir(exist_ok=False)
    tools = {name: digest(ROOT/'tests/neural'/name) for name in TOOLS}
    if not names or len(set(names)) != len(names) or any(n not in {a[0] for a in ARMS} for n in names):
        raise ValueError('Distinct known arm names required')
    if (REFERENCE in names) == (reference_study is not None):
        raise ValueError('Train arm A here or reuse it from an earlier study, not both')
    arms = [] if reference_study is None else [reference_arm(reference_study)]
    for name, mode, objective in (a for a in ARMS if a[0] in names):
        target = output/name
        target.mkdir()
        for filename in data['files']:
            os.link(PARENT/filename, target/filename)
        write(target/'data.json', data)
        scale = 1. if objective == 'unweighted L1' else calibration['objective_scale']
        plan = {**source_plan, 'schema_version': 4, 'status': 'frozen_input_mode_comparison',
            'created_at_utc': datetime.now(timezone.utc).isoformat(), 'steps': STEPS, 'campaign_seconds': 2400,
            'seeds': [71], 'input_mode': mode, 'objective': objective, 'objective_scale': scale,
            'parameters': parameter_count(in_channels(mode)), 'learning_rate': 1e-4, 'minimum_learning_rate': 1e-6,
            'schedule': 'cosine from 1e-4 to 1e-6 over the 4096 steps, from scratch',
            'sigma_floor': 1/source_plan['target_domain']['divisor'], 'progress_every': 128,
            'controls_at': [0, 1024, 2048, 3072, 4096], 'checkpoint_at': [STEPS], 'tools': tools,
            'loss_scale_calibration': dict(path=str(CALIBRATION.resolve()), sha256=digest(CALIBRATION)),
            'parent_plan_sha256': digest(PARENT/'plan.json'),
            'limits': source_plan['limits']+['Four short arms from scratch, one seed, known development tiles only',
                'Blind and Anscombe arms have 4 input channels; their initial weights differ from the 8-channel arms',
                'Anscombe arm: law a, b measured per ISO and plane; algebraic inverse matched to the z-domain target']}
        plan.pop('continuation', None)
        write(target/'plan.json', plan)
        arms.append(dict(name=name, path=str(target.resolve()), input_mode=mode, objective=objective,
                         objective_scale=scale, plan_sha256=digest(target/'plan.json')))
    write(output/'study.json', dict(schema_version=1, data_version=output.name.rsplit('-', 1)[-1],
        campaign_id=output.name, measured_at_utc=datetime.now(timezone.utc).isoformat(), status='prepared',
        arms=arms, parent_plan_sha256=digest(PARENT/'plan.json'), steps_per_arm=STEPS, tools=tools,
        resources='Sequential GPU workers; private 5 GiB soft / 6 GiB hard, CUDA 50 %, system 92 %; host pressure yields',
        evaluation='Same 72 development observations; per tile, ISO and source, paired with arm A'))


def prepare_long(output, name, steps, seeds, hours):
    """One long campaign of a chosen arm, from scratch, on the frozen v005 arrays; run it with physical_natural_training --mode run."""
    source_plan, data = read(PARENT/'plan.json'), read(PARENT/'data.json')
    for filename, expected in data['files'].items():
        if digest(PARENT/filename) != expected:
            raise ValueError('Parent array changed: '+filename)
    from input_modes import in_channels
    _, mode, objective = next(a for a in ARMS if a[0] == name)
    if objective != 'unweighted L1':
        raise ValueError('Long campaigns are prepared for unit-scale objectives only')
    if steps < 8000 or not seeds or len(set(seeds)) != len(seeds):
        raise ValueError('At least 8000 steps and distinct seeds required')
    output.mkdir(exist_ok=False)
    for filename in data['files']:
        os.link(PARENT/filename, output/filename)
    write(output/'data.json', data)
    every = steps//16
    plan = {**source_plan, 'schema_version': 4, 'status': 'frozen_long_input_mode_campaign',
        'created_at_utc': datetime.now(timezone.utc).isoformat(), 'steps': steps, 'seeds': list(seeds),
        'campaign_seconds': int(hours*3600), 'input_mode': mode, 'objective': objective, 'objective_scale': 1.,
        'parameters': parameter_count(in_channels(mode)), 'learning_rate': 1e-4, 'minimum_learning_rate': 1e-6,
        'schedule': f'cosine from 1e-4 to 1e-6 over {steps} steps, from scratch, each seed',
        'progress_every': 500, 'controls_at': [k*every for k in range(17)], 'checkpoint_at': [k*steps//4 for k in range(1, 5)],
        'tools': {n: digest(ROOT/'tests/neural'/n) for n in TOOLS}, 'arm': name,
        'parent_plan_sha256': digest(PARENT/'plan.json'),
        'limits': source_plan['limits']+[f'Arm {name} chosen on the 4096-step paired study (input-mode-study-v001/v002)',
            'Development tiles were used to choose the arm; they are not a fresh test']}
    plan.pop('continuation', None)
    write(output/'plan.json', plan)


def summarize(study):
    arms = {arm['name']: read(Path(arm['path'])/'seed71/progress.json') for arm in study['arms']}
    if any(r['completed_steps'] != STEPS or r['status'] != 'completed_natural_physical_development_training'
           for r in arms.values()):
        raise ValueError('Every arm must complete its updates')
    if len({r['input_selection_trace_sha256'] for r in arms.values()}) != 1:
        raise ValueError('Different data selections between arms')
    final = {name: {(row['target_index'], row['iso']): row for row in r['controls'][-1]['rows']}
             for name, r in arms.items()}
    reference = final[REFERENCE]
    groups = []
    for source in (0, 1):
        for iso in (3200, 6400, 16000, 51200):
            keys = [k for k in reference if k[1] == iso and k[0]//9 == source]
            entry = dict(source=source, iso=iso, arms={})
            for name, rows in final.items():
                if any(abs(rows[k]['observed_mse']/reference[k]['observed_mse']-1) > 1e-6 for k in keys):
                    raise ValueError('Different observed noise between arms')
                entry['arms'][name] = dict(
                    median_gain_db=statistics.median(rows[k]['psnr_gain_db'] for k in keys),
                    median_minus_A_db=statistics.median(rows[k]['psnr_gain_db']-reference[k]['psnr_gain_db'] for k in keys),
                    median_oracle_alpha=statistics.median(rows[k]['oracle_correction_alpha'] for k in keys),
                    max_abs_plane_bias=max(abs(v) for k in keys for v in rows[k]['mean_error_by_plane']))
            groups.append(entry)
    clipped = {name: sum(x['clipped'] for x in r['losses'])/len(r['losses']) for name, r in arms.items()}
    return dict(status='completed_input_mode_comparison_development_not_model_validation', summary=groups,
        clipped_fraction=clipped, identical_input_selection_trace=True,
        limits=['4096 updates from scratch per arm, one seed: early learning, not converged',
                'Two known development sources; no fresh test, real-noise or photographic validation',
                'Oracle alpha uses the clean target: diagnosis only'])


def run(output):
    study = read(output/'study.json')
    result = dict(schema_version=1, data_version=study['data_version'], campaign_id=output.name,
        measured_at_utc=datetime.now(timezone.utc).isoformat(), status='running', study=study, stages=[])
    env = dict(os.environ, OMP_NUM_THREADS='4', OPENBLAS_NUM_THREADS='1')
    python = ROOT.parent/'venv-neural-cuda/Scripts/python.exe'
    try:
        for arm in study['arms']:
            for name, expected in study['tools'].items():
                if digest(ROOT/'tests/neural'/name) != expected:
                    raise ValueError('Frozen study source changed: '+name)
            if digest(Path(arm['path'])/'plan.json') != arm['plan_sha256']:
                raise ValueError('Frozen arm plan changed')
            if 'reuse' in arm:
                folder = Path(arm['path'])/'seed71'
                if (digest(folder/'progress.json') != arm['reuse']['progress_sha256']
                        or digest(folder/'resume.pt') != arm['reuse']['checkpoint_sha256']):
                    raise ValueError('Reused reference arm changed')
                result['stages'].append(dict(arm=arm['name'], status='reused_verified_reference'))
                continue
            result['active_arm'] = arm['name']
            write(output/'progress.json', result)
            with (output/(arm['name']+'-supervisor.log')).open('x', encoding='utf-8') as log:
                process = subprocess.Popen([str(python), str(ROOT/'tests/neural/physical_natural_training.py'),
                    '--mode', 'run', '--output', arm['path']], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                    creationflags=subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS)
                try:
                    returncode = process.wait(timeout=3100)
                except BaseException:
                    (Path(arm['path'])/'STOP').write_text('Study cancelled: save and stop.', encoding='utf-8')
                    try:
                        process.wait(timeout=60)
                    except subprocess.TimeoutExpired:
                        subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                                       capture_output=True, timeout=20, check=False)
                        process.wait(timeout=15)
                    raise
            arm_result = read(Path(arm['path'])/'progress.json')
            result['stages'].append(dict(arm=arm['name'], returncode=returncode, status=arm_result['status']))
            if returncode or arm_result['status'] != 'completed_declared_seeds_development_only':
                raise RuntimeError('Arm failed: '+arm['name'])
        result.update(summarize(study))
    except Exception as error:
        result.update(status='stopped', error=str(error))
    finally:
        result['finished_at_utc'] = datetime.now(timezone.utc).isoformat()
        write(output/'progress.json', result)
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (ROOT/'docs/validation/raw'/f'neural-{output.name}-{stamp}.json').open('x', encoding='utf-8') as stream:
            json.dump(result, stream, allow_nan=False, separators=(',', ':'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--arms', nargs='+', default=[a[0] for a in ARMS])
    parser.add_argument('--reference-study', type=Path)
    parser.add_argument('--long', help='prepare one long campaign of this arm')
    parser.add_argument('--steps', type=int, default=64000)
    parser.add_argument('--seeds', type=int, nargs='+', default=[71, 73])
    parser.add_argument('--hours', type=float, default=15.)
    args = parser.parse_args()
    if args.long:
        prepare_long(args.output.resolve(), args.long, args.steps, args.seeds, args.hours)
    elif args.prepare:
        prepare(args.output.resolve(), args.arms, args.reference_study.resolve() if args.reference_study else None)
    else:
        run(args.output.resolve())
