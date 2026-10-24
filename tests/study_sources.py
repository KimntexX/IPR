"""Shared helpers of the study runners: RAW sources at the base roots and git provenance."""
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RAW_SUFFIXES = (".cr3", ".cr2")


def git(*args):
    return subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, text=True, check=True).stdout.strip()


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
