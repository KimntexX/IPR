"""Derived figures from the canonical exploratory noise-transfer JSON."""
import argparse
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    data = json.loads(args.input.read_bytes())
    args.output_dir.mkdir(parents=True, exist_ok=True)
    models = ('affine', 'dark_fixed', 'relative_affine', 'robust_relative', 'quadratic')
    labels = ('Affine absolu', 'Noir fixé', 'Affine relatif', 'Relatif robuste', 'Quadratique')
    figures, axes = plt.subplots(2, 3, figsize=(15, 8), constrained_layout=True)
    groups = sorted(set(r['folder'] for r in data['planes']), key=lambda s: int(s.split('-')[1]))
    for axis, group in zip(axes.flat, groups):
        records = [r for r in data['planes'] if r['folder'] == group]
        for kind, label in zip(models, labels):
            values = [100*r['models'][kind]['validation']['pair_id']['relative_mae'] for r in records]
            axis.plot([r['plane'] for r in records], values, marker='o', label=label)
        axis.set_title(group.upper())
        axis.set_ylabel('Erreur relative moyenne de variance (%)')
        axis.grid(alpha=.25)
    axes.flat[0].legend(fontsize=8)
    figures.suptitle('Validation interne : une paire entière retirée à chaque fois\n' + data['campaign_id'])
    figures.savefig(args.output_dir / 'comparaison-modeles.svg')
    plt.close(figures)
    figures, axes = plt.subplots(2, 3, figsize=(15, 8), constrained_layout=True)
    colors = ('#c0392b', '#229954', '#17a589', '#2471a3')
    for axis, group in zip(axes.flat, groups):
        for r, color in zip([r for r in data['planes'] if r['folder'] == group], colors):
            x = np.array([p['signal_dn'] for p in r['points']])
            y = np.array([p['variance_dn2'] for p in r['points']])
            axis.scatter(x, y, s=12, alpha=.65, color=color)
            model = r['models']['relative_affine']['fitted']
            signal = np.geomspace(x.min(), x.max(), 100)
            axis.plot(signal, model['a']*signal+model['b'], color=color, label=r['plane'])
            axis.scatter([x.min()*.6], [r['dark_variance_dn2']], marker='x', color=color)
        axis.set(xscale='log', yscale='log', title=group.upper(), xlabel='Signal utile (DN)', ylabel='Variance (DN²)')
        axis.grid(alpha=.25)
    axes.flat[0].legend(fontsize=8)
    figures.suptitle('Points retenus et affine relatif ; croix : variance des noirs (abscisse déplacée pour affichage)\n'
                     + data['campaign_id'])
    figures.savefig(args.output_dir / 'signal-variance.svg')
    plt.close(figures)


if __name__ == '__main__':
    main()
