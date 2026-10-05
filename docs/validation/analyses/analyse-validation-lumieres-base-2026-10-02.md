# Analyse initiale — corpus de validation des lumières

**Date :** 2 octobre 2026  
**Corpus :** `tests/image/base`

## Verdict

Le corpus convient pour commencer la validation. Il contient huit séries complètes : RAW CR3, TIFF Lightroom à `EV -1` et `EV +1`, TIFF IPR correspondants et sidecar XMP. La convention fournie est retenue : le TIFF sans suffixe numérique est `EV -1`, le fichier suffixé `-2` est `EV +1` et le suffixe `-3` est réservé au futur `EV 0`; les autres réglages sont à zéro.

Deux éléments sont à connaître avant une comparaison pixel-à-pixel définitive :

1. LibRaw expose une zone active inférieure d'un pixel en largeur **et** en hauteur à la zone conservée par Lightroom ; le comparateur IPR recadre désormais la référence sans interpolation ;
2. il manque une référence exportée `EV 0`.

## Inventaire et traçabilité

Les séries `6V1A0007`, `6V1A0043`, `6V1A0767`, `6V1A9073`, `6V1A9119`, `6V1A9734`, `6V1A9944` et `6V1A9947` sont complètes.

Les XMP indiquent pour leur état actuellement enregistré : profil `Adobe Standard`, balance des blancs `As Shot`, `Exposure2012="+1.00"`, courbe `Linear`, contraste, hautes lumières, ombres, blancs, noirs, texture, clarté, voile, vibrance et saturation à `0`.

Cela confirme les réglages neutres et l'état `+1 EV` des exports `-2`. Un XMP unique ne conserve cependant pas la preuve du réglage `-1 EV` : il faudra un XMP ou un manifeste par état pour les prochaines séries.

## Compatibilité des fichiers

| Producteur | Données | Profil ICC | Dimensions |
|---|---|---|---|
| Lightroom | RGB 16 bits | Adobe RGB (1998) | `4640 × 6960` ou `6960 × 4640` |
| IPR | RGBA 16 bits | sRGB | `4639 × 6959` ou `6959 × 4639` |

L'écart d'un pixel par axe est systématique sur les huit TIFF IPR. L'analyse des RAW a confirmé que ce n'est pas une troncature à l'écriture TIFF : les huit CR3 ont une taille brute `7144 × 4760` et LibRaw en expose une zone active `6959 × 4639`, tandis que Lightroom rend `6960 × 4640`.

Le correctif est dans `tests/tonal_reference_comparison_probe.cpp` : si la référence dépasse exactement le rendu d'un pixel sur chaque axe, elle est recadrée, sans redimensionnement. Les quatre bords possibles sont testés sur un échantillon et le bord qui minimise l'erreur d'alignement est retenu. La MAE et la superposition sont donc calculées sur des pixels réels, sans pixel artificiel ni interpolation globale. Le test de rendu associé passe sous WSL (`114,56 s`).

## Méthode

- Aucun fichier source n'a été modifié.
- Chaque TIFF a été réduit à un aperçu de maximum `384 × 576` pixels.
- Les aperçus Lightroom Adobe RGB et IPR sRGB ont été convertis par ICC vers sRGB, puis décodés en sRGB linéaire.
- La luminance relative est `Y = 0,2126R + 0,7152G + 0,0722B`.
- La MAE est la différence RGB absolue moyenne, entre `0` et `1`, entre l'aperçu IPR et Lightroom au même EV.

La passe est diagnostique : la conversion d'aperçu travaille à 8 bits après réduction. Elle compare les tendances, sans remplacer une mesure ICC 16 bits à pleine résolution.

## Résultats

| Image | LR : Y `-1` → `+1` | facteur LR | IPR : Y `-1` → `+1` | facteur IPR | MAE `-1` | MAE `+1` |
|---|---:|---:|---:|---:|---:|---:|
| 6V1A0007 | 0,1620 → 0,5982 | 3,693 | 0,0615 → 0,2659 | 4,326 | 0,1076 | 0,3352 |
| 6V1A0043 | 0,0107 → 0,0625 | 5,839 | 0,0067 → 0,0275 | 4,112 | 0,0072 | 0,0361 |
| 6V1A0767 | 0,0335 → 0,1929 | 5,761 | 0,0234 → 0,0917 | 3,919 | 0,0178 | 0,1057 |
| 6V1A9073 | 0,0892 → 0,4075 | 4,567 | 0,0468 → 0,1904 | 4,068 | 0,0556 | 0,2505 |
| 6V1A9119 | 0,0127 → 0,1092 | 8,615 | 0,0108 → 0,0440 | 4,090 | 0,0059 | 0,0686 |
| 6V1A9734 | 0,0680 → 0,3040 | 4,470 | 0,0277 → 0,1069 | 3,860 | 0,0382 | 0,1833 |
| 6V1A9944 | 0,4029 → 0,6511 | 1,616 | 0,1596 → 0,4914 | 3,078 | 0,2517 | 0,1599 |
| 6V1A9947 | 0,1617 → 0,4579 | 2,831 | 0,0520 → 0,2098 | 4,034 | 0,1214 | 0,2446 |

## Lecture des résultats

De `EV -1` à `EV +1`, l'écart est de deux stops. Sans saturation ni courbe de rendu, une lumière linéaire doit donc être multipliée par `2² = 4`.

