# Mélangeur huit bandes — vérification après correction

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

## Conclusion

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

## Corpus audité

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

## Méthode et limites

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

## Réponses des séries RAW exploitables

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

### Luminance et neutres

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

### Violet / magenta et calibration

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

### Clipping et rendu de base

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

## Diagnostic rouge / orange / jaune — non validant

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

## Preuves et reproduction

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

## Actions pour clôturer

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
