"""X4 A/B pilot on frozen historical pairs; no fitted coefficients or TIFFs."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess

from capture import compare_captures, load_capture, sha256
from signed_measurement import measure_pair


def save(path, value, exclusive=True):
    with path.open('x' if exclusive else 'w', encoding='utf-8') as handle:
        json.dump(value, handle, ensure_ascii=False, indent=2, allow_nan=False)


def select_inputs(baseline):
    expected = {(iso, role) for iso in (1600, 51200)
                for role in ('black', 'near_black_scene', 'illuminated_scene')}
    selected = [r for r in baseline['records'] if (r['iso'], r['role']) in expected]
    if len(selected) != 6 or {(r['iso'], r['role']) for r in selected} != expected:
        raise ValueError('Need exactly the six previously measured pairs')
    inputs = []
    for pair in selected:
        if len(pair['paths']) != 2 or len(set(pair['paths'])) != 2:
            raise ValueError('Invalid pair')
        shots = []
        for name in pair['paths']:
            source = Path(name); historical_record = baseline['native'][name]['record']
            if sha256(source) != historical_record['source_sha256']:
                raise ValueError('Missing/changed historical source: ' + name)
            shots.append(dict(path=str(source), sha256=historical_record['source_sha256'],
                              black=historical_record['applied_black_dn'], white=historical_record['white_before_process']))
        inputs.append(dict(iso=pair['iso'], role=pair['role'], shots=shots))
    return inputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--tool', type=Path, required=True)
    parser.add_argument('--engine-source', type=Path, required=True)
    parser.add_argument('--engine-revision', required=True)
    parser.add_argument('--previous-study', type=Path, help='Previous immutable X4, exact A check on shared boundaries')
    parser.add_argument('--runtime-dir', type=Path, action='append', required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    started = datetime.now(timezone.utc); stamp = started.strftime('%Y%m%dT%H%M%SZ')
    work = root/'build-patch_raw_bench/signed'/stamp
    work.mkdir(parents=True, exist_ok=False)
    tool = args.tool.resolve()
    baseline_hash = sha256(args.baseline)
    baseline = json.loads(args.baseline.read_text(encoding='utf-8'))
    previous_hash = sha256(args.previous_study) if args.previous_study else None
    previous = json.loads(args.previous_study.read_text(encoding='utf-8')) if args.previous_study else None
    inputs = select_inputs(baseline)
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    files = ['tests/raw_bench/signed_campaign.py', 'tests/raw_bench/signed_measurement.py',
             'tests/raw_bench/development_capture.cpp', 'tests/raw_bench/capture.py']
    sources = {p: dict(sha256=sha256(root/p), text=(root/p).read_text(encoding='utf-8')) for p in files}
    engine_files = ['src/raw/RawInput.cpp', 'src/raw/RawInput.h', 'src/raw/LibRawInput.cpp', 'src/raw/LibRawInput.h', 'src/raw/RawDevelopment.cpp',
                    'src/raw/RawDevelopment.h', 'src/raw/Demosaic.cpp', 'src/raw/Demosaic.h']
    engine_sources = {p: dict(sha256=sha256(args.engine_source/p), text=(args.engine_source/p).read_text(encoding='utf-8'))
                      for p in engine_files}
    tool_hash = sha256(tool)
    freeze = root/'docs/validation/raw'/f'gel-banc-raw-signe-v001-{stamp}.json'
    save(freeze, dict(schema_version=1, started_at_utc=started.isoformat(), inputs=inputs,
                      baseline=dict(path=str(args.baseline.resolve()), sha256=baseline_hash),
                      bench_revision=revision, engine_revision=args.engine_revision,
                      previous_study=dict(path=str(args.previous_study.resolve()), sha256=previous_hash) if previous else None,
                      engine_source=str(args.engine_source.resolve()), engine_sources=engine_sources,
                      tool=dict(path=str(tool), sha256=tool_hash), sources=sources,
                      conditions='As-shot gains, sensor orientation, three fixed256 ROIs, explicit previously applied black/white per frame; no DCP, optics or denoising; no coefficient fitted.',
                      policy_scope='F1: signed CFA through RCD and median, unbounded signed float output. PreOutputCameraRgb equals the signed output. A uses the reciprocal product grid historically delivered to DCP. Production default remains A.',
                      interpretation='Mean of black frames relative to applied black is a bias proxy, not independently measured ideal zero. Pair variance excludes common fixed pattern/scene; movement/drift not corrected. CFA phases distinct. No clean-reference or visual-success verdict.'))
    env = dict(os.environ); env['OMP_NUM_THREADS'] = '8'
    env['PATH'] = os.pathsep.join([str(d) for d in args.runtime_dir] + [env.get('PATH', '')])
    rows = []

    def status(**values):
        save(work/'status.json', dict(started_at_utc=started.isoformat(), updated_at_utc=datetime.now(timezone.utc).isoformat(), **values), False)

    print('FROZEN', freeze, flush=True)
    try:
        for index, pair in enumerate(inputs):
            captures = {'parity': [], 'signed': []}; provenance = {'parity': [], 'signed': []}
            for shot, item in enumerate(pair['shots']):
                for mode in ('parity', 'signed'):
                    status(status='running', phase='capture', completed_pairs=len(rows),
                           iso=pair['iso'], role=pair['role'], shot=shot, mode=mode)
                    if sha256(Path(item['path'])) != item['sha256']:
                        raise ValueError('Source changed before capture')
                    out = work/f'{index:02}-{shot}-{mode}.json'
                    command = [str(tool), '--input', item['path'], '--output', str(out),
                               '--path', 'candidate', '--mode', mode, '--black',
                               ','.join(str(v) for v in item['black']), '--white', str(item['white'])]
                    with (work/f'{index:02}-{shot}-{mode}.log').open('x', encoding='utf-8') as log:
                        subprocess.run(command, cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT,
                                       check=True, timeout=240)
                    capture = load_capture(out); manifest = capture[0]
                    if manifest['source']['sha256'] != item['sha256'] or manifest['metadata']['iso'] != pair['iso']:
                        raise ValueError('Capture source/ISO mismatch')
                    if manifest['engine_revision'] != args.engine_revision:
                        raise ValueError('Capture engine revision mismatch')
                    captures[mode].append(capture)
                    provenance[mode].append(dict(path=str(out), sha256=sha256(out), applied=manifest['applied']))
                for key in ('black', 'white', 'divisor', 'gains'):
                    if captures['parity'][shot][0]['applied'][key] != captures['signed'][shot][0]['applied'][key]:
                        raise ValueError('A/B development parameters differ: ' + key)
            invariance = []
            if previous:
                matching = [r for r in previous['records'] if r['iso'] == pair['iso'] and r['role'] == pair['role']]
                if len(matching) != 1:
                    raise ValueError('Previous study pair missing/ambiguous')
                for shot in range(2):
                    old = matching[0]['captures']['parity'][shot]
                    if sha256(Path(old['path'])) != old['sha256']:
                        raise ValueError('Previous A capture changed')
                    # Raw input and pre-output processing remain exact; F1 A
                    # intentionally changed final float representation to the
                    # product historically received by DCP, not division.
                    common = ('unpacked_cfa', 'black_subtracted_cfa', 'normalized_cfa',
                              'pre_demosaic_cfa', 'post_demosaic_camera_rgb',
                              'pre_output_camera_rgb')
                    check = compare_captures(old['path'], provenance['parity'][shot]['path'], common)
                    if check['verdict'] != 'pass':
                        raise ValueError('A changed against previous C4')
                    invariance.append(check)
            result = measure_pair(captures)
            rows.append(dict(iso=pair['iso'], role=pair['role'], inputs=pair['shots'],
                             captures=provenance, previous_A_invariance=invariance, measurements=result))
            print(len(rows), '/6', pair['iso'], pair['role'], 'measured', flush=True)
        if sha256(tool) != tool_hash or sha256(args.baseline) != baseline_hash:
            raise ValueError('Tool or baseline changed during campaign')
        if previous and sha256(args.previous_study) != previous_hash:
            raise ValueError('Previous study changed during campaign')
        if any(sha256(root/p) != info['sha256'] for p, info in sources.items()):
            raise ValueError('Study sources changed during campaign')
        if any(sha256(args.engine_source/p) != info['sha256'] for p, info in engine_sources.items()):
            raise ValueError('Engine sources changed during campaign')
        finished = datetime.now(timezone.utc)
        out = root/'docs/validation/raw'/f'banc-raw-signe-v001-{finished.strftime("%Y%m%dT%H%M%SZ")}.json'
        save(out, dict(schema_version=1, campaign_id='raw-signed-'+stamp,
                       started_at_utc=started.isoformat(), measured_at_utc=finished.isoformat(),
                       completed_at_utc=finished.isoformat(), freeze=dict(path=str(freeze), sha256=sha256(freeze)),
                       records=rows, verdict='measured_not_validated',
                       limitations='Small exploratory corpus, no reserved frames opened, no DCP/raster output or visual success claim; internal lower bounds documented in freeze. Variance is not a denoising quality score; less variance can mean clipping. No claim of recovery of all information or optimal black levels.'))
        status(status='succeeded', phase='complete', completed_pairs=len(rows), result=str(out))
        print('RESULT', out, flush=True)
    except Exception as error:
        save(work/'partial.json', dict(error=str(error), records=rows))
        status(status='failed', phase='error', completed_pairs=len(rows), error=str(error))
        raise


if __name__ == '__main__':
    main()
