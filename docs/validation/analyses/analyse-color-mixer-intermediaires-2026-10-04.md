# Mélangeur huit bandes — progressivité à ±25/±50

**Date :** 4 octobre 2026. **Protocole :**
[valeurs intermédiaires](../protocols/protocole-color-mixer-intermediaires-2026-10-04.md).
**Analyse :** comparateur version 4, moteur huit bandes après
[ADR-0005](../../decisions/ADR-0005-melangeur-huit-bandes-luminance-bornee.md).
Le moteur photographique n'a pas été modifié pendant cette campagne.

## Conclusion et décision

La **progressivité fonctionnelle est confirmée sur le corpus principal** :
les 24 séries IPR (huit couleurs × trois paramètres) sont strictement
croissantes sur les moyennes de zone, dans l'ordre −50, −25, 0, +25, +50.
Les 24 paires −50/+50 sont non identiques. Les 24 séries Lightroom sont
également strictement croissantes ; leurs amplitudes ne définissent pas pour
autant une obligation de parité pour IPR.

La luminance moyenne des zones reste stable pendant teinte/saturation :
l'écart maximal à leur neutre IPR est **0,00997 %**, sur le rouge.
Les résidus rouge (~1,9 %) et bleu (~0,67 %) de la
[campagne ±100](analyse-color-mixer-huit-bandes-2026-10-04.md) ne se reproduisent
pas dans cette nouvelle série. Les six neutres actifs sont **identiques octet
pour octet aux copies archivées**, vérifié par SHA-256 : l'amélioration mesurée
ne provient donc pas d'un remplacement des références. Aucun correctif de
moteur n'a été appliqué ici. Cela ne démontre ni l'origine de l'ancien résidu,
ni sa disparition à ±100 : les variantes et valeurs testées sont différentes.

**Décision :** conserver le modèle et les constantes actuels. Il n'y a pas
d'inversion, de commande bloquée ou d'éclaircissement excessif justifiant un
correctif sur ces données. Les réserves restantes concernent la calibration,
les transitions entre bandes, la recette visuelle et l'écart du développement
neutre, pas la progressivité moyenne à ±25/±50.

## Corpus et audit

Convention confirmée par l'utilisateur : 1–4 teinte, 5–8 saturation,
9–12 luminance ; chaque groupe suit −50, −25, +25, +50.
Le fichier sans suffixe est le n°1, **pas un neutre**.

| Couleur / scène | Lightroom | IPR | Pixels du masque / couverture |
| --- | ---: | ---: | ---: |
| Rouge / 0327 | 12 | 12 | 79 228 / 6,1333 % |
| Orange / 0327 | 12 | 12 | 656 747 / 50,8406 % |
| Jaune / 0327 | 12 | 12 | 102 029 / 7,8984 % |
| Vert / 0037 | 12 | 12 | 241 381 / 18,6860 % |
| Cyan / 0327 | 12 | 12 | 3 959 / 0,3065 % |
| Bleu / 0327 | 12 | 12 | 2 803 / 0,2170 % |
| Violet / 0043 | 12 | 12 | 1 854 / 0,1435 % |
| Magenta / 0043 | 12 | 12 | 5 678 / 0,4395 % |

Soit **96 variantes Lightroom et 96 IPR**, plus six neutres. Aucun fichier
principal manquant, aucun état XMP Lightroom erroné ou réglage hors mélangeur
différent du neutre : les trois audits principaux contiennent zéro anomalie.
Tous les TIFF principaux sont sRGB 16 bits, orientation 1, non compressés.
L'alpha des variantes IPR est opaque sur tous les pixels échantillonnés.
Les réglages des TIFF IPR restent déclarés par les noms et l'opérateur :
ils ne contiennent pas de manifeste permettant de certifier tous les paramètres.

Dimensions : 0327, LR 4640×6960 et IPR 4639×6959 ; 0037/0043,
LR 6960×4640 et IPR 6959×4639. Les références sont dans `base/neutre`.

### Complément vert / 0327

