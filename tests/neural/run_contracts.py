"""Run bounded synthetic CPU checks and write a new immutable evidence JSON.

No photo input, weight loading, downloads or production rendering. Training in
one test only verifies gradient wiring; its disposable weights are not saved.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import subprocess
import time
import unittest

import torch

from camera_unet import CameraUNet


def run(output_dir):
    torch.set_num_threads(2)
    torch.set_num_interop_threads(1)
    torch.manual_seed(41)
    torch.use_deterministic_algorithms(True)
    root = Path(__file__).resolve().parents[2]
    expected = root/'build-patch_neural_restoration'
    output_dir = output_dir.resolve()
    if not output_dir.is_relative_to(expected.resolve()):
        raise ValueError('Evidence output must stay inside the dedicated bench build')
    output_dir.mkdir(parents=True, exist_ok=True)
    start = time.perf_counter()
    suite = unittest.defaultTestLoader.discover(str(Path(__file__).parent),
                                               pattern='test_camera_unet.py')
    stream = io.StringIO()
    result = unittest.TextTestRunner(stream=stream, verbosity=2).run(suite)
    elapsed = time.perf_counter()-start
    files = [Path(__file__), Path(__file__).with_name('camera_unet.py'),
             Path(__file__).with_name('test_camera_unet.py')]
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    proof = dict(schema_version=1, created_utc=stamp, study='neural_signed_contracts',
                 revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,
                                                  text=True).strip(),
                 source_sha256={str(p.relative_to(root)).replace('\\','/'):
                                hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
                 python=platform.python_version(), torch=torch.__version__,
                 platform=platform.platform(), device='cpu',
                 threads_intra=torch.get_num_threads(), threads_inter=torch.get_num_interop_threads(),
                 host_logical_threads=os.cpu_count(), seed=41,
                 parameter_count=sum(p.numel() for p in CameraUNet().parameters()),
                 tests_run=result.testsRun, failures=len(result.failures),
                 errors=len(result.errors), skipped=len(result.skipped),
                 success=result.wasSuccessful(), elapsed_seconds=elapsed,
                 test_log=stream.getvalue(),
                 installed_packages=subprocess.check_output(
                     [os.sys.executable,'-m','pip','freeze'], text=True).splitlines(),
                 limits=['Technical synthetic checks only; no photographic quality or generalization result.',
                         'Two-level width-8 random-weight U-Net is a bench prototype, not a selected production model.',
                         'Tiny single-patch optimization is not a corpus training campaign; no weights saved.',
                         'No CFA/Canon noise model, GPU, ONNX, tiling, latency or peak-memory qualification.',
                         'Extended range is checked at identity; learned photometric fidelity remains unqualified.',
                         'No image, sealed frame, Lightroom TIFF or source decoder was read or changed.'])
    destination = output_dir/f'neural-contracts-v001-{stamp}.json'
    with destination.open('x', encoding='utf-8') as handle:
        json.dump(proof, handle, ensure_ascii=False, indent=2, allow_nan=False)
    print(stream.getvalue())
    print(destination)
    print(json.dumps({k:proof[k] for k in ('success','tests_run','parameter_count','elapsed_seconds')}))
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path,
                        default=Path(__file__).resolve().parents[2]/'build-patch_neural_restoration')
    raise SystemExit(run(parser.parse_args().output_dir))
