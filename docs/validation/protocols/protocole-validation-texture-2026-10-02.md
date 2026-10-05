# Protocole de validation — Texture

**Date :** 2 octobre 2026  
**Corpus :** huit scènes neutres dans `tests/image/base/neutre` et deux variantes par scène dans `tests/image/base/texture`.

## Objectif

Vérifier que Texture augmente ou diminue le micro-détail local, avec un effet limité sur les couleurs et la luminance moyenne. Texture doit rester distincte de Clarté, dont le rayon est plus large.

## Mesures

`tests/analyse_texture.py` extrait six zones de 512×512 pixels à leur définition native de chaque TIFF, les convertit en RGB linéaire et mesure sur la luminance :

- l'énergie du résidu à un flou gaussien 3×3, proxy du micro-détail ;
- l'énergie des gradients horizontaux et verticaux, proxy des contours ;
- le ratio de luminance, la variation moyenne de chrominance et le clipping.

Les variantes sans suffixe et `-2` doivent conserver tous les réglages hors Texture à zéro. Une variante doit augmenter les deux métriques de détail, l'autre les diminuer ; le ratio de luminance doit rester proche de `1` et la chrominance ne doit varier que marginalement.
