"""Examples for Scholar course 10; synthetic arrays only, NumPy required.

Run from repository root: python docs/scholar/references/haar-exemples.py
Conventions: rows first, columns second; 2D outputs are A, Dx, Dy, HH.
This is teaching code, not the sampled C++ production noise estimator.
"""
import numpy as np


def plane(values):
    p = np.asarray(values, dtype=np.float64)
    if p.ndim != 2 or min(p.shape) < 2 or any(n % 2 for n in p.shape):
        raise ValueError("Expected an even-sized 2D plane of at least 2x2")
    if not np.isfinite(p).all():
        raise ValueError("Expected finite values")
    return p


def haar2(values):
    p = plane(values)
    a, b, c, d = p[::2, ::2], p[::2, 1::2], p[1::2, ::2], p[1::2, 1::2]
    return ((a+b+c+d)/2, (a-b+c-d)/2, (a+b-c-d)/2, (a-b-c+d)/2)


def inverse2(coefficients):
    a, dx, dy, hh = (np.asarray(c, dtype=np.float64) for c in coefficients)
    if a.ndim != 2 or any(c.shape != a.shape for c in (dx, dy, hh)):
        raise ValueError("Expected four equally shaped 2D coefficient planes")
    out = np.empty((a.shape[0]*2, a.shape[1]*2), dtype=np.float64)
    out[::2, ::2] = (a+dx+dy+hh)/2
    out[::2, 1::2] = (a-dx+dy-hh)/2
    out[1::2, ::2] = (a+dx-dy-hh)/2
    out[1::2, 1::2] = (a-dx-dy+hh)/2
    return out


def sigma_hh(values):
    hh = haar2(values)[3]
    return float(np.median(np.abs(hh-np.median(hh)))/0.6744897501960817)


def main():
    pair = np.array([10., 14.])
    q = np.array([[1., 1.], [1., -1.]])/np.sqrt(2.)
    coefficients = q @ pair
    np.testing.assert_allclose(q.T @ coefficients, pair)
    np.testing.assert_allclose(np.sum(coefficients**2), np.sum(pair**2))
    print("Pair (10,14): A,D =", coefficients)

    p = np.array([[10, 12], [14, 18]], dtype=np.uint16)
    cs = haar2(p)  # Conversion before subtraction prevents unsigned overflow.
    np.testing.assert_allclose([c.item() for c in cs], [27, -3, -5, 1])
    np.testing.assert_allclose(inverse2(cs), p)
    np.testing.assert_allclose(sum(np.sum(c*c) for c in cs), np.sum(p.astype(float)**2))
    print("Block example: A,Dx,Dy,HH =", [c.item() for c in cs], "energy = 764")

    vertical = np.array([[0., 20.], [0., 20.]])
    np.testing.assert_allclose([c.item() for c in haar2(vertical)], [20, -20, 0, 0])
    np.testing.assert_equal(sigma_hh(np.full((64, 64), .1)), 0)
    print("Vertical border: Dx = -20, HH = 0 (not an absence of structure)")

    rng = np.random.default_rng(20261005)
    sigma = .01
    noisy = .1 + rng.normal(0., sigma, (512, 512))
    estimated = sigma_hh(noisy)
    relative_error = abs(estimated/sigma-1)
    assert relative_error < .04, relative_error
    np.testing.assert_allclose(inverse2(haar2(noisy)), noisy, atol=1e-14)
    print(f"White Gaussian: sigma={sigma:.6f}, estimated={estimated:.6f}, relative error={relative_error:.2%}")

    # Independent noise between blocks, perfectly correlated inside each block.
    common = np.repeat(np.repeat(rng.normal(0., sigma, (256, 256)), 2, axis=0), 2, axis=1)
    np.testing.assert_equal(sigma_hh(common), 0)
    print(f"Block-correlated noise: pixel std={common.std():.6f}, HH MAD estimate=0")

    # An even-positioned periodic checkerboard has equal nonzero HH coefficients.
    rows, columns = np.indices((64, 64))
    checker = np.where((rows+columns) % 2 == 0, 1., -1.)
    np.testing.assert_equal(haar2(checker)[3], np.full((32, 32), 2.))
    np.testing.assert_equal(sigma_hh(checker), 0)
    print("Checkerboard: every HH=2 but centered MAD=0")
    print("All teaching checks passed; no photographs read or modified.")


if __name__ == "__main__":
    main()
