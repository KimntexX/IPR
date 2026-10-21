"""Audit all root RAW calibration against fresh Adobe DNG and LibRaw scalar traces.

Diagnostic only: never writes sources, sidecars, deployed config or profiles.
"""
import argparse
from datetime import datetime, timezone
import math
import os
from pathlib import Path
import shutil
import subprocess

import numpy as np
from analysis import native
from audit_dng_baseline import audit as dng_audit
from audit_canon_metadata import audit as canon_audit
from investigate_tonal_base import profile_summary


def compare_levels(dng, trace):
    raw_ifds=[x for x in dng['sub_ifds'] if x.get('photometric')==32803]
    if len(raw_ifds)!=1:
        raise ValueError('Expected one mosaic RAW SubIFD')
    raw=raw_ifds[0]
    white=raw['white_level']
    if isinstance(white,list):
        if len(set(white))!=1: raise ValueError('Unequal multichannel white unsupported')
        white=white[0]
    black=raw['black_level']
    if not isinstance(black,list):black=[black]
    if not black or min(black)<0:raise ValueError('Invalid black levels')
    spans=[white-value for value in black]
    if min(spans)<=0:raise ValueError('Invalid DNG span')
    uniform=max(black)==min(black)
    span=spans[0] if uniform else None
    scale=trace['before_scale_colors']
    gain=scale['maximum']/span if uniform else None
    neutral=np.asarray(dng['ifd0']['as_shot_neutral'],dtype=float)
    gains=np.asarray(trace['after_process']['pre_mul'][:3],dtype=float)
    wb=(1/neutral)/(1/neutral)[1]
    return dict(dng_white=white,dng_black=black,dng_span=span,dng_spans_CFA_order=spans,
                black_uniform=uniform,
                libraw_maximum_before_scale=scale['maximum'],libraw_data_maximum=scale['data_maximum'],
                libraw_black_before_scale=scale['black'],
                white_scale_gain_diagnostic=gain,white_scale_EV_diagnostic=math.log2(gain) if uniform else None,
                white_scale_EV_bounds_diagnostic=[math.log2(scale['maximum']/max(spans)),math.log2(scale['maximum']/min(spans))],
                camera_baseline_EV=dng['ifd0']['baseline_exposure'],
                baseline_plus_white_EV_diagnostic=dng['ifd0']['baseline_exposure']+math.log2(gain) if uniform else None,
                WB_DNG_normalized=wb.tolist(),WB_LibRaw_normalized=(gains/gains[1]).tolist(),
                WB_max_relative_error=float(np.max(np.abs(wb/(gains/gains[1])-1))),
                note='Scalar span comparison, NOT a render prediction: ignores nonlinear stages, demosaic and clipping')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--summary',type=Path,required=True)
    parser.add_argument('--converter',type=Path,default=Path('C:/Program Files/Adobe/Adobe DNG Converter/Adobe DNG Converter.exe'))
    parser.add_argument('--probe',type=Path,default=Path('build-windows-msvc/Release/tonalBaseProbe.exe'))
    parser.add_argument('--reuse-artifacts',type=Path,help='Read complete existing diagnostic artifacts after checking RAW copies and corpus')
    args=parser.parse_args()
    now=datetime.now(timezone.utc)
    campaign='audit-niveaux-ouverture-v001-'+now.strftime('%Y%m%dT%H%M%S%fZ')
    folder=args.reuse_artifacts or Path('build-colorimetrie-inventory')/campaign
    sources=folder/'inputs-without-sidecars';converted=folder/'dng'
    if not args.reuse_artifacts:
        sources.mkdir(parents=True,exist_ok=False);converted.mkdir()
    summary=native.read_json(args.summary)
    probes={}
    inputs=[args.summary,args.converter,args.probe,Path(__file__),Path('tests/tonal_base_probe.cpp'),Path('tests/raw_opening_trial_helpers.h'),
            Path('tests/audit_dng_baseline.py'),Path('tests/audit_canon_metadata.py'),
            Path('src/raw/RawDecoder.cpp'),Path('src/raw/RawDecoder.h'),
            Path('src/raw/RawBlackLevelEstimator.cpp'),Path('src/raw/RawBlackLevelEstimator.h')]
    for row in summary['inputs']:
        if row['file'].endswith('.json') and Path(row['file']).name.startswith('inspection-ouverture-scene-'):
            path=Path(row['file'])
            if native.sha256(path)!=row['sha256']:raise ValueError('Opening evidence changed')
            item=native.read_json(path)['record'];probes[item['scene']]=item
    raws=sorted(p for p in Path('tests/image/base').iterdir() if p.suffix.lower() in ('.cr3','.cr2','.dng','.arw','.nef'))
    if {p.stem for p in raws}!=set(probes):raise ValueError('Corpus changed since reference summary')
    raw_hashes={}
    for source in raws:
        digest=native.sha256(source)
        if digest!=probes[source.stem]['sha256']:raise ValueError('RAW changed')
        raw_hashes[source]=digest;inputs.append(source)
        if not args.reuse_artifacts:shutil.copyfile(source,sources/source.name)
        if native.sha256(sources/source.name)!=digest:raise ValueError('Diagnostic copy differs')
    hashes={str(path):native.sha256(path) for path in inputs}
    commands=[]
    for raw in raws:
        command=[str(args.converter.resolve()),'-c','-p0','-d',str(converted.resolve()),str((sources/raw.name).resolve())]
        print('Reading existing DNG' if args.reuse_artifacts else 'Converting',raw.name,flush=True)
        if not args.reuse_artifacts:
            subprocess.run(command,check=True,timeout=300,
                           creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        commands.append(command)
        if not (converted/(raw.stem+'.dng')).exists():raise ValueError('DNG not generated')
    env=os.environ.copy()
    if os.name=='nt':env['PATH']=';'.join(['C:/Qt/6.11.2/msvc2022_64/bin','C:/opencv/build/x64/vc16/bin','C:/dev/vcpkg/installed/x64-windows/bin',env.get('PATH','')])
    traces=folder/'libraw-scalars'
    if not args.reuse_artifacts:
        subprocess.run([str(args.probe.resolve()),'--scalars-only',str(traces.resolve())]+[str(p.resolve()) for p in raws],env=env,check=True,timeout=900)
    libraw=native.read_json(traces/'manifest.json');records=[]
    if {Path(x['raw']).stem for x in libraw['images']}!={p.stem for p in raws} or len(libraw['images'])!=len(raws):
        raise ValueError('Incomplete or duplicated scalar corpus')
    for artifact in list(converted.glob('*.dng'))+list(traces.glob('*.json')):
        hashes[str(artifact)]=native.sha256(artifact)
    for raw in raws:
        print('Comparing',raw.name,flush=True)
        dng=dng_audit(converted/(raw.stem+'.dng'))
        trace=next(x for x in libraw['images'] if Path(x['raw']).stem==raw.stem)
        profile=Path(probes[raw.stem]['profile_path'])
        if native.sha256(profile)!=probes[raw.stem]['profile_sha256']:raise ValueError('DCP changed')
        records.append(dict(scene=raw.stem,raw_path=str(raw),raw_sha256=raw_hashes[raw],
                            DNG=dng,LibRaw=trace,Canon=canon_audit(raw),
                            deployed_CameraNeutral=profile_summary(profile),comparison=compare_levels(dng,trace)))
    for path,digest in hashes.items():
        if native.sha256(Path(path))!=digest:raise ValueError('Input changed during audit: '+path)
    for source,digest in raw_hashes.items():
        if native.sha256(sources/source.name)!=digest:raise ValueError('Converter changed copied RAW')
    result=dict(campaign_id=campaign,schema_version=1,data_version=1,group='base-colorimetrique',
                started_at_utc=now.isoformat(),measured_at_utc=datetime.now(timezone.utc).isoformat(),
                inputs=[dict(file=p,sha256=h) for p,h in hashes.items()],
                converter_commands=commands,LibRaw_version=libraw['libraw_version'],records=records,
                units=dict(levels='native mosaic codes',EV='log2 linear scale',WB='normalized linear RGB gains'),
                protocol='docs/validation/protocols/protocole-raw-profils.md',
                protocol_sha256=native.sha256(Path('docs/validation/protocols/protocole-raw-profils.md')),
                method=dict(reused_artifacts=bool(args.reuse_artifacts),artifact_directory=str(folder),
                            sidecars='Converter sees verified RAW copies only; LibRaw probe never restores editor sidecars',
                            converter='Installed Adobe interpretation, not physical calibration',production_modified=False),
                limitations=['No DNG pixel decoding or Adobe Stage3 execution',
                             'Scalar spans do not predict rendered luminance or clipping',
                             'Embedded DNG default profile need not equal deployed Camera Neutral',
                             'Canon unknown version offsets remain candidates, not certified fields',
                             'DNG/raw copies stay local in ignored build directory, no automatic cleanup'])
    destination=Path('docs/validation/raw')/(campaign+'.json')
    native.write_immutable(destination,result)
    print(destination,flush=True)


if __name__=='__main__':main()
