"""Compare rawpy DN to archived IPR signed-CFA captures without neural scores."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess

from prepare_next_training import read, write, digest


def sensor_crop(mosaic, origin, side, orientation):
    import numpy as np
    x,y = np.meshgrid(np.arange(side)+origin[0], np.arange(side)+origin[1])
    if orientation & 4:
        x,y = y,x
    if orientation & 1:
        x = mosaic.shape[1]-1-x
    if orientation & 2:
        y = mosaic.shape[0]-1-y
    if min(x.min(),y.min()) < 0 or x.max() >= mosaic.shape[1] or y.max() >= mosaic.shape[0]:
        raise ValueError('Oriented crop outside sensor')
    return mosaic[y,x]


def compare(native, dn, pattern, black, divisor):
    import numpy as np
    yy,xx = np.indices(native.shape)
    pedestal = np.asarray(black)[np.asarray(pattern)[yy%2,xx%2]]
    reconstructed = np.rint(native.astype(np.float64)*divisor+pedestal)
    expected = (dn.astype(np.float32)-pedestal.astype(np.float32))*np.float32(1/divisor)
    return dict(samples=dn.size, integer_dn_mismatches=int((reconstructed != dn).sum()),
                maximum_integer_dn_error=float(np.max(np.abs(reconstructed-dn))),
                maximum_normalized_error=float(np.max(np.abs(expected-native))),
                negative_fraction=float((native<0).mean()))


def run(root, output):
    import numpy as np
    import rawpy
    from memory_budget import MemoryGuard
    from mosaic_data import read_stage
    build = root/'build-patch_neural_restoration'
    tool = build/'native/Release/iprNeuralMosaicCapture.exe'
    plan = read(build/'natural-cfa-preflight-v002/plan.json')
    env = dict(os.environ, OMP_NUM_THREADS='1', OPENBLAS_NUM_THREADS='1')
    for folder in ('C:/Qt/6.11.2/msvc2022_64/bin','C:/opencv/build/x64/vc16/bin','C:/dev/vcpkg/installed/x64-windows/bin'):
        if Path(folder).is_dir():
            env['PATH'] = folder+os.pathsep+env.get('PATH','')
    result = dict(schema_version=1, started_at_utc=datetime.now(timezone.utc).isoformat(),
        tool=dict(path=str(tool), sha256=digest(tool)),
        tools={n:digest(Path(__file__).with_name(n)) for n in (
            'audit_native_cfa_bridge.py','mosaic_data.py','memory_budget.py','prepare_next_training.py')},
        sources=[], status='started', rawpy_version=rawpy.__version__, libraw=list(rawpy.libraw_version),
        scope='Exact DN bridge and normalized CFA against archived IPR renderer, not present HEAD or all pixels',
        limits=['Existing compiled capture tool; archived RGB replay required on each file',
                'No neural score or partition change; not a new camera calibration',
                'Only 36 native ROIs, not full-frame or arbitrary-camera decompression parity',
                'A future physically normalized dataset must use the verified IPR levels; active preflight untouched'])
    guard = MemoryGuard(hard_bytes=2*1024**3, soft_bytes=1280*1024**2, system_fraction=.8)
    try:
        with guard:
            for row in plan['sources']:
                guard.check()
                source = Path(row['source'])
                proof = build/'low-iso-supports-v001'/f'{source.stem}.json'
                path = output/f'{source.stem}.json'
                if not path.exists():
                    with (output/f'{source.stem}.log').open('x',encoding='utf-8') as log:
                        subprocess.run([str(tool),'--camera-proof',str(proof),'--output',str(path)], env=env,
                            stdout=log, stderr=subprocess.STDOUT, timeout=90, check=True)
                capture = read(path)
                if (capture['source_sha256'] != row['expected_sha256'] or digest(source)!=row['expected_sha256']
                        or not capture['rgb_replay_bit_exact'] or capture['rgb_regions_compared']!=9):
                    raise ValueError('Archived rendering or identity bridge failed')
                if digest(path.parent/capture['pixels']) != capture['pixels_sha256']:
                    raise ValueError('Native CFA bytes changed')
                capture['descriptor'] = str(path)
                record = dict(source=str(source), sha256=row['expected_sha256'],
                    native_descriptor_sha256=digest(path), archived_camera_proof_sha256=digest(proof),
                    native_tool_revision=capture['tool_revision'], black=capture['black'], white=capture['white'],
                    divisor=capture['divisor'], rgb_replay_bit_exact=True, rois=[])
                with rawpy.imread(str(source)) as raw:
                    visible = raw.raw_image_visible
                    for stage in capture['stages']:
                        if stage['stage']!='normalized_cfa_before_wb_unclipped':
                            continue
                        native = read_stage(capture, stage)
                        sensor = sensor_crop(visible, stage['origin_xy'],384,capture['orientation_flip'])
                        values = compare(native,sensor,stage['pattern'],capture['black'],capture['divisor'])
                        record['rois'].append(dict(roi=stage['roi'],**values))
                    del visible
                if len(record['rois'])!=9:
                    raise ValueError('Nine native CFA ROI comparisons required')
                result['sources'].append(record)
                write(output/'bridge-progress.json',result)
            mismatch = sum(r['integer_dn_mismatches'] for s in result['sources'] for r in s['rois'])
            error = max(r['maximum_normalized_error'] for s in result['sources'] for r in s['rois'])
            result.update(integer_dn_mismatches=mismatch, maximum_normalized_error=error,
                samples=sum(r['samples'] for s in result['sources'] for r in s['rois']),
                status='qualified_archived_ROI_CFA_bridge' if mismatch==0 and error<=3e-7 else 'bridge_failed')
    except Exception as error:
        result.update(status='bridge_audit_failed',error=type(error).__name__+': '+str(error))
        raise
    finally:
        result.update(finished_at_utc=datetime.now(timezone.utc).isoformat(), memory=guard.stats())
        write(output/'bridge-progress.json',result)
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-natural-CFA-bridge-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,allow_nan=False,separators=(',',':'))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    run(Path(__file__).resolve().parents[2],args.output)
