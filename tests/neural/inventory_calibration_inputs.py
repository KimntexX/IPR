"""Refresh existing calibration metadata without admitting files to training."""
from collections import Counter
from datetime import datetime, timezone
import json
from pathlib import Path

from audit_existing_training_corpus import read_metadata
from campaign_artifacts import sha256 as digest


def run():
    root=Path(__file__).resolve().parents[2]
    shared=root.parents[1]
    source=shared/'tests/image/colorimétrie'
    folders=[source/'protocole',source/'rafale',source/'neural-validation']
    result=dict(schema_version=1,measured_at_utc=datetime.now(timezone.utc).isoformat(),
                scope='Metadata refresh only, not RAW noise calibration, scene qualification or training admission.',
                records=[],errors=[],roots=[str(p) for p in folders],
                limits=['Folder names do not prove cap-closed provenance or spatially uniform illumination.',
                        'No frame copied, decoded, moved, removed or assigned to a new split.',
                        'Existing calibration under older black/decode chains must be replayed before physical noise coefficients are reused.',
                        'Independent RGB half stacks do not provide registered CFA ground truth or per-pixel sample counts.'])
    paths=sorted({p for folder in folders if folder.exists() for p in folder.rglob('*')
                  if p.suffix.lower() in ('.cr2','.cr3') and p.is_file()})
    if len(paths)>2000:
        raise ValueError('Unexpected inventory extent')
    for path in paths:
        try:
            row=read_metadata(path)
            row['role']='calibration_candidate' if 'protocole' in path.parts else 'existing_burst_or_reserved_role_unchanged'
            result['records'].append(row)
        except Exception as error:
            result['errors'].append(dict(path=str(path),error=str(error)))
    result['by_camera_iso_folder']=dict(Counter(
        f"{r['metadata'].get('model')}|{r['metadata'].get('iso')}|{Path(r['path']).parent.name}"
        for r in result['records']))
    result['status']='metadata_inventory_complete_not_requalified' if not result['errors'] else 'metadata_inventory_with_errors_not_requalified'
    result['files']=len(paths)
    result['script_sha256']=digest(Path(__file__))
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    target=root/'docs/validation/raw'/f'neural-calibration-inputs-v001-{stamp}.json'
    target.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(status=result['status'],files=len(paths),errors=len(result['errors']),groups=result['by_camera_iso_folder'])),flush=True)


if __name__=='__main__':run()
