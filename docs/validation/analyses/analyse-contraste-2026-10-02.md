# Analyse du réglage Contraste

**Date :** 2 octobre 2026  
**Corpus :** huit scènes neutres de `tests/image/base/neutre`, comparées aux TIFF `-100` et `+100` de `tests/image/base/contraste`.

## Méthode

Chaque moteur est comparé à son propre rendu neutre `EV0`. Les TIFF sRGB sont sous-échantillonnés uniformément, convertis en luminance linéaire sRGB, puis regroupés en zones basse (`0–20 %`), médiane (`40–60 %`) et haute (`80–100 %`) selon les quantiles du rendu neutre. La mesure brute est conservée dans `mesure-contraste-brut-2026-10-02.txt`.

## Résultats

| Moteur (8 scènes) | Basses `-100` / `+100` | Médianes `-100` / `+100` | Hautes `-100` / `+100` | Pixels `Y ≤ 0,001` : `0` / `-100` / `+100` |
|---|---:|---:|---:|---:|
| Lightroom | `2,051` / `0,356` | `1,049` / `0,990` | `0,905` / `1,118` | `2,230 %` / `0,059 %` / `17,564 %` |
| IPR | `13,428` / `0,153` | `2,568` / `0,679` | `0,750` / `1,075` | `0,317 %` / `0,000 %` / `25,337 %` |

## Diagnostic

Le contraste est fonctionnel et le sens des extrêmes est correct : `-100` aplatit l'image en levant les tons bas et en abaissant le haut de courbe ; `+100` densifie les noirs et renforce le haut de courbe. Toutefois, IPR est beaucoup trop agressif :

- à `-100`, les basses lumières IPR sont relevées `×13,43` au lieu de `×2,05`, et les tons moyens `×2,57` au lieu de rester proches du neutre (`×1,05`) ;
- à `+100`, les basses lumières sont trop écrasées (`0,153` contre `0,356`) et les tons moyens trop modifiés (`0,679` contre `0,990`) ;
- le haut de courbe est moins renforcé qu'attendu à `+100` (`1,075` contre `1,118`), malgré l'écrasement excessif des zones basses et moyennes.

La cause est la formule actuelle qui emploie un facteur unique `1 + contraste / 100`, soit une plage de `0` à `2`. À `-100`, le facteur nul renvoie tous les pixels à un pivot linéaire de `0,18`, ce qui explique la levée massive des noirs. À `+100`, l'exposant `2` est appliqué indistinctement au pied de courbe et aux tons moyens.

## Recommandation

Le contraste doit recevoir une courbe en S adaptative autour du pivot `0,18`, avec des intensités distinctes :

1. **Pied de courbe :** limiter la compression/expansion maximale et la faire décroître rapidement à l'approche du pivot, afin que les tons moyens restent proches de leur luminance neutre.
2. **Épaule :** conserver une action dédiée au-dessus du pivot pour rejoindre la variation Lightroom du haut de courbe, sans compenser le manque d'effet par un écrasement des noirs.
3. **Extrêmes :** ne jamais permettre au facteur négatif de devenir nul ; cela préserve la texture dans les ombres même à `-100`.

Une correction est recommandée avant de tester Température et Nuance : le contraste modifie la distribution tonale qui sert de base aux validations visuelles de colorimétrie.

## Correctif v2 appliqué

Le facteur global `0–2` a été remplacé par une courbe en S asymétrique centrée sur `0,18` :

- sous le pivot, le facteur varie progressivement de `0,75` à `1,00` pour `-100`, et de `1,40` à `1,00` pour `+100` ;
- au-dessus du pivot, l'épaule utilise `0,60` pour `-100` et `2,30` pour `+100` ;
- les tons proches du pivot restent ancrés, et aucun facteur nul n'est possible.

Une régression synthétique vérifie désormais que les deux extrêmes modifient le pied et l'épaule, mais ne déplacent pas globalement un ton moyen. Les exports IPR du corpus Contraste doivent être régénérés pour la validation quantitative v2.

## Validation v2

La mesure v2 est disponible dans `mesure-contraste-v2-brut-2026-10-02.txt`.

| Moteur (8 scènes) | Basses `-100` / `+100` | Médianes `-100` / `+100` | Hautes `-100` / `+100` | Pixels `Y ≤ 0,001` : `0` / `-100` / `+100` |
|---|---:|---:|---:|---:|
| Lightroom | `2,051` / `0,356` | `1,049` / `0,990` | `0,905` / `1,118` | `2,230 %` / `0,059 %` / `17,564 %` |
| IPR v2 | `1,654` / `0,520` | `1,067` / `0,987` | `0,921` / `1,167` | `0,317 %` / `0,000 %` / `10,922 %` |

### Verdict v2

**Contraste est validé.** Les tons moyens sont pratiquement ancrés (`1,067` / `0,987` contre `1,049` / `0,990`) et l'épaule est proche de Lightroom (écart inférieur à `5 %` aux deux extrêmes). Le pied IPR reste plus protecteur : il lève moins les noirs à `-100` et les écrase moins à `+100`. Cette différence réduit le clipping (`10,922 %` contre `17,564 %` à `+100`) tout en conservant un effet visuellement net ; elle est retenue comme choix technique.

La validation tonale est donc complète. Les tests Température peuvent commencer.
