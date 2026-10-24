"""Run the demosaicing study: LibRaw's Bayer algorithms and options on the same RAWs.

The native probe demosaicProbe demosaics each RAW with every variant, measures the
cost, the noise left in flat zones, the detail kept in textured zones (ratios to
AHD, the production algorithm) and the error on a re-mosaicked 2x2-binned truth.
This runner splits the scenes across processes, records the provenance (commit,
local changes, probe hash, duration) and writes one immutable, versioned JSON;
crops stay a derived folder.

Usage: python tests/study_demosaic.py --data-version N [--scene STEM ...]
           [--base tests/image/base ...] [--crops build-windows-msvc/dematricage-vNNN] [--jobs N]
           [--variants AHD,RCD,...] [--threads 8[,12,16]]
"""
import argparse
import json
import platform
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from analysis.render import available_memory, runtime_env  # noqa: E402
from analysis.native import sha256  # noqa: E402
from study_sources import git, sources  # noqa: E402

WORKER_BYTES = 2 * 1024 ** 3   # LibRaw image, processed copy, float planes and the truth


def jobs_for(requested, count):
    if requested:
        return max(1, min(requested, count))
    free = available_memory()
    by_memory = 1 if free is None else max(1, int((free - 2 * 1024 ** 3) // WORKER_BYTES))
    return max(1, min(by_memory, count, 3))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--base", type=Path, action="append", help="RAW folder (repeatable; default tests/image/base)")
    parser.add_argument("--probe", type=Path, default=ROOT / "build-windows-msvc/Release/demosaicProbe.exe")
    parser.add_argument("--crops", type=Path)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    parser.add_argument("--scene", action="append", default=[])
    parser.add_argument("--jobs", type=int, default=0, help="parallel probe processes (default: from free memory)")
    parser.add_argument("--timeout", type=int, default=7200, help="seconds per probe process")
    parser.add_argument("--variants", help="comma-separated variants (AHD, the reference, always runs first)")
    parser.add_argument("--threads", default="8",
                        help="OpenCV threads of our interpolations; a list (8,12,16) adds scaling timings")
    args = parser.parse_args(argv)
    if not args.probe.is_file():
        raise SystemExit("Probe not built: " + str(args.probe))
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-dematricage-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    crops = args.crops or ROOT / f"build-windows-msvc/dematricage-v{args.data_version:03d}"
    bases = args.base or [ROOT / "tests/image/base"]
    raws = sources(bases, args.scene)
    jobs = jobs_for(args.jobs, len(raws))
    chunks = [raws[i::jobs] for i in range(jobs)]
    start = time.monotonic()
    with tempfile.TemporaryDirectory() as tmp:
        def work(index):
            out = Path(tmp) / f"part-{index}.json"
            options = ["--threads", args.threads] + (["--variants", args.variants] if args.variants else [])
            r = subprocess.run([str(args.probe), *options, str(out), str(crops), *map(str, chunks[index])],
                               env=runtime_env(args.probe), capture_output=True, text=True, timeout=args.timeout)
            if r.returncode != 0:
                raise RuntimeError(r.stderr[-2000:])
            return json.loads(out.read_text(encoding="utf-8"))
        with ThreadPoolExecutor(jobs) as pool:
            parts = list(pool.map(work, range(jobs)))
    record = {key: value for key, value in parts[0].items() if key != "scenes"}
    record.update({
        "campaign_id": campaign,
        "runner": "tests/study_demosaic.py",
        "commit": git("rev-parse", "HEAD"),
        "local_changes": git("status", "--short").splitlines(),
        "probe": {"sha256": sha256(args.probe)},
        "jobs": jobs,
        "threads": args.threads,
        "timing_note": "dcraw_process wall time with the other probe processes running",
        "duration_seconds": round(time.monotonic() - start, 1),
        "python": platform.python_version(),
        "crops_directory": str(crops.relative_to(ROOT)) if crops.is_relative_to(ROOT) else str(crops),
        "crops_status": "derived 8-bit montages, regenerable, not tracked",
        "sources": [str(Path(b)) for b in bases],
        "scenes": sorted((s for p in parts for s in p["scenes"]), key=lambda s: s["raw"]),
    })
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{len(raws)} scènes, {jobs} processus, {record['duration_seconds']:.0f} s ; planches : {crops}")


if __name__ == "__main__":
    main()
