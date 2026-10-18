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

Cette conclusion est celle de la campagne du 2 octobre. La revalidation sur le
pipeline actuel est présentée ci-dessous ; les anciens TIFF Adobe Standard ont
été supprimés et ne doivent pas être mélangés avec cette nouvelle campagne.

La banque est suffisamment riche pour poursuivre. L'alignement de dimensions est traité dans le comparateur et les trois positions EV sont disponibles pour les mesures pleine résolution et les superpositions. Les TIFF `EV -1` et `EV +1` sont archivés séparément ; les RAW, XMP et TIFF `EV 0` restent dans `tests/image/base` pour la prochaine recette noirs/ombres à `-100`, `0` et `+100`.

## Revalidation de l'exposition — Camera Neutral, 9 octobre 2026

### Corpus et protocole

Six scènes : 6V1A0007, 6V1A0037, 6V1A0043, 6V1A0327, 6V1A9421 et
6V1A9950. Dans `tests/image/base/exposition`, les variantes sans suffixe
numérique correspondent à −1 EV et les `-2` à +1 EV. Cette correspondance
est confirmée par le XMP Lightroom ; côté IPR, elle repose sur les noms et
la réponse mesurée, car aucun manifeste d'export ne certifie les réglages.
Les neutres IPR utilisés sont les fichiers `neutre/<image>-IPR.tiff` ; les
neutres LR sont les `<image>-LR-CameraNeutral-AsShot.tif` conservés.

Audit : TIFF RGB/RGBA 16 bits non compressés, ICC sRGB. Les douze variantes LR
indiquent Camera Neutral, WB As Shot et EV −1/+1. Cinq neutres LR sont
disponibles ; celui de **6V1A0037 manque**. Les TIFF IPR ne permettent pas
de certifier le DCP, la WB ou les options optiques employés.

Les calculs lisent les valeurs natives 16 bits, sans conversion 8 bits,
redimensionnement ni interpolation : un pixel toutes les huit lignes/colonnes,
soit **504 600 échantillons par export**. Les mesures ne sont donc pas exhaustives.
Linéarisation sRGB, puis Y = 0,2126 R + 0,7152 G + 0,0722 B. Pour la loi EV,
on retient les pixels dont tous les canaux neutres sont entre 0,005 et 0,45,
avec canaux de variante inférieurs à 0,98. Le facteur attendu est `2^EV`.
L'erreur mesure `mean(abs(RGB_variante − RGB_neutre × 2^EV))` sur ce masque.

Chaque variante est comparée au neutre de **son moteur**, avec dimensions
identiques. Aucun MAE spatial direct IPR/LR n'est calculé : leur géométrie
diffère d'un pixel et les options optiques ne sont pas toutes tracées.
La proximité visuelle entre moteurs et la justesse physique restent distinctes.

### Résultats IPR

| Scène | Facteur médian −1 EV | Facteur médian +1 EV | MAE linéaire −1 / +1, pixels retenus | Pixels avec un canal saturé à +1 |
|---|---:|---:|---:|---:|
| 6V1A0007 | 0,500000 | 2,000000 | 0,00000210 / 0,00000629 | 14,62 % |
| 6V1A0037 | 0,500000 | 2,000000 | 0,00000102 / 0,00000305 | 2,49 % |
| 6V1A0043 | 0,500000 | 2,000000 | 0,00000129 / 0,00000388 | 0,69 % |
| 6V1A0327 | 0,500000 | 2,000000 | 0,00000150 / 0,00000449 | 0,002 % |
| 6V1A9421 | 0,500000 | 2,000000 | 0,00000175 / 0,00000523 | 13,02 % |
| 6V1A9950 | 0,500000 | 2,000000 | 0,00000138 / 0,00000410 | 17,61 % |

Les douze variantes ne présentent aucun échantillon inversant la direction
attendue au-delà de la tolérance Y de 0,00002. La réponse non saturée est
pratiquement exacte ; les erreurs résiduelles sont compatibles avec la
quantification 16 bits. Les moyennes globales peuvent s'écarter de ×2 à cause
des limites de sortie ; elles ne remplacent pas la mesure non saturée.

### Lightroom : réponse différente, sans imposer une correction EV

Pour les quatre scènes LR sans écart détecté de réglages hors EV :

| Scène | Facteur médian −1 EV | Facteur médian +1 EV | Canal saturé +1 |
|---|---:|---:|---:|
| 6V1A0007 | 0,3334 | 2,2075 | 0,0002 % |
| 6V1A0043 | 0,2403 | 3,0554 | 0,0182 % |
| 6V1A0327 | 0,2556 | 2,9364 | 0,0004 % |
| 6V1A9950 | 0,2275 | 3,2047 | 0 % |

Ces facteurs sont observés **dans le TIFF final linéarisé**, pas au niveau du
signal capteur. Même avec une courbe utilisateur déclarée Linear, le rendu
final Lightroom n'est pas nécessairement une simple multiplication de ce
TIFF par `2^EV`. Les données ne permettent pas d'attribuer l'écart à un
algorithme interne précis. Il ne faut pas recalibrer le curseur IPR pour
reproduire ces ratios, ni interpréter ceux-ci comme une erreur Lightroom.

**6V1A9421 LR est exclue de la comparaison contrôlée :** les deux variantes
indiquent `LensProfileEnable=1` et `AutoLateralCA=1`, contre 0 pour le neutre.
Les mesures diagnostiques sont conservées dans le JSON/CSV, sans verdict EV.
Les comparaisons pixel à pixel sur cette scène sont donc confondues avec
un changement géométrique/optique. Aucun ratio LR n'est mesuré pour 0037
sans sa référence EV0.

### Conclusion et suite

**Exposition IPR revalidée sur les six scènes à −1/+1 EV**, pour la loi et la
direction du réglage. Aucun correctif du code de production n'est nécessaire.
La ligne `adjusted *= exp2(settings.brightness)` du moteur correspond aux
mesures. Cela ne certifie ni la parité Lightroom, ni la restauration par
sidecar, ni l'ergonomie du curseur dans l'IHM.

La saturation de canaux à +1 EV est plus fréquente dans IPR sur certaines
scènes. C'est un sujet de rendu/gamut/compression de hautes lumières à étudier
avec les tests Blancs/HL, pas une raison de changer la loi physique d'exposition.
Un canal saturé peut perdre de la couleur sans que la luminance soit blanche.

À fournir pour compléter LR :

1. `neutre/6V1A0037-LR-CameraNeutral-AsShot.tif`, EV0 et autres réglages à zéro.
2. Réexporter `exposition/6V1A9421.tif` (−1) et `6V1A9421-2.tif` (+1),
   avec correction objectif et CA désactivées, comme leur neutre.
