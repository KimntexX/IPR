"""Reference case for the C++ neural module: one real crop, its law and the expected output.

CUDA environment. Takes a 512 x 768 packed crop of a real high-ISO frame
(the climbing gym evaluation crops, normalized by 12303), its measured law
normalized the same way, and computes the expected output exactly as the
production module does for an image smaller than one tile: sigma map on the
whole crop (5x5 mean, window clipped at the borders), crop and sigma padded
by reflection with the 192-cell halo, the exported graph, the halo cropped.
Writes little-endian float32 planes, the law and the expected output, with
a manifest, for tests/neural_model_case_test.cpp.
"""
import argparse
import json
from pathlib import Path

from prepare_next_training import digest, read, write


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT/'build-patch_neural_restoration'
BANK = ROOT/'docs/validation/raw/neural-sensor-noise-v001-20261105T173608Z.json'
DIVISOR = 12303.
HALO = 192
KEY = 'climbing_gym-6400-1-400'


def run(campaign, step, evaluation, output):
    import numpy as np
    import torch
    import torch.nn.functional as functional
    from denoise_to_dng import law_for_iso
    from export_onnx import exportable
    from input_modes import load_trained_model
    from physical_natural_training import sigma_map
    plan = read(campaign/'plan.json')
    model, checkpoint_sha256, checkpoint_step, _ = load_trained_model(campaign, plan, plan['seeds'][0], step)
    graph = exportable(model).cuda()
    crops = np.load(evaluation/'crops.npz')
    planes = np.ascontiguousarray(crops[KEY+'-noisy'][0][:, :512, :768], dtype=np.float32)
    law, provenance = law_for_iso(read(BANK), 6400)
    a = np.array([s for s, _ in law], np.float32)/np.float32(DIVISOR)
    b = np.array([r for _, r in law], np.float32)/np.float32(DIVISOR*DIVISOR)
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    with torch.inference_mode():
        x = torch.from_numpy(planes)[None].cuda()
        sigma = sigma_map(x, torch.from_numpy(a*DIVISOR)[None, :, None, None].cuda(),
                          torch.from_numpy(b*DIVISOR*DIVISOR)[None, :, None, None].cuda(), DIVISOR)
        padded = functional.pad(x, (HALO,)*4, mode='reflect')
        padded_sigma = functional.pad(sigma, (HALO,)*4, mode='reflect')
        expected = graph(padded, padded_sigma)[0, :, HALO:-HALO, HALO:-HALO].cpu().numpy()
    output.mkdir(parents=True, exist_ok=False)
    files = {}
    for name, array in (('planes', planes), ('law_a', a), ('law_b', b), ('expected', expected),
                        ('sigma', sigma[0].cpu().numpy())):
        path = output/f'{name}.bin'
        np.ascontiguousarray(array, dtype='<f4').tofile(path)
        files[name] = dict(file=path.name, sha256=digest(path), shape=list(array.shape))
    write(output/'manifest.json', dict(schema_version=1, campaign=str(campaign), checkpoint_step=checkpoint_step,
        checkpoint_sha256=checkpoint_sha256, crop=KEY, divisor=DIVISOR, halo=HALO, law_provenance=provenance,
        files=files, tolerance=dict(cpu=1e-4, directml=1e-3), tool_sha256=digest(Path(__file__))))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--campaign', type=Path, default=BUILD/'sigma-input-color-training-v001')
    parser.add_argument('--checkpoint-step', type=int, help='a planned intermediate checkpoint; the final one when omitted')
    parser.add_argument('--evaluation', type=Path, default=BUILD/'climbing-gym-evaluation-v001')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(args.campaign.resolve(), args.checkpoint_step, args.evaluation.resolve(), args.output.resolve())
