"""Compare complete in-memory uint16 renders; never writes image exports."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess


def sha256(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def render(executable, source, signed, cwd):
    request = dict(op="render", source=str(source), feature="neutral", value=0)
    if signed:
        request["cameraDenoise"] = dict(luminance=0, chrominance=0)
    # One bounded process per state: no accumulated masters, no image files.
    result = subprocess.run(
        [str(executable)], input=(json.dumps(request) + '\n{"op":"quit"}\n').encode(),
        capture_output=True, cwd=cwd, timeout=240, check=True,
    )
    line, separator, pixels = result.stdout.partition(b"\n")
    header = json.loads(line)
    if not separator or not header.get("ok"):
        raise RuntimeError(f"Render failed: {header}")
    expected = header["width"] * header["height"] * 3 * 2
    if header["dtype"] != "<u2" or header["bytes"] != expected or len(pixels) != expected:
        raise RuntimeError("Incomplete or unsupported pixel payload")
    declaration = header["declaration"]
    return dict(
        width=header["width"], height=header["height"], bytes=len(pixels),
        pixel_sha256=hashlib.sha256(pixels).hexdigest(),
        icc_sha256=hashlib.sha256(header["icc_base64"].encode()).hexdigest(),
        declaration={key: declaration[key] for key in (
            "source_sha256", "settings", "dcp_profile", "lcp_profile", "lcp_sha256",
            "raw_normalization", "lens_geometry_applied", "lens_vignetting_applied",
            "chromatic_aberration_applied",
        )},
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--cwd", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.output.exists():
        raise FileExistsError("Refusing to overwrite evidence")
    sources = sorted(p.resolve() for p in args.base.iterdir()
                     if p.is_file() and p.suffix.lower() in {".cr2", ".cr3", ".dng"})
    if not sources:
        raise ValueError("No RAW files at base root")
    proof = dict(
        schema_version=1, data_version=1,
        campaign_id=args.output.stem, measured_at_utc=datetime.now(timezone.utc).isoformat(),
        group="code-audit", status="running", baseline_revision="28c01d9",
        baseline_executable_sha256=sha256(args.baseline),
        candidate_executable_sha256=sha256(args.candidate), tool_sha256=sha256(__file__),
        candidate_revision=subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
        candidate_dirty=bool(subprocess.check_output(["git", "status", "--porcelain"], text=True).strip()),
        units=dict(pixels="little-endian uint16 sRGB codes, RGB row-major", comparison="exact bytes, no tolerance"),
        protocol_version=1, analyser_version=1,
        cwd=str(args.cwd.resolve()), records=[],
        method="Exact full-frame little-endian sRGB uint16 payload + ICC and recipe equality; A and signed B at zero",
        limitations=["Not a comparison of intermediate float32 master buffers",
                     "No injected LibRaw thumbnail allocation failure; unit tests cover synthetic invalid inputs",
                     "Calibration and Camera Neutral selection follow the shared production recipes"],
    )
    try:
        for source in sources:
            before_hash = sha256(source)
            for signed in (False, True):
                before = render(args.baseline.resolve(), source, signed, args.cwd)
                after = render(args.candidate.resolve(), source, signed, args.cwd)
                if sha256(source) != before_hash:
                    raise RuntimeError(f"Source changed during comparison: {source.name}")
                identical = before == after
                proof["records"].append(dict(
                    source=str(source), source_sha256=before_hash, signed=signed,
                    identical=identical, baseline=before, candidate=after,
                ))
                print(source.name, "B" if signed else "A", "identical" if identical else "DIFFERENT", flush=True)
                if not identical:
                    raise RuntimeError(f"Parity failed: {source.name}, signed={signed}")
        proof["status"] = "passed"
    except BaseException as error:
        proof["status"] = "failed"
        proof["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        proof["finished_at_utc"] = datetime.now(timezone.utc).isoformat()
        with args.output.open("x", encoding="utf-8") as stream:
            json.dump(proof, stream, ensure_ascii=False, indent=2, allow_nan=False)
            stream.write("\n")


if __name__ == "__main__":
    main()
