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
       python tests/study_noise_neighbourhoods.py --data-version N --pairs-from AUDIT.json
           [--pairs-root tests/image/colorimétrie] [--jobs N]
           developed noise truth from exposure-matched pairs (step A of the protocol)
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
from analysis.render import available_memory, runtime_env  # noqa: E402
from concurrent.futures import ThreadPoolExecutor  # noqa: E402

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
    """RAWs at the root of one base folder or of several (no recursion); a scene
    present in several folders is taken once, from the first folder."""
    folders = base if isinstance(base, (list, tuple)) else [base]
    found = {}
    for folder in folders:
        for p in Path(folder).iterdir():
            if p.is_file() and p.suffix.lower() in RAW_SUFFIXES:
                found.setdefault(p.stem, p)
    raws = sorted(found.values(), key=lambda p: p.name)
    if scenes:
        wanted = set(scenes)
        raws = [p for p in raws if p.stem in wanted]
        missing = wanted - {p.stem for p in raws}
        if missing:
            raise SystemExit("RAW missing from the base root: " + ", ".join(sorted(missing)))
    if not raws:
        raise SystemExit("No RAW at the base root")
    return raws


PAIR_WORKER_BYTES = 5 * 1024 ** 3   # two 32 Mpx masters, planes and bands; ~4 GiB observed + margin


def audited_pairs(audit, root):
    """Exposure-, geometry- and level-matched pairs of the noise pair audit, not rejected."""
    record = json.loads(Path(audit).read_text(encoding="utf-8"))
    found = []
    for group in record["groups"]:
        for pair in group["pair_candidates"]:
            if (pair.get("matching_exposure_metadata") and pair.get("matching_geometry")
                    and pair.get("matching_decoder_levels") and pair.get("status") != "rejected"
                    and not pair.get("declared_dark_pair")):
                a, b = (Path(root) / pair[k].replace("\\", "/") for k in ("left", "right"))
                if a.is_file() and b.is_file():
                    found.append((group["folder"], a, b))
    if not found:
        raise SystemExit("No usable pair in " + str(audit))
    return found


