"""Export all predeclared milestones; never choose the best checkpoint."""
from copy import deepcopy
import json
from pathlib import Path
import torch
from campaign_artifacts import sha256
from training_resume import validate_state


def run():
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    campaign=build/'sensor-learning-campaign-v001';output=campaign/'photo-plans';output.mkdir(exist_ok=False)
    parent=json.loads((build/'local-sigma-plan-v001/plan.json').read_text(encoding='utf-8'))
    for step in (256,1024,2048):
        folder=output/f'step{step}';folder.mkdir();plan=deepcopy(parent);plan['models']=[]
        for seed in (71,73):
            source=campaign/f'seed{seed}'
            result_path=source/'result.json';result=json.loads(result_path.read_text(encoding='utf-8'))
            if result['status']!='completed_sensor_curve_continuation_not_quality_qualification':raise ValueError('Both complete curves required')
            path=source/f'checkpoint-{step:06d}.pt';state=torch.load(path,weights_only=True,map_location='cpu')
            validate_state(state,state['context'])
            if state['completed_step']!=step or state['context']['seed']!=seed:raise ValueError('Wrong milestone')
            target=folder/f'seed{seed}-weights.pt'
            with target.open('xb') as stream:torch.save(dict(state_dict=state['model_state']),stream)
            plan['models'].append(dict(seed=seed,path=str(target.resolve()),weights_sha256=sha256(target),
                result_sha256=sha256(result_path),checkpoint_sha256=sha256(path),additional_steps=step))
        plan['milestone_scope']='Known photographic development controls only; all declared checkpoints, no score-based selection.'
        with (folder/'plan.json').open('x',encoding='utf-8') as stream:json.dump(plan,stream,indent=2)


if __name__=='__main__':run()
