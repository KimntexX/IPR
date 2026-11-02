import unittest
import torch

from bayer_canonical import predict
from closed_moment_unet import ClosedMomentUNet, TransposeConv
from prepare_closed_inference import prepare_inference
from joint_bayer_unet import sample_rgb
from synthetic_bayer_data import PATTERNS


class PreparedInferenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def test_prepared_copy_is_exact_for_signed_non_square_all_phases(self):
        torch.manual_seed(109)
        model = ClosedMomentUNet(4, True).eval()
        torch.nn.init.uniform_(model.head.weight, -.1, .1)
        torch.nn.init.uniform_(model.head.bias, -.1, .1)
        before = {n: v.clone() for n, v in model.state_dict().items()}
        random_state = torch.get_rng_state().clone()
        prepared = prepare_inference(model)
        self.assertTrue(torch.equal(random_state, torch.get_rng_state()))
        self.assertFalse(any(isinstance(m, TransposeConv) for m in prepared.modules()))
        self.assertFalse(any(p.requires_grad for p in prepared.parameters()))
        target = torch.rand(1, 3, 32, 40)*1.7-.3
        with torch.inference_mode():
            for values in PATTERNS:
                p = torch.tensor([values])
                packed = sample_rgb(target, p)
                args = (packed, torch.zeros_like(packed), p, True, True)
                self.assertTrue(torch.equal(predict(model, *args), predict(prepared, *args)))
        self.assertTrue(all(torch.equal(v, model.state_dict()[n]) for n, v in before.items()))

    def test_invalid_mode_and_nonfinite_weights_rejected(self):
        for model in (ClosedMomentUNet(4), ClosedMomentUNet(4).eval()):
            with self.assertRaises(ValueError):
                prepare_inference(model)
        model = ClosedMomentUNet(4, True).eval()
        with torch.no_grad():
            model.head.weight[0, 0, 0, 0] = float('nan')
        with self.assertRaises(ValueError):
            prepare_inference(model)


if __name__ == '__main__':
    unittest.main()
