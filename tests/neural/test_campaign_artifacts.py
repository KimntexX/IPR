import json
from pathlib import Path
import tempfile
import unittest

from campaign_artifacts import campaign_models, sha256


def fixture(root, final_quality=False):
    campaign = root/'campaign'
    campaign.mkdir()
    attempts = []
    for seed, number, status in ((71, 1, 'completed_synthetic_noise_pilot_not_photographic_qualification'),
            (73, 1, 'aborted_for_memory_budget'), (73, 2, 'completed_synthetic_noise_pilot_not_photographic_qualification')):
        folder = campaign/f'seed{seed}-attempt{number:02d}'
        folder.mkdir()
        progress = dict(completed_optimizer_steps=2048 if status.startswith('completed') else 64)
        if seed == 73 and number == 1:
            checkpoint = folder/'training-latest.pt'
            checkpoint.write_bytes(b'interrupted-checkpoint')
            meta = dict(path=str(checkpoint), sha256=sha256(checkpoint), completed_step=64)
            progress['checkpoint'] = meta
        resumed = None
        if number == 2:
            progress['restored_from'] = meta
            resumed = meta['path']
        proof = dict(seed=seed, status=status, plan_sha256='fixed-plan', plan=dict(config=dict(steps=2048)),
            training_progress=progress, pilot_gates_passed=final_quality)
        if status.startswith('completed'):
            weights = folder/'weights.pt'
            weights.write_bytes(b'weights'+bytes([seed]))
            proof['weights_sha256'] = sha256(weights)
        result = folder/'result.json'
        result.write_text(json.dumps(proof))
        attempts.append(dict(seed=seed, number=number, status=status, output=str(folder),
            result_sha256=sha256(result), resumed_from=resumed))
    report = dict(status='completed_both_seeds', plan_sha256='fixed-plan', attempts=attempts)
    (campaign/'campaign.json').write_text(json.dumps(report))
    return campaign, report


class CampaignArtifactTests(unittest.TestCase):
    def test_memory_resume_resolves_final_models_without_selecting_quality(self):
        with tempfile.TemporaryDirectory() as directory:
            campaign, _ = fixture(Path(directory), final_quality=False)
            models = campaign_models(campaign)
            self.assertEqual([p.name for p in models], ['seed71-attempt01', 'seed73-attempt02'])

    def test_rejects_broken_checkpoint_chain_and_changed_weights(self):
        for mutation in ('resume', 'checkpoint', 'weights', 'score_retry'):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as directory:
                campaign, report = fixture(Path(directory))
                if mutation == 'resume':
                    report['attempts'][-1]['resumed_from'] = None
                    (campaign/'campaign.json').write_text(json.dumps(report))
                elif mutation == 'checkpoint':
                    (campaign/'seed73-attempt01/training-latest.pt').write_bytes(b'changed')
                elif mutation == 'weights':
                    (campaign/'seed73-attempt02/weights.pt').write_bytes(b'changed')
                else:
                    path = campaign/'seed73-attempt01/result.json'
                    proof = json.loads(path.read_text())
                    proof['status'] = 'completed_synthetic_noise_pilot_not_photographic_qualification'
                    path.write_text(json.dumps(proof))
                    report['attempts'][1].update(status=proof['status'], result_sha256=sha256(path))
                    (campaign/'campaign.json').write_text(json.dumps(report))
                with self.assertRaises(ValueError):
                    campaign_models(campaign)
