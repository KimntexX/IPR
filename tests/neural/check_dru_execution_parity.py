"""Sequential CPU checkpoint comparison; verifies resource amendment did not change eight updates."""
import argparse
from datetime import datetime,timezone
import gc
import json
from pathlib import Path
import torch
from dru_checkpoint import fingerprint
from campaign_artifacts import sha256
from memory_budget import MemoryGuard


def run(prior,current,output):
    torch.set_num_threads(2)
    rows=[]
    with MemoryGuard(hard_bytes=6*1024**3,soft_bytes=5*1024**3,system_fraction=.92):
        for path in (prior,current):
            state=torch.load(path,weights_only=True,map_location='cpu')
            rows.append(dict(path=str(path.resolve()),sha256=sha256(path),step=state['completed_step'],losses=state['losses'],
                trajectory=fingerprint(dict(model=state['model_state'],optimizer=state['optimizer_state']))))
            del state;gc.collect()
    exact=rows[0]['step']==rows[1]['step']==8 and rows[0]['trajectory']==rows[1]['trajectory'] and rows[0]['losses']==rows[1]['losses']
    result=dict(status='passed_execution_amendment_parity' if exact else 'failed_execution_amendment_parity',
        measured_at_utc=datetime.now(timezone.utc).isoformat(),checkpoints=rows,
        scope='Eight matched reference updates, model+Adam+loss exact; not a universal equivalence proof or photo qualification.')
    with output.open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)
    if not exact:raise ValueError('Resource/scheduling amendment changed numerical trajectory')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('prior','current','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();run(a.prior,a.current,a.output)
