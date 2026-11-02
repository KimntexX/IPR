# Restauration neuronale — qualification du corpus

## 1er novembre 2026 — scène pilote 6V1A0133

**Conclusion : composition retenue pour poursuivre les acquisitions ; pas
encore une référence propre ni une scène d'entraînement qualifiée.**

Source : `D:/DCIM/101EOSR6/6V1A0133.CR3`, R6 Mark III, ISO 100,
1/15 s, f/5,6, 32 mm. Source laissée intacte. Lecture des métadonnées
CMT1/CMT2, mosaïque décompressée par rawpy/LibRaw 0.22.1 et inspection du
JPEG intégré, dont huit recadrages à taille native. Aucun entraînement,
développement photographique IPR ou changement en production.

### Intérêt des matières

| Élément fourni par l'utilisateur | Utilisation envisagée | Limite |
| --- | --- | --- |
| Feuille blanche | Surface peu texturée claire, gradients et bruit chromatique | Pas une mire de gris/couleur étalonnée ; éclairage non uniforme possible |
| Porte de réfrigérateur blanche et lisse, à droite (précision utilisateur) | Zone claire peu texturée supplémentaire pour observer le bruit et les dominantes | Sélectionner une zone intérieure sans bord ni reflet marqué ; un gradient d'éclairage n'est pas du bruit |
| Tôle d'aluminium | Surface relativement lisse, reflets et détails faibles | Reflets, rayures et salissures visibles : pas un aplat uniforme de calibration |
| Tapis de yoga | Stries régulières, teinte sombre, préservation des motifs | Son relief est du signal, pas du bruit |
| Peluche | Fibres fines blanches et noires, transitions contrastées | Les fibres noires peu visibles exigent une référence indépendante |
| Sac rouge | Texture textile, coutures, plis et couleur soutenue | Ne pas confondre trame et bruit chromatique |
| Pull orange | Maille, coutures et gradients sur les plis | Test de conservation de texture, pas vérité colorimétrique |
| Carton | Fibres et reliefs faibles beiges | Surface non uniforme, zone périphérique moins précise |
| Lino | Motifs imprimés et bords à faible contraste | L'utilisateur confirme que les veines sont imprimées ; ne pas les effacer comme du bruit |

### Netteté et exposition : conclusion prudente

La vue d'ensemble permet de distinguer les objets et leurs structures. Dans
les recadrages JPEG à taille native, les stries du tapis et les coutures sont
visibles ; des détails textiles, du carton et du sol sont plus doux. Cela ne
prouve ni un flou de bougé ni une netteté RAW optimale : le JPEG a subi les
traitements du boîtier. Ne pas qualifier cette prise seule comme référence
parfaitement nette. Une prise sur trépied avec mise au point vérifiée reste
nécessaire, en particulier à 1/15 s et sur cette scène en profondeur.

Sur les quatre plans CFA, 0,00036 % à 0,00453 % des échantillons atteignent
le blanc déclaré par LibRaw (16383). Aucun écrêtage massif à ce seuil n'est
mis en évidence. Ce seuil ne remplace pas le blanc effectif caractérisé du
boîtier, et les rares valeurs maximales ne sont pas identifiées ici comme
reflets, pixels isolés ou autre cause. Les noirs déclarés par cette version
de LibRaw (0/44/130/94) ne sont **pas validés** : aucune estimation du bruit
ni marge d'exposition en EV n'est déduite de ces noirs.

### Prochaine acquisition proposée

Conserver cette composition. Immobiliser le boîtier et tous les objets,
vérifier la mise au point, garder éclairage, cadrage et ouverture constants.
Une ouverture f/8 peut être essayée si les différents plans ne sont pas
assez nets ; refaire alors aussi la référence dans les mêmes conditions.

Prévoir une rafale de référence à bas ISO, puis des rafales à ISO plus élevés
en adaptant le temps de pose pour une luminosité comparable, sans saturer les
surfaces claires. Le nombre et les ISO de la campagne seront figés avant
entraînement. Une moyenne alignée de rafale peut réduire le bruit aléatoire,
pas garantir une vérité sans bruit fixe. Avec ce seul fichier, impossible
de mesurer le bruit indépendant, la stabilité temporelle ou le gain U-Net.

Conserver cette scène entière dans une seule partition du corpus. Les
recadrages et ISO de la même composition ne doivent pas être répartis entre
entraînement et test : cela créerait une fuite de contenu.

Les métriques et les coordonnées des recadrages sont conservées dans le
JSON canonique `neural-pilot-0133-v001-*` sous `../raw/`. Les planches JPEG
de diagnostic restent dans le build dédié, sans nouvelle copie du RAW.

## 1er novembre 2026 — réception des séries intérieure et végétale

**Premier audit terminé : compositions pertinentes et séries candidates,
pas encore des paires RAW d'entraînement qualifiées.** Les 170 CR3
`6V1A0131` à `6V1A0300` sont présents sur `D:/DCIM/101EOSR6`, dont les trois
prises pilotes déjà connues. Chaque fichier a été lu pour les métadonnées et
son aperçu JPEG ; aucun original n'a été déplacé ou modifié.

Mémoire canonique :
[neural-capture-series-v001-20261101T152651Z.json](../raw/neural-capture-series-v001-20261101T152651Z.json).
Les groupes suivent les changements consécutifs de métadonnées : une même
combinaison ISO/temps/focale ne prouve pas l'identité d'une scène.

| Composition observée | Fichiers | ISO | Temps | Nombre |
| --- | --- | ---: | ---: | ---: |
| Intérieur, pilotes à 32 mm | 0131–0133 | 100 | 1/15 s | 3 |
| Intérieur, série à 29 mm | 0134–0151 | 100 | 1/15 s | 18 |
| Intérieur | 0152–0180 | 3200 | 1/200 s | 29 |
| Intérieur | 0181–0194 | 12800 | 1/1250 s | 14 |
| Intérieur | 0195–0209 | 51200 | 1/3200 s | 15 |
| Intérieur, prise isolée | 0210 | 12800 | 1/1250 s | 1 |
| Herbe et feuilles, série à 24 mm | 0211–0243 | 100 | 1/25 s | 33 |
| Herbe et feuilles | 0244–0262 | 1600 | 1/500 s | 19 |
| Herbe et feuilles | 0263–0280 | 6400 | 1/1000 s | 18 |
| Herbe et feuilles | 0281–0300 | 12800 | 1/2500 s | 20 |

Ouverture constante déclarée : f/5,6. La végétation apporte les nervures,
brins fins et détails à faible contraste recherchés ; ne pas assimiler ses
motifs irréguliers à du bruit. Les pilotes à 32 mm ne remplacent pas la
référence intérieure à 29 mm.

### Contrôle préliminaire de stabilité

Recalage euclidien ECC de chaque aperçu contre le premier de son groupe,
largeur maximale 800 px, OpenCV limité à deux threads. Tous les recalages ont
convergé. Dans la série intérieure ISO 100 à 29 mm, le déplacement maximal
est 0,018 px d'aperçu. Dans les quatre séries végétales, il est inférieur à
0,094 px. Ces résultats sont favorables au cadrage global, **sans prouver
l'immobilité locale des feuilles ni la précision à l'échelle du RAW**.

La série intérieure ISO 51200 est moins stable : déplacement maximal
0,539 px d'aperçu et moyenne du gris JPEG comprise entre 0,593 et 0,627
(échelle codée 0..1). L'origine reste à établir : éclairage, traitement du
JPEG ou autre variation. Ce n'est pas une preuve d'instabilité RAW, ni une
raison suffisante pour jeter la série.

Les expositions diffèrent entre séries : le produit ISO × temps n'est pas
constant, et les aperçus changent effectivement de luminosité. Ne pas
soustraire directement une image ISO élevé de la référence ISO 100. Il faut
contrôler la radiométrie linéaire, le recalage inter-séries et les zones
saturées, sans convertir un écart d'exposition en faux bruit.

### Décision et suite

Conserver toutes ces acquisitions ; aucune nouvelle photo demandée à ce
stade. Priorité aux 18 références intérieures et aux 33 références végétales
à ISO 100. La prise 0210 seule ne constitue pas une rafale supplémentaire.
Avant toute moyenne ou entraînement : qualifier mouvement local, netteté,
exposition et saturation dans le RAW, préparer masques valides et références
indépendantes. Garder toutes les variantes ISO d'une même composition dans
une seule partition ; deux compositions ne suffisent pas à démontrer une
généralisation U-Net. Aucun entraînement ou rendu de débruitage n'est lancé
par ce premier audit.

## 1er novembre 2026 — contrôle temporel des mosaïques RAW

**166/166 CR3 lus avec succès ; pas encore de cible d'apprentissage
qualifiée.** Les huit rafales de deux compositions ont été contrôlées en
221,8 secondes. Les trois pilotes à 32 mm et la prise isolée 0210 ne font
pas partie de cette campagne. Aucun fichier source supprimé, déplacé ou
modifié, aucun TIFF produit, aucun entraînement ni changement en production.

Mémoire canonique :
[neural-raw-series-v001-20261101T195454Z.json](../raw/neural-raw-series-v001-20261101T195454Z.json).
Le SHA-256 intégral de chaque source, sa taille, son horodatage, les
métadonnées, la géométrie et les quatre positions CFA sont conservés.
Outil de mesure : `tests/neural/qualify_raw_series.py`, révision `ffdc347` ;
son empreinte exacte figure dans le JSON. La tête Git enregistrée au début
du calcul précède ce commit. Les identifiants canoniques ont été ajoutés
à la publication du JSON, sans changer les mesures ni leur provenance.