Les douze variantes IPR supplémentaires ont aussi été mesurées, séparément.
Leurs trois progressions moyennes sont croissantes, mais leur masque ne contient
que **134 pixels (0,0104 %)**. Les douze états Lightroom correspondants sont
absents ; ils apparaissent explicitement comme manquants dans cet audit.
Ce complément n'entre pas dans les 192 variantes appariées ni dans la validation
de calibration du vert. La scène 0037 apporte la couverture pertinente.

## Méthode et portée

Échantillonnage au pas 5 : **1 291 776 pixels réels par image**, sans
redimensionnement, interpolation ni conversion 8 bits. Lecture native 16 bits,
normalisation float32, décodage sRGB analytique, HSV en RGB linéaire et
`Y = 0,2126 R + 0,7152 G + 0,0722 B`.

Masque fixe sur le neutre Lightroom : H à ±15° du centre nominal,
S≥0,15 et 0,01<Y<0,9. Chaque réponse est mesurée au **neutre de sa propre
application**. Teinte : différence angulaire circulaire ; saturation : ΔS ;
luminance : somme Y / somme Y₀. Le point zéro est inséré uniquement si un
neutre a été effectivement mesuré. Les tests distinguent une suite simplement
non décroissante d'une réponse strictement croissante : un curseur inopérant
avec cinq points égaux ne serait pas déclaré fonctionnel.

Tolérances de monotonie : 0,05° pour H, 0,0001 pour ΔS et le rapport Y.
La réussite porte sur les **moyennes de zones**, pas sur tous les pixels,
l'absence de halos, les très faibles chromas ou une linéarité perceptuelle.
Les bandes cyan/bleu/violet/magenta sont moins représentées et demandent
une mire ou des plages continues supplémentaires pour une calibration robuste.

Le crop LR choisi par signature des contours absorbe seulement l'écart d'un
pixel : haut/gauche (1,0) sur 0327, (0,1) sur 0037/0043. Les candidats sont
parfois proches ; ce n'est pas une preuve de recalage subpixel. Les MAE/ΔE
interapplications sont donc descriptives, pas un verdict pixel-à-pixel définitif.

## Réponses IPR mesurées

Les quatre colonnes correspondent aux commandes −50, −25, +25, +50.
Le zéro propre est respectivement 0°, ΔS=0 et Y/Y₀=1.

### Teinte — ΔH moyen en degrés

| Couleur | −50 | −25 | +25 | +50 |
| --- | ---: | ---: | ---: | ---: |
| Rouge | −6,535 | −3,266 | +3,266 | +6,533 |
| Orange | −7,218 | −3,608 | +3,608 | +7,218 |
| Jaune | −6,845 | −3,422 | +3,423 | +6,846 |
| Vert | −5,588 | −2,796 | +2,797 | +5,591 |
| Cyan | −7,154 | −3,569 | +3,574 | +7,148 |
| Bleu | −3,310 | −1,607 | +1,630 | +3,300 |
| Violet | −1,549 | −0,783 | +0,763 | +1,527 |
| Magenta | −4,020 | −2,009 | +2,003 | +4,026 |

Les réponses observées sont proches d'une progression proportionnelle sur ce
corpus, sans que cela constitue un étalonnage. Le moteur utilise une rotation
maximale de ±18° pondérée par le support calculé sur la teinte **source**.
Le masque mesuré après rendu n'est pas ce support interne : une faible réponse
violette sur une zone choisie dans Lightroom ne prouve pas une défaillance du
curseur violet.

### Saturation — ΔS moyen en HSV linéaire

| Couleur | −50 | −25 | +25 | +50 |
| --- | ---: | ---: | ---: | ---: |
| Rouge | −0,22774 | −0,11384 | +0,11378 | +0,17221 |
| Orange | −0,18992 | −0,09493 | +0,09490 | +0,18813 |
| Jaune | −0,20701 | −0,10351 | +0,10350 | +0,20699 |
| Vert | −0,14120 | −0,07060 | +0,07053 | +0,14072 |
| Cyan | −0,14178 | −0,07090 | +0,07085 | +0,14170 |
| Bleu | −0,02721 | −0,01353 | +0,01351 | +0,02718 |
| Violet | −0,02167 | −0,01080 | +0,01083 | +0,02172 |
| Magenta | −0,10339 | −0,05168 | +0,05162 | +0,10305 |

