"""Exploratory green-only sheets on cached camera CFA; no clean reference."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import struct
import time
import zlib

import numpy as np
import torch

from audit_neural_tiles import validate_capture_coverage
from audit_spectral_noise import source_contract
from bayer_canonical import predict
from compare_reconstruction_objectives import confined
from diagnose_train_learnability import digest, save
from edge_protected_noise import EdgeProtectedNoisyUNet
from joint_bayer_unet import sample_rgb
from memory_budget import MemoryGuard, BudgetExceeded
from mosaic_data import pack_mosaic, read_stage
from neural_inference_contract import check_inference, INFERENCE_TOOLS
from prepare_closed_inference import prepare_inference
from run_noisy_moment_pilot import runtime_versions


def display_window(reference):
    if reference.dtype != np.float32 or reference.shape != (256, 256) or not np.isfinite(reference).all():
        raise ValueError('Finite camera green256x256 required')
    low, high = np.quantile(reference, [.005, .995]).tolist()
    middle = (low+high)/2
    half = max((high-low)/2, .01)
    return dict(low=middle-half, high=middle+half, gamma=2.2,
        policy='Shared RCD-only0.5/99.5percentile window, minimum span0.02, no independent panel normalization.')


def gray_view(green, window):
    if green.shape != (256, 256) or green.dtype != np.float32 or not np.isfinite(green).all():
        raise ValueError('Finite camera green256x256 required')
    view = np.clip((green-window['low'])/(window['high']-window['low']), 0, 1)
    return np.round(np.power(view, 1/window['gamma'])*255).astype(np.uint8)


def write_png(path, image):
    if image.ndim != 2 or image.dtype != np.uint8 or not image.size:
        raise ValueError('Nonempty grayscale8 image required')
    def chunk(kind, data):
        return struct.pack('>I', len(data))+kind+data+struct.pack('>I', zlib.crc32(kind+data))
    height, width = image.shape
    rows = b''.join(b'\x00'+row.tobytes() for row in image)
    with path.open('xb') as handle:
        handle.write(b'\x89PNG\r\n\x1a\n')
        handle.write(chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 0, 0, 0, 0)))
        handle.write(chunk(b'IDAT', zlib.compress(rows)))
        handle.write(chunk(b'IEND', b''))


def read_camera_rgb(camera, tile):
    if tile['dtype'] != '<f4' or tile['shape'] != [384, 384, 3] or tile['offset_bytes'] < 0:
        raise ValueError('Declared camera RGB384 tile required')
    path = Path(camera['descriptor']).parent/camera['pixels']
    data = np.fromfile(path, dtype='<f4', count=384*384*3, offset=tile['offset_bytes'])
    if data.size != 384*384*3 or not np.isfinite(data).all():
        raise ValueError('Truncated or nonfinite camera RGB')
    return data.reshape(384, 384, 3)


def run(root, proof_path, captures_path, output):
    confined(root, [proof_path, captures_path, output])
    proof = json.loads(proof_path.read_text(encoding='utf-8'))
    plan = source_contract(root, proof)
    if plan['data']['runtime'] != runtime_versions() or plan['kind'] != 'protected':
        raise ValueError('Pinned CPU protected model required')
    check_inference(plan['data'], plan['tools'])
    refs = plan['models']
    if len(refs) != 2 or {r['seed'] for r in refs} != {71, 73}:
        raise ValueError('Both model seeds required')
    inlet = json.loads(captures_path.read_text(encoding='utf-8'))
    if inlet['status'] != 'qualified_signed_mosaic_inlet_not_joint_model':
        raise ValueError('Qualified CFA inlet required')
    sources = [r['capture'] for r in inlet['records']]
    validate_capture_coverage(sources)
    camera_sources = []
    for capture in sources:
        descriptor = Path(capture['descriptor'])
        camera = Path(capture['camera_proof'])
        confined(root, [descriptor, camera, descriptor.parent/capture['pixels']])
        if (digest(descriptor) != capture['descriptor_sha256'] or digest(camera) != capture['camera_proof_sha256']
                or digest(descriptor.parent/capture['pixels']) != capture['pixels_sha256']):
            raise ValueError('Cached CFA or RGB proof changed')
        reference = json.loads(camera.read_text(encoding='utf-8'))
        reference['descriptor'] = str(camera)
        pixels = camera.parent/reference['pixels']
        confined(root, [pixels])
        if digest(pixels) != reference['pixels_sha256'] or len(reference['tiles']) != 9:
            raise ValueError('Cached camera RGB changed or incomplete')
        camera_sources.append(reference)
    now = datetime.now(timezone.utc)
    identity = 'neural-camera-visual-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    output.mkdir(parents=True, exist_ok=False)
    freeze = dict(source_proof_sha256=digest(proof_path), source_quality_passed=proof['pilot_gates_passed'],
        inlet_sha256=digest(captures_path), models=refs, sources=sources, sigma_scenarios=[0., .01, .03],
        domain='Signed balanced camera RGB, same pre-RCD white bound as cached RCD/median; no LCP/DCP/tones.',
        display='Green channel only, shared contrast window per ROI, gamma2.2; not an sRGB colour rendering.',
        limits=['RCD is a current-process comparison, not clean truth or an AI quality target.',
            'Constant supplied sigma is not calibrated camera noise or an ISO-specific denoising setting.',
            'Cached training-side sources only, no independent photographic validation.',
            'These diagnostic PNGs do not enter production or replace the canonical numerical JSON.'],
        tools={n: digest(Path(__file__).with_name(n)) for n in
            tuple(dict.fromkeys(INFERENCE_TOOLS+('preview_camera_neural.py', 'mosaic_data.py',
                'audit_neural_tiles.py', 'audit_spectral_noise.py', 'neural_inference_contract.py')))})
    save(output/'freeze.json', freeze)
    result = dict(schema_version=1, data_version=1, campaign_id=identity, measured_at_utc=now.isoformat(),
        freeze=freeze, freeze_sha256=digest(output/'freeze.json'), status='incomplete', records=[])
    guard, start = MemoryGuard(**plan['data']['memory_policy']), time.monotonic()
    try:
        with guard, torch.inference_mode():
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            for ref in refs:
                path = Path(ref['folder'])/'weights.pt'
                confined(root, [path])
                if digest(path) != ref['weights_sha256']:
                    raise ValueError('Model changed')
                model = EdgeProtectedNoisyUNet(plan['data']['config']['width']).eval().requires_grad_(False)
                model.load_state_dict(torch.load(path, weights_only=True, map_location='cpu')['state_dict'], strict=True)
                model = prepare_inference(model)
                for capture, camera in zip(sources, camera_sources):
                    for stage in capture['stages']:
                        if stage['stage'] != 'balanced_cfa_pre_rcd_white_clipped':
                            continue
                        guard.check()
                        roi = stage['roi']
                        tile = camera['tiles'][roi]
                        if tile['shape'] != [384, 384, 3] or tile['origin_xy'] != stage['origin_xy']:
                            raise ValueError('Camera RGB/CFA ROI alignment differs')
                        reference = read_camera_rgb(camera, tile)[64:-64, 64:-64]
                        packed = torch.from_numpy(pack_mosaic(read_stage(capture, stage)))[None]
                        tags = torch.tensor(np.asarray(stage['pattern']).reshape(1, 4), dtype=torch.int64)
                        window = display_window(reference[:, :, 1])
                        panels, measurements = [reference[:, :, 1]], []
                        for sigma in freeze['sigma_scenarios']:
                            guard.check()
                            image = predict(model, packed/2, torch.full_like(packed, sigma/2), tags, False, True)*2
                            if sigma == 0 and not torch.equal(sample_rgb(image, tags), packed):
                                raise ValueError('Zero-noise CFA sites changed')
                            image = image[0].permute(1, 2, 0).numpy()[64:-64, 64:-64].copy()
                            panels.append(image[:, :, 1])
                            measurements.append(dict(supplied_sigma=sigma,
                                rgb_mean=image.mean(axis=(0, 1), dtype=np.float64).tolist(),
                                rgb_rms_disagreement_to_rcd=float(np.sqrt(np.mean((image-reference).astype(np.float64)**2))),
                                display_low_fraction=float((image[:, :, 1] < window['low']).mean()),
                                display_high_fraction=float((image[:, :, 1] > window['high']).mean()),
                                negative_fraction=float((image < 0).mean())))
                        sheet = np.concatenate([gray_view(panel, window) for panel in panels], axis=1)
                        name = f"seed{ref['seed']}-{Path(capture['source']).stem}-roi{roi}.png"
                        write_png(output/name, sheet)
                        result['records'].append(dict(seed=ref['seed'], source_sha256=capture['source_sha256'],
                            iso=capture['iso'], roi=roi, origin_xy=stage['origin_xy'], pattern=stage['pattern'],
                            display_window=window, measurements=measurements, sheet=name, sheet_sha256=digest(output/name)))
                del model
                print('PREVIEWED seed'+str(ref['seed']), flush=True)
            if len(result['records']) != 54:
                raise ValueError('Missing photographic preview ROIs')
            from html import escape
            content = ''.join('<figure><figcaption>'+escape(row['sheet'])+'</figcaption>'
                '<div class="labels"><span>RCD / median</span><span>U-Net sigma0</span>'
                '<span>U-Net sigma0.01</span><span>U-Net sigma0.03</span></div>'
                '<img alt="Green camera channel diagnostic comparison" src="'+escape(row['sheet'], quote=True)+'"></figure>'
                for row in result['records'])
            with (output/'index.html').open('x', encoding='utf-8') as handle:
                handle.write('<!doctype html><meta charset="utf-8"><title>Camera neural diagnostic</title>'
                    '<style>body{background:#222;color:#eee;font:16px sans-serif;margin:24px}'
                    'figure{margin:25px 0;width:1024px}img{width:1024px;image-rendering:pixelated}'
                    '.labels{display:grid;grid-template-columns:repeat(4,1fr);margin:8px 0}</style>'
                    '<h1>Exploratory camera green channel</h1><p>No clean truth; constant sigma scenarios,'
                    ' shared contrast stretch per ROI. No DCP or sRGB colour rendering. '
                    'Both model seeds, three cached training-side sources.</p>'+content)
            result['index_sha256'] = digest(output/'index.html')
            guard.check()
            if any(digest(Path(__file__).with_name(n)) != h for n, h in freeze['tools'].items()):
                raise ValueError('Preview implementation changed')
            result['status'] = 'completed_unreferenced_camera_visual_probe_not_photographic_qualification'
    except (BudgetExceeded, MemoryError) as error:
        result['status'] = 'aborted_for_memory_budget'
        result['error'] = str(error)
        raise
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        result['memory'] = guard.stats()
        result['elapsed_seconds'] = time.monotonic()-start
        save(output/'result.json', result)
        save(root/'docs/validation/raw'/f'{identity}.json', result)
    print('COMPLETE '+identity, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--proof', type=Path, required=True)
    parser.add_argument('--captures', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(Path(__file__).resolve().parents[2], args.proof, args.captures, args.output)