### Méthode figée avant lecture des résultats

Décompression rawpy/LibRaw seulement : mosaïque visible entière ouverte,
puis neuf fenêtres fixes centrées à 20/50/80 % de chaque axe. Chaque
fenêtre contient 128 × 128 échantillons **par position du motif Bayer**,
soit environ 256 × 256 pixels capteur. Les coordonnées exactes figurent
dans le JSON. Aucun recalage, interpolation, correction de gain, masque
adaptatif ou rejet de prise. Aucun noir LibRaw soustrait et aucune valeur
déclarée comme calibration physique du R6 Mark III.

Pour chaque fenêtre et position CFA, conservation des moyennes de chaque
prise, de leur étendue, de la dispersion temporelle par pixel, des
différences entre prises consécutives divisées par √2 et de l'écart entre
les moyennes des deux demi-rafales entrelacées. Unités : codes capteur
(DN), sans balance des blancs, profil, gamma ni dématriçage. La dispersion
temporelle **mêle bruit, mouvement et éclairage** : ce n'est pas une
estimation isolée du bruit du capteur. La moyenne d'une rafale ne retire
pas le bruit fixe commun aux prises.

### Résultats de diagnostic

Les deux dernières colonnes sont les médianes des 36 couples fenêtre/CFA,
pas des pourcentages de bruit ni des valeurs RGB développées. L'étendue
porte sur la moyenne spatiale de chaque fenêtre au cours de la rafale ;
la dispersion est la racine de la moyenne spatiale des variances
temporelles non biaisées.

| Composition | ISO | Prises | Étendue des moyennes (DN) | Dispersion temporelle (DN) |
| --- | ---: | ---: | ---: | ---: |
| Intérieur | 100 | 18 | 1,641 | 11,032 |
| Intérieur | 3200 | 29 | 12,685 | 83,279 |
| Intérieur | 12800 | 14 | 15,858 | 158,027 |
| Intérieur | 51200 | 15 | 240,480 | 457,564 |
| Végétation | 100 | 33 | 76,533 | 56,008 |
| Végétation | 1600 | 19 | 8,256 | 53,867 |
| Végétation | 6400 | 18 | 24,313 | 155,779 |
| Végétation | 12800 | 20 | 28,710 | 195,130 |

La variation de la série intérieure ISO 51200 existe aussi dans les codes
RAW : elle ne vient donc pas uniquement du traitement du JPEG. Son origine
(éclairage, mouvement ou autre variation) reste ouverte. La végétation
ISO 100 varie davantage que ne le suggérait le seul recalage global JPEG ;
ce résultat interdit de déclarer sa moyenne comme référence propre sans
vérification locale. Les DN ne sont pas directement comparables entre
ISO et expositions comme un classement de bruit normalisé.

Certaines fenêtres atteignent le blanc **déclaré, non qualifié** par LibRaw
(16383). Le maximum observé sur une fenêtre/un plan/une prise est de 1,208 %
pour l'intérieur ISO 51200, et de 0,018 %, 3,290 %, 0,879 % pour la
végétation ISO 100, 6400 et 12800 respectivement. Ce ne sont pas des taux
sur l'image entière. Les autres groupes n'atteignent pas ce seuil dans la
grille observée ; cela ne prouve pas l'absence d'écrêtage ailleurs ou au
blanc effectif du boîtier. Aucune de ces fenêtres n'est retenue d'office
comme cible d'apprentissage valide.

### Décision et étape suivante

L'intérieur ISO 100 reste le premier candidat pour construire une référence
pilote ; sa stabilité temporelle ne prouve pas encore la netteté ni la
justesse radiométrique inter-ISO. Conserver toutes les rafales, sans nouvelle
acquisition demandée à ce stade. Avant entraînement : vérifier le mouvement
local et le recalage entre ISO, mesurer les écarts de gain/offset sur des
zones distinctes, puis extraire les données **caméra signées du pipeline IPR**
avec sa calibration effective. La décompression CFA de cette étude ne
remplace pas ce contrat d'entrée du réseau.

Construire ensuite les références et masques valides sans sélectionner les
zones sur le gain du futur réseau. Les deux demi-rafales entrelacées sont
un diagnostic de stabilité, pas un jeu de test indépendant. Conserver une
partition par composition/session et annoncer explicitement la limite de
deux compositions. Aucun gain photographique U-Net n'est démontré ici.

Vérification logicielle : 38 tests ciblés réussis (4 audit d'aperçus,
6 qualification CFA, 14 contrats de données, 14 réseau). Les six nouveaux
tests couvrent ordre CFA/géométrie impaire, unités temporelles, texture
statique non confondue avec bruit temporel, entrées invalides et confinement
des sorties au build dédié. Travail local sur `patch_neural_restoration`,
sans publication, fusion dans `dev`/`main` ni tag.

## 1er novembre 2026 — pilote photographique en caméra signée

### Conditions et protocole avant résultats

Précision de l'utilisateur : des branches oscillaient devant la fenêtre,
avec un soleil bas. C'est une cause plausible de variation spatiale et
temporelle de l'éclairage intérieur, **pas une cause démontrée**. Le
contrôle distingue donc une correction radiométrique globale des écarts
locaux ; il n'attribue pas automatiquement ces variations à du bruit.

Gel de campagne : `neural-photo-pilot-v001-20261101T202620Z`. Sélection
déterministe de 69 RAW parmi les 166 déjà contrôlés : les 18 références
intérieures et 33 végétales à ISO 100, puis les prises de début/milieu/fin
des six groupes d'ISO supérieurs. Pas de sélection selon le résultat du
réseau. Extraction avec la production de la révision `ff14324` ; le nouveau
banc est conservé dans `bd1fced`. Compilation et données restent dans le
build du worktree U-Net.

L'extracteur appelle `RawDecoder` en balance boîtier, développement signé,
calibration RAW active et optiques désactivées. Il prélève le véritable
buffer caméra retenu après RCD/médiane et avant LCP/DCP, et refuse une
calibration non appliquée. Le profil Camera Neutral sert au choix de la
calibration ; aucune table ou courbe DCP n'est appliquée aux pixels
d'apprentissage. Neuf fenêtres fixes de 384 × 384 pixels et un petit proxy
vert de recalage sont extraits, sans TIFF ni modification des originaux.

La suite est enchaînée automatiquement, sous conditions :

1. Recalage global par translation dans les proxies verts. Corrélation ECC
   minimale 0,97 et translation maximale de 12 pixels pleine définition.
   Ce modèle ne prouve pas l'absence de rotation ou de mouvement local.
2. Moyennes de rafale ISO 100, avec au moins huit prises recalées. Les
   fluctuations lentes, normalisées par les gains de balance des blancs,
   doivent rester sous 0,001 de l'unité caméra dans les pixels de référence.
   Masques de stabilité, saturation et bords ; pas de clamp des négatifs.
3. Une loi affine par canal pour rapprocher chaque série de la référence :
   cinq fenêtres servent au calcul et quatre autres au contrôle. Au moins
   deux fenêtres de contrôle doivent être cohérentes avant admission d'une
   prise. Les écarts lents résiduels servent à rejeter des fenêtres entières,
   pas à effacer le résidu de bruit fin pixel par pixel.
4. Seule la référence est interpolée dans les coordonnées de la prise
   bruitée ; **les entrées bruitées du réseau ne sont pas interpolées**.
   Découpes de 96 × 96 à positions fixes, au moins 90 % de pixels valides.
5. Intérieur exclusivement en apprentissage, végétation exclusivement en
   validation ; aucun jeu de test indépendant inventé. Seuils minimaux :
   32 découpes d'apprentissage et 16 de validation. Si ces conditions ne
   sont pas satisfaites, arrêt avant entraînement avec motifs conservés.
6. U-Net résiduel de largeur 8, 256 étapes Adam, lots de 4, taux 0,001,
   graine 71, échelle linéaire commune 2. Conditionnement par MAD Haar de
   chaque entrée seule : proxy influencé par les textures et le dématriçage,
   pas connaissance idéale du bruit. Aucun choix de checkpoint ni réglage
   des paramètres selon la validation. CPU : au plus quatre threads, en
   laissant deux processeurs logiques libres lorsque possible.

Ces seuils sont des **garde-fous exploratoires**, pas des garanties de
qualité universelles. Les moyennes conservent du bruit fixe et un bruit
résiduel corrélé par interpolation. La mesure d'erreur se fera contre ces
références imparfaites, pas contre une vérité sans bruit. Un pilote sur deux
compositions ne qualifie pas encore un modèle de production.

Contrôles avant apprentissage réel : compilation native et extraction de
0134 réussies ; calibration appliquée. Les 11 nouveaux tests numériques
et le test synthétique de bout en bout de l'entraîneur passent, en plus
des 38 contrats ciblés précédents. Le test synthétique ne constitue pas un
résultat photographique. Aucun changement de calcul en production.

### Résultat du premier passage et correction du banc

**69/69 extractions réussies en 583,2 s**, calibration appliquée, identité
des sources vérifiée. Les buffers caméra et leurs descripteurs restent dans
`build-patch_neural_restoration/photo-pilot-v001`. Aucun original modifié.
Premier résultat : 45 découpes intérieures et aucune végétale admises ;
arrêt automatique avant entraînement.

