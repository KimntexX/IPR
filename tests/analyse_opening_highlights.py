"""Offline DCP stage ablation, gamut study and editor reproduction; no production tuning."""
import argparse
from datetime import datetime,timezone
import os
from pathlib import Path
import subprocess
from analysis import native
from inspect_saved_raw_state import inspect


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('exports',type=Path);args=parser.parse_args()
    memory=native.read_json(args.exports);now=datetime.now(timezone.utc)
    previous=sorted(Path('docs/validation/raw').glob('investigation-ouverture-hl-v[0-9][0-9][0-9]-*.json'))
    version=int(previous[-1].stem.split('-v')[-1][:3])+1 if previous else 1
    campaign=f'investigation-ouverture-hl-v{version:03d}-'+now.strftime('%Y%m%dT%H%M%S%fZ')
    folder=Path('build-colorimetrie-inventory')/campaign;folder.mkdir(parents=True,exist_ok=False)
    executable=Path('build-windows-msvc/Release/rawNormalizationCorpusProbe.exe')
    editor_exe=Path('build-windows-msvc/Release/editorRawDevelopmentTests.exe')
    manifest=Path('config/raw-normalization.json')
    sources=[args.exports,executable,editor_exe,manifest,Path(__file__),Path('tests/raw_normalization_corpus_probe.cpp'),
             Path('tests/raw_opening_trial_helpers.h'),Path('src/core/ColorPipeline.cpp'),Path('src/raw/RawDecoder.cpp'),
             Path('src/raw/RawNormalization.cpp'),Path('src/editor/imageeditor.cpp'),Path('src/qml/RawDevelopmentModule.qml'),
             Path('src/profiles/CameraProfileEngine.cpp'),Path('src/profiles/CameraProfileEngine.h')]
    sources.extend([Path('tests/dcp_headroom_experiment.h'),Path('tests/raw_highlight_headroom.h')])
    hashes={str(p):native.sha256(p) for p in sources};rows=[]
    env=os.environ.copy();env['PATH']=';'.join(['C:/Qt/6.11.2/msvc2022_64/bin','C:/opencv/build/x64/vc16/bin','C:/dev/vcpkg/installed/x64-windows/bin',env.get('PATH','')])
    env['QT_QPA_PLATFORM']='offscreen';env['QT_PLUGIN_PATH']='C:/Qt/6.11.2/msvc2022_64/plugins'
    for scene in ('6V1A0043','6V1A0081','6V1A9940','6V1A0524','7Y6A7003','7Y6A7298','7Y6A7599'):
        row=next(x for x in memory['records'] if x['scene']==scene)
        raw=Path('tests/image/base')/(scene+'.CR3');sidecar=Path(str(raw)+'.ipr');state=inspect(sidecar)
        profile=Path(state['raw']['profile']);hashes[str(raw)]=native.sha256(raw);hashes[str(sidecar)]=state['sha256'];hashes[str(profile)]=native.sha256(profile)
        out=folder/scene;print('Highlight stages',scene,flush=True)
        subprocess.run([str(executable.resolve()),str(raw.resolve()),str(profile.resolve()),str(manifest.resolve()),str(out.resolve())],env=env,check=True,stdout=subprocess.DEVNULL,timeout=180)
        proof=native.read_json(out/'manifest.json');stage=proof['highlight_stage_audit'];reference=row['LR']['mean_Y']
        stage['candidate']['mean_Y_relative_to_LR_percent']=100*(stage['candidate']['mean_Y']/reference-1)
        stage['current_mean_Y_relative_to_LR_percent']=100*(stage['current_map_mean_Y']/reference-1)
        study=proof['headroom_experiment']
        for model in study['models'].values():
            model['mean_Y_relative_to_LR_percent']=100*(model['mean_Y']/reference-1)
            model['clipping_to_LR_percentage_points']=model['encoded_clipped_percent']-row['LR']['encoded_clipped_percent']
        rows.append(dict(scene=scene,stages=stage,LR=row['LR'],
                         dcp_stage_ablation=proof['dcp_stage_ablation'],camera_buffer=proof['camera_buffer'],
                         full_replay_max_abs_error=proof['full_replay_max_abs_error'],
                         headroom_experiment=study,
                         native_proof_sha256=native.sha256(out/'manifest.json')))
    raw=Path('tests/image/base/6V1A0054.CR3');sidecar=Path(str(raw)+'.ipr')
    hashes[str(raw)]=native.sha256(raw);hashes[str(sidecar)]=native.sha256(sidecar)
    editor_output=folder/'0054-editor-reproduction.json';print('0054 editor activation/deactivation on temporary RAW copy',flush=True)
    subprocess.run([str(editor_exe.resolve()),str(raw.resolve()),str(manifest.resolve()),str(editor_output.resolve())],env=env,check=True,stdout=subprocess.DEVNULL,timeout=240)
    for path,sha in hashes.items():
        if native.sha256(Path(path))!=sha:raise ValueError('Input changed: '+path)
    result=dict(campaign_id=campaign,schema_version=1,data_version=version,group='base-colorimetrique',
                supersedes_campaign_id=previous[-1].stem if previous else None,
                measured_at_utc=datetime.now(timezone.utc).isoformat(),inputs=[dict(file=p,sha256=h) for p,h in hashes.items()],
                records=rows,editor_0054=native.read_json(editor_output),
                method=dict(sample_step=8,optics=False,production_modified=False,
                            candidate='Radial chroma compression around original linear Y, before negative-channel clipping',
                            headroom='Isolated real DCP stages queried through inverse matrix/exposure; p+s*(F(clamp(p/s))-clamp(p/s)), s=max(1,max(p))',
                            shoulder='Fixed study knee .85; peak compression before actual curve, or chroma compression around original sRGB Y; not camera-dependent or fitted to LR',
                            gated_gamut='Smoothstep blend current/soft gamut using current linear sRGB Y from .25 to .75; exactly current below .25; fixed study thresholds, no LR fitting',
                            previews='Native sample grid step 8 PNG, not full-resolution validation TIFF; under build folder only',
                            ablation='Cumulative matrix/exposure, hue-sat, look, full curve, replayed via production applyDcp on same sampled camera buffer',
                            domains='DCP stages are unbounded linear sRGB; nested ProPhoto records and camera buffer report component bounds only, no sRGB luminance interpretation',
                            profile_copies='Temporary IFD tag identifiers hidden; matrix/baseline payloads unchanged; original DCP never modified',
                            camera_bound='At least one camera RGB component >=65534/65535 after LibRaw WB, not proof of sensor saturation'),
                units=dict(Y='relative linear sRGB luminance',fractions='percent of samples',errors='relative percent'),
                limitations=['Seven targeted scenes only, not corpus acceptance',
                             'No mosaic or floating-point LibRaw intermediate; the post-WB camera buffer is already quantized to 16 bits',
                             'Ablations localize changes but are not acceptable alternative complete renderings',
                             'Candidate is an experiment, not deployed and not a Lightroom algorithm',
                             'Independent geometry, no pixel MAE or physical colour target',
                             '0054 test reproduces programmatic editor activation, not the exact user click sequence'])
    target=Path('docs/validation/raw')/(campaign+'.json');native.write_immutable(target,result);print(target)


if __name__=='__main__':main()
