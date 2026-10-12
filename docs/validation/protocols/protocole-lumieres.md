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

- EV : dans les pixels non écrêtés et hors branche non linéaire de sortie,
  vérifier le gain attendu `2^EV`, neutralité EV0 et conservation des ratios
  RGB avant gamut. Les TIFF peuvent dévier près des limites de sortie.
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

Les valeurs proviennent des déclarations TIFF IPR et du XMP LR, pas des
suffixes des anciennes campagnes. Une référence ambiguë nécessite une
association explicite. Les mémoires historiques restent inchangées.
Les pixels sont natifs, avec pas d'échantillonnage déclaré ; ne pas utiliser
cette décimation pour mesurer le bruit.

Tests synthétiques ciblés : `toneRecoveryTests`, `colorPipelineTests`,
`imageExportTests`, via le lanceur Windows qui prépare Qt.
Rapport unique : [analyse Lumières](../analyses/analyse-validation-lumieres-base-2026-10-02.md).
Contrat de mémoire, recette IHM et suppression : socle commun.
