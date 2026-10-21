# Protocole — Lumières

Version 1 — 10 octobre 2026. Statut : recette prête ; ombres/HL de la version
figée acceptées visuellement sur le corpus courant, pas parité LR universelle.
Appliquer le [socle commun](protocole-commun.md).

## Corpus et états

RAW 0007, 0037, 0043, 0327, 9421, 9950 : faible chroma, végétation, couleurs,
peau, éclairage artificiel et ciel. Une scène à contre-jour ajoute un stress.

| Contrôle | Série |
| --- | --- |
| `exposure` | −1, −0,5, 0, +0,5, +1 EV ; ±3/±5 seulement en stress |
| `contrast`, `shadows`, `blacks`, `highlights`, `whites` | −100, −75, −50, −25, 0, +25, +50, +75, +100 |

Un contrôle à la fois ; garder sa base Camera Neutral et son état optique.
Déclarer explicitement fichiers/valeurs : ne pas réinterpréter les numéros
globaux du générateur comme les suffixes d'une ancienne campagne.

## Mesures / critères

- EV (depuis le 18 octobre 2026) : pour un RAW avec DCP, le gain `2^EV` porte
  sur le signal caméra **avant** le profil ; le TIFF suit ensuite la courbe et les
  tables du DCP, comme Lightroom. Ne plus exiger `2^EV` dans le TIFF final : ce
  critère validait l'ancien calcul après courbe, rejeté visuellement. Vérifier la
  neutralité à 0 EV, la monotonie et l'écrêtage, puis l'écart à LR par quantile de Y
  à réglages égaux (calibration RAW active, optique dans le même état des deux côtés).
  Les tests moteur imposent `2^EV` sur le signal caméra et l'absence de doublement
  après courbe. Sources sans signal caméra : `2^EV` linéaire dans le TIFF.
- Ombres/noirs : mesurer Y dans les bas 20 % et les noirs profonds, mais aussi
  tons moyens/HL. Ombres positives : aucune remontée discontinue du noir,
  inversion tonale, posterisation ou halo ; ne pas masquer une amplification
  normale du bruit préexistant. Noirs se teste séparément des Ombres.
- HL/blancs : même contrôle dans les hauts 20 %, les canaux proches de saturation
  et les couleurs saturées ; mesurer nouvel écrêtage et ratios RGB. Blancs est
  distinct de HL ; des pixels capteur déjà saturés ne sont pas récupérables.
- Contraste : rampe autour du pivot et réponse des bas/hauts tons ; contrôle
  du clipping, des couleurs et de la continuité aux valeurs intermédiaires.
- Toutes séries : zones fixes calculées sur le neutre propre, moyennes,
  percentiles, nombre de pixels, clips et variations hors zone ciblée. Les
  quantiles peuvent contenir des ex æquo : conserver effectifs et bornes.
- **Critère d'écart à LR, décision utilisateur du 20 octobre 2026** : écart de
  clarté perceptuelle |ΔL\*| (CIE L\* moyen) sur la zone du réglage, IPR contre
  LR à réglages égaux. Réussi si |ΔL\*| ≤ 2 sur au moins 80 % des scènes, et jamais
  au-delà de 4 sur plus de deux scènes. ΔL\* ≤ 2 équivaut à l'ancien ±3 % de
  luminosité affichée sur une scène claire (≈ 2,2) ; sur une scène sombre, le ±3 %
  relatif exigeait ≈ 0,7, en deçà de la perception. Pour HL, la zone est celle des
  10 % les plus clairs du neutre ; les scènes sans vraies hautes lumières
  (moins de 5 % de pixels à L\* ≥ 70) comptent moins dans les ajustements.

Les tests moteur du correctif actuel contrôlent, pour Ombres positives, noir
continu, ordre tonal et gain ≤×4 ; pour HL positives, marge RGB fondée sur
le canal maximum. Ce sont des invariants d'implémentation, pas des paramètres
LR ni des promesses de récupération de signal perdu. Étendre aux autres
boîtiers/objectifs avant généralisation.

## Reproduction disponible

```bash
python tests/analyse_lumiere.py --base tests/image/base --strict --data-version 1
python tests/analyse_lumiere.py --base tests/image/base --feature shadows --feature highlights --data-version 2
```

Les valeurs proviennent des déclarations des rendus IPR en mémoire et du XMP LR, pas des
suffixes des anciennes campagnes. Une référence ambiguë nécessite une
association explicite. Les mémoires historiques restent inchangées.
Les pixels sont natifs, avec pas d'échantillonnage déclaré ; ne pas utiliser
cette décimation pour mesurer le bruit.

Tests synthétiques ciblés : `toneRecoveryTests`, `colorPipelineTests`,
`imageExportTests`, via le lanceur Windows qui prépare Qt.
Rapport unique : [analyse Lumières](../analyses/analyse-validation-lumieres-base-2026-10-02.md).
Contrat de mémoire, recette IHM et suppression : socle commun.
