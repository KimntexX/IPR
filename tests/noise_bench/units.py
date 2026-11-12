"""DN noise-law conversions extracted from audit_noise_raw_domain/coverage.

variance_DN2 = slope_DN * (raw_DN - black_DN) + intercept_DN2.
The normalization divisor is explicit, not inferred from white or black.
"""
import numpy as np


def convert_noise_law(slope_dn, intercept_dn2, *, black_delta_dn, divisor_dn,
                      black_origin):
    """Subtracting B+delta gives s'=s-delta, b'=b+a*delta.

    A fitted intercept can be negative; no clipping or physical fit is implied.
    """
    values = np.asarray([slope_dn, intercept_dn2, black_delta_dn, divisor_dn], dtype=float)
    if not np.isfinite(values).all() or divisor_dn <= 0 or slope_dn < 0:
        raise ValueError('Finite law, nonnegative slope and positive divisor required')
    if not isinstance(black_origin, str) or not black_origin.strip():
        raise ValueError('Documented black origin required')
    shifted = intercept_dn2 + slope_dn * black_delta_dn
    return dict(slope_normalized=float(slope_dn / divisor_dn),
                intercept_normalized=float(shifted / divisor_dn**2),
                shifted_intercept_dn2=float(shifted), black_origin=black_origin,
                divisor_dn=float(divisor_dn), black_delta_dn=float(black_delta_dn))


def coverage(rows, *, iso_values, signal_edges_dn):
    """Count all frozen ISO/signal bins, including empty ones; no sample weighting.

    Rows carry iso, signal_dn, capture_id. Bins are [left,right), last right
    inclusive. Measurements and distinct captures are separate denominators.
    Out-of-range rows are reported, never silently dropped.
    """
    isos = list(iso_values)
    edges = np.asarray(signal_edges_dn, dtype=float)
    if (not isos or len(set(isos)) != len(isos)
            or any(isinstance(i, bool) or not isinstance(i, (int, np.integer)) or i <= 0 for i in isos)
            or edges.ndim != 1 or len(edges) < 2 or not np.isfinite(edges).all()
            or np.any(np.diff(edges) <= 0)):
        raise ValueError('Distinct positive ISO values and increasing finite edges required')
    counts = {(i, b): [] for i in isos for b in range(len(edges)-1)}
    outside = 0
    for row in rows:
        signal, identity, iso = row['signal_dn'], row['capture_id'], row['iso']
        if not np.isfinite(signal) or not isinstance(identity, str) or not identity:
            raise ValueError('Finite DN signal and capture identity required')
        if iso not in isos or signal < edges[0] or signal > edges[-1]:
            outside += 1
            continue
        b = min(int(np.searchsorted(edges, signal, side='right')-1), len(edges)-2)
        counts[iso, b].append(identity)
    return dict(outside_measurements=outside, bins=[
        dict(iso=int(i), signal_interval_dn=edges[b:b+2].tolist(),
             measurements=len(ids), distinct_captures=len(set(ids)))
        for (i, b), ids in counts.items()])
