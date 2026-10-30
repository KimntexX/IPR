"""Measure existing Lightroom exports in the exact coarse cells, without another IPR render."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import cv2
import numpy as np
from audit_dark_mean import sha256


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cells-proof',type=Path,required=True)
    parser.add_argument('--reference-proof',type=Path,required=True)
    parser.add_argument('--project-root',type=Path,required=True)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    cells=json.loads(args.cells_proof.read_text(encoding='utf-8'))
    reference=json.loads(args.reference_proof.read_text(encoding='utf-8'))
    if cells['reference_proof_sha256']!=sha256(args.reference_proof):
        raise ValueError('Different frozen reference proof')
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    proof=dict(schema_version=1,created_utc=stamp,study='lightroom_means_in_coarse_cells',
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        cells_proof_sha256=sha256(args.cells_proof),reference_proof_sha256=sha256(args.reference_proof),
        source=dict(sha256=sha256(__file__),text=Path(__file__).read_text(encoding='utf-8')),
        versions=dict(opencv=cv2.__version__,numpy=np.__version__),scenes=[],
        limits=['Existing LR TIFF values in 0..255 units via uint16/257, not quantized to uint8.',
                'Cells selected from the LR neutral of the same RAW, not independent clean truth.',
                'No claim that LR100/100 and IPR100/100 have equivalent denoising strength.',
                'No new IPR render; retain original cells proof for all native settings and provenance.'])
    failed=False
    cv2.setNumThreads(1)
    try:
        for entry in cells['scenes']:
            name=entry['scene']
            if 'measurements' not in entry:
                proof['scenes'].append(dict(scene=name,status=entry['status']))
                continue
            scene=reference['scenes'][name]
            area=scene['masks']['common_area']
            ox,oy=area['lr_origin_xy'];ix,iy=area['ipr_origin_xy']
            record=dict(scene=name,states={})
            proof['scenes'].append(record)
            sources={'0/0/0':scene['lr_neutral'],**{k:scene['lr_states'][k] for k in ('75/0/0','100/100/0')}}
            for state,source in sources.items():
                path=args.project_root/source['file']
                if sha256(path)!=source['sha256']:
                    raise ValueError(f'{name}/{state}: reference TIFF hash mismatch')
                image=cv2.imread(str(path),cv2.IMREAD_UNCHANGED)
                if image is None or image.dtype!=np.uint16 or image.ndim!=3 or image.shape[2]<3:
                    raise ValueError('Invalid uint16 reference')
                values=[]
                for cell in entry['cells']:
                    x,y,w,h=cell['camera_roi'];x=x-ix+ox;y=y-iy+oy
                    sample=image[y:y+h,x:x+w,:3]
                    if sample.shape[:2]!=(h,w):
                        raise ValueError('Cell outside TIFF')
                    bgr=sample.astype(np.float32)/257.
                    values.append(float((.2126*bgr[...,2]+.7152*bgr[...,1]+.0722*bgr[...,0]).mean()))
                record['states'][state]=dict(file=str(path),sha256=source['sha256'],
                    cell_means=values,mean=float(np.mean(values)))
                del image
            print(name,{state:round(data['mean'],6) for state,data in record['states'].items()},flush=True)
    except Exception as exc:
        failed=True
        proof['error']=f'{type(exc).__name__}: {exc}'
    proof['verdict']='fail' if failed else 'measured_not_quality_validation'
    output=root/'docs/validation/raw'/f'camera-denoise-coarse-lr-means-v001-{stamp}.json'
    with output.open('x',encoding='utf-8') as handle:
        json.dump(proof,handle,ensure_ascii=False,indent=2,allow_nan=False)
    print(output,proof['verdict'],proof.get('error',''))
    return int(failed)


if __name__=='__main__':
    raise SystemExit(main())
