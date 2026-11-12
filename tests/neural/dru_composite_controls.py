"""Prospective composite holdout, opened only after both candidate runs are frozen."""
import math
import torch
from torch.nn import functional as F
from bayer_canonical import canonicalize,spatial_reflect
from context_data import extend_observation
from confirm_fine_noise import observation
from dru_composite_data import background_case,reserved_signal
from synthetic_bayer_data import PATTERNS
from run_synthetic_joint_pilot import tensor_hash
from run_noisy_moment_pilot import NOISE


def reserve():
    cases=[];fields=[]
    for chromatic in (False,True):
        for draw in range(4):
            identity=870000000+int(chromatic)*100+draw
            draws=torch.rand(3,generator=torch.Generator().manual_seed(identity)).tolist()
            low,high=((4.,12.) if draw//2==0 else (12.,28.))
            for kind in ('flat','edge','coarse'):
                case=dict(identity=identity,frequency=math.exp(math.log(low)+draws[0]*math.log(high/low)),
                    frequency_bin=draw//2,amplitude=(.006,.018)[draw%2],angle=math.pi*draws[1],phase=2*math.pi*draws[2],
                    chromatic=chromatic,colours=([1.,-.7,.4] if chromatic else [1.,1.,1.]),
                    background=background_case(identity,kind,96.))
                truth,_,_,_=reserved_signal(case);case['target_sha256']=tensor_hash(truth)
                case_index=len(cases);cases.append(case)
                for phase in range(4):
                    for level in (1,2):
                        for replicate in (0,1):
                            field=dict(case_index=case_index,phase_index=phase,noise_index=level,replicate=replicate,
                                noise_seed=990000000+case_index*1000+phase*100+level*10+replicate)
                            _,values,sigma=observation(truth,torch.tensor([PATTERNS[phase]]),field)
                            field.update(observation_sha256=tensor_hash(values),sigma_sha256=tensor_hash(sigma));fields.append(field)
    if len(cases)!=24 or len(fields)!=384 or len({c['target_sha256'] for c in cases})!=24:raise ValueError('Full disjoint composite reserve required')
    return dict(cases=cases,fields=fields,noise_laws_balanced=NOISE,
        role='Prospectively sealed heldout for this experiment only; not independent of all project design decisions or photographic validation.',
        policy='Open model predictions once after both candidates complete; no selection of weights or recipe from this reserve.')


def response(values,truth,wave,cosine,unit,has_background):
    rows=[]
    for site in range(4):
        y,x=divmod(site,2)
        def samples(v):return v[y::2,x::2][32:-32,32:-32].double().flatten()
        w=samples(wave);q=samples(cosine);bg=samples(unit)
        columns=[torch.ones_like(w),w,q]
        if has_background:columns.append(bg)
        design=torch.stack(columns,dim=1)
        if int(torch.linalg.matrix_rank(design))!=len(columns):raise ValueError('Degenerate joint weak/background fit')
        inverse=torch.linalg.pinv(design)
        actual=values[0,site,32:-32,32:-32].double().flatten()
        clean=truth[0,site,32:-32,32:-32].double().flatten()
        measured=inverse@actual;ideal=inverse@clean
        if abs(float(ideal[1]))<1e-8:raise ValueError('Degenerate weak contrast')
        rows.append(dict(sine_gain=float(measured[1]/ideal[1]),quadrature_gain=float(measured[2]/ideal[1]),
            background_gain=float(measured[3]/ideal[3]) if has_background else None,
            dc_error=float(measured[0]-ideal[0]),mse=float((actual-clean).square().mean()),
            design_condition=float(torch.linalg.cond(design))))
    return rows


def evaluate(model,plan,check):
    rows=[];model.eval()
    with torch.inference_mode():
        for field in plan['fields']:
            check();case=plan['cases'][field['case_index']]
            truth,wave,cosine,unit=reserved_signal(case)
            if tensor_hash(truth)!=case['target_sha256']:raise ValueError('Reserved clean target changed')
            tags=torch.tensor([PATTERNS[field['phase_index']]])
            clean,observed,sigma=observation(truth,tags,field)
            if tensor_hash(observed)!=field['observation_sha256'] or tensor_hash(sigma)!=field['sigma_sha256']:raise ValueError('Reserved noisy field changed')
            expanded,_,_,_=reserved_signal(case,1024,-416)
            values,deviation=extend_observation(expanded,tags,observed,sigma,*NOISE[field['noise_index']],210000000+field['noise_seed'])
            packed,sd,labels,flags=canonicalize(values/2,deviation/2,tags)
            corrected=model.correct_observations(packed.cuda(),sd.cuda(),labels.cuda()).cpu()
            restored=F.pixel_unshuffle(spatial_reflect(F.pixel_shuffle(corrected,2),flags)*2,2)[:,:,208:304,208:304]
            rows.append(dict(**field,site_colours=tags[0].tolist(),
                corrected=response(restored,clean,wave,cosine,unit,case['background']['kind']!='flat'),
                observed=response(observed,clean,wave,cosine,unit,case['background']['kind']!='flat')))
    return rows
