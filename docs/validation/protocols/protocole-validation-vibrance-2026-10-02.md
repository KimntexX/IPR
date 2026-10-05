# Protocole de validation — Vibrance

**Date :** 2 octobre 2026  
**Corpus :** huit scènes neutres dans `tests/image/base/neutre` et deux variantes par scène dans `tests/image/base/vibrance`.

## Objectif

Valider que Vibrance agit dans les deux directions et que son effet est plus fort sur les pixels initialement peu saturés que sur les couleurs déjà vives, sans dérive notable de luminance.

## Fichiers

| Référence neutre | Lightroom | IPR |
|---|---|---|
| `6V1Axxxx-3.tif` | `6V1Axxxx.tif`, `6V1Axxxx-2.tif` | `6V1Axxxx-IPR.tiff`, `6V1Axxxx-IPR-2.tiff` |

Les deux variantes doivent reprendre les mêmes valeurs de Vibrance dans les deux logiciels. Tous les autres réglages, y compris Saturation, doivent rester à zéro.

## Mesures

La chrominance relative est `C = (max(R,G,B) - min(R,G,B)) / max(R,G,B)` en RGB linéaire. L'analyse mesure :

- la variation moyenne de `C` ;
- la variation dans les pixels peu saturés (`C < 0,25`) et déjà saturés (`C ≥ 0,50`) ;
- la variation relative `ΔC / C` dans ces deux groupes (en excluant les gris quasi parfaits, où le ratio est indéfini) ;
- le ratio de luminance ;
- le clipping.

`tests/analyse_vibrance.py` produit les mesures. La chrominance absolue varie naturellement davantage dans les couleurs déjà vives ; le comportement adaptatif est donc validé sur la **variation relative** : pour une Vibrance positive, elle doit être plus forte sur les basses chrominances, et les signes s'inversent pour une valeur négative.
