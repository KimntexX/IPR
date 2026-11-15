"""Metadata-only audit of existing LR neutral/100 noise reference pairs."""
from datetime import datetime, timezone
import json
from pathlib import Path
import sys

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.analysis.native import embedded_metadata, sha256, signature


def main():
    if len(sys.argv) != 2:
        raise SystemExit('Usage: audit_noise_reference_settings.py <tests/image/base>')
    base = Path(sys.argv[1]).resolve()
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    source = Path(__file__)
    proof = dict(schema_version=1, created_utc=stamp,
                 study='LR_noise_neutral_vs_100_reference_settings_audit',
                 analyzer=dict(sha256=sha256(source), text=source.read_text(encoding='utf-8')),
                 metadata_reader=dict(sha256=sha256(ROOT/'tests/analysis/native.py')),
                 scenes=[], scope='Metadata only, no pixels decoded or changed')
    for name in ('6V1A9608', '6V1A9890'):
        records = {}
        for state, folder in (('LR0', 'neutre'), ('LR100', 'bruit/proto')):
            path = base/folder/(name+'.tif')
            before = signature(path)
            _, settings = embedded_metadata(path)
            with Image.open(path) as image:
                shape = [image.width, image.height]
            records[state] = dict(path=str(path), sha256=sha256(path), settings=settings,
                                  dimensions=shape, bytes=before[0], mtime_ns=before[1])
            if signature(path) != before:
                raise ValueError('Reference changed during audit')
        a, b = records['LR0']['settings'], records['LR100']['settings']
        differences = {k: [a.get(k), b.get(k)] for k in sorted(a.keys()|b.keys()) if a.get(k) != b.get(k)}
        optics_match = all(a.get(k) == b.get(k) for k in ('LensProfileEnable', 'AutoLateralCA'))
        proof['scenes'].append(dict(scene=name, states=records, differences=differences,
                                    optics_match=optics_match,
                                    verdict='confounded_by_optics_settings' if not optics_match else 'no_optics_toggle_difference'))
    proof['verdict'] = 'not_controlled_noise_only_pairs' if any(not s['optics_match'] for s in proof['scenes']) else 'metadata_optics_match_only'
    proof['limits'] = ['XMP flags are declarations, not proof of the actual optical correction magnitude.',
                       'This audit does not disprove visible smoothing or quantify its causes.',
                       'Do not calibrate a noise-only force ratio from mismatched optics settings.']
    output = ROOT/'docs/validation/raw'/f'audit-noise-lr-settings-v001-{stamp}.json'
    with output.open('x', encoding='utf-8') as handle:
        json.dump(proof, handle, ensure_ascii=False, indent=2, allow_nan=False)
    print(output, proof['verdict'])


if __name__ == '__main__':
    main()
