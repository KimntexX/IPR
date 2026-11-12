"""Exploratory predeclared gates, not photographic/perceptual tolerances."""
import math


def gate_config():
    return dict(overall_mse_ratio_max=.98,overall_opponent_ratio_max=1.,edge_mse_ratio_max=1.,
        edge_opponent_ratio_max=1.,gradient_gain_fraction_min=.95,
        simple_signal_mse_max=1e-6,wave_extra_mse_max=1e-6,observed_site_mse_max=1e-6)


def qualifies(result,config):
    if not result.get('records'):raise ValueError('Nonempty reconstruction measurements required')
    checks={}
    le=lambda value,bound:isinstance(value,(int,float)) and math.isfinite(value) and value<=bound
    ge=lambda value,bound:isinstance(value,(int,float)) and math.isfinite(value) and value>=bound
    summary=result['summary'];families=result['by_family']
    checks['overall_rgb_gain']=le(summary['mse_ratio'],config['overall_mse_ratio_max'])
    checks['overall_opponent_not_worse']=le(summary['opponent_mse_ratio'],config['overall_opponent_ratio_max'])
    edge=families['edge']
    checks['edge_rgb_not_worse']=le(edge['mse_ratio'],config['edge_mse_ratio_max'])
    checks['edge_opponent_not_worse']=le(edge['opponent_mse_ratio'],config['edge_opponent_ratio_max'])
    checks['edge_gradient_preserved']=ge(edge['output_gradient_gain'],
        edge['baseline_gradient_gain']*config['gradient_gain_fraction_min'])
    for family in ('constant','ramp'):
        checks[family+'_absolute_fidelity']=le(families[family]['output_mse'],config['simple_signal_mse_max'])
    for family in ('weak_achromatic_wave','chromatic_wave'):
        row=families[family]
        checks[family+'_absolute_fidelity']=le(row['output_mse'],row['baseline_mse']+config['wave_extra_mse_max'])
        checks[family+'_gradient_preserved']=ge(row['output_gradient_gain'],
            row['baseline_gradient_gain']*config['gradient_gain_fraction_min'])
    measured=result['records']
    checks['observed_sensor_sites_preserved']=le(sum(r['observed_site_mse'] for r in measured)/len(measured),
        config['observed_site_mse_max'])
    return dict(checks=checks,passed=all(checks.values()),
        decision='eligible_for_separate_synthetic_noise_stage' if all(checks.values()) else 'stop_before_noise_stage')
