# Analyse de la correction du voile

**Date :** 2 octobre 2026  
**Corpus :** huit scènes TIFF sRGB neutres et deux extrêmes Lightroom/IPR dans `tests/image/base/correction_de_voile`.

## Méthode

La comparaison est faite en RGB linéaire, par rapport au TIFF neutre propre à chaque moteur. Elle porte sur les ratios des percentiles de luminance 5 %, 50 % et 95 %, le contraste interpercentile `P95 - P05`, la chrominance moyenne et le clipping. Les mesures sont produites par `tests/analyse_dehaze.py` et conservées dans `mesure-correction-voile-brut-2026-10-02.txt`.

## Résultats

| Moteur | Variante positive : P05 / P50 / P95 | Contraste | Δ chrominance | Clipping | Variante négative : P05 / P50 / P95 | Contraste | Δ chrominance | Clipping |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Lightroom | `13,841 / 2,852 / 1,415` | `1,214` | `-0,236` | `5,741 %` | `0,253 / 0,607 / 0,851` | `0,867` | `+0,222` | `1,987 %` |
| IPR | `1,296 / 1,279 / 1,151` | `1,140` | `-0,085` | `1,864 %` | `0,662 / 0,651 / 0,776` | `0,783` | `+0,183` | `1,925 %` |

Les percentiles bas Lightroom sont très proches du noir dans les références, ce qui explique les ratios élevés au dévoilement positif ; ils doivent être lus avec le contraste et le clipping, et non isolément.

## Verdict

La correction du voile est **validée fonctionnellement et globalement** :

- l'extrême positif augmente le contraste dans les deux moteurs (`1,140` dans IPR) et rétablit les valeurs faibles ;
- l'extrême négatif réduit le contraste (`0,783` dans IPR) et ajoute l'effet atmosphérique attendu ;
- les variations de chrominance ont le même sens que Lightroom ;
- IPR garde le clipping sous `1,93 %`, sensiblement sous l'extrême positif Lightroom (`5,74 %`).

IPR est volontairement plus conservateur dans la restitution des ombres : la transmission est bornée et l'airlight est estimé globalement, afin d'éviter noir écrasé, couleur instable et bruit excessif. Une validation visuelle complémentaire sur des paysages avec brume dense peut être menée plus tard ; aucun défaut fonctionnel ne ressort du corpus actuel.
