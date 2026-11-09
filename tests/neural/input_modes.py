"""Network input modes of the noise-input comparison: sigma map, blind, or Anscombe domain.

- sigma_map: noisy planes and the sigma map in, x - sigma*F out (current DRU-CFA adapter).
- sigma_input: noisy planes and the sigma map in, plain x - F out (the map as
  information only, without the sigma-scaled output).
- blind: noisy planes only, x - F out; the network must infer the noise level itself.
- anscombe: generalized Anscombe transform with the measured per-plane law
  (variance a*y + b), so the noise has unit variance everywhere; the network
  sees a fixed multiple of z and removes noise in z, the algebraic inverse
  brings the estimate back. Training compares z estimates with the transform
  of the clean target, so the algebraic inverse (not the unbiased one) is the
  matching return path.
"""
import torch

from dru_cfa import DruCfaCorrector


MODES = ('sigma_map', 'sigma_input', 'blind', 'anscombe')
# Network input u = scale*z: unit noise in z becomes a fixed 0.05 in u for every ISO and tone.
ANSCOMBE_INPUT_SCALE = .05


def in_channels(mode):
    if mode not in MODES:
        raise ValueError('Unknown input mode')
    return 8 if mode in ('sigma_map', 'sigma_input') else 4


def anscombe(y, a, b):
    """Generalized Anscombe transform for variance a*y + b.

    Deep-shadow samples whose argument is negative (about two read-noise sigmas
    below black) keep a signed square root, so the transform stays strictly
    monotone and exactly invertible instead of collapsing them to zero.
    """
    argument = a*y+.375*a*a+b
    return 2/a*argument.sign()*argument.abs().sqrt()


def algebraic_inverse(z, a, b):
    half = a*z/2
    return (half.sign()*half.square()-.375*a*a-b)/a


class ModeCorrector(DruCfaCorrector):
    """Same DRUNet core and checkpointing as DruCfaCorrector; only the input and return path change."""

    def __init__(self, core, mode, checkpoint_stages=False):
        in_channels(mode)
        super().__init__(core, checkpoint_stages=checkpoint_stages)
        self.mode = mode

    def correct(self, noisy, sigma, pattern, a, b):
        """Corrected planes in the normalized sensor domain, plus z_hat for the Anscombe objective."""
        if self.mode == 'sigma_map':
            return dict(corrected=self.correct_observations(noisy, sigma, pattern), z_hat=None)
        if noisy.ndim != 4 or noisy.shape[1] != 4 or a.shape != (len(noisy), 4, 1, 1) or b.shape != a.shape:
            raise ValueError('Four-plane batch with per-sample per-plane noise law required')
        if not torch.isfinite(noisy).all() or (a <= 0).any() or (b < 0).any():
            raise ValueError('Finite observations and a positive noise law required')
        if self.mode in ('blind', 'sigma_input'):
            if self.mode == 'sigma_input' and (sigma.shape != noisy.shape or not torch.isfinite(sigma).all() or (sigma < 0).any()):
                raise ValueError('Finite nonnegative sigma map matching the planes required')
            values = noisy if self.mode == 'blind' else torch.cat((noisy, sigma), dim=1)
            corrected = noisy-self._residual(values)
            z_hat = None
        else:
            z = anscombe(noisy, a, b)
            z_hat = z-self._residual(ANSCOMBE_INPUT_SCALE*z)
            corrected = algebraic_inverse(z_hat, a, b)
        if not torch.isfinite(corrected).all():
            raise ValueError('Nonfinite sensor output')
        return dict(corrected=corrected, z_hat=z_hat)


def mode_loss(mode, objective, output, target, sigma, a, b, sigma_floor, objective_scale):
    from physical_training_objective import training_loss
    if mode == 'anscombe':
        if objective != 'anscombe_l1' or output['z_hat'] is None:
            raise ValueError('The Anscombe input trains with the Anscombe-domain L1')
        return objective_scale*(output['z_hat']-anscombe(target, a, b)).abs().mean()
    if objective == 'anscombe_l1':
        raise ValueError('Anscombe-domain L1 needs the Anscombe input')
    return training_loss(output['corrected'], target, sigma, objective, sigma_floor, objective_scale)


def oracle_alpha(noisy, corrected, clean):
    """Least-squares amplitude missing from the correction (diagnosis only, uses the clean target)."""
    d = (noisy-corrected).double().flatten()
    y = (noisy-clean).double().flatten()
    return float(d@y/(d@d).clamp_min(1e-30))
