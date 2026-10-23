"""Audit actual C++ Haar helper and independent noise-level prototypes. No denoising."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import struct
import subprocess
import sys
import tempfile

import numpy as np
import scipy
from scipy.fft import dctn
from scipy.ndimage import convolve, gaussian_filter
from numpy.lib.stride_tricks import sliding_window_view

NORMAL_MAD = .67448975
NOISES = ("clean", "white_005", "white_025", "white_050", "correlated_025", "signal_dependent", "mixed_impulse")


def upper_median(values):
    values = np.asarray(values)
    if not values.size:
        return 0.
    return np.partition(values, values.size // 2)[values.size // 2]


def production_port(plane):
    """Float32, upper median, exact-constant exclusion and sampling stride of C++."""
    p = np.asarray(plane, dtype=np.float32)
    rows, cols = p.shape[0] // 2, p.shape[1] // 2
    stride = max(1, int(np.sqrt(rows * cols / 16384.)))
    a = p[:2 * rows:2 * stride, :2 * cols:2 * stride]
    b = p[:2 * rows:2 * stride, 1:2 * cols:2 * stride]
    c = p[1:2 * rows:2 * stride, :2 * cols:2 * stride]
    d = p[1:2 * rows:2 * stride, 1:2 * cols:2 * stride]
    valid = ~((a == b) & (a == c) & (a == d))
    hh = ((a - b - c + d) * np.float32(.5))[valid]
    if not hh.size:
        return 0.
    return float(np.float32(upper_median(np.abs(hh - upper_median(hh)))) / np.float32(NORMAL_MAD))


def block_counts(plane):
    p = np.asarray(plane, dtype=np.float32)
    rows, cols = p.shape[0] // 2, p.shape[1] // 2
    stride = max(1, int(np.sqrt(rows * cols / 16384.)))
    a = p[:2 * rows:2 * stride, :2 * cols:2 * stride]
    b = p[:2 * rows:2 * stride, 1:2 * cols:2 * stride]
    c = p[1:2 * rows:2 * stride, :2 * cols:2 * stride]
    d = p[1:2 * rows:2 * stride, 1:2 * cols:2 * stride]
    constant = (a == b) & (a == c) & (a == d)
    return dict(sampled_blocks=int(a.size), constant_blocks=int(constant.sum()),
                retained_blocks=int((~constant).sum()), stride=stride)


def quantization_controls():
    clean = scenes()["slanted_edge"]
    base = np.random.default_rng(1409).normal(size=clean.shape)
    for sigma in (0., 1e-7, 1e-5, 1e-4, .001, .005):
        for step in (0., 1 / 65535, 1 / 255):
            observed = clean + sigma * base
            if step:
                observed = np.round(observed / step) * step
            yield dict(prequantization_sigma=sigma, quantization_step=step,
                       realized_error_std=float(np.std(observed - clean)),
                       plane=observed.astype(np.float32))


def haar_all(plane):
    p = np.asarray(plane, dtype=np.float64)
    p = p[:p.shape[0] // 2 * 2, :p.shape[1] // 2 * 2]
    return (p[::2, ::2] - p[::2, 1::2] - p[1::2, ::2] + p[1::2, 1::2]) / 2


def mad(values):
    return float(np.median(np.abs(values - np.median(values))) / NORMAL_MAD) if values.size else 0.


def patches(plane, size, stride):
    blocks = sliding_window_view(plane, (size, size))[::stride, ::stride].reshape(-1, size, size)
    return blocks[np.ptp(blocks, axis=(1, 2)) > 0]


def prototypes(plane):
    p = np.asarray(plane, dtype=np.float64)
    if p.ndim != 2 or min(p.shape) < 8 or not np.isfinite(p).all():
        raise ValueError("Expected a finite 2D plane, at least 8x8")
    kernel = np.outer([1., -2., 1.], [1., -2., 1.]) / 6
    response = convolve(p, kernel, mode="mirror")[1:-1, 1:-1]
    blocks = patches(p, 8, 4)
    dct_sigma = 0.
    selected_count = 0
    if len(blocks):
        spectrum = dctn(blocks, axes=(-2, -1), norm="ortho")
        y, x = np.mgrid[:8, :8]
        low = ((x + y) > 0) & ((x + y) <= 3)
        high = (x + y) >= 8
        energy = np.mean(spectrum[:, low] ** 2, axis=1)
        selected_count = min(len(blocks), max(32, int(np.ceil(.2 * len(blocks)))))
        chosen = np.argsort(energy, kind="stable")[:selected_count]
        dct_sigma = mad(spectrum[chosen][:, high].ravel())
    blocks_pca = patches(p, 7, 3)
    pca_sigma = 0.
    if len(blocks_pca) >= 64:
        vectors = blocks_pca.reshape(-1, 49)
        centered = vectors - np.mean(vectors, axis=0)
        covariance = centered.T @ centered / (len(vectors) - 1)
        eigenvalues = np.linalg.eigvalsh(covariance)
        pca_sigma = float(np.sqrt(np.maximum(eigenvalues[:16], 0).mean()))
    return dict(haar_python_previous=mad(haar_all(p)), laplacian_mad=mad(response),
                laplacian_mean=float(np.mean(np.abs(response)) * np.sqrt(np.pi / 2)),
                weak_dct=dct_sigma, pca_tail_raw=pca_sigma), dict(dct_patches=len(blocks),
                dct_selected=selected_count, pca_patches=len(blocks_pca),
                pca_available=len(blocks_pca) >= 64)


def scenes():
    y, x = np.mgrid[:96, :96]
    radius = np.hypot(x - 48.4, y - 44.7)
    border = np.zeros((96, 96))
    border[24:72, 24:72] = .4
    return {
        "flat_008": np.full((96, 96), .08), "flat_060": np.full((96, 96), .6),
        "linear_ramp": .05 + .8 * (x + .2 * y) / 114,
        "curved_surface": .1 + .4 * (x / 95) ** 2 + .2 * (y / 95) ** 2,
        "slanted_edge": np.where(x < .77 * y + 12, .1, .8),
        "thin_oblique_grid": np.where(((x - 2 * y) % 23 == 0) | ((2 * x + y) % 29 == 0), .8, .15),
        "periodic_texture": .4 + .1 * np.sin(x * np.pi / 2.3) * np.sin(y * np.pi / 3.7),
        "radial_chirp": .4 + .075 * np.sin(.018 * radius ** 2),
        "stochastic_clean_texture": .4 + np.random.default_rng(1771).normal(0, .03, (96, 96)),
        "weak_texture": .5 + .015 * np.sin(x * .83) * np.cos(y * .69),
        "signed_hdr_surface": -.07 + 2.1 * (x / 95) ** 2 - .04 * (y / 95) ** 2,
        "black_border": border,
    }


def records():
    for seed in (1201, 1231):
        rng = np.random.default_rng(seed)
        for scene, clean in scenes().items():
            active = np.ones(clean.shape, dtype=bool)
            if scene == "black_border":
                active[:] = False
                active[24:72, 24:72] = True
            for noise in NOISES:
                variance = np.zeros(clean.shape)
                impulse = np.zeros(clean.shape, dtype=bool)
                if noise.startswith("white_"):
                    sigma = {"white_005": .005, "white_025": .025, "white_050": .05}[noise]
                    variance[active] = sigma ** 2
                    residual = rng.normal(0, sigma, clean.shape)
                elif noise == "correlated_025":
                    variance[active] = .025 ** 2
                    residual = gaussian_filter(rng.normal(0, 1, clean.shape), .65, mode="mirror")
                    residual *= .025 / np.std(residual)
                elif noise == "signal_dependent":
                    variance[active] = np.maximum(clean[active], 0) / 2500 + .01 ** 2
                    residual = rng.normal(0, np.sqrt(variance))
                elif noise == "mixed_impulse":
                    variance[active] = .025 ** 2
                    residual = rng.normal(0, .025, clean.shape)
                    impulse = (rng.random(clean.shape) < .03) & active
                else:
                    residual = np.zeros(clean.shape)
                residual[~active] = 0
                noisy = clean + residual
                if impulse.any():
                    noisy = np.where(impulse, rng.choice([-.1, 1.1], clean.shape), noisy)
                yield dict(scene=scene, seed=seed, noise=noise, plane=noisy.astype(np.float32),
                           target_sigma=float(np.sqrt(variance[active].mean())),
                           full_plane_sigma=float(np.sqrt(variance.mean())), active_pixels=int(active.sum()),
                           impulse_pixels=int(impulse.sum()),
                           realized_total_std=float(np.std((noisy - clean)[active])),
                           haar_noise_response_std=float(np.std(haar_all(residual))))


def cpp_estimates(probe, planes, workdir, opencv_bin=None):
    if sys.byteorder != "little":
        raise RuntimeError("Probe interchange currently requires a little-endian host")
    with tempfile.TemporaryDirectory(prefix="noise-batch-", dir=workdir) as temporary:
        path = Path(temporary) / "samples.bin"
        with path.open("wb") as stream:
            stream.write(b"IPRNS001" + struct.pack("<I", len(planes)))
            for plane in planes:
                stream.write(struct.pack("<II", *plane.shape))
                stream.write(np.asarray(plane, dtype="<f4").tobytes())
        env = os.environ.copy()
        if opencv_bin:
            env["PATH"] = str(opencv_bin) + os.pathsep + env.get("PATH", "")
        result = subprocess.run([str(probe.resolve()), str(path.resolve())], capture_output=True,
                                text=True, env=env, timeout=60, check=True)
    parsed = [line.split() for line in result.stdout.splitlines()]
    if len(parsed) != len(planes) or any(int(row[0]) != i for i, row in enumerate(parsed)):
        raise RuntimeError("Incomplete C++ probe response")
    return [float(row[1]) for row in parsed]


def run(probe, workdir, opencv_bin):
    calibration = []
    for seed in range(901, 913):
        p = np.random.default_rng(seed).normal(0, 1, (96, 96)).astype(np.float32)
        value = prototypes(p)[0]["pca_tail_raw"]
        calibration.append(dict(seed=seed, true_sigma=1., raw_estimate=value))
    factor = 1 / np.mean([r["raw_estimate"] for r in calibration])
    data = list(records())
    # An additional larger field exercises the production sampling stride.
    large = np.random.default_rng(1301).normal(.3, .025, (768, 768)).astype(np.float32)
    quantized = list(quantization_controls())
    actual = cpp_estimates(probe, [r["plane"] for r in data] + [large] + [r["plane"] for r in quantized], workdir, opencv_bin)
    rows = []
    differences = []
    for case, cpp in zip(data, actual):
        estimates, support = prototypes(case["plane"])
        estimates["haar_production_cpp"] = cpp
        estimates["pca_tail_white_calibrated"] = estimates["pca_tail_raw"] * factor
        port = production_port(case["plane"])
        differences.append(abs(cpp - port))
        for method, estimate in estimates.items():
            target = case["target_sigma"]
            rows.append(dict(**{k: v for k, v in case.items() if k != "plane"}, method=method,
                             estimated_sigma=estimate, absolute_error=abs(estimate - target),
                             ratio=estimate / target if target else None, pixels=9216, support=support,
                             production_sampling=block_counts(case["plane"])))
    large_actual = actual[len(data)]
    differences.append(abs(large_actual - production_port(large)))
    controls = []
    for case, cpp in zip(quantized, actual[len(data) + 1:]):
        differences.append(abs(cpp - production_port(case["plane"])))
        controls.append(dict(**{k: v for k, v in case.items() if k != "plane"},
                             cpp_sigma=cpp, previous_python_sigma=mad(haar_all(case["plane"])),
                             production_sampling=block_counts(case["plane"])))
    return dict(pca_white_calibration=calibration, pca_white_factor=float(factor),
                cpp_port_max_absolute_difference=max(differences),
                stride_control=dict(size=[768, 768], seed=1301, true_sigma=.025,
                                    cpp=large_actual, python_port=production_port(large), stride=3),
                quantization_controls=controls, rows=rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--opencv-bin", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not args.probe.is_file():
        parser.error("Compile the isolated C++ probe first")
    now = datetime.now(timezone.utc)
    root = Path(__file__).resolve().parents[1]
    sources = [Path(__file__), root / "tests/noise_estimator_probe.cpp", root / "src/core/NoiseEstimation.cpp",
               root / "src/core/NoiseEstimation.h"]
    env = os.environ.copy()
    if args.opencv_bin:
        env["PATH"] = str(args.opencv_bin) + os.pathsep + env.get("PATH", "")
    native_versions = subprocess.check_output([str(args.probe.resolve()), "--version"], text=True, env=env, timeout=10)
    native_libraries = {}
    if args.opencv_bin:
        for path in sorted(args.opencv_bin.glob("opencv_world*.dll")):
            if not path.stem.endswith("d"):
                native_libraries[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
    result = dict(campaign_id="noise-level-v004-" + now.strftime("%Y%m%dT%H%M%SZ"), schema_version=1, data_version=4,
                  supersedes_campaign_id="noise-level-v003-20261020T151031Z",
                  revision_reason="Probe the shared Haar component; this does not validate the complete adaptive affine estimator",
                  measured_at_utc=now.isoformat(), revision=subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
                  source_sha256={str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
                  probe_sha256=hashlib.sha256(args.probe.read_bytes()).hexdigest(),
                  versions=dict(python=platform.python_version(), numpy=np.__version__, scipy=scipy.__version__,
                                native_probe=native_versions.splitlines()), opencv_library_sha256=native_libraries,
                  configuration=dict(seeds=[1201, 1231], calibration_seeds=list(range(901, 913)),
                                     scenes=list(scenes()), noises=NOISES, size=[96, 96],
                                     noise="white .005/.025/.05; correlated blur .65 scaled to .025; variance max(X,0)/2500+.01^2; 3% extra impulses",
                                     target="RMS expected diffuse standard deviation on active pixels, excluding impulse variance",
                                     border_target="black_border: inner 48x48 noisy; outer region unchanged exactly",
                                     space="linear scalar signed luminance; float32 inputs; no clipping",
                                     dct=dict(size=8, stride=4, low_frequency="1<=u+v<=3", high_frequency="u+v>=8",
                                              selected_fraction=.2, minimum_selected=32, statistic="HF MAD / .67448975"),
                                     pca=dict(size=7, stride=3, tail_eigenvalues=16, minimum_patches=64,
                                              centering="per-coordinate across patches; not per-patch mean removal"),
                                     quantization_control=dict(scene="slanted_edge", seed=1409,
                                                               sigmas=[0, 1e-7, 1e-5, 1e-4, .001, .005],
                                                               steps=[0, 1/65535, 1/255],
                                                               target_note="prequantization sigma is not postquantization Gaussian sigma"),
                                     laplacian="outer([1,-2,1],[1,-2,1])/6; valid interior; MAD and scaled mean absolute response"),
                  audit=dict(production_modified=False, cpp_helper_executed=True, photographs_measured=False,
                             denoising_compared=False, published_algorithms_fully_reproduced=False,
                             correlation_model_estimated=False, signal_dependent_curve_estimated=False),
                  **run(args.probe, args.probe.parent, args.opencv_bin))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2, allow_nan=False)
    print(f"{len(result['rows'])} estimates: {args.output}")
    print("C++ vs faithful port maximum difference:", result["cpp_port_max_absolute_difference"])
    print("PCA white calibration factor:", result["pca_white_factor"])


if __name__ == "__main__":
    main()
