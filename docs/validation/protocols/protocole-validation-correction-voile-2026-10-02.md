# Protocole de validation — Correction du voile

**Date :** 2 octobre 2026  
**Corpus :** huit scènes neutres dans `tests/image/base/neutre`, avec deux extrêmes dans `tests/image/base/correction_de_voile`.

## Objectif

Vérifier que la correction de voile augmente le contraste pour une valeur positive et le réduit pour une valeur négative, sans écrêtage ou dérive chromatique excessive.

## Mesures

`tests/analyse_dehaze.py` compare chaque TIFF à son neutre propre, en RGB linéaire, sur :

- les ratios des percentiles de luminance 5 %, 50 % et 95 % ;
- le ratio du contraste interpercentile `P95 - P05` ;
- la variation moyenne de chrominance ;
- le clipping.

Les deux variantes doivent utiliser les mêmes valeurs Lightroom/IPR, avec tous les autres réglages neutres. Le contrôle est validé si les directions sont opposées, le contraste évolue dans le bon sens et les extrêmes restent bornés.