Le rouge +50 ne double pas la réponse +25 ; il continue bien d'augmenter.
Le modèle borne `S_source × (1 + commande pondérée)` à [0,1] : une atténuation
de pente sur des pixels déjà saturés est compatible avec cette limite.
Le TIFF final ne permet toutefois pas d'attribuer seul toute la courbure à
ce clamp, indépendamment des étapes suivantes. Ne pas exiger une droite
jusqu'aux extrêmes ni supprimer cette borne pour reproduire Lightroom.

### Luminance — rapport Y/Y₀

| Couleur | −50 | −25 | +25 | +50 |
| --- | ---: | ---: | ---: | ---: |
| Rouge | 0,88359 | 0,93959 | 1,06515 | 1,13545 |
| Orange | 0,86571 | 0,93036 | 1,07502 | 1,15582 |
| Jaune | 0,87426 | 0,93501 | 1,06955 | 1,14397 |
| Vert | 0,90405 | 0,95046 | 1,05292 | 1,10941 |
| Cyan | 0,86524 | 0,93013 | 1,07528 | 1,15640 |
| Bleu | 0,95586 | 0,97730 | 1,02408 | 1,04963 |
| Violet | 0,95632 | 0,97753 | 1,02382 | 1,04906 |
| Magenta | 0,89970 | 0,94818 | 1,05539 | 1,11459 |

Les gains sont modérés et progressifs. Avant les transformations de sortie,
le modèle est `Y_cible = Y_source × 2^(0,5 × commande/100 × poids)`.
À ±50, la borne théorique pour une seule bande à poids 1 est donc
`2^±0,25`, soit environ **0,84090 à 1,18921** ; ce n'est ni ±50 % de Y ni
±0,5 EV à ces positions. Toutes les moyennes mesurées restent dans cet
intervalle. Cela confirme le comportement prévu sans réintroduire le fort
éclaircissement des couleurs sombres de l'ancien modèle.

## Conservation de luminance et sortie

Maximum de `100 × |Y/Y₀ − 1|` sur les huit états teinte/saturation IPR de
chaque couleur, exprimé en **pour cent** et non en ratio :

| Couleur | Écart maximal de Y |
| --- | ---: |
| Rouge | 0,00997 % |
| Orange | 0,00154 % |
| Jaune | 0,00029 % |
| Vert | 0,00131 % |
| Cyan | 0,00128 % |
| Bleu | 0,00427 % |
| Violet | 0,00252 % |
| Magenta | 0,00129 % |

Sur l'échantillon complet, aucune variante 0327 ne contient de canal plafonné
au seuil de sortie 16 bits. Le vert 0037 varie de 0,5106 à 0,5314 % de pixels
avec au moins un canal au plafond ; violet/magenta restent sous 0,0048 %.
Ce contrôle ne prouve pas l'absence de clipping en amont ou entre échantillons.
Il n'y a aucun alpha non opaque échantillonné. Les gris parfaits restent couverts
par les tests synthétiques du moteur, pas par une mire dédiée dans ce corpus.

## Lightroom : comparaison, pas égalité

Les modèles ont des amplitudes différentes. Quelques exemples à +50, toujours
par rapport au neutre propre et sur le même masque d'observation :

| Mesure / couleur | IPR | Lightroom |
| --- | ---: | ---: |
| ΔH vert | +5,591° | +24,986° |
| ΔH violet | +1,527° | +32,461° |
| ΔH magenta | +4,026° | +2,129° |
| ΔS rouge | +0,17221 | +0,04816 |
| Y/Y₀ rouge | 1,13545 | 1,33897 |
| Y/Y₀ orange | 1,15582 | 1,13294 |
| Y/Y₀ bleu | 1,04963 | 1,03798 |

Augmenter globalement une constante pour rapprocher le violet de Lightroom
détériorerait potentiellement d'autres couleurs. Une éventuelle calibration
doit considérer profils, espace de calcul, supports et réponse par composante.

Les rendus neutres restent différents, indépendamment de ces curseurs :

| Scène | MAE RGB linéaire neutre | ΔE76 moyen neutre |
| --- | ---: | ---: |
| 0327 | 0,07337 | 15,643 |
| 0037 | 0,04535 | 15,951 |
| 0043 | 0,01766 | 7,844 |

