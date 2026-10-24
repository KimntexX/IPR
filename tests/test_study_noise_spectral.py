import copy
import unittest

from study_noise_spectral import FEATURE_GROUPS, classify, evaluate, fit_models


def tile(i, bin_id=8):
    return dict(plane=0, bin=bin_id, kind="pair-difference", signature=dict(valid=True,
                features={name: i*.001 for name in FEATURE_GROUPS["combined"]}))


def training():
    return [dict(a_sha256=str(i), split="train", tiles=[tile(j) for j in range(32)]) for i in range(2)]


class SpectralStudyTests(unittest.TestCase):
    def test_support_needs_two_pairs_and_64_tiles(self):
        pairs = training()
        self.assertEqual(len(fit_models(pairs)),4)
        self.assertFalse(fit_models(pairs[:1]))
        pairs[1]["tiles"].pop()
        self.assertFalse(fit_models(pairs))

    def test_heldout_and_single_frame_data_never_fit_the_model(self):
        pairs = training()
        baseline = fit_models(pairs)
        heldout = dict(a_sha256="unseen",split="heldout",tiles=[tile(10000) for _ in range(90)])
        self.assertEqual(fit_models(pairs+[heldout]),baseline)
        single = copy.deepcopy(pairs[0])
        single["a_sha256"]="single"
        for item in single["tiles"]:
            item["kind"]="single-a"
        self.assertEqual(fit_models(pairs+[single]),baseline)

    def test_outlying_descriptor_is_not_noise_compatible(self):
        model = fit_models(training())[0]
        self.assertTrue(classify(tile(16),model)["within_envelope"])
        self.assertFalse(classify(tile(10000),model)["within_envelope"])

    def test_missing_bin_and_degenerate_signature_abstain(self):
        scene=dict(tiles=[tile(1,3),tile(1,8)])
        scene["tiles"][1]["signature"]={"valid":False}
        evaluate([], [scene], [], fit_models(training()))
        for record in scene["tiles"]:
            self.assertTrue(all(not d["available"] for d in record["diagnostic"].values()))

    def test_nonfinite_descriptor_is_refused(self):
        bad=tile(0)
        bad["signature"]["features"]["log_fine_hh"]=float("nan")
        with self.assertRaises(ValueError):
            classify(bad,fit_models(training())[0])


if __name__ == "__main__":
    unittest.main()
