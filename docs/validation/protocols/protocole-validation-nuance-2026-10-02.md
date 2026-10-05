# Protocole de validation — Nuance vert–magenta

**Date :** 2 octobre 2026  
**Corpus :** les huit scènes neutres de `tests/image/base/neutre` et les deux variantes de `tests/image/base/nuance`.

## Objectif

Vérifier que le curseur Nuance IPR déplace les couleurs vers le vert pour une valeur négative et vers le magenta pour une valeur positive, avec une amplitude comparable aux exports Lightroom, sans dérive excessive de luminance ni écrêtage.

## Fichiers comparés

Chaque variante est comparée au TIFF neutre du même moteur :

| Référence neutre | Variante Lightroom | Variante IPR |
|---|---|---|
| `6V1Axxxx-3.tif` | `6V1Axxxx.tif`, `6V1Axxxx-2.tif` | `6V1Axxxx-IPR.tiff`, `6V1Axxxx-IPR-2.tiff` |

Les deux variantes doivent correspondre aux mêmes valeurs de Nuance dans Lightroom et IPR, avec tous les autres réglages neutres, notamment Température sur *As Shot*, exposition `0`, contraste `0`, réglages tonals `0`, saturation `0` et vibrance `0`.

## Mesures

- gains moyens RGB en sRGB linéaire ;
- variation de `log(√(R×B)/G)`, négative vers le vert et positive vers le magenta ;
- ratio de luminance moyenne ;
- taux de pixels avec au moins un canal écrêté.

La comparaison est produite par `tests/analyse_tint.py`. La direction est validée si les deux variantes ont des signes opposés et cohérents entre Lightroom et IPR ; l'amplitude et le clipping déterminent la validation quantitative.
