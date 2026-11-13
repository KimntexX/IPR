"""The single-RAW estimator against a qualified camera law, on ordinary photos of that camera.

Usage: python tests/neural/estimator_against_qualified_law.py <qualification.json> <output.json> <raw>...

The qualification is the evidence of tests/qualify_camera_noise.py; photos of other cameras
are skipped. At an ISO the qualification did not measure, the law is interpolated in log ISO
between its neighbours (marked "interpolated"); beyond them, the photo is skipped. Ratios are
estimated over qualified, per CFA position. Needs rawpy and Pillow (not a unit test).
"""
import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from estimate_single_raw_law import estimate_law  # noqa: E402

FULL_STOPS = {100, 200, 400, 800, 1600, 3200, 6400, 12800}


def exif(path):
    with Image.open(path) as im:
        base = im.getexif()
        iso = base.get_ifd(0x8769).get(0x8827)
        return str(base.get(0x0110) or ''), int(iso[0] if isinstance(iso, tuple) else iso)


def law_at(qualified, iso):
    """Per position (a, b): measured ISO, else log-ISO interpolation between measured ones."""
    isos = sorted(int(k) for k in qualified['isos'])
    def at(i):
        return [(p['fit_dark_read']['shot_slope_dn'], p['fit_dark_read']['read_variance_dn2'])
                for p in qualified['isos'][str(i)]['positions']]
    if iso in isos:
        return at(iso), 'measured'
    if iso < isos[0] or iso > isos[-1]:
        return None, 'outside'
    lo = max(i for i in isos if i < iso); hi = min(i for i in isos if i > iso)
    t = (math.log(iso) - math.log(lo)) / (math.log(hi) - math.log(lo))
    return [(math.exp((1 - t) * math.log(a0) + t * math.log(a1)), math.exp((1 - t) * math.log(b0) + t * math.log(b1)))
            for (a0, b0), (a1, b1) in zip(at(lo), at(hi))], 'interpolated'


def main(qualified_path, out, *photos):
    if Path(out).exists():
        raise SystemExit(f'{out} exists: a new version gets a new name')
    qualified = json.load(open(qualified_path, encoding='utf-8'))
    rows = []
    for path in photos:
        model, iso = exif(path)
        if model != qualified['model'] and not qualified['model'].endswith(model) and not model.endswith(qualified['model']):
            continue
        law, how = law_at(qualified, iso)
        if law is None:
            continue
        est = estimate_law(path)
        ra = [e[0] / c[0] for e, c in zip(est, law)]
        rb = [e[1] / c[1] for e, c in zip(est, law)]
        rows.append(dict(file=Path(path).name, iso=iso, full_stop=iso in FULL_STOPS, law=how, estimated=est,
                         qualified=law, a_ratio=ra, b_ratio=rb))
        print(Path(path).name, iso, how, 'a/a0', [round(x, 2) for x in ra], 'b/b0', [round(x, 2) for x in rb], flush=True)
    def summary(sel):
        a = [x for r in sel for x in r['a_ratio']]; b = [x for r in sel for x in r['b_ratio']]
        if not a:
            return None
        return dict(photos=len(sel), a_median=float(np.median(a)), a_p10=float(np.quantile(a, .1)), a_p90=float(np.quantile(a, .9)),
                    a_min=float(min(a)), b_median=float(np.median(b)), b_p10=float(np.quantile(b, .1)), b_p90=float(np.quantile(b, .9)))
    result = dict(schema_version=1, status='exploratory_estimator_against_qualified_law', camera=qualified['model'],
                  qualified_law=Path(qualified_path).name,
                  summary=dict(all=summary(rows), full_stops=summary([r for r in rows if r['full_stop']]),
                               intermediate=summary([r for r in rows if not r['full_stop']])), rows=rows)
    Path(out).write_text(json.dumps(result, indent=1), encoding='utf-8')
    print(json.dumps(result['summary'], indent=1))


if __name__ == '__main__':
    main(*sys.argv[1:])
