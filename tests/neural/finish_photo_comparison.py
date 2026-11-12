"""Render sealed real-photo variants through the same RCD and display snapshot."""
import argparse
from datetime import datetime, timezone
import html
import json
from pathlib import Path
import subprocess
import sys

from prepare_photo_comparison import digest, orient_buffer


def run(output):
    root = Path(__file__).resolve().parents[2]
    build = root / 'build-patch_neural_restoration'
    if not output.resolve().is_relative_to(build.resolve()):
        raise ValueError('Dedicated build output required')
    output.mkdir(parents=True, exist_ok=False)
    base = build / 'real-photo-baselines-v001'
    plan = json.loads((base / 'plan.json').read_text(encoding='utf-8'))
    baseline = json.loads((base / 'progress.json').read_text(encoding='utf-8'))
    inference_path = build / 'real-photo-neural-v001/result.json'
    inference = json.loads(inference_path.read_text(encoding='utf-8'))
    if not inference['status'].startswith('completed_'):
        raise ValueError('Inference incomplete')
    native = plan['native']['path']
    if digest(native) != plan['native']['sha256']:
        raise ValueError('Native comparator changed')
    display = build / 'native/Release/iprNeuralDisplayCapture.exe'
    result = dict(schema_version=1, status='started', started_at_utc=datetime.now(timezone.utc).isoformat(),
                  plan_sha256=digest(base / 'plan.json'), inference_sha256=digest(inference_path),
                  renderer_sha256=digest(display), scenes=[],
                  limits=['Development photos without clean truth; no photographic qualification.',
                          'Classic comparator is intensity only, excluding colour after tones.',
                          'Both fixed seeds retained; supplied sigma is not ISO calibration or an equivalent slider.',
                          'Common frozen DCP display snapshot, not a claim of current complete production parity.'])

    def execute(job, command):
        subprocess.run([sys.executable, 'tests/neural/run_guarded_native.py', '--output', str(job),
                        '--hard-mib', '4096', '--soft-mib', '3072', '--', *map(str, command)], check=True)

    def render(folder, label, camera, context, halo=0):
        prefix = folder / (label + '-srgb')
        execute(folder / (label + '-display-job'), [display, '--input', camera, '--context', context,
                                                    '--output', prefix, '--halo', halo])
        return dict(label=label, display=str(prefix) + '.json', camera=str(camera), halo=halo)

    try:
        for scene in plan['scenes']:
            name = scene['name']
            folder = output / name
            folder.mkdir()
            parent = next(r for r in baseline['scenes'] if r['name'] == name)
            records = [render(folder, 'RCD-neutral', parent['camera'], scene['context'])]
            records.append(render(folder, 'RCD-neutral-context', parent['camera'], scene['context'], 192))
            for state in parent['classic_outputs']:
                records.append(render(folder, f"classic-intensity-{state['strength']}", state['path'], scene['context']))
            for item in [r for r in inference['records'] if r['scene'] == name]:
                if digest(item['path']) != item['sha256']:
                    raise ValueError('Corrected CFA changed')
                label = f"neural-seed{item['seed']}-sigma{item['sigma']}"
                prefix = folder / label
                height, width, _ = item['shape']
                pattern = ''.join('RGBG'[v] for row in scene['pattern'] for v in row)
                execute(folder / (label + '-rcd-job'), [native, '--input', item['path'], '--width', width,
                        '--height', height, '--output', prefix, '--pattern', pattern, '--kind', 'cfa',
                        '--median', 'flat', '--range', 'signed', '--denoise=', '--repeat', '1'])
                camera, shape = orient_buffer(Path(str(prefix) + '.rcd.f32'), [height, width, 3],
                                              scene['orientation'], folder / (label + '-oriented.f32'))
                if shape != parent['shape']:
                    raise ValueError('RGB geometry differs')
                records.append(render(folder, label, camera, scene['context']))
            result['scenes'].append(dict(name=name, iso=scene['iso'], variants=records))
            (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
            print('DISPLAY', name, flush=True)
        result['status'] = 'completed_real_photo_display_not_qualification'
    except Exception as error:
        result.update(status='stopped_on_error', error=str(error))
        raise
    finally:
        (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    panels = []
    for scene in result['scenes']:
        for roi in range(9):
            figures = []
            for variant in scene['variants']:
                if variant['halo']:
                    continue
                relative = f"{scene['name']}/{variant['label']}-srgb-roi{roi}.png"
                figures.append(f'<figure><figcaption>{html.escape(variant["label"])}</figcaption>'
                               f'<img width="384" height="384" src="{relative}"></figure>')
            panels.append(f'<h2>{scene["name"]} ISO {scene["iso"]} — ROI {roi}</h2><div>{"".join(figures)}</div>')
    (output / 'index.html').write_text('<!doctype html><meta charset="utf-8"><title>Real photo comparison</title>'
        '<style>body{background:#24272b;color:#eee;font:16px sans-serif}div{display:flex;overflow:auto}'
        'figure{margin:8px;flex:none}img{image-rendering:auto}figcaption{padding:8px}</style>'
        '<h1>Exploratory comparison, 100% pixels</h1><p>' + html.escape(' '.join(result['limits'])) + '</p>'
        + ''.join(panels), encoding='utf-8')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    proof = root / 'docs/validation/raw' / f'neural-real-photo-display-v001-{stamp}.json'
    proof.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    run(parser.parse_args().output)
