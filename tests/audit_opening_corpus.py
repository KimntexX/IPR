"""Audit RAW roots and stored neutral TIFFs without applying sidecars or writing images."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path

import numpy as np

from analysis import native
from inspect_neutral_colour_pair import statistics


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, default=Path('tests/image/base'))
    parser.add_argument('--output-dir', type=Path, default=Path('docs/validation/raw'))
    parser.add_argument('--card-inventory', type=Path)
    args = parser.parse_args()
    now = datetime.now(timezone.utc)
    campaign = 'audit-corpus-ouverture-v001-'+now.strftime('%Y%m%dT%H%M%S%fZ')
    result = dict(campaign_id=campaign, schema_version=1, data_version=1,
                  measured_at_utc=now.isoformat(), group='base-colorimetrique',
                  analyser_sha256=native.sha256(Path(__file__)), RAW=[], neutral_exports=[],
                  method={'space':'sRGB 16-bit native, linear Y, CIELAB D65', 'sample_step':8,
                          'geometry':'Independent image statistics, no alignment or cross-engine MAE',
                          'sidecars':'Inventoried only, never applied to fresh decoder probes'})
    for path in sorted(args.base.iterdir()):
        if path.suffix.lower() not in ('.cr3', '.cr2', '.dng', '.nef', '.arw'):
            continue
        before = native.signature(path)
        digest = native.sha256(path)
        sidecars = []
        for suffix in ('.xmp', path.suffix+'.ipr'):
            sidecar = path.with_suffix(suffix)
            if sidecar.exists():
                sidecars.append(dict(path=str(sidecar), sha256=native.sha256(sidecar), bytes=sidecar.stat().st_size))
        result['RAW'].append(dict(scene=path.stem,path=str(path),sha256=digest,bytes=before[0],sidecars=sidecars))
        native.verify_unchanged(path,before,digest)
    for path in sorted((args.base/'neutre').iterdir()):
        if path.suffix.lower() not in ('.tif','.tiff'):
            continue
        print('Auditing',path.name,flush=True)
        try:
            pixels,metadata,before = native.inspect(path)
            row = dict(file=str(path),scene=path.stem.split('-')[0],metadata=metadata,
                       declaration=native.embedded_metadata(path)[0],
                       distribution=statistics(native.encoded(pixels,step=8)),
                       status='measured')
            native.verify_unchanged(path,before,metadata['sha256'])
            del pixels
        except (OSError,ValueError,TypeError) as error:
            row = dict(file=str(path),status='diagnostic_error',error=str(error))
        result['neutral_exports'].append(row)
    if args.card_inventory:
        card = json.loads(args.card_inventory.read_text(encoding='utf-8'))
        reasons = {'6V1A0318':'Low-ISO sunlit skin, rocks and coloured crash pads',
                   '6V1A0524':'Backlit skin, sky and dark rocks; tonal headroom',
                   '6V1A9608':'Blue hold on yellow background; saturated hue interaction at high ISO',
                   '6V1A9616':'Red hold on light wall; warm/neutral contrast at ISO 1600'}
        imports = []
        for row in card['files']:
            source = Path(row['path'])
            if source.stem not in reasons:
                continue
            target = args.base/source.name
            digest, destination_hash = native.sha256(source),native.sha256(target)
            if digest != destination_hash:
                raise ValueError('Card copy hash mismatch: '+source.name)
            imports.append(dict(source=str(source),destination=str(target),sha256=digest,
                                metadata=row['metadata'],selection_reason=reasons[source.stem]))
        if len(imports)!=4:
            raise ValueError('Expected the four inspected card additions')
        result['card_selection'] = dict(inventoried_CR3=len(card['files']),viewed_embedded_JPEGs=len(card['preview_indices']),
                                      source_inventory_sha256=native.sha256(args.card_inventory),imports=imports,
                                      note='Embedded JPEGs used only for selection, not RAW colour measurement')
    result['limitations'] = ['Neutral/profile names alone do not attest IPR settings',
                            'Different export generations are not interchangeable',
                            'Stored images remain user-managed; no automatic deletion']
    native.write_immutable(args.output_dir/(campaign+'.json'),result)
    print(campaign,len(result['RAW']),'RAW',len(result['neutral_exports']),'neutral TIFFs',flush=True)


if __name__=='__main__':
    main()
