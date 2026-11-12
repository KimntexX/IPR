"""CPU cost of the trained network on a full R6 III frame, PyTorch fp32 at 8 threads.

Times one inference per tile size on random planes (cost does not depend on
content), then counts the tiles a full 2319 x 3479 packed frame needs for a
given core and halo. The exact receptive radius (dru_cfa.support) gives the
seamless halo; production would use ONNX Runtime, so this is a reference
order of magnitude, not the shipped cost.
"""
import argparse
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import platform
import time

from prepare_next_training import digest, read, write


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT/'build-patch_neural_restoration'
FRAME = (2319, 3479)
LAYOUTS = ((512, 192), (1024, 192), (512, 64))


def tile_count(frame, core):
    return math.ceil(frame[0]/core)*math.ceil(frame[1]/core)


def run(campaign, output, threads):
    import sys
    import torch
    from dru_cfa import support
    from input_modes import ModeCorrector, in_channels
    plan = read(campaign/'plan.json')
    sys.path.insert(0, str(BUILD/'third-party-drunet-v001'))
    from models.network_unet import UNetRes
    torch.set_num_threads(threads)
    torch.set_num_interop_threads(1)
    mode = plan.get('input_mode', 'sigma_map')
    core = UNetRes(in_nc=in_channels(mode), out_nc=4, nc=[32, 64, 128, 256], nb=4, act_mode='R',
                   downsample_mode='strideconv', upsample_mode='convtranspose')
    model = ModeCorrector(core, mode).eval()
    seed = plan['seeds'][0]
    state = torch.load(campaign/f'seed{seed}/resume.pt', map_location='cpu', weights_only=False)
    model.load_state_dict(state['model_state'])
    del state
    output.mkdir(exist_ok=False)
    tags = torch.tensor([[0, 1, 3, 2]])
    a, b = torch.full((1, 4, 1, 1), 5e-4), torch.full((1, 4, 1, 1), 1e-6)
    layouts = []
    with torch.inference_mode():
        for core_side, halo in LAYOUTS:
            side = core_side+2*halo
            side += (-side) % 8
            x = torch.rand(1, 4, side, side)*.1
            sigma = (a*x+b).sqrt()
            model.correct(x, sigma, tags, a, b)
            times = []
            for _ in range(3):
                started = time.perf_counter()
                model.correct(x, sigma, tags, a, b)
                times.append(time.perf_counter()-started)
            tiles = tile_count(FRAME, core_side)
            layouts.append(dict(core=core_side, halo=halo, tile_side=side, tile_seconds_min=min(times),
                tile_seconds=times, tiles=tiles, frame_seconds=tiles*min(times),
                seamless=halo >= support()['packed_radius']))
    result = dict(schema_version=1, status='cpu_cost_reference_not_shipped_runtime',
        measured_at_utc=datetime.now(timezone.utc).isoformat(), campaign=str(campaign.resolve()),
        input_mode=mode, threads=threads, cpu_count=os.cpu_count(), processor=platform.processor(),
        torch_version=str(torch.__version__), frame_packed=list(FRAME), receptive_radius_packed=support()['packed_radius'],
        layouts=layouts, opening_budget_seconds=20.8, tool_sha256=digest(Path(__file__)),
        limits=['PyTorch fp32 eager on CPU; ONNX Runtime, fused kernels or reduced precision not measured',
                'Only the network; raw decoding, sigma map and tiling copies are not timed',
                'Halo 64 is not seamless; its seam error is not measured here'])
    write(output/'result.json', result)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    with (ROOT/'docs/validation/raw'/f'neural-{output.name}-{stamp}.json').open('x', encoding='utf-8') as stream:
        json.dump(result, stream, allow_nan=False, separators=(',', ':'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--campaign', type=Path, default=BUILD/'sigma-input-training-v001')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--threads', type=int, default=8)
    args = parser.parse_args()
    run(args.campaign.resolve(), args.output.resolve(), args.threads)
