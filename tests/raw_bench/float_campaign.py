"""F1 signed decoder inlet qualification on existing pairs; requires F1 tool/API."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import numpy as np
from capture import load_capture, sha256
from float_qualification import compare_module_inlet, qualify_inlet
from signed_campaign import select_inputs


def save(path, data, exclusive=True):
    with path.open('x' if exclusive else 'w', encoding='utf-8') as handle:
        json.dump(data, handle, ensure_ascii=False, indent=2, allow_nan=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ('baseline', 'production-freeze', 'manifest', 'tool', 'engine-source'):
        parser.add_argument('--'+arg, type=Path, required=True)
    parser.add_argument('--engine-revision', required=True)
    parser.add_argument('--module-study', type=Path, required=True, help='F1 signed module study for exact black-witness inlet comparison')
    parser.add_argument('--runtime-dir', action='append', type=Path, required=True)
    args = parser.parse_args(); root = Path(__file__).resolve().parents[2]
    started = datetime.now(timezone.utc); stamp = started.strftime('%Y%m%dT%H%M%SZ')
    work = root/'build-patch_raw_bench/float-signed'/stamp; work.mkdir(parents=True)
    baseline = json.loads(args.baseline.read_text(encoding='utf-8'))
    inputs = select_inputs(baseline)
    module_study = json.loads(args.module_study.read_text(encoding='utf-8'))
    production = json.loads(args.production_freeze.read_text(encoding='utf-8'))
    profile = production['profiles']['EOS R6 Mark III']
    if sha256(args.manifest) != production['manifest']['sha256'] or sha256(Path(profile['path'])) != profile['sha256']:
        raise ValueError('Changed profile/calibration')
    tool_hash = sha256(args.tool)
    files = ['tests/raw_bench/float_campaign.py', 'tests/raw_bench/float_qualification.py',
             'tests/raw_bench/capture.py', 'tests/raw_bench/production_capture.cpp', 'tests/raw_bench/signed_campaign.py']
    sources = {p: dict(sha256=sha256(root/p), text=(root/p).read_text(encoding='utf-8')) for p in files}
    engine_hashes = {str(p.relative_to(args.engine_source)): sha256(p)
                     for p in (args.engine_source/'src').rglob('*') if p.is_file() and p.suffix in ('.h', '.cpp')}
    if not (args.engine_source/'src/raw/RawDecoder.cpp').is_file() or not engine_hashes:
        raise ValueError('Missing frozen engine source tree')
    freeze = root/'docs/validation/raw'/f'gel-banc-raw-flottant-signe-v001-{stamp}.json'
    save(freeze, dict(schema_version=1, started_at_utc=started.isoformat(), inputs=inputs,
                      parents={str(p.resolve()): sha256(p) for p in (args.baseline, args.production_freeze, args.module_study)},
                      profile=profile, manifest=dict(path=str(args.manifest.resolve()), sha256=sha256(args.manifest)),
                      tool=dict(path=str(args.tool.resolve()), sha256=tool_hash), engine_revision=args.engine_revision,
                      engine_source=str(args.engine_source.resolve()), engine_hashes=engine_hashes, sources=sources,
                      conditions='signedDevelopment requested, WB as-shot, optics off, actual pre-DCP inlet retained; existing six noise pairs, full-frame stats and three256 ROIs. Native decode budget20.8s, 8 RCD workers, OpenCV1/OMP8.',
                      limitations='No nonlinear DCP range modification, no optics/fallback/no-profile inlet coverage, no visual or denoising benefit claim. Negative presence alone does not prove absence of every intermediate quantization.'))
    env = dict(os.environ); env['OMP_NUM_THREADS'] = '8'
    env['PATH'] = os.pathsep.join([str(d) for d in args.runtime_dir]+[env.get('PATH', '')])
    empty = work/'empty-profiles'; empty.mkdir(); records = []

    def status(**values):
        save(work/'status.json', dict(started_at_utc=started.isoformat(),
                                      updated_at_utc=datetime.now(timezone.utc).isoformat(), **values), False)

    print('FROZEN', freeze, flush=True)
    try:
        for index, pair in enumerate(inputs):
            for shot, item in enumerate(pair['shots']):
                source = Path(item['path'])
                status(status='running', phase='decode', completed=len(records), source=source.name, role=pair['role'])
                out = work/f'{index:02}-{shot}-{source.stem}.json'
                with (work/f'{index:02}-{shot}.log').open('x', encoding='utf-8') as log:
                    subprocess.run([str(args.tool.resolve()), '--input', str(source), '--output', str(out),
                                    '--manifest', str(args.manifest.resolve()), '--profile', profile['path'],
                                    '--profile-root', str(empty), '--capture-camera', '--development', 'signed'],
                                   cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=240)
                manifest, stages = load_capture(out)
                if (manifest['mode'] != 'signed' or manifest['engine_revision'] != args.engine_revision or
                        manifest['source']['sha256'] != item['sha256'] or manifest['metadata']['iso'] != pair['iso'] or
                        not manifest.get('capabilities', {}).get('signed_development')):
                    raise ValueError('Wrong source/engine/policy/capability')
                inlet = qualify_inlet(out, require_negatives=pair['role'] == 'black')
                matched = [r for r in module_study['records'] if r['iso'] == pair['iso'] and r['role'] == pair['role']]
                if len(matched) != 1:
                    raise ValueError('Module pair missing or ambiguous')
                module = matched[0]['captures']['signed'][shot]
                if sha256(Path(module['path'])) != module['sha256']:
                    raise ValueError('Module capture changed')
                transfer = compare_module_inlet(out, module['path'])
                if pair['role'] == 'black' and transfer['verdict'] != 'pass_exact_module_to_decoder_inlet':
                    raise ValueError('Black witness lacks exact module/inlet comparison')
                if not all(np.isfinite(values).all() for _, values in stages.values()):
                    raise ValueError('Nonfinite camera/master capture')
                budget = manifest['decode_ms'] <= 20800 and manifest['rcd_workers'] <= 8 and manifest['opencv_threads'] <= 1
                records.append(dict(iso=pair['iso'], role=pair['role'], source=item,
                                    capture=dict(path=str(out), sha256=sha256(out)), inlet=inlet, module_to_inlet=transfer,
                                    metadata=manifest['metadata'], decode_ms=manifest['decode_ms'],
                                    peak_bytes=manifest['decode_peak_working_set_bytes'], resource_budget_pass=budget))
                print(len(records), '/12', source.name, 'inlet pass', 'budget', budget, flush=True)
        if sha256(args.tool) != tool_hash or any(sha256(root/p) != v['sha256'] for p, v in sources.items()):
            raise ValueError('Bench sources/tool changed')
        if any(sha256(args.engine_source/p) != digest for p, digest in engine_hashes.items()):
            raise ValueError('Engine sources changed')
        if sha256(args.manifest) != production['manifest']['sha256'] or sha256(Path(profile['path'])) != profile['sha256']:
            raise ValueError('Profile/calibration changed')
        verdict = 'pass_scoped_F1_signed_inlet' if all(r['resource_budget_pass'] for r in records) else 'fail_resource_budget'
        result = root/'docs/validation/raw'/f'banc-raw-flottant-signe-v001-{datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")}.json'
        save(result, dict(schema_version=1, measured_at_utc=datetime.now(timezone.utc).isoformat(),
                          freeze=dict(path=str(freeze), sha256=sha256(freeze)), records=records, verdict=verdict,
                          limitations='F1 signed inlet only, not A invariance, clean noise truth, visual quality or fully signed DCP processing. Peak memory is measured, not a system80% ceiling proof.'))
        status(status='succeeded' if verdict.startswith('pass') else 'failed', phase='complete', completed=len(records), result=str(result))
        print('RESULT', result, verdict, flush=True)
    except Exception as error:
        save(work/'partial.json', dict(error=str(error), records=records))
        status(status='failed', phase='error', completed=len(records), error=str(error))
        raise


if __name__ == '__main__':
    main()