Ce premier verdict doit être lu comme **historique et partiellement
invalidé** : le masque de saturation du banc divisait les canaux par les
gains de balance des blancs. Or `RawDevelopment::develop()` borne déjà la
mosaïque balancée à **1 par canal**, dans les deux politiques, avant RCD.
Un rouge à 1 ne redevient donc pas un échantillon non saturé parce que son
gain vaut environ 1,9. Le masque pouvait laisser passer des zones écrêtées.
Cette erreur appartient au banc, pas aux acquisitions ni à LibRaw.

Correction `c5be51c` : seuil appliqué directement dans les axes caméra
balancés ; pixels proches du blanc exclus du recalage vert, avec marge
spatiale. Deux tests supplémentaires couvrent le blanc après gains et
l'absence de solution inventée lorsque le support de recalage est saturé.
**Aucun seuil de corrélation, stabilité ou admission n'a été abaissé.**
Rejeu sur les mêmes captures immuables, sans nouvelle décompression et
sans modification du moteur.

Mémoires canoniques :

- [v001 — premier passage invalidé, sans entraînement](../raw/neural-photo-pilot-v001-20261101T202620Z.json).
- [v002 — qualification corrigée](../raw/neural-photo-pilot-v002-20261101T204009Z.json).

Elles conservent les gels de protocole, les 69 descripteurs d'extraction,
empreintes de sources/pixels/profil/calibration, diagnostics de référence,
recalages, lois radiométriques par canal, fenêtres admises ou rejetées et
identités des découpes. Le résultat absent d'entraînement est `null`, avec
son motif : aucune métrique ou amélioration du réseau n'est inventée.

### État après correction : référence intérieure exploitable, contrôle manquant

| Ensemble | Résultat du rejeu corrigé | Interprétation |
| --- | --- | --- |
| Référence intérieure ISO 100 | Fraction stable admissible : environ 91 % à 100 % selon fenêtre | Bon candidat pour le pilote dans les fenêtres contrôlées ; pas une vérité parfaitement propre |
| Intérieur ISO 3200 : 0152, 0166, 0180 | Corrélation inter-ISO 0,939 à 0,943, sous 0,97 | Non admis par ce modèle global ; pas une preuve que chaque zone de ces photos est inutilisable |
| Intérieur ISO 12800 : 0181, 0187, 0194 | Trois prises avec 3 ou 4 fenêtres de contrôle cohérentes | Découpes admises pour apprentissage exploratoire |
| Intérieur ISO 51200 : 0195, 0202, 0209 | Trois prises avec 2 ou 3 fenêtres de contrôle cohérentes | Certaines zones sont récupérables malgré la variation initiale ; pas rejet de la rafale entière |
| Référence végétale ISO 100 | Fraction admissible d'environ 4 % à 78 % selon fenêtre | Stabilité locale hétérogène ; pas une moyenne propre généralisable à toute l'image |
| Végétation ISO 1600 : 0244, 0253, 0262 | Corrélation inter-ISO 0,835 à 0,837 | Sous le seuil du protocole |
| Végétation ISO 6400 : 0263, 0271, 0280 | Corrélation inter-ISO 0,769 à 0,773 | Sous le seuil du protocole |
| Végétation ISO 12800 : 0281, 0290, 0300 | Corrélation inter-ISO 0,728 à 0,729 | Sous le seuil du protocole |

Total corrigé : **351 découpes intérieures admissibles, 0 végétale**, contre
les minimums figés de 32 en apprentissage et 16 en validation. Les 351
découpes proviennent uniquement des six prises à ISO 12800/51200. Les
écarts radiométriques ont été contrôlés sur des fenêtres distinctes du
calcul de gain/offset. Cela montre qu'une différence de luminosité entre
ISO n'impose pas de jeter une série : certaines zones sont rapprochables.

Pour la végétation, un contrôle complémentaire des JPEG intégrés donne
aussi une corrélation inter-ISO dégradée sur les prises de début de groupe
(environ 0,924, 0,865, 0,846). Ce diagnostic n'est pas une métrique RAW de
vérité ni une preuve de la cause. Il renforce seulement la prudence : le
défaut de correspondance n'apparaît pas exclusivement dans les buffers
caméra extraits. Mouvement, lumière, géométrie non modélisée et différences
de mise au point restent à distinguer ; **ne pas conclure que tout vient
des branches ou que les photos sont mauvaises**.

La mémoire de ce contrôle JPEG est
[neural-preview-cross-check-v001-20261101T204657Z.json](../raw/neural-preview-cross-check-v001-20261101T204657Z.json) :
sélection fixe, empreintes des huit sources, versions et calculs conservés.
Ses scores ne servent pas à admettre des paires RAW.

**L'apprentissage photographique n'a pas été lancé.** Le contrôle d'arrêt
a fonctionné : entraîner sur les seules découpes intérieures puis les
réutiliser en validation créerait une fuite de contenu. Les tests
synthétiques restent des tests logiciels, pas une substitution à la
validation photographique. Aucun poids photographique n'a été produit.

### Suite recommandée et limites de cette étape

Conserver les acquisitions actuelles et les références ; aucune suppression
ni réexport TIFF demandé. Pour débloquer le pilote inter-ISO, la voie la
plus sûre est une **deuxième composition statique** : objets et tissus
immobiles, lumière stable sans ombres mobiles, trépied et mise au point
verrouillée entre séries. Prévoir 16 prises ISO 100, puis au moins 8 à
chaque ISO choisi (par exemple 1600, 6400, 12800), ouverture et cadrage
constants. Adapter le temps pour une exposition comparable, sans chercher
une correspondance exacte ; préserver les hautes lumières. Une scène
intérieure peut rester à l'apprentissage et cette nouvelle composition
entière au contrôle ; une troisième scène réellement réservée sera ensuite
nécessaire avant toute conclusion de généralisation.

