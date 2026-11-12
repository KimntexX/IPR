"""Seal a bounded continuation curve over the unchanged known training targets."""
from datetime import datetime,timezone
import json
from pathlib import Path
import time
import numpy as np
import torch
from campaign_artifacts import sha256
from memory_budget import MemoryGuard


def run():
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    output=build/'sensor-learning-plan-v001';output.mkdir(exist_ok=False)
    initial_path=build/'local-sigma-plan-v001/plan.json'
    initial=json.loads(initial_path.read_text(encoding='utf-8'))
    witness_path=build/'repeatability-control-campaign-v001/seed71-attempt01/result.json'
    witness=json.loads(witness_path.read_text(encoding='utf-8'))
    parent=witness['plan'];source_path=Path(parent['source'])/'result.json'
    if sha256(source_path)!=parent['source_result_sha256']:raise ValueError('Training source changed')
    source=json.loads(source_path.read_text(encoding='utf-8'))
    dataset=output/'targets.f32'
    plan=dict(schema_version=1,status='preparing',created_at_utc=datetime.now(timezone.utc).isoformat(),
        models=initial['models'],scenarios=[dict(name=s['name'],iso=s['iso'],parameters=s['parameters']) for s in initial['scenes']],
        input_plan_sha256=sha256(initial_path),training_witness_sha256=sha256(witness_path),source_sha256=sha256(source_path),
        config=dict(steps=2048,checkpoints=[0,256,1024,2048],checkpoint_interval=64,accumulation=4,
            learning_rate=.00025,scale=2.,side=256,loss_border=112,width=8,structure_weight=0.,
            scope='cfa_front',statistics_trainable=True,training_seed_base=450000000,maximum_campaign_seconds=5400),
        policy='Continue both frozen current weights, fresh Adam; repeat exact known512step target schedule4times with fresh deterministic noise. No new procedural target variants, no photographic training or reserve opening.',
        limits=['Noise law and continuation length both change; not a causal duration-only comparison.',
                'Synthetic truth, independent Gaussian sensor approximation; not learned correlated compression/fixed pattern.',
                'No checkpoint selected by best development score, all milestones retained. Not an independent photographic qualification.'],
        tools={p:sha256(Path(__file__).with_name(p)) for p in ('prepare_sensor_learning_curve.py','training_texture_data.py','context_data.py')},
        dataset=dict(path=str(dataset.resolve()),dtype='<f4',shape=[2048,3,256,256]),entries=[])
    with (output/'intention.json').open('x',encoding='utf-8') as stream:json.dump(plan,stream,indent=2)
    guard=MemoryGuard(hard_bytes=2*1024**3,soft_bytes=1280*1024**2,system_fraction=.92)
    started=time.monotonic()
    try:
        with guard:
            from run_balanced_moment_pilot import balanced_schedule
            from run_memory_bounded_reconstruction import SampleStream
            from training_texture_data import training_texture,training_context
            from fine_texture_training import POLICY
            from context_data import crop_centre,extend_edge,extend_signal
            from run_synthetic_joint_pilot import tensor_hash
            torch.set_num_threads(4);torch.set_num_interop_threads(1)
            schedule,_=balanced_schedule(source['freeze']['train_slots'],512,4,128)
            if tensor_hash(torch.tensor([[*e['ids'],*e['phases']] for e in schedule]))!=source['freeze']['phase_schedule_sha256']:
                raise ValueError('Known schedule changed')
            sample=SampleStream(source);events={e['serial']:e for e in parent['texture_augmentation']['events']}
            with dataset.open('xb') as stream:
                for step,entry in enumerate(schedule,1):
                    for position,(index,phase) in enumerate(zip(entry['ids'],entry['phases'])):
                        guard.check();serial=4*(step-1)+position;case=source['freeze']['train_slots'][index]
                        if case['family'] in POLICY['families']:
                            event=events[serial];truth,_=training_texture(event)
                            if tensor_hash(truth)!=event['target_sha256']:raise ValueError('Known texture changed')
                            extended=training_context(event,256)
                        else:
                            truth,_,_=sample.sample(step,position,index,phase)
                            event=sample.events.get((step,position))
                            extended=extend_edge(event,256) if event is not None else extend_signal(case['family'],case['seed'],256)
                        if not torch.equal(crop_centre(extended),truth):raise ValueError('Original target centre changed')
                        array=extended.numpy().astype('<f4',copy=False)
                        if array.shape!=(1,3,256,256) or not np.isfinite(array).all():raise ValueError('Invalid target context')
                        stream.write(array.tobytes())
                        plan['entries'].append(dict(serial=serial,phase=phase,family=case['family'],target_sha256=tensor_hash(extended)))
                    if step%64==0:print('TARGETS',step,'/512',flush=True)
            plan['dataset']['sha256']=sha256(dataset)
            plan['status']='sealed_sensor_learning_continuation'
    except Exception as error:
        plan.update(status='stopped_on_error',error=str(error));raise
    finally:
        plan.update(memory=guard.stats(),preparation_seconds=time.monotonic()-started)
        with (output/'plan.json').open('x',encoding='utf-8') as stream:json.dump(plan,stream,indent=2)


if __name__=='__main__':run()
