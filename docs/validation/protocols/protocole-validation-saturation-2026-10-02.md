# Protocole de validation — Saturation

**Date :** 2 octobre 2026  
**Corpus :** huit scènes neutres dans `tests/image/base/neutre` et deux variantes par scène dans `tests/image/base/saturation`.

## Objectif

Vérifier que Saturation réduit puis augmente la chrominance dans les deux directions, autour de la luminance, sans le comportement adaptatif propre à Vibrance.

## Mesures

La chrominance relative est `C = (max(R,G,B) - min(R,G,B)) / max(R,G,B)` en RGB linéaire. `tests/analyse_saturation.py` compare chaque export à son neutre propre et rapporte :

- variation moyenne de chrominance ;
- variation relative de chrominance dans les groupes peu saturé (`0,02 ≤ C < 0,25`) et très saturé (`C ≥ 0,50`) ;
- ratio de luminance et clipping.

Les deux variantes doivent être produites avec les mêmes valeurs Lightroom/IPR, tous les autres réglages à zéro. Les signes doivent être opposés. Les variations relatives des deux groupes peuvent différer à cause du gamut de sortie, mais ne doivent pas traduire la protection délibérée des couleurs saturées propre à Vibrance.