Une autre voie serait d'étudier des cibles moyennées **au même ISO**,
constituées de prises indépendantes de l'entrée. Le principe de
[Noise2Noise, ICML 2018](https://proceedings.mlr.press/v80/lehtinen18a.html)
montre l'intérêt de cibles bruitées sous des hypothèses statistiques
appropriées. Ce n'est pas une validation acquise pour notre signal après
RCD/médiane : bruit fixe commun, biais, écrêtage et mouvement peuvent
enfreindre ces hypothèses. Cette variante n'a **pas** été implémentée pour
contourner l'échec du protocole figé ; elle demanderait une étude distincte.

État logiciel : 52 tests ciblés réussis après correction (38 précédents,
13 contrôles de paires et 1 test synthétique d'apprentissage de bout en
bout). Aucune intégration ou publication Git ; production inchangée.

## 1er novembre 2026 — acquisition de la nouvelle composition de contrôle

À la demande de l'utilisateur, récupération de **66 CR3**, de `6V1A0301`
à `6V1A0366`, depuis `D:/DCIM/101EOSR6`, dans le corpus partagé
`tests/image/colorimétrie/neural-validation`. Volume : **2 742 941 352 octets**.
Copie exclusive sans écrasement ; les 66 empreintes SHA-256 des copies sont
identiques à celles des sources. Aucun original supprimé ou modifié.

| Groupe chronologique | Fichiers | ISO | Temps | Nombre |
| --- | --- | ---: | ---: | ---: |
| 1 | 0301–0309 | 100 | 0,8 s | 9 |
| 2 | 0310–0319 | 1600 | 1/10 s | 10 |
| 3 | 0320–0327 | 100 | 0,8 s | 8 |
| 4 | 0328–0339 | 3200 | 1/40 s | 12 |
| 5 | 0340–0351 | 6400 | 1/80 s | 12 |
| 6 | 0352–0366 | 12800 | 1/160 s | 15 |

Boîtier déclaré : Canon EOS R6 Mark III ; ouverture f/5,6 et focale 24 mm
constantes. Inspection des JPEG intégrés : nouvel agencement des mêmes
types de matières, éclairage plus chaud ; cette composition entière est
réservée à la **validation**, tous ISO confondus, sans ajout à l'apprentissage.
Cela reste une scène de contrôle apparentée, pas un test produit indépendant
sur de nouvelles familles de matières ou un autre boîtier.

Le dépistage sur aperçus de 800 px au maximum ne rencontre aucun échec
de recalage intra-groupe. Corrélation ECC minimale selon groupe : environ
0,999919 à 0,999990 ; translation maximale d'environ 0,058 pixel d'aperçu.
Ces résultats sont encourageants, mais **ne qualifient ni la stabilité
locale RAW, ni l'absence d'écrêtage, ni les cibles propres du réseau**.

Les deux séries ISO 100 sont conservées séparément : leur niveau gris moyen
encodé diffère d'environ 1,5 %, sans cause établie. Ne pas les réunir comme
référence de 17 prises avant contrôle RAW. ISO 1600 est également plus
exposé : le produit ISO × temps vaut 160 contre 80 pour les autres groupes.
Une adaptation radiométrique reste possible dans le protocole, sans
prétendre que la lumière réelle ou le traitement JPEG sont identiques.

Mémoires canoniques :

- [Inventaire et stabilité des aperçus](../raw/neural-capture-series-v001-20261101T212959Z.json).
- [Acquisition et 66 couples d'empreintes](../raw/neural-corpus-import-v001-20261101T213020Z.json).

Quatre tests ciblés vérifient la copie, le refus d'écrasement, le refus d'un
inventaire périmé et la détection d'une empreinte divergente. Les copies
partielles restent visibles en cas d'erreur ; aucun nettoyage automatique.
Prochaine action : qualification des références et des paires dans le signal
caméra signé IPR, avec les mêmes critères qu'au passage corrigé précédent.
**Aucun nouvel entraînement photographique ni changement moteur à ce stade.**

## 1er novembre 2026 — qualification RAW de la nouvelle composition : lancement

Campagne gelée `neural-control-pairs-v001-20261101T213657Z`, banc local
`41cd686`, dans `build-patch_neural_restoration/control-pairs-0301-0366-v001`.
Ce paragraphe constate le lancement, **pas le résultat** de la qualification.

Sélection avant mesure : les 17 références ISO 100, et le début, milieu
entier inférieur et dernier fichier de chacune des quatre séries à ISO
plus élevé, soit **29 extractions**. Référence principale : 0301–0309,
première série chronologique ; 0320–0327 reste un diagnostic distinct.
Ne pas réunir les références ni remplacer la principale après observation
des nombres de fenêtres admises. La comparaison porte sur les mêmes
12 fichiers bruités dans les deux passages.

Signal extrait par le banc natif reconstruit : RGB caméra signé balancé,
après RCD/médiane, avant LCP/DCP, sans réduction du bruit ni corrections
optiques. Profil Camera Neutral et calibration identifiés par empreinte ;
la première extraction 0301 confirme la calibration réellement appliquée.
Les seuils et fenêtres du passage corrigé v002 sont repris sans modification.

L'identité de composition et la partition sont désormais lues dans le gel,
au lieu d'une liste codée limitée à `indoor`/`vegetation`. Refus des sources
dupliquées, d'une partition inconnue et d'une référence incomplète. Huit
tests nouveaux et les treize tests numériques de paires réussissent ;
construction de l'extracteur réussie. Aucun calcul photographique modifié.

Cette campagne ne contient **que de la validation**, donc aucun exemple
d'apprentissage. Le statut interne du qualificateur partagé exigeant à la
fois apprentissage et validation reste insuffisant pour entraîner ; le
bilan externe distinguera explicitement l'admission de paires de contrôle.
Même si elle réussit, cette étape ne lance pas automatiquement de réseau.
Le résultat final et les diagnostics seront conservés dans un nouveau JSON
canonique au nom de la campagne ; aucune ancienne preuve n'est remplacée.

### Résultat : rafales stables, contrôle principal bloqué par le déplacement

La campagne est terminée : **29/29 extractions**, environ 199 s pour
l'extraction et les deux qualifications. Aucune erreur du moteur ou de
calibration. Les neuf fenêtres de chaque référence ISO 100 ont une fraction
stable et valide de 100 % dans le support intérieur mesuré ; cela ne fait
pas de leur moyenne une vérité parfaitement propre.

Le contrôle principal produit **0 découpe** : toutes les prises bruitées
dépassent la limite de déplacement de 12 pixels caméra. Leurs corrélations
ECC sont pourtant d'environ 0,993 à 0,997, au-dessus du seuil 0,97.
Les translations estimées sont d'environ 93 pixels horizontalement à ISO
1600, puis 129–130 aux autres ISO, avec environ −13 à −16 verticalement.
Le motif de rejet est ici la portée géométrique du banc, **pas une
corrélation insuffisante ni une instabilité démontrée des rafales**.

La seconde référence admet **243 découpes à ISO 1600**, sur trois prises
déplacées d'environ 4 pixels horizontalement. Les autres ISO restent
rejetés par des déplacements d'environ 40 pixels, malgré une corrélation
supérieure à 0,997. Ces découpes diagnostiques ne remplacent pas le contrôle
principal après observation des résultats. Aucun entraînement n'est lancé.

Preuve : [qualification des deux références](../raw/neural-control-pairs-v001-20261101T213657Z.json).
Le protocole initial et son résultat restent conservés sans modification.

### Audit suivant : translation, rotation rigide et transformation affine

Même signal immuable : 2 références × 12 prises × 3 modèles = **72 cas**,
environ 14,7 s. Fenêtres d'ajustement radiométrique et de contrôle distinctes,
seuils radiométriques et locaux inchangés. Seule la référence est interpolée,
jamais l'entrée bruitée ; supports transformés et érodés pour exclure les
voisins invalides d'interpolation. Les deux séries ISO100 restent séparées.

L'audit **mesure au-delà de 12 pixels sans promouvoir de paires**. Il
diagnostique l'explication du rejet, sans modifier silencieusement le gel.

| Référence et modèle diagnostique | ISO 1600 | ISO 3200 | ISO 6400 | ISO 12800 |
| --- | ---: | ---: | ---: | ---: |
| Principale, translation | 2/4 | 2/4 | 2/4 | 1/4 |
| Principale, rigide | 1/4 | 1/4 | 1/4 | 0/4 |
| Principale, affine | 1/4 | 1/4 | 1/4 | 0/4 |
| Secondaire, translation | 4/4 | 4/4 | 4/4 | 3/4 |
| Secondaire, rigide | 4/4 | 4/4 | 4/4 | 3/4, 3/4, 4/4 |
| Secondaire, affine | 4/4 | 4/4 | 4/4 | 3/4 |

Chaque cellule indique le nombre de fenêtres de contrôle cohérentes pour
chacune des trois prises, sauf la cellule détaillée. Les 72 lois
radiométriques restent dans les bornes initiales. Un score global meilleur
ne suffit donc pas : rotation et affine ne rendent pas le contrôle
principal localement meilleur.

Preuve : [audit géométrique v001](../raw/neural-geometry-audit-v001-20261101T214408Z.json).
Six tests synthétiques réussissent : coordonnées globales/locales, support
interpolé, déplacement supérieur à la limite initiale, retour à l'origine
du proxy recadré et refus des entrées invalides.

**Conclusion actuelle : aucune nouvelle photo demandée à ce stade.**
Avant le pilote neuronal, préparer une version explicite du protocole
géométrique : support commun valide plutôt que portée fixe de 12 pixels,
garde de recouvrement, contrôles locaux et première référence conservée.
Ne pas simplement augmenter le seuil ou choisir la seconde référence parce
qu'elle passe mieux. ISO12800 reste insuffisant sur le contrôle principal
dans ce diagnostic. Aucun gain photographique du réseau revendiqué,
aucune modification moteur, aucun calcul actif à la clôture de cet audit.

## 1er novembre 2026 — protocole de recouvrement et contrôle principal débloqué

Révision explicite du protocole géométrique, pas rectification rétroactive
de l'ancien rejet : la portée fixe de 12 pixels disparaît du qualificateur
courant. L'estimation reste une translation, sans rotation ni affine.
Deux nouvelles gardes sont figées avant le rejeu : au moins **50 % de
recouvrement géométrique pleine image** et une erreur de réciprocité
aller-retour d'au plus **1 pixel caméra**, avec ECC ≥ 0,97 dans les deux sens.
Ces gardes sont provisoires, pas des constantes universelles de qualité.

Le recouvrement global ne suffit pas à admettre une paire. Les exigences
existantes restent présentes : stabilité des références, saturation caméra,
bornes de la loi radiométrique, au moins 50 % de support local, deux fenêtres
de contrôle cohérentes distinctes des cinq fenêtres d'ajustement, puis 90 %
de pixels valides par découpe. Les masques interpolés exigent maintenant
que **tous** les voisins bilinéaires contributeurs soient valides ; un
masque au plus proche ne suffit plus. Aucune entrée bruitée n'est interpolée.

Le rejeu utilise les captures immuables existantes, sans nouvelle lecture
des RAW, et **uniquement la première référence ISO100 (0301–0309)**.
La seconde reste hors de cette qualification. Gel et outils : `67f9bdf`.
Résultat en environ **3,65 s** :

| ISO de contrôle | Prises admises / évaluées | Découpes admissibles |
| --- | ---: | ---: |
| 1600 | 3/3 | 54 |
| 3200 | 3/3 | 60 |
| 6400 | 3/3 | 72 |
| 12800 | 0/3 | 0 |
| Total | 9/12 | 186 |

Recouvrement pleine image mesuré : environ 97,81 % à 98,40 % ; erreur
aller-retour maximale **0,118 pixel**. Les deux nouvelles gardes passent
pour les douze prises ; ISO12800 reste refusé par la cohérence locale
(1 fenêtre de contrôle au lieu de 2), et non par le déplacement.
Les neuf fenêtres de la référence conservent 100 % de support intérieur
stable et valide selon le diagnostic du banc.

Mémoire canonique complète :
[contrôle par recouvrement v001](../raw/neural-control-overlap-v001-20261101T215121Z.json).
Elle conserve les conditions initiales, la raison de révision, les
empreintes des captures, les décalages dans les deux sens, les masques
résumés, les décisions par fenêtre et les 186 identités de découpes.
Les campagnes antérieures restent inchangées. Les 186 découpes sont
corrélées, issues de neuf prises d'une seule composition, pas de 186 scènes.

Tests : huit contrats de recouvrement/réciprocité/masques, treize tests
numériques de paires, huit de gel des références et six de géométrie.
Le refus explicite d'un résultat non fini du solveur évite toute preuve
JSON contenant NaN. Ces tests vérifient le banc, pas la qualité du réseau.

**Conclusion actuelle : le pilote dispose d'un contrôle exploratoire
admissible à ISO1600–6400. Aucun entraînement lancé dans cette étape.**
Suite : reconstruire le jeu d'apprentissage existant avec le même
qualificateur courant, vérifier séparation par composition et couverture
ISO, puis lancer le pilote à architecture et durée figées. Les anciennes
découpes d'apprentissage étaient à ISO12800/51200 : ne pas présenter le
contrôle à ISO1600–6400 comme une validation aux mêmes ISO ou comme une
qualification produit. Garder ISO12800 exclu du contrôle jusqu'à preuve
complémentaire, sans choisir la seconde référence pour obtenir un succès.
Pas de nouvelle photo demandée à ce stade ; aucun changement moteur,
publication Git ou calcul actif à la clôture de cette étape.

## 1er novembre 2026 — premier pilote photographique figé : résultat négatif

Campagne `neural-reserved-photo-pilot-v001-20261101T220513Z`, orchestration
locale `50afa23`. Requalification des deux compositions avec exactement le
même protocole courant, sans nouvelle décompression : **342 découpes
d'apprentissage et 186 de contrôle**. Les 186 couples d'empreintes de pixels
entrée/cible et leurs identités sont **strictement identiques** au contrôle
qualifié précédent. Aucun exemple de la nouvelle composition n'entre dans
l'optimiseur ; aucune partition de test indépendante n'est inventée.

Le contrôle de provenance initial a arrêté le lancement avant calcul :
l'identifiant embarqué du premier moteur précédait le commit d'ajout du
banc d'extraction. Il ne désignait donc pas son fichier C++ d'extraction.
La vérification distingue désormais ces deux identités : sources de
production et calibration identiques entre les moteurs ; source historique
de l'extracteur vérifiée par l'empreinte canonique du commit `bd1fced`.
Sa seule différence avec l'actuelle est le nettoyage d'includes déjà relu
(`algorithm` retiré, `QCommandLineOption` explicité), sans changement de
corps de calcul. Toute autre différence est refusée. Sept tests de
provenance/partition et le test synthétique de bout en bout passent.
Ce correctif du banc ne modifie ni les captures ni les seuils photographiques.

Configuration reprise du gel initial, sans recherche : petit U-Net résiduel
à deux niveaux, largeur 8, six canaux d'entrée caméra/sigma, échelle commune
2, graine 71, Adam à 0,001, lot 4, **256 pas**, seul état final évalué.
Calcul CPU : 4 threads internes et 1 inter-opération, sur 20 processeurs
logiques disponibles. Environ **7,64 s** d'entraînement et évaluation,
**18,91 s** pour l'orchestration complète. Ces temps sur découpes ne
prédisent pas la latence d'un traitement pleine image ou d'un modèle produit.

| Partition / ISO | Découpes | Rapport MSE sortie / entrée |
| --- | ---: | ---: |
| Apprentissage ISO 12800 | 198 | 1,01308 |
| Apprentissage ISO 51200 | 144 | 0,99586 |
| Apprentissage, total pondéré | 342 | 0,99893 |
| Contrôle ISO 1600 | 54 | 1,03120 |
| Contrôle ISO 3200 | 60 | 1,08318 |
| Contrôle ISO 6400 | 72 | 1,02938 |
| Contrôle, total pondéré | 186 | 1,04192 |

Rapports pondérés par le nombre de pixels-canaux valides, et non moyenne
des rapports par découpe. Valeur inférieure à 1 = erreur plus faible par
rapport à la référence moyennée ; supérieure à 1 = erreur accrue. Le modèle
ne gagne qu'environ **0,11 %** en MSE globale d'apprentissage et **dégrade
le contrôle d'environ 4,19 %**. La pire découpe de contrôle atteint un
rapport de 1,586. Aucune équivalence perceptuelle, Lightroom ou amélioration
de bruit visible ne se déduit de ces chiffres.

Mémoire canonique :
[premier pilote à contrôle réservé](../raw/neural-reserved-photo-pilot-v001-20261101T220513Z.json).
Elle conserve les 48 descripteurs de captures, gels parents, contrats,
configurations, décisions et 528 identités de découpes, la suite des pertes,
les métriques par découpe et les empreintes du jeu et des poids. Les buffers,
le jeu `dataset.npz` et les poids `pilot-weights.pt` restent dans le build
dédié ; aucun fichier de modèle n'est intégré en production ou publié.

**Conclusion actuelle : pas de gain de débruitage démontré par ce pilote.**
Ne pas régler largeur, durée, force ou sélection d'état final en fonction
de cette scène de contrôle pour transformer cet échec en succès apparent.
Le faible apprentissage global et l'écart de couverture ISO empêchent
d'attribuer le résultat uniquement à la généralisation. Il ne réfute pas
non plus l'intérêt d'un réseau neuronal avec des données et un entraînement
adéquats : il ne qualifie que cet essai très court sur deux compositions
apparentées, sans ISO commun entre apprentissage et contrôle.

Prochain diagnostic recommandé, **sur l'apprentissage seulement** : vérifier
la capacité à apprendre un problème contrôlé et distinguer sous-apprentissage,
bruit des cibles et biais des paires avant toute nouvelle campagne de
transfert. Conserver ce premier contrôle comme résultat historique ; si
ses résultats pilotent ensuite des choix de modèle, il ne pourra plus être
présenté comme un test final vierge. Pas de nouvelle photo demandée à ce
stade, pas de variante relancée automatiquement, aucune modification moteur,
aucune publication Git et aucun calcul encore actif.

## 1er novembre 2026 — diagnostic d'apprenabilité sur l'apprentissage : lancement

Campagne `neural-train-learnability-v001-20261101T221250Z`, outil local
`d9b44a7`. Aucun nouveau RAW, aucune modification moteur. Le jeu parent
est vérifié par empreintes ; seuls les **342 exemples d'apprentissage**
entrent dans les tenseurs, métriques et optimisations. L'archive partagée
contient aussi les exemples de contrôle, mais leurs lignes ne sont pas
sélectionnées. Un test place volontairement NaN dans les lignes réservées
et vérifie qu'elles n'entrent pas dans ce diagnostic.

Plan figé avant exécution, sans recherche d'hyperparamètres :

| Cas | Données et objectif | Pas fixés |
| --- | --- | ---: |
| Décalage connu | Quatre cibles d'apprentissage à indices régulièrement espacés ; ajout RGB constant (+0,02 ; −0,015 ; +0,01) | 256 |
| Bruit synthétique renouvelé | Mêmes quatre cibles ; ajout gaussien indépendant σ = 0,02 à chaque mise à jour ; réalisation distincte fixe pour la mesure | 1024 |
| Mémorisation d'une paire | Première paire réelle d'apprentissage, sélectionnée par son ordre et non sa performance | 1024 |
| Apprentissage réel prolongé | Les 342 exemples, mêmes réglages que le pilote ; mesures aux pas 0, 256, 1024, 2048 | 2048 |

Architecture, largeur 8, graine 71, lot 4, Adam à 0,001 et échelle 2
inchangés. Quatre threads CPU maximum, un thread inter-opération. Les
mesures d'apprentissage ne sélectionnent aucun état du modèle ; aucun
réglage ni aucune inférence ne consulte la composition de contrôle.

Le cas de décalage vérifie la capacité d'optimisation élémentaire. Le cas
gaussien vérifie un problème de bruit **connu**, pas le bruit réel du capteur.
Les moyennes de rafales restent des images imparfaites ; leur bruit est ici
une partie fixe de la cible synthétique. La mémorisation ne vaut pas
généralisation ; le calcul prolongé distingue seulement quelques causes
possibles du faible apprentissage initial.

Le banc relève aussi le biais moyen par canal et l'énergie de la moyenne
locale 9×9 du résidu entrée/cible, par ISO d'apprentissage. Ces diagnostics
ne peuvent, à eux seuls, distinguer texture, erreur de recalage et bruit
corrélé. Les normes de gradient du début vérifient notamment que le corps
du réseau reçoit un gradient après le premier pas : la tête initialisée à
zéro donne normalement un gradient nul au corps au tout premier pas.

Six tests ciblés passent : sélection exclusive du train, conservation du
signal signé, refus d'empreintes altérées ou d'un train vide, propagation
des gradients, reproductibilité de la graine et statistique d'un résidu
constant. Ce paragraphe constate **le lancement, pas le résultat** de la
campagne. Les poids diagnostiques restent dans le build, sans déploiement.

### Résultat : apprentissage fonctionnel, budget initial trop court pour ces données

Campagne terminée sans erreur, en **77,72 s**. Les quatre cas fixés ont
été exécutés ; le jeu parent et les sources des outils sont restés
inchangés. Aucune inférence ou mesure sur la composition de contrôle.

| Cas | Rapport MSE après 256 pas | Après 1024 pas | Après 2048 pas |
| --- | ---: | ---: | ---: |
| Décalage RGB connu | 0,000000489 | — | — |
| Bruit gaussien renouvelé, mesure sur réalisation indépendante | 1,00010 | 0,62725 | — |
| Mémorisation de la première paire réelle | 0,98643 | 0,23437 | — |
| Toutes les paires d'apprentissage réelles | 0,99893 | 0,26964 | 0,15765 |

Les ratios sont mesurés par rapport à l'entrée, sur les pixels-canaux
valides. Le cas réel prolongé réduit donc l'erreur **d'apprentissage**
d'environ 73,0 % à 1024 pas, puis 84,2 % à 2048 pas. Ce ne sont pas des
pourcentages de bruit visuellement retiré, ni un résultat de généralisation.
Le diagnostic à 256 pas retrouve la MSE d'apprentissage du premier pilote
à environ 2,2×10⁻¹¹ près (évaluation par lots différente), ce qui soutient
la comparaison de trajectoire plutôt qu'un changement caché de données.

Les normes de gradient confirment le fonctionnement attendu : tête
non nulle au premier pas, corps nul à ce seul premier pas puis non nul
au suivant, dans les quatre cas. Le réseau n'est donc pas bloqué à
l'identité. Le décalage connu est appris ; le bruit synthétique indépendant
diminue après entraînement plus long, sur les quatre mêmes cibles de
train. La mémorisation d'une paire réussit partiellement, sans rien
prouver sur une photo inconnue.

Le résidu réel n'est pas expliqué principalement par un décalage RGB
constant : le carré du biais moyen par canal représente environ 0,009 %
de la MSE à ISO12800 et 0,48 % à ISO51200. L'énergie de sa moyenne locale
9×9 représente environ 10,25 % et 9,96 % de la MSE. Cela ne sépare pas
bruit corrélé, erreur géométrique et structure ; aucune attribution à
une « vérité de bruit » n'est faite.

Preuve complète :
[diagnostic d'apprenabilité train-only v001](../raw/neural-train-learnability-v001-20261101T221250Z.json).
Elle conserve les 342 identités train, les quatre plans et graines, les
4352 pertes de mise à jour, les mesures aux points fixés, gradients et
empreintes des quatre modèles. Ces poids restent **diagnostiques** dans
le build ; ils ne remplacent ni le pilote initial ni le moteur classique.

**Conclusion actuelle : piste concrète de sous-apprentissage du premier
pilote à 256 pas, pas de preuve d'amélioration sur une nouvelle image.**
Le diagnostic exclut un blocage élémentaire de la chaîne d'optimisation ;
il ne démontre ni la propreté des cibles, ni la conservation des textures,
ni la cause unique de la dégradation de 4,19 % sur le contrôle initial.

Prochaine épreuve recommandée : geler l'état réel à 2048 pas, choisi ici
sur le train uniquement, et effectuer **un contrôle de transfert distinct**,
avec erreur, biais chromatique et préservation des structures. Ne pas
chercher ensuite la meilleure durée ou force sur cette composition.
La scène de contrôle a déjà servi à un premier pilote : elle n'est pas un
test final vierge ; un futur jeu final indépendant restera nécessaire.
Pas de nouveau calcul lancé après ce diagnostic, de changement moteur,
de push, de fusion ou de tag.

## 1er novembre 2026 — transfert du modèle figé à 2048 pas : échec du contrôle

Campagne `neural-reserved-transfer-v001-20261101T222029Z`, banc `84f7d7d`.
Un seul état évalué : `real_training_long` à 2048 pas, empreinte vérifiée
contre le diagnostic train-only, force **100** fixée avant mesure. Aucun
optimiseur, réentraînement, recherche de force ou sélection de checkpoint.
Les 186 découpes de contrôle restent celles de la première référence
ISO100 ; pas de substitution de référence ou de modification des masques.

Les seuils de structure sont les quartiles 25 % et 75 % du module du
gradient vert caméra lissé des **cibles d'apprentissage uniquement** :
environ 0,00074636 et 0,00344116, en unités caméra par pixel. Les gradients
de contrôle ne choisissent donc pas leurs propres catégories après rendu.
Une mesure centrale exclut 24 pixels sur chaque bord de découpe pour
réduire l'effet du contexte limité des convolutions. Les indicateurs de
structure requièrent aussi un voisinage 5×5 entièrement valide.

### Erreur de reconstruction et couleur

| ISO de contrôle | Rapport MSE complète | Rapport MSE au centre | Rapport d'erreur opposée couleur au centre |
| --- | ---: | ---: | ---: |
| 1600 | 2,02684 | 1,84831 | 12,11289 |
| 3200 | 0,99396 | 0,92860 | 3,51251 |
| 6400 | 0,59007 | 0,53095 | 1,72353 |
| Total pondéré | 1,31050 | 1,20773 | 5,94242 |

Ratios sortie / entrée relativement à la même cible, pondérés par le
nombre de pixels valides. L'erreur totale augmente donc de **31,05 %**,
et de **20,77 %** au centre. Un gain MSE est présent à ISO6400, et
faiblement à ISO3200, mais il ne valide ni la couleur ni les détails.
Sur l'ensemble des découpes, le rapport d'erreur opposée couleur vaut
6,74003 ; l'exclusion des bords ne fait pas disparaître le défaut.

« Couleur » désigne ici les axes **R−G et B−G du RGB caméra balancé**,
pas une chrominance perceptuelle ou un ΔE. Les écarts moyens centraux de
sortie par rapport aux cibles sont environ (−0,00287 ; −0,00766 ; +0,00696)
dans les trois canaux caméra. Ils ne permettent pas de déduire directement
une dominante d'affichage sans développement par le profil et rendu écran.

### Structures : le lissage seul ne suffit pas

Sur les fenêtres de structure forte, le gain de projection des gradients
sur ceux de la référence passe de **0,72309 à l'entrée à 0,32687 en sortie**.
Pour les structures faibles : 0,25944 à 0,08379. Cela signale une
atténuation de la structure corrélée à la référence, sans permettre de
chiffrer un pourcentage absolu de « détails perdus » : l'entrée n'est déjà
pas à un gain de 1 et les références restent bruitées/imparfaitement calées.

En parallèle, l'erreur quadratique de gradient baisse sur les structures
fortes (rapport 0,49676) et faibles (0,18395). **Une réduction de l'erreur
haute fréquence peut donc coexister ici avec une perte de structure
corrélée**. Ne pas confondre cette baisse avec une conservation des détails
ou avec une amélioration photographique visuellement validée.

Preuve complète :
[transfert du modèle figé v001](../raw/neural-reserved-transfer-v001-20261101T222029Z.json).
Elle conserve le gel avant inférence, les empreintes des poids et données,
les définitions des métriques et empreintes des outils, les sommes par découpe,
les dénominateurs et les agrégations par ISO. Calcul d'inférence et métriques
environ **1,55 s** sur les découpes, CPU quatre threads ; pas une mesure de
traitement pleine définition. Aucune interpolation ou modification des
entrées, aucune saturation artificielle du signal signé.

Huit tests ciblés passent : identité, absence de faux gain à erreur nulle,
détection de l'aplatissement des gradients, biais couleur signé, voisinages
invalides, pondération des pixels, régions vides et refus de poids ou
checkpoint différents. La force zéro reste une identité exacte.

**Conclusion actuelle : ne pas retenir ces poids pour la production ni
comme candidat de validation visuelle dans IPR.** L'apprentissage prolongé
réduit l'erreur sur le train, mais ne résout pas le transfert au corpus de
contrôle et amplifie les défauts de couleur ; prolonger encore les pas
n'est pas une justification suffisante. Le changement simultané de scène,
d'éclairage et d'ISO empêche d'en attribuer une cause unique avec ce seul test.

Suite recommandée : travailler sur la diversité et la représentativité du
jeu d'apprentissage et sur le modèle de bruit conditionnel, notamment aux
faibles niveaux de bruit, plutôt que chercher une force qui passe sur cette
scène. Inventorier d'abord les données déjà présentes ; aucune nouvelle
prise demandée à ce stade. Cette composition a désormais été consultée
deux fois : ne pas la déclarer test final vierge dans une campagne future.
Aucun nouveau calcul lancé après ce contrôle, aucun changement moteur,
publication, fusion ou tag.

## 1er novembre 2026 — audit du corpus existant et couverture du faible bruit

Audit local `44e3489`, sans entraînement ni développement RAW, sur les
dossiers partagés `colorimétrie`, `base`, `base-hors-protocole`, les images
utilisateur et les deux cartes SD D:/E:. Formats parcourus : CR3 et CR2
uniquement ; ne pas présenter cet inventaire comme celui de tous les formats
ou de toutes les marques. Lecture de métadonnées statiques CMT pour CR3,
TIFF/EXIF par Pillow pour CR2, puis criblage borné des JPEG intégrés.

**4 011 fichiers lus**, copies comprises :

| Boîtier déclaré | Fichiers avec métadonnées lues |
| --- | ---: |
| Canon EOS R6 Mark III | 2032 |
| Canon EOS 60D | 1733 |
| Canon EOS R7 | 246 |

Trois autres CR2 ne sont pas identifiés par Pillow : `IMG_7039.CR2`,
`IMG_7040.CR2`, `IMG_7041.CR2`, dans `Pictures/RAW/Oliana` ; **cela ne
démontre pas une corruption ni un échec de LibRaw**. Le diagnostic et
les chemins sont conservés. Ces fichiers
restent hors du classement de métadonnées ; ils ne bloquent pas l'étude R6III.

L'inventaire signale 387 enregistrements avec des alias possibles par
nom, modèle et date de prise. Il ne calcule pas toutes les empreintes
complètes : ces rapprochements sont une **quarantaine conservatrice**,
pas un dédoublonnage certifié. Aucune image supprimée, déplacée ou copiée.
La propagation des rôles protège aussi les copies possibles des compositions
de contrôle et les séries de calibration.

| Classe de provenance / usage possible | Enregistrements |
| --- | ---: |
| Images R6III non qualifiées, isolées ou en séquence | 1204 |
| Rafale existante à qualifier pour le réseau | 42 |
| Calibration seulement, selon provenance du dossier | 252 |
| Composition d'apprentissage déjà utilisée | 79 |
| Composition de contrôle déjà évaluée | 132 |
| Végétation initialement réservée, non qualifiée | 91 |
| Astronomie spécialisée, à traiter séparément | 232 |
| Autres boîtiers nécessitant qualification distincte | 1979 |

Ces nombres désignent des fichiers et copies, **pas des scènes physiques
indépendantes**. Un nom tel que `6V1A0145` ou `6V1A0282` peut aussi désigner
une prise d'août et une prise de novembre différentes : conserver modèle,
date, chemin et SHA-256, jamais le seul nom comme identité de corpus.

### Le manque de couverture est mesurable dans les données du pilote

Le modèle a vu **six RAW bruités d'une seule composition** pendant
l'apprentissage, contre neuf RAW d'une autre composition au contrôle.
Les nombres de découpes 342/186 masquaient cette faible diversité.

| Canal caméra | Médiane du proxy sigma en train | Médiane au contrôle | Rapport train / contrôle |
| --- | ---: | ---: | ---: |
| R | 0,009631 | 0,003955 | 2,44 |
| G | 0,008973 | 0,003463 | 2,59 |
| B | 0,009582 | 0,003532 | 2,71 |

Le 10e percentile du train est même au-dessus du 90e percentile du contrôle
dans les trois canaux. **Ce proxy Haar/MAD est affecté par les textures et
les corrélations du dématriçage : ce n'est pas un sigma physique certain.**
Il montre néanmoins que la variable de conditionnement réellement fournie
au réseau n'a pas la même distribution entre les deux partitions. L'ISO,
à lui seul, ne décrit pas le bruit ; la luminosité et les distributions de
couleur des pixels échantillonnés diffèrent également.

Vérification complémentaire des descripteurs parents : les six RAW de train
et les neuf de contrôle ont le même `camera_neutral`, environ
(0,52837977 ; 1 ; 0,59088286). **Ne pas attribuer ces écarts à un changement
de ce vecteur de balance**. Cela n'implique pas que la lumière physique ou
les couleurs de la scène étaient les mêmes. Les descripteurs et les
distributions sont traçables dans les preuves parentes et l'audit.

### Séquences candidates : priorité à la rafale existante ISO 4000

Dix séquences de huit fichiers ou plus satisfont le groupement temporel
conservateur (noms consécutifs, réglages identiques, intervalles bornés).
Neuf groupes ont été criblés par leurs prises de début, milieu et fin,
soit au maximum 27 aperçus avec empreintes des sources sélectionnées.
Ce groupement ne prouve ni cadrage fixe, ni immobilité, ni référence propre.

- **`colorimétrie/rafale/6V1A9819.CR3` à `6V1A9860.CR3` : 42 prises,
  ISO4000, 1/200 s, f/5,6, 26 mm.** Priorité pour une qualification caméra
  signée au même ISO, sans remélanger au contrôle. Le dépistage JPEG donne
  des ECC d'environ 0,998 au milieu et 0,988 à la fin, mais aussi des
  déplacements d'environ 53 et 112 pixels pleine image. Il faut contrôler
  recouvrement, mouvement local et évolution du signal après développement.
- Huit prises `6V1A9984` à `9991` à ISO250 sur E: sont très majoritairement
  sombres (environ 99,7 % de pixels d'aperçu sous le seuil de gris 0,01).
  Usage spécialisé à identifier : **ni cible propre générique ni preuve
  de bouchon fermé**, malgré des corrélations élevées.
- Les autres séquences criblées, à ISO400/3200/4000/8000, ont des changements
  de correspondance importants ou des échecs de recalage. Les ECC de
  plusieurs prises sont entre environ 0,41 et 0,94. Ne pas les moyenner
  automatiquement comme des scènes immobiles ; les zones locales peuvent
  rester utiles, mais ne sont pas qualifiées par cet audit.

Les 252 fichiers de provenance calibration peuvent aider à construire et
contrôler un modèle de bruit ; des noirs et aplats ne remplacent pas la
diversité de textures nécessaire à un réseau. Les CR2 du 60D et les CR3
du R7 restent des chantiers séparés : ne pas mélanger leurs RGB caméra
avec ceux du R6III sans contrat colorimétrique et caractérisation adaptés.

### Images isolées à faible ISO : supports possibles, pas vérité propre

Un second criblage borné `c88bb21` trouve **510 enregistrements candidats**
à ISO ≤ 800, focale ≤ 105 mm et temps ≤ 1/250 s, hors rôles protégés.
Ce sont des priorités de lecture, pas une preuve de faible bruit réel.
Sélection déterministe d'au plus une image par date, sans score du modèle :

| Fichier | Date de prise | ISO | Emplacement |
| --- | --- | ---: | --- |
| 6V1A9072.CR3 | 17 juillet 2026 | 100 | Pictures/RAW/pleneuf |
| 6V1A9938.CR3 | 25 juillet 2026 | 100 | Pictures/RAW/24-07-26 |
| 6V1A0005.CR3 | 5 août 2026 | 100 | Pictures/RAW/05-08-26 |
| 6V1A0145.CR3 | 12 août 2026 | 100 | tests/image/base-hors-protocole |
| 6V1A0282.CR3 | 23 août 2026 | 100 | D:/DCIM/100EOSR6 |

Les cinq aperçus sont lus et leurs sources empreintées. Celui de 9938 a
environ 6,9 % de pixels avec au moins un canal proche du blanc encodé :
contrôler les zones RAW avant usage, sans conclure à l'écrêtage du RAW
depuis le JPEG. Cinq dates ne prouvent pas cinq scènes indépendantes.
Une image ISO100 isolée reste bruitée et n'a pas de cible propre appariée.

Elle pourrait fournir un support de contenu pour un apprentissage avec
bruit ajouté, **à condition de qualifier le bruit ajouté**, ses dépendances
au signal, aux canaux et au développement, et de conserver le bruit résiduel
de la cible dans les limites de conclusion. Une simple injection gaussienne
non représentative ne résoudrait pas le défaut de domaine observé.

Mémoires canoniques :

- [Inventaire, séquences et couverture du pilote](../raw/neural-corpus-audit-v001-20261101T223414Z.json).
- [Criblage de cinq sources à faible ISO](../raw/neural-low-noise-source-screen-v001-20261101T223744Z.json).
- [Descripteurs parents des captures et partitions](../raw/neural-reserved-photo-pilot-v001-20261101T220513Z.json).

Audit principal en environ 53,4 s ; six tests de classification/protection
et trois de sélection bornée passent. Les aperçus ne sont pas développés
en RAW ; aucun nouvel entraînement, aucune redistribution des partitions.
Tous les travaux et données restent locaux, sans publication.

**Conclusion actuelle : des données utiles existent ; pas de nouvelles
photos demandées avant leur qualification.** Ordre proposé :

1. Qualifier la rafale ISO4000 dans le signal caméra signé, en séparant les
   prises sources de celles servant à la moyenne de référence. Vérifier
   stabilité locale et bruit commun ; une moyenne n'est pas une vérité absolue.
2. Qualifier les régions des cinq sources ISO100 et les profils de bruit
   disponibles pour étudier un entraînement plus divers et mieux conditionné,
   surtout au faible bruit. Ne pas les déclarer propres depuis leur seul ISO.
3. Maintenir la composition de contrôle hors du train, et prévoir une
   future qualification indépendante sans chercher la force ou durée qui
   passe sur cette scène déjà consultée.

Aucun de ces candidats n'est automatiquement autorisé ou qualifié pour
l'apprentissage par le seul inventaire. Aucun changement moteur, aucune
suppression ou copie de RAW, aucun calcul actif à la clôture de l'audit.

## 1er novembre 2026, 22:49 UTC — qualification de la rafale ISO4000 lancée

Campagne `neural-same-iso-burst-v001-20261101T224914Z`, banc local
`47f7bf9`, après 33 tests ciblés réussis. Les 42 RAW `6V1A9819` à
`6V1A9860` sont lus dans le signal caméra signé d'IPR, sans optique ni
débruitage. Le premier développement a confirmé la calibration requise.

La répartition est figée avant mesure : positions chronologiques impaires
pour les 21 prises de référence, positions paires pour les 21 sources.
Aucune source ne participe à sa cible. Toutes les prises conservent leur
ISO4000 réel ; aucun gain ou décalage radiométrique n'est ajusté. Les
contrôles existants de recouvrement, réciprocité, stabilité temporelle et
cohérence locale sont maintenus, avec au moins huit références recalées.

La dispersion temporelle et la différence des deux demi-moyennes seront
consignées sur les pixels valides. L'incertitude de moyenne calculée en
`1/sqrt(N)` est conditionnelle à l'indépendance des prises : elle ne mesure
pas le bruit fixe commun et ne transforme pas la moyenne en vérité propre.

**État courant : calcul en cours, aucun résultat d'admission acquis.**
Il s'agit uniquement de qualifier des paires candidates d'apprentissage ;
aucun entraînement, changement de partition du contrôle, changement moteur
ou publication. La campagne conservera sa mémoire JSON sous
`docs/validation/raw/` à sa clôture, y compris en cas d'échec.

## 2 novembre 2026 — résultats de la qualification ISO4000

La campagne précédente s'est terminée normalement en **328,91 s** :
42 développements caméra signés, calibration appliquée sur chacun.
Les 50 vérifications d'empreintes du gel, des outils, dépendances,
descripteurs et pixels passent ; la mémoire canonique est identique au
résultat local. Aucun ajustement radiométrique n'a été appliqué.

- Les 21 références passent le recalage global.
- 18 des 21 sources passent ; `6V1A9852`, `6V1A9854` et `6V1A9856`
  échouent à la réciprocité : respectivement 1,012, 1,445 et 1,028 pixel,
  pour un plafond préalablement fixé à 1 pixel. Leurs corrélations sont
  pourtant supérieures à 0,988 et leur recouvrement supérieur à 97 %.
  Aucun seuil n'est assoupli après observation.
- **152 paires de patches 96 × 96** sont admises : 52 dans la zone 0,
  54 dans la zone 3 et 46 dans la zone 5. Ces trois zones conservent
  respectivement 67,7 %, 69,3 % et 64,3 % de support valide de référence.
  Les autres zones ne fournissent pas de patches admis ; une bonne
  corrélation globale ne suffit donc pas à qualifier toute la photo.

Les références sont des moyennes de 21 prises, avec des demi-moyennes
de 11 et 10 prises. Sur les trois zones utilisées, l'écart des demi-moyennes
est voisin de deux fois l'incertitude théorique de la moyenne complète,
ce qui est compatible avec l'échelle attendue sous indépendance. Cela
**ne prouve ni l'indépendance du bruit, ni l'absence de bruit fixe commun**.
Le masque de stabilité sélectionne aussi des régions moins variables.

Le statut extérieur `candidate_training_pairs_available` indique seulement
la disponibilité de candidats de train. Le statut interne
`insufficient_qualified_pairs_no_training` est attendu : cette campagne
isolée ne contient ni validation ni test, et ne satisfait donc pas la
barrière d'un pilote complet. Aucun `dataset.npz` d'entraînement complet
ni nouveau modèle n'est produit ; captures, masques reconstructibles,
identités et décisions d'admission sont conservés.

Mémoire canonique : [qualification de la rafale ISO4000](../raw/neural-same-iso-burst-v001-20261101T224914Z.json).

**Conclusion actuelle : ajout exploitable pour un train exploratoire,
pas validation du U-Net.** Les 152 patches proviennent de seulement
18 sources très corrélées d'une même composition ; ils ne constituent
pas 152 scènes indépendantes. Le contrôle existant reste hors du train.
La prochaine étape proposée est de qualifier les cinq supports ISO100
et le bruit résiduel, avant de figer un nouveau pilote combinant les
compositions d'apprentissage. Aucune nouvelle photo demandée à ce stade,
aucun changement en production ou publication, aucun calcul actif.

## 2 novembre 2026, 09:55 UTC — diagnostic caméra des cinq supports ISO100 lancé

Campagne `neural-low-iso-supports-v001-20261102T095553Z`, banc
`41a5a30`, cinq tests ciblés réussis. La sélection reste celle du criblage
précédent : `9072`, `9938`, `0005`, `0145` d'août et `0282` d'août,
avec leurs chemins et empreintes complets, sans confusion avec les
homonymes du contrôle de novembre.

Les développements signés fournissent neuf zones fixes, chacune examinée
par neuf patches 96 × 96 : quantiles du signal, valeurs négatives,
support non saturé, proxy Haar-MAD, résidu passe-haut et gradients
passe-bas. Le support est utilisable au sens limité de ce diagnostic
si au moins 90 % de ses pixels restent sous 0,9 dans chaque canal caméra.
Ce critère n'est pas une certification de netteté ou de cible propre.
Aucun seuil sur le proxy de bruit ne sélectionne les régions.

**Limite principale : aucune prise appariée ne permet ici de séparer
physiquement bruit et texture.** Les mesures caractérisent leur mélange ;
elles ne suffisent pas à calibrer un bruit synthétique de capteur. Aucun
modèle de bruit ajouté, apprentissage ou affectation de partition n'est
lancé. Les valeurs signées restent inchangées et les sources sont lues
sans modification. Résultats en cours, à consigner en JSON à la clôture.

### Résultats de cette campagne — clôture le 2 novembre 2026

Les cinq développements se sont terminés en **39,42 s**, calibration
appliquée sur chacun ; mémoire canonique identique au résultat local.
Les 405 patches fixes ont été mesurés, dont **393** satisfont le seul
critère de support non saturé. Aucun patch n'est exclu parce que son
proxy de bruit est élevé.

| Source ISO100 | Supports non saturés / 81 | Médiane Haar-MAD R / G / B sur les 81 patches |
| --- | ---: | --- |
| 6V1A9072 | 81 | 0,003429 / 0,003228 / 0,003159 |
| 6V1A9938 | 69 | 0,001440 / 0,001434 / 0,001489 |
| 6V1A0005 | 81 | 0,000898 / 0,000884 / 0,000910 |
| 6V1A0145, août | 81 | 0,000467 / 0,000461 / 0,000471 |
| 6V1A0282, août | 81 | 0,001595 / 0,001548 / 0,001607 |

Unités : RGB caméra balancé signé normalisé, avant LCP/DCP. Ces médianes
incluent aussi les patches refusés au critère de support pour ne pas
masquer la sélection. Les douze refus de `9938` ne certifient pas un
écrêtage du capteur : le seuil porte sur le signal caméra balancé.
L'écart de proxy de plus de sept fois entre `9072` et `0145` n'est pas
un rapport démontré de bruit physique ; contenu, exposition, texture et
corrélations du développement peuvent tous contribuer.

Mémoire canonique : [diagnostic des supports ISO100](../raw/neural-low-iso-supports-v001-20261102T095553Z.json).

**Conclusion actuelle : supports de contenu disponibles, aucune nouvelle
cible propre qualifiée.** Ne pas injecter un bruit arbitraire en prenant
ces images pour une vérité sans bruit. Pour avancer avec des données
réellement appariées, la prochaine proposition est un pilote exploratoire
combinant le train déjà qualifié et les 152 paires de la rafale ISO4000,
avec durée fixée avant mesure et contrôles par ISO. Le contrôle existant
reste hors du train mais, déjà consulté, ne constitue plus un test final
indépendant. Les supports ISO100 attendent un modèle de bruit qualifié
ou des références appariées avant leur utilisation comme cibles.
Pas d'entraînement lancé, pas de modification moteur ou publication ;
aucun calcul actif à cette clôture.

## 2 novembre 2026, 10:02 UTC — pilote enrichi lancé

Campagne `neural-enriched-photo-pilot-v001-20261102T100228Z`, banc
`97b30b8`, 23 tests ciblés réussis dont un entraînement synthétique de
contrat. Les 342 patches du train initial sont combinés aux 152 paires
ISO4000 ; le contrôle demeure constitué des mêmes 186 patches.
L'assemblage exige les identités exactes de chaque source et cible des
deux campagnes parentes. Si elles changent, aucun entraînement ne démarre.

La radiométrie de chaque composition reste celle de sa qualification :
ajustement global par canal pour les paires inter-ISO existantes, identité
sans fit pour la rafale ISO4000. Aucun seuil n'est modifié. Les sources
ISO100 isolées du dernier diagnostic ne sont pas utilisées comme cibles.

Budget figé : **2 048 étapes**, mêmes largeur, graine, échelle, optimiseur
et taille de lot que le pilote précédent ; un seul état final, sans
sélection sur le contrôle. Deux compositions au train, une composition de
contrôle hors train, mais déjà examinée : ce n'est pas un test indépendant
final. L'effet de l'ajout de données reste à mesurer, notamment par ISO et
sur les détails/couleurs ; un gain MSE seul ne suffira pas à qualifier le
rendu. Aucun changement de production ou publication. Calcul en cours.

Les prises utiles pour séparer les variations temporelles du détail réel
sont décrites dans le [complément de capture du protocole bruit](../protocols/protocole-bruit.md).

### Résultats du pilote enrichi — même journée

Le calcul s'est terminé normalement en **59,73 s**, dont **35,17 s**
d'entraînement CPU. Toutes les identités des sources et cibles restent
identiques aux qualifications parentes : 494 patches au train, 186 au
contrôle. Pas de cible ISO100 isolée, pas de bruit synthétique ajouté.

| Ensemble | Rapport MSE sortie / entrée |
| --- | ---: |
| Train, ISO4000/12800/51200 | 0,17469 |
| Contrôle complet | 0,66333 |
| Contrôle ISO1600, 54 patches | 0,87290 |
| Contrôle ISO3200, 60 patches | 0,64955 |
| Contrôle ISO6400, 72 patches | 0,40225 |

Les ratios par ISO sont pondérés par le nombre de valeurs valides, comme
l'agrégat global. La MSE au contrôle baisse donc de **33,67 %** par rapport
à l'entrée bruitée, et baisse aux trois ISO. Le pire patch de contrôle a
toutefois un ratio de **1,17433** : l'amélioration n'est pas uniforme.
Le précédent transfert à 2048 étapes avait un ratio global de 1,31050 ;
la nouvelle expérience donne une meilleure direction, mais ne démontre
pas à elle seule pourquoi ni un comportement général sur d'autres scènes.

Mémoire canonique : [pilote enrichi](../raw/neural-enriched-photo-pilot-v001-20261102T100228Z.json).

**Conclusion actuelle : première amélioration exploratoire encourageante
sur ce contrôle, non validée visuellement ou en production.** Le contrôle
a déjà été consulté et sa référence moyenne reste bruitée. La MSE ne
distingue pas à elle seule un débruitage fidèle d'un lissage destructeur.
Prochaine épreuve : rejouer les mesures de structure faible et forte,
de chroma et de biais sur les mêmes pixels, avec les seuils de diagnostic
existants, sans réentraînement ni choix du meilleur état. Les nouvelles
prises du protocole pourront ensuite fournir des scènes indépendantes.
Aucun calcul actif, aucune intégration ou publication.
