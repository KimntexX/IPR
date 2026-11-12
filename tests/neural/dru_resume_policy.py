"""Read-only eligibility of completed or interrupted owned training folders."""
import json
from pathlib import Path
from campaign_artifacts import sha256


def action(folder,plan_sha256,steps):
    folder=Path(folder)
    if not folder.exists():return 'new'
    state=json.loads((folder/'result.json').read_text(encoding='utf-8'))
    if state['plan_sha256']!=plan_sha256:raise ValueError('Training plan differs from existing run')
    if state['status']=='completed_dru_bootstrap_not_photographic_qualification':
        if state['completed_steps']!=steps or len(state['losses'])!=steps:raise ValueError('Incomplete completed run')
        refs=state['milestones']+state['controls']
        if not any(Path(r['path']).name=='weights.pt' and r['step']==steps for r in refs):raise ValueError('Missing final export')
        for ref in refs:
            path=Path(ref['path'])
            if not path.resolve().is_relative_to(folder.resolve()) or sha256(path)!=ref['sha256']:
                raise ValueError('Completed artifact changed')
        return 'reuse'
    if state['status'] not in ('stopped_on_error','paused_dru_training'):raise ValueError('Existing worker is not closed')
    ref=json.loads((folder/'resume.json').read_text(encoding='utf-8'))
    if (not isinstance(ref['step'],int) or isinstance(ref['step'],bool) or not 0<=ref['step']<=steps
            or Path(ref['path']).resolve()!=(folder/'resume.pt').resolve() or sha256(folder/'resume.pt')!=ref['sha256']):
        raise ValueError('Invalid durable checkpoint')
    return 'resume'
