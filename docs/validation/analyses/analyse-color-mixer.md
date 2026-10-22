# Mélangeur de couleurs — rapport consolidé

Consolidation du 8 octobre 2026. Les campagnes restent séparées : un résultat historique ne valide pas une révision ultérieure. La première campagne ci-dessous est la plus récente.

État actuel : [matrice de validation](../matrice-validation.md). Méthodes communes : [protocoles](../protocols/README.md).

<a id="campagne-1"></a>

## Campagne : analyse-color-mixer-intermediaires-2026-10-04.md

## Mélangeur huit bandes — progressivité à ±25/±50

**Date :** 4 octobre 2026. **Protocole :**
[valeurs intermédiaires](../protocols/protocole-color-mixer-intermediaires-2026-10-04.md).
**Analyse :** comparateur version 4, moteur huit bandes après
[ADR-0005](../../decisions/ADR-0005-melangeur-huit-bandes-luminance-bornee.md).
Le moteur photographique n'a pas été modifié pendant cette campagne.

### Conclusion et décision

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

### Corpus et audit

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

#### Complément vert / 0327

Les douze variantes IPR supplémentaires ont aussi été mesurées, séparément.
Leurs trois progressions moyennes sont croissantes, mais leur masque ne contient
que **134 pixels (0,0104 %)**. Les douze états Lightroom correspondants sont
absents ; ils apparaissent explicitement comme manquants dans cet audit.
Ce complément n'entre pas dans les 192 variantes appariées ni dans la validation
de calibration du vert. La scène 0037 apporte la couverture pertinente.

### Méthode et portée

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

### Réponses IPR mesurées

Les quatre colonnes correspondent aux commandes −50, −25, +25, +50.
Le zéro propre est respectivement 0°, ΔS=0 et Y/Y₀=1.

#### Teinte — ΔH moyen en degrés

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

#### Saturation — ΔS moyen en HSV linéaire

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

#### Luminance — rapport Y/Y₀

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

### Conservation de luminance et sortie

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

### Lightroom : comparaison, pas égalité

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

### Preuves et reproductibilité

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

### Suite recommandée

1. Valider visuellement aperçu/export et transitions de bandes sur aplats,
   faibles chromas, carnations et gradients bleu/violet/magenta.
2. Si l'on veut clore le diagnostic historique rouge/bleu, réexporter leurs
   états ±100 avec exactement le même état de base tracé ; ne pas conclure
   rétrospectivement à partir des seuls ±25/±50.
3. Passer ensuite au protocole color grading, sans modifier ici la calibration
   HSL. La parité Lightroom et l'amélioration du développement neutre restent
   deux chantiers dédiés.

<a id="campagne-2"></a>

## Campagne : analyse-color-mixer-huit-bandes-2026-10-04.md

## Mélangeur huit bandes — vérification après correction

**Date :** 4 octobre 2026. **Périmètre :** nouveaux TIFF IPR, huit bandes,
extrêmes −100/+100 ; Lightroom utilisé comme référence photographique,
pas comme définition obligatoire du modèle IPR.

**Dernière consolidation :** fichiers bleus complétés, référence LR corrigée
et nouveau neutre IPR de 0327 mesuré. Les données brutes des passes précédentes
restent conservées ; elles décrivent l'état des fichiers au moment de leur mesure.

**Archivage après clôture des extrêmes :** les 127 TIFF de `base/color-mixer`
(y compris les variantes LR supplémentaires) sont déplacés dans
`tests/image/archive/color-mixer/2026-10-04-extremes/base/color-mixer`.
Les six neutres sont copiés à côté ; RAW et neutres actifs restent dans `base`.
Le manifeste de l'archive conserve chemins, tailles et dates. Les chemins des
mesures brutes décrivent leur emplacement historique et ne sont pas réécrits.
Pour reproduire cette campagne, ajouter au comparateur :
`--root tests/image/archive/color-mixer/2026-10-04-extremes/base`.

### Conclusion

Le correctif de luminance est confirmé sur les séries RAW exploitables :
l'éclaircissement excessif des couleurs sombres a disparu. Sur le vert de
6V1A0037, le rapport Y à +100 passe d'environ ×19,74 dans la campagne historique
à **×1,234** ; sur 6V1A0043, de ×22,08 à **×1,209**. Teinte et saturation
préservent pratiquement la luminance moyenne des zones mesurées.

Violet et magenta disposent maintenant de séries distinctes sur 6V1A0043,
avec des réponses différentes et non nulles. Leurs amplitudes restent très
différentes de Lightroom, surtout pour le violet : la séparation fonctionnelle
est étayée, **la calibration photographique n'est pas finalisée**.

Le **fonctionnement aux extrêmes est confirmé sur le corpus complet** :
54 variantes IPR et 54 Lightroom, soit 27 paires sur neuf séries couleur/scène.
Chaque paire IPR produit une différence non nulle et une réponse moyenne dans
le sens attendu pour la composante commandée. Il n'y a plus de fichier manquant
ni de réglage XMP parasite dans les références Lightroom retenues.

Cela clôture cette vérification fonctionnelle à −100/+100, pas toute la recette
photographique : les petits résidus de Y rouge/bleu persistent malgré le neutre
frais ; les valeurs intermédiaires ±25/±50 et la recette visuelle aperçu/export
restent à vérifier. La reproduction de Lightroom n'est pas validée.

Aucune amplitude du moteur n'est modifiée à partir de ces seules mesures.
La commande, la conservation de Y et la proximité avec Lightroom sont trois
critères distincts ; un résultat positif sur l'un ne valide pas les autres.

### Corpus audité

Les paires restent : 1/2 teinte, 3/4 saturation, 5/6 luminance ; impair −100,
pair +100. Le fichier sans suffixe dans une couleur est une variante, pas un
neutre. Les neutres des trois scènes sont directement dans `tests/image/base`.

| Série | IPR présents / 6 | Lightroom présents / 6 | Utilisation |
| --- | ---: | ---: | --- |
| Rouge, orange, jaune / 6V1A0327 | 6 chacun | 6 chacun | RAW régénérés, remesurés avec le nouveau neutre ; résidu rouge persistant. |
| Cyan / 6V1A0327 | 6 | 6 | Analyse relative aux neutres de chaque application. |
| Bleu / 6V1A0327 | 6 | 6 | Série complète ; LR luminance +100 corrigé et audité. |
| Vert / 6V1A0037 | 6 | 6 | Analyse principale du vert. |
| Vert, violet, magenta / 6V1A0043 | 6 chacun | 6 chacun | Analyse relative aux neutres ; comparaison des deux dernières bandes. |

Soit **54 variantes IPR et 54 variantes Lightroom**, réparties sur neuf séries
couleur/scène. Les séries LR supplémentaires sans nouvelle série IPR associée
ne sont pas utilisées pour simuler une validation manquante.

Fichiers initialement manquants dans `tests/image/base/color-mixer/blue`,
désormais reçus et mesurés :

- `6V1A0327-IPR-2.tiff` : IPR, teinte +100 ;
- `6V1A0327-6.tif` : Lightroom, luminance +100.

Le premier LR bleu n°6 ajouté activait luminance jaune +100 et bleue +100 ;
il a été exclu de la comparaison isolée, puis réexporté par l'utilisateur avec
uniquement le bleu actif. L'inventaire v5 et la mesure bleu v3 auditent cet état
corrigé. La mesure cyan/bleu v2, faite pendant les remplacements, garde un état
bleu incomplet et une référence LR non conforme : ne pas l'utiliser pour clôturer
le bleu, même si certains de ses chiffres restent utiles au diagnostic.