3. Pour les nouvelles campagnes, conserver un manifeste IPR avec DCP, WB,
   optique, normalisation et version du build ; maintenir ces états constants.

### Données et reproductibilité

- [Mesures et audits JSON](../raw/mesure-exposition-camera-neutral-20261009.json).
- [Mesures CSV](../raw/mesure-exposition-camera-neutral-20261009.csv).
- [Analyseur](../../../tests/analyse_exposition.py) et
  [tests ciblés](../../../tests/test_analyse_exposition.py) : cinq tests réussis.

Depuis la racine du dépôt, sous WSL :

```bash
python3 tests/analyse_exposition.py --output docs/validation/raw/mesure-exposition-camera-neutral-20261009.json
python3 -m unittest discover -s tests -p test_analyse_exposition.py
```

Cette commande remplace uniquement les mesures de cette campagne ; elle ne
modifie aucune image. Aucun build applicatif n'est nécessaire pour ces mesures.

## Campagne complète Lumières — nouveaux neutres, 9 octobre 2026

### Audit actualisé et portée

Les nouveaux neutres sont disponibles : `neutre/6V1A0037.tif` est utilisé
comme référence LR 0037 ; le neutre LR 9421 a été remplacé. Celui-ci indique
maintenant correction objectif et CA activées (`LensProfileEnable=1`,
`AutoLateralCA=1`), comme **toutes ses variantes**. Le défaut de cohérence
signalé dans la première passe est donc résolu sans réexporter les variantes.
Les autres scènes restent cohérentes avec leurs propres options optiques.

**144 variantes analysées**, soit six scènes × six réglages × deux extrêmes
× deux moteurs, plus douze neutres. Aucune image manquante, aucun avertissement
dans l'audit XMP effectué. LR : Camera Neutral, WB As Shot, autres contrôles
lumineux/couleur/détail vérifiés à zéro et options constantes dans chaque triplet.
IPR : réglages inférés des noms ; le TIFF ne certifie pas le profil ou les options
du développement. Absence d'avertissement ne signifie pas traçabilité équivalente.

La convention reste −1/+1 pour l'exposition et −100/+100 pour les autres
réglages (sans suffixe / `-2`). Mesures natives 16 bits, échantillonnage au pas de
8×8, 504 600 pixels par fichier. Trois zones sont définies **sur le neutre de
chaque moteur** : les 20 % les plus sombres, les quantiles 40–60 %, les 20 %
les plus clairs. Ces zones ne représentent pas forcément les mêmes pixels
dans IPR et LR, ni des tons proches du pivot physique 18 %.

Le script contrôle les valeurs testées, les autres curseurs et les différences
de réglages XMP vis-à-vis du neutre. Il n'analyse pas exhaustivement les masques,
le color grading, les courbes point par point ou l'historique avant export.
Pas de MAE spatial IPR/LR, pas de recalage géométrique, pas de certification
de parité Lightroom. À seulement deux extrêmes, la continuité/progressivité
sur toute la plage n'est pas démontrée.

### Exposition : revalidation complète

Les six scènes IPR conservent ×0,5/×2 en pixels non saturés, sans inversion.
Les références LR des six scènes sont maintenant disponibles et cohérentes.
Leur réponse finale est non linéaire, comme lors de la première passe : ce
n'est pas une justification pour modifier `2^EV` dans IPR.

### Synthèse des autres réglages

Les valeurs ci-dessous sont les **médianes des six ratios de moyennes par
scène**. Elles ne sont ni des moyennes pondérées par pixels ni des facteurs
universels. Un ratio élevé dans les noirs peut être dominé par un neutre
presque nul ; lire aussi les deltas absolus dans les CSV.

| Réglage / moteur | Basses −100 / +100 | Médianes −100 / +100 | Hautes −100 / +100 |
|---|---:|---:|---:|
| Contraste IPR | 3,196 / 0,163 | 1,787 / 0,412 | 0,852 / 1,392 |
| Contraste LR | 2,860 / 0,126 | 2,183 / 0,251 | 0,953 / 1,082 |
| Ombres IPR | 0,468 / 44,302 | 0,542 / 4,529 | 0,935 / 1,046 |
| Ombres LR | 0,360 / 3,298 | 0,613 / 1,719 | 0,969 / 1,032 |
| Noirs IPR | 0,000 / 6,222 | 0,034 / 2,458 | 0,971 / 1,005 |
| Noirs LR | 0,000 / 2,231 | 0,002 / 2,292 | 0,649 / 1,198 |
| Blancs IPR | 1,000 / 1,000 | 1,000 / 1,100 | 0,841 / 2,396 |
| Blancs LR | 0,988 / 3,316 | 0,954 / 5,443 | 0,859 / 2,653 |
| Hautes lumières IPR | 1,000 / 1,000 | 1,000 / 1,010 | 0,747 / 1,896 |
| Hautes lumières LR | 0,875 / 1,150 | 0,786 / 1,266 | 0,522 / 1,838 |

Tous les contrôles IPR unidirectionnels (exposition, ombres, noirs, blancs,
HL) ont **0 % d'inversions mesurées**, avec tolérance Y = 0,00002.
Le contraste n'est volontairement pas soumis à ce critère : une courbe en S
doit agir en directions opposées selon la luminance.

### Saturation et amplitude : principaux points à améliorer

