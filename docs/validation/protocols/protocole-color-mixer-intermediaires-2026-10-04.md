# Mélangeur huit bandes — valeurs intermédiaires

**Date :** 4 octobre 2026. **Objet :** vérifier la progressivité après la
[campagne des extrêmes](protocole-validation-color-mixer-2026-10-04.md), sans
confondre fonctionnement, stabilité de luminance et parité Lightroom.

## Corpus et convention

Chaque série contient douze variantes par application. Le numéro 1 est le nom
sans suffixe ; les suivants portent `-2` à `-12`. Lightroom : `.tif` ; IPR :
`-IPR.tiff` ou `-IPR-N.tiff`. **Ne pas interpréter ces fichiers avec le mode
historique à six variantes ±100.**

| Paramètre seul modifié | −50 | −25 | +25 | +50 |
| --- | --- | --- | --- | --- |
| Teinte | 1 | 2 | 3 | 4 |
| Saturation | 5 | 6 | 7 | 8 |
| Luminance | 9 | 10 | 11 | 12 |

- Rouge/orange/jaune/cyan/bleu : `6V1A0327`.
- Vert : `6V1A0037`.
- Violet/magenta : `6V1A0043`.
- Neutres de chaque application : `tests/image/base/neutre/`.
- Les douze IPR verts supplémentaires de `6V1A0327` ne constituent pas une
  série Lightroom/IPR appariée : aucune variante Lightroom correspondante.

Le moteur déclaré est le mélangeur huit bandes après ADR-0005, sans changement
de calcul pendant cette campagne. Les exports IPR ne tracent pas leurs
réglages dans des métadonnées équivalentes à Camera Raw : l'identité de leurs
états repose sur la convention fournie par l'opérateur.

## Contrôles avant calcul

1. Inventorier les 192 variantes principales et leurs six neutres : dimensions,
   profondeur, orientation, ICC et compression.
2. Vérifier dans le XMP Lightroom le réglage exact, tous les autres HSL à zéro
   et les paramètres hors mélangeur identiques au neutre.
3. Utiliser le neutre **de la même application**, sans prendre Lightroom comme
   zéro d'IPR. Un neutre absent/interdit empêche la validation relative à zéro.
4. Ne pas modifier les photos, les sidecars ou les mesures historiques. Écrire
   de nouvelles mesures versionnées ; la campagne ±100 est archivée séparément.

## Mesures

Lecture native TIFF 16 bits, échantillonnage de pixels sans interpolation,
décodage analytique sRGB vers RGB linéaire, Y Rec.709 et HSV linéaire. Même
masque fixe que précédemment : teinte Lightroom neutre à ±15° du centre,
saturation ≥0,15 et 0,01<Y<0,9. Le nombre de pixels doit accompagner la conclusion.

Le recadrage de comparaison absorbe uniquement l'écart d'un pixel de dimensions
entre applications. Il est choisi par signature de contours échantillonnée ;
ce n'est pas un recalage subpixel démontré. La MAE et ΔE76 restent descriptifs.

Pour chaque composante/application, ordonner `−50, −25, 0, +25, +50` et relever :

- teinte : moyenne des différences angulaires circulaires au neutre ;
- saturation : moyenne de S−S₀ ;
- luminance : rapport des sommes de Y à Y₀, éventuellement exprimé en EV ;
- paires extrêmes de cette campagne : réponse −50→+50 et identité des pixels ;
- conservation de Y pour teinte/saturation, variation hors support nominal,
  clipping et MAE de l'effet après soustraction des neutres propres.

La monotonie des **moyennes de zone** est un contrôle fonctionnel, pas une
garantie pixel par pixel ni une exigence de linéarité. Tolérances de calcul :
0,05° pour H, 0,0001 pour ΔS et le rapport Y. Une inversion au voisinage du
neutre ou un résidu non nul doit être expliqué plutôt que masqué. Une faible
couverture, un XMP erroné ou un neutre absent ne peuvent valider une commande.
La progressivité et l'identité des états ne suffisent pas à valider la qualité
visuelle, les transitions entre couleurs ou la concordance aperçu/export.

## Reproduction

Depuis la racine, Python avec NumPy/Pillow ; utiliser un **nouveau** nom de
sortie pour chaque passe :

```bash
python3 tests/analyse_color_mixer.py red orange yellow cyan blue --levels intermediate --ipr-mixer-version eight --analyse --output NOUVELLE-MESURE-0327.json
python3 tests/analyse_color_mixer.py green --image-id 6V1A0037 --levels intermediate --ipr-mixer-version eight --analyse --output NOUVELLE-MESURE-0037.json
python3 tests/analyse_color_mixer.py violet magenta --image-id 6V1A0043 --levels intermediate --ipr-mixer-version eight --analyse --output NOUVELLE-MESURE-0043.json
bash scripts/build-windows.sh
```

Sans `--analyse`, la commande fait seulement l'inventaire/audit. Les neutres
sont recherchés à la racine du corpus puis dans `neutre/` ; `--root` permet de
relire l'archive historique en gardant son mode `extremes`.

## Sortie et décision

Publier les inventaires et mesures immuables, les écarts au neutre, le résultat
des 24 progressions IPR et leurs limites dans
[l'analyse dédiée](../analyses/analyse-color-mixer-intermediaires-2026-10-04.md).
Ne pas modifier une calibration du moteur sur la seule base de moyennes
photographiques. Distinguer un contrôle fonctionnel réussi d'un résidu de
rendu à investiguer.