Après soustraction des neutres propres, la MAE moyenne des effets, moyennée
sur douze états de chaque bande, va de **0,00041 à 0,00438**. Elle est globale,
donc atténuée par les pixels non affectés : ce n'est pas un score de parité
colorimétrique. Les détails sont conservés par état dans les JSON, avec MAE
de zone et ΔE76. Les variations « hors support » restent diagnostiques car le
support d'observation Lightroom ne reconstruit pas celui de l'entrée IPR.

## Preuves et reproductibilité

Inventaires préalables :
[0327](../raw/inventaire-color-mixer-intermediaires-0327-2026-10-04-v1.json),
[0037](../raw/inventaire-color-mixer-intermediaires-0037-2026-10-04-v1.json),
[0043](../raw/inventaire-color-mixer-intermediaires-0043-2026-10-04-v1.json).

Mesures immuables :
[rouge/orange/jaune/cyan/bleu](../raw/mesure-color-mixer-intermediaires-0327-2026-10-04-v1.json),
[vert 0037](../raw/mesure-color-mixer-intermediaires-0037-2026-10-04-v1.json),
[violet/magenta](../raw/mesure-color-mixer-intermediaires-0043-2026-10-04-v1.json),
[vert 0327 complément non apparié](../raw/mesure-color-mixer-intermediaires-green-supplement-0327-2026-10-04-v1.json).

Empreintes SHA-256 des sources présentes lors de la mesure (arbre local non
committé ; ce n'est pas une identification certifiée du binaire des exports) :

```text
tests/analyse_color_mixer.py
789B9E8F9EF702B4C65EDF7538911A4EFE40DEEC66C286EED375D5B6979B6D23
src/core/ColorMixer.cpp
B31B1794643B3C7B89BA5AF5DB76F5816BE4337906D32F740CF4283B3C0A4C9C
```

Les six paires archive/actif sont identiques par SHA-256. Empreintes des neutres :

```text
6V1A0037.tif      960A7C78258D83936CED167A2F0E33BE06C03F00F89D4A387C9BA370BA03CB34
6V1A0037-IPR.tiff B0304B6F6636967C91EAEBD527144E078C7E83684FB6997B98DC4957CC06C64C
6V1A0043.tif      C6C941AC0FA653D645176A5DFFC52F480F82BB3CEAD358B6FB9655E60716532F
6V1A0043-IPR.tiff D5976169C81213481B9DDFAC406AE8D0B54AB668FB217C3C2A5E0D5FE2162842
6V1A0327.tif      724ED17795BC4BB7C199EA40522287DE6400A78378AB538BF0E509961F1D38A2
6V1A0327-IPR.tiff D3C23B03ED54A0FC0EC8C5B9C6CBB1951AD55965EA12B3CE4EF2ABD5D800CDB9
```

Le comparateur garde le mode historique `extremes` par défaut ; cette campagne
nécessite `--levels intermediate`. Les tests couvrent les douze états, leur
audit XMP, le point zéro, les inversions, le refus de valider sans neutre et
le cas de douze sorties identiques. **Treize tests du comparateur réussis**
avec sélection automatique ciblée ; six tests du sélecteur et onze CTest
natifs Windows validés au bootstrap. Build Release réussi, checkpoint à jour.
Voir [contrat de build](../../architecture/tests-cibles-et-build-windows.md).

Les TIFF, RAW, sidecars et préférences utilisateur n'ont pas été modifiés.
Les anciens TIFF ±100 restent archivés ; les nouvelles images restent actives.

## Suite recommandée

1. Valider visuellement aperçu/export et transitions de bandes sur aplats,
   faibles chromas, carnations et gradients bleu/violet/magenta.
2. Si l'on veut clore le diagnostic historique rouge/bleu, réexporter leurs
   états ±100 avec exactement le même état de base tracé ; ne pas conclure
   rétrospectivement à partir des seuls ±25/±50.
3. Passer ensuite au protocole color grading, sans modifier ici la calibration
   HSL. La parité Lightroom et l'amélioration du développement neutre restent
   deux chantiers dédiés.
