"""Derived SVG of diagnostic responses; canonical data remain in the input JSON."""
import argparse
from html import escape
import json
from pathlib import Path


def render(data):
    models = [("current", "IPR courant", "#666666"),
              ("A1_endpoint_rgb", "A1 extrémités RGB", "#1976d2"),
              ("A2_adaptive_oklch", "A2 OKLCh", "#00876c"),
              ("B_pr598_prophoto", "B PR598 ProPhoto", "#c35091")]
    out = ['<svg xmlns="http://www.w3.org/2000/svg" width="1100" height="1000" viewBox="0 0 1100 1000">',
           '<rect width="1100" height="1000" fill="white"/>',
           '<g font-family="sans-serif" font-size="12" fill="#222">',
           '<text x="35" y="28" font-size="20">Étude des modèles Couleur — médianes sur sept scènes</text>']
    for i, (_, label, colour) in enumerate(models + [("LR", "LR descriptif", "#b57600")]):
        x = 35 + i * 210
        out += [f'<path d="M{x},52 h22" stroke="{colour}" stroke-width="3"/>',
                f'<text x="{x+28}" y="56">{escape(label)}</text>']
    for col, feature in enumerate(("vibrance", "saturation")):
        rows = [r for r in data["aggregates"] if r["feature"] == feature]
        for row, zone in enumerate(("low_chroma", "high_chroma", None)):
            x0, y0, w, h = 85 + col * 540, 120 + row * 270, 430, 195
            curves = []
            for model, label, colour in models + [("LR", "LR descriptif", "#b57600")]:
                series = sorted([r for r in rows if r["model"] == ("current" if model == "LR" else model)], key=lambda r: r["value"])
                prefix = "LR" if model == "LR" else "candidate"
                points = [(r["value"], 100 * (r["zones"][zone][prefix + "_relative_delta_median"] if zone
                          else r[prefix + "_Y_ratio_median"] - 1)) for r in series]
                curves.append((colour, model == "LR", points))
            values = [v for _, _, pts in curves for _, v in pts] + [0]
            lo, hi = min(values), max(values)
            margin = max((hi-lo)*.08, .1)
            lo, hi = lo-margin, hi+margin
            def xy(x, y):
                return x0+(x+100)/200*w, y0+h-(y-lo)/(hi-lo)*h
            title = "ΔC/C chroma " + ("faible (%)" if row == 0 else "fort (%)") if zone else "Δ luminance Y (%)"
            out.append(f'<text x="{x0}" y="{y0-18}">{feature} — {escape(title)}</text>')
            for i in range(5):
                v = lo+(hi-lo)*i/4
                _, y = xy(0, v)
                out += [f'<path d="M{x0},{y:.2f} h{w}" stroke="#e0e0e0"/>',
                        f'<text x="{x0-8}" y="{y+4:.2f}" text-anchor="end">{v:.1f}</text>']
            for x in (-100, -50, 0, 50, 100):
                px, _ = xy(x, 0)
                out.append(f'<text x="{px}" y="{y0+h+20}" text-anchor="middle">{x}</text>')
            for colour, dashed, pts in curves:
                points = ' '.join(f'{px:.2f},{py:.2f}' for px, py in (xy(x, y) for x, y in pts))
                dash = ' stroke-dasharray="6 4"' if dashed else ''
                out.append(f'<polyline points="{points}" fill="none" stroke="{colour}" stroke-width="2"{dash}/>')
    out += ['<text x="35" y="942">Réponses propres à chaque moteur ; masques variables ; corrections optiques différentes. Pas de MAE inter-moteurs.</text>',
            f'<text x="35" y="966">Source : {escape(data["campaign_id"])}</text>', '</g></svg>']
    return '\n'.join(out) + '\n'


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("memory", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(render(json.loads(args.memory.read_text(encoding="utf-8"))), encoding="utf-8")
