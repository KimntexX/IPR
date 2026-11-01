"""Original teaching examples: scalar gradients, XOR, then optional tiny U-Net.

No photos, network access, saved weights or output files. Default: standard
library only. --torch explicitly runs a small synthetic CPU demonstration.
Not a training recipe or qualified denoiser for IPR.
"""
import argparse
import math
import random


def linear_demo():
    samples = [(-1., -1.), (0., 1.), (1., 3.), (2., 5.)]
    w, b = 0., 0.
    for _ in range(1000):
        dw = db = 0.
        for x, target in samples:
            error = w*x+b-target
            dw += 2*error*x/len(samples)
            db += 2*error/len(samples)
        w -= .05*dw
        b -= .05*db
    assert abs(w-2) < 1e-6 and abs(b-1) < 1e-6
    print(f'Linear: w={w:.8f}, b={b:.8f}')


def gradient_check():
    x, target, w, b = 2., 5., 2., 2.
    def loss(weight):
        return .5*(weight*x+b-target)**2
    analytic = (w*x+b-target)*x
    epsilon = 1e-5
    numeric = (loss(w+epsilon)-loss(w-epsilon))/(2*epsilon)
    assert abs(analytic-numeric) < 1e-7
    print(f'Gradient: analytic={analytic:.8f}, finite difference={numeric:.8f}')


def sigmoid(z):
    # Avoid overflow on either tail.
    if z >= 0:
        return 1/(1+math.exp(-z))
    ez = math.exp(z)
    return ez/(1+ez)


def xor_demo():
    rng = random.Random(17)
    data = [([0., 0.], 0.), ([0., 1.], 1.), ([1., 0.], 1.), ([1., 1.], 0.)]
    count = 8
    weights = [[rng.uniform(-1, 1) for _ in range(2)] for _ in range(count)]
    biases = [rng.uniform(-.2, .2) for _ in range(count)]
    output_weights = [rng.uniform(-1, 1) for _ in range(count)]
    output_bias = 0.
    def forward(x):
        hidden = [math.tanh(sum(a*b for a,b in zip(row,x))+bias)
                  for row,bias in zip(weights,biases)]
        logit = sum(a*b for a,b in zip(output_weights,hidden))+output_bias
        return hidden, sigmoid(logit)
    for _ in range(3000):
        gw = [[0.,0.] for _ in range(count)]
        gb = [0.]*count
        gv = [0.]*count
        gc = 0.
        for x,target in data:
            hidden,prediction = forward(x)
            delta = (prediction-target)/len(data)  # BCE + sigmoid derivative
            gc += delta
            for j in range(count):
                gv[j] += delta*hidden[j]
                dh = delta*output_weights[j]*(1-hidden[j]**2)
                gb[j] += dh
                for k in range(2):
                    gw[j][k] += dh*x[k]
        for j in range(count):
            biases[j] -= .5*gb[j]
            output_weights[j] -= .5*gv[j]
            for k in range(2):
                weights[j][k] -= .5*gw[j][k]
        output_bias -= .5*gc
    predictions = [forward(x)[1] for x,target in data]
    assert all((p > .8 if y else p < .2) for p,(_,y) in zip(predictions,data))
    print('XOR:',[round(p,5) for p in predictions])


def torch_demo():
    try:
        import torch
        from torch import nn
        from torch.nn import functional as F
    except ImportError as error:
        raise SystemExit('PyTorch absent: use an isolated environment or omit --torch') from error
    torch.set_num_threads(2)
    torch.set_num_interop_threads(1)
    torch.manual_seed(17)
    torch.use_deterministic_algorithms(True)
    def block(inputs,outputs):
        return nn.Sequential(nn.Conv2d(inputs,outputs,3,padding=1),nn.SiLU(),
                             nn.Conv2d(outputs,outputs,3,padding=1),nn.SiLU())
    class TinyUNet(nn.Module):
        def __init__(self):
            super().__init__()
            self.e1=block(6,8)
            self.e2=block(8,16)
            self.middle=block(16,32)
            self.d2=block(48,16)
            self.d1=block(24,8)
            self.head=nn.Conv2d(8,3,1)
            nn.init.zeros_(self.head.weight)
            nn.init.zeros_(self.head.bias)
        def forward(self,image,sigma):
            a=self.e1(torch.cat((image,sigma),dim=1))
            b=self.e2(F.avg_pool2d(a,2))
            c=self.middle(F.avg_pool2d(b,2))
            d=self.d2(torch.cat((F.interpolate(c,size=b.shape[2:],mode='nearest'),b),1))
            e=self.d1(torch.cat((F.interpolate(d,size=a.shape[2:],mode='nearest'),a),1))
            return image-self.head(e)
    model=TinyUNet()
    axis=torch.linspace(-.1,1.2,32)
    clean=axis[None,None,None,:].expand(1,3,32,32).clone()
    sigma=torch.full_like(clean,.05)
    noisy=clean+sigma*torch.randn_like(clean)
    original=noisy.clone()
    assert torch.equal(model(noisy,sigma),noisy)
    optimizer=torch.optim.Adam(model.parameters(),lr=.002)
    initial=((noisy-clean)**2).mean().item()
    model.train()
    for _ in range(32):
        optimizer.zero_grad(set_to_none=True)
        loss=(model(noisy,sigma)-clean).square().mean()
        loss.backward()
        assert all(p.grad is None or torch.isfinite(p.grad).all() for p in model.parameters())
        optimizer.step()
    model.eval()
    with torch.no_grad():
        restored=model(noisy,sigma)
        final=(restored-clean).square().mean().item()
    assert torch.equal(original,noisy)
    assert torch.isfinite(restored).all() and restored.shape==clean.shape
    assert final < initial
    print('U-Net CPU:',sum(p.numel() for p in model.parameters()),'parameters')
    print(f'Single-patch MSE: {initial:.8f} -> {final:.8f}')
    print('Teaching-only memorization check, NOT photographic validation; no weights saved.')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--torch',action='store_true',help='also run the bounded synthetic CPU U-Net example')
    args=parser.parse_args()
    linear_demo()
    gradient_check()
    xor_demo()
    if args.torch:
        torch_demo()
    print('All requested teaching checks passed; no files written.')


if __name__=='__main__':
    main()
