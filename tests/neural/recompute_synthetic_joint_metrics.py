"""Versioned metric correction only; unchanged predictions, no neural execution."""
import argparse
from copy import deepcopy
from datetime import datetime,timezone
import json
from pathlib import Path
import subprocess

from diagnose_train_learnability import digest,save
from run_synthetic_joint_pilot import aggregate
from synthetic_bayer_data import FAMILIES


def run(source,output):
    root=Path(__file__).resolve().parents[2];output=output.resolve()
    if not output.is_relative_to((root/'build-patch_neural_restoration').resolve()):raise ValueError('Use dedicated neural build')
    parent=json.loads(source.read_text(encoding='utf-8'))
    if parent['status']!='completed_synthetic_joint_pilot_not_photographic_qualification':raise ValueError('Expected complete pilot')
    now=datetime.now(timezone.utc);campaign='neural-synthetic-joint-v002-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze=dict(schema_version=1,data_version=2,campaign_id=campaign,measured_at_utc=now.isoformat(),
        git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        parent=dict(path=str(source.resolve()),sha256=digest(source),freeze_sha256=parent['freeze_sha256']),
        correction='Suppress relative MSE/opponent error when baseline MSE<=1e-12; absolute errors and predictions unchanged.',
        tools={n:digest(Path(__file__).with_name(n)) for n in
            ('recompute_synthetic_joint_metrics.py','run_synthetic_joint_pilot.py')})
    output.mkdir(parents=True,exist_ok=False);save(output/'freeze.json',freeze)
    rows=deepcopy(parent['records'])
    result=dict(schema_version=1,data_version=2,campaign_id=campaign,measured_at_utc=now.isoformat(),
        freeze=freeze,freeze_sha256=digest(output/'freeze.json'),records=rows,
        training_freeze=parent['freeze'],original_weights_sha256=parent['weights_sha256'],
        summary=aggregate(rows),by_family={f:aggregate([r for r in rows if r['family']==f]) for f in FAMILIES},
        by_noise={str(i):aggregate([r for r in rows if r['noise_index']==i]) for i in range(3)},
        by_family_and_noise={f:{str(i):aggregate([r for r in rows if r['family']==f and r['noise_index']==i])
            for i in range(3)} for f in FAMILIES},
        ambiguity_witness=parent['ambiguity_witness'],status='completed_metric_reanalysis_no_training',
        limits=parent['limits']+['Baseline-near-zero ratios are undefined; inspect absolute errors instead.'])
    save(output/'result.json',result);save(root/'docs/validation/raw'/f'{campaign}.json',result)
    print('COMPLETE '+campaign,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();run(a.source,a.output)
