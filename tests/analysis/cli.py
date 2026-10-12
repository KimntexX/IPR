"""Protocol-grouped campaigns from current TIFF metadata or explicit mappings."""
import argparse
from pathlib import Path
import sys

from .campaign import analyse
from .catalog import GROUPS, ALIASES, group_for
from .inputs import base_records, mapping_records
from .native import write_immutable, local_path, signature, sha256, verify_unchanged


def run_group(group, argv=None):
    arguments = list(sys.argv[1:] if argv is None else argv)
    if any(a == "--group" or a.startswith("--group=") for a in arguments):
        print("Group fixed by entry point; use analyse_groupes.py to choose another", file=sys.stderr)
        return 1
    return main(["--group", group, *arguments])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--group", choices=[*GROUPS, *ALIASES, "all"], default="all")
    parser.add_argument("--feature", action="append", default=[], help="Restrict mapped controls, repeatable")
    parser.add_argument("--scene", action="append", default=[], help="Restrict mapped source stems/scene IDs, repeatable")
    parser.add_argument("--base", type=Path, help="Existing shared base layout; states audited from TIFF metadata")
    parser.add_argument("--mapping", action="append", default=[], help="Explicit IPR/LR file/reference/state JSON, repeatable")
    parser.add_argument("--output", type=Path, help="Exact immutable output, one group only")
    parser.add_argument("--output-dir", type=Path, default=Path("docs/validation/raw"))
    parser.add_argument("--data-version", type=int, default=1)
    parser.add_argument("--supersedes", help="Replaced campaign ID, without changing its JSON")
    parser.add_argument("--step", type=int, default=8, help="Global native pixel stride; spatial ROI always stride 1")
    parser.add_argument("--patch-size", type=int, default=256)
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--strict", action="store_true", help="Exit 2 after publishing if exclusions or unaudited states exist")
    args = parser.parse_args(argv)
    if args.list:
        for group, controls in GROUPS.items():
            print(group + ": " + ", ".join(controls))
        print("ihm-persistance-export: manual/test-suite recipe, not a pixel analyser")
        return 0
    if not args.base and not args.mapping:
        parser.error("Provide --base and/or --mapping; no LR suffix inference")
    if args.step < 1 or args.patch_size < 2 or args.data_version < 1:
        parser.error("--step/--data-version positive; --patch-size >=2")
    try:
        input_snapshots = {str(local_path(p)): (signature(local_path(p)), sha256(local_path(p)))
                           for p in args.mapping}
        records = []
        group = ALIASES.get(args.group, args.group)
        if args.base:
            records.extend(base_records(args.base, list(GROUPS) if group == "all" else [group]))
        for mapping in args.mapping:
            records.extend(mapping_records(mapping))
        for feature in args.feature:
            group_for(feature)
        records = [r for r in records if (not args.feature or r["feature"] in args.feature)
                   and (not args.scene or r["scene"] in args.scene)]
        group = ALIASES.get(args.group, args.group)
        groups = [g for g in GROUPS if any(group_for(r["feature"]) == g for r in records)] if group == "all" else [group]
        if not groups:
            raise ValueError("No photographic states")
        if args.output and len(groups) != 1:
            raise ValueError("--output needs exactly one group; use --output-dir for all")
        if args.supersedes and len(groups) != 1:
            raise ValueError("--supersedes replaces one group campaign, not several")
        if args.output and args.output.exists():
            raise ValueError("Output already exists; choose a new version/timestamp")
        diagnostic = False
        for group in groups:
            result = analyse(group, records, args.step, args.patch_size, args.data_version, args.supersedes)
            output = args.output or args.output_dir / (result["campaign_id"] + ".json")
            protected = {local_path(p) for p in args.mapping}
            protected.update(local_path(r[k]) for r in records for k in ("file", "reference") if r.get(k))
            if output.resolve() in protected:
                raise ValueError("Output must not be an input")
            for path, (before, digest) in input_snapshots.items():
                verify_unchanged(path, before, digest)
            write_immutable(output, result)
            print(str(output) + " " + str(result["coverage"]))
            diagnostic |= bool(result["coverage"]["diagnostic"] or result["coverage"]["excluded"])
        return 2 if args.strict and diagnostic else 0
    except (ValueError, OSError, KeyError, TypeError) as error:
        print("Analysis failed: " + str(error), file=sys.stderr)
        return 1