Les métadonnées des 54 variantes Lightroom finalement retenues indiquent la bonne
composante isolée et le bon signe. L'audit ne relève pas de différence des
autres réglages XMP avec le neutre LR associé. Les TIFF sont sRGB 16 bits ;
l'alpha IPR échantillonné est opaque sur toutes les variantes mesurées.
Les états IPR restent déclarés par convention de noms et par l'opérateur :
leurs TIFF ne contiennent pas un manifeste de réglages permettant de certifier
le RAW source, le retour à zéro des autres commandes ou le binaire utilisé.

### Méthode et limites

Lecture NumPy des valeurs natives 16 bits, puis conversion float32 et
décodage analytique sRGB. Pas de conversion préalable en 8 bits, interpolation
ou redimensionnement. Chaque scène utilise un pas spatial de 5, soit
**1 291 776 pixels réels** échantillonnés, non la totalité des pixels.

La luminance relative est `Y = 0,2126 R + 0,7152 G + 0,0722 B` en linéaire.
Les masques communs sont définis sur le neutre LR : teinte HSV linéaire à ±15°
du centre IPR, S ≥ 0,15 et 0,01 < Y < 0,9. Ils ne reproduisent ni les masques
du moteur IPR avant rendu ni ceux, propriétaires, de Lightroom.

Les neutres RAW IPR font 4639 × 6959 ou 6959 × 4639 pixels ; Lightroom fait
4640 × 6960 ou 6960 × 4640. Le meilleur recadrage de bord LR est choisi parmi
quatre candidats sur les contours échantillonnés. Leurs scores sont proches,
notamment sur 6V1A0037 : cela n'établit pas une registration géométrique exacte.
MAE et ΔE76 sont des diagnostics, pas un verdict pixel-à-pixel définitif.

Dans la première passe rouge/orange/jaune, les variantes IPR faisaient
**4640 × 6960**, comme le TIFF
Lightroom, avec des contours beaucoup plus proches de cette référence que le
neutre RAW IPR. Cela est compatible avec une ouverture du TIFF comme source,
mais ne la prouve pas. Une simple découpe d'un pixel ne corrigerait pas un
changement de rendu d'entrée. Le mode explicite `--without-ipr-reference`
exclut donc le neutre incompatible : réponses +100/−100 mesurées, mais aucun
rapport Y IPR/neutre ni MAE de réponse relative IPR/LR n'est calculé.

**Mise à jour pendant la campagne :** les trois séries ont ensuite été
régénérées par l'utilisateur, entre 16 h 53 et 16 h 55. Elles font maintenant
**4639 × 6959**, comme le RAW neutre IPR. Elles sont remesurées dans des JSON v2,
sans remplacer le diagnostic v1. Les chiffres du tableau principal ci-dessous
proviennent des variantes RAW v2, pas des premières séries incompatibles.

### Réponses des séries RAW exploitables

Les valeurs sont des moyennes de zone, dans l'ordre −100 puis +100.
ΔS est exprimé sur l'échelle 0–1 ; Y est le rapport à la zone du neutre
de **la même application**. La couverture décrit le masque LR, pas le nombre
de pixels recevant un poids important dans le moteur IPR.

| Bande / scène | Pixels de zone | ΔH IPR | ΔS IPR | Rapport Y IPR | Rapport Y Lightroom |
| --- | ---: | --- | --- | --- | --- |
| Rouge / 0327, RAW v2 | 79 228 | −13,07° / +13,07° | −0,4557 / +0,2388 | ×0,7983 / ×1,3177 | ×0,4509 / ×1,6886 |
| Orange / 0327, RAW v2 | 656 747 | −14,43° / +14,44° | −0,3801 / +0,3209 | ×0,7502 / ×1,3369 | ×0,7561 / ×1,2713 |
| Jaune / 0327, RAW v2 | 102 029 | −13,69° / +13,69° | −0,4140 / +0,4045 | ×0,7645 / ×1,3088 | ×0,5223 / ×1,3804 |
| Vert / 0037 | 241 381 | −11,18° / +11,18° | −0,2825 / +0,2645 | ×0,8198 / ×1,2342 | ×0,7120 / ×1,3395 |
| Vert / 0043 | 5 086 | −9,60° / +9,62° | −0,2027 / +0,1983 | ×0,8321 / ×1,2088 | ×0,7239 / ×1,3138 |
| Cyan / 0327 | 3 959 | −14,31° / +14,30° | −0,2836 / +0,2639 | ×0,7491 / ×1,3380 | ×0,6716 / ×1,2924 |
| Bleu / 0327, consolidé v3 | 2 803 | −6,66° / +6,67° | −0,0541 / +0,0544 | ×0,9233 / ×1,1121 | ×0,9269 / ×1,0767 |
| Violet / 0043 | 1 854 | −3,08° / +3,08° | −0,0435 / +0,0429 | ×0,9174 / ×1,1042 | ×0,5829 / ×1,3729 |
| Magenta / 0043 | 5 678 | −8,05° / +8,04° | −0,2068 / +0,1950 | ×0,8118 / ×1,2457 | ×0,8957 / ×1,0910 |

#### Luminance et neutres

Les rapports de luminance mesurés restent dans l'intervalle du modèle de gain
linéaire [1/√2, √2], contrairement à l'ancienne interpolation V→1. C'est une
confirmation sur ce corpus, pas une preuve que chaque pixel TIFF respecte
la même borne après gamut mapping, quantification et autres étapes du rendu.

Pour teinte/saturation, l'écart du rapport Y à 1 reste inférieur à **0,0085 %**
sur vert, cyan, violet et magenta. Plusieurs états bleus présentent un résidu d'environ
**+0,67 %** en teinte −100 et saturation ±100 ; le nouveau bleu teinte +100
reste près de 1 (×0,999967). Les deux extrêmes de teinte n'ont donc pas exactement
la même base apparente de Y. Ne pas attribuer une cause précise au seul curseur
sans audit des paramètres et des étapes du rendu.

La nouvelle série rouge présente également un résidu stable de **+1,907 %**
en teinte ±100 et saturation −100, et **+1,918 %** en saturation +100. Ce n'est
pas une preuve de variation de luminance due à ces commandes : le décalage
commun aux états suggère de contrôler d'abord la base, sans en certifier la
cause. Sur orange, les résidus valent environ +0,027 à +0,035 % ; sur jaune,
l'écart reste inférieur à 0,0032 %. Un neutre de 0327 entièrement réinitialisé
et exporté dans la même session a été reçu à 16 h 58. Ses grandeurs mesurées
(Y moyen et écarts au neutre LR) sont identiques à celles du neutre précédent ;
les résidus rouge/bleu restent présents dans les nouvelles mesures.
Le remplacement du neutre ne suffit donc pas à expliquer ou supprimer ces
différences. Leur cause reste ouverte : contrôler les états exacts des exports
et la chaîne complète de rendu, sans modifier arbitrairement un gain HSL.

La sonde moteur conserve les cinq niveaux de gris pour les **48 états**
(8 bandes × 3 commandes × 2 extrêmes), et les tests mathématiques vérifient
la conservation en flottant. Cela est distinct de la conservation des couleurs
quasi neutres et des effets éventuels du gamut mapping sur une photographie.

#### Violet / magenta et calibration

Les six états de chaque bande produisent des réponses mesurables et leurs
paires ne sont pas identiques. La séparation ne constitue donc plus le blocage
structurel de la campagne précédente.

