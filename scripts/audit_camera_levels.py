"""Sensor levels of one camera's RAW files, for its normalisation rules (ADR-0011).

For every RAW of the given extension (and camera model) at the root of
tests/image/base: Adobe's own interpretation (DNG Converter on a verified copy:
WhiteLevel, BlackLevel, BaselineExposure) and what LibRaw reports to IPR after
unpacking (black, per-channel black, maximum: the values a rule is matched
against), with ISO, Canon white levels and the DCP IPR selects. The rules of
the R6 Mark III and R7 take Adobe's WhiteLevel and BaselineExposure; this tool
proposes the same for another camera, grouped by sensor mode.

Diagnostic only: sources, sidecars, profiles and the deployed catalogue are not
written. Copies and DNG stay in an ignored build directory. One immutable JSON.
Usage: python scripts/audit_camera_levels.py --extension cr2 --model "EOS 60D"
"""
import argparse
from collections import defaultdict
from datetime import datetime, timezone
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from analysis import native  # noqa: E402
from analysis.render import runtime_env  # noqa: E402
from audit_dng_baseline import audit as dng_audit  # noqa: E402

CONVERTER = Path("C:/Program Files/Adobe/Adobe DNG Converter/Adobe DNG Converter.exe")
PROBE = ROOT / "build-windows-msvc/Release/tonalBaseProbe.exe"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--extension", required=True)
    parser.add_argument("--model", required=True, help="LibRaw camera model, e.g. 'EOS 60D'")
    parser.add_argument("--converter", type=Path, default=CONVERTER)
    parser.add_argument("--probe", type=Path, default=PROBE)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args()
    base = ROOT / "tests/image/base"
    raws = sorted(p for p in base.iterdir() if p.is_file() and p.suffix.lower() == "." + args.extension.lower())
    if not raws:
        raise SystemExit("No ." + args.extension + " RAW at the root of base")
    now = datetime.now(timezone.utc)
    campaign = f"audit-niveaux-{args.model.lower().replace(' ', '')}-v001-{now.strftime('%Y%m%dT%H%M%S%fZ')}"
    folder = ROOT / "build-colorimetrie-inventory" / campaign
    copies, converted, traces = folder / "inputs-without-sidecars", folder / "dng", folder / "libraw-scalars"
    copies.mkdir(parents=True)
    converted.mkdir()
    digests = {}
    for raw in raws:
        digests[raw.name] = native.sha256(raw)
        shutil.copyfile(raw, copies / raw.name)
        if native.sha256(copies / raw.name) != digests[raw.name]:
            raise ValueError("Copy differs: " + raw.name)
    commands = []
    for raw in raws:
        command = [str(args.converter), "-c", "-p0", "-d", str(converted), str(copies / raw.name)]
        print("Converting", raw.name, flush=True)
        subprocess.run(command, check=True, timeout=300, creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        commands.append(command)
    subprocess.run([str(args.probe), "--scalars-only", str(traces)] + [str(copies / r.name) for r in raws],
                   env=runtime_env(args.probe), check=True, timeout=1800)
    manifest = native.read_json(traces / "manifest.json")
    records, modes = [], defaultdict(list)
    for raw in raws:
        trace = next(x for x in manifest["images"] if Path(x["raw"]).name == raw.name)
        if trace["camera"] != args.model:
            print("Skipped (other model):", raw.name, trace["camera"])
            continue
        dng = dng_audit(converted / (raw.stem + ".dng"))
        mosaic = [x for x in dng["sub_ifds"] if x.get("photometric") == 32803]
        if len(mosaic) != 1:
            raise ValueError("Expected one mosaic RAW SubIFD: " + raw.name)
        white, black = mosaic[0]["white_level"], mosaic[0]["black_level"]
        white = white[0] if isinstance(white, list) else white
        black = black if isinstance(black, list) else [black]
        libraw = trace["before_black_estimation"]
        totals = [libraw["black"] + c for c in libraw["cblack"]]
        record = {"file": raw.name, "sha256": digests[raw.name], "iso": trace["iso"],
                  "dcp": trace["dcp"], "dcp_sha256": native.sha256(Path(trace["dcp"])),
                  "libraw": {"black": libraw["black"], "cblack": libraw["cblack"], "black_totals": totals,
                             "maximum": libraw["maximum"], "dng_baseline_exposure": libraw["libraw_dng_baseline_exposure"],
                             "canon_specular_white": libraw["canon"]["specular_white"],
                             "canon_normal_white": libraw["canon"]["normal_white"]},
                  "adobe_dng": {"white_level": white, "black_level": black,
                                "baseline_exposure": dng["ifd0"].get("baseline_exposure"),
                                "baseline_exposure_offset": dng["ifd0"].get("baseline_exposure_offset")}}
        records.append(record)
        modes[(white, dng["ifd0"].get("baseline_exposure"))].append(record)
        print(f"{raw.name} ISO {trace['iso']:>6}: LibRaw black {totals} max {libraw['maximum']}"
              f" | Adobe white {white} black {black} baseline {dng['ifd0'].get('baseline_exposure')}", flush=True)
    print("\nProposed modes (Adobe white, baseline):")
    for (white, baseline), group in sorted(modes.items(), key=lambda kv: str(kv[0])):
        blacks = [t for r in group for t in r["libraw"]["black_totals"]]
        maxima = [r["libraw"]["maximum"] for r in group]
        isos = sorted({r["iso"] for r in group})
        print(f"  white {white}, baseline {baseline}: {len(group)} files, ISO {isos}, LibRaw black {min(blacks)}-{max(blacks)},"
              f" maximum {min(maxima)}-{max(maxima)}, DCP {sorted({r['dcp_sha256'][:12] for r in group})}")
    result = {"campaign_id": campaign, "schema_version": 1, "measured_at_utc": now.isoformat(),
              "group": "raw", "camera_model": args.model, "format_extension": args.extension.lower(),
              "purpose": "Sensor levels for RAW normalisation rules (ADR-0011)",
              "libraw_version": manifest["libraw_version"], "converter_commands": commands,
              "method": {"adobe": "Adobe DNG Converter -c -p0 on a verified copy: mosaic SubIFD WhiteLevel/BlackLevel, "
                                  "IFD0 BaselineExposure (Adobe interpretation, not a physical calibration)",
                         "libraw": "tonalBaseProbe --scalars-only, values after unpack (rule matching inputs)"},
              "software": {"probe_sha256": native.sha256(args.probe), "converter_sha256": native.sha256(args.converter),
                           "script_sha256": native.sha256(Path(__file__))},
              "artifacts": str(folder.relative_to(ROOT)), "records": records}
    destination = args.output_dir / (campaign + ".json")
    native.write_immutable(destination, result)
    print(destination)


if __name__ == "__main__":
    main()
