import unittest
import torch
from sensor_transfer_math import site_response
from confirm_fine_noise import fine_signal
from joint_bayer_unet import sample_rgb
from synthetic_bayer_data import PATTERNS


class SensorTransferMathTests(unittest.TestCase):
    def test_all_bayer_phases_recover_known_attenuation_at_physical_sites(self):
        truth,wave,cosine=fine_signal(dict(frequency=26,amplitude=.06,angle=.63,phase=1.17,chromatic=True))
        for phase in PATTERNS:
            clean=sample_rgb(truth,torch.tensor([phase]))
            for gain in (1.,.75):
                changed=.3+(clean-.3)*gain
                for row in site_response(changed,clean,wave,cosine):
                    self.assertAlmostEqual(row['sine_gain'],gain,places=6)
                    self.assertAlmostEqual(row['quadrature_gain'],0.,places=6)


if __name__=='__main__':unittest.main()