Sur le violet de 0043, la paire de teinte parcourt environ 6,16° dans IPR,
contre 105,90° dans LR. Sur le magenta, elle parcourt 16,09° contre 14,40°.
La paire de saturation magenta vaut environ 0,4017 dans IPR contre 0,0865
dans LR ; celle du violet 0,0864 contre 0,5429. Les écarts ne se corrigent donc
pas avec un multiplicateur global commun aux huit bandes.

Seuls environ 32 % des pixels du masque violet LR montrent une hausse de
teinte/saturation au-delà du seuil de mesure entre les extrêmes IPR. Ce chiffre
n'est pas une inversion de commande : les masques sont calculés sur une autre
base colorimétrique, et les centres avant/après profil et courbe peuvent différer.
Il faut une mire ou une seconde scène colorée, puis mesurer les supports dans
le RGB de travail IPR, avant d'élargir arbitrairement le violet ou son amplitude.

#### Clipping et rendu de base

Sur les variantes IPR, le maximum de pixels ayant au moins un canal à la borne
haute vaut environ 0,537 % pour vert/0037, 0,0011 % pour vert/0043, 0,0056 %
pour violet/0043 et 0,0496 % pour magenta/0043 ; aucun n'est détecté dans
l'échantillon cyan/bleu. Ce sont des fractions totales, pas des accroissements
attribuables au seul mélangeur, ni une inspection des halos ou des transitions.

Les neutres IPR restent plus sombres que LR : Y moyen 0,04705 contre 0,12045
sur 0327, 0,03667 contre 0,06524 sur 0037 et 0,01382 contre 0,02824 sur 0043.
Les MAE directes restent influencées par cette base. Ne pas modifier l'exposition
ou un gain HSL pour annuler cet écart sans une étude séparée du rendu neutre.
Les nouvelles variantes RAW rouge/orange/jaune n'ont pas de canal détecté
à la borne haute dans l'échantillon et leur alpha échantillonné est opaque.

### Diagnostic rouge / orange / jaune — non validant

Le mode sans neutre confirme que les paires produisent des différences dans
le sens global attendu, sur une source de dimensions identiques à LR.
Les amplitudes ci-dessous ne doivent pas servir à calibrer les exports RAW :

| Bande | Pixels de zone | ΔH de paire IPR | ΔS de paire IPR |
| --- | ---: | ---: | ---: |
| Rouge | 79 182 | +30,22° | +0,8529 |
| Orange | 656 871 | +26,85° | +0,7425 |
| Jaune | 102 049 | +29,04° | +0,8729 |

Ces mesures sont conservées comme diagnostic avant régénération. Ne pas les
fusionner avec la future passe RAW ou leur affecter le neutre RAW existant.

### Preuves et reproduction

Mesures immuables de cette passe :

- [Cyan/bleu 0327](../raw/mesure-color-mixer-huit-bandes-cyan-blue-0327-2026-10-04-v1.json).
- [Vert 0037](../raw/mesure-color-mixer-huit-bandes-green-0037-2026-10-04-v1.json).
- [Vert/violet/magenta 0043](../raw/mesure-color-mixer-huit-bandes-green-violet-magenta-0043-2026-10-04-v1.json).
- [Rouge/orange/jaune, diagnostic sans neutre](../raw/mesure-color-mixer-huit-bandes-red-orange-yellow-diagnostic-0327-2026-10-04-v1.json).
- [Rouge/orange RAW régénérés, v2](../raw/mesure-color-mixer-huit-bandes-red-orange-0327-2026-10-04-v2.json).
- [Jaune RAW régénéré, v2](../raw/mesure-color-mixer-huit-bandes-yellow-0327-2026-10-04-v2.json).
- **Consolidation avec le neutre frais :**
  [rouge/orange/jaune v3](../raw/mesure-color-mixer-huit-bandes-red-orange-yellow-0327-2026-10-04-v3.json),
  [cyan, dans la passe cyan/bleu v2](../raw/mesure-color-mixer-huit-bandes-cyan-blue-0327-2026-10-04-v2.json),
  [bleu complet et LR corrigé v3](../raw/mesure-color-mixer-huit-bandes-blue-0327-2026-10-04-v3.json).
  Pour le bleu, seule cette dernière mesure clôture les extrêmes de cette campagne.
- Inventaires : [0327](../raw/inventaire-color-mixer-huit-bandes-0327-2026-10-04-v1.json),
  [0037](../raw/inventaire-color-mixer-huit-bandes-0037-2026-10-04-v1.json),
  [0043](../raw/inventaire-color-mixer-huit-bandes-0043-2026-10-04-v1.json).
- [Inventaire consolidé 0327 après régénération, v3](../raw/inventaire-color-mixer-huit-bandes-0327-2026-10-04-v3.json).
  L'inventaire intermédiaire rouge/orange/jaune v2 capture la régénération en
  cours ; il n'est pas l'inventaire final de la série jaune.
- [Inventaire 0327 v4](../raw/inventaire-color-mixer-huit-bandes-0327-2026-10-04-v4.json) :
  fichiers présents mais LR bleu n°6 avec deux luminances actives.
- [Inventaire 0327 v5, conforme](../raw/inventaire-color-mixer-huit-bandes-0327-2026-10-04-v5.json) :
  aucun fichier manquant, aucun réglage XMP incohérent après réexport LR.

Commandes depuis la racine du dépôt, Python NumPy/Pillow sous WSL ; utiliser
un nouveau nom de sortie à chaque nouvelle passe :

```sh
python3 -m unittest discover -s tests -p test_analyse_color_mixer.py
python3 tests/analyse_color_mixer.py cyan blue --ipr-mixer-version eight --analyse --output NOUVELLE-MESURE-0327.json
python3 tests/analyse_color_mixer.py green --image-id 6V1A0037 --ipr-mixer-version eight --analyse --output NOUVELLE-MESURE-0037.json
python3 tests/analyse_color_mixer.py green violet magenta --image-id 6V1A0043 --ipr-mixer-version eight --analyse --output NOUVELLE-MESURE-0043.json
python3 tests/analyse_color_mixer.py red orange yellow --ipr-mixer-version eight --analyse --output NOUVELLE-MESURE-RAW-0327.json
python3 tests/analyse_color_mixer.py red orange yellow --ipr-mixer-version eight --without-ipr-reference --analyse --output NOUVEAU-DIAGNOSTIC-SANS-NEUTRE.json
ctest --test-dir build-wsl-libraw -R Tests --output-on-failure
build-wsl-libraw/colorMixerNeutralProbe
```

**Vérifications : 7 tests Python et 11 tests CTest réussis.** Le nouveau test
du comparateur vérifie qu'un neutre explicitement exclu n'est pas lu et ne
produit aucune métrique IPR relative à ce neutre, tout en conservant les paires.
Aucune photo, aucun sidecar, aucune préférence personnelle ni aucun paramètre
du moteur de rendu n'est modifié par cette campagne.

### Actions pour clôturer

