"""Same-grid 16-bit diagnostic PNG comparison to IPR witness, not aligned Lightroom MAE."""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import numpy as np
from analysis import native
from analyse_base_colorimetrique_v2 import srgb_lab, delta_e_2000


def compare_candidate(baseline, candidate):
    if baseline.shape != candidate.shape or baseline.ndim != 3 or baseline.shape[-1] != 3:
        raise ValueError('Equal RGB grids required')
    if not baseline.size or baseline.dtype != np.uint16 or candidate.dtype != np.uint16:
        raise ValueError('Native unsigned 16-bit pixels required')
    base_lab, base_y = srgb_lab(baseline.astype(float)/65535)
    cand_lab, cand_y = srgb_lab(candidate.astype(float)/65535)
    de = delta_e_2000(base_lab, cand_lab)
    delta_y = cand_y-base_y
    base_c = np.hypot(base_lab[...,1],base_lab[...,2])
    cand_c = np.hypot(cand_lab[...,1],cand_lab[...,2])
    hue = np.abs((np.degrees(np.arctan2(cand_lab[...,2],cand_lab[...,1])-
                             np.arctan2(base_lab[...,2],base_lab[...,1]))+180)%360-180)
    chromatic = (base_c>=5)&(cand_c>=5)
    iy,ix = np.indices(base_y.shape);height,width=base_y.shape
    masks = dict(global_grid=np.ones(base_y.shape,dtype=bool),
                 brightest_five_percent=base_y>=np.percentile(base_y,95,method='lower'),
                 dark_Y_below_point05=base_y<.05,
                 central_fixed_ROI=(ix>=width*.25)&(ix<width*.75)&(iy>=height*.48)&(iy<height*.76))
    result={}
    for name,mask in masks.items():
        n=int(mask.sum())
        if not n:
            result[name]=dict(samples=0,reason='Empty baseline-defined mask');continue
        color_mask=mask&chromatic
        result[name]=dict(samples=n,delta_E00_mean=float(de[mask].mean()),
                          delta_E00_p95=float(np.percentile(de[mask],95,method='lower')),
                          delta_E00_max=float(de[mask].max()),
                          delta_E00_over_one_percent=float(100*np.mean(de[mask]>1)),
                          signed_Y_change=float(delta_y[mask].mean()),
                          mean_chroma_change=float((cand_c-base_c)[mask].mean()),
                          chromatic_samples=int(color_mask.sum()),
                          hue_change_degrees_p95=float(np.percentile(hue[color_mask],95,method='lower')) if color_mask.any() else None)
    return result


def main():
    import cv2  # Required only for manual 16-bit PNG analysis, not numeric unit tests.
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('study',type=Path);args=parser.parse_args()
    memory=native.read_json(args.study);inputs={str(args.study):native.sha256(args.study)};records=[]
    for path in (Path(__file__),Path('tests/analyse_base_colorimetrique_v2.py'),Path('tests/analysis/native.py')):
        inputs[str(path)]=native.sha256(path)
    def load(model):
        path=Path(model['preview']);sha=native.sha256(path)
        if sha!=model['preview_sha256']:raise ValueError('Preview changed: '+str(path))
        pixels=cv2.imread(str(path),cv2.IMREAD_UNCHANGED)
        if pixels is None or pixels.dtype!=np.uint16 or pixels.ndim!=3 or pixels.shape[-1] not in (3,4):
            raise ValueError('Native RGB/RGBA 16-bit PNG required: '+str(path))
        inputs[str(path)]=sha
        return pixels[...,:3][...,::-1].copy()
    for row in memory['records']:
        print('Qualifying',row['scene'],flush=True)
        models=row['headroom_experiment']['models'];baseline=load(models['current']);result={}
        for name,model in models.items():
            if name=='current':continue
            result[name]=compare_candidate(baseline,load(model))
        records.append(dict(scene=row['scene'],models=result))
    for path,sha in inputs.items():
        if native.sha256(Path(path))!=sha:raise ValueError('Input changed: '+path)
    now=datetime.now(timezone.utc);prefix='qualification-candidats-hl'
    previous=sorted(Path('docs/validation/raw').glob(prefix+'-v[0-9][0-9][0-9]-*.json'))
    version=int(previous[-1].stem.split('-v')[-1][:3])+1 if previous else 1
    campaign=f'{prefix}-v{version:03d}-'+now.strftime('%Y%m%dT%H%M%S%fZ')
    result=dict(campaign_id=campaign,schema_version=1,data_version=version,
                supersedes_campaign_id=previous[-1].stem if previous else None,
                measured_at_utc=now.isoformat(),group='base-colorimetrique',
                parent_campaign_id=memory['campaign_id'],inputs=[dict(file=p,sha256=h) for p,h in inputs.items()],
                versions=dict(opencv=cv2.__version__,numpy=np.__version__),records=records,
                units=dict(delta_E00='CIEDE2000 D65, relative to current IPR witness',Y='linear sRGB absolute difference',hue='degrees',chroma='Lab C* units'),
                method=dict(sample_step=8,native_bits=16,masks='Defined only on current witness; fixed geometric ROI is not a semantic segmentation'),
                limitations=['No Lightroom pixel comparison or physical colour target',
                             'PNG sample grids, not full-resolution detail validation',
                             'A lower clipping rate is not evidence of recovered detail',
                             'Perceptual distances describe changes; thresholds are descriptive, not user acceptance'])
    target=Path('docs/validation/raw')/(campaign+'.json');native.write_immutable(target,result);print(target)


if __name__=='__main__':main()
