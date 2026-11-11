"""Export a trained sigma-input network to ONNX and check it against PyTorch.

CUDA environment with onnx and onnxruntime-directml. The exported graph takes
`planes` and `sigma` (1, 4, H, W), H and W multiples of 8, and returns
`corrected` = planes - F(planes, sigma), exactly the network's sigma_input
mode without the Python-side argument checks (the production module does
them). Concordance on the 72 development observations of the campaign
(same noise draws as its controls): largest absolute difference, PSNR gain
and per-plane mean error, for ONNX Runtime CPU and DirectML. Three reference
tiles (inputs and PyTorch output, little-endian float32) are written for the
C++ tests of the production module.
"""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import time

from prepare_next_training import digest, read, write


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT/'build-patch_neural_restoration'
OPSET = 17
REFERENCE_TILES = ((0, 0), (9, 2), (12, 3))   # (development index, ISO index): source 1 ISO 3200, source 2 ISO 16000 and 51200


def exportable(model):
    """The sigma_input arithmetic alone, as a plain module for tracing."""
    import torch

    class SigmaInputGraph(torch.nn.Module):
        def __init__(self, core):
            super().__init__()
            self.core = core

        def forward(self, planes, sigma):
            return planes-self.core(torch.cat((planes, sigma), dim=1))
    return SigmaInputGraph(model.core).eval()


def gain_db(output, clean, noisy):
    import numpy as np
    inner = (Ellipsis, slice(32, -32), slice(32, -32))
    before = float(np.square((noisy-clean)[inner], dtype=np.float64).mean())
    after = float(np.square((output-clean)[inner], dtype=np.float64).mean())
    return 10*math.log10(before/max(after, 1e-30))


