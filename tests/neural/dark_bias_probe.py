"""Mean bias of the network in blacks and deep shadows, on held-out real dark crops.

CUDA environment. Uses the 'control' dark crops of the frozen v005 arrays,
never drawn during training. Two cases per ISO:
  pure black   the real dark crop normalized as production would, minus the
               nominal 2048 black, and minus the dark frame's own pedestal;
  flat shadows constant clean levels from 0.1 % to 5 % of white, with the
               measured photon noise and the same real dark added, either
               neutral in raw (four equal planes) or at the median raw colour
               ratios of the training tiles (R 0.609, B 0.536 of green).
The bias is the mean of (output - clean) per plane, in native DN, compared
with the noisy input's own mean error. Diagnosis only.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path

from prepare_next_training import digest, read, write


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT/'build-patch_neural_restoration'
LEVELS = (0., .001, .003, .01, .03, .05)
NOMINAL_BLACK = 2048.
# Median plane-to-green ratios of 300 training tiles (R, G1, G2, B).
NATURAL_RATIOS = (.609, 1., 1., .536)


def run(campaign, output):
    import numpy as np
    import sys
    import torch
    from input_modes import ModeCorrector, in_channels
    from physical_natural_training import CANONICAL_TAGS, NoiseModel, sigma_map
    plan, data = read(campaign/'plan.json'), read(campaign/'data.json')
    for name, expected in data['files'].items():
        if digest(campaign/name) != expected:
            raise ValueError('Campaign array changed: '+name)
    sys.path.insert(0, str(BUILD/'third-party-drunet-v001'))
    from models.network_unet import UNetRes
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    output.mkdir(exist_ok=False)
    noise = NoiseModel(campaign, plan, 'cuda')
    tags = torch.tensor([CANONICAL_TAGS], device='cuda')
    mode = plan.get('input_mode', 'sigma_map')
    divisor = noise.divisor
    rows = []
    for seed in plan['seeds']:
        progress = read(campaign/f'seed{seed}/progress.json')
        if digest(campaign/f'seed{seed}/resume.pt') != progress['final_checkpoint_sha256']:
            raise ValueError('Checkpoint changed')
        core = UNetRes(in_nc=in_channels(mode), out_nc=4, nc=[32, 64, 128, 256], nb=4, act_mode='R',
                       downsample_mode='strideconv', upsample_mode='convtranspose')
        model = ModeCorrector(core, mode).cuda()
        state = torch.load(campaign/f'seed{seed}/resume.pt', map_location='cuda', weights_only=False)
        model.load_state_dict(state['model_state'])
        del state
        model.eval()
        with torch.inference_mode():
            for iso_index, iso in enumerate(plan['training_iso']):
                ids = torch.tensor([iso_index], device='cuda')
                a, b = noise.law(ids)
                slope, readv = noise.slope[ids][:, :, None, None], noise.read[ids][:, :, None, None]
                pool = noise.pools[iso_index]['control']
                cases = ('pure_black_nominal', 'pure_black_pedestal')+tuple(
                    f'{kind}_{level:g}' for kind in ('neutral', 'natural') for level in LEVELS if level > 0)
                for case in cases:
                    output_bias, input_bias = [], []
                    for count, index in enumerate(pool):
                        raw = torch.from_numpy(np.asarray(noise.crops[iso_index][index], dtype=np.float32)).cuda()[None]
                        if case.startswith('pure_black'):
                            black = NOMINAL_BLACK if case.endswith('nominal') else torch.from_numpy(
                                noise.pedestals[iso_index][index]).cuda()[None, :, None, None]
                            noisy = (raw-black)/divisor
                            clean = torch.zeros_like(noisy)
                        else:
                            kind, level = case.split('_')[0], float(case.split('_')[1])
                            ratios = torch.tensor(NATURAL_RATIOS if kind == 'natural' else (1.,)*4, device='cuda')
                            clean = level*ratios[None, :, None, None].expand_as(raw).contiguous()
                            dark = noise.dark(iso_index, 'control', count)[None]
                            generator = torch.Generator(device='cuda').manual_seed(7700000+iso_index*1000+count)
                            noisy, _ = noise.sample(clean, ids, dark, generator)
                        sigma = sigma_map(noisy, slope, readv, divisor)
                        corrected = model.correct(noisy, sigma, tags, a, b)['corrected']
                        inner = (Ellipsis, slice(32, -32), slice(32, -32))
                        output_bias.append(((corrected-clean)[inner].mean(dim=(-2, -1))[0]*divisor).tolist())
                        input_bias.append(((noisy-clean)[inner].mean(dim=(-2, -1))[0]*divisor).tolist())
                    level_dn = clean[0].mean(dim=(-2, -1))*divisor
                    rows.append(dict(seed=seed, iso=iso, case=case, crops=len(pool), clean_level_dn=level_dn.tolist(),
                        output_bias_dn=np.mean(output_bias, axis=0).tolist(), input_bias_dn=np.mean(input_bias, axis=0).tolist(),
                        output_bias_spread_dn=np.std(output_bias, axis=0).tolist()))
        del model
        torch.cuda.empty_cache()
    result = dict(schema_version=1, status='dark_bias_diagnosis_not_qualification',
        measured_at_utc=datetime.now(timezone.utc).isoformat(), campaign=str(campaign.resolve()),
        plan_sha256=digest(campaign/'plan.json'), tool_sha256=digest(Path(__file__)), levels=list(LEVELS), natural_ratios=list(NATURAL_RATIOS), rows=rows,
        limits=['Held-out control crops of the same dark frames as training, not new dark captures',
                'Flat shadows use the training simulator for the photon part; the dark part is real',
                'Bias in native DN over the 192x192 inner cells of 256x256 crops'])
    write(output/'result.json', result)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    with (ROOT/'docs/validation/raw'/f'neural-{output.name}-{stamp}.json').open('x', encoding='utf-8') as stream:
        json.dump(result, stream, allow_nan=False, separators=(',', ':'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--campaign', type=Path, default=BUILD/'sigma-input-training-v001')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(args.campaign.resolve(), args.output.resolve())
