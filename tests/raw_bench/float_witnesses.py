"""Targeted F1 guard regression: three Canon models, A/B, with/without DCP."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
from capture import load_capture, sha256
from float_qualification import qualify_inlet
from production_campaign import full_equal


def save(path, data):
    with path.open('x', encoding='utf-8') as handle:
        json.dump(data, handle, ensure_ascii=False, indent=2, allow_nan=False)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('reference-study', 'production-freeze', 'manifest', 'tool', 'engine-source'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--engine-revision', required=True)
    p.add_argument('--runtime-dir', action='append', type=Path, required=True)
    a = p.parse_args(); root = Path(__file__).resolve().parents[2]
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    work = root/'build-patch_raw_bench/float-witnesses'/stamp; work.mkdir(parents=True)
    ref = json.loads(a.reference_study.read_text(encoding='utf-8'))
    settings = json.loads(a.production_freeze.read_text(encoding='utf-8'))
    names = {'6V1A9153.CR3', '7Y6A7599.CR3', 'IMG_3135.CR2'}
    inputs = [r for r in settings['inputs'] if Path(r['source']).name in names]
    if len(inputs) != 3 or sha256(a.manifest) != settings['manifest']['sha256']:
        raise ValueError('Missing witnesses or changed calibration')
    sources = {n: dict(sha256=sha256(root/n), text=(root/n).read_text(encoding='utf-8'))
               for n in ('tests/raw_bench/float_witnesses.py', 'tests/raw_bench/production_capture.cpp',
                         'tests/raw_bench/float_qualification.py', 'tests/raw_bench/capture.py')}
    tool_hash = sha256(a.tool)
    hashes = {str(f.relative_to(a.engine_source)): sha256(f) for f in (a.engine_source/'src').rglob('*') if f.is_file() and f.suffix in ('.h', '.cpp')}
    freeze = root/'docs/validation/raw'/f'gel-banc-raw-flottant-temoins-v001-{stamp}.json'
    save(freeze, dict(schema_version=1, started_at_utc=datetime.now(timezone.utc).isoformat(), inputs=inputs,
                      parents={str(v.resolve()): sha256(v) for v in (a.reference_study, a.production_freeze)},
                      tool=dict(path=str(a.tool.resolve()), sha256=tool_hash), engine_revision=a.engine_revision,
                      engine_source=str(a.engine_source.resolve()), engine_hashes=hashes, sources=sources,
                      scope='Three Canon models; 12 decodes A/B with/without DCP. A full master/metadata against C3, B finite master and actual signed DCP inlet; 9153 overshoot above1 required. No optics/noise benefit or nonlinear DCP range qualification.'))
    env = dict(os.environ); env['OMP_NUM_THREADS'] = '8'
    env['PATH'] = os.pathsep.join([str(v) for v in a.runtime_dir]+[env.get('PATH', '')])
    empty = work/'empty-profiles'; empty.mkdir(); records = []
    print('FROZEN', freeze, flush=True)
    try:
        for item in inputs:
            source = Path(item['source']); profile = settings['profiles'][item['model']]
            if sha256(source) != item['source_sha256'] or sha256(Path(profile['path'])) != profile['sha256']:
                raise ValueError('Witness source/profile changed')
            for mode in ('dcp', 'matrix'):
                for policy in ('parity', 'signed'):
                    out = work/f'{source.stem}-{mode}-{policy}.json'
                    command = [str(a.tool.resolve()), '--input', str(source), '--output', str(out),
                               '--profile-root', str(empty), '--manifest', str(a.manifest.resolve()), '--development', policy]
                    if mode == 'dcp':
                        command += ['--profile', profile['path'], '--capture-camera']
                    with out.with_suffix('.log').open('x', encoding='utf-8') as log:
                        subprocess.run(command, cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=240)
                    capture, _ = load_capture(out)
                    if capture['engine_revision'] != a.engine_revision or capture['mode'] != policy:
                        raise ValueError('Wrong engine/policy')
                    row = dict(source=item, mode=mode, policy=policy, capture=dict(path=str(out), sha256=sha256(out)),
                               full_image=capture['full_image'], decode_ms=capture['decode_ms'], peak_bytes=capture['decode_peak_working_set_bytes'])
                    if policy == 'parity':
                        previous = [r for r in ref['records'] if r['mode'] == mode and r['source']['source_sha256'] == item['source_sha256']]
                        if len(previous) != 1 or not full_equal(capture['full_image'], previous[0]['full_images']['candidate']) or capture['metadata'] != previous[0]['metadata']['candidate']:
                            raise ValueError('Guard changes finite A render')
                        row['A_unchanged'] = True
                    if mode == 'dcp':
                        row['inlet'] = qualify_inlet(out)
                        if policy == 'signed' and source.name == '6V1A9153.CR3':
                            if max(row['inlet']['full_camera']['maximum']) <= 1:
                                raise ValueError('9153 signed overshoot was bounded before DCP')
                            row['above_one_preserved'] = True
                    if capture['decode_ms'] > 20800 or capture['rcd_workers'] > 8 or capture['opencv_threads'] > 1:
                        raise ValueError('Witness resource budget exceeded')
                    records.append(row)
                    print(len(records), '/12', source.name, mode, policy, 'pass', flush=True)
        if sha256(a.tool) != tool_hash or any(sha256(root/n) != v['sha256'] for n, v in sources.items()):
            raise ValueError('Bench changed')
        if any(sha256(a.engine_source/n) != v for n, v in hashes.items()):
            raise ValueError('Engine changed')
        result = root/'docs/validation/raw'/f'banc-raw-flottant-temoins-v001-{datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")}.json'
        save(result, dict(schema_version=1, measured_at_utc=datetime.now(timezone.utc).isoformat(),
                          freeze=dict(path=str(freeze), sha256=sha256(freeze)), records=records,
                          verdict='pass_scoped_guard_witnesses', limitations='Three models only, optics off, no malformed RAW corpus or denoising/visual-quality validation; resources exploratory.'))
        print('RESULT', result, flush=True)
    except Exception as error:
        save(work/'partial.json', dict(error=str(error), records=records))
        raise


if __name__ == '__main__':
    main()
