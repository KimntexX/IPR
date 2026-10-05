# Rapport — ouverture CR3 `6V1A9933-2`

Date : 30 septembre 2026

## Corpus comparé

| Fichier | Origine | Dimensions |
|---|---|---:|
| `6V1A9933-2.CR3` | RAW Canon EOS R6 Mark III | RAW |
| `6V1A9933-2.jpg` | export Adobe Lightroom 9.5.1 | 4 640 × 6 960 |
| `6V1A9933-2-IPR.jpg` | export IPR | 4 639 × 6 959 |

La différence d’un pixel provient de la zone d’image développée retournée par
LibRaw ; elle n’explique pas l’écart tonal.

## Résultats

La sonde `tonalReferenceComparisonProbe`, exécutée sans réglage utilisateur
(`IPR_PROBE_DEFAULT_SETTINGS=1`), donne pour le rendu IPR en balance boîtier
*As Shot* :

| Mesure sRGB | IPR | JPEG Lightroom |
|---|---:|---:|
| Moyenne RGB | 104,9 / 102,9 / 101,0 | 157,4 / 156,3 / 156,0 |
| MAE RGB | 56,5 | — |
| MAE luminance | 56,1 | — |
| Luminance P05 | 45 | 59 |
| Luminance P50 | 89 | 151 |
| Luminance P95 | 209 | 250 |

L’analyse directe des deux JPEG, après recherche du meilleur décalage de
recadrage, confirme un écart moyen de luminance d’environ 57 niveaux. L’IPR
est donc plus sombre et conserve davantage de marge dans les hautes lumières ;
il n’est pas sur-développé.

La sonde a aussi été exécutée avec la base Lightroom documentée
(`-0,49 EV`, contraste `+41`, hautes lumières/ombres/blancs/noirs `-100`).
L’IPR atteint alors une luminance moyenne d’environ 47–48, contre 156 pour le
JPEG Lightroom. Cette divergence est celle du comportement tonal des curseurs
et du processus Lightroom, pas celle de l’ouverture sans réglage.

## Prétraitements réellement appliqués à l’ouverture

L’ouverture CR3 n’applique aucun réglage de l’interface : les valeurs
d’exposition, contraste, tons, couleur et effets sont réinitialisées à zéro.
Le développement nécessaire du RAW contient néanmoins :

1. dématriçage LibRaw en RGB 16 bits linéaire ;
2. balance des blancs boîtier *As Shot* ;
3. estimation de niveau noir Canon depuis les marges CFA lorsque LibRaw ne
   fournit pas de niveau noir fiable. Pour ce fichier, la valeur mesurée est
   `2050` pour R, G1, B et G2, cohérente avec l’offset attendu ;
4. profils DCP caméra et LCP objectif sélectionnés pour le boîtier et l’objectif
   détectés ;
5. conversion en sRGB linéaire et compression de gamut au moment de l’aperçu
   ou de l’export.

Ces opérations font partie du développement minimal d’un RAW ; elles ne sont
pas des réglages photographiques cachés.

## Vérification de la luminosité automatique LibRaw

`no_auto_bright` a été temporairement désactivé dans une version de mesure.
Les résultats sont restés identiques sur ce CR3, car le chemin de sortie
caméra/DCP linéaire ne fournit pas de normalisation automatique exploitable.
Le réglage explicite `no_auto_bright=1` est donc conservé : il évite un gain
heuristique non reproductible et préserve la signification du réglage
d’exposition en EV.

## Conclusion et décision

Aucune correction du prétraitement d’ouverture n’est appliquée : le constat
mesuré est une sous-luminosité relative à Lightroom, non un excès de
prétraitement. Ajouter une exposition ou une courbe cachée lors de l’ouverture
masquerait l’écart, casserait le caractère non destructif des réglages et
rendrait `0 EV` ambigu.

Pour rapprocher volontairement l’apparence par défaut de Lightroom, il faut
concevoir séparément un rendu de base explicite (courbe de processus et
compensation d’exposition affichées dans l’interface), puis le valider contre
un corpus de scènes. Ce travail ne doit pas être confondu avec le décodage
CR3 minimal actuel.
