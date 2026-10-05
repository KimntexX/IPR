# Protocole de validation — Clarté

**Date :** 2 octobre 2026  
**Corpus :** huit scènes neutres et deux extrêmes par scène dans `tests/image/base/clarte`.

## Objectif

Valider que Clarté agit sur le contraste local à moyenne échelle (rayon `sigma = 8` dans IPR), dans les deux sens, sans dérive chromatique ou d'exposition notable. Les ombres profondes et hautes lumières doivent être protégées.

## Mesures

`tests/analyse_clarity.py` analyse six zones natives de 512×512 pixels. Il mesure le ratio du résidu par rapport à un lissage local 17×17, les gradients, la luminance, la dérive de chrominance et le clipping. Les variantes doivent avoir des effets opposés et préserver la luminance moyenne ; une évaluation visuelle complémentaire reste nécessaire pour exclure les halos.
