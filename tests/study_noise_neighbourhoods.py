"""Run the adaptive-neighbourhood denoising study (protocole-bruit-voisinages-adaptatifs).

The native probe noiseNeighbourhoodProbe renders each RAW of the base root in
memory (neutral linear Rec.2020 master before the denoiser), applies the
production reference and the experimental candidates A-D to the same zones and
measures them. This runner records the provenance the protocol asks for
(commit, local changes, probe hash, machine, duration) around the probe output
and writes one immutable, versioned JSON; crops stay a derived folder.

Usage: python tests/study_noise_neighbourhoods.py --data-version N [--scene STEM ...]
           [--base tests/image/base] [--probe build-patch_noise/Release/noiseNeighbourhoodProbe.exe]
           [--crops build-patch_noise/voisinages-vNNN] [--output-dir docs/validation/raw]
           [--tuning STEM ...]   scenes used to choose the parameters (the others verify them)
"""
import argparse
import hashlib
import json
import platform
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from analysis.render import runtime_env  # noqa: E402

RAW_SUFFIXES = (".cr3", ".cr2")


def git(*args):
    return subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, text=True, check=True).stdout.strip()


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def sources(base, scenes):
    raws = sorted(p for p in Path(base).iterdir() if p.is_file() and p.suffix.lower() in RAW_SUFFIXES)
    if scenes:
        wanted = set(scenes)
        raws = [p for p in raws if p.stem in wanted]
        missing = wanted - {p.stem for p in raws}
        if missing:
            raise SystemExit("RAW missing from the base root: " + ", ".join(sorted(missing)))
    if not raws:
        raise SystemExit("No RAW at the base root")
    return raws


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--probe", type=Path, default=ROOT / "build-patch_noise/Release/noiseNeighbourhoodProbe.exe")
    parser.add_argument("--crops", type=Path)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    parser.add_argument("--scene", action="append", default=[])
    parser.add_argument("--tuning", action="append", default=[])
    parser.add_argument("--timeout", type=int, default=3600, help="seconds per run of the probe")
    args = parser.parse_args(argv)
    if not args.probe.is_file():
        raise SystemExit("Probe not built: " + str(args.probe))
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-voisinages-bruit-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    crops = args.crops or ROOT / f"build-patch_noise/voisinages-v{args.data_version:03d}"
    raws = sources(args.base, args.scene)
    with tempfile.TemporaryDirectory() as tmp:
        probe_json = Path(tmp) / "probe.json"
        start = time.monotonic()
        result = subprocess.run([str(args.probe), str(probe_json), str(crops), *map(str, raws)],
                                env=runtime_env(args.probe), capture_output=True, text=True, timeout=args.timeout)
        duration = time.monotonic() - start
        if result.returncode != 0:
            raise SystemExit("Probe failed: " + result.stderr[-2000:])
        record = json.loads(probe_json.read_text(encoding="utf-8"))
    record.update({
        "campaign_id": campaign,
        "protocol": "docs/validation/protocols/protocole-bruit-voisinages-adaptatifs.md",
        "runner": "tests/study_noise_neighbourhoods.py",
        "commit": git("rev-parse", "HEAD"),
        "local_changes": git("status", "--short").splitlines(),
        "probe": {"path": str(args.probe.relative_to(ROOT)) if args.probe.is_relative_to(ROOT) else str(args.probe),
                  "sha256": sha256(args.probe)},
        "duration_seconds": round(duration, 1),
        "python": platform.python_version(),
        "tuning_scenes": sorted(args.tuning),
        "verification_scenes": sorted(p.stem for p in raws if p.stem not in set(args.tuning)),
        "crops_directory": str(crops.relative_to(ROOT)) if crops.is_relative_to(ROOT) else str(crops),
        "crops_status": "derived 8-bit sRGB montages, regenerable, not tracked",
    })
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{len(raws)} scènes, {duration:.0f} s ; planches : {crops}")


if __name__ == "__main__":
    main()