Les facteurs IPR vont de `3,078` à `4,326`, pour une moyenne géométrique d'environ `3,92`. La réponse est donc proche de `2^EV`, avec compression sur les scènes lumineuses : c'est encourageant pour la sémantique du curseur IPR.

Les facteurs Lightroom sont plus dispersés (`1,616` à `8,615`, moyenne géométrique environ `4,22`). Ceci est attendu avec un rendu de base et une courbe de tonalité : la moyenne d'une image ne suit plus forcément un facteur quatre lorsqu'ombres et hautes lumières sont traitées différemment.

IPR est généralement plus sombre que Lightroom. La MAE moyenne vaut `0,0757` à `EV -1` et `0,1730` à `EV +1`. Cet écart accru à haute exposition est cohérent avec une différence de rendu de base, de compression des hautes lumières et de profils d'entrée/sortie. Il ne désigne pas IPR comme incorrect : les références n'ont ni le même profil de sortie (Adobe RGB contre sRGB), ni nécessairement la même courbe de développement.

## Actions recommandées

1. Exporter deux à quatre paires supplémentaires à `EV 0`, par exemple `6V1A0007-LR-EV0.tif` et `6V1A0007-IPR-EV0.tiff`.
2. Conserver un manifeste ou sidecar par état : RAW, application/version, profil, espace de sortie, WB, correction d'objectif, EV, crop et redimensionnement.
3. Conserver cette politique de comparaison : recadrer la référence Lightroom d'un pixel pour les CR3 concernés ; ne pas ajouter un pixel synthétique au TIFF IPR.
4. Utiliser le même espace ICC 16 bits pour les deux applications lors des mesures ; garder les JPEG sRGB en parallèle pour le visuel.
5. Après stabilisation des dimensions, mesurer à pleine résolution en 16 bits : MAE, clipping et profils de luminance par zone d'intérêt.

## Relance avec EV 0 — 2 octobre 2026

Les nouveaux TIFF Lightroom `-3` sont tous des TIFF RGB 16 bits sRGB avec ICC incorporé. Les TIFF Lightroom `EV -1` et `EV +1` déjà présents restent Adobe RGB (1998), mais leurs ICC sont incorporés et ont été convertis vers sRGB pour les mesures. Pour un protocole entièrement homogène, ils pourront être réexportés en sRGB lors d'une passe ultérieure ; ce n'est pas bloquant pour cette analyse.

Les huit triplets Lightroom/IPR sont maintenant complets et ont été mesurés.

| Image | IPR : Y `-1` → `0` → `+1` | facteur `0/-1` | facteur `+1/0` | MAE IPR/LR `-1` / `0` / `+1` |
|---|---:|---:|---:|---:|
| 6V1A0007 | 0,0615 → 0,1279 → 0,2659 | 2,081 | 2,078 | 0,1076 / 0,2323 / 0,3352 |
| 6V1A0043 | 0,0067 → 0,0134 → 0,0275 | 2,010 | 2,046 | 0,0072 / 0,0168 / 0,0361 |
| 6V1A0767 | 0,0234 → 0,0465 → 0,0917 | 1,987 | 1,973 | 0,0178 / 0,0435 / 0,1057 |
| 6V1A9073 | 0,0468 → 0,0954 → 0,1904 | 2,038 | 1,995 | 0,0556 / 0,1375 / 0,2505 |
| 6V1A9119 | 0,0108 → 0,0218 → 0,0440 | 2,027 | 2,018 | 0,0059 / 0,0212 / 0,0686 |
| 6V1A9734 | 0,0277 → 0,0557 → 0,1069 | 2,012 | 1,918 | 0,0382 / 0,0933 / 0,1833 |
| 6V1A9944 | 0,1596 → 0,3195 → 0,4914 | 2,001 | 1,538 | 0,2517 / 0,2144 / 0,1599 |
| 6V1A9947 | 0,0520 → 0,1045 → 0,2098 | 2,010 | 2,007 | 0,1214 / 0,2173 / 0,2446 |

La moyenne géométrique du premier stop IPR est `2,020`, donc pratiquement la loi attendue `2^EV`. Le second stop a une moyenne géométrique `1,939`, abaissée principalement par `6V1A9944` : cette scène est très lumineuse et compresse déjà fortement dans Lightroom (`0,5361 → 0,6511`, facteur `1,214`). En retirant cette scène saturée, les sept autres valeurs IPR sont proches de deux.

La MAE moyenne IPR/Lightroom est `0,0757` à `EV -1`, `0,1220` à `EV 0` et `0,1730` à `EV +1`. L'écart de rendu global demeure donc un sujet séparé de la justesse du curseur EV : la réponse d'exposition IPR est validée sur ce corpus, tandis que le rendu de base, les profils et la compression des hautes lumières devront être comparés zone par zone.

## Conclusion

Le curseur d'exposition IPR est **validé** sur ce corpus : autour de `EV 0`, chaque stop suit la loi attendue `2^EV` (moyennes géométriques `×2,020` puis `×1,939`, la seconde étant comprimée par une scène déjà saturée). Sa formule ne doit pas être modifiée dans la prochaine phase.

La banque est suffisamment riche pour poursuivre. L'alignement de dimensions est traité dans le comparateur et les trois positions EV sont disponibles pour les mesures pleine résolution et les superpositions. Les TIFF `EV -1` et `EV +1` sont archivés séparément ; les RAW, XMP et TIFF `EV 0` restent dans `tests/image/base` pour la prochaine recette noirs/ombres à `-100`, `0` et `+100`.
