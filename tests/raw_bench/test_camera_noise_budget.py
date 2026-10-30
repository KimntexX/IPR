import copy
import unittest
import numpy as np
from camera_noise_budget import prepare_context,phase_budget,mosaic,simulate_context,local_mean,profile_key,band_covariances
from study_context_transfer import fixture,contexts,GUIDE_SEED,CONTROL_SEEDS,PATTERNS
from study_cfa_signal_transport import CALIBRATION_SEEDS


def catalog():
    return dict(verdict='audited_frozen_CFA_law_and_signed_domain_not_production_noise_profile',models=[
        dict(camera='Canon EOS R6 Mark III',iso=1600,phase=p,slope_dn=3.,fit_signal_range_dn=[0.,100.],
             regions=[dict(roi=1,temporal_dark_variance_dn2=4.,reference_black_dn=2048.)]) for p in ('R','G1','G2','B')])


def applied():return dict(gains=[2.,1.,3.,4.],black=[2050.,2050.,2050.,2050.],white=16383.,divisor=10000.,policy='signed')


class CameraBudgetContracts(unittest.TestCase):
    def setUp(self):self.context=prepare_context(catalog(),'a'*64,'EOS R6 Mark III',1600,applied())

    def test_no_camera_or_iso_borrowing(self):
        for camera,iso in (('EOS R7',1600),('EOS 60D',1600),('EOS R6 Mark III',1601),('EOS R6 Mark III',1600.5)):
            with self.assertRaises(ValueError):prepare_context(catalog(),'a'*64,camera,iso,applied())

    def test_normalized_variance_and_black_origin(self):
        mean=np.array([.01,.02,.03]);budget=phase_budget(self.context,mean)
        for phase,colour in (('R',0),('G1',1),('G2',1),('B',2)):
            p=self.context['phases'][phase]
            self.assertAlmostEqual(budget[phase]['variance_balanced'],p['A']*mean[colour]+p['B'])
        self.assertEqual(self.context['phases']['G2']['gain'],4.)
        self.assertEqual(self.context['phases']['B']['gain'],3.)

    def test_fixed_dn_gain_square_and_divisor_inverse_square(self):
        raw_signal=30.;variance=4.+3.*raw_signal
        for p in self.context['phases'].values():
            mean=p['gain']*(raw_signal+2048.-p['runtime_black'])/self.context['divisor']
            self.assertAlmostEqual(p['A']*mean+p['B'],variance*(p['gain']/self.context['divisor'])**2)

    def test_negative_mean_retained_but_variance_floor_physical(self):
        context=prepare_context(catalog(),'a'*64,'EOS R6 Mark III',1600,{**applied(),'black':[2048.]*4})
        sample=simulate_context(context,np.full((256,256),-.01),np.zeros((256,256)),np.random.default_rng(1),'gaussian')
        np.testing.assert_array_equal(sample,np.full_like(sample,-.01))
        self.assertTrue(all(v['below_zero_latent'] for v in phase_budget(context,[-.01]*3).values()))

    def test_key_separates_context_and_reference(self):
        first=profile_key(self.context,[.03]*3,'read','tool',(513,),'gaussian')
        other=copy.deepcopy(self.context);other['gains'][0]+=1
        self.assertNotEqual(first,profile_key(other,[.03]*3,'read','tool',(513,),'gaussian'))
        self.assertNotEqual(first,profile_key(self.context,[.03]*3,'other','tool',(513,),'gaussian'))
        self.assertNotEqual(first,profile_key(self.context,[.03]*3,'read','tool',(513,),'gaussian',(96,32,64,64)))

    def test_guide_sees_only_mean_not_high_frequency_variance(self):
        observed=np.full((256,256,3),.03)
        np.testing.assert_allclose(local_mean(observed,(96,96,64,64)),[.03]*3,atol=1e-15)
        with self.assertRaises(ValueError):local_mean(observed,(250,250,64,64))

    def test_fixtures_and_seed_partitions(self):
        self.assertFalse(set(CONTROL_SEEDS)&set(CALIBRATION_SEEDS))
        self.assertNotIn(GUIDE_SEED,CONTROL_SEEDS)
        for iso in (1600,51200):
            for name in PATTERNS:
                rgb=fixture(iso,name)
                self.assertTrue(np.isfinite(rgb).all());self.assertGreater(rgb.min(),0)
                self.assertEqual(mosaic(rgb).shape,(256,256))

    def test_constant_has_zero_wavelet_covariance(self):
        matrices=band_covariances(np.full((256,256,3),-.03),(96,96,64,64))
        self.assertEqual(matrices.shape,(5,3,3));self.assertLess(float(np.abs(matrices).max()),1e-25)

    def test_invalid_normalization_fails(self):
        for change in ({'divisor':0.},{'black':[17000.]*4},{'gains':[0.,1.,1.,1.]},{'policy':'parity'}):
            with self.assertRaises(ValueError):prepare_context(catalog(),'a'*64,'EOS R6 Mark III',1600,{**applied(),**change})

    def test_context_witnesses_do_not_mutate_capture_metadata(self):
        manifest=dict(applied=applied(),metadata=dict(model='EOS R6 Mark III',iso=1600))
        original=copy.deepcopy(manifest)
        variants=contexts(catalog(),'a'*64,manifest)
        self.assertEqual(manifest,original)
        self.assertEqual(variants['WB_witness']['gains'][0],2.4)
        self.assertEqual(variants['normalization_witness']['black'][0],2054.)


if __name__=='__main__':unittest.main()