Un « pixel saturé » signifie ici **au moins un canal sRGB à 65535** (tolérance
d'un pas 16 bits), et non nécessairement un pixel blanc ni un capteur saturé.
Ces taux décrivent la sortie TIFF, sans quantifier le détail récupérable du RAW.
Moyennes arithmétiques des six taux par scène :

| Réglage positif | IPR, canal saturé | LR, canal saturé |
|---|---:|---:|
| Exposition +1 | 8,072 % | 0,217 % |
| Contraste +100 | 0,291 % | 1,105 % |
| Ombres +100 | 0,004 % | 0,026 % |
| Noirs +100 | 0,003 % | 0,058 % |
| Blancs +100 | **18,877 %** | **4,746 %** |
| Hautes lumières +100 | **5,622 %** | **0,311 %** |

**Priorité 1 — Blancs/HL positifs et sortie couleur.** Blancs +100 atteint
41,97 % de canaux saturés sur 0007, 32,07 % sur 9950, 31,16 % sur 9421.
HL +100 atteint 16,71 % sur 9950 et 11,53 % sur 9421. Le moteur modifie la
luminance puis multiplie RGB par `Y_cible/Y_source` ; une cible Y bornée ne
garantit pas que tous les canaux restent ≤1. Le code est compatible avec
une saturation accrue, mais ces seules mesures ne prouvent pas que ce soit
l'unique cause. Investiguer gamut et épaule tonale avec une sonde des valeurs
avant encodage/clipping ; comparer −50/+50 avant de changer les amplitudes.
Ne pas réduire la loi EV pour masquer le problème.

**Priorité 2 — Ombres +100.** Le gain médian ×44 dans les basses n'est pas
un facteur global d'exposition : les références IPR sont très sombres dans
ces zones. Le delta Y des basses vaut néanmoins **+0,05847 en moyenne IPR**,
contre **+0,01635 LR** ; celui des tons moyens +0,04679 contre +0,02579.
Sur 0043, les basses passent à ×167,59, mais avec delta absolu +0,06313.
Le comportement est directionnellement correct, sans saturation importante,
mais l'ouverture des ombres à l'extrême mérite une recette visuelle à 100 %
et des valeurs intermédiaires avant de retenir cette amplitude comme choix.

**Noirs et Contraste.** Noirs IPR reste concentré en pied de courbe et n'est
pas équivalent au contrôle LR plus étendu. À −100, 50,81 % des pixels IPR
en moyenne atteignent Y≤0,001 ; l'extrême peut volontairement écraser les
basses, mais demande confirmation visuelle. Le contraste augmente/diminue
correctement la séparation des zones. Les « médianes » de ces scènes sombres
ne sont pas le gris 18 % ; leur forte variation ne prouve pas que le pivot
soit déplacé. Vérifier le pivot sur rampe synthétique plutôt que sur quantiles.

### Verdict et procédure suivante

- **Exposition : loi et direction revalidées.** Aucun correctif nécessaire.
- **Contraste et Noirs : fonctionnement cohérent aux extrêmes**, amplitude
  et rendu à contrôler visuellement ; pas de parité LR annoncée.
- **Ombres : direction correcte**, amplitude positive à affiner/valider.
- **Blancs et HL : direction et ciblage cohérents**, qualité de sortie positive
  à revalider pour saturation des canaux. Les verdicts du 2 octobre restent
  historiques et ne certifient pas ce nouveau corpus/pipeline.

Aucune modification du moteur de production lors de cette analyse. Préparer
une investigation Blancs/HL/gamut, puis Ombres +25/+50/+75 ; conserver les
profils, WB et corrections optiques constants. Les nouveaux neutres sont
suffisants : pas besoin de nouvelles scènes pour ce premier affinage.

### Preuves, courbes et relance

- [Audit et mesures des 144 variantes](../raw/mesure-lumieres-camera-neutral-20261009.json).
- [CSV par scène/contrôle](../raw/mesure-lumieres-camera-neutral-20261009.csv).
- [CSV de synthèse](../metrics/lumieres-camera-neutral-20261009/resume-lumieres-camera-neutral-20261009.csv).
- Courbes de réponse : [Exposition](../metrics/lumieres-camera-neutral-20261009/lumieres-exposition-camera-neutral-20261009.svg),
  [Contraste](../metrics/lumieres-camera-neutral-20261009/lumieres-contraste-camera-neutral-20261009.svg),
  [Ombres](../metrics/lumieres-camera-neutral-20261009/lumieres-ombre-camera-neutral-20261009.svg),
  [Noirs](../metrics/lumieres-camera-neutral-20261009/lumieres-noir-camera-neutral-20261009.svg),
  [Blancs](../metrics/lumieres-camera-neutral-20261009/lumieres-blanc-camera-neutral-20261009.svg),
  [HL](../metrics/lumieres-camera-neutral-20261009/lumieres-hl-camera-neutral-20261009.svg).

Les courbes représentent les deltas moyens Y de trois états (−extrême, 0,
+extrême), avec même poids pour les scènes. Les segments ne prouvent pas
la réponse intermédiaire réelle et ne sont pas des benchmarks de vitesse.

```bash
python3 tests/analyse_lumieres.py --output docs/validation/raw/mesure-lumieres-camera-neutral-20261009.json
python3 scripts/summarize_lighting_validation.py docs/validation/raw/mesure-lumieres-camera-neutral-20261009.json --output docs/validation/metrics/lumieres-camera-neutral-20261009
python3 -m unittest discover -s tests -p test_analyse_lumieres.py
python3 -m unittest discover -s tests -p test_analyse_exposition.py
```
## Campagne intermédiaire Ombres/HL — 10 octobre 2026

**Mémoire canonique :** [JSON v002 horodaté](../raw/mesure-lumieres-intermediaires-v002-20261010T120745Z.json).
Cette révision enrichit la mesure initiale avec les seuils de quantiles et les
effectifs exacts des zones ; elle ne change pas les mesures des variantes.
Le nom du fichier v001 avait un horaire saisi incorrectement (15:00 UTC) : son
champ `measured_at_utc` indique bien le début réel, 12:00:22 UTC. Utiliser v002.

### Couverture et méthode

Six scènes 0007/0037/0043/0327/9421/9950, deux contrôles, huit valeurs et deux
moteurs : **192 variantes**, douze neutres, aucun fichier manquant et aucun
avertissement d'audit Lightroom. TIFF natifs sRGB 16 bits, échantillonnage tous
les huit pixels horizontalement/verticalement. Les zones sont définies sur le
neutre de chaque moteur (quantiles Y 20 %, 40–60 %, 80 %), pas une même ROI
physique inter-moteur : ces résultats ne sont pas une MAE pixel à pixel LR/IPR.

Les XMP Lightroom donnent la correspondance réelle des suffixes : sans
suffixe = −100, −2 = +100, −3 = −75, −4 = −50, −5 = −25, −6 = +25,
−7 = +50, −8 = +75. Les réglages IPR sont **inférés par cette association**,
sans manifeste des exports manuels ; le build exact demeure inconnu.
Les neutres IPR sont les génériques du 9 octobre (`ID-IPR.tiff`), pas les
anciens `ID-IPR-CameraNeutral-AsShot.tiff`. Les neutres Lightroom sont Camera
Neutral/As Shot, dont le fichier corrigé `6V1A0037.tif`. L'optique reste
constante dans chaque série ; 9421 conserve les corrections activées.

Avant suppression, les JSON préservent SHA-256 complets, chemins, dates de
modification, dimensions/ICC/XMP, données non arrondies par scène, seuils,
effectifs et limites de provenance. Ils ne permettent ni de reconstruire les
pixels, ni de calculer une nouvelle métrique après suppression.

### Résultats et verdicts

Les deux curseurs IPR présentent **0 % d'inversions directionnelles** sur
tous les états. Les deltas moyens IPR sont presque linéaires avec la valeur
sur chaque branche positive/négative ; la réponse Lightroom ne suit pas
exactement cette loi. Les moyennes ci-dessous donnent le même poids aux six
scènes ; Y est la luminance linéaire normalisée, non un pourcentage.

| Valeur positive | Ombres : delta basses IPR / LR | HL : canaux saturés IPR / LR |
|---|---:|---:|
| +25 | 0,014618 / 0,002619 | 0,0172 % / 0,1138 % |
| +50 | 0,029236 / 0,005934 | 0,1506 % / 0,1939 % |
| +75 | 0,043854 / 0,010319 | 0,7091 % / 0,2539 % |
| +100 | 0,058472 / 0,016347 | 5,6218 % / 0,3114 % |

**Ombres : fonctionnement/progressivité confirmés, qualité non tranchée.**
L'ouverture IPR des basses est plus forte dès +25 (environ 5,6 fois le delta
LR), pas uniquement à +100. Ce rapport n'établit pas une erreur physique :
les bases et algorithmes diffèrent. Il faut une recette visuelle détails/bruit
et décider de l'amplitude recherchée avant de modifier le moteur. Verdict
global **en attente**, donc aucune suppression des séries Ombres.

