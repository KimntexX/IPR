"""Compare a fresh production camera-WB probe with audited native LR TIFFs.

The probe measures exact stride-8 pixels, not resized previews or old exports.
The As Shot check and manual 5200/tint0 response supports are distinct.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path

from analysis import campaign, native
from compare_temperature_tint import records, pairings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe', type=Path)
    parser.add_argument('--lr-root', type=Path, default=Path('tests/image/base/couleur'))
    parser.add_argument('--output-dir', type=Path, default=Path('docs/validation/raw/comparaison-wb-20261011'))
    args = parser.parse_args()
    probe_hash = native.sha256(args.probe)
    probe = native.read_json(args.probe)
    if probe.get('kind') != 'fresh_RAW_camera_WB_probe' or not probe['all_neutrals_exact']:
        raise ValueError('Fresh production probe with exact neutral identity required')
    source_hashes = probe['source_hashes']
    for path, digest in source_hashes.items():
        if native.sha256(path) != digest:
            raise ValueError('Source changed after RAW probe: ' + path)
    lr, inventory = records(args.lr_root, [])
    document = campaign.analyse('couleur', lr, step=8, data_version=3)
    for record in document['records']:
        record['issues'].extend(record.get('preflight_issues', []))
        if record['issues'] and record['status'] != 'excluded':
            record['status'], record['protocol_usable'] = 'diagnostic', False
    fresh = [dict(r, status='measured', protocol_usable=True) for r in probe['records']]
    pairs, aggregates = pairings(fresh + document['records'])
    document['comparison'] = {
        'probe_memory': str(args.probe.resolve()), 'probe_sha256': probe_hash,
        'fresh_IPR_render': True, 'all_AsShot_neutrals_exact': True,
        'IPR_records': fresh, 'pairs': pairs, 'aggregates': aggregates,
        'lr_inventory': inventory,
        'limits': ['Pointwise RAW/DCP production probe, no TIFF IPR files generated',
                   'Independent engine/state unclipped masks; no inter-engine pixel MAE',
                   'Manual 5200/tint0 supports are not shared As Shot neutrals',
                   'IPR tint is relative to measured As Shot; LR tint is its absolute coordinate',
                   'No guarantee of Lightroom parity or visual acceptance'],
        'cleanup_eligible': False,
    }
    if native.sha256(args.probe) != probe_hash:
        raise ValueError('Probe memory changed during analysis')
    for path, digest in source_hashes.items():
        if native.sha256(path) != digest:
            raise ValueError('Source changed during comparison: ' + path)
    now = datetime.now(timezone.utc)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    output = args.output_dir / ('comparaison-wb-capteur-v003-' + now.strftime('%Y%m%dT%H%M%S%fZ') + '.json')
    with output.open('x', encoding='utf8') as stream:
        json.dump(document, stream, indent=2, ensure_ascii=False, allow_nan=False)
    print(output, flush=True)
    print(json.dumps(aggregates, indent=2), flush=True)


if __name__ == '__main__':
    main()
