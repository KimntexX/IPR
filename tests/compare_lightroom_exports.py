"""IPR rendered in memory against Lightroom TIFF exports (sRGB or ProPhoto) of the same RAWs (§56 of the colour analysis).
Tones (L* percentiles, linear Y offset), chroma and local contrast at 3, 9 and 33 px.
Usage: python tests/compare_lightroom_exports.py <ipr worktree> <out.json> <raw>=<tif>[:nolens] ...
Environment: AMOUNT (IPR sharpening, default 0), CHROMA (IPR colour denoise, default 0)."""
import io
import json
import os
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageCms

root = Path(sys.argv[1])
sys.path.insert(0, str(root))
from tests.analysis.render import RenderServer  # noqa: E402

SRGB_TO_XYZ = np.array([[0.4124564, 0.3575761, 0.1804375], [0.2126729, 0.7151522, 0.0721750],
                        [0.0193339, 0.1191920, 0.9503041]])
BRADFORD_D65_D50 = np.array([[1.0478112, 0.0228866, -0.0501270], [0.0295424, 0.9904844, -0.0170491],
                             [-0.0092345, 0.0150436, 0.7521316]])
SRGB_TO_XYZ50 = BRADFORD_D65_D50 @ SRGB_TO_XYZ
PROPHOTO_TO_XYZ = np.array([[0.7976749, 0.1351917, 0.0313534], [0.2880402, 0.7118741, 0.0000857],
                            [0.0, 0.0, 0.8252100]])
WHITE = np.array([0.9642, 1.0, 0.8249])


def srgb_linear(e):
    return np.where(e <= .04045, e / 12.92, ((e + .055) / 1.055) ** 2.4)


def prophoto_linear(e):
    return np.where(e < 16 / 512, e / 16, e ** 1.8)


def read_tiff(path):
    im = Image.open(path)
    icc = ImageCms.getProfileDescription(ImageCms.ImageCmsProfile(io.BytesIO(im.info["icc_profile"]))).strip()
    w, h = im.size
    a = np.memmap(path, mode="r", dtype="<u2", offset=im.tag_v2[273][0], shape=(h, w, 3))
    e = np.asarray(a, np.float32) / 65535
    if "prophoto" in icc.lower():
        return (prophoto_linear(e) @ PROPHOTO_TO_XYZ.T).astype(np.float32)
    if "srgb" in icc.lower():
        return (srgb_linear(e) @ SRGB_TO_XYZ50.T).astype(np.float32)
    raise ValueError(icc)


def lab(xyz):
    t = xyz / WHITE
    f = np.where(t > (6 / 29) ** 3, np.cbrt(np.maximum(t, 0)), t / (3 * (6 / 29) ** 2) + 4 / 29)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def box(p, k):
    c = np.cumsum(np.cumsum(np.pad(p.astype(np.float64), ((1, 0), (1, 0))), 0), 1)
    return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)


def detail(L, k):
    b = box(L, k)
    o = k // 2
    return float(np.sqrt(np.mean((L[o:o + b.shape[0], o:o + b.shape[1]] - b) ** 2)))


def crop_to(var, ref, reach=12):
    """Crop var to ref's size at the best integer offset around the centre (log-luminance correlation)."""
    rh, rw = ref.shape[:2]
    vh, vw = var.shape[:2]
    oy, ox = (vh - rh) // 2, (vw - rw) // 2
    core = (slice(rh // 4, 3 * rh // 4, 4), slice(rw // 4, 3 * rw // 4, 4))
    r = np.log(ref[..., 1][core] + 1e-4)
    r = (r - r.mean()) / r.std()
    best = None
    for dy in range(-reach, reach + 1):
        for dx in range(-reach, reach + 1):
            y0, x0 = oy + dy, ox + dx
            if y0 < 0 or x0 < 0 or y0 + rh > vh or x0 + rw > vw:
                continue
            v = np.log(var[y0:y0 + rh, x0:x0 + rw, 1][core] + 1e-4)
            v = (v - v.mean()) / v.std()
            s = float((r * v).mean())
            if best is None or s > best[0]:
                best = (s, y0, x0)
    s, y0, x0 = best
    return var[y0:y0 + rh, x0:x0 + rw], {"corr": round(s, 4), "y0": y0, "x0": x0}


def summary(xyz):
    h, w = xyz.shape[:2]
    sub = xyz[64:h - 64, 64:w - 64]
    L = lab(sub)
    q = [1, 5, 25, 50, 75, 95, 99]
    Y = sub[..., 1]
    return {"L": dict(zip(map(str, q), np.percentile(L[..., 0], q).round(2).tolist())),
            "Y": dict(zip(map(str, q), np.percentile(Y, q).round(5).tolist())),
            "chroma": round(float(np.hypot(L[..., 1], L[..., 2]).mean()), 3),
            "detail": {k: round(detail(L[..., 0], k), 3) for k in (3, 9, 33)}}


amount = int(os.environ.get("AMOUNT", "0"))
chroma = int(os.environ.get("CHROMA", "0"))
out = Path(sys.argv[2])
results = {"amount": amount, "chroma": chroma, "black_env": os.environ.get("IPR_EXPERIMENT_BLACK_RENDER"),
           "server": os.environ.get("IPR_RENDER_SERVER_BIN"), "photos": {}}
with RenderServer(timeout=900) as server:
    for spec in sys.argv[3:]:
        lens = not spec.endswith(":nolens")
        raw, tif = spec.removesuffix(":nolens").split("=")
        lr = read_tiff(tif)
        pixels, meta = server.render(raw, "lens-correction", 1 if lens else 0,
                                     denoise={"luminance": 0, "chrominance": chroma},
                                     sharpening={"amount": amount, "radius": 1.0, "masking": 0})
        ipr = (srgb_linear(np.asarray(pixels, np.float32) / 65535) @ SRGB_TO_XYZ50.T).astype(np.float32)
        ipr, where = crop_to(ipr, lr)
        a, b = summary(ipr), summary(lr)
        dY = {k: round(a["Y"][k] - b["Y"][k], 5) for k in a["Y"]}
        dL = {k: round(a["L"][k] - b["L"][k], 2) for k in a["L"]}
        results["photos"][Path(raw).name] = {"lens": lens, "align": where, "ipr": a, "lightroom": b,
                                             "dL": dL, "dY": dY, "dcp": meta["declaration"].get("dcp_profile")}
        print(Path(raw).name, "dL", dL, "| dY", dY, "| chroma", a["chroma"], b["chroma"],
              "| detail", a["detail"], b["detail"], "| corr", where["corr"], flush=True)
out.write_text(json.dumps(results, indent=1), encoding="utf-8")
