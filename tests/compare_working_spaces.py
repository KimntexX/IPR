"""Controlled post-DCP working-space experiment. No production migration or RAW mutation."""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import subprocess
import time
import numpy as np
from analysis import native
from inspect_saved_raw_state import inspect as saved_state
from compare_raw_radiometric_output import distribution
from qualify_headroom_candidates import compare_candidate

SCENES=('6V1A0043','6V1A0081','6V1A9940','6V1A0524','7Y6A7003','7Y6A7298','7Y6A7599')
PRIMARIES={
    'srgb_extended':((.64,.33),(.30,.60),(.15,.06)),
    'prophoto_linear':((.7347,.2653),(.1596,.8404),(.0366,.0001)),
    'rec2020_linear':((.708,.292),(.170,.797),(.131,.046))}
WHITES={'srgb_extended':(.3127,.3290),'prophoto_linear':(.3457,.3585),'rec2020_linear':(.3127,.3290)}
OPERATIONS=('neutral','exposure_minus1EV','exposure_plus1EV','chroma_1p25_commonY','signed_channel_power_1p2','clamp_unit_cube')


def xyz_white(xy):
    x,y=xy
    return np.array([x/y,1,(1-x-y)/y],dtype=float)


def rgb_matrix(primaries, white):
    columns=np.array([xyz_white(p) for p in primaries]).T
    return columns@np.diag(np.linalg.solve(columns,xyz_white(white)))


def adaptation(source, target):
    if source==target:return np.eye(3)
    bradford=np.array([[.8951,.2664,-.1614],[-.7502,1.7135,.0367],[.0389,-.0685,1.0296]])
    return np.linalg.inv(bradford)@np.diag((bradford@xyz_white(target))/(bradford@xyz_white(source)))@bradford


def spaces():
    reference=rgb_matrix(PRIMARIES['srgb_extended'],WHITES['srgb_extended']);result={}
    for name,primaries in PRIMARIES.items():
        to_srgb=np.linalg.solve(reference,adaptation(WHITES[name],WHITES['srgb_extended'])@rgb_matrix(primaries,WHITES[name]))
        if name=='srgb_extended':to_srgb=np.eye(3)
        result[name]=dict(to_srgb=to_srgb.astype(np.float32),from_srgb=np.linalg.inv(to_srgb).astype(np.float32),
                          Y_weights=(reference@to_srgb)[1].astype(np.float32))
    return result


def operate(rgb, operation, weights):
    if operation=='neutral':return rgb.copy()
    if operation=='exposure_minus1EV':return rgb*np.float32(.5)
    if operation=='exposure_plus1EV':return rgb*np.float32(2)
    if operation=='chroma_1p25_commonY':
        Y=(rgb@weights)[...,None]
        return Y+np.float32(1.25)*(rgb-Y)
    if operation=='signed_channel_power_1p2':return np.copysign(np.abs(rgb)**np.float32(1.2),rgb)
    if operation=='clamp_unit_cube':return np.clip(rgb,0,1)
    raise ValueError('Unknown diagnostic operation')


def current_gamut(source):
    rgb=np.maximum(source,np.float32(0))
    Y=rgb[...,0]*np.float32(.2126)+rgb[...,1]*np.float32(.7152)+rgb[...,2]*np.float32(.0722)
    gain=np.ones_like(Y);np.divide(1,Y,out=gain,where=Y>1)
    rgb*=gain[...,None];Y=np.minimum(Y,np.float32(1))
    chroma=rgb-Y[...,None];scale=np.ones_like(Y)
    for c in range(3):
        d=chroma[...,c];upper=np.ones_like(Y);lower=np.ones_like(Y)
        np.divide(1-Y,d,out=upper,where=d>0);np.divide(-Y,d,out=lower,where=d<0)
        scale=np.minimum(scale,np.minimum(upper,lower))
    return Y[...,None]+chroma*np.clip(scale,0,1)[...,None]


def encode16(linear):
    rgb=np.clip(linear.astype(float),0,1)
    encoded=np.where(rgb<=.0031308,12.92*rgb,1.055*rgb**(1/2.4)-.055)
    return np.floor(encoded*65535+.5).astype(np.uint16)


def stats(encoded):
    Y=native.linearize(encoded.astype(float)/65535)@np.array([.2126,.7152,.0722])
    return dict(samples=int(Y.size),mean_Y=float(Y.mean()),Y_p05_p50_p95=np.percentile(Y,[5,50,95],method='lower').tolist(),
                encoded_clipped_percent=float(100*np.mean(np.any(encoded>=65534,axis=-1))),
                all_channels_high_percent=float(100*np.mean(np.all(encoded>=65534,axis=-1))))


