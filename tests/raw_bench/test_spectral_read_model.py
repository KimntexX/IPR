import unittest
import numpy as np
from spectral_read_model import prepare,draw
from audit_signal_noise_bridge import PHASES


class SpectralReadContracts(unittest.TestCase):
    def test_real_deterministic_fields_and_expected_phase_variance(self):
        rng=np.random.default_rng(81023)
        reads=[rng.normal(size=(256,256))*3 for _ in range(3)]
        context=dict(phases={phase:dict(b=9.) for phase in PHASES})
        model=prepare(reads,context)
        expected=np.mean(np.sum(np.abs(model['root'])**2,axis=-1),axis=(0,1))
        np.testing.assert_allclose(expected,[9.]*4,rtol=1e-12)
        np.testing.assert_array_equal(draw(model,1,1600),draw(model,1,1600))
        values=[]
        for seed in range(24):
            sample=draw(model,seed,1600)
            values.append([sample[dy::2,dx::2].var() for dy,dx,_ in PHASES.values()])
        np.testing.assert_allclose(np.mean(values,axis=0),[9.]*4,rtol=.04)

    def test_common_phase_correlation_is_preserved(self):
        rng=np.random.default_rng(81024);reads=[]
        for _ in range(3):
            shared=rng.normal(size=(128,128));mosaic=np.empty((256,256))
            for dy,dx,_ in PHASES.values():mosaic[dy::2,dx::2]=shared
            reads.append(mosaic)
        model=prepare(reads,dict(phases={p:dict(b=1.) for p in PHASES}))
        sample=draw(model,1,1600)
        self.assertGreater(np.corrcoef(sample[::2,::2].ravel(),sample[1::2,1::2].ravel())[0,1],.99999)

    def test_no_spectrum_from_single_frame_or_zero_variance(self):
        context=dict(phases={p:dict(b=1.) for p in PHASES})
        for reads in ([np.ones((256,256))],[np.ones((256,256))]*3):
            with self.assertRaises(ValueError):prepare(reads,context)


if __name__=='__main__':unittest.main()
