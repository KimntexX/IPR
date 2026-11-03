from pathlib import Path
import unittest

from diagnose_train_learnability import digest
from neural_inference_contract import check_inference, INFERENCE_TOOLS
from run_noisy_moment_pilot import NOISE, runtime_versions


class NeuralInferenceContractTests(unittest.TestCase):
    def test_training_only_change_does_not_hide_forward_change(self):
        tools = {n: digest(Path(__file__).with_name(n)) for n in INFERENCE_TOOLS}
        data = dict(runtime=runtime_versions(), noise_levels=[dict(a=a, b=b) for a, b in NOISE])
        check_inference(data, dict(tools, **{'run_noisy_moment_pilot.py': 'archived-training-only-driver'}))
        wrong = dict(tools)
        wrong['noisy_moment_unet.py'] = 'other'
        with self.assertRaises(ValueError):
            check_inference(data, wrong)
        with self.assertRaises(ValueError):
            check_inference(dict(data, noise_levels=[]), tools)
        with self.assertRaises(ValueError):
            check_inference(dict(data, runtime={}), tools)


if __name__ == '__main__':
    unittest.main()
