"""Validate the actual pre-DCP inlet probe on the frozen A reference, not F1."""
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--production-study', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--tool', type=Path, required=True)
    parser.add_argument('--runtime-dir', action='append', type=Path, required=True)
    args = parser.parse_args(); root = Path(__file__).resolve().parents[2]
    now = datetime.now(timezone.utc); stamp = now.strftime('%Y%m%dT%H%M%SZ')
    work = root/'build-patch_raw_bench/float-reference'/stamp; work.mkdir(parents=True)
    study = json.loads(args.production_study.read_text(encoding='utf-8'))
    freeze_path = Path(study['freeze']['path'])
    if sha256(freeze_path) != study['freeze']['sha256']:
        raise ValueError('Previous freeze changed')
    previous_freeze = json.loads(freeze_path.read_text(encoding='utf-8'))
    if sha256(args.manifest) != previous_freeze['manifest']['sha256']:
        raise ValueError('Different calibration configuration')
    selected = {'6V1A0043.CR3', 'IMG_3135.CR2', '7Y6A7599.CR3', '6V1A0003.CR3', '6V1A9950.CR3'}
    cases = [r for r in study['records'] if r['mode'] == 'dcp' and Path(r['source']['source']).name in selected]
    if len(cases) != len(selected):
        raise ValueError('Missing reference witness')
    tool_hash = sha256(args.tool); records = []
    freeze = root/'docs/validation/raw'/f'gel-banc-raw-entree-dcp-reference-v001-{stamp}.json'
    sources = {p: dict(sha256=sha256(root/p), text=(root/p).read_text(encoding='utf-8')) for p in
               ('tests/raw_bench/float_reference_smoke.py', 'tests/raw_bench/production_capture.cpp',
                'tests/raw_bench/float_qualification.py', 'tests/raw_bench/capture.py')}
    save(freeze, dict(schema_version=1, started_at_utc=now.isoformat(),
                      engine_revision='ee8f26d4af6e2b3c4edaf71ca62951c13f0187c2',
                      parent=dict(path=str(args.production_study.resolve()), sha256=sha256(args.production_study)),
                      tool=dict(path=str(args.tool.resolve()), sha256=tool_hash), sources=sources,
                      cases=cases, scope='Five A witnesses; actual DCP inlet stats/ROIs and full working-image equality against prior C3. No F1 candidate or signed B tested.'))
    env = dict(os.environ); env['OMP_NUM_THREADS'] = '8'
    env['PATH'] = os.pathsep.join([str(d) for d in args.runtime_dir]+[env.get('PATH', '')])
    empty = work/'empty-profiles'; empty.mkdir()
    print('FROZEN', freeze, flush=True)
    try:
        for index, case in enumerate(cases):
            source = Path(case['source']['source'])
            profile = previous_freeze['profiles'][case['source']['model']]
            if sha256(source) != case['source']['source_sha256'] or sha256(Path(profile['path'])) != profile['sha256']:
                raise ValueError('Source/profile changed')
            out = work/f'{index:02}-{source.stem}.json'
            with (work/f'{index:02}.log').open('x', encoding='utf-8') as log:
                subprocess.run([str(args.tool.resolve()), '--input', str(source), '--output', str(out),
                                '--manifest', str(args.manifest.resolve()), '--profile', profile['path'],
                                '--profile-root', str(empty), '--capture-camera'],
                               env=env, cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=240)
            manifest, _ = load_capture(out)
            if manifest['engine_revision'] != 'ee8f26d4af6e2b3c4edaf71ca62951c13f0187c2':
                raise ValueError('Wrong reference engine')
            check = qualify_inlet(out)
            unchanged = full_equal(manifest['full_image'], case['full_images']['candidate']) and manifest['metadata'] == case['metadata']['candidate']
            if not unchanged:
                raise ValueError('Reference changed against C3')
            # A reference camera signal is non-negative; B later must be tested
            # independently with signedDevelopment, never inferred from this run.
            if any(v != 0 for v in manifest['full_camera']['negative_fraction']):
                raise ValueError('Unexpected negative camera signal in A reference')
            records.append(dict(source=str(source), capture=dict(path=str(out), sha256=sha256(out)),
                                inlet=check, full_working_image_unchanged=True,
                                decode_ms=manifest['decode_ms'], peak_bytes=manifest['decode_peak_working_set_bytes']))
            print(len(records), '/5', source.name, 'reference probe pass', flush=True)
        if sha256(args.tool) != tool_hash or any(sha256(root/p) != v['sha256'] for p, v in sources.items()):
            raise ValueError('Tool sources changed')
        result = root/'docs/validation/raw'/f'banc-raw-entree-dcp-reference-v001-{datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")}.json'
        save(result, dict(schema_version=1, measured_at_utc=datetime.now(timezone.utc).isoformat(),
                          freeze=dict(path=str(freeze), sha256=sha256(freeze)), records=records,
                          verdict='pass_reference_probe_not_F1', limitations='Only A reference. No F1, B, no-profile inlet, optics or nonlinear-DCP signed behavior validation.'))
        print('RESULT', result, flush=True)
    except Exception as error:
        save(work/'partial.json', dict(error=str(error), records=records))
        raise


if __name__ == '__main__':
    main()
