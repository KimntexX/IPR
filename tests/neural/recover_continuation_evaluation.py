"""Resume evaluation only from sealed continuation campaigns; never retrain."""
import argparse
from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import sys

from campaign_artifacts import campaign_models, sha256
from run_neural_followup import execute
from run_noisy_campaign import capacity
from run_stability_qualification import write


def run(root, output):
    base = root/'build-patch_neural_restoration'
    if not output.resolve().is_relative_to(base.resolve()):
        raise ValueError('Dedicated neural build required')
    output.mkdir(parents=True, exist_ok=False)
    old = base/'paired-continuation-v001/progress.json'
    prepared = base/'paired-warm-plan-v002'
    campaign = base/'continuation-progressive-campaign-v001'
    control_campaign = base/'continuation-control-campaign-v001'
    control_fine = base/'continuation-control-fine-v001/result.json'
    prefix = 'continuation-progressive-recovered'
    qualification = base/(prefix+'-qualification-v001')
    state = dict(schema_version=1, status='validating_sealed_artifacts',
        started_at_utc=datetime.now(timezone.utc).isoformat(), script_sha256=sha256(Path(__file__)),
        original_run_sha256=sha256(old), stages=[],
        policy='Evaluation recovery only, no training or score selection. Preserve old failure. Resume-linked final weights are immutable; all gates unchanged.')
    def command(name, script, *args):
        state['status'] = 'admission_'+name
        write(output/'progress.json', state)
        sample = capacity(datetime.now(timezone.utc)+timedelta(hours=2), .85, 15)
        if sample is None:
            raise TimeoutError('No RAM admission within two hours')
        row = dict(name=name, started_at_utc=datetime.now(timezone.utc).isoformat(), admission=sample,
            command=[sys.executable, str(root/'tests/neural'/script), *map(str, args)])
        state['stages'].append(row)
        state['status'] = name
        write(output/'progress.json', state)
        with (output/(name+'.log')).open('x', encoding='utf-8') as log:
            code = execute(row['command'], log)
        row.update(returncode=code, finished_at_utc=datetime.now(timezone.utc).isoformat())
        if code:
            raise RuntimeError('Recovery execution failed: '+name)
    try:
        old_state = json.loads(old.read_text(encoding='utf-8'))
        if old_state.get('error') != 'Failed execution: qualify_progressive':
            raise ValueError('This recovery requires the recorded qualification failure')
        for arm in ('control', 'progressive'):
            if sha256(prepared/arm/'plan.json') != old_state['plans_sha256'][arm]:
                raise ValueError('Original paired plan changed')
        state['resolved_models'] = [str(p) for p in campaign_models(campaign)]
        state['control_models'] = [str(p) for p in campaign_models(control_campaign)]
        state['campaign_sha256'] = sha256(campaign/'campaign.json')
        control = json.loads(control_fine.read_text(encoding='utf-8'))
        if control['status'] != 'completed_fine_signal_regression_not_photographic_qualification':
            raise ValueError('Complete sealed control fine regression required')
        state['control_fine_sha256'] = sha256(control_fine)
        command('qualify_progressive', 'run_stability_qualification.py', '--campaign', campaign,
            '--data-plan', prepared/'progressive/plan.json', '--prefix', prefix, '--output', qualification)
        verdict = json.loads((qualification/'progress.json').read_text(encoding='utf-8'))
        if verdict['status'] == 'stopped_on_error':
            raise RuntimeError('Qualification calculation error')
        command('paired_followup', 'run_neural_followup.py', '--qualification', qualification/'progress.json',
            '--data-plan', prepared/'progressive/plan.json', '--campaign', campaign, '--prefix', prefix,
            '--previous-fine', control_fine, '--previous-campaign', control_campaign,
            '--output', base/(prefix+'-followup-v001'))
        followup = json.loads((base/(prefix+'-followup-v001/progress.json')).read_text(encoding='utf-8'))
        state.update(status='completed_evaluation_recovery', candidate_followup_status=followup['status'])
        comparison = base/(prefix+'-followup-v001/paired-fine.json')
        if comparison.exists():
            state['paired_fine'] = json.loads(comparison.read_text(encoding='utf-8'))
    except Exception as error:
        state.update(status='stopped_on_error', error=str(error))
        raise
    finally:
        state['finished_at_utc'] = datetime.now(timezone.utc).isoformat()
        write(output/'progress.json', state)
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        destination = root/'docs/validation/raw'/('neural-continuation-recovery-v001-'+stamp+'.json')
        if destination.exists():
            raise FileExistsError(destination)
        write(destination, state)
        print(state['status'], flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(Path(__file__).resolve().parents[2], args.output)
