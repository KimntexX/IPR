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

## 2 novembre 2026, 10:09 UTC — détails, chroma et biais du pilote enrichi

Campagne `neural-enriched-transfer-v001-20261102T100913Z`, banc
`b698294`, douze tests ciblés réussis ; évaluation terminée en **1,41 s**.
Un seul état final à 2048 étapes, force100, sans optimiseur ou recherche
de force. Les 186 identités de contrôle restent celles du transfert
précédent. Les seuils de structures issus du train initial
`[0,00074636034 ; 0,00344116474]` sont réutilisés sans recalcul sur le
nouveau train ; code des métriques, normalisation et architecture identiques.

| Mesure au contrôle | Entrée | Ancien modèle 2048 | Pilote enrichi 2048 |
| --- | ---: | ---: | ---: |
| Rapport MSE RGB, patch entier | 1 | 1,31050 | 0,66333 |
| Rapport MSE RGB, cœur excluant 24 px de bord | 1 | 1,20773 | 0,56166 |
| Rapport erreur opposée R−G/B−G, patch entier | 1 | 6,74003 | 1,32049 |
| Rapport erreur opposée R−G/B−G, cœur | 1 | 5,94242 | 0,87791 |
| Gain de projection des gradients verts faibles | 0,25944 | 0,08379 | 0,10061 |
| Gain de projection des gradients verts forts | 0,72309 | 0,32687 | 0,37234 |

L'erreur opposée est une erreur de signal caméra par rapport à la moyenne
de référence, pas une mesure perceptuelle de couleur ou de bruit chroma
pur. Le résultat s'améliore fortement par rapport à l'ancien modèle, mais
reste **32,05 % au-dessus de l'entrée** pour cette erreur sur les patches
entiers, contre une baisse de 12,21 % au cœur. La partie hors cœur donne
un ratio de 1,47267. Cela motive une vérification du contexte d'inférence ;
ce n'est pas une preuve que le padding explique tout. À ISO1600, le ratio
opposé reste 1,36812 **même au cœur** (2,21238 sur le patch entier).
ISO3200 : 0,74660 au cœur ; ISO6400 : 0,51016.

Les gains de projection augmentent par rapport à l'ancien modèle mais
restent inférieurs à ceux de l'entrée : la réduction du bruit mesurée
s'accompagne encore d'une atténuation du signal corrélé aux détails de
référence. Ces valeurs ne sont pas des pourcentages absolus de détails
préservés : bruit résiduel de la moyenne et recalage affectent la projection.
La baisse de l'erreur des gradients ne suffit pas à lever cette réserve.

Le biais moyen sortie moins référence vaut en RGB caméra signé normalisé
`[-0,001639 ; -0,004801 ; -0,000838]` sur les patches entiers ; au cœur,
`[-0,001046 ; -0,004396 ; -0,001679]`. Le canal vert est le plus
négativement décalé dans cette mesure. Aucun pourcentage de luminosité
affichée ni diagnostic de dominante après DCP n'en est déduit ici.

Mémoire canonique : [contrôle des détails et couleurs du pilote enrichi](../raw/neural-enriched-transfer-v001-20261102T100913Z.json).

**Conclusion actuelle : amélioration de la MSE sur ce contrôle, mais
candidat non qualifié pour la production.** Défauts restants : atténuation
des détails, biais par canal et erreur opposée à faible ISO, partiellement
renforcée près des bords. Prochaine épreuve proposée : inférer le même
modèle sur des zones plus larges, puis mesurer les mêmes pixels centraux
avec le même conditionnement sigma, sans changer force, poids, masques ou
référence. Cette ablation séparera un éventuel manque de contexte d'un
défaut appris ; elle ne remplace pas un test sur des scènes indépendantes.
Aucun réentraînement, nouvelle photo nécessaire à ce diagnostic,
changement moteur ou publication ; aucun calcul actif.

## 2 novembre 2026, 10:19 UTC — ablation du contexte d'inférence 96/384

Campagne `neural-context-ablation-v001-20261102T101919Z`, banc
`d0e91df`, **17 tests ciblés réussis**, mesure terminée en **4,04 s**.
Les 186 patches de contrôle sont comparés par deux calculs du même modèle :
inférence isolée 96 × 96, puis inférence de leur zone caméra 384 × 384
avec recadrage aux mêmes pixels. Les 33 contextes sont relus avec leurs
empreintes de descripteur et de pixels ; les sources recadrées sont
exactement identiques aux tenseurs du pilote. Chaque groupe conserve
le même sigma par canal, sans réestimation sur le contexte agrandi.

Poids, force100, échelle, masques, cibles et seuils de détail sont inchangés.
Les origines sont multiples de quatre, préservant la phase des deux
sous-échantillonnages du réseau. La branche96 rejoue les métriques
précédentes aux tolérances numériques fixées ; aucun réentraînement.

| Mesure, mêmes pixels | Entrée | Inférence 96 | Contexte 384 |
| --- | ---: | ---: | ---: |
| Rapport MSE RGB, patch entier | 1 | 0,66333 | 0,57809 |
| Rapport erreur opposée R−G/B−G, patch entier | 1 | 1,32049 | 0,91097 |
| Rapport MSE RGB, cœur24 | 1 | 0,56166 | 0,56166 |
| Rapport erreur opposée R−G/B−G, cœur24 | 1 | 0,87791 | 0,87791 |
| Projection gradients faibles, cœur | 0,25944 | 0,10061 | 0,10061 |
| Projection gradients forts, cœur | 0,72309 | 0,37234 | 0,37234 |

Le contexte réduit donc la MSE globale de **42,19 % par rapport à
l'entrée**, au lieu de 33,67 %. L'erreur opposée globale passe d'une
hausse à une baisse de **8,90 %**. Les valeurs du cœur et des projections
restent identiques à l'arrondi ; cela soutient un effet du contexte sur
la périphérie, pas une récupération des structures centrales atténuées.
Les zones384 gardent elles-mêmes leurs frontières ; certains patches
n'ont que 24 pixels de marge externe. Ce test ne prouve pas une absence
d'effets de bord pour tout découpage ou toute taille d'image.

| ISO de contrôle | MSE RGB contexte / entrée | Erreur opposée contexte / entrée |
| --- | ---: | ---: |
| 1600 | 0,74727 | 1,44128 |
| 3200 | 0,57910 | 0,74961 |
| 6400 | 0,35957 | 0,52701 |

Le défaut opposé à ISO1600 persiste : +44,13 % sur les mêmes patches
avec contexte, +36,81 % au cœur. Il ne peut pas être attribué uniquement
aux frontières des petites entrées. Le biais RGB global avec contexte
vaut `[-0,000906 ; -0,004330 ; -0,001736]` en unités caméra signées,
sans traduction en dominante perceptuelle ou luminosité affichée.

Mémoire canonique : [ablation du contexte 96/384](../raw/neural-context-ablation-v001-20261102T101919Z.json).

**Conclusion actuelle : le contexte d'inférence compte, mais ne suffit
pas à qualifier le modèle.** Un futur rendu tuilé devra être qualifié
avec contexte et recadrage plutôt qu'en juxtaposant des sorties96.
L'atténuation mesurée des structures centrales et le défaut faible ISO
restent ouverts ; aucune intégration de ce prototype en production.
La prochaine vérification proposée est de comparer les mêmes diagnostics
au cœur sur le train et sur le contrôle, sans changer les poids. Cela
aidera à distinguer un défaut déjà appris sur les références du train
d'un défaut surtout lié au transfert vers le contrôle, avant de changer
l'objectif d'apprentissage. Aucun calcul actif, aucune nouvelle photo
nécessaire à ce diagnostic, aucune publication ou modification moteur.

## 2 novembre 2026, 10:23 UTC — comparaison apprentissage / contrôle

Campagne `neural-train-control-v001-20261102T102305Z`, banc `3acb5f1`,
**17 tests ciblés réussis**, diagnostic terminé en **5,92 s**. Même état
final du pilote enrichi, force100, sigma, seuils de structure historiques
et inférence96 ; mesures au cœur24 pour limiter l'effet de bord identifié.
Les 680 identités de pixels sont vérifiées. Le contrôle rejoue les mêmes
ratios RGB et opposés que la campagne précédente, sans tolérance assouplie.
Aucun entraînement ou changement de recette.

| Composition et rôle | Patches / RAW sources | Rapport MSE RGB cœur | Rapport erreur opposée cœur |
| --- | ---: | ---: | ---: |
| Intérieur initial, train ISO12800/51200 | 342 / 6 | 0,15596 | 0,26342 |
| Rafale ISO4000, train | 152 / 18 | 0,41433 | 0,52494 |
| Intérieur de contrôle, ISO1600/3200/6400 | 186 / 9 | 0,56166 | 0,87791 |

Les résultats du train sont des mesures sur les images apprises, pas une
validation indépendante. Scène, ISO, structures et radiométrie diffèrent
entre groupes ; leurs écarts ne permettent pas une attribution causale
unique à la capacité du réseau ou à sa généralisation.

### L'atténuation existe déjà au train

Sur l'intérieur initial, la projection des gradients verts faibles passe
de **0,66464 à 0,21819**, et celle des forts de **0,87493 à 0,53970**.
Le contrôle donne respectivement **0,25944 à 0,10061** et **0,72309 à
0,37234**. Les seuils sont communs, issus du train initial. Ces observations
écartent l'explication « uniquement un défaut sur scène inconnue » :
l'atténuation du signal corrélé à la référence existe aussi sur le train.
Elles ne quantifient toujours pas un pourcentage absolu de détail réel
perdu, car les références et le recalage restent imparfaits.

La rafale ISO4000 présente **zéro composante de gradient forte** et
seulement **492 composantes faibles**, contre 686250 composantes dans la
bande basse, sur le support retenu. Cela ne décrit pas toute la photo,
mais uniquement ses régions admises. Les 152 paires enrichissent donc
surtout les contenus peu structurés ; elles ne suffisent pas à enseigner
la conservation de détails forts à bruit plus faible. Ne pas réviser les
seuils après coup pour faire apparaître artificiellement une couverture.

### Biais moyen distinct d'un simple défaut de référence inter-ISO

Le diagnostic sépare entrée moins référence, sortie moins référence et
déplacement sortie moins entrée, canal par canal. Sur le cœur de la rafale
ISO4000, l'entrée moins référence est presque centrée :
`[-0,0000254 ; -0,0000133 ; +0,0000257]`. Le modèle déplace la moyenne de
`[-0,0008859 ; -0,0029199 ; -0,0021920]`, donnant une sortie moins référence
`[-0,0009113 ; -0,0029332 ; -0,0021663]`, en unités caméra signées.
Cette série est à ISO constant, sans fit radiométrique : le défaut ne
peut donc pas être attribué entièrement aux ajustements inter-ISO.

Le déplacement moyen vert vaut aussi **−0,0036223** sur le train intérieur
et **−0,0042379** sur le contrôle. Cela documente un comportement commun
du modèle sur ces groupes, pas sa cause unique ni un pourcentage de
luminosité à l'affichage. À ISO1600, l'erreur opposée au cœur reste 1,36812,
contre 0,74660 à ISO3200 et 0,51016 à ISO6400.

Mémoire canonique : [diagnostic train / contrôle](../raw/neural-train-control-v001-20261102T102305Z.json).

**Conclusion actuelle : il faut traiter un défaut déjà présent dans
l'apprentissage, pas seulement multiplier les images de contrôle.**
Le contexte périphérique est un problème séparé ; les priorités restantes
sont le centrage des corrections par canal, puis la protection des
structures, avec des données appariées couvrant effectivement ces structures.
La prochaine proposition est une expérience isolée de contrainte du biais
moyen par canal, budget et poids de perte figés avant mesure, sans cumuler
immédiatement une nouvelle perte de gradients ni ajuster sur le contrôle.
Le protocole de nouvelles scènes répétées pourra compléter la qualification
de la généralisation, mais aucune reprise des photos existantes n'est
requise pour consigner ce diagnostic. Aucun calcul actif, aucune intégration
en production ou publication.

## 2 novembre 2026, 10:28 UTC — expérience isolée de pénalisation du biais

Campagne `neural-bias-penalty-v001-20261102T102832Z`, banc `fb2b11a`,
dix tests ciblés réussis. Après accord de l'utilisateur, un seul candidat
est entraîné, même architecture, graine, lot, Adam, taux d'apprentissage,
échelle et budget de 2048 étapes que le pilote enrichi. Les tenseurs du
corpus sont copiés à l'identique pour protéger le parent ; aucune nouvelle
photo, requalification de paire ou modification des masques.

Seul changement d'objectif :

```text
L = MSE masquée + 1 × moyenne_{patch,canal}(moyenne_{pixels valides du cœur24}(sortie − référence)²)
```

La moyenne est calculée séparément pour chaque patch et canal, avant
élévation au carré, pour empêcher la compensation entre patches de biais
opposés. Le poids 1 est fixé avant mesure : une contribution supplémentaire
de même unité, sans recherche du meilleur coefficient. Ce n'est ni une
correction imposée après inférence, ni une garantie de moyenne nulle.
La perte utilise les mêmes cibles signées normalisées que la MSE et ne
reçoit aucune image de contrôle durant l'optimisation.

