"""Exact parity of two render servers on every settings family (B0.4); never writes images."""
import argparse
import hashlib
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path

STATES = (
    ("neutral", 0), ("exposure", 1), ("exposure", -1), ("contrast", 50), ("contrast", -50),
    ("highlights", -50), ("shadows", 50), ("whites", 50), ("blacks", -50),
    ("temperature", 3500), ("tint", 25), ("vibrance", 50), ("saturation", -50), ("grayscale", 1),
    ("texture", 50), ("clarity", -50), ("dehaze", 50), ("dehaze", -25), ("grain", 50), ("blur", 5),
    ("mixer-orange-saturation", 50), ("grading-shadows-saturation", 50), ("curve-luminance", 50),
    ("denoise-luminance", 50), ("denoise-chrominance", 50), ("sharpening-amount", 100),
    ("lens-correction", 0), ("chromatic-aberration", 0), ("auto-wb", 1),
)
# Signed development: the camera denoise and the colour denoise after the tones.
SIGNED = ((0, 0), (50, 0), (100, 0), (100, 100))
RAW_SUFFIXES = {".cr2", ".cr3", ".dng"}


def sha256(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def requests(source, states=STATES):
    lines = [json.dumps(dict(op="render", source=str(source), feature=feature, value=value))
             for feature, value in states]
    # The other path of the opening: the signed development and its denoise.
    lines += [json.dumps(dict(op="render", source=str(source), feature="neutral", value=0,
                              cameraDenoise=dict(luminance=luminance, chrominance=chrominance)))
              for luminance, chrominance in SIGNED]
    return "\n".join(lines) + '\n{"op":"quit"}\n'


def digests(output, count):
    """One digest per render of a server's stdout: a JSON header line, then its pixels."""
    results = []
    for _ in range(count):
        line, separator, output = output.partition(b"\n")
        header = json.loads(line)
        if not separator or not header.get("ok"):
            raise RuntimeError(f"Render failed: {header}")
        size = header["bytes"]
        if len(output) < size:
            raise RuntimeError("Incomplete pixel payload")
        pixels, output = output[:size], output[size:]
        results.append(dict(width=header["width"], height=header["height"], bytes=size,
                            pixel_sha256=hashlib.sha256(pixels).hexdigest()))
    return results


def render(executable, source, cwd, states=STATES):
    # One bounded process per source: no accumulated masters between photos.
    result = subprocess.run([str(executable)], input=requests(source, states).encode(), capture_output=True,
                            cwd=cwd, timeout=1800, check=True)
    return digests(result.stdout, len(states) + len(SIGNED))


def compare(baseline, candidate, sources, cwd, record, states=STATES):
    """Records one line per state; returns the number of differences."""
    names = ([f"{feature}={value}" for feature, value in states] +
             [f"neutral=0,signed,luminance={luminance},chrominance={chrominance}"
              for luminance, chrominance in SIGNED])
    different = 0
    for source in sources:
        before_hash = sha256(source)
        before = render(baseline, source, cwd, states)
        after = render(candidate, source, cwd, states)
        if sha256(source) != before_hash:
            raise RuntimeError(f"Source changed during comparison: {source.name}")
        for name, a, b in zip(names, before, after):
            identical = a == b
            different += not identical
            record(dict(source=str(source), source_sha256=before_hash, state=name, identical=identical,
                        baseline=a, candidate=b))
        print(source.name, "done", flush=True)
    return different


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--baseline-revision", required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--cwd", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--signed-only", action="store_true", help="Compare only the signed development states")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.output.exists():
        raise FileExistsError("Refusing to overwrite evidence")
    sources = sorted(p.resolve() for p in args.base.iterdir() if p.is_file() and p.suffix.lower() in RAW_SUFFIXES)
    if not sources:
        raise ValueError("No RAW files at base root")
    entries_before = sorted(p.name for p in args.base.iterdir())
    proof = dict(
        schema_version=1, data_version=1, campaign_id=args.output.stem,
        measured_at_utc=datetime.now(timezone.utc).isoformat(), group="b0-foundation", status="running",
        baseline_revision=args.baseline_revision, baseline_executable_sha256=sha256(args.baseline),
        candidate_executable_sha256=sha256(args.candidate), tool_sha256=sha256(__file__),
        candidate_revision=subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
        candidate_dirty=bool(subprocess.check_output(["git", "status", "--porcelain"], text=True).strip()),
        units=dict(pixels="little-endian uint16 sRGB codes, RGB row-major", comparison="exact bytes, no tolerance"),
        protocol_version=1, analyser_version=1, cwd=str(args.cwd.resolve()),
        method=("Full-frame in-memory renders of the signed development and its denoise"
                if args.signed_only else "Full-frame in-memory renders of every settings family at one or two values, "
                "standard and signed development with their denoise") +
               "; source digests before and after; folder listing before and after",
        limitations=["Compares the render servers, not the application's preview path",
                     "One or two values per family, not every slider value"],
        records=[],
    )
    try:
        proof["different"] = compare(args.baseline.resolve(), args.candidate.resolve(), sources, args.cwd,
                                     proof["records"].append, () if args.signed_only else STATES)
        proof["identical"] = len(proof["records"]) - proof["different"]
        proof["folder_unchanged"] = sorted(p.name for p in args.base.iterdir()) == entries_before
        proof["status"] = "passed" if proof["different"] == 0 and proof["folder_unchanged"] else "failed"
    except BaseException as error:
        proof["status"] = "failed"
        proof["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        proof["finished_at_utc"] = datetime.now(timezone.utc).isoformat()
        with args.output.open("x", encoding="utf-8") as stream:
            json.dump(proof, stream, ensure_ascii=False, indent=2, allow_nan=False)
            stream.write("\n")
    print(f"identical={proof.get('identical')} different={proof.get('different')} status={proof['status']}")


if __name__ == "__main__":
    main()