**Suivi après cette campagne :** les
[mesures intermédiaires ±25/±50](analyse-color-mixer-intermediaires-2026-10-04.md)
confirment la progressivité moyenne des huit bandes et ne reproduisent pas les
résidus rouge/bleu (dérive Y <0,01 %, neutres identiques à l'archive).
Cette nouvelle passe ne réécrit pas les résultats ±100 ci-dessous ni ne démontre
l'origine des anciens écarts ; elle clôture le contrôle des valeurs intermédiaires
sur ce nouveau corpus, pas la recette visuelle ou la parité Lightroom.

La recette fonctionnelle aux extrêmes est clôturée sur ce corpus. Pour clôturer
la stabilité photographique et, éventuellement, la calibration :

1. Investiguer les résidus rouge/bleu persistants : tracer les paramètres
   exacts des exports et comparer Y avant/après module et après sortie sRGB.
   Un nouveau neutre seul n'a pas résolu ce point ; aucun réglage du moteur
   n'est changé sur la seule base d'une moyenne de zone.
2. Contrôler ±25/±50, transitions entre bandes, aplats peu saturés, carnations
   et cohérence aperçu/export. Les extrêmes ne prouvent pas la progressivité.
3. Si l'objectif reste une commande propre à IPR : accepter/documenter ses
   amplitudes après recette visuelle. Si l'objectif est la parité Lightroom :
   ouvrir une campagne dédiée sur les supports et la calibration, sans confondre
   la base RAW, les commandes HSL et le gamut mapping.

Références internes : [protocole](../protocols/protocole-validation-color-mixer-2026-10-04.md),
[campagne historique](analyse-color-mixer-corpus-etendu-2026-10-04.md),
[ADR-0005](../../decisions/ADR-0005-melangeur-huit-bandes-luminance-bornee.md).

<a id="campagne-3"></a>

## Campagne : analyse-color-mixer-corpus-etendu-2026-10-04.md

## Mélangeur — Vert complémentaire et autres couleurs

**Date :** 4 octobre 2026. **Périmètre :** vert sur 6V1A0037/6V1A0043 ; orange,
cyan et bleu sur 6V1A0327 ; violet et références magenta sur 6V1A0327/6V1A0043.
Cette campagne complète [la première analyse](analyse-color-mixer-red-yellow-green-2026-10-04.md).

### Conclusion

Les nouvelles images rendent le **vert suffisamment représenté** pour observer
sa réponse. Les trois commandes fonctionnent sur les deux scènes, mais la teinte
est beaucoup moins ample que Lightroom et la luminance +100 est beaucoup plus
forte. Il ne s’agit plus d’un résultat fondé sur quelques pixels de bruit.

Orange et cyan montrent aussi des réponses dans le bon sens, avec une luminance
sélective trop énergique **par rapport à la référence Lightroom choisie**. La
comparaison bleu est partielle : le TIFF Lightroom +100 de luminance reçu lors
de la mesure est en réalité marqué −100.

Violet et magenta ne sont pas encore deux bandes indépendantes dans IPR.
L’utilisateur confirme avoir utilisé le dernier cercle pour les exports violet ;
le code l’appelle `Magentas` et le centre moteur est 300°. Comparer ces exports
au `Purple` Lightroom n’est donc **pas une comparaison à bandes identiques**.
Les références Lightroom magenta sont mesurées, sans inventer d’exports IPR
absents. Le mélangeur complet ne peut pas être déclaré validé.

Le défaut déjà confirmé sur les gris en luminance rouge demeure à corriger.
Aucune modification du rendu, des bandes ou des sidecars n’est effectuée ici.

### Protocole et mesures conservées

Le [protocole](../protocols/protocole-validation-color-mixer-2026-10-04.md) garde
les paires (1,2) teinte, (3,4) saturation, (5,6) luminance, avec impair −100 et
pair +100. Les TIFF sans suffixe dans une couleur sont des variantes −100,
pas des neutres. Les neutres sont directement dans `tests/image/base`.

Mesures de référence pour cette campagne :

- [Vert 6V1A0037, v2](../raw/mesure-color-mixer-green-6V1A0037-2026-10-04-v2.json).
- [Vert/violet/magenta 6V1A0043](../raw/mesure-color-mixer-green-violet-magenta-6V1A0043-2026-10-04-v1.json).
- [Orange/cyan/bleu/violet/magenta 6V1A0327](../raw/mesure-color-mixer-orange-cyan-blue-violet-magenta-6V1A0327-2026-10-04-v1.json).
- [Bleu/violet 6V1A0327, v2](../raw/mesure-color-mixer-blue-violet-6V1A0327-2026-10-04-v2.json) :
  paires IPR violet complétées, référence LR bleu n°6 absente.
- [Violet 6V1A0043, v2](../raw/mesure-color-mixer-violet-6V1A0043-2026-10-04-v2.json) :
  dernière paire IPR complétée.
- [Audit du nouveau TIFF n°7](../raw/inventaire-export-supplementaire-6V1A0327-7-2026-10-04.json) :
  jaune +100 et Purple +100 simultanés, pas une référence bleu +100.

Les fichiers précédents restent conservés ; une régénération ultérieure d’un
TIFF ne modifie pas rétroactivement l’état enregistré dans ces mesures.

Le lecteur travaille en RGB natif 16 bits puis float32, sans conversion 8 bits
ni interpolation. Le pas est 5, soit 1 291 776 pixels réels par scène. Pour les
nouvelles passes, l’alignement privilégie le **neutre IPR** ; les quatre choix de
recadrage d’un pixel LR sont comparés, sans certifier une registration optique.
Les réponses sont relatives au neutre de leur propre application. Les masques
communs viennent du HSV **RGB linéaire de sortie du neutre LR**, pas du RAW
avant pipeline ni des masques propriétaires Lightroom.

Le comparateur a été étendu pour accepter une série incomplète ou LR seule,
signaler les états manquants, auditer les profils sRGB et distinguer Purple de
Magenta. Le masque diagnostique violet est centré à 270° ; il **n’ajoute aucune
bande au moteur** et ne change pas les supports des sept bandes IPR.

### Couverture des scènes

| Bande / scène | Pixels au cœur du masque | Part de l’échantillon | Usage |
| --- | ---: | ---: | --- |
| Vert / 6V1A0037 | 241 381 | 18,686 % | Scène principale pour le vert. |
| Vert / 6V1A0043 | 5 086 | 0,394 % | Confirmation sur une seconde scène. |
| Orange / 6V1A0327 | 656 747 | 50,841 % | Très bonne couverture de cette bande. |
| Cyan / 6V1A0327 | 3 959 | 0,306 % | Tendances mesurables, autre scène souhaitable. |
| Bleu / 6V1A0327 | 2 803 | 0,217 % | Tendances mesurables, référence +100 à corriger. |
| Violet / 6V1A0327 | 543 | 0,042 % | Insuffisant seul. |
| Violet / 6V1A0043 | 1 854 | 0,144 % | Meilleur corpus, mais bande IPR non équivalente. |
| Magenta / 6V1A0327 | 163 | 0,0126 % | Insuffisant et pas de variante IPR. |
| Magenta / 6V1A0043 | 5 678 | 0,440 % | Références LR exploitables, pas de variante IPR. |

Le cœur correspond à ±15° du centre, S ≥ 0,15 et 0,01 < Y < 0,9 sur le neutre LR.
Ces effectifs ne mesurent pas toute l’étendue d’un masque interne de développement.

### Vert : résultats sur les deux nouvelles images

Les valeurs suivantes sont des moyennes de zone ; elles ne sont pas des gains
à recopier dans le moteur. La variation S est exprimée sur l’échelle 0–1.

| Scène / commande, −100 puis +100 | Lightroom | IPR |
| --- | --- | --- |
| 6V1A0037 — Δ teinte | Environ −57,5° / +42,1° | Environ −11,2° / +11,2° |
| 6V1A0037 — Δ S | Environ −0,614 / +0,267 | Environ −0,283 / +0,265 |
| 6V1A0037 — rapport Y, luminance | Environ ×0,712 / ×1,340 | Environ ×0,409 / ×19,74 |
| 6V1A0043 — Δ teinte | −59,19° / +46,83° | −9,59° / +9,61° |
| 6V1A0043 — Δ S | −0,435 / +0,315 | −0,203 / +0,199 |
| 6V1A0043 — rapport Y, luminance | ×0,724 / ×1,314 | ×0,461 / ×22,08 |

Le vert n’est pas inactif et les commandes ne sont pas inversées. La réponse
est moins ample en teinte, moins désaturante au minimum, et plus énergique en
luminance aux deux extrêmes. La luminance +100 présente la même tendance sur
deux scènes distinctes ; elle mérite une refonte du modèle de gain, pas une
conclusion basée sur la seule première image pauvre en verts.

Les grands rapports sont aussi favorisés par les zones initialement sombres.
Ils ne signifient ni un facteur uniforme sur toute l’image, ni une exposition
appliquée à l’ensemble de la scène. Le passage à un V HSV proche de 1 peut
cependant créer une hausse très importante dans une plage colorée sombre.

### Orange et cyan

| Bande / commande, −100 puis +100 | Lightroom | IPR |
| --- | --- | --- |
| Orange — Δ teinte | −18,86° / +25,60° | −14,43° / +14,44° |
| Orange — Δ S | −0,462 / +0,168 | −0,380 / +0,321 |
| Orange — rapport Y, luminance | ×0,756 / ×1,271 | ×0,166 / ×16,25 |
| Cyan — Δ teinte | −30,26° / +42,12° | −14,30° / +14,30° |
| Cyan — Δ S | −0,277 / +0,325 | −0,284 / +0,264 |
| Cyan — rapport Y, luminance | ×0,672 / ×1,292 | ×0,163 / ×5,18 |

Les commandes agissent dans le sens attendu. Orange +100 de saturation est plus
fort que LR, tandis que le cyan a une amplitude de saturation plus proche de la
référence sur cette scène. La luminance est plus extrême dans les deux bandes.
L’orange est particulièrement probant car il représente plus de la moitié des
échantillons : cet écart ne vient pas d’un masque portant sur quelques pixels.

Comme pour les premières bandes, conserver V pendant une saturation HSV ne
conserve pas Y. La saturation orange IPR −100 augmente ici Y d’environ 21 %, et
+100 le réduit d’environ 19 % dans le masque. Il faut distinguer ce couplage
entre paramètres d’un défaut de direction du curseur de saturation.

### Bleu : résultat partiel

- Teinte LR : −47,71° / +52,01° ; IPR : −7,22° / +7,11°.
- Saturation LR : −0,141 / +0,163 ; IPR : −0,0575 / +0,0575.
- Luminance −100 : LR ×0,927 ; IPR ×0,709.
- Luminance +100 IPR : ×10,27 ; **pas de comparaison LR +100 valide** dans la
  passe enregistrée.

Le XMP incorporé dans `blue/6V1A0327-6.tif` indique `LuminanceAdjustmentBlue=-100`.
Le comparateur le marque invalide et n’utilise pas cette paire pour valider la
parité de luminance. L’utilisateur a été invité à le régénérer à +100.
Les chiffres teinte/saturation restent exploitables ; l’absence d’un état LR
correct n’efface pas toutes les mesures de cette bande.

À la seconde passe, ce fichier n°6 n’est plus présent. Le nouveau
`violet/6V1A0327-7.tif` contient `LuminanceAdjustmentYellow=+100` et
`LuminanceAdjustmentPurple=+100`, sans réglage bleu +100. Il est explicitement
exclu, pas renommé ni substitué. L’emplacement d’un éventuel export bleu correct
a été demandé à l’utilisateur ; sa paire reste à compléter.

### Violet : mesure possible, équivalence impossible pour le moment

L’utilisateur confirme avoir utilisé **le dernier cercle de l’IHM**. Il y a
actuellement sept contrôles, avec un dernier libellé `Magentas`, une pastille
d’apparence violette et un centre moteur 300°. Lightroom possède séparément
`Purple` et `Magenta`. Il manque donc une **distinction indépendante entre ces
deux couleurs**, pas simplement un nom ou une pastille à renommer.

Sur 6V1A0043, le masque violet fournit 1 854 pixels. Les exports disponibles
donnent par exemple une teinte LR −54,03° / +51,87°, contre −5,50° / +5,50°
avec le dernier contrôle IPR. Ce constat décrit deux opérations différentes :
il ne démontre pas un défaut de gain sur une hypothétique bande Purple IPR.
Les comparaisons croisées sont explicitement marquées **non équivalentes**.

États IPR manquants lors de la première passe :

- 6V1A0327 : n°2, n°4 et n°6.
- 6V1A0043 : n°6.

L’utilisateur les a ensuite ajoutés ; les deux séries IPR violet sont maintenant
**complètes et remesurées en v2**. Sur 6V1A0043, la luminance +100 du dernier
contrôle donne ×4,640 de Y, contre ×1,373 pour Purple LR ; sur 6V1A0327, ×5,270
contre ×1,060. Ces chiffres portent toujours sur des bandes non équivalentes.
L’analyse de 6V1A0043
confirme qu’un corpus violet existe, mais la séparation des bandes demeure
un préalable à sa validation photographique.

### Magenta : références Lightroom seulement

Aucun TIFF IPR magenta n’est présent dans les deux séries. Les six états LR
ont été mesurés pour chaque image ; aucune MAE IPR/LR magenta ni aucun verdict
sur des paramètres IPR non testés n’est produit.

6V1A0043 est la meilleure référence fournie : 5 678 pixels au cœur du masque,
contre 163 pour 6V1A0327. Ses réponses LR pourront servir à la future validation
de la bande dédiée. Ne pas déplacer ou renommer les exports violet actuels en
magenta sans connaître les paramètres réellement utilisés.

### Neutres et écarts de base

| Scène | Y moyen Lightroom | Y moyen IPR |
| --- | ---: | ---: |
| 6V1A0327 | 0,12045 | 0,04705 |
| 6V1A0037 | Environ 0,06524 | Environ 0,03667 |
| 6V1A0043 | 0,02824 | 0,01382 |

IPR est plus sombre sur les trois neutres. Les mesures relatives à chaque
application permettent de séparer en partie cet écart de la réponse du
mélangeur, sans rendre identiques les profils, masques et courbes de développement.
Les écarts ne justifient donc pas des corrections basées sur une MAE directe
unique ou un multiplicateur arbitraire.

### Suite recommandée

**Suivi après autorisation utilisateur :** les étapes 1–3 ci-dessous sont désormais
implémentées : huit bandes, gris protégés, gains cumulés, Y préservé pour teinte
et saturation en linéaire et luminance bornée ±0,5 EV. Voir
[ADR-0005](../../decisions/ADR-0005-melangeur-huit-bandes-luminance-bornee.md).
Les mesures de ce rapport restent celles du moteur antérieur ; elles n'ont pas
été remplacées par des chiffres simulés ni ne valident la nouvelle calibration.
La phrase « non exécutées » plus bas décrit la demande d'analyse initiale.

1. Corriger la protection des achromatiques dans la luminance rouge, puis
   ajouter un vrai test de non-régression sur gris, noirs et faibles chromas.
2. Définir deux bandes clairement identifiées pour violet et magenta si leur
   séparation est retenue : IHM, moteur, sidecar versionné, migration des
   anciennes sept bandes et préférences de sélection. Ajouter une pastille seule
   ne suffit pas ; le contrôle existant doit garder une migration explicite.
3. Revoir le modèle de luminance sélective, qui pousse actuellement V vers 1,
   puis contrôler saturation/Y et transitions sur ces trois scènes avant
   d’ajuster les amplitudes de teinte.
4. Compléter le TIFF LR bleu +100. Les états violet ont été ajoutés, mais les
   bandes restent non équivalentes. Régénérer les
   TIFF IPR après un changement de moteur, en conservant cette campagne comme
   référence avant correction.

Les étapes 1–3 restent des recommandations, pas des modifications exécutées
dans cette demande d’analyse.

### Vérifications

- Comparateur : **5 tests Python réussis**, avec correspondance Purple,
  distinction des centres diagnostics/moteur, audit des séries invalides,
  inspection CLI et correspondance explicite de fichier.
- `colorMixerQmlTests` et `imageExportTests` : réussis.
- La sonde précédente sur les gris reste la preuve des deux échecs de
  protection en luminance rouge ; les tests existants réussis ne l’annulent pas.
- Aucune image source, aucun sidecar et aucun réglage du moteur modifié.

<a id="campagne-4"></a>

## Campagne : analyse-color-mixer-red-yellow-green-2026-10-04.md

## Analyse du mélangeur — Rouge, jaune et vert

**Date :** 4 octobre 2026. **Scène :** 6V1A0327.
**Ordre exécuté :** `red`, `yellow`, `green`.

### Conclusion

**Défaut confirmé à corriger en priorité : la luminance rouge agit sur les gris
neutres**, qu’elle transforme en noir à −100 et en blanc à +100 dans la sonde
synthétique. Ce n’est pas seulement une différence de rendu avec Lightroom.

Sur les pixels colorés de cette photo, le rouge et le jaune présentent une
réponse cohérente : teinte, saturation et luminance évoluent dans le sens attendu.
**Leur rendu n’est pas encore équivalent à Lightroom**, surtout en saturation
et en luminance. Le vert a été mesuré, mais le corpus est trop pauvre dans le
cœur de cette bande pour le valider ou calibrer sa réponse.

Cette passe n’autorise donc pas une validation globale du mélangeur. Elle
identifie les points à affiner après la réception des autres bandes, sans
modifier le moteur ni changer arbitrairement ses coefficients sur une scène.

**Campagne suivante :** le [corpus étendu](analyse-color-mixer-corpus-etendu-2026-10-04.md)
ajoute 6V1A0037 et 6V1A0043 et lève le manque de couverture du vert. Il analyse
aussi orange/cyan/bleu et les références violet/magenta, sans corriger le moteur.

### Références et traçabilité

- [Protocole et méthode](../protocols/protocole-validation-color-mixer-2026-10-04.md).
- [Mesure finale v2](../raw/mesure-color-mixer-red-yellow-green-2026-10-04-v2.json) :
  états par fichier, résultats et comparaison à chaque neutre.
- [Inventaire initial](../raw/inventaire-color-mixer-2026-10-04.json) : état avant
  correction de l’export Lightroom jaune +100.
- [Passe exploratoire](../raw/mesure-color-mixer-red-yellow-green-2026-10-04.json) :
  conservée pour traçabilité ; la **v2 fait référence** pour les résultats ci-dessous.
- Comparateur : `tests/analyse_color_mixer.py` ; contrôles déterministes dans
  `tests/test_analyse_color_mixer.py`.
- [Sonde sur gris neutres](../raw/sonde-color-mixer-gris-neutres-2026-10-04.json),
  exécutée par `colorMixerNeutralProbe` via le moteur réel, pas une réimplémentation Python.

### Audit des entrées

Les TIFF Lightroom incorporent la preuve des réglages : (1,2) teinte,
(3,4) saturation, (5,6) luminance, impair −100 et pair +100. Le fichier sans
suffixe constitue le n°1. Les autres réglages XMP sont identiques au neutre LR.

Le fichier Lightroom jaune `6V1A0327-6.tif` était initialement marqué −100.
L’utilisateur l’a régénéré ; le XMP de la passe finale confirme **+100**.
Il a également fourni le TIFF IPR neutre dans `base`. Les **18 états Lightroom**
de la passe finale sont cohérents avec les noms attendus.

Les TIFF LR sont RGB 16 bits sRGB, 4640 × 6960 ; les TIFF IPR sont RGBA
16 bits sRGB, 4639 × 6959. Cet écart connu vient de la zone active RAW et ne
désigne pas une perte introduite par l’export TIFF.
Le neutre LR est sans réglage HSL, mais conserve notamment netteté 40 et bruit
couleur 25. Les TIFF IPR n’incorporent pas de réglages XMP ; leurs états sont
identifiés par la convention fournie par l’utilisateur.

Un fichier supplémentaire `yellow/6V1A0327-IPR5.tiff` a été ignoré : seul
`6V1A0327-IPR-5.tiff` est pris comme variante n°5. Aucun fichier image ni
sidecar source n’a été déplacé ou modifié par l’analyse.

### Méthode et couverture

Mesures sur **1 291 776 pixels RGB réels**, échantillonnés tous les cinq pixels
après recadrage de la référence LR à la taille IPR, sans interpolation ni
conversion 8 bits. Les déplacements de teinte sont calculés en HSV du RGB
linéaire. Y est la luminance linéaire sRGB. Chaque réponse est rapportée au
neutre de son propre moteur ; le masque spatial commun vient du neutre LR.

| Bande | Pixels au cœur du masque | Part de l’échantillon | Capacité à conclure |
| --- | ---: | ---: | --- |
| Rouge | 79 228 | 6,133 % | Suffisant pour constater les tendances sur cette scène. |
| Jaune | 102 029 | 7,898 % | Suffisant pour constater les tendances sur cette scène. |
| Vert | 134 | 0,0104 % | Insuffisant pour valider/calibrer la bande. |

Le recadrage retenu élimine la première ligne et la dernière colonne LR.
Les quatre coûts de contours sont très proches : 0,386716 à 0,387236.
La correspondance de bord reste donc peu discriminante et la MAE est un
diagnostic échantillonné, pas une registration géométrique définitive.

### Rendu neutre : un écart préalable important

| Mesure globale | Lightroom | IPR |
| --- | ---: | ---: |
| Y moyen | 0,12045 | 0,04705 |

La MAE RGB linéaire neutre est **0,07337** et le ΔE76 moyen **15,64**.
IPR produit ici un neutre sensiblement plus sombre. Une comparaison directe
des TIFF modifiés mélange donc différences de base et différences HSL.
Ces chiffres ne suffisent pas à accuser un profil ou un réglage particulier ;
le rendu neutre reste un chantier distinct du comportement du mélangeur.

### Rouge

| Commande, −100 / +100 | Lightroom | IPR |
| --- | --- | --- |
| Déplacement moyen de teinte | −9,39° / +11,95° | −13,07° / +13,07° |
| Variation de saturation S | −0,282 / +0,106 | −0,456 / +0,239 |
| Rapport Y pour la luminance | ×0,451 / ×1,689 | ×0,276 / ×7,248 |

La direction des trois commandes est cohérente. La saturation IPR agit plus
fortement et modifie sensiblement Y : son réglage −100 donne ici ×1,821 de Y,
et +100 ×0,665. Ce n’est pas une inversion de saturation : S diminue/augmente
correctement, mais le modèle HSV à valeur conservée ne conserve pas Y.

La luminance +100 est nettement plus énergique que LR, même après soustraction
de la différence de neutre : la MAE de réponse RGB dans le masque atteint
**0,2920**. Ce point mérite une calibration dédiée si une réponse photographique
proche de Lightroom est l’objectif.

### Jaune

| Commande, −100 / +100 | Lightroom | IPR |
| --- | --- | --- |
| Déplacement moyen de teinte | −26,13° / +33,16° | −13,69° / +13,69° |
| Variation de saturation S | −0,290 / +0,168 | −0,414 / +0,404 |
| Rapport Y pour la luminance | ×0,522 / ×1,380 | ×0,224 / ×4,494 |

La teinte IPR évolue dans le bon sens mais a une amplitude plus faible que LR.
La saturation agit plus fortement, notamment à +100. La luminance est plus
agressive aux deux extrêmes. La MAE de réponse RGB pour luminance +100 atteint
**0,2919** dans le masque, ce qui confirme un écart propre à la réponse, au-delà
de la différence de rendu neutre.

Les valeurs de ces tableaux ne sont pas des facteurs de correction à recopier
dans le code : les masques, le rendu et les transformations ne sont pas identiques
entre les deux applications.

### Vert

| Commande, −100 / +100 | Lightroom | IPR |
| --- | --- | --- |
| Déplacement moyen de teinte | −58,30° / +46,16° | −4,00° / +3,83° |
| Variation de saturation S | −0,335 / +0,358 | −0,0966 / +0,0953 |
| Rapport Y pour la luminance | ×0,769 / ×1,318 | ×0,772 / ×2,657 |

Les extrêmes ne sont pas identiques et les trois réponses existent. Mais ces
résultats portent sur **134 pixels échantillonnés** seulement, définis à partir
du neutre LR, pas sur une plage verte propre et calibrée. Ils ne permettent ni
de conclure à une erreur du vert IPR, ni de déterminer son gain, ni de valider
sa parité. Il faut une seconde scène contenant une surface verte nette ou du
feuillage, accompagnée des mêmes six variantes et deux neutres.

### Explication technique des différences

L’implémentation actuelle dans `ImageEditor::renderWorkingImage()` utilise
des supports triangulaires entre centres de teinte du **RGB linéaire**, puis :

```text
H' = H + 0,18 × teinte × poids
S' = clamp(S × (1 + 0,01 × saturation × poids), 0, 1)
a  = 0,01 × luminance × poids
V' = max(0, V + a × (a >= 0 ? 1 - V : V))
```

- `0,18` limite le déplacement d’une bande centrale à ±18° avant les autres
  étapes du rendu : ce n’est pas une calibration uniforme sur Lightroom.
- Une saturation qui garde V peut changer Y. Le résultat rouge observé est
  cohérent avec cette propriété ; « luminance constante » n’est pas le contrat
  de cette transformation HSV.
- À luminance +100 et poids 1, V tend vers 1, même pour un pixel sombre. Cela
  explique pourquoi un neutre sombre peut recevoir un gain très important.
- La commande « luminance » agit donc actuellement sur **V HSV**, pas
  directement sur Y physique ou L* perceptuel.

Les algorithmes internes de Lightroom ne sont pas déduits de ces seuls exports.
Le fait que ses réponses diffèrent ne prouve pas qu’il applique une formule HSV,
Lab ou un autre modèle précis.

### Défaut isolé — Protection des gris dans la luminance rouge

La lecture du code a motivé une sonde complémentaire sur une image synthétique
de cinq gris `R=G=B`, sans profil RAW ni différence de cadrage. Elle passe par
les API réelles de chargement/réinitialisation/réglage/export PNG d’IPR et utilise
uniquement un répertoire temporaire.

| État | Valeurs exportées des cinq gris, 8 bits | Protection des neutres |
| --- | --- | --- |
| Neutre | 0, 64, 128, 192, 255 | Référence |
| Luminance rouge −100 | 0, 0, 0, 0, 0 | **Échec** |
| Luminance rouge +100 | 255, 255, 255, 255, 255 | **Échec** |
| Luminance jaune ±100 | 0, 64, 128, 192, 255 | Préservée |
| Luminance verte ±100 | 0, 64, 128, 192, 255 | Préservée |

Cause confirmée : `rgbToHsv()` donne H=0 lorsque la chroma est nulle. Le masque
rouge reçoit donc un poids 1, et la luminance sélective modifie V sans tenir
compte de l’absence de couleur. Au minimum V devient 0 ; au maximum V devient 1.
Même un noir parfait peut ainsi devenir blanc avec la luminance rouge +100.

Ce résultat impose une protection des achromatiques, avec une transition douce
sur les faibles chromas à étudier pour éviter une frontière visible. Le rendu
actuel ne doit pas être déclaré validé sur les neutres. La sonde diagnostique
termine avec succès si les mesures s’achèvent, mais **deux de ses six contrôles
de conservation échouent** : ce n’est pas un test de non-régression « vert ».

Reproduction :

```sh
cmake --build build-wsl-libraw --target colorMixerNeutralProbe -j 2
build-wsl-libraw/colorMixerNeutralProbe
```

Après correction autorisée, transformer ce cas en vrai test de non-régression
qui exige la conservation des gris, et ajouter des gris légèrement teintés,
des noirs et des valeurs intermédiaires. Aucun correctif moteur n’est appliqué
dans cette demande d’analyse.

### Actions proposées

1. Corriger en priorité la protection des gris après accord, puis revalider
   rouge/jaune/vert et les transitions à faible chroma. La correction de ce
   défaut doit être séparée de la calibration visant Lightroom.
2. Ajouter une scène verte suffisamment représentée et quelques états ±25/±50,
   pour étudier la progressivité et non seulement les extrêmes.
3. Après réception de toutes les bandes, traiter séparément le rendu neutre et
   le mélangeur. Pour ce dernier, étudier une saturation avec protection de Y
   et une luminance sélective dont le gain n’est pas une poussée automatique de
   V vers 1 ; tester le gamut et les transitions avant calibration des amplitudes.
4. Ne pas choisir un coefficient par couleur à partir de cette seule photo.
5. Distinguer le futur test violet de magenta : Lightroom possède huit bandes
   dont Purple, IPR n’a actuellement pas de violet indépendant.

### Vérifications automatiques

- `colorMixerQmlTests` : réussi, sélection des bandes sans contamination.
- `imageExportTests` : réussi, sept bandes × trois commandes, isolation des
  centres et persistance v3/migration.
- Comparateur Python : **3 tests réussis**, conventions, lecture native,
  échantillonnage/recadrage et calculs de couleur.
- Sonde moteur sur gris : six mesures réalisées ; **deux échecs de protection**
  confirmés pour la luminance rouge, quatre conservations pour jaune/vert.

Ces résultats automatisés et les signes mesurés ne remplacent pas la validation
photographique de toutes les bandes. Aucun rebuild applicatif n’est nécessaire
pour cette passe : le code de rendu n’a pas été modifié.

## Campagne Lightroom complète — 22 octobre 2026

**Entrées.** Préréglages autonomes du groupe « IPR Mélangeur »
(`scripts/make_lr_presets.py --controls mixer`) : 24 réglages (8 bandes ×
teinte/saturation/luminance) à −100, −50, −25, +25, +50, +100, sur 9 scènes
choisies pour couvrir chaque bande dans au moins 8 scènes et chaque famille de
boîtiers (6V1A0830, 6V1A9608, 6V1A9890, 7Y6A7003, 7Y6A7232, 7Y6A7298,
IMG_3064, IMG_3089, _MG_7906). 1 296 exports LR, neutres communs de `base/neutre` ;
les 9 doublons identiques de « Bleu Luminance +100 » ont été supprimés avec
l'accord de l'utilisateur. Les composantes d'une bande partagent son dossier :
l'analyseur ignore, pour une composante, les exports à 0 sur son curseur.

**Mesure du mélangeur actuel d'IPR**
([mesure-melangeur-v001](../raw/mesure-melangeur-v001-20261022T151922205633Z.json),
2 808 états, aucune exclusion), médianes sur les scènes, LR / IPR, dans le
noyau de chaque bande défini par les centres HSV d'IPR :

| Bande | Rotation teinte −100 / +100 | Δ saturation −100 / +100 | Rapport Y −100 / +100 |
| --- | --- | --- | --- |
| Rouge | −8,4 / −7,9 ; +10,5 / +9,0 | −0,22 / −0,35 ; +0,09 / +0,14 | 0,85 / 0,82 ; 1,17 / 1,23 |
| Orange | −11,4 / −12,5 ; +14,7 / +14,7 | −0,23 / −0,44 ; +0,10 / +0,19 | 0,78 / 0,78 ; 1,23 / 1,28 |
| Jaune | −28,7 / −20,7 ; +48,2 / +23,8 | −0,43 / −0,53 ; +0,18 / +0,25 | 0,55 / 0,74 ; 1,37 / 1,35 |
| Vert | −53,3 / −16,3 ; +39,2 / +11,5 | −0,56 / −0,48 ; +0,19 / +0,16 | 0,62 / 0,78 ; 1,37 / 1,28 |
| Cyan | −23,1 / −10,4 ; +32,9 / +11,8 | −0,33 / −0,47 ; +0,15 / +0,18 | 0,83 / 0,75 ; 1,19 / 1,32 |
| Bleu | −45,0 / −19,7 ; +47,0 / +23,3 | −0,36 / −0,57 ; +0,17 / +0,23 | 0,61 / 0,74 ; 1,45 / 1,35 |
| Violet | −52,3 / −19,1 ; +51,9 / +18,2 | −0,38 / −0,37 ; +0,22 / +0,24 | 0,68 / 0,78 ; 1,37 / 1,28 |
| Magenta | −6,9 / −10,7 ; +3,0 / +7,8 | −0,04 / −0,22 ; +0,02 / +0,16 | 0,98 / 0,85 ; 1,02 / 1,18 |

Le curseur Teinte d'IPR (±18° HSV) est 2 à 3 fois trop faible hors rouge et
orange ; la saturation d'IPR est trop forte sur la plupart des bandes ; la
luminance est trop faible en jaune, vert, bleu et violet. Le magenta de LR est
centré plus loin (≈315° HSV sur l'interface) que celui d'IPR (300°), d'où un
noyau IPR presque inactif côté LR.

**Réponse pixel par pixel de Lightroom** (teinte Oklab du pixel neutre, pas de
5°). Centres et rotations Teinte −100 / +100 : rouge 12°, −20° / +36° ;
orange 47–57°, −38° / +49° ; jaune 92°, −49° / +41° ; vert 138°, −40° / +36° ;
cyan 178°, −34° / +68° ; bleu 240–270° (bande de 80–90°), −78° / +70° ;
violet 300–320°, −62° / +32° ; magenta 350°, −27° / +20°. Le curseur Teinte
change aussi la chroma (jusqu'à ×0,6 en cyan +100) et le curseur Saturation
fait tourner la teinte (jusqu'à ±8°). La rotation dépend peu de la clarté et
modérément de la saturation (±4°) ; les gains de saturation +100 et de
luminance dépendent fortement de la saturation et de la clarté du pixel ;
dispersion entre scènes faible (±3° sur la rotation).

**Suite proposée.** Tables de réponse LR par bande, composante et valeur
(rotation, gain de chroma, gain de clarté selon teinte fine, saturation et
clarté), sur le modèle de la vibrance, validées sur scènes exclues puis sur
le rendu.

### Correctif : tables de réponse Lightroom — 22 octobre 2026

Le mélangeur HSV triangulaire (±18°) est remplacé par des tables mesurées
([modele-melangeur-lr-v001](../raw/modele-melangeur-lr-v001-20261022T153032225474Z.json),
`scripts/build_mixer_model.py`, `src/core/ColorMixerModel.h`) : pour chaque bande,
composante et valeur, rotation de teinte, gain de chroma et gain de clarté Oklab
selon la teinte Oklab du pixel (5°), sa saturation ProPhoto linéaire et sa clarté
Oklab ; replis qui conservent la teinte, effet principal monotone, fenêtre de
teinte par bande, effets estompés sous une chroma Oklab de 0,02, effets des
curseurs additionnés (hypothèse non vérifiée par des exports combinés).
Validation croisée à deux plis de scènes, erreur médiane de l'effet principal
moyenné sur la bande : 0,04° (p90 0,15°) de rotation, 0,002 de gain de chroma,
0,004 de gain de clarté (log).

**Magenta décalé de −10°** vers l'ancienne bande d'IPR, à la demande de
l'utilisateur (garder le magenta proche de celui d'IPR ou le décaler peu).

**Mesure du rendu**
([mesure-melangeur-v002](../raw/mesure-melangeur-v002-20261022T172841133607Z.json),
2 808 états, aucune exclusion), écart médian par scène |IPR − LR| dans le noyau
des bandes, ancien → nouveau, à −100 / +100 :

| Bande | Rotation de teinte (°) | Δ saturation | Rapport Y |
| --- | --- | --- | --- |
| Rouge | 0,64 → 0,71 ; 1,47 → 0,42 | 0,140 → 0,006 ; 0,047 → 0,013 | 0,062 → 0,026 ; 0,076 → 0,023 |
| Orange | 1,46 → 0,41 ; 1,91 → 0,65 | 0,201 → 0,021 ; 0,080 → 0,006 | 0,030 → 0,032 ; 0,064 → 0,023 |
| Jaune | 7,99 → 0,21 ; 24,28 → 0,65 | 0,097 → 0,017 ; 0,059 → 0,013 | 0,185 → 0,055 ; 0,095 → 0,039 |
| Vert | 36,69 → 2,05 ; 28,07 → 1,12 | 0,048 → 0,024 ; 0,030 → 0,026 | 0,186 → 0,079 ; 0,156 → 0,064 |
| Cyan | 12,70 → 1,27 ; 21,13 → 0,90 | 0,174 → 0,018 ; 0,041 → 0,009 | 0,067 → 0,041 ; 0,124 → 0,039 |
| Bleu | 25,63 → 2,87 ; 23,80 → 1,73 | 0,157 → 0,009 ; 0,066 → 0,008 | 0,177 → 0,069 ; 0,215 → 0,060 |
| Violet | 33,04 → 1,19 ; 33,70 → 0,60 | 0,039 → 0,020 ; 0,025 → 0,017 | 0,124 → 0,072 ; 0,177 → 0,079 |
| Magenta | 3,58 → 19,00 ; 4,79 → 10,21 | 0,188 → 0,181 ; 0,137 → 0,094 | 0,114 → 0,161 ; 0,145 → 0,162 |

Sept bandes passent sous 3° de rotation et sous 0,03 de saturation. Le magenta
s'écarte de LR par construction : le noyau mesuré est celui de l'ancien magenta
d'IPR, où LR agit peu ; la bande décalée y agit davantage. Revue visuelle de
l'utilisateur à faire, notamment sur le magenta.
