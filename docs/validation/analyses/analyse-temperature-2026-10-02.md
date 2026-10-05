# Analyse du réglage Température

**Date :** 2 octobre 2026  
**Corpus :** huit scènes de `tests/image/base/neutre` et deux extrêmes Lightroom/IPR de `tests/image/base/temperature`.

## Méthode

Chaque variante est comparée au TIFF neutre produit par le même moteur. Les TIFF sRGB sont sous-échantillonnés uniformément et convertis en RGB linéaire. Les mesures portent sur :

- les gains moyens des canaux R, G et B ;
- la variation moyenne de `log(R/B)`, axe robuste de déplacement chaud/froid ;
- le rapport de luminance moyenne ;
- le taux de pixels comportant au moins un canal écrêté.

La variante sans suffixe est identifiée comme **froide** par sa signature bleue, tandis que `-2` est **chaude**. La mesure brute est disponible dans `mesure-temperature-brut-2026-10-02.txt` et est reproductible avec `tests/analyse_temperature.py`.

## Résultats

| Moteur (8 scènes) | Variante froide : R / G / B | `Y` | `Δ log(R/B)` | Clipping | Variante chaude : R / G / B | `Y` | `Δ log(R/B)` | Clipping |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Lightroom | `0,432 / 1,121 / 4,535` | `1,103` | `-7,044` | `19,172 %` | `1,939 / 1,019 / 0,422` | `1,208` | `2,338` | `4,725 %` |
| IPR | `0,611 / 1,015 / 5,155` | `1,177` | `-3,730` | `18,375 %` | `1,522 / 0,986 / 0,317` | `1,053` | `2,004` | `1,389 %` |

## Diagnostic

Le sens des deux extrêmes est correct dans IPR : la variante froide diminue R et augmente fortement B, la variante chaude augmente R et diminue B. Le clipping froid est également comparable à Lightroom, ce qui confirme que le curseur a un effet réel et n'est pas bridé.

La réponse reste néanmoins asymétrique :

- **froid :** IPR atteint `Δ log(R/B) = -3,730`, soit environ `53 %` de l'amplitude Lightroom (`-7,044`) ; le bleu est même légèrement plus amplifié, mais l'atténuation du rouge est insuffisante (`0,611` contre `0,432`) ;
- **chaud :** IPR atteint `2,004`, soit environ `86 %` de Lightroom (`2,338`) ; la direction et l'ordre de grandeur sont utilisables, avec un clipping plus protecteur ;
- la luminance moyenne varie différemment de Lightroom, surtout dans le rendu chaud (`1,053` contre `1,208`). Cela est cohérent avec la matrice d'adaptation chromatique actuelle, qui ne normalise pas explicitement l'énergie Y après transformation.

## Verdict et recommandation

La température est **fonctionnelle mais non encore validée quantitativement** : le chaud est proche, le froid est trop peu rouge-supprimé. Il ne faut pas compenser par une correction globale du bleu, qui augmenterait inutilement le clipping déjà comparable à Lightroom.

La correction recommandée est une calibration asymétrique de la branche froide dans l'adaptation LMS/Bradford : renforcer sélectivement l'écart rouge/bleu lorsque la température cible est inférieure à la température de référence, tout en conservant la branche chaude actuelle. Après cette correction, rejouer les huit TIFF avant de passer à la Nuance, car Température et Nuance partagent la même étape d'adaptation chromatique.

## Correctif v2 appliqué

La recommandation a été implémentée dans `ColorPipeline.cpp`. La matrice Bradford reste le socle pour toute la plage, mais une matrice diagonale RGB est composée **uniquement** lorsque `température cible < température de référence`. Elle est interpolée continûment en logarithme des kelvins, de l'identité à la référence jusqu'aux coefficients `(R 0,70 ; G 1,10 ; B 0,88)` à 2000 K. Ces coefficients correspondent aux ratios agrégés nécessaires pour rapprocher l'extrême froid IPR mesuré de Lightroom (`0,611 → 0,432`, `1,015 → 1,121`, `5,155 → 4,535`).

La branche chaude n'est pas modifiée. Les seize TIFF IPR utilisés pour l'état avant correction sont archivés dans `tests/image/base/temperature/archive-ipr-pre-correction-v1-2026-10-02`. La prochaine exportation doit reprendre exactement les huit scènes et les deux extrêmes ; elle permettra de valider l'amplitude effective, le clipping et l'absence de régression chaude avant d'entamer la Nuance.

## Vérification v2 et calibration v3

Les huit exports IPR froids v2 ont été comparés le 2 octobre 2026. Le correctif va dans le bon sens : les gains passent de `(0,611 ; 1,015 ; 5,155)` à `(0,527 ; 1,071 ; 4,691)`, le clipping baisse de `18,375 %` à `17,663 %`, et `Δ log(R/B)` progresse de `-3,730` à `-4,063`. Cependant, la cible Lightroom reste `-7,044` : l'amplitude ne représente que `57,7 %` de la référence, contre `53,0 %` avant correction. **La température froide n'est donc pas encore validée.**

Une calibration v3, obtenue par interpolation entre les états avant/v2 et la cible Lightroom, remplace le point terminal v2 `(0,70 ; 1,10 ; 0,88)` par `(0,35 ; 1,19 ; 0,84)` à 2000 K. La fonction d'interpolation et la branche chaude sont inchangées. Les huit TIFF IPR froids v2 sont archivés dans `tests/image/base/temperature/archive-ipr-pre-correction-v2-2026-10-02`; il faut les régénérer avec l'exécutable v3 avant la validation finale. La mesure brute est conservée dans `mesure-temperature-v2-brut-2026-10-02.txt`.

## Vérification v3 — validation partielle

Les huit TIFF IPR froids v3 ont été mesurés après régénération. Le résultat global est désormais proche de Lightroom :

| Moteur | R / G / B | `Y` | `Δ log(R/B)` | Clipping |
|---|---:|---:|---:|---:|
| Lightroom froid | `0,4323 / 1,1213 / 4,5353` | `1,1027` | `-7,0436` | `19,172 %` |
| IPR froid v3 | `0,4664 / 1,1360 / 4,5323` | `1,1952` | `-4,6672` | `17,472 %` |

Les écarts des gains moyens sont contenus à `+7,9 %` en rouge, `+1,3 %` en vert et `-0,1 %` en bleu. Le clipping IPR reste inférieur de `1,700` point. C'est une amélioration nette par rapport à v2 (`Δ log(R/B) = -4,0630`) et au réglage initial (`-3,7297`).

La mesure locale `Δ log(R/B)` n'atteint cependant que `66,3 %` de l'amplitude Lightroom. La température froide est donc **validée fonctionnellement et globalement**, mais **pas encore validée à parité locale stricte**. Les moyennes RGB déjà très proches alors que l'axe local reste différent montrent qu'une quatrième amplification diagonale dégraderait probablement les tons moyens au lieu de corriger les zones sombres et saturées responsables de l'écart. La prochaine amélioration, si la parité Lightroom est recherchée, doit être une analyse par plages de luminance et une adaptation chromatique/gamut mapping dépendante de la luminance, plutôt qu'un nouveau gain global. La branche chaude demeure à contrôler visuellement ou avec ses exports existants, mais n'a subi aucune modification dans v2/v3.

La mesure brute v3 est disponible dans `mesure-temperature-v3-brut-2026-10-02.txt`.