La perte MSE sans pénalité demeure strictement identique dans le test
synthétique. Aucune perte de gradients n'est ajoutée simultanément.
Le diagnostic final rejouera les mesures au cœur par composition et ISO,
avec les seuils historiques, sans sélection de checkpoint sur le contrôle.
**Calcul lancé, résultat non acquis** ; aucun changement de production,
publication ou tag. Un gain de biais ne suffira pas si l'erreur couleur
ou la préservation des structures se dégrade.

### Résultats — variante de poids 1 non retenue

Calcul terminé normalement en **52,51 s**, dont **42,56 s**
d'entraînement. Les mêmes tenseurs sont utilisés et le corpus parent
reste inchangé. Les contrôles d'identité et des outils passent ; aucune
recherche de coefficient ou de checkpoint supplémentaire.

| Mesure au cœur24 | Pilote enrichi | Avec pénalité de biais |
| --- | ---: | ---: |
| MSE train intérieur / entrée | 0,15596 | 0,22456 |
| MSE train ISO4000 / entrée | 0,41433 | 0,65765 |
| MSE contrôle / entrée | 0,56166 | 0,57598 |
| Erreur opposée contrôle / entrée | 0,87791 | 0,87231 |
| Erreur opposée contrôle ISO1600 / entrée | 1,36812 | 1,07473 |
| Projection gradients faibles du contrôle | 0,10061 | 0,12305 |
| Projection gradients forts du contrôle | 0,37234 | 0,41863 |

La projection des détails s'améliore sans rejoindre celle de l'entrée,
mais la MSE RGB se dégrade sur les deux compositions du train et sur
l'agrégat de contrôle. L'erreur opposée à ISO1600 s'améliore nettement,
tout en restant au-dessus de l'entrée ; à ISO6400 elle passe au contraire
de 0,51016 à 0,72152. Une moyenne globale seule masque ces compromis.

La grandeur directement visée par la nouvelle perte doit aussi être
vérifiée, sans la confondre avec le biais global pouvant se compenser.
En unités caméra non normalisées, la moyenne sur les patches et canaux
du carré de l'erreur moyenne du cœur vaut :

| Composition | Pilote enrichi | Avec pénalité |
| --- | ---: | ---: |
| Train intérieur | 0,0000172340 | 0,0000195531 |
| Train ISO4000 | 0,0000049920 | 0,0000078239 |
| Contrôle | 0,0000099688 | 0,0000076224 |

Ces valeurs se recalculent depuis les sommes de biais et les nombres de
pixels par patch du JSON ; elles sont quatre fois les pénalités moyennes
correspondantes dans l'espace d'entraînement d'échelle2. Le candidat
réduit cette grandeur sur le contrôle mais pas sur le train. Le biais
global vert du contrôle s'améliore de −0,004396 à −0,002297 tandis que
celui du bleu passe de −0,001679 à −0,003049 : pas une correction uniforme.

Mémoire canonique : [expérience de pénalisation du biais](../raw/neural-bias-penalty-v001-20261102T102832Z.json).

**Conclusion actuelle : candidat non retenu ; le pilote enrichi reste le
point de comparaison expérimental, lui-même non qualifié en production.**
L'essai réfute l'idée qu'ajouter ce terme de poids1 suffirait, à budget
inchangé, à corriger le défaut de moyenne sans contrepartie. Il ne réfute
pas toute régularisation possible : convergence, compromis de pertes,
variabilité des lots et références peuvent intervenir. Ne pas poursuivre
une recherche de poids guidée par le contrôle déjà consulté. Avant de
cumuler une perte de gradients, la suite doit examiner la stabilité de
l'optimisation sur le train seul et l'équilibrage des contenus ; les
nouvelles scènes répétées du protocole restent utiles pour une future
évaluation indépendante. Aucun calcul actif ni intégration ou publication.

## 2 novembre 2026, 10:37 UTC — vérification de stabilité sur le train seul lancée

Campagne `neural-training-stability-v001-20261102T103741Z`, banc
`d495b5b`, neuf tests ciblés réussis. Deux rejeux des recettes déjà figées
sont lancés : MSE seule et MSE avec pénalité de moyenne de poids1. Chaque
rejeu conserve graine, lots, architecture, optimiseur et budget2048. Aucun
poids candidat n'est publié ni choisi ; les tenseurs finaux doivent
reproduire exactement leurs modèles parents. Un écart rend le rejeu non
conforme au lieu d'être interprété comme un nouvel essai photographique.

Les pertes de petits lots ne mesurent pas à elles seules la convergence.
Des diagnostics sur **les 494 patches du train** sont donc fixés aux
étapes `0, 512, 1024, 1536, 1792, 1920, 2048`. La fenêtre terminale
`1792/1920/2048` est déclarée avant mesure. Aucun pixel du contrôle n'est
évalué dans cette campagne. Les sondes ne doivent consommer aucun tirage
aléatoire ni modifier les paramètres ; les deux recettes doivent recevoir
exactement la même séquence de lots. Il n'y a ni continuation adaptative,
ni sélection d'étape selon les résultats.

L'audit de contenu réutilise les preuves du train : la composition initiale
représente **69,23 % des patches et 99,13 % de la somme des erreurs RGB
quadratiques initiales**, contre **30,77 % / 0,87 %** pour la rafaleISO4000.
Ce contraste documente une forte disparité d'échelle d'erreur, pas une
mesure directe de contribution aux gradients ou un profil physique du
bruit. La répartition des structures et des fichiers sources est conservée.
Le biais moyen par patch/canal est suivi séparément de la MSE et de la
moyenne globale, pour ne pas masquer les compensations entre patches.

**Calcul en cours ; convergence non déclarée.** La variabilité sur trois
sondes finales sera descriptive, pas une preuve d'optimum global. L'objectif
agrégé sur tout le train n'est pas exactement l'espérance des pertes des
lots à masques de tailles variables. Aucun réglage n'est choisi à partir
du contrôle ; aucune modification moteur, publication ou nouveau corpus.

### Résultats de l'audit de stabilité — lecture du 2 novembre 2026

Les deux rejeux sont terminés normalement en **86,86 s**. Les poids finaux
reproduisent **exactement** les tenseurs des modèles parents, avec les
mêmes lots tirés dans le même ordre. Les sondes n'ont consommé aucun
tirage du générateur d'apprentissage. Le diagnostic ne met donc pas en
évidence une non-reproductibilité de ces deux entraînements. Il ne
qualifie pas, à lui seul, le dématriçage ou la généralisation.

| Étape fixe | Objectif train MSE seule | Objectif train MSE + biais |
| --- | ---: | ---: |
| 0 | 0,000137587 | 0,000140595 |
| 512 | 0,000066065 | 0,000140424 |
| 1024 | 0,000047295 | 0,000068218 |
| 1536 | 0,000030071 | 0,000049166 |
| 1792 | 0,000026452 | 0,000041142 |
| 1920 | 0,000023444 | 0,000034894 |
| 2048 | 0,000024035 | 0,000037326 |

Unités normalisées d'échelle2. Les colonnes n'ont pas le même objectif :
la seconde inclut le terme de biais. Leur écart brut ne doit pas servir
à classer les recettes ; la MSE caméra finale commune vaut respectivement
0,000096141 et 0,000133361. Sur la fenêtre terminale fixée, l'étendue
divisée par la moyenne vaut **12,21 %** et **16,54 %**. L'objectif à2048
est environ 9,14 % et 9,28 % sous celui à1792, mais remonte par rapport
à1920. Il n'y a pas de plateau de convergence établi ; aucune étape n'est
sélectionnée rétroactivement. Le rejet de la pénalité reste un verdict
au budget fixé, pas une preuve qu'elle échouerait à toute optimisation.

Le déséquilibre de contenu/échelle est confirmé : la composition initiale
concentre 99,13 % de la somme des erreurs initiales et toutes les
composantes fortes du train aux seuils historiques ; la rafale4000 apporte
surtout des zones peu structurées. Cela ne mesure pas directement les
gradients des paramètres et ne démontre pas une cause unique des défauts.

Mémoire canonique : [rejeux exacts et stabilité du train](../raw/neural-training-stability-v001-20261102T103741Z.json).

**Conclusion actuelle : calcul reproductible, convergence et équilibre
des contenus encore à qualifier.** L'utilisateur confirme le maintien
d'Adam : pas de recherche d'optimiseur à ce stade. L'étude d'un bloc
conjoint dématriçage/débruitage est une direction pertinente à comparer,
pas une intégration autorisée ni une preuve que RCD cause les défauts du
pilote. Un dématriçage transforme le bruit déjà présent et peut introduire
des erreurs de reconstruction ; les biais appris identifiés ici doivent
en rester distingués. Aucun calcul actif, changement moteur ou publication.

## 2 novembre 2026, 11:33 UTC — entrée mosaïque pour le futur modèle conjoint

Campagne `neural-mosaic-inlet-v001-20261102T113314Z`, banc `76d098a`,
terminée en **8,17 s**. Cinq tests Python passent, ainsi que seize contrôles
C++ de recadrage et origine CFA sur les huit orientations, avec origines
paires et impaires. Le banc compile dans le build neural dédié ; aucun
fichier `src/` n'est modifié et les sources moteur ne diffèrent pas des
révisions des captures parentes.

### Capture qualifiée, pas encore un réseau conjoint

Les anciens tenseurs du pilote étaient des RGB après RCD/médiane, pas
des mosaïques. Les RAW conservés permettent maintenant une extraction
supplémentaire en réutilisant les fonctions exactes de préparation d'IPR
dans un exécutable de test, selon le mécanisme déjà employé par
`tonal_base_probe`. Aucune copie du code LibRaw ni nouvelle API moteur.
La calibration blanche est celle de chaque capture de production figée ;
ce test ne requalifie pas la sélection automatique des calibrations.

Deux étapes sont capturées sur les mêmes neuf régions384 :

- CFA signée normalisée, avant gains de balance des blancs et borne haute ;
- CFA signée balancée reçue par RCD, qui comporte déjà sa borne haute à1.

Les niveaux noirs, diviseur, quatre gains, origine capteur, orientation et
motif CFA sont conservés. Le second vert garde son identité ; ne pas
supposer un ordre RGGB. Le rangement réversible produit quatre plans
192 × 192 correspondant aux positions haut-gauche/haut-droite/bas-gauche/
bas-droite. L'aller-retour est exact et n'interpole aucune valeur.

| Source, côté apprentissage | ISO | Orientation LibRaw | Régions RGB reproduites bit à bit |
| --- | ---: | ---: | ---: |
| 6V1A0134 | 100 | 0 | 9 / 9 |
| 6V1A0181 | 12800 | 0 | 9 / 9 |
| 6V1A9819 | 4000 | 5 | 9 / 9 |

Les 27 régions RGB reconstruites reproduisent les pixels des captures de
production figées, avec le même RCD/médiane signé. Les 54 régions CFA
passent le rangement aller-retour ; la relation gains/balance/borne haute
est vérifiée avec un écart maximum de **5,96×10⁻⁸**, compatible avec les
arrondis flottants des deux calculs. Les valeurs sous zéro sont conservées :
jusqu'à 10,74 % et 31,89 % dans certains plans/régions à ISO12800 et4000.
Ces maxima ne décrivent pas le pourcentage global de pixels bruités.

