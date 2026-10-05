# Analyse du réglage Texture

**Date :** 2 octobre 2026  
**Corpus :** huit scènes TIFF sRGB neutres, deux extrêmes Lightroom/IPR dans `tests/image/base/texture`.

## Méthode

Texture étant un traitement de micro-détail, la mesure ne sous-échantillonne pas l'image entière. Six zones de 512×512 pixels à leur définition native sont prélevées par TIFF. Sur la luminance linéaire, deux indicateurs sont calculés :

- énergie du résidu après un flou 3×3, pour le micro-détail ;
- énergie des gradients horizontaux et verticaux, pour les contours locaux.

Les ratios sont mesurés par rapport au TIFF neutre du même moteur. Luminance moyenne, dérive de chrominance et clipping complètent l'analyse. Le script est `tests/analyse_texture.py` et les données brutes sont dans `mesure-texture-brut-2026-10-02.txt`.

## Résultats

| Moteur | Variante négative : détail | Gradients | `Y` | Δ chrominance | Clipping | Variante positive : détail | Gradients | `Y` | Δ chrominance | Clipping |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Lightroom | `0,515` | `0,511` | `0,979` | `0,0202` | `0,812 %` | `1,448` | `1,433` | `1,033` | `0,0218` | `0,900 %` |
| IPR | `0,446` | `0,558` | `0,999` | `0,0075` | `0,346 %` | `1,687` | `1,570` | `1,001` | `0,0075` | `0,351 %` |

## Verdict

Texture est **validée fonctionnellement et globalement** :

- les deux variantes ont l’effet attendu et de sens opposé ;
- IPR diminue le micro-détail à `44,6 %` au minimum et le renforce à `168,7 %` au maximum ;
- la luminance IPR reste stable à moins de `0,1 %`, alors que les exports Lightroom du corpus varient davantage ;
- les rapports RGB sont bien préservés (`Δ chrominance 0,0075`) et le clipping est plus protecteur que Lightroom.

IPR est légèrement plus énergique que Lightroom dans le renforcement positif et dans la suppression du résidu fin ; cette différence reste cohérente avec le rayon plus court (`sigma = 1,2`) et ne traduit ni dérive colorimétrique ni défaut fonctionnel. La comparaison doit rester effectuée à résolution native : une mesure après réduction d'image sous-estime artificiellement le réglage Texture.