def pair_jobs(requested, count):
    free = available_memory()
    by_memory = 1 if free is None else max(1, int((free - 2 * 1024 ** 3) // PAIR_WORKER_BYTES))
    return max(1, min(by_memory, count, requested if requested > 0 else 4))


def run_pairs(args, stamp):
    campaign = f"etude-bruit-paires-developpe-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    pairs = audited_pairs(args.pairs_from, args.pairs_root)
    jobs = pair_jobs(args.jobs, len(pairs))
    chunks = [pairs[i::jobs] for i in range(jobs)]
    start = time.monotonic()
    with tempfile.TemporaryDirectory() as tmp:
        def work(index):
            out = Path(tmp) / f"pairs-{index}.json"
            cmd = [str(args.probe), "--pairs", str(out)] + [str(x) for _, a, b in chunks[index] for x in (a, b)]
            r = subprocess.run(cmd, env=runtime_env(args.probe), capture_output=True, text=True, timeout=args.timeout)
            if r.returncode != 0:
                raise RuntimeError(r.stderr[-2000:])
            return json.loads(out.read_text(encoding="utf-8"))["pairs"]
        with ThreadPoolExecutor(jobs) as pool:
            results = [p for chunk in pool.map(work, range(jobs)) for p in chunk]
    folders = {(a.name, b.name): folder for folder, a, b in pairs}
    for r in results:
        r["folder"] = folders.get((r["a"], r["b"]))
    record = {
        "campaign_id": campaign,
        "protocol": "docs/validation/protocols/protocole-bruit-voisinages-adaptatifs.md",
        "step": "A - developed noise truth from exposure-matched pairs vs blind single-image estimate",
        "runner": "tests/study_noise_neighbourhoods.py",
        "pairs_source": {"audit": str(Path(args.pairs_from).name), "audit_sha256": sha256(args.pairs_from)},
        "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
        "probe": {"sha256": sha256(args.probe)}, "jobs": jobs,
        "duration_seconds": round(time.monotonic() - start, 1),
        "bands": "haar_hh: Haar HH MAD; fine: x - G1; mid: G1 - G4; coarse: G4 - G16; pixel: x - G16 (MAD sigma)",
        "pairs": results,
    }
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{len(results)} paires, {jobs} processus, {record['duration_seconds']:.0f} s")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--base", type=Path, action="append", help="RAW folder (repeatable; default tests/image/base)")
    parser.add_argument("--probe", type=Path, default=ROOT / "build-patch_noise/Release/noiseNeighbourhoodProbe.exe")
    parser.add_argument("--crops", type=Path)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    parser.add_argument("--scene", action="append", default=[])
    parser.add_argument("--tuning", action="append", default=[])
    parser.add_argument("--candidate-e-only", action="store_true", help="reference and E only; full-image timing at 8 threads")
    parser.add_argument("--impulse-study", action="store_true", help="reference, E and experimental impulse prefilter F")
    parser.add_argument("--profile-study", action="store_true", help="reference, E and experimental support/structure gates G")
    parser.add_argument("--timeout", type=int, default=3600, help="seconds per run of the probe")
    parser.add_argument("--pairs-from", type=Path, help="noise pair audit JSON (step A)")
    parser.add_argument("--pairs-root", type=Path, default=ROOT.parents[1] / "tests/image/colorimétrie"
                        if (ROOT.parents[1] / "tests/image/colorimétrie").is_dir() else ROOT / "tests/image/colorimétrie")
    parser.add_argument("--jobs", type=int, default=0, help="parallel probe processes (default: from free memory)")
    args = parser.parse_args(argv)
    if sum((args.candidate_e_only,args.impulse_study,args.profile_study))>1:
        parser.error("Select one photographic study scope")
    if args.pairs_from and (args.candidate_e_only or args.impulse_study or args.profile_study):
        parser.error("Photographic candidate flags do not apply to pair calibration")
    if not args.probe.is_file():
        raise SystemExit("Probe not built: " + str(args.probe))
    stamp = datetime.now(timezone.utc)
    if args.pairs_from:
        return run_pairs(args, stamp)
    campaign = f"etude-voisinages-bruit-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    crops = args.crops or ROOT / f"build-patch_noise/voisinages-v{args.data_version:03d}"
    raws = sources(args.base or [ROOT / "tests/image/base"], args.scene)
    source_signatures = {p: (p.stat().st_size,p.stat().st_mtime_ns) for p in raws}
    with tempfile.TemporaryDirectory() as tmp:
        probe_json = Path(tmp) / "probe.json"
        start = time.monotonic()
        scope = ["--profile-study"] if args.profile_study else ["--impulse-study"] if args.impulse_study else ["--e-only"] if args.candidate_e_only else []
        result = subprocess.run([str(args.probe), *scope, str(probe_json), str(crops), *map(str, raws)],
                                env=runtime_env(args.probe), capture_output=True, text=True, timeout=args.timeout)
        duration = time.monotonic() - start
        if result.returncode != 0:
            raise SystemExit("Probe failed: " + result.stderr[-2000:])
        record = json.loads(probe_json.read_text(encoding="utf-8"))
    for source,before in source_signatures.items():
        if (source.stat().st_size,source.stat().st_mtime_ns) != before:
            raise SystemExit("Source changed during study: " + str(source))
    record.update({
        "campaign_id": campaign,
        "schema_version": 1, "data_version": args.data_version,
        "measured_at_utc": stamp.isoformat(),
        "runner_sha256": sha256(Path(__file__)),
        "source_paths": [str(p) for p in raws],
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
        "sources": [str(Path(b)) for b in (args.base or [ROOT / "tests/image/base"])],
    })
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{len(raws)} scènes, {duration:.0f} s ; planches : {crops}")


if __name__ == "__main__":
    main()