def main():
    import cv2
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('references',type=Path);args=parser.parse_args()
    memory=native.read_json(args.references);selected={r['scene']:r for r in memory['records']}
    exe=Path('build-windows-msvc/Release/rawNormalizationCorpusProbe.exe');manifest=Path('config/raw-normalization.json')
    sources=[args.references,exe,manifest,Path(__file__),Path('tests/raw_normalization_corpus_probe.cpp'),
             Path('tests/raw_opening_trial_helpers.h'),Path('tests/dcp_headroom_experiment.h'),Path('tests/raw_highlight_headroom.h'),
             Path('tests/qualify_headroom_candidates.py'),Path('tests/analyse_base_colorimetrique_v2.py'),
             Path('tests/compare_raw_radiometric_output.py'),Path('tests/inspect_saved_raw_state.py'),Path('tests/analysis/native.py'),
             Path('src/raw/RawDecoder.cpp'),Path('src/raw/RawNormalization.cpp'),Path('src/profiles/CameraProfileEngine.cpp'),Path('src/core/ColorPipeline.cpp')]
    inputs={str(p):native.sha256(p) for p in sources};prepared=[]
    # Complete reference audit before decoding: missing/changed file stops all work.
    for scene in SCENES:
        if scene not in selected:raise ValueError('Missing scene reference: '+scene)
        record=selected[scene];meta=record['LR_metadata'];path=Path(meta['path'])
        pixels,checked,before=native.inspect(path)
        if checked['sha256']!=meta['sha256']:raise ValueError('LR reference changed: '+str(path))
        settings=checked['xmp_settings']
        if settings.get('CameraProfile')!='Camera Neutral' or settings.get('WhiteBalance')!='As Shot':
            raise ValueError('Camera Neutral / As Shot required: '+str(path))
        reference=distribution(pixels);native.verify_unchanged(path,before,checked['sha256']);del pixels
        inputs[str(path)]=checked['sha256']
        raw=Path('tests/image/base')/(scene+'.CR3');sidecar=Path(str(raw)+'.ipr');state=saved_state(sidecar)
        profile=Path(state['raw']['profile'])
        for p in (raw,sidecar,profile):inputs[str(p)]=native.sha256(p)
        if inputs[str(profile)]!=state['raw']['profileSha256']:raise ValueError('Saved DCP changed')
        prepared.append((scene,raw,profile,reference,checked))
    now=datetime.now(timezone.utc);prefix='comparatif-espaces-travail'
    previous=sorted(Path('docs/validation/raw').glob(prefix+'-v[0-9][0-9][0-9]-*.json'))
    version=int(previous[-1].stem.split('-v')[-1][:3])+1 if previous else 1
    campaign=f'{prefix}-v{version:03d}-'+now.strftime('%Y%m%dT%H%M%S%fZ')
    work=Path('build-colorimetrie-inventory')/campaign;work.mkdir(parents=True,exist_ok=False)
    env=os.environ.copy();env['PATH']=';'.join(['C:/Qt/6.11.2/msvc2022_64/bin','C:/opencv/build/x64/vc16/bin','C:/dev/vcpkg/installed/x64-windows/bin',env.get('PATH','')])
    env['QT_QPA_PLATFORM']='offscreen';env['QT_PLUGIN_PATH']='C:/Qt/6.11.2/msvc2022_64/plugins'
    definitions=spaces();rows=[]
    for scene,raw,profile,reference,lr_meta in prepared:
        print('Decoding common source',scene,flush=True);out=work/scene
        subprocess.run([str(exe.resolve()),str(raw.resolve()),str(profile.resolve()),str(manifest.resolve()),str(out.resolve()),'--working-space-source'],env=env,check=True,stdout=subprocess.DEVNULL,timeout=180)
        proof=native.read_json(out/'manifest.json');source_path=out/'post-dcp-srgb-linear.f32';witness_path=out/'native-current.png'
        if native.sha256(source_path)!=proof['source_sha256'] or native.sha256(witness_path)!=proof['native_preview_sha256']:
            raise ValueError('Native artefact changed')
        if not proof['no_lcp'] or proof['CA'] or not proof['normalization_applied']:raise ValueError('Native recipe mismatch')
        source=np.fromfile(source_path,dtype='<f4' if proof['byte_order']=='little' else '>f4').reshape(proof['rows'],proof['cols'],3).astype(np.float32)
        if not np.isfinite(source).all():raise ValueError('Nonfinite source')
        witness=cv2.imread(str(witness_path),cv2.IMREAD_UNCHANGED)
        if witness is None or witness.dtype!=np.uint16:raise ValueError('Native 16-bit preview required')
        witness=witness[...,:3][...,::-1].copy()
        reconstructed=encode16(current_gamut(source));native_error=int(np.max(np.abs(witness.astype(np.int32)-reconstructed.astype(np.int32))))
        if native_error>4:raise ValueError('Output replay exceeds 4 native 16-bit codes: '+str(native_error))
        controls={op:operate(source,op,definitions['srgb_extended']['Y_weights']) for op in OPERATIONS}
        encoded_controls={op:encode16(current_gamut(rgb)) for op,rgb in controls.items()};models=[]
        for name,definition in definitions.items():
            for operation in OPERATIONS:
                elapsed=[]
                for _ in range(3):
                    start=time.perf_counter();working=source@definition['from_srgb'].T
                    edited=operate(working,operation,definition['Y_weights']);returned=edited@definition['to_srgb'].T
                    elapsed.append(1000*(time.perf_counter()-start))
                if not np.isfinite(returned).all():raise ValueError('Nonfinite result')
                error=float(np.max(np.abs(returned-controls[operation])))
                if operation in OPERATIONS[:4] and error>5e-6:raise ValueError('Coordinate-invariant operation diverged: '+name+' '+operation)
                encoded=encode16(current_gamut(returned));model=dict(space=name,operation=operation,
                    working_negative_percent=float(100*np.mean(np.any(working<0,axis=-1))),
                    working_above_one_percent=float(100*np.mean(np.any(working>1,axis=-1))),
                    working_channel_min=working.min(axis=(0,1)).astype(float).tolist(),working_channel_max=working.max(axis=(0,1)).astype(float).tolist(),
                    returned_max_abs_difference_to_same_operation_srgb=error,output=stats(encoded),
                    change_to_same_operation_srgb=compare_candidate(encoded_controls[operation],encoded),
                    timing_samples_ms=elapsed,timing_median_ms=float(np.median(elapsed)),
                    logical_four_buffer_bytes=int(source.nbytes+working.nbytes+edited.nbytes+returned.nbytes))
                if operation in ('neutral','clamp_unit_cube'):
                    model['output']['mean_Y_relative_to_LR_percent']=100*(model['output']['mean_Y']/reference['mean_Y']-1)
                if operation=='clamp_unit_cube':model['change_to_current']=compare_candidate(witness,encoded)
                if operation in ('neutral','clamp_unit_cube','signed_channel_power_1p2'):
                    target=out/(name+'-'+operation+'.png')
                    if target.exists() or not cv2.imwrite(str(target),encoded[...,::-1]):raise ValueError('Cannot save new diagnostic preview')
                    model.update(preview=str(target),preview_sha256=native.sha256(target))
                models.append(model)
        rows.append(dict(scene=scene,camera=proof['camera'],native=proof,native_proof_sha256=native.sha256(out/'manifest.json'),
                         output_replay_max_16bit_codes=native_error,LR=reference,LR_metadata=lr_meta,models=models))
        print('Compared',scene,len(models),'variants',flush=True)
    for path,sha in inputs.items():
        if native.sha256(Path(path))!=sha:raise ValueError('Input changed: '+path)
    result=dict(campaign_id=campaign,schema_version=1,data_version=version,supersedes_campaign_id=previous[-1].stem if previous else None,
        measured_at_utc=datetime.now(timezone.utc).isoformat(),group='base-colorimetrique',
        inputs=[dict(file=p,sha256=h) for p,h in inputs.items()],records=rows,
        space_definitions={n:dict(primaries_xy=PRIMARIES[n],white_xy=WHITES[n],**{k:v.astype(float).tolist() for k,v in d.items()}) for n,d in definitions.items()},
        versions=dict(numpy=np.__version__,opencv=cv2.__version__),
        method=dict(sample_step=8,dtype='float32',DCP='Production unchanged before all branches',output='Same production-equivalent sRGB gamut mapping and 16-bit encoding',
                    adaptation='Bradford, D50 to D65 for ProPhoto; no chromatic adaptation for Rec2020/D65',
                    Y_reference='Common XYZ D65 Y, weights transformed to each workspace',
                    operators='Exposure gains .5/2; chroma gain 1.25 around common Y; signed RGB power 1.2 is basis-dependent diagnostic, not production slider; cube clamp explicitly isolated',
                    timings='Three NumPy transform/operator/inverse passes; excludes decode, output, metrics and I/O; NOT application latency benchmark',
                    previews='16-bit sRGB PNG on sample grid, cv2 outputs without embedded ICC; no source edits',production_modified=False),
        units=dict(Y='relative linear sRGB luminance',errors='absolute linear RGB, native 16-bit codes, or Delta E00 as named',fractions='percent',time='milliseconds',memory='logical buffer bytes, not measured peak RSS'),
        limitations=['Seven supplied scenes only; no camera-independent validation','Post-DCP signal: cannot recover upstream clipping',
                     'No aligned pixel comparison to Lightroom; LR distributions are neutral references only',
                     'Synthetic diagnostic operators do not migrate or validate all production adjustments',
                     'Native sample grids are not full-resolution detail validation','No perceptual ranking from out-of-cube counts alone'])
    target=Path('docs/validation/raw')/(campaign+'.json');native.write_immutable(target,result);print(target)


if __name__=='__main__':main()
