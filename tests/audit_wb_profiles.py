"""Read-only DCP matrix inventory for white-balance investigations."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path

import numpy as np

from analysis.native import sha256
from investigate_tonal_base import read_dcp, profile_summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--compare', type=Path, action='append', required=True)
    parser.add_argument('--output-dir', type=Path, default=Path('docs/validation/raw'))
    args = parser.parse_args()
    now = datetime.now(timezone.utc)
    tags0 = read_dcp(args.reference)
    names = {50721: 'ColorMatrix1', 50722: 'ColorMatrix2',
             50964: 'ForwardMatrix1', 50965: 'ForwardMatrix2'}
    profiles = []
    for path in [args.reference, *args.compare]:
        tags = read_dcp(path)
        matrices = {name: list(tags[tag]) if tag in tags else None for tag, name in names.items()}
        differences = {name: float(np.max(np.abs(np.array(tags[tag]) - tags0[tag])))
                       if tag in tags and tag in tags0 and len(tags[tag]) == len(tags0[tag]) else None
                       for tag, name in names.items()}
        profiles.append({**profile_summary(path), 'matrices': matrices,
                         'maximum_absolute_coefficient_difference_to_reference': differences})
    result = {'campaign_id': 'audit-profils-wb-v001-' + now.strftime('%Y%m%dT%H%M%S%fZ'),
              'schema_version': 1, 'data_version': 1, 'measured_at_utc': now.isoformat(),
              'reference': str(args.reference), 'profiles': profiles,
              'units': {'matrix_coefficients': 'dimensionless'},
              'analyser_sha256': sha256(__file__), 'dcp_reader_sha256': sha256('tests/investigate_tonal_base.py'),
              'limitations': ['Local DCP inventory, not proof of the Lightroom rendering algorithm',
                              'Coefficient differences are not perceptual color differences',
                              'No image rendered or profile modified']}
    args.output_dir.mkdir(parents=True, exist_ok=True)
    output = args.output_dir / (result['campaign_id'] + '.json')
    with output.open('x', encoding='utf8') as stream:
        json.dump(result, stream, indent=2, ensure_ascii=False, allow_nan=False)
    print(output)


if __name__ == '__main__':
    main()
