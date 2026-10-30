"""Bench-only nonlinear-mean and naive VST controls; no production implementation."""
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import random
import subprocess


def positive_gaussian_mean(mean, sigma):
    if sigma <= 0:
        raise ValueError('positive sigma required')
    z = mean / sigma
    return sigma * math.exp(-z*z/2) / math.sqrt(2*math.pi) + mean * (1+math.erf(z/math.sqrt(2))) / 2


def encode(value):
    value = max(0., value)
    return 12.92*value if value <= .0031308 else 1.055*value**(1/2.4)-.055


def naive_vst(value, a, b):
    # Integral of 1/sqrt(a*x+b), with the generalized Anscombe 3*a*a/8 term.
    # This intentionally tests the unsafe root clamp and algebraic inverse,
    # not Makitalo/Foi's exact unbiased inverse. Not a recommended denoiser.
    return 2/a * math.sqrt(max(a*value + 3*a*a/8 + b, 0.))


def algebraic_inverse(value, a, b):
    return a*value*value/4 - 3*a/8 - b/a


def main():
    root = Path(__file__).resolve().parents[2]
    random_source = random.Random(20261030)
    half = [random_source.gauss(0, 1) for _ in range(32768)]
    z = [v for sample in half for v in (sample, -sample)]
    records = []
    for mean, sigma in ((0., .01), (.001, .01), (.02, .01)):
        for factor in (1., .5, .2):
            # Oracle shrink around the KNOWN truth, not an image denoiser.
            values = [mean+sigma*factor*v for v in z]
            average = math.fsum(values)/len(values)
            assert abs(average-mean) < 1e-12
            records.append(dict(truth=mean, sigma=sigma, oracle_residual_factor=factor,
                signed_mean=average,
                clipped_mean=math.fsum(max(v, 0.) for v in values)/len(values),
                clipped_gaussian_expected=positive_gaussian_mean(mean, sigma*factor),
                srgb_encoded_mean_255=255*math.fsum(encode(v) for v in values)/len(values)))
    assert records[2]['clipped_mean'] < records[0]['clipped_mean']
    assert records[8]['srgb_encoded_mean_255'] > records[6]['srgb_encoded_mean_255']
    a, b = .001, .00001
    values = [.01*v for v in z]
    reconstructed = [algebraic_inverse(naive_vst(v, a, b), a, b) for v in values]
    threshold = -(3*a*a/8+b)/a
    vst = dict(a=a, b=b, input_mean=math.fsum(values)/len(values),
        output_mean_without_any_denoising=math.fsum(reconstructed)/len(values),
        root_clamp_threshold=threshold,
        root_clamped_fraction=sum(v < threshold for v in values)/len(values),
        max_round_trip_error=max(abs(x-y) for x, y in zip(values, reconstructed)))
    assert vst['root_clamped_fraction'] > 0
    assert vst['output_mean_without_any_denoising'] > 0
    for x in (0., .01, 1.):
        assert abs(algebraic_inverse(naive_vst(x, a, b), a, b)-x) < 1e-12
    path = Path(__file__)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    proof = dict(schema_version=1, created_utc=stamp, study='nonlinear_mean_and_naive_vst_controls',
        revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        source=dict(path=str(path.relative_to(root)), sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                    text=path.read_text(encoding='utf-8')),
        samples=len(z), seed=20261030, Gaussian_controls=records, naive_vst_control=vst,
        verdict='synthetic_mechanisms_only_not_actual_DCP_causality',
        limits=['Antithetic scalar Gaussian samples, no real CFA, spatial or channel covariance.',
                'Oracle shrink knows the true mean; it is not a denoising algorithm.',
                'sRGB encoding alone, not the full DCP, no upper clipping or 8-bit rounding.',
                'Naive algebraic VST round-trip is a negative control, not an unbiased inverse.',
                'No proof of real-scene improvement or patent clearance.'])
    output = root/'docs/validation/raw'/f'noise-domain-controls-v001-{stamp}.json'
    with output.open('x', encoding='utf-8') as handle:
        json.dump(proof, handle, ensure_ascii=False, indent=2, allow_nan=False)
    print(output)
    for row in records:
        print(row['truth'], row['oracle_residual_factor'], row['signed_mean'],
              row['clipped_mean'], row['srgb_encoded_mean_255'])
    print(json.dumps(vst))


if __name__ == '__main__':
    main()