def run(campaign, step, output):
    import numpy as np
    import onnx
    import onnxruntime
    import torch
    from input_modes import load_trained_model
    from physical_natural_training import NoiseModel
    plan = read(campaign/'plan.json')
    if plan.get('input_mode') != 'sigma_input':
        raise ValueError('Only the sigma_input network is exported')
    seed = plan['seeds'][0]
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    model, checkpoint_sha256, checkpoint_step, _ = load_trained_model(campaign, plan, seed, step, device='cpu')
    graph = exportable(model)
    output.mkdir(parents=True, exist_ok=False)
    path = output/f'ipr-neural-denoise-{campaign.name}-step{checkpoint_step}.onnx'
    example = (torch.rand(1, 4, 64, 64)*.1, torch.full((1, 4, 64, 64), .01))
    torch.onnx.export(graph, example, str(path), input_names=['planes', 'sigma'], output_names=['corrected'],
                      dynamic_axes={name: {2: 'height', 3: 'width'} for name in ('planes', 'sigma', 'corrected')},
                      opset_version=OPSET, dynamo=False)
    onnx.checker.check_model(onnx.load(str(path)))
    sessions = {name: onnxruntime.InferenceSession(str(path), providers=[provider])
                for name, provider in (('cpu', 'CPUExecutionProvider'), ('directml', 'DmlExecutionProvider'))}
    for name, session in sessions.items():
        if session.get_providers()[0] != {'cpu': 'CPUExecutionProvider', 'directml': 'DmlExecutionProvider'}[name]:
            raise RuntimeError(f'{name} provider not active')
    graph_cuda = exportable(model).cuda()
    noise = NoiseModel(campaign, plan, 'cuda')
    development = torch.from_numpy(np.load(campaign/'development.npy', allow_pickle=False)).cuda()
    rows, references = [], {}
    with torch.inference_mode():
        for index, target in enumerate(development):
            for iso_index, iso in enumerate(plan['training_iso']):
                g = torch.Generator(device='cuda').manual_seed(9101000+index*8+iso_index)
                clean = target.unsqueeze(0)
                ids = torch.tensor([iso_index], device='cuda')
                dark = noise.dark(iso_index, 'control', index).unsqueeze(0)
                noisy, sigma = noise.sample(clean, ids, dark, g)
                expected = graph_cuda(noisy, sigma).cpu().numpy()
                feeds = dict(planes=noisy.cpu().numpy(), sigma=sigma.cpu().numpy())
                row = dict(target_index=index, iso=iso, torch_gain_db=gain_db(expected, clean.cpu().numpy(), feeds['planes']))
                for name, session in sessions.items():
                    got = session.run(['corrected'], feeds)[0]
                    row[name] = dict(max_abs_difference=float(np.abs(got-expected).max()),
                                     gain_db=gain_db(got, clean.cpu().numpy(), feeds['planes']),
                                     plane_mean_difference=(got-expected)[0].mean(axis=(-2, -1)).astype(float).tolist())
                rows.append(row)
                if (index, iso_index) in REFERENCE_TILES:
                    references[f'dev{index}-iso{iso}'] = dict(planes=feeds['planes'], sigma=feeds['sigma'], corrected=expected)
    tiles = output/'reference-tiles'
    tiles.mkdir()
    manifest = []
    for key, arrays in references.items():
        entry = dict(key=key, shape=list(arrays['planes'].shape), dtype='float32-le')
        for name, array in arrays.items():
            file = tiles/f'{key}-{name}.bin'
            np.ascontiguousarray(array, dtype='<f4').tofile(file)
            entry[name] = dict(file=file.name, sha256=digest(file))
        manifest.append(entry)
    write(tiles/'manifest.json', dict(schema_version=1, model=path.name, model_sha256=digest(path), tiles=manifest,
                                      tolerance_note='Compare with max |difference| <= 1e-4 on CPU and <= 1e-3 on DirectML'))
    timing = {}
    big = dict(planes=np.random.default_rng(1).uniform(0, .2, (1, 4, 1408, 1408)).astype(np.float32),
               sigma=np.full((1, 4, 1408, 1408), .01, np.float32))
    for name, session in sessions.items():
        session.run(['corrected'], big)
        started = time.perf_counter()
        session.run(['corrected'], big)
        timing[name] = time.perf_counter()-started
    summary = {}
    for name in sessions:
        summary[name] = dict(max_abs_difference=max(r[name]['max_abs_difference'] for r in rows),
                             largest_gain_difference_db=max(abs(r[name]['gain_db']-r['torch_gain_db']) for r in rows),
                             largest_plane_mean_difference=max(abs(v) for r in rows for v in r[name]['plane_mean_difference']),
                             tile_1408_seconds=timing[name])
    result = dict(schema_version=1, status='onnx_export_concordance', measured_at_utc=datetime.now(timezone.utc).isoformat(),
        campaign=str(campaign.resolve()), checkpoint_step=checkpoint_step, checkpoint_sha256=checkpoint_sha256, seed=seed,
        model=path.name, model_sha256=digest(path), model_bytes=path.stat().st_size, opset=OPSET,
        onnx_version=onnx.__version__, onnxruntime_version=onnxruntime.__version__, torch_version=str(torch.__version__),
        inputs=dict(planes='(1,4,H,W) float32, (DN - black)/12303, canonical R G1 G2 B', sigma='(1,4,H,W) float32, sqrt(a*max(box5(planes),0)+b)'),
        output='corrected (1,4,H,W) float32 = planes - F(planes, sigma)', summary=summary, rows=rows,
        tool_sha256=digest(Path(__file__)),
        limits=['Development observations of the training simulator, not real photos',
                'DirectML timing on a GPU shared with any running training; one 1408x1408 tile after one warm-up'])
    write(output/'result.json', result)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    with (ROOT/'docs/validation/raw'/f'neural-{output.name}-{stamp}.json').open('x', encoding='utf-8') as stream:
        json.dump(result, stream, allow_nan=False, separators=(',', ':'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--campaign', type=Path, default=BUILD/'sigma-input-color-training-v001')
    parser.add_argument('--checkpoint-step', type=int, default=75000)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(args.campaign.resolve(), args.checkpoint_step, args.output.resolve())
