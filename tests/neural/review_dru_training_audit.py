"""Deterministic interpretation of coverage audit; no resampling, training or threshold fitting."""
from datetime import datetime,timezone
import json
from pathlib import Path
from campaign_artifacts import sha256


def run():
    root=Path(__file__).resolve().parents[2]
    path=root/'build-patch_neural_restoration/dru-training-audit-v001/result.json'
    source=json.loads(path.read_text(encoding='utf-8'))
    if source['status']!='completed_read_only_training_coverage_and_objective_audit':raise ValueError('Complete audit required')
    rows=source['training'];textures=[r for r in rows if 'texture' in r]
    counts=[]
    for seed in (71,73):
        for frequency in (18,22):
            for amplitude in (.04,.06):
                selected=[r for r in textures if r['seed']==seed and r['texture']['frequency']>=frequency
                    and r['texture']['amplitude']/1.35>=amplitude]
                counts.append(dict(seed=seed,minimum_primary_frequency=frequency,
                    minimum_effective_primary_amplitude=amplitude,exposures=len(selected),
                    scope='Primary component before per-colour coefficient; secondary amplitude <=.12*.35/1.35 <.04. Not a count of all signal energy.'))
    result=dict(schema_version=1,status='completed_training_audit_review_no_model_verdict',
        measured_at_utc=datetime.now(timezone.utc).isoformat(),audit_sha256=sha256(path),
        script_sha256=sha256(Path(__file__)),training_exposures=len(rows),texture_exposures=len(textures),
        zero_noise_exposures=sum(r['scenario_index'] is None for r in rows),high_frequency_counts=counts,
        training_witness_losses=source['training_summary'],control_witness_losses=source['control_summary'],
        analytic_sigma_at_signal03=source['analytic_sigma_at_signal03'],control_sigma_at_signal03=source['old_control_sigma_at_signal03'],
        findings=['Fixed blur witness lowers average training loss while harming known strong control loss; does not prove the network applies that kernel.',
            'Only25percent of exposures use broad2..28cycle random textures; phase/family joint cycle covers all four physical phases.',
            'Identity depends on both serial and model seed, so dataset and initialization effects are confounded.',
            'Matched0.3DC control low sigma.00922 is below all three training-law sigmas at that DC; old control noise is equal across colours unlike training.',
            'Analytic ramp extension exceeds balanced camera [-8,9] in halo and [-1.8,3.1] in core; signed/HDR legality does not demonstrate representative photo distribution.',
            'Noise-normalization derivative is checked against autograd; standardized residual parametrization compensates sigma, so inverse variance alone does not establish an exploding-gradient bug.'],
        limits=['Read-only known development audit, no independent confirmation, no new reserve or photo truth.',
            'Uniform averaging of exposures/groups is not a perceptual quality metric.',
            'Coverage mismatch, loss, and weak training duration are not causally separated here.',
            'Sigma quantiles use declared stride4 spatial samples; loss denominators and ranges use full core/context.'],
        next_proposal='Keep architecture fixed, separate data seed from initialization, predeclare broad noise/colour-ratio coverage and detail quotas; compare noise-only and coverage-only changes with matched budgets and fixed controls. Do not train on exact test targets or select best seed.')
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    with (root/'docs/validation/raw'/f'neural-dru-training-audit-review-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
        json.dump(result,stream,indent=2,allow_nan=False)
    print('Coverage/objective review saved; no new experiment launched.')


if __name__=='__main__':run()