**HL : verdict qualité négatif pour les fortes valeurs positives**, malgré
une direction correcte. +25/+50 ne présentent pas l'excès de clipping moyen
observé à +75/+100. À +100, le delta des hautes vaut 0,23220 IPR / 0,20362 LR,
mais le clipping IPR monte fortement. Cela localise la priorité sur l'épaule
et le gamut à forte amplitude, sans démontrer leur cause unique. À −100,
IPR réduit moins les hautes (delta −0,06601 / −0,13641 LR). Pas de correctif
de production dans cette passe ; préparer une sonde RGB avant clipping,
puis des exports de contrôle. Ne pas changer la loi EV pour compenser.

Conformément à la consigne du 10 octobre : supprimer uniquement les 48 TIFF
IPR de la série HL après contrôle des empreintes et journalisation, conserver
les 48 Lightroom HL, les Ombres, tous les RAW et les neutres communs. Une
future recette visuelle HL devra régénérer les IPR depuis les RAW.

Suppression effectuée : **48 TIFF, 12 396 654 432 octets (12,40 Go)**,
définitive, sans corbeille. [Journal des chemins et empreintes](../../reports/storage/lighting-cleanup-20261010T120857Z/deletion-log.csv).
Six fichiers HL IPR d'autres scènes (0767/9073/9119/9734/9944/9947) restent
hors de cette campagne : pas de référence Lightroom associée dans la série
analysée ; ils ne sont ni validés ni supprimés. Le journal, pas un glob de
fichiers, définit exactement la suppression réalisée.

### Courbes et génération suivante

[CSV de synthèse](../metrics/mesure-lumieres-intermediaires-v002-20261010T120745Z/summary.csv),
[courbes Ombres](../metrics/mesure-lumieres-intermediaires-v002-20261010T120745Z/ombre.svg),
[courbes HL](../metrics/mesure-lumieres-intermediaires-v002-20261010T120745Z/hl.svg).
Les segments relient les états mesurés ; ce sont des courbes de réponse,
**pas des mesures de vitesse**. Le zéro est l'identité du neutre.

Le lanceur historique `scripts/generate-photographic-campaign.ps1`
(retiré le 11 octobre ; voir le [générateur courant](../tools/generateur-images-tests.md))
préparait alors 527 TIFF pour les neuf groupes photographiques, sans
régénérer Ombres/HL : six scènes pour les Lumières restantes, une ou deux pour
les autres groupes. Camera Neutral imposé, normalisation expérimentale
désactivée, sidecars ignorés, TIFF 16 bits sRGB SDR, aucun écrasement.
Environ 136 Go estimés ; réserve minimale 30 Gio. Les manifestes JSON conservent
chaque réglage et export. Cette génération n'est pas une validation automatique.

Tests ciblés : **8/8 Python** (analyse Lumières et mémoire immuable),
**2/2 CTest Windows** (générateur/export), build du générateur réussi.

La génération a été lancée le 10 octobre à **12:09:12 UTC**. Racines :
`tests/image/base/<groupe>/generation-20261010T120912Z/`. Elle reste en cours
au moment de cette mise à jour ; ne pas annoncer les 527 TIFF comme produits.
Les premiers exports Lumière confirment Camera Neutral effectivement appliqué.
La mémoire des groupes terminés sera conservée dans
`docs/validation/raw/generation-photographique-20261010T120912Z/`.

## Investigation et correctif de production Ombres/HL — 10 octobre 2026

Le retour utilisateur confirme une **dégradation visuelle des Ombres
positives**. Le verdict « qualité en attente » de la campagne précédente
est donc remplacé par **négatif pour l'ancien rendu**, même si sa direction
et sa progression avec le curseur étaient correctes. Il ne faut plus demander
de revalider les anciens exports pour décider s'il existe un défaut.

### Cause démontrée des Ombres

L'ancien opérateur ajoutait, avant la remise à l'échelle RGB :

```text
Y' = Y + 0.065 * a * masqueOmbres(Y) * (1 - sqrt(Y / 0.45))
gainRGB = Y' / Y
```

Pour Y<0,10 le masque vaut 1. À +100, l'ajout tend vers **0,065** lorsque
Y tend vers zéro : le gain devient arbitrairement élevé. Le garde-fou de
`applyLuminance` ignorait Y≤10⁻⁶, donc le noir exact restait noir, mais une
discontinuité apparaissait immédiatement au-dessus. C'est une élévation du
plancher proche du noir, pas une récupération saine de détails.

Plus grave : dans cette plage, la dérivée vaut
`1 - 0.065*a/(2*sqrt(0.45*Y))`. À +100, elle devient **négative sous
Y≈0,002347**. Par exemple Y=0,001 devient ≈0,062935 tandis que Y=0,002
devient ≈0,062667 : **le ton initialement plus sombre devient le plus clair**.
Cela explique mathématiquement une dégradation des gradients/textures.
Le test directionnel précédent ne la voyait pas : il vérifiait chaque pixel
avant/après, pas l'ordre entre pixels. À Y=0,001 le gain était ≈×62,9,
amplifiant aussi les écarts chromatiques/bruit avant encodage. Les mesures
LR/IPR par quantiles confirment une amplitude trop forte, mais ne suffisaient
pas seules à identifier cette inversion.

### Correction des Ombres positives

La production utilise désormais un gain rationnel borné :

```text
deltaY = 3*a*Y/(1+Y/0.025)^2 * masqueOmbres(Y)
deltaY = min(deltaY, 0.90 * margeRGB)
Y' = Y + deltaY
```

- `a = clamp(curseur/100,0,1)` ; le masque 0,10–0,56 est conservé.
- Gain ≤×4 (+2 EV) ; l'ajout tend vers zéro au noir, sans socle artificiel.
  Le garde-fou historique Y≤10⁻⁶ n'interrompt plus la remise à l'échelle pour
  cette branche bornée ; le noir exactement nul reste inchangé.
- Genou Y=0,025 : ajout maximal non masqué de 0,01875, plutôt que 0,065 au noir.
- Sans masque, dérivée ≥8/9 ; avec masque et protection RGB, les rampes
  testées restent croissantes. Les tests couvrent aussi les tons très faibles.
