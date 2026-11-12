"""Adapter to the existing raw_bench capture reader, without a RAW decoder."""
import numpy as np
from raw_bench.capture import load_capture
from .contracts import field


def load_field(path, *, stage, origin_yx, domain, expected_unit):
    """Read one hash-checked current capture stage into CHW.

    CFA order is positional (00,01,10,11), NOT inferred canonical colours.
    Pattern and crop origin remain explicit; odd crop extents are rejected.
    The capture's unit must match the caller's expectation exactly.
    """
    manifest, stages = load_capture(path)
    descriptor, pixels = stages[stage, tuple(origin_yx)]
    if (descriptor['unit'] != expected_unit or not expected_unit
            or descriptor['orientation'] != 'sensor'):
        raise ValueError('Expected units and sensor orientation required')
    if domain == 'cfa':
        if (descriptor['layout'] != 'cfa' or pixels.shape[2] != 1
                or any(s % 2 for s in pixels.shape[:2])):
            raise ValueError('Even single-channel CFA extent required')
        pattern = np.asarray(descriptor['context'].get('cfa'))
        if pattern.shape != (2, 2):
            raise ValueError('Explicit visible-origin Bayer pattern required')
        packed = np.stack([pixels[y::2, x::2, 0] for y, x in ((0, 0), (0, 1), (1, 0), (1, 1))])
        oy, ox = origin_yx
        colours = [pattern[(oy+y) % 2, (ox+x) % 2].item()
                   for y, x in ((0, 0), (0, 1), (1, 0), (1, 1))]
    else:
        if descriptor['layout'] != 'rgb' or pixels.shape[2] != 3:
            raise ValueError('Three rendered RGB channels required')
        packed, colours = pixels.transpose(2, 0, 1), None
    return field(packed, domain), dict(
        domain=domain, unit=expected_unit, stage=stage, origin_yx=list(origin_yx),
        source=manifest['source'], context=descriptor['context'],
        plane_colours=colours, spatial_step_sensor_pixels=2 if domain == 'cfa' else 1)
