"""Record native synthetic impulse controls; known noise is not camera calibration."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import tempfile

from analysis.native import sha256
from analysis.render import runtime_env
from study_sources import git, ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, default=ROOT / 'build-patch_noise/Release/noiseNeighbourhoodProbe.exe')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--data-version', type=int, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit('Output exists, nothing overwritten')
    with tempfile.TemporaryDirectory() as folder:
        output = Path(folder) / 'controls.json'
        subprocess.run([str(args.probe), '--impulse-controls', str(output)],
                       env=runtime_env(args.probe), capture_output=True, text=True, check=True, timeout=120)
        result = json.loads(output.read_text(encoding='utf-8'))
    stamp = datetime.now(timezone.utc)
    result.update(campaign_id=f"etude-impulsions-bruit-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}",
                  schema_version=1, data_version=args.data_version, measured_at_utc=stamp.isoformat(),
                  commit=git('rev-parse', 'HEAD'), local_changes=git('status', '--short').splitlines(),
                  protocol='docs/validation/protocols/protocole-bruit-voisinages-adaptatifs.md',
                  probe_sha256=sha256(args.probe), runner_sha256=sha256(Path(__file__)),
                  implementation_sha256=sha256(ROOT / 'tests/noise_impulse_filter.cpp'),
                  units={'mse': 'squared linear sample units', 'sigma': 'linear sample units'},
                  production_modified=False)
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, ensure_ascii=False, indent=1, allow_nan=False)
    print(args.output, len(result['cases']), 'controlled cases')


if __name__ == '__main__':
    main()