- À Y=0,001, gain ≈×3,77 au lieu de ×62,9. La limitation est structurelle,
  pas une simple division arbitraire de l'ancien curseur.

Les constantes 3/0,025 sont une **recette initiale**, non une estimation de
la loi Lightroom. L'amplitude et le bruit perçu doivent être revalidés sur
les six scènes ; la parité Adobe n'est pas annoncée.

### Cause et correction des HL positives

L'ancien relèvement utilisait la marge `1-Y_courant`, puis multipliait RGB
par `Y'/Y`. Sur un pixel coloré, Y'<1 n'implique pas que tous les canaux soient
<1 : un canal dominant pouvait déborder et être écrêté à l'export.
Ce mécanisme existe dans le code et explique un risque de perte de teinte et
de détail. Sa contribution exacte aux TIFF anciens ne sera pas chiffrée sans
nouveaux exports/sondes ; le clipping +100 déjà mesuré était 5,622 % en moyenne.

La branche positive utilise maintenant la frontière RGB compatible avec une
mise à l'échelle commune : `Y_limite=Y_source/maxRGB`, pour 0<maxRGB<1.
`deltaY=0.70*a*masqueHL*max(0,Y_limite-Y_courant)`. Elle conserve 30 % de
marge au maximum du curseur. Un pixel déjà hors gamut/HDR n'est ni écrêté ni
relevé par cette branche ; il conserve sa valeur existante. Les proportions
RGB restent inchangées avant les opérations suivantes. Ce n'est pas un
écrêtage indépendant de chaque canal ni une reconstruction des RAW saturés.

**Portée :** Ombres/HL positives seulement ; EV, Noirs, Blancs et branches
négatives inchangés. Les valeurs nulles restent identitaires. Des réglages
combinés Blancs/saturation/grading peuvent encore introduire du clipping.
Les sidecars restent compatibles, mais un ancien réglage positif non nul
donne désormais un autre rendu : comportement volontaire, sans migration de valeur.

### Preuves et limites

- [Sonde JSON canonique](../raw/sonde-correctif-ombres-hl-v001-20261010T122315Z.json) :
  empreinte du code et de la sonde, 192 états de diagnostic ancien/nouveau.
- `toneRecoveryTests` : 1 500 000 états de rampes, gain, ordre tonal,
  marge RGB et progression du curseur ; tests réussis.
- `imageExportTests` : TIFF réel d'une rampe sombre (noir préservé,
  absence d'inversion et de socle), couleurs saturées à HL +100 sans nouveau
  canal à 255, plus les régressions existantes ; réussi. Le test historique
  imposant +8 niveaux dans les tons moyens a été remplacé par un relèvement
  modéré de 1 à 7, précisément pour ne plus exiger l'ancien excès.
- Build Windows de **`build-windows-msvc/Release/appIPR.exe`** réussi.
  SHA-256 final : `f6b280f97ef17b7b18fe8ba316e19914a349b01ea1f6886b3fc991aee5246f87`.
- 2/2 CTest ciblés réussis ; 13/13 tests des règles de sélection réussis.
  Pas de suite complète ni de validation finale des six RAW dans cette passe.