Mémoire canonique : [qualification de l'entrée mosaïque signée](../raw/neural-mosaic-inlet-v001-20261102T113314Z.json).

### Qualification des cibles et besoins en photos

**Pas de nouvelles prises nécessaires pour l'entrée et le premier
prototype.** Les RAW des rafales sont conservés et les partitions restent
inchangées. En revanche, leurs moyennes RGB issues de RCD/médiane héritent
de cette reconstruction : elles ne constituent pas une vérité indépendante
pour démontrer un dématriçage meilleur que RCD. Le contrôle bit à bit
ci-dessus valide le raccordement des données, pas la fidélité de RCD.

La prochaine étape proposée est un test de reconstruction sur des signaux
RGB connus, échantillonnés en Bayer, puis un pilote apparié sur les rafales
réelles avec cette limite de cible explicitement conservée. Les détails
absents de l'échantillonnage ne deviennent pas récupérables sans hypothèses.
Garder ensuite des scènes naturelles indépendantes pour la revue visuelle.
Si de nouvelles prises deviennent nécessaires, demander une scène répétée,
immobile, détaillée et éclairée de façon stable selon le protocole déjà
fourni, sans refaire les séries existantes à ce stade.

**Conclusion actuelle : contrat d'entrée Bayer signé qualifié sur trois
R6III seulement ; modèle conjoint non entraîné et non validé.** Adam est
maintenu. Le choix d'une activation par défaut faible ou d'une action
utilisateur reste en attente des retours de l'entourage. Aucun raccordement
de réseau dans l'application, changement d'ouverture, suppression de RAW,
publication, fusion ou tag ; aucun calcul actif à la clôture.

## 2 novembre 2026, 11:44 UTC — premier pilote conjoint sur signaux connus lancé

Campagne `neural-synthetic-joint-v001-20261102T114401Z`, banc `6a4d6e9`,
**dix tests ciblés réussis**. Cette campagne ne lit aucune photographie :
elle vérifie le fonctionnement du nouveau contrat mosaïque/RGB avec une
vérité synthétique entièrement connue, distincte des références RCD.

Le prototype reçoit quatre plans Bayer spatiaux et quatre cartes de sigma,
plus les étiquettes explicites des couleurs des quatre positions. Un petit
U-Net de largeur8 opère sur les plans demi-résolution et produit douze
plans réorganisés en trois canaux RGB pleine résolution. Sa correction
résiduelle s'ajoute à une interpolation bilinéaire fixe, reproduite
exactement à l'initialisation. Aucun poids préentraîné ni code de réseau
tiers n'est repris. Les valeurs signées et supérieures au blanc restent
autorisées ; le domaine est ici une mosaïque balancée **non bornée**, qui
pourra être préparée depuis la capture normalisée avant gain, pas depuis
la seule étape pré-RCD déjà bornée en haut.

Protocole figé :

- 40 signaux d'apprentissage couvrant constantes, rampes, frontières,
  faibles ondulations achromatiques et ondulations chromatiques ;
- 60 cas de contrôle procéduraux, paramètres/germes distincts, cinq
  familles × quatre phases Bayer × trois niveaux de bruit ;
- bruit nul ou gaussien à variance dépendante du signal `a*max(signal,0)+b`,
  coefficients de simulation déclarés, non calibrés sur un Canon ;
- sigma d'entrée calculé depuis l'observation bruitée et les coefficients
  connus de simulation, **pas depuis la vérité propre** ;
- Adam, graine71, largeur8, lot4, taux0,001, échelle2,2048 étapes,
  un seul état final, MSE sur le cœur après exclusion de48 pixels ;
- mesures RGB, erreur opposée, biais et projection des gradients au même
  cœur ; aucune sélection de force, de coefficient ou de checkpoint.

Les tests vérifient toutes les phases, constantes négatives/supérieures
au blanc, reconstruction des rampes dans l'intérieur, non-modification
des entrées, finitude des gradients et générateurs reproductibles.
Un témoin distinct montre aussi que deux RGB différents peuvent produire
la même mosaïque : il est exclu de l'agrégat de qualité et rappelle une
limite d'identifiabilité, pas un défaut corrigible par tout réseau.

**Calcul en cours, modèle conjoint non qualifié sur photos.** Le contrôle
partage les règles de construction du train ; ses graines distinctes ne
prouvent pas une généralisation à des scènes naturelles. La comparaison
porte sur la bilinéaire de ce banc, pas sur RCD, Lightroom ou le pilote
RGB antérieur. Optique, réponse spectrale, bruit fixe, compression et
variantes de capteur ne sont pas reproduits. Aucune nouvelle photo requise
pour cette étape ; aucune modification du moteur ou du comportement IHM.

### Résultats du pilote synthétique — même journée

Le calcul s'est terminé normalement en **30,58 s**. Le réseau comporte
31084 paramètres ; les soixante cas ont été mesurés après les2048 étapes,
sans choix de checkpoint. Le résultat permet de vérifier l'exécution du
contrat, mais **ne qualifie pas la qualité de reconstruction du modèle**.

| Famille, trois niveaux de bruit confondus | Rapport MSE réseau / bilinéaire |
| --- | ---: |
| Constantes | 0,84654 |
| Rampes | 0,82600 |
| Frontières | 1,83628 |
| Faibles ondulations achromatiques | 0,83476 |
| Ondulations chromatiques | 0,87981 |
| Ensemble | **1,33820** |

Les agrégats mélangent reconstruction et bruit : ce ne sont pas des
pourcentages de bruit supprimé. Les contours dégradés pèsent assez pour
que l'erreur globale soit **33,82 % au-dessus de la bilinéaire**. L'erreur
opposée globale donne1,11336 ; la projection des gradients verts passe
de0,83024 pour la bilinéaire à0,72747 pour le réseau face à la vérité
synthétique. Le candidat lisse donc aussi des structures dans ce banc.

Par niveau de bruit, les ratios MSE globaux valent2,20847 sans bruit,
1,68057 au niveau intermédiaire et1,07848 au plus fort niveau simulé.
Sur les constantes bruitées seules, le ratio est0,86069 et0,84157 ;
le débruitage existe, mais il ne compense pas la reconstruction dégradée.
Sans bruit, la bilinéaire est exacte sur les constantes et presque exacte
sur les rampes ; le réseau introduit respectivement une MSE absolue de
0,0000011861 et0,0000011323. Les contours sans bruit passent d'une MSE
de0,00044502 à0,00097625. Le problème n'est donc pas uniquement le retrait
de bruit dans les cas les plus difficiles.

### Correction documentaire et numérique des ratios quasi nuls

La première mémoire v001 conservait un ratio numériquement énorme pour
les rampes sans bruit, parce qu'une erreur d'arrondi non nulle de la
bilinéaire était utilisée comme dénominateur. **Ce ratio n'est pas une
comparaison relative interprétable.** Le calcul des métriques a été corrigé
avec un seuil explicite de MSE de référence de10⁻¹² : sous ce seuil,
le ratio RGB ou opposé vaut `null` et les erreurs absolues sont conservées.
Cette garde ne sélectionne aucun modèle et ne modifie aucun pixel.

La mémoire v002 est une réanalyse versionnée des mêmes soixante prédictions,
pas un nouvel entraînement. Les sommes d'erreurs, biais, projections,
empreintes de prédictions et poids sont inchangés, de même que les ratios
globaux ci-dessus. Onze tests ciblés passent après correction. La mémoire
initiale reste historique, sans écrasement.

- [Pilote initial et provenance de l'apprentissage](../raw/neural-synthetic-joint-v001-20261102T114401Z.json).
- [Mesures corrigées, source courante pour les ratios](../raw/neural-synthetic-joint-v002-20261102T114800Z.json).

**Conclusion actuelle : contrat fonctionnel, premier candidat synthétique
non retenu.** Ne pas passer directement à un apprentissage photographique
avec cette recette. La prochaine proposition est de séparer les phases
d'apprentissage du **même réseau** : qualifier d'abord la reconstruction
sur mosaïques sans bruit, puis étudier l'ajout du débruitage en conservant
ce contrôle de fidélité. Cela ne nécessite pas deux moteurs exécutés en
production. Adam reste fixé ; aucune recherche de coefficients sur les
photos de contrôle. Ce résultat ne réfute pas l'approche conjointe, mais
écarte une bascule prématurée de ce prototype.
Aucune nouvelle photo nécessaire pour cette vérification synthétique ;
pas de nouveau calcul actif, changement moteur, IHM, publication ou tag.

## 2 novembre 2026, 11:53 UTC — phase de reconstruction sans bruit lancée

Campagne `neural-synthetic-reconstruction-v001-20261102T115339Z`, banc
`22bed68`, **15 tests ciblés réussis**. Le même réseau de31084 paramètres
est réinitialisé avec la même graine71 ; ce n'est pas une continuation du
candidat conjoint rejeté. Train de40 signaux, Adam0,001, lot4,2048 étapes,
échelle2 et cœur48 sont conservés. Seul le bruit d'apprentissage est retiré :
mosaïques propres et cartes sigma nulles. Les20 cas de contrôle sont les
cas sans bruit déjà définis dans les mêmes cinq familles/quatre phases.
Leurs règles sont connues et déjà consultées : pas de qualification de
généralisation photographique ni de jeu final indépendant.

Critères exploratoires figés avant le calcul :

- ratio MSE globale réseau/bilinéaire au plus0,98, et erreur opposée
  globale non supérieure à celle de la bilinéaire ;
- contours : erreur RGB et opposée non augmentées, projection du gradient
  au moins95 % de celle de la bilinéaire ;
- constantes et rampes : MSE absolue au plus10⁻⁶ ;
- ondulations faibles/chromatiques : MSE au plus celle de la bilinéaire
  plus10⁻⁶, projection au moins95 % de la bilinéaire ;
- MSE sur les sites réellement observés au plus10⁻⁶ en moyenne des cas.

Ces seuils ne sont pas des tolérances perceptuelles ou physiques qualifiées.
La garde des ratios à MSE de référence quasi nulle reste10⁻¹² ; les erreurs
absolues sont conservées. Toutes les conditions sont nécessaires ; aucune
adaptation au bruit ne sera lancée automatiquement si l'une échoue.
Le résultat sera celui de l'état final, pas du meilleur checkpoint.

**Calcul en cours.** Aucune perte supplémentaire, modification d'architecture,
nouvelle photo, modification moteur ou activation IHM. Cette phase doit
distinguer l'apprentissage de la reconstruction de celui du bruit avant
de cumuler les deux objectifs dans le même réseau.

### Résultats de la reconstruction seule — arrêt avant adaptation au bruit

Calcul terminé normalement en **27,14 s**. Les20 cibles, mosaïques et
cartes sigma sont identiques, par empreinte, au sous-ensemble sans bruit
de l'expérience mixte précédente. Les résultats restent donc comparables
sur ces mêmes entrées, sans nouveau cas sélectionné.

| Mesure sans bruit | Apprentissage mixte précédent | Apprentissage reconstruction seule |
| --- | ---: | ---: |
| MSE globale réseau / bilinéaire | 2,20847 | 1,08016 |
| Erreur opposée globale / bilinéaire | 1,36566 | 1,07588 |
| MSE contours / bilinéaire | 2,19371 | 1,07063 |
| Projection gradients verts globale | 0,73085 | 0,84098 |
| Projection contours | 0,68208 | 0,81319 |

La projection globale de la bilinéaire vaut0,83389, celle des contours
0,80468. Le retrait du bruit à l'apprentissage améliore fortement la
fidélité par rapport au premier essai, mais la MSE reste **8,02 % plus
élevée que la bilinéaire**, l'erreur opposée **7,59 % plus élevée**.
Les contours donnent une MSE de0,00047646 contre0,00044502 pour la
bilinéaire. Un gradient davantage conservé ne suffit pas à qualifier
couleurs et localisation du contour.

Six critères passent et six échouent. Échecs : gain RGB global,
erreur opposée globale, fidélité RGB et opposée des contours, fidélité
absolue des ondulations chromatiques, et fidélité des sites capteur
observés. Les critères ne sont pas modifiés après mesure ;
`decision=stop_before_noise_stage`. Aucune adaptation au bruit lancée.

La MSE moyenne aux valeurs réellement observées vaut **0,0000059614**,
au-delà du plafond préfixé10⁻⁶. Sans bruit simulé, ces valeurs sont des
observations exactes : le réseau n'avait pas besoin de les modifier pour
estimer les deux couleurs manquantes. Cela identifie une faiblesse du
contrat de sortie non contraint, sans prouver que cette faiblesse explique
à elle seule toutes les erreurs sur les composantes manquantes.

Constantes et rampes passent la tolérance absolue déclarée, avec des MSE
de0,0000008732 et0,0000009703 ; elles ne sont pas reconstruites exactement.
Les faibles ondes passent les seuils déclarés, mais leur MSE augmente de
0,0000000396 à0,0000008644 : une tolérance exploratoire respectée ne doit
pas être présentée comme une absence de dégradation. Les ondes chromatiques
passent de0,0000034305 à0,0000052785 et échouent à leur critère de fidélité.

Mémoire canonique : [phase de reconstruction sans bruit](../raw/neural-synthetic-reconstruction-v001-20261102T115339Z.json).

**Conclusion actuelle : progrès du découpage de l'apprentissage, candidat
non qualifié ; arrêt avant débruitage.** La prochaine correction proposée
est d'étudier la cohérence des sorties avec les valeurs CFA exactement
observées lors de la reconstruction sans bruit, au lieu de demander au
réseau de les réapprendre. Une contrainte dure de ce type serait légitime
uniquement dans ce régime sans bruit : dans le régime bruité, les mesures
doivent pouvoir être corrigées. Ne pas étendre cette contrainte sans
qualification à la sortie d'un débruiteur conjoint. Même Adam, mêmes
familles de contrôle déjà consultées ; pas de conclusion photographique.
Aucune nouvelle photo nécessaire à cette correction de principe, aucun
calcul actif, changement moteur, IHM ou publication.

## 2 novembre 2026, 12:01 UTC — cohérence des observations sans bruit lancée

Campagne `neural-synthetic-consistent-reconstruction-v001-20261102T120141Z`,
banc `d212919`, **19 tests ciblés réussis**. Le même réseau est réinitialisé
avec la même graine, les mêmes40 signaux et20 cas de contrôle, Adam0,001,
lot4,2048 étapes, échelle2, cœur48. Les critères de passage précédents
restent strictement inchangés.

Seul changement : dans le régime **explicitement déclaré sans bruit**,
la sortie RGB reprend exactement la composante mesurée à chaque photosite ;
les deux composantes manquantes restent les prédictions du réseau. Cette
projection est appliquée pendant l'apprentissage et l'évaluation. Elle
bloque les gradients de la prédiction sur les valeurs connues, sans
modifier les cibles ni les valeurs des composantes inconnues. Le réseau
conserve ses31084 paramètres. Aucun nouveau coefficient de perte.

La projection refuse les cartes sigma non nulles ; elle ne s'active pas
implicitement parce qu'un estimateur de bruit aurait fourni zéro. Le
contrat demande un choix explicite de régime. La voie bruitée conserve
la possibilité de corriger les composantes mesurées : pas de verrou
sur les RAW photographiques ni changement à l'ouverture d'IPR.

Un témoin contre-factuel est fixé : rejouer l'ancien modèle sans
réentraînement, puis projeter uniquement ses valeurs observées. Ses
couleurs manquantes ne changent pas. Ce témoin distinguera le gain mécanique
de la projection du gain éventuel d'un apprentissage contraint. Les
empreintes des entrées et le rejeu numérique de l'ancien modèle sont
vérifiés, sans sélection selon le contrôle.

**Calcul en cours, adaptation au bruit toujours suspendue.** La fidélité
exacte aux sites observés est une propriété imposée, pas un succès appris ;
elle ne suffira pas à passer les autres critères. Aucune nouvelle photo
requise et aucun changement moteur, IHM, publication ou tag.

### Résultats — cohérence acquise, reconstruction encore non qualifiée

Le calcul s'est terminé normalement en **43,08 s**. Les valeurs observées
sont conservées exactement sur les20 cas, y compris hors du cœur mesuré ;
la MSE sur ces sites est0. Les composantes manquantes restent libres et
ne sont pas remplacées par la vérité. Le rejeu de l'ancien modèle et les
empreintes d'entrées passent.

| Mesure sans bruit | Ancien modèle | Ancien modèle projeté, sans apprentissage | Apprentissage contraint |
| --- | ---: | ---: | ---: |
| MSE globale / bilinéaire | 1,08016 | 1,05800 | 1,03604 |
| Erreur opposée / bilinéaire | 1,07588 | 1,05231 | 1,06326 |
| Projection gradient vert globale | 0,84098 | 0,84169 | 0,84694 |
| MSE moyenne sur sites observés | 0,0000059614 | 0 | 0 |

La projection seule retire une part de l'erreur, sans changer les couleurs
manquantes. L'apprentissage contraint réduit encore la MSE RGB, mais son
erreur opposée reste plus élevée que celle du témoin projeté sans
apprentissage. Il n'y a donc pas d'amélioration cohérente sur tous les
critères : le gain global ne peut pas servir de validation couleur.

La MSE globale reste **3,60 % au-dessus de la bilinéaire**, l'erreur opposée
**6,33 % au-dessus**. Pour les contours : ratiosRGB1,01556 et opposé1,04037,
projection0,81992 contre0,80468 en bilinéaire. Les rampes ont une MSE
absolue de0,0000011260, dépassant légèrement le seuil10⁻⁶. Les ondes
chromatiques passent de0,0000052785 dans l'ancien modèle à0,0000097357,
contre0,0000034305 pour la bilinéaire : la correction des sites connus
n'a pas résolu leur reconstruction.

Six critères échouent : gain RGB global, erreur opposée globale,
fidélité RGB et opposée des contours, fidélité absolue des rampes et des
ondes chromatiques. Six passent, dont la cohérence des sites observés,
imposée par construction. Les critères ne sont pas assouplis et la
décision reste `stop_before_noise_stage`. Aucun apprentissage bruité lancé.

Mémoire canonique : [reconstruction cohérente sans bruit et témoin projeté](../raw/neural-synthetic-consistent-reconstruction-v001-20261102T120141Z.json).

**Conclusion actuelle : cohérence des observations démontrée, candidat
non qualifié sur les couleurs manquantes.** Avant un nouvel entraînement,
la prochaine vérification proposée est une comparaison des quatre phases
Bayer sur **le même signal RGB**, à modèle fixe. Dans le premier banc,
chaque phase avait aussi un germe de contenu distinct ; ces résultats
ne permettent donc pas d'attribuer un défaut à la phase seule. Cette
ablation croisée évitera de modifier au hasard architecture ou pertes.
Les contrôles synthétiques déjà consultés restent exploratoires. Aucun
besoin de nouvelle photo pour cette vérification ; pas de calcul actif,
adaptation au bruit, modification moteur/IHM ou publication.

## 2 novembre 2026, 12:10 UTC — comparaison croisée des phases Bayer

Campagne `neural-bayer-phase-audit-v001-20261102T121007Z`, banc `60eb794`,
**13 tests ciblés réussis**, mesure terminée en **0,46 s**. Aucun entraînement.
Les20 contenus RGB déjà définis sont chacun échantillonnés selon les quatre
phases, soit80 cas. Vérité RGB, modèle contraint, échelle2, sigma0 et cœur48
restent fixes. Les20 associations contenu/phase d'origine sont rejouées
avec leurs identités de cibles et mosaïques ; leurs erreurs reproduisent
les mesures précédentes. Tous les sites observés restent exacts.

| Phase, mêmes20 contenus | MSE RGB réseau / bilinéaire | Erreur opposée / bilinéaire |
| --- | ---: | ---: |
| RGGB | 1,74868 | 1,74185 |
| GRBG | 1,07464 | 1,05416 |
| GBRG | 1,07298 | 1,07164 |
| BGGR | 1,03805 | 1,05511 |
| Ensemble80 | 1,23125 | 1,22784 |

Cette matrice retire la confusion entre phase et germe de contenu. Elle
confirme une sensibilité du prototype aux phases, surtout RGGB. Le ratio
global des80 cas ne doit pas être présenté comme une régression après un
nouvel apprentissage : les poids n'ont pas changé ; le précédent agrégat
de20 cas sous-échantillonnait les associations contenu/phase défavorables.

La bilinéaire change aussi avec les mesures disponibles, notamment près
des frontières. Son erreur globale par phase varie ici entre0,000088307
et0,000090765 ; celle du modèle entre0,000092200 et0,000154421. Une partie
de la variation vient donc de l'échantillonnage, mais ne suffit pas à
expliquer l'écart observé sur le réseau.

Le cas constant est particulièrement instructif : les quatre phases
permettent à la bilinéaire de retrouver exactement le même RGB, avec
variance de phase0. La variance correspondante du réseau vaut
0,0000004671. Sa MSE sur les constantes vaut0,0000020232 en RGGB contre
0,0000004398 en BGGR. Les rampes donnent une variance de phase presque
nulle en bilinéaire (1,47×10⁻¹⁶) contre5,61×10⁻⁷ pour le modèle. Ce sont
des défauts supplémentaires de reconstruction, pas des différences dues
à un bruit simulé ou à des capteurs physiques différents.

Sur les ondes chromatiques, le ratio MSE vaut3,78807 en RGGB, contre
1,21432 en BGGR. Les ratios restent au-dessus de1 pour les quatre phases.
Sur les contours, ils vont de1,03371 à1,71732. La fidélité exacte aux sites
connus ne garantit donc pas la bonne estimation des deux couleurs manquantes.
La variance de phase des images complètes mêle toutefois observations
différentes, reconstruction et projection ; elle n'est pas une mesure
pure d'un défaut de l'encodage des étiquettes du réseau.

Mémoire canonique : [audit croisé des phases Bayer](../raw/neural-bayer-phase-audit-v001-20261102T121007Z.json).

**Conclusion actuelle : défaut du prototype dépendant de la phase,
pas conclusion sur la qualité d'un type de capteur Bayer.** Ne pas choisir
BGGR parce qu'il est le moins mauvais sur ces résultats, ni restreindre
la qualification à nos fichiers Canon. La prochaine proposition est de
qualifier une transformation spatiale réversible vers une convention
canonique fixe (RGGB), avec son inverse, sans interpolation ni permutation
des canaux RGB. Puis comparer un apprentissage de même architecture et
budget sur cette représentation, sur l'ensemble des phases et contenus.
Ce sera une hypothèse à tester, pas une amélioration garantie ou une
correction de LibRaw/RCD. Aucun nouveau réglage issu d'images de contrôle
naturelles, aucune photo requise, adaptation au bruit, modification moteur
ou publication ; aucun calcul actif.

## 2 novembre 2026, 12:20 UTC — représentation Bayer canonique lancée

Campagne d'apprentissage `neural-synthetic-canonical-reconstruction-v001-20261102T122036Z`,
banc `5357b26`, **26 tests ciblés réussis**. La convention est fixée à
rouge en haut-gauche et bleu en bas-droite (RGGB), et non à BGGR qui avait
obtenu le meilleur ratio sur le contrôle précédent. La transformation
emploie uniquement des retournements horizontaux/verticaux déterminés
par le motif d'entrée ; leur inverse replace le RGB dans son repère initial.

Le rangement spatial, les étiquettes des verts et les cartes sigma suivent
exactement les mêmes retournements. Aucun rééchantillonnage, modification
des valeurs, permutation des canaux RGB ou renommage des verts. Les tests
couvrent les quatre phases et leurs variantes d'identité des verts, des
images non carrées, des valeurs négatives/supérieures au blanc, les cartes
sigma distinctes par site et la propagation des gradients. La cohérence
des observations reste exacte et limitée au régime explicitement sans bruit.

Le réseau conserve31084 paramètres, initialisation71, Adam0,001, lot4,
2048 étapes, train40, sigma0, échelle2 et cœur48. La sortie est ramenée
au repère original avant la perte ; la vérité n'est pas modifiée. Les
critères de reconstruction sur les20 cas historiques sont inchangés.
Le modèle contraint précédent est rejoué sur ces mêmes entrées comme
témoin ; ce n'est pas un réglage de coefficient sur le contrôle.

Un audit croisé80cas est enchaîné après le calcul : mêmes20 contenus
chacun sous quatre phases, avec routage canonique déclaré par la recette
du modèle. Aucun passage au bruit prévu dans cette chaîne, même si les
critères des20 cas sont satisfaits : le comportement de toutes les phases
doit être relu. Les80cas sont consultés et procéduraux, pas un test final.

**Calcul en cours.** Aucun nouveau RAW, besoin de photo, modification de
RCD, LibRaw, moteur de profils, IHM, publication, fusion ou tag. Le gain
attendu de la représentation reste une hypothèse, pas une validation.

### Résultats — réduction de l'écart entre phases, candidat non retenu

Apprentissage terminé en **35,38 s**, audit croisé en **0,51 s**. Les
80 vérités RGB et mosaïques d'entrée sont identiques par empreinte à
celles de l'audit précédent ; seuls les poids appris avec la nouvelle
représentation et son routage changent. Le témoin précédent est rejoué
sur les20 cas historiques. Les sites observés restent exacts dans tous
les cas, et les pixels sont replacés dans le repère d'origine.

| Phase, mêmes20 contenus | Ratio MSE avant représentation canonique | Ratio MSE canonique |
| --- | ---: | ---: |
| RGGB | 1,74868 | 1,02769 |
| GRBG | 1,07464 | 1,11614 |
| GBRG | 1,07298 | 1,01744 |
| BGGR | 1,03805 | 1,02176 |
| Ensemble80 | 1,23125 | 1,04603 |

Tous les ratios comparent le réseau à la même bilinéaire. L'erreur sur
les80 cas passe donc de **+23,12 % à +4,60 %**, et l'erreur opposée de
1,22784 à1,06073. Le grand écart de RGGB est réduit, mais GRBG se dégrade.
Il ne faut pas choisir la phase la plus favorable ni déclarer une
invariance acquise. Les observations sous phases différentes ne sont
toujours pas les mêmes mesures physiques d'un signal.

Sur les20 cas historiques seulement, le ratio MSE passe de1,03604
à1,10592 et le ratio opposé de1,06326 à1,16847. Cet écart avec le bilan80
confirme qu'une sélection de couples contenu/phase aurait masqué des
comportements défavorables dans les deux directions. Les contrôles restent
exploratoires et déjà consultés, pas une validation finale indépendante.

Huit critères sur douze échouent sur les20 cas : gain RGB global,
fidélité opposée globale, fidélité RGB et opposée des contours, fidélité
absolue des constantes, rampes, ondes faibles et ondes chromatiques.
Les trois critères de projection de gradients et la cohérence imposée
des sites passent. La décision reste `stop_before_noise_stage` ;
aucune adaptation au bruit n'est lancée.

Le défaut sur les signaux simples persiste. Sur les20 cas, les MSE des
constantes et rampes valent0,0000013114 et0,0000012242, supérieures au
plafond10⁻⁶ ; leurs bases bilinéaires sont exactes ou quasi exactes.
Le biais vert agrégé des80 cas vaut−0,0011285 en unités caméra. La
variance entre phases sur les constantes augmente de4,67×10⁻⁷ à7,68×10⁻⁷,
alors qu'elle reste nulle en bilinéaire. La représentation canonique
ne suffit donc pas à supprimer les erreurs résiduelles sur les aplats.

Mémoires canoniques :

- [Apprentissage sur représentation canonique et critères historiques](../raw/neural-synthetic-canonical-reconstruction-v001-20261102T122036Z.json).
- [Audit des80 cas croisés du modèle canonique](../raw/neural-bayer-phase-audit-v001-20261102T122115Z.json).

**Conclusion actuelle : transformation exacte et utile à l'étude de phase,
mais pas de candidat qualifié.** Elle corrige une partie de la sensibilité
du prototype, sans rendre la reconstruction meilleure que la bilinéaire.
La prochaine vérification proposée est de mesurer les résidus des aplats
par composante et par position dans la cellule2×2, à poids fixes, pour
chercher une correction périodique indue liée aux sorties sous-pixel.
Cette cause reste à vérifier avant de modifier à nouveau architecture,
pertes ou durée d'apprentissage. Aucun besoin de nouvelle photo ni calcul
actif ; pas de changement moteur, IHM, publication, fusion ou tag.

## 2 novembre 2026, 12:38 UTC — résidus des aplats par position sous-pixel

Campagne `neural-flat-subpixel-audit-v001-20261102T123826Z`, banc
`87b66bf`, **neuf tests ciblés réussis**, mesure terminée en **0,26 s**.
Aucun entraînement ou poids modifié. Les quatre couleurs constantes déjà
définies sont croisées avec les quatre phases, pour les modèles contraints
avec et sans représentation canonique :32 cas. Le modèle canonique rejoue
les erreurs et identités de l'audit précédent. Les sites observés restent
exacts et les empreintes des fichiers de poids sont inchangées.

Le diagnostic porte sur le **cœur32 × 32**, après exclusion de48 pixels
sur une entrée128 × 128. Pour chaque canal et position haut-gauche/
haut-droite/bas-gauche/bas-droite, il conserve l'erreur moyenne et sépare
la moyenne par canal, le motif2 × 2 et le résidu spatial restant. Les
identités de décomposition de l'énergie sont vérifiées numériquement.
Sur ces32cas, le motif2 × 2 explique **100 % de la MSE mesurée** ; le
résidu à l'intérieur de chaque sous-grille est nul. Ce résultat concerne
les aplats synthétiques au cœur, pas les contours ou les images naturelles.

### Distinguer biais des couleurs manquantes et différences sous-pixel

Un simple biais uniforme sur les seules couleurs manquantes devient déjà
périodique quand les valeurs observées sont exactes. Le considérer comme
une anomalie indépendante des quatre sorties surinterpréterait le résultat.
Une seconde décomposition respecte donc le masque de mesure :

| Modèle | MSE totale des aplats | Part du biais moyen par canal sur sites manquants | Part périodique supplémentaire |
| --- | ---: | ---: | ---: |
| Sans représentation canonique | 0,0000008169 | 51,06 % | 48,94 % |
| Canonique | 0,0000013114 | 79,32 % | 20,68 % |

Le reste spatial est nul pour les deux modèles. La représentation canonique
réduit la part relative des différences supplémentaires entre positions,
mais augmente l'erreur totale et ne supprime pas le biais des couleurs
manquantes. Les sorties sous-pixel libres permettent ici une correction
répétée non nulle d'un signal qui devrait rester uniforme. Cela décrit
le comportement appris, pas un défaut inévitable de l'opération PixelShuffle.

### Annuler le paramètre de biais de la tête n'est pas une solution

Le diagnostic isole algébriquement la contribution du biais explicite de
la dernière convolution, avec les mêmes masques et retournements. Il ne
modifie pas le réseau et ne sélectionne aucune correction. Retirer cette
contribution ferait passer la MSE canonique de0,0000013114 à0,0001235894,
soit environ94fois plus ; pour le modèle sans représentation canonique,
elle serait environ28fois plus grande. Les contributions des caractéristiques
et du biais explicite se compensent donc en partie. **Le biais moyen
mesuré des couleurs manquantes n'est pas le seul paramètre `.head.bias`.**
Ne pas appliquer une remise à zéro de ce paramètre ni un recentrage global
des photos sur la base de cette observation.

Mémoire canonique : [audit des résidus sous-pixel des aplats](../raw/neural-flat-subpixel-audit-v001-20261102T123826Z.json).

**Conclusion actuelle : défaut structurel de fidélité aux signaux constants
confirmé sur le prototype, sans bruit ni référence RCD.** Avant de nouveaux
réglages de pertes ou de durée, la prochaine proposition est une interpolation
apprise sous contraintes : construire les composantes manquantes depuis
des observations de même couleur, avec conservation des constantes, puis
étudier la reproduction des rampes. Le réseau prédirait des poids de
reconstruction plutôt qu'une correction RGB entièrement libre. Ces propriétés
devraient d'abord être prouvées par tests, sans prétendre garantir la fidélité
des contours ou le débruitage. Adam reste inchangé, critères de passage non
assouplis ; adaptation au bruit suspendue. Aucune nouvelle photo nécessaire
pour cette vérification de principe, aucun calcul actif, changement moteur,
IHM ou publication.

## 2 novembre 2026, 13:03 UTC — interpolation apprise à moments contraints lancée

Campagne `neural-synthetic-moment-reconstruction-v001-20261102T130329Z`,
banc `11d5f79`, **23 tests ciblés réussis**. Le prototype remplace la tête
de correction RGB libre par des coefficients d'interpolation des couleurs
manquantes. Tronc U-Net de largeur8, représentation RGGB canonique,
graine71, train40, Adam0,001, lot4,2048 étapes, échelle2, cœur48 et critères
de passage sont conservés. La tête prédit32 coefficients au lieu de12
corrections :31264 paramètres contre31084, pas une architecture strictement
identique ni un gain déjà mesuré.

Pour chacune des huit composantes manquantes d'une cellule RGB/Bayer,
quatre candidats utilisent uniquement des échantillons de la même couleur :
deux interpolations symétriques et deux extrapolations unilatérales.
Chaque candidat a une somme des poids égale à1 et un premier moment nul
autour de la position cible. Les combinaisons unilatérales sont nécessaires
pour pouvoir privilégier un côté d'un contour ; elles ne sont pas des
moyennes convexes et peuvent comporter des poids négatifs.

La sortie est la bilinéaire plus une combinaison des différences
`candidat − bilinéaire`, avec coefficients bornés par tanh. Les différences
ont une masse et un premier moment nuls : la correction ne peut donc
introduire un biais sur un signal constant, et reproduit un plan affine
dans l'intérieur où les appuis ne sont pas réfléchis. L'initialisation
reproduit la bilinéaire. Les positions observées ne sont jamais réécrites.
Les appuis sont au plus à trois pixels ; les bords réfléchis conservent
les constantes, mais ne garantissent pas la reproduction d'une rampe.

Les tests vérifient la couleur de chaque appui, les deux moments, les
constantes signées/supérieures au blanc, les rampes avec coefficients
arbitraires et tête non nulle, l'aller-retour des phases et les gradients.
La voie refuse le bruit ou un régime d'observations non explicitement exact.
Ce n'est donc pas encore un débruiteur conjoint entraîné sur des RAW.

Le pilote sans bruit et l'audit croisé80cas sont lancés. Le modèle RGB
libre canonique précédent est rejoué comme témoin, sans rechercher un
nouveau coefficient ou une meilleure étape. Les contrôles restent
procéduraux et déjà consultés. **Résultat en attente**, pas d'adaptation
au bruit, de photo requise, changement moteur/IHM, publication ou tag.

### Résultats — invariants acquis, contours encore bloquants

Apprentissage terminé normalement en **43,84 s**, audit80cas en **0,62 s**.
Les80 vérités RGB et mosaïques sont identiques par empreinte à celles de
l'audit canonique précédent. Les sites observés restent exacts dans les
20cas historiques et les80cas croisés ; les paramètres comptés sont31264.

| Mesure, mêmes80 cas | Tête RGB libre canonique | Interpolation contrainte |
| --- | ---: | ---: |
| MSE des constantes | 0,0000013114 | 0 |
| MSE des rampes | environ0,0000012 | 2,67×10⁻¹⁶ |
| Variance de phase sur constantes | 7,68×10⁻⁷ | 0 |
| Rapport MSE global / bilinéaire | 1,04603 | 1,06891 |
| Rapport erreur opposée / bilinéaire | 1,06073 | 1,10813 |

La conservation des signaux simples se vérifie donc après apprentissage,
et pas seulement à l'initialisation : le défaut périodique sur les aplats
mesurés est supprimé. Cela constitue une propriété utile du nouvel opérateur,
pas la preuve d'une meilleure reconstruction de toutes les images.
Les erreurs globales se dégradent au contraire sur ces80cas, dominées par
les contours ; ne pas retenir uniquement le gain sur les aplats.

| Phase, mêmes20 contenus chacune | Rapport MSE réseau / bilinéaire |
| --- | ---: |
| RGGB | 1,08734 |
| GRBG | 1,26212 |
| GBRG | 1,06751 |
| BGGR | 0,85456 |

Une seule phase s'améliore globalement ; elle ne doit pas être sélectionnée
pour déclarer le modèle qualifié. Sur les80cas, les contours donnent un
ratioRGB1,07057 et opposé1,11133. Les ondes chromatiques s'améliorent
(RGB0,85299, opposé0,78071), alors que les faibles ondes achromatiques
se dégradent relativement (RGB1,10753), malgré une faible MSE absolue
de4,39×10⁻⁸. Les comportements restent dépendants du contenu.

Sur les20cas historiques, le ratioRGB vaut1,01602, opposé1,02926 ;
les contours valent1,01682 et1,03169. **Huit critères passent, quatre
échouent** : gain RGB global, erreur opposée globale, fidélité RGB et
opposée des contours. Les critères ne sont pas modifiés ; la décision
reste `stop_before_noise_stage`. Aucun apprentissage bruité lancé.

Mémoires canoniques :

- [Pilote d'interpolation contrainte et critères](../raw/neural-synthetic-moment-reconstruction-v001-20261102T130329Z.json).
- [Audit80cas du modèle à moments contraints](../raw/neural-bayer-phase-audit-v001-20261102T130416Z.json).

**Conclusion de cette campagne : garanties de fidélité aux constantes/rampes
acquises, candidat non qualifié pour les contours ou les photos.** Avant
un nouvel entraînement, la prochaine proposition est de calculer, sur les
signaux connus, le meilleur résultat accessible par ces voisinages sous
la borne déclarée des coefficients. Cette borne « oracle » utilisera la
vérité synthétique, jamais disponible sur une vraie photo : elle servira
seulement à distinguer une limite des appuis d'une difficulté à apprendre
les bons poids. Une borne favorable ne prouverait pas que le petit U-Net
peut l'atteindre ; une borne défavorable éviterait de prolonger inutilement
son optimisation. Adam et les seuils restent fixés, aucune nouvelle photo
nécessaire, aucun calcul actif, moteur/IHM, publication, fusion ou tag.

## 2 novembre 2026, 13:17 UTC — borne oracle des voisinages contraints

Vérification demandée après l'échec des contours, banc `3851365`,
**11 tests ciblés réussis** (interpolation et oracle), calcul terminé en
**1,33 s**. Les mêmes20 contenus et quatre phases forment80cas, avec
les mêmes empreintes de vérité et de mosaïque. Les80 prédictions du réseau
figé sont reproduites exactement par empreinte. Aucun apprentissage,
changement d'Adam, de seuil ou du modèle précédent.

### Question et calcul

Pour une composante manquante, noter `b` la bilinéaire, `d_i` les quatre
différences `candidat_i − b`, et `t = vérité − b`. La correction autorisée
est `somme(alpha_i * d_i)`, avec chaque `alpha_i` dans `[-1,1]`.
Elle peut parcourir tout l'intervalle `[-S,S]`, où
`S = somme(abs(d_i))`. Le minimum de l'erreur quadratique RGB locale
est donc obtenu par `correction = clamp(t, -S, S)`.

Un témoin explicite utilise `alpha_i = clamp(t/S,-1,1) * sign(d_i)` ;
si `S=0`, tous les coefficients valent0. Le banc applique ces coefficients
à l'interpolateur existant, puis vérifie la concordance avec la projection
calculée et la copie exacte des sites observés. Les tests couvrent le
support nul, les cibles accessibles/inaccessibles positives et négatives,
les coins de la boîte, le changement d'échelle, les données invalides et
les valeurs non finies. La comparaison porte sur le même cœur32×32 après
exclusion de48 pixels de bord, en unités RGB caméra balancées, signées.

Il s'agit d'une **borne optimiste utilisant la vérité synthétique**.
La boîte fermée autorise les extrémités±1, que tanh n'atteint qu'à la
limite ; les coefficients peuvent varier indépendamment à chaque site.
La capacité finie du U-Net, la cohérence spatiale et la possibilité de
déduire ces choix depuis la seule mosaïque ne sont pas contraintes ici.
Les erreurs opposées et gradients du témoin RGB sont descriptifs : ce ne
sont pas leurs minima propres. Aucun oracle utilisable sur une photo.

### Résultats des80 cas

| Famille,16cas chacune | MSE bilinéaire | MSE réseau figé | MSE oracle RGB |
| --- | ---: | ---: | ---: |
| Constantes | 0 | 0 | 0 |
| Rampes | 2,64×10⁻¹⁶ | 2,67×10⁻¹⁶ | 6,59×10⁻¹⁷ |
| Contours | 4,44×10⁻⁴ | 4,76×10⁻⁴ | 1,06×10⁻¹⁸ |
| Ondes achromatiques faibles | 3,96×10⁻⁸ | 4,39×10⁻⁸ | 0 |
| Ondes chromatiques | 3,43×10⁻⁶ | 2,93×10⁻⁶ | 2,73×10⁻²³ |
| Ensemble pondéré par pixels | 8,96×10⁻⁵ | 9,58×10⁻⁵ | 1,34×10⁻¹⁷ |

Sur163840 composantes manquantes mesurées, aucune cible n'est hors de
l'intervalle accessible au-delà de **10⁻⁷ unité caméra**, tolérance de
diagnostic déclarée avant le calcul. Le décompte strict sans tolérance
donne1,095 % hors intervalle : ces dépassements restent sous10⁻⁷, pas
une erreur de structure démontrée. Les43,75 % de supports de rayon nul
incluent notamment les constantes et les plages constantes des contours ;
un rayon nul n'est pas un échec quand la bilinéaire est déjà juste.
La précision des rampes ne doit pas être transformée en gain photographique.

**Interprétation : les appuis actuels ne constituent pas le verrou sur
ces signaux.** Ils peuvent représenter les contours de ce banc, alors que
le réseau appris donne encore un ratio MSE global de1,06891 par rapport
à la bilinéaire et échoue aux quatre critères de la campagne précédente.
Cela ne prouve ni l'identifiabilité des couleurs manquantes depuis le CFA,
ni la capacité de ce U-Net à réaliser le champ oracle, ni la couverture
de textures naturelles, d'aliasing ou de bruit capteur.

Mémoire canonique : [borne oracle,80cas et provenance](../raw/neural-moment-oracle-v001-20261102T131715Z.json).

**Conclusion de cette campagne : capacité locale favorable, apprentissage encore
non qualifié.** La prochaine vérification proposée est de comparer les
erreurs du réseau figé sur ses contenus d'entraînement et les contrôles,
en isolant les pixels proches des contours et leurs orientations. Cela
distinguera un défaut déjà présent sur l'entraînement d'une mauvaise
généralisation avant de changer l'objectif ou d'élargir les appuis.
Ne pas enseigner directement un unique champ de coefficients oracle :
plusieurs champs peuvent donner la même sortie, et la vérité est utilisée
pour les calculer. Pas d'adaptation au bruit ni de validation photographique
déclarée ; aucune nouvelle photo nécessaire pour cette vérification.
Aucun calcul actif, modification moteur/IHM, publication, fusion ou tag.

## 2 novembre 2026, 14:56 UTC — apprentissage, contrôles et contours par phase

Banc `f8caae7`, **15 tests ciblés réussis**, campagne terminée en
**1,99 s**, sans entraînement ni modification des poids. Rejeu des40
contenus d'entraînement et20 contenus de contrôle dans les quatre phases,
soit240 cas pour60 contenus distincts, **pas240 scènes indépendantes**.
Les80 contrôles reproduisent exactement les empreintes des vérités,
mosaïques et sorties de l'audit précédent. Les sites observés restent
exacts dans les240cas. Adam, critères et moteur photographique inchangés.

Une seule phase était présentée pendant l'apprentissage de chaque contenu.
Le diagnostic sépare donc les40 couples contenu/phase effectivement vus
des120 couples utilisant les mêmes contenus avec une autre phase. Le
rapport d'erreur est le quotient des sommes d'erreurs quadratiques RGB,
avec le même cœur32×32, le même domaine caméra balancé signé et la même
bilinéaire de référence. Un ratio inférieur à1 indique une amélioration.

| Groupe | Cas | Ratio MSE RGB / bilinéaire | Ratio erreur opposée / bilinéaire |
| --- | ---: | ---: | ---: |
| Entraînement, phase effectivement vue | 40 | 0,85822 | 0,78555 |
| Mêmes contenus, trois phases non vues | 120 | 1,06786 | 1,02102 |
| Entraînement, quatre phases réunies | 160 | 1,01547 | 0,96208 |
| Contrôles, quatre phases réunies | 80 | 1,06891 | 1,10813 |

Le réseau apprend donc une correction utile sur les couples vus : gain
RGB global d'environ14,18 %, essentiellement porté par les contours.
Ce gain ne se maintient pas lors du changement de phase sur les mêmes
contenus, où l'erreur augmente d'environ6,79 %. Ce n'est pas uniquement
un échec sur de nouvelles scènes : une sensibilité à l'échantillonnage
Bayer reste présente malgré les coordonnées canoniques. Les couleurs
observées changent avec la phase ; le résultat ne démontre donc pas à lui
seul une faute d'indexation. Les contrôles de représentation précédents
restent valides ; aucune modification du décodeur n'est justifiée ici.

### Localisation des erreurs de contours

Le masque de diagnostic est calculé depuis la vérité synthétique :
les deux extrémités de chaque transition sont marquées, puis dilatées de
trois pixels en distance de Chebyshev, avant découpe du cœur. Il est
indépendant des sorties du réseau. Le complément forme la région éloignée ;
la somme des pixels et erreurs des deux régions est contrôlée contre
les mesures globales. Ces masques ne sont pas un détecteur utilisable
sur une photo inconnue.

Sur les32 cas de contours issus de l'entraînement,9652 pixels sont proches
du contour et23116 éloignés. Sur les16 contrôles de contours,4540 sont
proches et11844 éloignés. **Toute l'erreur de ces contours est dans la
bande proche** ; l'erreur loin du contour vaut0 pour la bilinéaire et
le réseau. Les garanties sur les constantes empêchent donc la diffusion
du défaut dans ces plages uniformes.

Les ratios RGB des seuls contours sont0,85754 sur les8 couples appris,
1,06805 sur les24 autres phases des mêmes contours et1,07057 sur les16
contrôles. Le problème est la reconstruction locale des transitions,
pas un nouveau biais sur les aplats.

### Couverture des orientations : lacune explicite

L'angle indiqué est celui de la **normale du contour**, issu de la règle
du générateur figé, mesuré depuis l'axe horizontal dans les coordonnées
de l'image d'origine. Il ne s'agit pas de l'orientation de sa tangente.
Les quatre classes de45° sont définies avant le rejeu.

| Normale | Contenus train | Ratio RGB, quatre phases | Contenus contrôle | Ratio RGB, quatre phases |
| --- | ---: | ---: | ---: | ---: |
| [0°,45°[ | 2 | 1,04533 | 1 | 0,91994 |
| [45°,90°[ | 0 | non mesuré | 0 | non mesuré |
| [90°,135°[ | 2 | 1,13482 | 2 | 1,16227 |
| [135°,180°[ | 4 | 0,95097 | 1 | 0,99369 |

Le secteur[90°,135°[ est défavorable dans les deux groupes, mais les
effectifs et contrastes sont trop faibles et inégaux pour attribuer
causalement l'erreur à l'angle. **Le secteur[45°,90°[ est absent des
deux groupes** : les précédents tests ne couvraient pas toutes les
orientations, même si quatre phases étaient parcourues. Cette lacune
est conservée dans les preuves ; elle n'est pas corrigée rétroactivement.
Ces contrôles procéduraux déjà consultés restent distincts d'un jeu de
validation photographique indépendant.

Mémoire canonique : [diagnostic des240 cas, régions, angles et provenance](../raw/neural-moment-generalization-v001-20261102T145658Z.json).

**Conclusion de cette campagne : apprentissage utile sur les couples vus,
généralisation de phase insuffisante et couverture angulaire incomplète.**
La prochaine expérience proposée est une ablation unique : équilibrer
les quatre phases pendant l'entraînement, avec les mêmes40 contenus,
architecture, Adam, nombre d'étapes et critères. Les contenus restent
identiques mais leur exposition par phase change ; ne pas annoncer une
expérience à données strictement identiques. Fixer avant cette expérience
un contrôle angulaire supplémentaire couvrant aussi le secteur absent,
sans l'utiliser dans l'optimisation ni ajuster les seuils à ses résultats.
Cela testera cette piste avant d'élargir les appuis ou le réseau.
Aucun nouvel apprentissage lancé dans cette campagne, aucune nouvelle
photo nécessaire pour cette étape synthétique. Aucun calcul actif,
modification moteur/IHM, publication, fusion ou tag.

## 2 novembre 2026, 15:05 UTC — exposition Bayer équilibrée et contrôle angulaire

Campagne `neural-balanced-moment-v001-20261102T150546Z`, banc `f88af98`,
**19 tests ciblés réussis**. L'unique changement d'apprentissage est
l'exposition aux phases : pour chaque contenu tiré, la phase avance dans
un cycle des quatre dispositions. Les comptes par phase d'un même
contenu diffèrent au plus de1. Cette garantie est par contenu ; les
comptes globaux par phase ne sont pas imposés strictement identiques.

Les40 contenus, initialisation graine71, U-Net contraint31264 paramètres,
Adam0,001, lot4,2048 étapes, échelle2, cœur48 et critères sont conservés.
La suite des indices d'images est conservée exactement, y compris les
tirages aléatoires de bruit effectués par l'ancien pilote même à sigma0.
Le cycle de phase n'utilise pas de nouvel aléa. Le calendrier et ses
comptes sont figés avant apprentissage, contrôlés pendant son exécution.
Les cibles dans les coordonnées canoniques et les sites observés changent
avec la phase : il s'agit d'une augmentation d'échantillonnage, pas d'un
entraînement sur des tenseurs strictement identiques.

Un contrôle nouveau est également figé avant entraînement : normales
11,25°,33,75°,…,168,75°, chacune avec deux offsets−0,08/+0,08 et quatre
phases, soit16 contenus et64 cas. La paire de couleurs caméra est fixée
à(0,12;0,32;0,55) et(0,80;0,54;0,18). Les empreintes des vérités et
mosaïques sont enregistrées avant construction du modèle. Ces contenus
ne participent ni à l'optimisation ni à une sélection d'étape ; un seul
checkpoint final est retenu. Ils couvrent notamment le secteur absent
précédemment, mais une unique paire de couleurs ne qualifie pas la
diversité des contours naturels.

La comparaison utilise les20 contrôles historiques, leur croisement80cas
et les64 nouveaux cas angulaires, pour le candidat et le précédent modèle
figé. Les80 sorties de référence doivent être reproduites par empreinte.
Les12 critères historiques et leurs seuils sont inchangés ; ils sont
aussi appliqués séparément au croisement80cas. Le contrôle angulaire
reste descriptif, sans seuil ajusté à ses résultats. Régime sans bruit,
pas de nouvelle photo nécessaire ni modification moteur/IHM.

### Résultats — phases rapprochées, gain global insuffisant

Campagne terminée normalement en **46,39 s**, apprentissage et évaluations
compris. Les8192 présentations se répartissent en2050 RGGB,2049 GRBG,
2044 GBRG et2049 BGGR ; la différence par contenu reste au plus1.
Les80 sorties du précédent modèle sont reproduites exactement par
empreinte. Le nouveau modèle préserve les observations sur les80 contrôles
croisés et64 contrôles angulaires. Aucun changement des données ou outils
pendant la campagne n'est détecté.

| Mesure, ratio MSE / bilinéaire | Précédent modèle | Phases équilibrées |
| --- | ---: | ---: |
| RGB,20 contrôles historiques | 1,01602 | 1,17866 |
| Erreur opposée,20 contrôles historiques | 1,02926 | 1,25313 |
| RGB,80 contrôles croisés | 1,06891 | 1,16417 |
| Erreur opposée,80 contrôles croisés | 1,10813 | 1,24489 |
| RGB,contours des80 contrôles croisés | 1,07057 | 1,16680 |
| RGB,64 nouveaux contrôles angulaires | 1,07631 | 1,00412 |
| Erreur opposée,64 contrôles angulaires | 1,08445 | 1,00918 |

La comparaison historique reste défavorable : environ16,42 % d'erreur
RGB au-dessus de la bilinéaire sur80cas, contre6,89 % précédemment.
**Huit critères passent et quatre échouent**, tant sur les20cas que
sur leur croisement80cas : gainRGB global, erreur opposée globale,
fidélitéRGB et opposée des contours. Les critères ne sont pas assouplis ;
la décision reste `stop_before_noise_stage`.

Les garanties sur constantes et rampes sont conservées : MSE0 sur
constantes,2,66×10⁻¹⁶ sur rampes des80cas. Les ondes chromatiques donnent
un ratio0,82457 ; les ondes achromatiques faibles1,15941 pour une MSE
absolue de4,59×10⁻⁸. Un gain sur une famille ne compense pas les contours
bloquants pour déclarer le modèle qualifié.

| Phase, mêmes20 contenus | Ratio RGB précédent | Ratio RGB équilibré |
| --- | ---: | ---: |
| RGGB | 1,08734 | 1,16966 |
| GRBG | 1,26212 | 1,15477 |
| GBRG | 1,06751 | 1,16093 |
| BGGR | 0,85456 | 1,17164 |

L'étendue des ratios entre phases passe d'environ0,408 à0,017 : le
déséquilibre observé est fortement réduit sur ce banc. **Se rapprocher
entre phases ne signifie pas mieux reconstruire** : toutes les phases
se trouvent maintenant au-dessus de la bilinéaire. La phase auparavant
favorable n'est pas conservée comme résultat sélectionné.

Sur le contrôle angulaire nouveau, les huit orientations s'améliorent
par rapport au précédent modèle, mais seules quatre passent sous la
bilinéaire. Dans le secteur auparavant absent, les normales56,25° et
78,75° donnent respectivement1,01313 et1,06150, contre1,08407 et1,11877.
Le résultat global proche de1 ne vaut donc pas conformité de chaque
orientation. Les autres détails par angle/phase/offset sont conservés
dans le JSON. Ces16 contenus partagent une paire de couleurs et plusieurs
symétries ; les64cas ne constituent pas64 scènes indépendantes.

Mémoire canonique : [ablation phases équilibrées, calendrier, critères et144 contrôles](../raw/neural-balanced-moment-v001-20261102T150546Z.json).

**Conclusion de cette campagne : l'équilibrage réduit la dépendance aux phases,
sans rendre le modèle admissible.** L'hypothèse d'un simple manque
d'exposition aux phases ne suffit pas à expliquer les erreurs de contours.
Cette campagne ne mesure pas encore les pertes finales du candidat sur
les40 contenus d'entraînement ; elle ne tranche donc pas entre ajustement
insuffisant de ces contenus et généralisation insuffisante.

La prochaine vérification proposée est un rejeu du candidat figé sur
l'entraînement, comparé au précédent modèle sur les mêmes160 couples
contenu/phase, sans prolonger l'apprentissage. Il faut établir ce point
avant d'ajouter des orientations, changer la perte ou augmenter la durée.
Les contrôles angulaires viennent désormais d'être consultés : ne plus
les présenter comme un jeu vierge pour une sélection ultérieure.
Aucun apprentissage bruité lancé, aucune nouvelle photo nécessaire,
aucun calcul actif, changement moteur/IHM, publication, fusion ou tag.

## 2 novembre 2026, 15:11 UTC — rejeu apparié des deux modèles sur l'entraînement

Banc `d1275e3`, **11 tests ciblés réussis**, audit terminé en **3,13 s**.
Les deux checkpoints sont figés et évalués sur les mêmes160 couples
contenu/phase des40 contenus d'entraînement. Aucune étape d'optimisation,
aucun gradient calculé et aucun nouveau contrôle utilisé. Les160 sorties
de l'ancien modèle reproduisent exactement leurs empreintes du premier
diagnostic d'entraînement ; les sources sont inchangées et les sites
observés conservés par les deux modèles.

Les deux modèles ont vu les mêmes40 contenus, mais seul le candidat
équilibré a vu chacun dans les quatre phases. La séparation historique
des40 couples vus par l'ancien et120 autres couples est conservée pour
expliquer la redistribution du gain ; tous sont des données d'entraînement
du nouveau modèle et **aucun résultat de cette section ne vaut validation**.

| Groupe, même bilinéaire de référence | Ancien ratio MSE RGB | Candidat équilibré |
| --- | ---: | ---: |
| Ensemble des160 couples d'entraînement | 1,01547 | 0,93526 |
| Contours,32 couples d'entraînement | 1,01544 | 0,93478 |
| 40 couples vus auparavant par l'ancien | 0,85822 | 0,96576 |
| 120 autres couples des mêmes contenus | 1,06786 | 0,92509 |
| 80 contrôles historiques, rappel campagne précédente | 1,06891 | 1,16417 |

Sur l'ensemble d'entraînement apparié, le candidat réduit la MSE RGB
d'environ **7,90 % par rapport à l'ancien modèle**, et de **6,47 % face
à la bilinéaire**. L'erreur opposée donne un ratio0,91586 face à la
bilinéaire, contre0,96208 pour l'ancien. Il abandonne une partie du gain
sur les anciens couples privilégiés, mais améliore les trois autres
phases qui lui sont désormais présentées.

Le résultat se maintient avec les fréquences réelles de présentation :
sur la distribution d'exposition du candidat, MSE RGB caméra de
0,000130881 pour le candidat contre0,000142095 pour l'ancien. Ces valeurs
sont en unités caméra au carré ; l'objectif normalisé d'apprentissage
les divise par4 (échelle2). Il s'agit d'une évaluation finale de tous
les couples, pas d'une comparaison de derniers lots aléatoires.

Parmi les96 couples des trois familles dont l'erreur bilinéaire dépasse
le plancher10⁻¹²,52 s'améliorent et44 se dégradent par rapport à l'ancien,
avec une tolérance absolue de MSE10⁻¹² fixée avant le calcul. Le gain
agrégé n'est donc pas universel. Les64 couples constants/rampes sont
exclus de ce décompte relatif : constantes MSE0, rampes1,71×10⁻¹⁶.
Les ondes chromatiques restent à1,04547 fois l'erreur bilinéaire sur
l'entraînement, malgré leur gain sur les contrôles de la campagne
précédente ; ne pas confondre une famille et ses quelques réalisations.

Les erreurs des contours restent intégralement dans la bande de trois
pixels autour des transitions vérité ; leur complément est exact.
Les résultats par phase et régions, ainsi que les empreintes des deux
sorties pour chaque couple, sont conservés dans la mémoire canonique.

Mémoire canonique : [rejeu apparié des160 couples et pertes pondérées](../raw/neural-balanced-training-audit-v001-20261102T151106Z.json).

**Conclusion de cette campagne : le candidat ajuste mieux l'entraînement, mais
ce gain ne se généralise pas aux contrôles consultés.** Le pipeline
d'apprentissage n'est pas complètement inopérant ; l'équilibrage améliore
son objectif d'entraînement tout en aggravant les contrôles historiques.
La différence train/contrôles est réelle sur ces petits jeux procéduraux,
pas une estimation statistique du comportement sur toutes les photos.
L'ajustement n'est pas parfait : cet audit ne démontre ni convergence,
ni absence de limite d'optimisation ou de capacité du modèle.

La prochaine expérience proposée est de diversifier explicitement les
contours d'entraînement (orientations, positions et contrastes), tout en
gardant les phases équilibrées et le budget d'optimisation fixé. Préparer
avant ce nouvel apprentissage des contrôles distincts réservés, sans les
utiliser pour choisir une étape ou régler les seuils ; conserver les
contrôles actuels comme régressions déjà consultées. Cette piste est
prioritaire sur une simple augmentation de durée ou de largeur du réseau.
Aucun nouvel apprentissage lancé ici, aucune nouvelle photo nécessaire
pour cette étape synthétique. Aucun calcul actif, modification moteur/IHM,
publication, fusion ou tag.

## 2 novembre 2026, 15:18 UTC — diversité des contours à budget constant

Banc `f54412b`, **19 tests ciblés réussis**. Cette ablation conserve
l'architecture31264 paramètres, graine71, Adam0,001, lot4,2048 étapes,
échelle2, cœur48 et les critères. Le calendrier des indices et phases
est exactement celui du précédent pilote équilibré, contrôlé par empreinte
et par rejeu des tirages aléatoires. Le changement porte sur les exemples
de contours, pas sur la perte, l'optimiseur ou le nombre de présentations.

Les32 contenus hors contours restent fixes. Les huit emplacements de
contours reçoivent de nouvelles vérités toutes les quatre visites de
l'emplacement, après parcours des quatre phases. Cela produit **418
contours distincts pour1657 présentations**, en conservant8192 présentations
totales. Le dernier cycle de chaque emplacement peut être partiel ; aucun
contour n'est présenté deux fois dans la même phase. Ce n'est donc plus
un corpus de40 images fixes, mais32 contenus fixes et418 contours.

Les normales sont réparties sur32 classes de5,625°, avec une variation
déterministe dans chaque classe. Position de transition dans[−0,15;+0,15],
deux couleurs RGB caméra dans[0,03;0,97] et présence de contours
achromatiques varient à partir de graines déclarées. Chaque recette,
vérité et mosaïque par phase est figée avant construction du modèle.
L'ablation change ensemble orientations, positions, contrastes et nombre
de vérités distinctes ; elle ne permettra pas d'attribuer un gain à un
seul de ces facteurs.

Un nouveau groupe réservé comprend32 contours issus de graines disjointes,
un par classe angulaire, évalués dans les quatre phases :128cas. Aucun
partage d'empreinte de vérité avec l'entraînement n'est accepté. Ces
contrôles partagent le générateur et ne constituent pas une validation
sur des photos indépendantes. Ils sont exclus de l'optimisation et de
toute sélection d'étape. Les seuils de contours existants leur sont
appliqués : ratios RGB/opposés au plus1, gain de gradient au moins95 %
de celui de la bilinéaire, sans seuil nouvellement ajusté.

Les80 régressions historiques et64 contrôles angulaires déjà consultés
sont conservés. Le précédent modèle équilibré est rejoué avec contrôle
de ses empreintes sur ces144cas. Un petit témoin d'entraînement est aussi
figé avant apprentissage : première présentation rencontrée dans chacune
des32 classes angulaires, évaluée dans sa phase effectivement apprise.
Il ne représente pas la perte complète des1657 présentations de contours.
Un seul checkpoint final est évalué ; aucun apprentissage bruité lancé.

### Résultats — gain synthétique retrouvé, qualification encore incomplète

Campagne `neural-diverse-moment-v001-20261102T151802Z` terminée en
**49,20 s**, apprentissage et évaluations compris. Le précédent modèle
équilibré est reproduit exactement sur les144 régressions. Les sites
observés restent exacts pour les deux modèles sur tous les groupes
mesurés. Les empreintes des exemples et du code sont contrôlées ; aucune
modification pendant le calcul n'est détectée.

| Groupe, ratio MSE / bilinéaire | Équilibrage seul | Contours diversifiés |
| --- | ---: | ---: |
| RGB,20 contrôles historiques | 1,17866 | 0,99334 |
| RGB,80 contrôles historiques croisés | 1,16417 | 0,980113 |
| Erreur opposée,80 contrôles historiques croisés | 1,24489 | 0,98122 |
| RGB,64 contrôles angulaires déjà consultés | 1,00412 | 0,98074 |
| Erreur opposée,64 contrôles angulaires | 1,00918 | 0,98335 |
| RGB,128 nouveaux contrôles réservés | 1,03126 | 0,98231 |
| Erreur opposée,128 contrôles réservés | 1,01944 | 0,98073 |
| RGB,sonde32 présentations d'entraînement | 1,01436 | 0,98608 |

La diversification rétablit un gain agrégé sur les groupes mesurés,
au lieu de seulement mieux ajuster les anciens contenus d'entraînement.
Les gains RGB sur80 régressions et128 contrôles réservés sont d'environ
**1,99 % et1,77 % face à la bilinéaire**. Les gradients des contours
réservés passent de0,78054 pour la bilinéaire à0,79192 pour le candidat ;
il ne s'agit pas d'un gain photographique perceptuel mesuré.

| Phase | RGB,80 régressions | RGB,128 réservés |
| --- | ---: | ---: |
| RGGB | 0,97387 | 0,97916 |
| GRBG | 0,98607 | 0,98551 |
| GBRG | 0,98649 | 0,98713 |
| BGGR | 0,97373 | 0,97740 |

Toutes les phases ont un ratio agrégé inférieur à1 sur ces deux groupes.
Cela ne signifie pas que chaque exemple s'améliore. Parmi les128 couples
réservés,58 améliorent la bilinéaire et70 la dégradent (tolérance absolue
de MSE10⁻¹² pour ce décompte dérivé). En regroupant les quatre phases d'un
même contenu par somme d'erreurs,22 contenus s'améliorent et10 se
dégradent. Les gains et pertes n'ont pas tous la même amplitude ; un
gain agrégé pondéré ne remplace pas la revue des régressions individuelles.
Sur les64 anciens contrôles angulaires,32 couples gagnent et32 perdent.

Sur les80 régressions, les constantes restent exactes et les rampes à
2,66×10⁻¹⁶ de MSE. Les contours donnent un ratioRGB0,98349 et opposé0,98598.
Les ondes achromatiques faibles donnent0,68298 et les chromatiques0,54698.
Ces valeurs concernent toujours les quelques contenus procéduraux fixés,
pas une validation générale de ces familles naturelles.

**Onze critères sur douze passent**, aussi bien sur les20 contrôles que
sur leur croisement80cas. Le seul échec restant est le gain RGB global
minimal : seuil fixé à0,98, résultat0,99334 sur20cas et **0,9801126705**
sur80cas. L'arrondi de ce dernier à0,98 ne le rend pas conforme. Les
trois contrôles de contours réservés (RGB, opposé, gradient) passent,
mais ne remplacent pas le critère global. La décision reste
`stop_before_noise_stage`, sans adaptation des seuils ni sélection d'une
phase ou d'une étape favorable.

Mémoire canonique : [diversité des contours, recettes,128 contrôles réservés et régressions](../raw/neural-diverse-moment-v001-20261102T151802Z.json).

**Conclusion de cette campagne : piste de diversité utile sur ce banc synthétique,
candidat encore non qualifié.** L'amélioration se retrouve sur des vérités
réservées avant apprentissage et dans chaque phase agrégée, ce qui est
plus encourageant que l'équilibrage seul. Elle reste faible et inégale
par exemple. Une seule initialisation ne démontre pas sa reproductibilité,
et les changements simultanés de diversité ne permettent pas de désigner
un facteur causal unique. Ces contrôles réservés viennent désormais
d'être consultés ; ils ne seront plus présentés comme vierges.

Avant un nouvel ajustement, la prochaine vérification proposée est de
caractériser les régressions individuelles par contraste, orientation
et phase, sur les résultats déjà produits, puis de préparer une
initialisation indépendante à protocole inchangé si cette lecture ne
révèle pas de défaut bloquant. Ne pas retenir ensuite la meilleure graine
pour effacer une régression. Aucun apprentissage bruité lancé, aucune
nouvelle photo nécessaire pour cette étape. Aucun calcul actif, modification
moteur/IHM, publication, fusion ou tag.

## 2 novembre 2026, 15:24 UTC — régressions par contraste, normale et phase

Banc `10aae55`, **4 tests ciblés réussis**. Lecture du seul JSON canonique
de diversification, sans PyTorch, inférence, gradient ou entraînement.
Le calcul de regroupement dure **0,0043 s** et reproduit exactement le
ratio global0,9823057465 et le support131072 pixels des128 couples
réservés. Les32 vérités et leurs mosaïques par phase sont identifiées
par empreinte ; chaque contenu doit posséder les quatre phases, sans
doublon. Les anciens JSON et les checkpoints ne sont pas modifiés.

Les classes de ce diagnostic sont fixées avant le calcul, mais portent
sur des résultats déjà consultés : elles sont **descriptives et non
confirmatoires**. Le contraste est `max(abs(couleur_2 − couleur_1))`
dans les trois canaux caméra balancés ; ce n'est ni une luminance
colorimétrique ni un contraste perceptuel. Les normales sont groupées
par45° dans les coordonnées de l'image d'origine. Les effectifs et les
sommes d'erreurs sont conservés pour chaque groupe, ainsi que les
croisements contraste/phase et normale/phase.

### Le gain moyen masque des pertes plus nombreuses et souvent petites

Le ratio global pondéré0,98231 représente une amélioration de1,77 %
face à la bilinéaire. La médiane des128 ratios individuels vaut pourtant
**1,003915**, soit une hausse d'erreur d'environ0,39 % pour le ratio
médian. On retrouve58 couples améliorés et70 dégradés, avec tolérance
absolue de MSE10⁻¹². Une MSE relative n'est pas une variation visuelle
équivalente et ce décompte ne constitue pas un seuil de perceptibilité.

| Contraste caméra | Contenus | Couples | Ratio pondéré | Couples améliorés / dégradés |
| --- | ---: | ---: | ---: | ---: |
| [0;0,15[ | 7 | 28 | 0,98665 | 15 / 13 |
| [0,15;0,35[ | 3 | 12 | 0,99423 | 6 / 6 |
| [0,35;0,65[ | 13 | 52 | 0,98636 | 22 / 30 |
| [0,65;+∞[ | 9 | 36 | 0,97876 | 15 / 21 |

Toutes ces classes ont un gain pondéré ; la régression ne se limite
donc pas aux faibles contrastes. Les groupes achromatiques (6contenus,
24couples) et chromatiques (26contenus,104couples) donnent respectivement
0,97773 et0,98277. Ces effectifs sont inégaux et confondent contraste,
angle, position et couleur : aucun facteur causal n'est isolé.

### Interaction structurée entre orientation et phase

Les moyennes par normale (0,97954;0,99240;0,98355;0,97716) et par phase
(0,97916;0,98551;0,98713;0,97740) restent toutes inférieures à1. Leur
croisement révèle cependant une alternance qui était masquée.

| Normale | RGGB | GRBG | GBRG | BGGR |
| --- | ---: | ---: | ---: | ---: |
| [0°;45°[ | 1,01231 | 0,94881 | 0,95009 | 1,00679 |
| [45°;90°[ | 1,02462 | 0,95762 | 0,96209 | 1,02577 |
| [90°;135°[ | 0,95002 | 1,01818 | 1,01896 | 0,94652 |
| [135°;180°[ | 0,94314 | 1,01127 | 1,01143 | 0,94242 |

Chaque cellule regroupe les mêmes8 contenus de sa classe d'orientation,
dans une phase donnée. Dans[45°;90°[, les8 contenus régressent en RGGB
et les8 en BGGR. Dans[90°;135°[, les8 régressent en GRBG et les8 en
GBRG. Il ne s'agit donc pas uniquement de pertes dispersées dans quelques
cas indépendants. Les quatre mesures d'un contenu restent corrélées.

Le pire ratio individuel est **1,10729**, sur la graine synthétique
`2000016`, phaseGBRG, normale94,617°, contraste caméra0,39757. Sa hausse
de MSE vaut environ1,56×10⁻⁵ unité caméra au carré. Les quatre phases
de ce même contenu régressent d'environ9,76 à10,73 %. Cela interdit de
présenter toutes les pertes comme négligeables, même si leur effet
photographique n'est pas établi par ce banc.

Le motif est compatible avec une sensibilité directionnelle après
canonisation : RGGB ne subit pas de réflexion, BGGR subit deux réflexions,
GRBG et GBRG une seule dans `bayer_canonical.py`. Une seule réflexion
inverse le signe de l'orientation, modulo180°. Cette lecture du contrat
est une **piste explicative après observation**, pas une preuve d'erreur
du mapping, des appuis, du réseau ou du décodeur. Les changements de
sites observés entre phases sont aussi réels.

Mémoire canonique : [diagnostic des régressions, regroupements et mesures individuelles](../raw/neural-contour-regressions-v001-20261102T152447Z.json).

**Conclusion de cette campagne : gain moyen confirmé, asymétrie orientation/phase
à expliquer avant de poursuivre l'apprentissage.** Ce résultat modifie
l'ordre proposé : l'essai d'une seconde initialisation est reporté.
La prochaine vérification proposée porte sur les symétries du chemin
canonique et des appuis, avec une même mosaïque transformée et ses tags
Bayer transformés ensemble, puis remise des sorties dans les mêmes
coordonnées. Comparer séparément la bilinéaire, les appuis contraints et
le réseau figé permettra de localiser une éventuelle asymétrie sans
confondre transformation exacte d'une observation et nouvel échantillonnage
de la vérité. Aucun correctif n'est décidé à partir de ces groupes.
Qualification inchangée, pas d'apprentissage bruité, de nouvelle photo,
de calcul actif, modification moteur/IHM, publication, fusion ou tag.

## 2 novembre 2026, 15:32 UTC — symétries exactes de la mosaïque et des appuis

Banc `691d53f`, **8 tests ciblés réussis**. Le contrôle porte sur les32
contours déjà consultés, leurs quatre phases et les huit transformations
du carré : quatre rotations, avec ou sans réflexion. Soit1024 cas de
transformation pour128 observations sources, et toujours32 contenus
distincts. Les valeurs de la mosaïque et les tags2×2 sont transformés
ensemble, sans interpolation ni acquisition de nouveaux sites depuis la
vérité. Les sorties RGB sont ensuite remises dans les coordonnées sources.

Les tests préalables couvrent aussi les tailles non carrées, valeurs
signées/supérieures au blanc, les deux identités des verts, la copie des
échantillons capteur et les aller-retour exacts. Les32 vérités et128
prédictions sources du réseau doivent reproduire les empreintes de la
campagne précédente. Tous les calculs utilisent le cœur excluant48pixels
de bord, les poids figés, sans gradient ni étape d'apprentissage.

Cinq opérateurs sont distingués : bilinéaire ; interpolateur canonique
avec coefficients nuls ; même interpolateur avec les deux seuls candidats
symétriques pondérés chacun par0,25 ; même interpolateur avec ses quatre
candidats pondérés chacun par0,25 ; réseau appris. Les deux témoins à
poids fixes ne sont pas proposés comme nouveaux modèles de qualité.
Leur rôle est de séparer un effet des appuis d'un effet exclusivement lié
aux poids appris. La tolérance absolue de cohérence de la bilinéaire et
du témoin nul est fixée à10⁻⁶ unité caméra avant le calcul.

Un second contrôle transforme symboliquement chaque liste de taps par
transposition et recherche le candidat correspondant au site transposé,
sans renommer les couleurs. Il teste la fermeture de la **liste** des
candidats, pas celle de toutes leurs combinaisons : un candidat absent
pourrait encore être représentable dans leur espace engendré. Aucun
changement de rendu ou de profil n'est autorisé par ce diagnostic.

### Résultats — asymétrie du prototype établie, mapping exact

Campagne `neural-bayer-symmetries-v001-20261102T153218Z` terminée en
**23,25 s**. Les1024 aller-retour mosaïque/tags sont exacts et les sites
observés restent identiques dans chaque opérateur. Les128 sorties sources
du modèle figé reproduisent leurs empreintes ; le code et les poids
n'ont pas changé pendant le calcul.

Le tableau mesure la différence entre une sortie source et la sortie
transformée puis remise en coordonnées source. **Ce n'est pas une erreur
de reconstruction face à la vérité**, ni une estimation du bruit.
Les valeurs sont dans le domaine caméra balancé signé, avant profil.

| Opérateur | MSE de désaccord,1024 cas | Écart absolu maximal |
| --- | ---: | ---: |
| Bilinéaire | 1,21×10⁻¹⁷ | 1,19×10⁻⁷ |
| Canonique, coefficients nuls | 3,23×10⁻¹⁸ | 1,19×10⁻⁷ |
| Deux candidats symétriques à poids fixes | 2,19×10⁻¹⁸ | 1,19×10⁻⁷ |
| Quatre candidats à poids fixes | 5,05×10⁻⁵ | 0,27438 |
| Réseau figé | 1,78×10⁻⁶ | 0,03568 |

Les trois premiers témoins satisfont la tolérance10⁻⁶ fixée avant le
calcul. Les deux derniers présentent un désaccord structurel bien
au-delà de l'arrondi. L'écart du témoin à poids fixes ne représente pas
la qualité d'un modèle proposé ; son poids0,25 arbitraire sert à rendre
visible une propriété de l'opérateur, sans apprentissage.

Après remise en coordonnées source, le réseau est exactement cohérent
pour les rotations0°/180° et les réflexions associées. Les quatre autres
transformations, impliquant un échange des axes après canonisation,
présentent le même maximum0,03568 et une MSE de désaccord3,56×10⁻⁶.
La moyenne1024cas inclut donc une moitié de cas à désaccord nul ; ne
pas la présenter comme l'amplitude de chaque transformation.

### Appuis localisés et limites de l'attribution

Le contrôle symbolique trouve **4 candidats sur32 absents de la liste
après transposition** : les deux extrapolations unilatérales du vert
au site rouge, et les deux au site bleu. Elles utilisent le voisinage
horizontal ; les extrapolations verticales correspondantes ne sont pas
dans la liste. Les16 candidats symétriques et les12 autres unilatéraux
possèdent leur correspondant.

La non-fermeture de liste et le témoin numérique à poids fixes confirment
une asymétrie de l'opérateur d'interpolation utilisé par le pilote.
Le réseau peut également apprendre des coefficients directionnels :
ce diagnostic ne décompose pas toute sa différence entre appuis et
poids, et ne prouve pas que chaque cible serait inaccessible avec les
appuis actuels. La borne oracle favorable précédente reste valide dans
ses conditions.

Les résultats ne mettent pas en défaut les rotations/réflexions des
données ni les tags Bayer sur ce banc. Ils n'impliquent aucune correction
du décodage Canon, de RCD ou du pipeline de production : ces éléments
ne sont pas les opérateurs modifiés ou qualifiés ici.

Mémoire canonique : [audit des1024 symétries, témoins et listes d'appuis](../raw/neural-bayer-symmetries-v001-20261102T153218Z.json).

**Conclusion actuelle : asymétrie directionnelle confirmée dans le
prototype d'interpolation ; nouvelle initialisation toujours reportée.**
La prochaine expérience proposée est un témoin symétrisé sans nouvel
apprentissage : moyenner à poids égaux la prédiction du réseau et sa
prédiction sur la mosaïque transposée, remise dans les coordonnées sources.
Ce témoin conserve les observations et utilise les mêmes poids figés ;
il permettrait de mesurer si supprimer cette dépendance améliore aussi
la reconstruction, au prix d'une seconde inférence. La symétrie seule
ne garantit pas ce gain de qualité. Une fermeture explicite de la liste
des appuis pourrait ensuite être étudiée si le bénéfice le justifie,
sans l'assimiler à un correctif déjà validé ou livré.
Aucun calcul actif, apprentissage bruité, nouvelle photo, modification
moteur/IHM, publication, fusion ou tag.
