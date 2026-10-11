# Analyse évolutive — Couleur

Groupe : température, nuance, vibrance, saturation, niveaux de gris.
Protocole : [Couleur](../protocols/protocole-couleur.md).
Les campagnes ci-dessous vérifient aussi l'outillage ; elles ne remplacent
pas les validations photographiques historiques ni ne certifient la fidélité.

## Correctif du 11 octobre 2026 — retouche WB dans l'espace capteur

**Correctif de production livré depuis `patch_color` dans `dev`**, isolé
des autres travaux locaux, par fusion `--no-ff` et tag `v0.1.17-alpha`.
La [CI 38166258163](https://github.com/KimntexX/IPR/actions/runs/38166258163)
est réussie sur Windows, Ubuntu, macOS Apple Silicon et macOS Intel :
18 CTest et 148 tests Python par plateforme. Le code final est identique
au commit testé `74f4310f590312ac4f5462a00e68ebd869ab308f` ; seuls les
documents et preuves de livraison complètent la fusion. `main` reste inchangée.
L'entrée utilisateur « 6V1A6421 » correspond dans le
corpus à **6V1A9421**. Audit XMP : `nuance/6V1A9421-5.tif` = 5200 K / 0,
les indices 6, 7, 8, 9 = +25, +50, +100, +150, profil Camera Neutral.
Il ne manque donc plus le support manuel 0 de cette scène.

La température/nuance d'un RAW DCP est maintenant appliquée au RGB capteur
**avant** matrices, tables et courbe du profil, puis les autres réglages.
Le moteur conserve un contexte immuable, réutilisé pour les curseurs et
le changement de profil ; aucun nouveau décodage à chaque état. Les matrices
du DCP et le blanc effectivement utilisé par LibRaw déterminent les gains.
La norme vert=1 et la distance de nuance en CIE 1960 remplacent, pour ces RAW,
l'adaptation RGB de sortie. Les TIFF/JPEG ont toujours leur propre adaptation
RGB, sans prétendre disposer d'une caractérisation de capteur.
Voir la [description technique et les équations](../../camera-profile-engine.md#retouche-températurenuance-avant-dcp--11-octobre-2026).

### Vérifications réalisées

- Compilation Windows Release de l'application et de ses tests réussie.
- **17/17 CTest** réussis, dont Lumières, DCP, RAW, export/générateur,
  mémoire et IHM. **47 tests Python ciblés** réussis : comparaison WB (10),
  analyseurs de groupe (22), sélection Windows (15).
- Tests synthétiques : identité au neutre avec température source fractionnaire,
  gains avant la courbe non linéaire, absence d'accumulation des retouches,
  options d'exposition DCP conservées, températures et nuances individuelles,
  erreur explicite si la caractérisation ColorMatrix manque.
- Le cache LRU inclut le tampon capteur et ne double pas la charge d'une
  allocation partagée. Le proxy porte un tampon capteur à sa propre géométrie.
- Sonde fraîche de **six RAW × quinze états = 90 états**, avec Camera Neutral,
  WB boîtier, normalisation et optique désactivées. Le neutre interne source
  est conservé **exactement sur les six scènes**, sans changer son décodage
  ni lui attribuer artificiellement 5200/+5. Cela vérifie l'absence de régression
  de la retouche neutre ; ce n'est pas une identité de fichiers avec les anciens
  TIFF partagés, dont l'optique et la provenance complète restent distinctes.

Mémoire canonique : [sonde RAW/DCP v003](../raw/comparaison-wb-20261011/sonde-wb-capteur-v003-20261011T133656397Z.json).
Elle conserve valeurs, mesures, gains, blanc capteur, options, empreintes RAW,
DCP, code et exécutable. Il s'agit de points natifs espacés de huit, encodés
par le moteur de sortie 16 bits, sans redimensionnement ; aucun nouvel export
TIFF n'a été généré par cette sonde. Au réglage **2000 K**, le pourcentage de
points écrêtés est compris entre **0 et 0,0652 %** : quatre scènes à zéro,
0037 à 0,0289 % et 0043 à 0,0652 %. Ces chiffres ne doivent pas être soustraits
sans précaution aux anciens exports IPR de provenance différente.

### Statut de validation

La compilation et les tests structurels ne certifient pas la parité Lightroom.
La [comparaison fraîche v003](../raw/comparaison-wb-20261011/comparaison-wb-capteur-v003-20261011T134140543650Z.json)
est terminée : **91 états LR mesurés**, aucun exclu/diagnostique ; les deux
exports nuance 0 de 0037 restent visibles, mais cette scène est exclue de
l'agrégat strict à 0 (cinq scènes), sans sélectionner arbitrairement un doublon.
Les autres agrégats portent six scènes.

| État | Axe IPR médian | Axe LR médian | Médiane des écarts appariés IPR−LR |
| --- | ---: | ---: | ---: |
| Température 2000 K, Δlog(R/B) | −2,8123 | −2,9200 | −0,1259 |
| Température 3500 K, Δlog(R/B) | −0,9901 | −1,0978 | +0,0594 |
| Température 9000 K, Δlog(R/B) | +0,9398 | +0,9718 | −0,0536 |
| Nuance −150, Δlog(√RB/G) | −1,2595 | −1,1957 | −0,0629 |
| Nuance +150, Δlog(√RB/G) | +1,2122 | +1,2611 | −0,0240 |

Les douze séries IPR (six scènes × deux commandes) ont des réponses moyennes
monotones sur les états échantillonnés, à tolérance `1e-4`. Cela ne garantit
pas la monotonie de chaque pixel : le masque de points non écrêtés peut varier.
Les réponses de nuance et de température sont maintenant bien plus cohérentes
avec les coordonnées du profil qu'une multiplication après sa courbe.

**Ne pas masquer les divergences par la médiane signée** : à 2000 K, les
écarts appariés vont de −0,7004 à +0,6027 selon la scène, et la médiane des
valeurs absolues est **0,5017**. Les trois plus fortes divergences sont 0327,
0007 et 0037. Les masques de points sont indépendants entre moteurs, les
blancs source et les rendus de base diffèrent ; ce n'est ni une MAE couleur
ni une preuve de précision physique. L'influence des tables/courbes DCP et
du gamut subsiste. Il n'y a pas lieu d'ajouter un poids commercial arbitraire
par profil pour annuler cet agrégat.

Conclusion : intégration et non-régression neutre vérifiées. **Rendu accepté
visuellement par l'utilisateur le 11 octobre 2026** : légèrement plus violet
à 2000 K et plus orangé à 50000 K que Lightroom, écarts jugés acceptables.
Cette validation n'établit ni une parité numérique LR, ni une calibration
physique, ni une couverture de tous les boîtiers et profils. Aucun poids
supplémentaire ou correction plus forte n'est ajouté ; leur éventuelle
évolution attend les retours de tiers et d'autres boîtiers/objectifs.
La condition de CI multiplateforme avant fusion est satisfaite. Les paquets
de livraison sont contrôlés par un workflow distinct ; cette CI native ne
certifie pas à elle seule les archives ni les fonctions non couvertes visuellement.
Pour reproduire le contrôle, ouvrir les six RAW dans le nouvel
`build-windows-msvc/Release/appIPR.exe`, Camera Neutral, normalisation et auto WB
désactivées ; essayer surtout 2000/3500/9000 K et nuances ±150, puis revenir
au blanc source. Contrôler continuité, absence de zones saturées artificielles,
aller-retour au neutre, export et restauration de la recette. Les neutres
partagés restent dans `base/neutre`. Aucun nouveau fichier LR n'est requis
pour ce premier contrôle visuel ; on ne demande pas de refaire tout le corpus.
Le standalone précédemment distribué n'est pas mis à jour par ce seul build.
Aucun RAW, neutre ou TIFF n'a été supprimé.
Les preuves précédentes restent immuables et les résultats ci-dessous
décrivent les versions antérieures, pas le nouveau moteur.

## Reprise du 11 octobre 2026 — neutres partagés et TIFF LR corrigés

### Références et interprétation de la balance des blancs

Les neutres communs **LR et IPR sont dans `tests/image/base/neutre`**.
Lightroom rapporte sur ce corpus **5200 K / nuance +5 en As Shot** :
ce n'est pas une erreur de saisie de l'utilisateur. Ces coordonnées sont
propres à l'interprétation du blanc par LR et au boîtier/profil concerné ;
elles ne justifient pas une constante +5 pour tous les boîtiers dans IPR.

Ne pas confondre trois références : le neutre As Shot partagé, le support
manuel 5200/+5 de la campagne température du 10 octobre, et le support
manuel 5200/0 de la campagne nuance. Un support manuel mesure la réponse
d'un curseur ; il ne remplace pas l'audit du rendu à l'ouverture.
Le générateur simplifié du 11 octobre utilise désormais nuance 0 comme
support de température : ses futurs rendus ne sont pas interchangeables
sans audit avec les exports à +5 du 10 octobre.

L'audit retrouve la référence LR de 0037 sous **`6V1A0037.tif`**,
grâce à son `RawFileName` embarqué, sans déduire le RAW du numéro d'export.
Les deux candidats IPR de chaque scène sont conservés dans l'inventaire,
avec leur hash : le nom `-IPR-CameraNeutral-AsShot` guide la sélection,
mais ne prouve pas à lui seul les réglages ou l'exécutable de génération.

**Confirmation de l'utilisateur du 11 octobre** : les fichiers à retenir
comme références du rendu souhaité sont **`-IPR-CameraNeutral-AsShot.tiff`**,
et la **normalisation RAW n'était pas cochée** lors de leur génération.
Il s'agit d'une confirmation opérateur, pas d'une métadonnée extraite du TIFF.
Cette déclaration est conservée dans une
[mémoire JSON de contexte opérateur](../raw/comparaison-wb-20261011/contexte-operateur-wb-v002-20261011T093003Z.json),
liée par empreinte à l'audit des fichiers, sans modifier celui-ci.
Ne pas substituer les fichiers génériques `-IPR.tiff`, ni expliquer le rendu
retenu par une normalisation activée. Les réglages de génération du jeu
générique restent non établis.

Le neutre 9421 a été remplacé pendant cette reprise. Le fichier principal
`6V1A9421-LR-CameraNeutral-AsShot.tif` indique maintenant optique désactivée,
5200/+5 et **WhiteBalance=Custom** ; la copie `_optique` indique As Shot
avec correction d'objectif et aberration chromatique activées. La copie
optique n'est pas choisie comme référence commune. `Custom` à ces mêmes
coordonnées n'est pas une erreur de valeur, mais ne doit pas être présenté
comme un export As Shot confirmé par son XMP.

Source de vérité de cet audit :
[neutres et métadonnées WB, mémoire v002](../raw/comparaison-wb-20261011/audit-neutres-wb-v002-20261011T091439198763Z.json).
Les fichiers ont été relus et leurs empreintes revérifiées avant publication.
Le premier essai a été interrompu par la disparition d'un nom de fichier
pendant son remplacement : aucune mémoire partielle n'a été publiée.

Constat important : les deux neutres IPR ont des empreintes différentes sur
cinq scènes sur six ; **0037 est la seule paire strictement identique**.
Moyennes Y linéaires relatives à LR, sur pixels natifs échantillonnés à pas 8
sans recalage inter-moteurs :

| Scène | `IPR-CameraNeutral-AsShot` / LR | `IPR.tiff` / LR |
| --- | ---: | ---: |
| 0007 | 1,00363 | 0,77008 |
| 0037 | 0,87732 | 0,87732 |
| 0043 | 0,99393 | 0,61586 |
| 0327 | 1,04723 | 0,67858 |
| 9421 | 1,04728 | 0,71967 |
| 9950 | 1,37872 | 0,98532 |

Ce tableau est un **diagnostic photométrique**, pas une MAE, une mesure de
justesse des couleurs ni une preuve de réglages identiques. Les réglages
et l'exécutable des neutres IPR ne sont pas attestés par leurs noms ; la
confirmation opérateur ci-dessus établit le jeu et l'absence de normalisation,
mais pas l'ensemble des réglages ni la version de l'exécutable. Ne pas
mélanger les candidats pour ajuster la température ou la nuance ; vérifier
le profil, la normalisation RAW et l'état restauré à leur génération.
Pour 0007, le candidat nommé CameraNeutral/AsShot est proche du neutre LR
en Y, alors que le générique est sensiblement plus sombre : les candidats
ne sont donc pas interchangeables. Cela ne démontre pas la cause de l'écart.

### Corrections reçues et couverture restante

- 0007 : le TIFF nuance +150 est désormais présent et identifié à +150.
- 9421 : les variantes température/nuance indiquent désormais les deux
  corrections optiques désactivées. Le point **nuance 0 manque encore** :
  conserver les huit valeurs dans l'inventaire, sans inventer le neuvième
  ni utiliser le neutre +5 comme support zéro.
- 0037 : deux fichiers sont encore identifiés à nuance 0 (`-4` et `-6`).
  Ils restent traçables ; cette valeur dupliquée ne participe pas à une
  médiane stricte et ne permet pas de certifier la progression complète.

### Correctifs de l'analyseur et état de la production

La recette WB sait auditer les vrais neutres du dossier commun, conserve
les métadonnées As Shot et distingue la copie optique de la référence
principale. Ses médianes strictes excluent désormais les paires diagnostiques
et les associations dupliquées dans l'un ou l'autre moteur.

La réévaluation des anciens pixels IPR est explicite : leurs mémoires JSON
immuables servent à retrouver les réglages et empreintes, revérifiées sur
les fichiers présents. Ce n'est **pas une nouvelle génération IPR**.
L'ancien argument de manifeste `--generation` de cette recette a été retiré,
sans supprimer les preuves historiques ni les images.

**Aucune correction température/nuance du moteur de production n'est
encore livrée dans cette reprise.** Le déplacement de la WB avant le DCP,
son ancrage au blanc caméra et la suppression éventuelle de la compensation
froide empirique demandent une ablation contrôlée, sans altérer le neutre
As Shot approuvé. La réception de TIFF LR corrigés ne constitue pas,
à elle seule, une preuve permettant de choisir de nouveaux coefficients.

La sonde d'ablation projette la formule actuelle, sa version sans diagonale
froide et un Bradford complet sur les **mêmes neutres sélectionnés**.
Ces projections restent diagnostiques : une image déjà rendue et encodée
ne permet pas de reconstituer le RGB caméra avant DCP, sa dynamique perdue
ou ses tables non linéaires. Ne pas intégrer le modèle donnant seulement
la meilleure moyenne comme correctif de production.

Contrôle mathématique synthétique à 2000 K et référence de décodage
4924,211 K, avant compression de gamut : un gris RGB linéaire `(0,18; 0,18;
0,18)` devient environ `(0,00724; 0,19369; 1,07302)` avec la formule actuelle,
`(0,02069; 0,16277; 1,27740)` sans diagonale froide, et
`(−0,05674; 0,22963; 1,27312)` avec le Bradford complet. Ce ne sont pas des
mesures des photographies : ces valeurs illustrent qu'une suppression
aveugle peut augmenter le bleu hors gamut, et qu'une adaptation complète
appliquée après le profil n'est pas automatiquement une WB caméra correcte.
Le calcul conserve des valeurs hors `[0,1]` jusqu'au gamut de sortie ; ne
pas les écrêter dans la matrice pour dissimuler le problème.

### Réévaluation des séries de curseurs avec les TIFF LR corrigés

Source de vérité :
[comparaison native v002, 172 états](../raw/comparaison-wb-20261011/comparaison-temperature-nuance-v002-20261011T093638765086Z.json).
90 états IPR historiques réutilisés et 82 états LR mesurés, 172 mesures,
aucun diagnostic ou fichier exclu parmi les états associés. Les huit
variantes LR nuance de 9421 restent **dans l'inventaire sans réponse mesurée**,
car leur support manuel 0 manque ; ne pas interpréter `excluded=0` comme
une couverture complète du protocole. Le doublon 0037 à zéro reste visible.

Les empreintes des quatre sources de production contrôlées sont identiques
à celles conservées dans les mémoires originales IPR. La différence des
résumés ci-dessous n'est donc **pas une amélioration du code** : les TIFF
LR ont été corrigés et les effectifs/règles d'agrégation ont changé.
Les supports de réponse sont toujours ceux de la campagne originale
(température : 5200/+5 ; nuance : 5200/0), distincts des neutres partagés.

| Contrôle | États strictement appariés | Axe médian IPR | Axe médian LR | Médiane des écarts IPR−LR |
| --- | ---: | ---: | ---: | ---: |
| Température 2000 K | 6 scènes | −4,7030 | −2,9200 | −1,6836 |
| Température 3500 K | 6 scènes | −1,3305 | −1,0978 | −0,2675 |
| Température 6500 K | 6 scènes | +0,4436 | +0,4523 | −0,0356 |
| Température 9000 K | 6 scènes | +0,9258 | +0,9718 | −0,0986 |
| Nuance −150 | 5 scènes | −1,0397 | −1,1430 | +0,1033 |
| Nuance +150 | 5 scènes | +1,0397 | +1,2151 | −0,1754 |

Température : axe `Δlog(R/B)` ; nuance : axe `Δlog(√(RB)/G)`.
À nuance 0, quatre paires strictes seulement (doublon 0037 exclu de
l'agrégat, absence de support 9421). À 2000 K, le clipping médian des
exports réels de cette série vaut environ **5,339 % IPR / 0,049 % LR** ;
0007 demeure à **69,268 % / 0,003 %**. Ce sont les anciens exports IPR
contre les nouveaux LR, pas des exports générés après correctif.

Le décalage froid extrême et la différence de modèle de nuance sont
confirmés malgré la correction des références LR. La conformité n'est
pas validée. Il n'y a pas de preuve de la nécessité d'un poids empirique
spécifique au nom du profil : il faut investiguer la caractérisation du
blanc caméra et l'ordre WB/DCP.

### Ablation sur les neutres retenus — diagnostic, pas rendu de production

Source de vérité :
[18 projections température et variantes LR](../raw/comparaison-wb-20261011/investigation-adaptation-wb-v002-20261011T093106401927Z.json).
Six scènes, trois températures (2000, 3500, 9000 K), trois hypothèses par
état, mêmes pixels d'entrée IPR. L'axe LR est calculé contre son neutre
commun, à nuance +5 ; aucune variation de nuance supplémentaire n'est
appliquée aux neutres IPR As Shot. Les masques restent indépendants entre
les moteurs et états. La CCT de référence provient du décodage RAW
antérieurement mesuré, et non d'une mesure du TIFF sélectionné.

À **2000 K**, médianes des six scènes :

| Projection diagnostique | Écart absolu d'axe `Δlog(R/B)` à LR | Pixels écrêtés |
| --- | ---: | ---: |
| Formule actuelle projetée | 1,6164 | 15,842 % |
| Sans diagonale froide | 0,8255 | 19,817 % |
| Bradford complet sans diagonale | 0,7884 | 19,738 % |

Le clipping LR médian de ces états est environ **0,049 %**. Ce tableau
ne donne pas le clipping d'un nouvel export IPR réel : les trois lignes
sont des projections post-profil sur des TIFF déjà encodés. Les images
hors gamut et valeurs négatives perdues à l'export ne sont pas retrouvées.

**Verdict de l'ablation** : enlever la diagonale froide rapproche l'axe
rouge/bleu mais augmente le clipping dans **les six scènes**. Le Bradford
complet augmente également le clipping partout et dégrade l'écart d'axe
sur certaines scènes, notamment 9950. Aucun de ces remplacements simples
ne satisfait le double objectif chromatique et qualité des hautes lumières.
L'inverse d'une adaptation physique ne suffit pas à caractériser une
balance caméra dans une image déjà transformée en sRGB par un profil.

La prochaine correction doit agir sur les gains du RGB caméra **avant**
les tables et la courbe non linéaire DCP, avec un blanc caméra dérivé de
la caractérisation du profil et des multiplicateurs As Shot. Le profil
intervient par ses matrices, pas par un poids arbitraire choisi selon son
nom. La source `RawDecoder::DcpSource::cameraRgb` déjà retenue pour changer
rapidement de profil est un point d'intégration à examiner, afin d'éviter
un nouveau décodage LibRaw à chaque mouvement de curseur.
Conserver l'identité exacte du rendu au zéro As Shot, contrôler mémoire,
proxy/pleine résolution, cache, export et restauration du sidecar avant
de considérer ce chemin comme intégré ou validé.

Cette orientation est cohérente avec la caractérisation caméra et les
conversions de blanc du
[DNG SDK, `dng_color_spec.cpp`](https://android.googlesource.com/platform/external/dng_sdk/+/master/source/dng_color_spec.cpp).
Elle ne prouve pas le détail du moteur Lightroom ni l'identité des couleurs
entre profils créatifs. **L'intégration pré-DCP reste à réaliser** ; aucune
correction empirique de substitution n'est livrée dans cette passe.

Tests ciblés de cette reprise : **10 tests de comparaison WB, 22 tests
d'analyse de groupe, 3 tests mathématiques d'ablation et 15 tests de sélection
des tests réussis**. Aucune suppression d'image, aucun commit,
fusion ou publication ; les campagnes ci-dessous restent historiques.

## Campagne native du 10 octobre 2026 — intégration des analyseurs

Source de vérité : [40 états natifs](../raw/modernisation-analyseurs/mesure-couleur-v001-20261010T190505433356Z.json),
mesurés le 10 octobre à 19:05:05 UTC. Ce JSON, avec hashes du code, des TIFF,
réglages/provenance, unités, effectifs, audits et progressions, est immuable.
[Vérification des outils](../raw/modernisation-analyseurs/verification-outils-v001-20261010T190542Z.json).

### Entrées et conditions

Manifeste [génération Couleur](../raw/generation-photographique-20261010T120912Z/couleur.json),
RAW 6V1A0043 et 6V1A0327. Camera Neutral requis et appliqué selon le manifeste,
normalisation d'environnement désactivée ; neutres propres à chaque source.
TIFF sRGB 16 bits, dimensions natives, états isolés ; IPR uniquement, sans
nouvelles variantes Lightroom. Les ressources DCP/LCP sont identifiées par
nom mais leurs hashes ne sont pas fournis par ce manifeste : limite explicite.

Lecture memmap native sans conversion 8 bits ni interpolation. Global à pas 8
(pixels réels, pas exhaustif), sRGB linéarisé et Y Rec.709. Les variantes et
neutres correspondent à leurs empreintes déclarées ; hashes des sources de
l'analyseur relus et conformes aux fichiers au terme de cette passe.

### Couverture et observations

| Contrôle | États mesurés, deux scènes | Observation |
| --- | ---: | --- |
| Température | 6 | Deux progressions observées croissantes de l'axe log(R/B) |
| Nuance | 10 | Deux progressions observées croissantes de l'axe vert/magenta |
| Vibrance | 10 | Deux progressions observées croissantes de chroma faible |
| Saturation | 10 | Deux progressions observées croissantes de chroma faible |
| Niveaux de gris | 4 | Deux états activés : écart maximal linéaire entre canaux = 0 sur les pixels mesurés |

40 états `measured`, 0 `diagnostic`, 0 `excluded`. Huit progressions
observées monotones, **sans ajouter d'ancre zéro fictive**. Ce constat ne
garantit ni monotonie pixel par pixel, ni progressivité complète sur toutes
les valeurs, ni protection des couleurs saturées validée visuellement.
Les valeurs détaillées par scène, contrôles et masques sont dans le JSON.

### Décision

Intégration de l'outil Couleur réussie : lecture 16 bits, associations,
audits, provenance et publication JSON fonctionnent sur ce corpus réel.
Fonctionnement photographique complet, qualité visuelle, fidélité physique
et parité Lightroom restent **non évalués dans cette campagne**. Aucune
nouvelle validation des curseurs ni modification du moteur de production.
Aucune image supprimée : `cleanup_eligible=false`.

### Étapes suivantes

Auditer les références LR Camera Neutral/As Shot avec mappings explicites,
étendre gris/carnations et couleurs hors gamut ; contrôler faibles/fortes
chromas, clipping et rendu visuel. Définir les tolérances avant la campagne.
Recette IHM/reset/restauration et éventuelle charte restent nécessaires.
Maintenir les prochaines conclusions ici, les JSON successifs séparément.

## Campagne température/nuance Camera Neutral — 10 octobre 2026

### Conditions et audit des entrées

Six RAW : 6V1A0007, 6V1A0037, 6V1A0043, 6V1A0327, 6V1A9421,
6V1A9950. Les fichiers LR sont dans `tests/image/base/couleur/temperature`
et `nuance`, pas dans les anciens dossiers homonymes à la racine de `base`.
Les valeurs sont lues dans le XMP, jamais déduites du suffixe.

- Température LR : 2000, 3500, 5200, 6500, 9000, 50000 K, nuance constante +5.
- Nuance LR : −150, −100, −50, −25, 0, +25, +50, +100, +150,
  température constante 5200 K. Le fichier `nuance/6V1A0007-9.tif`
  contient encore +100 : +150 manque pour cette scène. 0037 a deux états 0.
- Profil LR : Camera Neutral, WB Custom, réglages Lumières/couleur/détail
  audités à zéro. Ces supports manuels ne sont **pas** des neutres As Shot.
- Exception optique : les quinze fichiers LR de 6V1A9421 ont
  `AutoLateralCA=1` et `LensProfileEnable=1`, contrairement aux cinq autres
  scènes. Conserver cette scène comme diagnostic, pas comme paire stricte
  du seul contrôle température/nuance.

La génération IPR utilise le moteur de production inchangé, Camera Neutral
requis, sidecars ignorés, normalisation d'environnement désactivée. Elle
reproduit les valeurs affichées et conserve un support interne par scène :
5200/+5 pour température et 5200/0 pour nuance. 102 TIFF natifs 16 bits sRGB
sont prévus dans `tests/image/base/couleur/comparaison-wb-20261010-v1`.
Les options de support ont été ajoutées uniquement au générateur et testées.
Un nombre de nuance identique n'est pas un blanc physique identique : IPR
définit son zéro par la WB caméra décodée, LR possède sa propre conversion.

### Investigation du pipeline et hypothèse des profils

Constats dans le code actuel :

1. `RawDecoder` estime la température source à partir de `cam_mul` et
   `cam_xyz` LibRaw, pas des matrices du DCP sélectionné. Elle vaut environ
   4924,21 K sur les scènes déjà générées de ce corpus.
2. `CameraProfileEngine::apply` interpole déjà les matrices et les éventuelles
   HueSatMaps selon l'illuminant. Mais la température passée est celle du
   **décodage source**, non celle du curseur de retouche.
3. Le DCP est rendu avant `renderWorkingImage`. Le curseur température agit
   ensuite sur le RGB linéaire sRGB, après la LookTable et la courbe DCP.
   Changer le curseur ne recalcule donc pas cette interpolation DCP.
4. La transformation Bradford utilisée laisse le gain du cône M à 1 ; ce
   n'est pas une adaptation Bradford complète. La branche froide ajoute
   `diag(1−0,65s, 1+0,19s, 1−0,16s)`, avec
   `s=clamp(log(Tref/T)/log(Tref/2000),0,1)`. À 2000 K, cette diagonale
   vaut (0,35 ; 1,19 ; 0,84). C'est une calibration de l'ancien corpus,
   pas une propriété physique des profils.
5. La nuance est une diagonale RGB normalisée sur la luminance d'un gris :
   elle n'est pas une conversion température/nuance vers un blanc caméra.

Sa signature théorique, avant écrêtage et à température fixe, est
`Δlog(sqrt(RB)/G) = tint×ln(2)/100`. La normalisation de luminance disparaît
dans ce rapport. Les bornes ±150 correspondent donc à ±1,03972 sur cet axe,
quelle que soit la scène non écrêtée. Cette loi universelle ne consulte
aucune matrice de profil ; vérifier son accord avec LR est distinct de
vérifier que la direction vert/magenta fonctionne.

Le DCP local Camera Neutral R6 Mark III possède deux ColorMatrix, deux
ForwardMatrix, illuminants A et D65, LookTable 90×16×16, courbe de 128 points,
offset d'exposition −0,5 EV ; pas de HueSatMaps dans ce fichier. Son inventaire
et son hash devront être conservés dans la campagne. Le nom du manifeste
n'établit pas, à lui seul, une identité cryptographique avec le profil LR.

L'[audit des trois DCP locaux](../raw/comparaison-wb-20261010/audit-profils-wb-v001-20261010T193929806024Z.json)
confirme que Camera Neutral et Camera Standard ont des ColorMatrix et
ForwardMatrix identiques. Adobe Standard partage leurs ColorMatrix mais
diffère sur les ForwardMatrix (écart absolu maximal de coefficient :
0,2983 et 0,2933). Ces écarts de coefficients ne sont pas des ΔE ni des
erreurs perceptuelles. Les tables/courbes diffèrent également : changer de
profil ne se résume pas à changer un gain bleu.

Dans le SDK Adobe, le blanc xy détermine les matrices interpolées en 1/T,
les coordonnées du blanc caméra puis les gains et la transformation vers D50.
Cela soutient l'idée d'une **adaptation dépendante de la caractérisation DCP**,
plutôt qu'un poids arbitraire par profil. Ce code public ne prouve pas les
choix exacts du moteur Lightroom actuel. Source :
[Adobe DNG SDK, dng_color_spec.cpp (miroir AOSP)](https://android.googlesource.com/platform/external/dng_sdk/+/master/source/dng_color_spec.cpp).
Cela ne démontre notamment pas que LR remplace sa courbe tonale à 2000 K :
un changement de blanc caméra en amont d'une courbe non linéaire suffit à
modifier la réponse visible, même si les points de cette courbe restent fixes.

### Direction recommandée, à confirmer par les mesures

Replacer la WB manuelle dans l'espace caméra, avant LookTable/courbe et
limitation de gamut ; convertir température/nuance en blanc xy et dériver
les gains avec la caractérisation du profil. Réévaluer les matrices au blanc
cible et préserver un intermédiaire flottant pour éviter un nouveau décodage
RAW. Vérifier ensuite l'identité As Shot, les gris/carnations, les deux profils
et les extrêmes avant de retirer la calibration empirique historique.

L'objectif est une balance des blancs cohérente et prévisible, **pas le même
bleu forcé dans tous les profils** : leur rendu créatif peut légitimement
changer teinte et saturation. Ce corpus contient Camera Neutral seulement ;
il ne démontre pas une différence causée par un changement de profil.
Une série Adobe Standard strictement identique sur une ou deux scènes
permettra cette comparaison ultérieure, pas une calibration automatique à
partir du nom du profil.

Schéma mathématique de la piste (pas un correctif implémenté) : blanc cible
`Wxy(T,t)`, neutral caméra `n=CMp(T)×Wxy`, gains diagonaux proportionnels à
`1/n`, normalisation d'exposition, puis transformation DCP vers XYZ D50
et rendu du profil. Les calibrations individuelles/AnalogBalance doivent
être prises en compte lorsqu'elles existent. L'interpolation utilise les
bornes d'illuminants du profil ; elle ne doit pas extrapoler aveuglément
jusqu'à 2000 ou 50000 K. Température, nuance et blanc As Shot doivent être
convertis de façon cohérente, sans appliquer deux fois les gains source.

Si `Fp` désigne le rendu non linéaire du profil et `AT` la correction de WB,
en général `AT(Fp(x)) ≠ Fp(AT(x))`. Une simple diagonale placée après la
LookTable/courbe ne corrige donc pas forcément cet ordre des opérations.
Cette architecture pourra utiliser un buffer caméra conservé et un rerendu
du profil ; elle n'impose pas de réouvrir/décoder le RAW à chaque geste.
Elle devra cependant conserver l'identité du neutre actuel approuvé et être
validée sur les autres réglages : changer l'ordre WB/profil peut modifier
les supports des masques tonals, les couleurs saturées et le clipping.
Ne pas livrer ce déplacement comme une simple retouche de coefficients.

### Résultats température — comparaison native

[Mémoire JSON température](../raw/comparaison-wb-20261010/comparaison-temperature-nuance-v001-20261010T193900537890Z.json) :
72 états, 66 mesurés, 6 diagnostics LR pour l'optique 9421, aucun exclu.
Les valeurs ci-dessous sont les médianes des **cinq scènes comparables**,
excluant 9421 ; les agrégats généraux du JSON portent les six scènes.
Les données par scène, dénominateurs, hashes et réglages font autorité.

| Température | Axe Δlog(R/B) IPR | Axe LR | Médiane des écarts IPR−LR | Clipping IPR | Clipping LR |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 2000 K | −4,793 | −2,829 | −1,918 | 5,136 % | 0,072 % |
| 3500 K | −1,280 | −0,984 | −0,296 | 0,153 % | 0 % |
| 5200 K | 0 | 0 | 0 | 0 % | 0 % |
| 6500 K | +0,412 | +0,448 | −0,035 | 0 % | 0 % |
| 9000 K | +0,863 | +0,962 | −0,098 | 0,0002 % | 0 % |
| 50000 K | +1,786 | +1,877 | −0,095 | 0,0004 % | 0,0028 % |

Chaque axe est une variation par rapport au support 5200 K propre au moteur.
Le zéro à 5200 est donc une identité interne, **pas une preuve d'égalité des
rendus IPR/LR**. Médiane des différences ≠ différence des médianes.
Les axes emploient des masques non nuls/non saturés indépendants entre
moteurs/états : à 2000 K la population IPR est fortement réduite. Il ne faut
pas interpréter leur écart comme un gain exact applicable à chaque pixel.
Le clipping est calculé séparément sur tous les pixels natifs échantillonnés
au pas 8 : au moins un canal encodé ≥ 65534/65535. Ce n'est pas un taux de
pixels blancs, ni un taux exhaustif pleine image.

Cas marqués à 2000 K : 0007 atteint 69,268 % de clipping IPR contre
0,003 % LR ; 9950 atteint 32,447 % contre 0,653 % ; 0327 atteint
5,136 % contre 0 %. 0007 conserve seulement 22 093 pixels pour l'axe WB
IPR, contre 504 427 LR. Cela documente une forte réduction du support
colorimétrique utilisable, pas uniquement un bleu esthétiquement différent.
Les valeurs détaillées sont conservées dans le JSON.

Les six progressions IPR observées sont monotones sur cet axe moyen.
Le fonctionnement directionnel est donc présent, mais la fidélité au rendu
LR du froid extrême est insuffisante sur ce corpus. Une monotonie moyenne
n'efface pas le clipping. Les états chauds sont plus proches sur l'axe étudié,
sans constituer une validation visuelle ou une mesure de fidélité physique.

Le soupçon de départ est pertinent comme **piste d'architecture**, mais ces
mesures ne répartissent pas causalement l'erreur entre la diagonale froide,
le modèle de nuance, l'ancre As Shot et l'ordre WB/DCP. Un test d'ablation
contrôlé reste nécessaire avant de choisir des coefficients ou de déplacer
la WB en production. Aucun correctif du moteur de retouche n'est appliqué.

Attention : la compensation froide réduit déjà le bleu (×0,84 à 2000 K).
La supprimer globalement peut augmenter l'écrêtage bleu, même si elle
réduit la sur-atténuation du rouge. La prochaine ablation devra distinguer
compensation diagonale, modèle d'adaptation et ordre des opérations ; ne
pas remplacer un réglage empirique par un autre à partir de la seule moyenne.

### Résultats nuance — comparaison native

[Mémoire JSON nuance](../raw/comparaison-wb-20261010/comparaison-temperature-nuance-v001-20261010T194534501239Z.json) :
109 états (54 IPR et 55 LR), 100 mesurés, 9 diagnostics optiques 9421,
aucun exclu. Les duplications LR +100 de 0007 et zéro de 0037 sont
conservées, sans inventer le +150 de 0007. La progression LR automatique
est indéterminée pour les séries à doublons et pour 9421 diagnostique ;
ce n'est pas une non-monotonie démontrée. Les six progressions IPR sont
monotones sur les moyennes observées.

Médianes sur les paires `measured` des deux moteurs, hors 9421 :

| Nuance | Scènes | Axe Δlog(√(RB)/G) IPR | Axe LR | Médiane des écarts IPR−LR |
| ---: | ---: | ---: | ---: | ---: |
| −150 | 5 | −1,040 | −1,143 | +0,103 |
| −100 | 5 | −0,693 | −0,801 | +0,108 |
| −50 | 5 | −0,347 | −0,386 | +0,040 |
| −25 | 5 | −0,173 | −0,192 | +0,019 |
| 0 | 5 | 0 | 0 | 0 |
| +25 | 5 | +0,173 | +0,190 | −0,017 |
| +50 | 5 | +0,347 | +0,383 | −0,037 |
| +100 | 5 | +0,693 | +0,788 | −0,095 |
| +150 | 4 | +1,040 | +1,261 | −0,221 |

La loi IPR théorique est retrouvée à la quantification près ; LR présente
une réponse plus ample et moins symétrique. À +150, sur quatre scènes,
le clipping médian est 1,015 % IPR contre 0,096 % LR, et le rapport de
luminance moyen à son support vaut environ 0,998 contre 1,215. Ce ne sont
pas des exigences d'égalité de luminance : les normalisations et le rendu
des deux moteurs diffèrent.

Le couplage chromatique confirme une différence de modèle : à +150,
`Δlog(R/B)` IPR reste proche de zéro (quelques millionièmes), tandis que
LR vaut −0,209 pour 0037, −0,107 pour 0043, −0,272 pour 0327 et −0,039
pour 9950. Un simple coefficient multipliant la diagonale de nuance IPR
ne reproduira pas ce couplage rouge/bleu. Les masques sont indépendants,
les différences de profil/blanc source restent des réserves d'attribution.

### Conclusion et suite

L'observation visuelle à 2000 K est confirmée : le problème n'est pas
réductible à un choix esthétique de profil, car le froid extrême introduit
un écrêtage important dans les exports IPR. La direction des curseurs
fonctionne ; leur équivalence LR et leur qualité aux extrêmes ne sont pas
validées. La nuance présente une divergence moins spectaculaire mais réelle,
à la fois en amplitude, luminance et couplage rouge/bleu.

Priorité recommandée : ablation contrôlée de la branche froide, puis modèle
température/nuance → blanc caméra cohérent avec les matrices DCP, avant le
rendu non linéaire du profil et le gamut de sortie. Vérifier le neutre As Shot
approuvé et les Lumières figées avant intégration. Pas de poids arbitraires
par nom de profil, ni de garantie de bleu identique à travers les rendus créatifs.

Les images actuelles suffisent pour commencer ces investigations. Pour
compléter la conformité, réexporter seulement `nuance/6V1A0007-9.tif` à
+150 et les séries 9421 avec les deux corrections optiques désactivées,
ou appliquer/auditer des corrections strictement identiques dans les moteurs.
Une comparaison causale entre profils demandera ensuite, par exemple sur
0007 et 0037, des états 2000/5200/9000 sous Adobe Standard et Camera Neutral,
avec nuance +5 constante et tous les autres contrôles identiques.
Il ne faut pas encore préparer de nouveaux RAW pour cette étape.

102 TIFF IPR générés, 181 états analysés au total, mémoires JSON publiées
sans écrasement. Tests ciblés : 86 tests Python et les deux tests CTest
générateur/export réussis. La vérification documentaire globale relève
quatre liens historiques vers des images supprimées dans le rapport du
29 septembre ; aucun ne concerne cette recette.

Aucun correctif de production n'est livré : seuls les outils de génération
et d'analyse ont changé, sur `patch_couleur` créée depuis `dev`, sans commit,
fusion ou publication automatique. Les TIFF restent conservés à cette passe :
conformité incomplète (+150 manquant, optique non homogène), attribution causale
non isolée et `cleanup_eligible=false`. RAW et neutres communs sont intacts.

## Historique et compatibilité

Un premier [smoke-test de cinq saturations 0327](../raw/modernisation-analyseurs/mesure-couleur-v001-20261010T185910569266Z.json)
a précédé le renforcement de l'audit des neutres. Il reste une preuve
d'intégration datée, pas un résultat à agréger avec les 40 états suivants.
Les deux identifiants portent `v001` car ce sont deux campagnes/scopes
distincts, chacune horodatée ; leurs hashes de code sont conservés.

Les anciennes analyses [température](analyse-temperature-2026-10-02.md),
[nuance](analyse-nuance-2026-10-02.md), [vibrance](analyse-vibrance-2026-10-02.md)
et [saturation](analyse-saturation-2026-10-02.md) restent historiques.
Leurs diagnostics anciens ne sont pas numériquement comparables à cette
méthode native lorsque lecture, filtre, profil ou neutre diffèrent.