Ouvrir les ombres révèle nécessairement une partie du bruit existant :
**aucun opérateur tonal ne garantit zéro dégradation perceptuelle sur tout RAW**.
Adobe documente ce bruit visible à l'ouverture des ombres dans
[son guide Lightroom](https://helpx.adobe.com/lightroom/desktop/edit-photos/edit-photos.html).
L'ancien défaut d'inversion et de gain non borné est corrigé ; la préservation
perceptuelle des détails reste à confirmer sur les vrais exports. Un traitement
local préservant les contours est une piste future, pas une fonction ajoutée ici
([approche RawTherapee](https://rawpedia.pixls.us/shadows--highlights/)).

### Nouvelle conformité à réaliser

1. Utiliser l'exécutable **du build ci-dessus**, pas l'ancien standalone `dist`.
   Fermer/relancer IPR si elle était déjà lancée. Ouvrir les six RAW, conserver
   Camera Neutral/As Shot et la même optique, normalisation expérimentale désactivée.
2. Réinitialiser les réglages photographiques : EV=0, autres curseurs à zéro,
   courbes identitaires, mélangeur/grading à zéro, netteté/bruit à zéro.
3. Réexporter Ombres et HL séparément en TIFF sRGB 16 bits SDR, pleine définition.
   Pour garder les paires LR : sans suffixe −100, −2 +100, −3 −75, −4 −50,
   −5 −25, −6 +25, −7 +50, −8 +75. Les anciens IPR de ces séries sont supprimés
   suivant la consigne utilisateur ; **ne pas supprimer les références Lightroom**.
4. Comparer à 100 % les transitions proches du noir, détails/bruit et teintes ;
   pour HL vérifier couleurs et zones brillantes, particulièrement 9421/9950.
5. Reprendre l'analyse dans une nouvelle campagne JSON horodatée, sans écraser
   les anciennes mesures. Vérifier à nouveau clipping, amplitude et continuité.
   Ne pas déclarer le nouveau rendu validé avant cette comparaison.

La campagne automatique des autres groupes continue avec son générateur
déjà chargé (ancien binaire). Elle ne teste **aucun Ombres/HL non nul** : les
nouveaux opérateurs y sont identitaires et ces séries ne constituent pas une
preuve du correctif. Recompiler `iprGenerateTests` après sa fin avant de
générer Ombres/HL en ligne de commande ; le build IHM est déjà corrigé.

Les 48 anciens TIFF IPR Ombres ont été supprimés définitivement, sans
corbeille : **12 396 654 432 octets (12,40 Go)** libérés. Les références
Lightroom, RAW et neutres sont préservés.
[Journal de suppression](../../reports/storage/lighting-cleanup-20261010T122527Z/deletion-log.csv).

## Version figée après validation visuelle — 10 octobre 2026

L'utilisateur a vérifié visuellement les modifications en production et
**approuve l'état actuel des Ombres et Hautes lumières**. Le correctif constitue
désormais la référence visuelle acceptée du corpus actuel. Les conclusions
négatives précédentes concernent l'ancien rendu, pas cette version corrigée.

Cette acceptation visuelle complète les tests synthétiques/export réussis ;
elle ne vaut ni parité numérique Lightroom ni validation universelle de tous
les capteurs. **Des évolutions pourront être apportées après des tests avec
d'autres boîtiers et/ou objectifs**, notamment pour les amplitudes, couleurs
saturées, bruit et interactions avec profils DCP/LCP. Ces évolutions donneront
lieu à de nouvelles campagnes JSON versionnées, sans réécrire les preuves
actuelles ni supprimer cette référence.

Référence Git prévue pour cette version : branche `patch_light`, tag
`validation-ombres-hl-2026-10-10`. Le tag identifiera le commit de code publié ;
les futures améliorations se feront dans de nouveaux commits. Aucune fusion
dans `main` n'est effectuée dans cette publication. Le SHA-256 du build Windows
validé figure dans la section de preuves ci-dessus.

Contrôle avant publication : **9/9 CTest ciblés** réussis (rendu, détail,
export, récupération tonale, IHM des réglages, restauration RAW, profils,
décodeur et générateur). Le générateur a été recompilé avec le correctif après
la fin de la campagne automatique : ses neuf manifestes confirment désormais
**527/527 exports produits**, sans constituer une validation de leur rendu.

## Campagne Lumières sur le pipeline Rec.2020 — 17 octobre 2026

Les validations précédentes portent sur l'ancien rendu. La livraison
`v0.1.23-alpha` a changé la base : master Rec.2020 linéaire, calibration
d'ouverture RAW, Qt 6.8 minimum. Les six réglages sont revalidés dans l'ordre
demandé par l'utilisateur : exposition, contraste, ombres, noirs, HL, blancs.

Génération `iprGenerateTests lumiere` depuis `patch_light`, sur les six scènes
du protocole (0007, 0037, 0043, 0327, 9421, 9950). Les treize autres CR3 ont
été déplacés temporairement dans `tests/image/base-hors-protocole/`. Résultat :
300 variantes et six neutres `-IPR-neutre-lumiere`, Camera Neutral, sans
erreur. Les références Lightroom ont été réexportées par l'utilisateur :
4 états d'exposition et 8 états pour chaque autre réglage, sur les six scènes.

### Exposition

Mémoire : [mesure-lumiere-v001](../raw/mesure-lumiere-v001-20261017T215824485914Z.json),
protocole `grouped-native-v2`, analyse 2.0. États : 50 mesurés, 4 exclus.
Échantillonnage natif une ligne/colonne sur huit, sans conversion 8 bits.

| Moteur | −1 EV | −0,5 EV | 0 EV | +0,5 EV | +1 EV |
| --- | --- | --- | --- | --- | --- |
| Gain attendu `2^EV` | 0,5000 | 0,7071 | 1,0000 | 1,4142 | 2,0000 |
| IPR, médiane des 6 scènes | 0,5000 | 0,7071 | 1,0000 | 1,4142 | 2,0000 |
| LR, plage des 5 scènes auditées | 0,23–0,33 | 0,47–0,59 | — | 1,55–1,80 | 2,21–3,20 |

- **IPR** : loi `2^EV` exacte sur les pixels non écrêtés des six scènes, avec
  des quantiles à ±0,0002 et aucune inversion ; 0 EV est identique au neutre.
- **Écrêtage IPR à +1 EV** : 14,62 % (0007), 13,02 % (9421) et 17,61 % (9950)
  des échantillons ont un canal saturé ; de 0 à 2,50 % ailleurs. Ces valeurs
  sont pratiquement celles du 9 octobre : la nouvelle base ne les modifie pas.
- **Lightroom** : réponse non linéaire du TIFF final, quasiment sans écrêtage
  (au plus 0,51 % sur 9421 à +1 EV). Ses gains sont identiques à ceux du 9 octobre,
  ce qui confirme la cohérence des exports. Ce n'est pas une raison de modifier `2^EV`.
- **Exclusion** : les 4 variantes LR de 6V1A0037 portent `WhiteBalance=Custom`,
  avec la même température et la même nuance ; leur neutre porte `As Shot`.
  L'analyseur exige une égalité stricte : un neutre LR « Custom » est demandé à l'utilisateur.

Lecture : la loi d'exposition est revalidée sur le pipeline Rec.2020. L'écart
visible avec Lightroom en surexposition tient à la compression des hautes
lumières de sortie (écrêtage net d'IPR, adoucissement de LR). Il sera traité
avec Blancs et HL, sans modifier l'exposition. Décision visuelle de l'utilisateur
et parité Lightroom : non évaluées à ce stade.

### Exposition — correction avant le DCP et validation visuelle, 18 octobre 2026

La comparaison dans l'IHM a contredit la lecture précédente. À +1 EV, IPR
délavait le mur jaune de 9421, que Lightroom conservait ; à 0 EV, IPR
paraissait un peu plus sombre sur toutes les images. La campagne v001
validait un invariant (`2^EV` appliqué au rendu), pas le comportement
photographique. Cause : l'exposition multipliait le master déjà développé, donc
**après** la courbe tonale et les tables du DCP, sans épaule pour les hautes lumières.

Correction (`patch_light`) : pour un RAW avec DCP, l'exposition et la balance
des blancs agissent ensemble sur le signal caméra linéaire, **avant** le DCP
(`CameraProfileEngine::renderFromCameraSignal`). Les sources sans signal caméra
(raster, RAW sans DCP) gardent la multiplication linéaire. Les tests imposent
désormais que +1 EV ne soit pas un doublement après la courbe.

Rendu diagnostique des cinq scènes ayant un neutre LR, hors corpus
(quantiles de Y linéaire, pas d'échantillonnage 8) :

| +1 EV | Canal saturé, avant → après (LR) | Chroma du décile haut, avant → après (LR) |
| --- | --- | --- |
| 6V1A9421 | 13,18 % → 0,28 % (0,53 %) | 0,526 → 0,780 (0,695) |
| 6V1A0007 | 15,00 % → 0 % (0 %) | 0,178 → 0,154 (0,097) |
| 6V1A9950 | 17,86 % → 0 % (0 %) | 0,297 → 0,232 (0,193) |

Les hautes lumières sont désormais comprimées par l'épaule du profil, et la
forme de la réponse se rapproche de LR. Un écart de niveau subsiste à tous les
EV : IPR reste plus sombre dans les tons moyens et bas, dès 0 EV. Exemple à 0 EV,
Y médian 0,089 contre 0,149 sur 9421, et 0,020 contre 0,039 sur 0327. Cet écart
du rendu de base est un sujet distinct, à étudier avant le contraste.

**Décision de l'utilisateur, 18 octobre 2026 : exposition validée dans cet état.**
Les TIFF `lumiere/exposure` de la génération du 17 octobre restent ceux de l'ancien
calcul ; une campagne v002 sera produite après leur régénération. Les variantes des
cinq autres réglages, générées à 0 EV, ne sont pas affectées par cette correction.

### Exposition — campagne v002 sur base comparable, 18 octobre 2026

L'écart de base à 0 EV ne venait pas du moteur, mais de deux biais de méthode.
D'une part, le **générateur n'appliquait pas la calibration RAW** que l'IHM active
par défaut (ADR-0007) : ses TIFF étaient 0,3 à 1,1 EV plus sombres que l'écran.
D'autre part, l'optique différait : le générateur l'activait, alors que les
références LR ont `LensProfileEnable=0` (effet net sur 9950, vignetage).
Rendus diagnostiques à 0 EV, calibration active et optique coupée : écart à LR
de ±0,01 EV à tous les quantiles, sur les cinq scènes. Correction du générateur
(calibration par défaut, optique coupée hors série `optique`) ; groupe `lumiere`
entièrement régénéré.

Mémoire : [mesure-lumiere-v002](../raw/mesure-lumiere-v002-20261018T095236504304Z.json),
qui remplace la v001 sans la modifier. États : 50 mesurés, 4 exclus (LR de 0037,
neutre « Custom » manquant).

| | −1 EV | −0,5 EV | 0 EV | +0,5 EV | +1 EV |
| --- | --- | --- | --- | --- | --- |
| Gain médian IPR / LR, chacun contre son neutre | 0,249 / 0,256 | 0,517 / 0,540 | — | 1,768 / 1,764 | 2,997 / 2,936 |
| Écart IPR − LR au q50, EV, médiane des scènes | +0,01 | +0,00 | +0,00 | +0,00 | +0,00 |
| Canal saturé, maximum par scène, IPR / LR | 0 % / 0 % | 0,01 % / 0 % | — | 0,54 % / 0,13 % | 0,91 % / 0,51 % |

Sur les 25 couples scène × EV, l'écart IPR − LR reste dans ±0,03 EV de q10 à q99,
sauf 9421 à +1 EV (+0,07 EV au q90). L'exposition est **conforme à LR à réglages
égaux** sur ce corpus. Ce n'est pas une parité universelle : un seul boîtier
(R6 Mark III), Camera Neutral, optique coupée, six scènes.

L'exposition est complétée par [v003](../raw/mesure-lumiere-v003-20261018T100527858427Z.json)
(54 états, aucun exclu) : variantes LR de 0037 réexportées en « As Shot ».

### Contraste — campagne du 18 octobre 2026

Mémoire : [mesure-lumiere contraste](../raw/mesure-lumiere-v001-20261018T100916500016Z.json),
102 états mesurés (54 IPR, 48 LR), aucun exclu, base calibrée et optique coupée.
Le numéro v001 suit la numérotation de ce réglage ; l'horodatage identifie la campagne.
Ratios Y variante / neutre du même moteur, médiane des six scènes :

| Valeur | Bas 20 %, IPR / LR | Tons moyens 40–60 %, IPR / LR | Haut 20 %, IPR / LR | Canal saturé max, IPR / LR |
| --- | --- | --- | --- | --- |
| −100 | 3,054 / 2,860 | 1,519 / 2,183 | 0,825 / 0,953 | 0,00 % / 0,01 % |
| −50 | 1,740 / 1,703 | 1,226 / 1,505 | 0,916 / 0,973 | 0,01 % / 0,01 % |
| +50 | 0,419 / 0,484 | 0,737 / 0,577 | 1,236 / 1,037 | 1,02 % / 0,20 % |
| +100 | 0,179 / 0,126 | 0,558 / 0,251 | 1,417 / 1,082 | 6,81 % / 1,02 % |

Écart direct IPR − LR au q50 : −0,36 EV à −100, −0,21 EV à −50, +0,18 EV à +50 et
+0,99 EV à +100 (pire écart toutes scènes et quantiles : 1,44 EV à +100).

Lecture : le sens est correct et les ombres répondent comme LR. En revanche,
les tons moyens réagissent environ deux fois moins dans IPR, et un contraste
positif fait monter les hautes lumières (×1,42 et 6,81 % d'écrêtage à +100,
contre ×1,08 et 1,02 % pour LR). Cause probable : courbe en S linéaire autour
de 18 % appliquée au rendu final, sans protection des hautes lumières, alors que
LR travaille dans un domaine perceptuel avec des hautes lumières protégées.
**Contrôle visuel de l'utilisateur demandé avant toute correction.**

#### Correction du contraste et validation visuelle, 18 octobre 2026

Les captures de l'utilisateur confirment les mesures : à +100, IPR poussait
blancs et HL sans harmonie ; à +50, presque aucun effet visible. Nouvelle courbe :
S à extrémités fixes en domaine logarithmique (16 EV sous le blanc), pivot 0,32,
appliquée **canal par canal**. Un premier essai, qui conservait les rapports RGB, blanchissait
et refroidissait le jaune de 9421 à +100 (saturation des tons clairs −0,085 contre LR).
L'application par canal donne +0,003. Paramètres ajustés sur les 48 références LR,
écart de luminance médian 0,08 EV. Force positive 0,60 ; force négative 0,45 × 1,08,
renforcée de 8 % à la demande de l'utilisateur.

**Décision de l'utilisateur : fonctionnement du contraste approuvé dans cette
version, avec une réserve à +100**, jugé environ 3 % plus fort que Lightroom.

#### Contraste — campagne v002 sur la version approuvée

Mémoire : [mesure-lumiere contraste v002](../raw/mesure-lumiere-v002-20261018T111849048537Z.json),
qui remplace la campagne du matin ; 102 états mesurés, aucun exclu. Groupe `lumiere`
régénéré avec la courbe par canal.

| Valeur | Bas 20 %, IPR / LR | Tons moyens, IPR / LR | Haut 20 %, IPR / LR | Canal saturé max, IPR / LR |
| --- | --- | --- | --- | --- |
| −100 | 4,527 / 2,860 | 2,394 / 2,183 | 0,963 / 0,953 | 0,00 % / 0,01 % |
| −50 | 2,207 / 1,703 | 1,604 / 1,505 | 0,978 / 0,973 | 0,01 % / 0,01 % |
| +50 | 0,351 / 0,484 | 0,502 / 0,577 | 1,039 / 1,037 | 0,20 % / 0,20 % |
| +100 | 0,128 / 0,126 | 0,224 / 0,251 | 1,094 / 1,082 | 0,57 % / 1,02 % |

Les hautes lumières suivent désormais LR, avec moins d'écrêtage. Les tons moyens
sont 0,1 à 0,2 EV plus marqués que LR des deux côtés, ce qui est cohérent avec la
réserve de l'utilisateur à +100. **Point de vigilance** : à −100, les noirs profonds
remontent bien plus que dans LR (×4,53 contre ×2,86 sur les 20 % bas, +0,44 EV au q5),
ce qui risque de révéler le bruit sur les scènes à ISO élevé. À reprendre si la
validation des ombres et des noirs le confirme visuellement.

### Ombres, noirs, HL et blancs — plan A, campagnes du 18 octobre 2026

Plan A ([roadmap Lumières](../../architecture/roadmap-lumieres.md)) : courbe de
luminance unique à plages fixes, ajustée sur les références LR de huit scènes
(six du protocole, plus 6V1A0524 et 7Y6A7003, claires), sous les règles de
séparation des tests d'export. Groupe `lumiere` entièrement régénéré.

Mémoires, chacune remplaçant la précédente sans la modifier :
[exposition v004](../raw/mesure-lumiere-v004-20261018T145654729513Z.json) (72 états),
[contraste v003](../raw/mesure-lumiere-v003-20261018T150227383825Z.json),
[ombres v002](../raw/mesure-lumiere-v002-20261018T150753870527Z.json),
[noirs v002](../raw/mesure-lumiere-v002-20261018T151320602917Z.json),
[HL v002](../raw/mesure-lumiere-v002-20261018T151846378645Z.json) et
[blancs v002](../raw/mesure-lumiere-v002-20261018T152411246100Z.json) (136 états chacune).
Aucun exclu.

Exposition et contraste tiennent sur huit scènes : exposition à ±0,01 EV en
médiane, pire écart 0,08 EV ; contraste conforme à sa v002.

Ratios Y variante / neutre du même moteur à ±100, médiane des scènes :

| Réglage | Bas 20 %, IPR / LR | Tons moyens, IPR / LR | Haut 20 %, IPR / LR | Canal saturé max, IPR / LR |
| --- | --- | --- | --- | --- |
| Ombres −100 | 0,279 / 0,360 | 0,659 / 0,700 | 0,989 / 0,969 | 0,34 % / 0,03 % |
| Ombres +100 | 2,820 / 3,327 | 1,531 / 1,398 | 1,009 / 1,031 | 0,35 % / 0,11 % |
| Noirs −100 | 0,000 / 0,000 | 0,343 / 0,107 | 0,928 / 0,759 | 0,27 % / 0,05 % |
| Noirs +100 | 2,122 / 2,293 | 1,038 / 1,866 | 1,000 / 1,138 | 0,35 % / 0,08 % |
| HL −100 | 0,982 / 0,883 | 0,898 / 0,782 | 0,762 / 0,626 | 0,27 % / 0,01 % |
| HL +100 | 1,009 / 1,135 | 1,052 / 1,194 | 1,150 / 1,563 | 3,72 % / 36,68 % |
| Blancs −100 | 0,994 / 0,981 | 0,925 / 0,931 | 0,787 / 0,856 | 0,00 % / 0,02 % |
| Blancs +100 | 1,042 / 2,101 | 1,079 / 4,210 | 1,158 / 2,174 | 12,26 % / 7,24 % |

Lecture : chaque réglage agit dans sa zone, sans déborder sur l'autre
extrémité. Ombres et blancs négatifs suivent LR. Les écarts restants viennent
surtout de l'adaptation de LR à ces scènes sombres : ses noirs et blancs
déplacent aussi les tons moyens (blancs +100 : ×4,2), ce que les plages fixes
excluent (plan B). HL et blancs positifs sont donc nettement plus doux que LR ;
les blancs écrêtent tôt sur la scène claire (≈ 10 % dès +25, contre 2,4 %).

**Défaut corrigé après campagne** : les noirs négatifs écrasaient trop tôt
(à −25, bas 20 % à 0,029 contre 0,481 pour LR). L'ajustement plafonnait les
tons à 2^-12 et masquait ces tons très profonds. Correction : orteil doux
y² / (y + orteil) avant un point noir plus tardif ; ajustement avec la moyenne
du bas 20 % sans ce plancher. Écart prévu à −25 et −50 : +0,01 et +0,00 EV.
Les rendus noirs de cette campagne précèdent ce correctif : **à régénérer et
remesurer** avant validation des noirs.

**Validation visuelle de l'utilisateur demandée**, réglage par réglage.

#### Validation visuelle et règle de zones redéfinie, 18 octobre 2026

Retour de l'utilisateur sur IPR face à LR : blancs incorrects, HL encore trop
douces, noirs incorrects à −100 comme à +100 ; ombres à +100 acceptables
(légère perte de détail, compensable par les autres réglages). Cause : la règle
des tests d'export isolait chaque réglage, alors que LR fait atteindre les tons
moyens aux noirs et aux blancs. Règle redéfinie (voir la
[roadmap Lumières](../../architecture/roadmap-lumieres.md)) : effet gradué, le
plus faible à l'extrémité opposée de l'histogramme. Noirs, HL et blancs
réajustés ; courbe échantillonnée et rendue croissante. Groupe `lumiere`
régénéré.

Mémoires : [ombres v003](../raw/mesure-lumiere-v003-20261018T165524982746Z.json),
[noirs v003](../raw/mesure-lumiere-v003-20261018T170033102725Z.json),
[HL v003](../raw/mesure-lumiere-v003-20261018T170543333217Z.json),
[blancs v003](../raw/mesure-lumiere-v003-20261018T171151440516Z.json) ; 136 états
chacune, aucun exclu. Écart |EV| moyen à LR des rendus : ombres 0,24, noirs 0,11
(contre 0,33), HL 0,22, blancs 0,24.

| Réglage | Bas 20 %, IPR / LR | Tons moyens, IPR / LR | Haut 20 %, IPR / LR | Canal saturé max, IPR / LR |
| --- | --- | --- | --- | --- |
| Noirs −100 | 0,000 / 0,000 | 0,137 / 0,107 | 0,824 / 0,759 | 0,00 % / 0,05 % |
| Noirs −25 | 0,504 / 0,481 | 0,784 / 0,853 | 0,967 / 0,982 | 0,09 % / 0,04 % |
| Noirs +100 | 2,598 / 2,293 | 1,885 / 1,866 | 1,090 / 1,138 | 0,36 % / 0,08 % |
| HL −100 | 0,925 / 0,883 | 0,844 / 0,782 | 0,720 / 0,626 | 0,27 % / 0,01 % |
| HL +100 | 1,046 / 1,135 | 1,103 / 1,194 | 1,203 / 1,563 | 4,18 % / 36,68 % |
| Blancs −100 | 0,970 / 0,981 | 0,897 / 0,931 | 0,827 / 0,856 | 0,00 % / 0,02 % |
| Blancs +100 | 1,179 / 2,101 | 1,239 / 4,210 | 1,333 / 2,174 | 12,26 % / 7,24 % |

Les noirs suivent désormais LR des deux côtés. HL et blancs négatifs sont
proches de LR. HL et blancs positifs restent nettement plus doux : LR y place
son point blanc selon le contenu de l'image (q75 de 6V1A0043 remonté de plus de
4 EV, 45 % de 6V1A0007 écrêtés), ce qu'aucune courbe fixe ne reproduit sans
sur-remonter les tons profonds. **Décision de l'utilisateur attendue** : plan B
(point blanc adaptatif) pour HL et blancs positifs.
