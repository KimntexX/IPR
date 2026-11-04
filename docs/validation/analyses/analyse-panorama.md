# Analyse — Panorama

Rapport évolutif du panorama (lot B8, paliers M3 et M4 du
[plan d'empilement et panorama](../../architecture/empilement-et-panorama.md)). Les JSON sous
`docs/validation/raw/` font autorité ; ce rapport les interprète. Suivi des validations
partielles : [issue #25](https://github.com/KimntexX/IPR/issues/25). Correction d'objectif trouvée
en chemin : [issue #26](https://github.com/KimntexX/IPR/issues/26).

État au 4 novembre 2026 : moteur et panneau sur `patch_ui_panorama`, non fusionnés. Parcours complet
vérifié par l'agent dans la vraie fenêtre, hors écran, sur onze CR2 : sélection, assemblage, aperçu,
exports DNG et TIFF, réouverture. **Validation de l'utilisateur attendue.**

## Le moteur

Un seul moteur pour les trois dispositions, dans `src/panorama/`, sans `features2d` ni `stitching`,
qui ne se présentent pas de la même façon sur les versions d'OpenCV du projet :

1. **Points communs** (`PanoramaFeatures`) : coins de Harris sur deux échelles, décrits par un
   carré normalisé orienté (MOPS de Brown, Szeliski et Winder), sur la racine de la luminance.
2. **Recouvrements** (`PanoramaGeometry::verifyPair`) : toutes les paires sont essayées, car l'ordre
   d'une série ne dit rien de sa disposition. Une homographie robuste doit expliquer assez de ce
   qui tombe dans le recouvrement (critère de Brown et Lowe).
3. **Géométrie** (`solve`, `refine`) : une rotation par image et un objectif, ajustés ensemble sur
   tous les recouvrements, puis mesurés à nouveau sur des images plus fines. Rien ne s'accumule le
   long de la série, et plusieurs rangées ne demandent rien de plus. Une paire que les autres
   contredisent est écartée ; une image sans recouvrement fiable l'est aussi, avec sa raison.
4. **Mise à niveau** (`straighten`) : **le bas de la vue reste de niveau**, à la demande de
   l'utilisateur (voir plus bas).
5. **Projection** : plane jusqu'à 50° le long du panorama, cylindrique au-delà, sphérique si le
   champ dépasse 60° en travers. L'utilisateur peut l'imposer.
6. **Raccords** (`PanoramaBlend`) : coupe minimale sur une copie réduite du canevas, là où les vues
   diffèrent le moins. Le détail se joint au raccord, les variations lentes sur une large
   transition, et les gains par canal se répartissent sur les recouvrements.
7. **Écriture par bandes** (`PanoramaJob`) : chaque image passe une fois par un dossier tampon
   possédé par la tâche ; seules les lignes en cours sont en mémoire.

## Aligner par le bas

Consigne de l'utilisateur : « on aligne toujours par le bas d'une image. Généralement une suite de
photos prises pour faire un panorama, son point d'appui c'est le bas de la vue. » Elle est appliquée
ainsi, **interprétation à confirmer** :

- Dans un balayage, les bords bas de la rangée la plus basse donnent le roulis du monde et sa
  courbure. Un arc court renseigne mal sur la courbure, que de petits changements d'inclinaison
  décideraient seuls : un rappel vers zéro l'écarte alors, et laisse passer un arc large. Une
  première version sans ce rappel faisait tourner les images d'une série réelle de près de 18°.
- Dans un panorama vertical, l'image la plus basse reste d'équerre.
- Le recadrage garde le bas de la vue : parmi les rectangles pleins d'au moins 90 % de la plus
  grande surface, il prend le plus bas, et coupe donc plutôt par le haut.

Mesure synthétique : sur un balayage de 96° incliné de 9°, le bord bas reste de niveau à moins de
1,5 pixel (`panoramaTests`).

## Série réelle : Oliana, 60D et Sigma 70-200 à 70 mm

Quatorze CR2 (`_MG_7596` à `_MG_7609`), à main levée, en 80 secondes. Lightroom en avait tiré
`_MG_7601-Panorama`, 20 452 × 4 680 pixels. Les sept images découpées fournies sont des tranches
contiguës de ce résultat, sans aucun recouvrement : elles ne peuvent pas servir d'entrée. Quelles
photos Lightroom a utilisées n'est pas enregistré dans le fichier ; le jeu de onze ci-dessous est
une estimation à l'œil.

| Série | Images | Rangées | Focale (px) | Résidu (px) | Pire paire (px) | Canevas | Conservé | Temps | Preuve |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Une rangée | 5/5 | 1 | 16 910 | 1,68 | 3,73 | 16 444 × 3 978 | 16 027 × 3 003 | 6,8 s | [JSON](../raw/b8-panorama-audit-v001-une-rangee-20261104T082640Z.json) |
| Deux rangées | 11/11 | 2 | 16 662 | 2,44 | 8,5 | 18 847 × 5 094 | 15 041 × 4 056 | 20,8 s | [JSON](../raw/b8-panorama-audit-v001-deux-rangees-20261104T082708Z.json) |
| Série entière | 14/14 | 2 | 16 768 | 2,54 | 8,21 | 29 177 × 7 823 | 21 320 × 2 460 | 23,4 s | [JSON](../raw/b8-panorama-audit-v001-serie-entiere-20261104T082739Z.json) |

Les temps excluent le décodage ; ils incluent le rendu des raccords. Dans l'application, onze photos
prennent 65 s, décodage compris. Pour onze photos de 18 Mpx, le tampon reçoit 5,4 Go et le processus
de mesure culmine à 3,7 Go, panorama rendu compris.

Lecture :

- Toutes les images se joignent, sans liste ni ordre fournis. Les paires les pires sont celles de
  quelques dizaines de points dans un coin (41 points pour la paire à 8,5 pixels).
- Le résidu de 1 à 4 pixels vient de la parallaxe : la série est prise à main levée, avec jusqu'à
  33 s entre deux groupes, et à 16 000 pixels de focale un déplacement de 30 cm suffit.
- **Vu à 100 % par l'agent** sur six recouvrements de la série à deux rangées : ni détail dédoublé,
  ni raccord visible. Six endroits ne sont pas tout le panorama : l'œil de l'utilisateur reste à venir.
- La forme de la montagne correspond à celle de Lightroom. Lightroom montre plus de ciel et de
  crête : son résultat semble déformer les bords pour remplir le cadre, ce qu'IPR ne fait pas ; il
  recadre, ou garde toute la surface avec ses coins vides si on le demande.

## Un défaut de la correction d'objectif, révélé en chemin

Laissé libre, l'ajustement mesure ce que la correction d'objectif laisse dans l'image. Il trouvait
3,9 à 5 % de distorsion au coin et une focale de 10 000 à 11 500 pixels, contre environ 15 800
d'après les EXIF du 60D. **Sans** le profil, il trouvait 0,74 % et 16 024 pixels. La correction
ajoutait donc de la distorsion.

La cause : `applyLcp` travaillait la géométrie en unités du grand côté de l'image lorsque le profil
ne donne pas `FocalLengthX`, soit 1 371 des 3 633 profils installés, dont le RF 24-105 du R6 III. Le
modèle s'exprime en unités de focale. Sur le Sigma à 70 mm et le 60D, le coin recevait 4,4 % au lieu
de 0,85 %, ce qui correspond à la mesure. Le vignettage ignorait par ailleurs le capteur.

| Ajustement libre sur `_MG_7603` à `_MG_7606` | Focale (px) | Distorsion au coin | Points de la paire 7604-7605 | Preuve |
| --- | --- | --- | --- | --- |
| Avant la correction | 10 014 | +3,87 % | 446 | [JSON](../raw/b8-panorama-audit-v001-lcp-before-20261104T081644Z.json) |
| Sans profil | 16 024 | -0,74 % | 1 973 | [JSON](../raw/b8-panorama-audit-v001-lcp-none-20261104T081713Z.json) |
| Après la correction | 16 157 | +0,05 % | 1 976 | [JSON](../raw/b8-panorama-audit-v001-lcp-after-20261104T081701Z.json) |

**Ce changement touche le rendu de toutes les photos corrigées**, pas seulement les panoramas. Il
n'existe pas encore d'export Lightroom avec profil d'objectif pour comparer, ni de mesure sur un
boîtier plein format : l'issue #26 le suit.

Le même constat a donné une règle du moteur : ce que les fichiers savent de l'objectif n'est pas
laissé à l'ajustement. La focale reste à 7 % de la valeur EXIF, et aucune distorsion n'est cherchée
si un profil l'a corrigée. Sur une série avec parallaxe, un objectif libre absorbe ce qu'il ne cause
pas et déforme tout le panorama pour un résidu plus petit. Les deux restent libres quand le fichier
ne dit rien.

## Tests

`panoramaTests`, sur des séries synthétiques à géométrie connue : rotations relatives retrouvées à
0,05° et focale à 2 % sur deux rangées données dans le désordre, distorsion retrouvée, objectif connu
respecté, formes reconnues, recouvrement refusé entre vues sans rapport, raccord passant par la
bande où les vues s'accordent, gains, image seule rendue à l'identique, pas de marche de lumière,
mêmes pixels deux fois, image d'une autre taille ou focale écartée avec sa raison, images illisibles,
canevas refusé ou trop grand, disque insuffisant, annulation à cinq moments, dossier tampon vide
dans tous les cas. `stackSessionTests` et `assembleQmlTests` couvrent la session et le panneau.
`cameraProfileEngineTests` vérifie la correction d'objectif sur une rampe.

## Blocage trouvé par l'utilisateur

Au premier essai de l'utilisateur, sur sept photos, la progression s'est figée : processeur à zéro
pendant plusieurs minutes, 4 Go de mémoire libres sur 16. La tâche gardait sa première réservation en
demandant celle du canevas, et le contrôleur mémoire n'admet rien de plus quand il n'y a pas de place
et qu'une réservation existe : elle s'attendait elle-même. Nos essais précédents avaient de la marge
et ne pouvaient pas le voir.

Il n'y a plus qu'une réservation, qui change de taille, et une attente de mémoire s'arrête après deux
minutes avec sa raison, pour l'empilement aussi. `stackSessionTests` le reproduit sous pression
simulée, et échoue si l'on rétablit l'ancien comportement. La progression avance aussi pendant la
comparaison des paires et le calcul des raccords.

## Remplir les bords

À la demande de l'utilisateur, les coins vides peuvent être complétés : l'onglet propose **Recadrer**
(par défaut, rien d'inventé), **Remplir** et **Laisser vides**. `PanoramaFill` ne s'appuie sur aucun
modèle appris. Chaque pixel manquant cherche, sur une pyramide, un carré de l'image réelle qui
ressemble à ses alentours, en préférant la même hauteur et le voisinage, puis reçoit la moyenne de
ce que proposent ces carrés. Le détail fin revient ensuite en pleine résolution depuis ces mêmes
endroits, et la texture réelle se prolonge en miroir sur 64 pixels au-delà de sa limite. **Aucun
pixel réel n'est modifié** (vérifié au bit près), et le résultat ne dépend pas du nombre de threads.
La part inventée figure dans le message de fin et dans le DNG.

Sur la série à deux rangées, le recadrage perd 26 % des pixels réels. Remplir garde tout le canevas,
18 847 × 5 094 pixels  dont 13,3 % inventés, la crête entière, pour 5 s de plus ([JSON](../raw/b8-panorama-audit-v001-remplissage-20261104T222419Z.json)).

Mesure où la vérité est connue : un coin réel du panorama recadré, 2 % de l'image, est caché puis
rempli (erreur moyenne sur la racine de la luminance, ramenée à une image où 1 est le blanc ;
détail : écart moyen au flou, réel puis inventé).

| Coin caché | Erreur | Détail réel | Détail inventé |
| --- | --- | --- | --- |
| Haut gauche (ciel) | 0,024 | 0,0022 | 0,0018 |
| Haut droit (ciel) | 0,029 | 0,0022 | 0,0020 |
| Bas gauche (végétation) | 0,060 | 0,0154 | 0,0151 |
| Bas droit (sol) | 0,109 | 0,0185 | 0,0068 |

**Vu à l'œil par l'agent, à 100 %** :

- **Ciel : convaincant.** Aucune limite visible, même à 100 %. Des nuages peuvent apparaître là où
  il n'y en avait pas : ils sont inventés, pas retrouvés.
- **Végétation et sol : à vérifier image par image.** Les lignes droites du premier essai ont disparu
  et la limite ne se voit plus à l'échelle de l'image, mais à 100 % des blocs restent mal placés
  (un buisson clair dans la forêt, une pente qui change de nature), et la transition en miroir laisse
  de légères ondulations et parfois un trait fin.

### La coquille du toit

Sur la série de l'utilisateur, `_MG_7596` à `_MG_7601`, la rangée du haut seule : 15,2 % à inventer
([avant](../raw/b8-panorama-audit-v001-coquille-avant-20261104T221703Z.json), [après](../raw/b8-panorama-audit-v001-coquille-apres-20261104T222327Z.json)). Ces photos coupent la maison à leur limite. Le
toit se reflétait en dessous, en losange. Lightroom a aussi un toit dédoublé sur son panorama.

Trois règles sont ajoutées, sans changer un pixel réel :

- La texture réelle n'est prolongée en miroir que là où elle ressemble à ce qui a été trouvé.
- Un **objet isolé**, qui tranche avec ses alentours, n'est plus une source et n'est jamais reflété :
  une maison ne se trouve pas deux fois. Taille retenue : de 0,05 % à 1 % de l'image.
- Ces deux bornes comptent. Sans le plancher, chaque pierre d'une pente comptait comme objet et
  privait la pente de sources : une large tache brune apparaissait. Au-delà du plafond, c'est la
  texture d'une falaise.

Vu à 100 % par l'agent : le toit est coupé net, comme derrière les arbres ; la pente de droite est
sans couture. **Reste** un fin trait le long de la limite, le léger reflet d'un petit arbre, et deux
antennes recopiées dans la zone inventée. La limite est décidée par la luminance : un objet discret,
ou très fin, passe à travers.

### L'avertissement

Au-delà de 25 % de pixels inventés, le message de fin commence par « Attention » et propose de recadrer
ou de retirer les photos les plus décalées. Il ne bloque rien. Il ne peut pas venir avant le calcul :
cette part n'est connue qu'une fois la géométrie trouvée.

Ce n'est pas une génération « intelligente » : rien ici ne sait ce qu'est une maison ou un arbre.
Un modèle génératif reste un besoin futur, tracé par l'[issue #27](https://github.com/KimntexX/IPR/issues/27).

## Limites

- Aucune vraie série verticale ni plein format n'a été mesurée : le vertical repose sur le synthétique.
- Pas de correction locale de la parallaxe : seuls les raccords la cachent.
- Pas de déformation des bords pour remplir le cadre.
- Un tour complet de 360° n'est pas traité comme une boucle.
- Le DNG reste à ouvrir dans Lightroom, Photoshop et DxO, comme pour l'empilement ([issue #23](https://github.com/KimntexX/IPR/issues/23)).
- Pas de recette de régénération : le panorama est une nouvelle image.
- Le remplissage invente : ses pixels ne sont pas distingués des pixels réels dans le fichier, seule
  leur part est indiquée. Il ne convient pas à un usage documentaire.
