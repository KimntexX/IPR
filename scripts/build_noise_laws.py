"""Build config/noise-laws.json from the measured sensor noise bank, never by hand.

For every ISO and CFA position of the bank: the shot slope (DN of variance
per DN of signal) and the read variance (DN^2, the reference dark variance
used by the neural training). A slope the bank could not fit stays null;
the engine interpolates it between measured ISOs. The catalogue records the
proof file and its SHA-256 so a law can always be traced to its measurement.
"""
import argparse
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BANK = ROOT/'docs/validation/raw/neural-sensor-noise-v001-20261105T173608Z.json'
OUTPUT = ROOT/'config/noise-laws.json'


def build(bank_path, make, model):
    bank = json.loads(bank_path.read_text(encoding='utf-8'))
    by_iso = {}
    for fit in bank['fits']:
        if fit['plane'] not in range(4):
            raise ValueError('CFA position outside the 2x2 cell')
        slope = fit['fit'].get('shot_slope_dn')
        if slope is not None and slope <= 0:
            raise ValueError(f'Non-positive shot slope at ISO {fit["iso"]}')
        if fit['reference_dark_variance_dn2'] <= 0:
            raise ValueError(f'Non-positive read variance at ISO {fit["iso"]}')
        by_iso.setdefault(fit['iso'], [None]*4)[fit['plane']] = dict(
            shot_slope_dn=slope, read_variance_dn2=fit['reference_dark_variance_dn2'])
    if any(None in planes for planes in by_iso.values()):
        raise ValueError('Every ISO needs its four CFA positions')
    return dict(schema_version=1, cameras=[dict(
        make=make, model=model,
        positions='raster order of the 2x2 CFA cell: (0,0), (0,1), (1,0), (1,1)',
        isos=[dict(iso=iso, positions=planes) for iso, planes in sorted(by_iso.items())],
        provenance=dict(proof=bank_path.relative_to(ROOT).as_posix(),
                        sha256=hashlib.sha256(bank_path.read_bytes()).hexdigest(),
                        status='descriptive fits of lit pairs and darks, R6 III, 5 November 2026'))])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bank', type=Path, default=BANK)
    parser.add_argument('--output', type=Path, default=OUTPUT)
    args = parser.parse_args()
    catalogue = build(args.bank.resolve(), 'Canon', 'EOS R6 Mark III')
    args.output.write_text(json.dumps(catalogue, indent=2)+'\n', encoding='utf-8')
