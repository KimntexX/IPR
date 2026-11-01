"""Windows build orchestrator and conservative, snapshot-based test selection."""
import argparse
import fnmatch
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
RULES_PATH = ROOT / "scripts/test-rules.json"


def matching(path, patterns):
    path = path.replace("\\", "/").removeprefix("./")
    return any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns)


def select_tests(paths, rules, force_full=False, bootstrap=False):
    meaningful = sorted({path.replace("\\", "/").removeprefix("./") for path in paths
                         if not matching(path, rules["ignored_patterns"])})
    reasons = []
    if force_full:
        reasons.append("full validation explicitly requested or GitHub Actions")
    if bootstrap:
        reasons.append("first tested build / configuration changed")
    if len(meaningful) >= rules["full_threshold"]:
        reasons.append("large modification: file threshold reached")
    if any(matching(path, rules["full_patterns"]) for path in meaningful):
        reasons.append("shared interface, build or test infrastructure modified")
    tests, python_suites = set(), set()
    for path in meaningful:
        matched = False
        for rule in rules["rules"]:
            if matching(path, rule["patterns"]):
                matched = True
                tests.update(rule["tests"])
                python_suites.update(rule.get("python", []))
        if not matched and not matching(path, rules["full_patterns"]):
            reasons.append("unmapped code: " + path)
    full = bool(reasons)
    if full:
        tests = set(rules["targets"])
        python_suites = set(rules["python_suites"])
    return {"full": full, "reasons": reasons, "changed": meaningful,
            "tests": sorted(tests), "python": sorted(python_suites),
            "targets": sorted({rules["targets"][test] for test in tests})}


def source_hashes(root=ROOT):
    extensions = {".cpp", ".c", ".h", ".hpp", ".qml", ".json", ".ini", ".py",
                  ".sh", ".ps1", ".cmake", ".yml", ".yaml", ".mm"}
    files = [root / name for name in ("CMakeLists.txt", "CMakePresets.json")]
    for area in ("src", "tests", "scripts", "config", "profiles", "cmake", ".github"):
        for folder, directories, names in os.walk(root / area):
            directories[:] = [name for name in directories if name not in
                               {"image", "__pycache__", "dcp", "lcp", ".git"}]
            files.extend(Path(folder) / name for name in names
                         if Path(name).suffix in extensions or name == "CMakeLists.txt")
    return {path.relative_to(root).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(files) if path.is_file()}


def windows_path(path):
    if os.name == "nt":
        return str(path.resolve())
    for executable in ("wslpath", "cygpath"):
        try:
            return subprocess.check_output([executable, "-w", str(path.resolve())],
                                           text=True).strip()
        except FileNotFoundError:
            continue
    raise RuntimeError("Use WSL or Git Bash on Windows; no native Windows path converter found")


def running_build_executables(build, config, lister=None):
    """Running programs built in this tree. Windows locks a running .exe: its
    link fails (LNK1168) and the previous binary silently stays in place."""
    output = build / config
    names = {path.name.lower(): path.name for path in output.glob("*.exe")} if output.is_dir() else {}
    if not names:
        return []
    try:
        listing = lister() if lister else subprocess.check_output(
            ["tasklist.exe", "/fo", "csv", "/nh"], text=True, errors="replace")
    except (FileNotFoundError, subprocess.CalledProcessError):
        return []  # No Windows process list (CI or plain Linux): nothing to protect.
    running = {line.split('","')[0].strip('"').lower() for line in listing.splitlines() if line.startswith('"')}
    return sorted(names[name] for name in names.keys() & running)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build-windows-msvc")
    parser.add_argument("--config", choices=("Release", "Debug"), default="Release")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--paths", nargs="+", help="Explicit repo-relative paths; no snapshot advancement")
    parser.add_argument("--full-tests", action="store_true")
    parser.add_argument("--no-tests", action="store_true", help="Build only; never marks code as tested")
    parser.add_argument("--tests-only", action="store_true", help="Build/run selected tests without linking appIPR")
    parser.add_argument("--dry-run", action="store_true", help="Print plan; no build, tests or snapshot writes")
    parser.add_argument("--cmake-arg", action="append", default=[], help="Use --cmake-arg=-DNAME=VALUE")
    args = parser.parse_args(argv)
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if args.no_tests and (args.full_tests or args.tests_only):
        parser.error("--no-tests conflicts with full validation / tests-only")
    if args.paths and any(Path(path).is_absolute() or ".." in Path(path.replace("\\", "/")).parts
                          for path in args.paths):
        parser.error("--paths must be repository-relative, without ..")
    rules = json.loads(RULES_PATH.read_text(encoding="utf-8"))
    build = args.build_dir.resolve()
    snapshot_path = build / ".ipr-tested-snapshot.json"
    configuration = {"config": args.config, "cmake_args": args.cmake_arg}
    previous = {}
    if snapshot_path.exists():
        try:
            previous = json.loads(snapshot_path.read_text(encoding="utf-8"))
            if not isinstance(previous, dict) or not isinstance(previous.get("hashes"), dict):
                previous = {}
        except (ValueError, OSError):
            pass  # An invalid checkpoint requires a full validation, not a silent skip.
    current = source_hashes()
    changed = [path for path in current.keys() | previous.get("hashes", {}).keys()
               if current.get(path) != previous.get("hashes", {}).get(path)]
    ci = os.environ.get("GITHUB_ACTIONS", "").lower() == "true"
    bootstrap = (previous.get("schema") != 1 or previous.get("configuration") != configuration
                 or not (build / "CMakeCache.txt").exists())
    # Explicit scopes can test a zone without resetting unrelated dirty-code history.
    plan = select_tests(args.paths if args.paths is not None else changed, rules,
                        args.full_tests or ci, bootstrap and args.paths is None)
    if args.no_tests and ci:
        parser.error("GitHub validation may not skip tests")
    if args.no_tests:
        plan.update(full=False, tests=[], python=[], targets=[], reasons=["build only; checkpoint unchanged"])
    plan.update(config=args.config, jobs=args.jobs, tests_only=args.tests_only,
                configure_args=args.cmake_arg)
    print(json.dumps(plan, indent=2, ensure_ascii=False), flush=True)
    if args.dry_run:
        return 0
    locked = running_build_executables(build, args.config)
    if locked:
        raise RuntimeError("close " + ", ".join(locked) + " before compiling: Windows locks a running"
                           " executable, so it would not be rebuilt")
    build.mkdir(parents=True, exist_ok=True)
    plan_path = build / ".ipr-test-plan.json"
    plan_path.write_text(json.dumps(plan), encoding="utf-8")
    subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                    windows_path(ROOT / "scripts/build-windows.ps1"), "-ProjectRoot", windows_path(ROOT),
                    "-BuildDirectory", windows_path(build), "-PlanPath", windows_path(plan_path)],
                   check=True, cwd=ROOT)
    for suite in plan["python"]:
        subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", "tests",
                        "-p", rules["python_suites"][suite]], check=True, cwd=ROOT)
    if source_hashes() != current:
        raise RuntimeError("Sources changed during validation; checkpoint not updated, rerun the build")
    if not args.no_tests and (args.paths is None or plan["full"]):
        checkpoint = {"schema": 1, "configuration": configuration, "hashes": current}
        pending = snapshot_path.with_suffix(".tmp")
        pending.write_text(json.dumps(checkpoint, indent=2), encoding="utf-8")
        pending.replace(snapshot_path)
        print("Successful validation checkpoint saved.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Build failed: {error}", file=sys.stderr)
        sys.exit(1)
