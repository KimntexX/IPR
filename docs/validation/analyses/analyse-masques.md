# Analyse — Masques

**Bilan actuel — 12 novembre 2026.** Les cônes triangulaires et trapézoïdaux
sont ajoutés sur `patch_masks` ; tests ciblés validés, revue visuelle utilisateur
encore attendue. Cette extension ne requalifie pas tout le moteur des masques.
Voir la dernière campagne en fin de rapport.

Rapport évolutif du moteur des masques (lot B2,
[ADR-0021](../../decisions/ADR-0021-socle-edition-beta.md)). Les JSON sous
`docs/validation/raw/` font autorité ; ce rapport les interprète. Suivi des validations
partielles : [issue #24](https://github.com/KimntexX/IPR/issues/24).

État au 3 novembre 2026 : moteur et panneau intégrés à `dev` (`v0.5.110-beta`). **Dégradés
linéaire et radial, et pinceau, validés à l'usage par l'utilisateur** (« ça fait le travail
attendu »), avec la réserve que ces outils ne sont pas sa spécialité. Deux dégradés se superposent
comme attendu, sans latence notable à l'usage. **Plage de tons essayée par l'utilisateur** sur
`6V1A0064.CR3`, avec quatre masques (« plutôt content du résultat ») ; comme sa zone d'action
n'était pas évidente à première vue, elle s'affiche maintenant tant qu'un de ses curseurs est tenu.
**Le design du panneau ne convient pas à l'utilisateur** : pas de cible définie pour l'instant, le
sujet est reporté.

## Le modèle

« Calque » désigne ici ce que l'ADR a retenu, comme le fait Lightroom vu de l'extérieur : un
masque n'est pas une image posée sur une autre, mais une zone qui porte ses propres valeurs des
réglages existants. À chaque étage, un pixel prend la valeur globale plus les valeurs des
masques pondérées par leur couverture. Les masques n'ont donc ni ordre ni mode de fusion.

Ce modèle laisse de côté une partie du plan initial de la feuille de route, écrit avant cette
décision : groupes de calques, ordre, modes de fusion.

## Choix techniques

| Choix | Raison | Solution écartée |
| --- | --- | --- |
| Formes vectorielles sur le canevas normalisé, rastérisées à la résolution du rendu | Décision de l'ADR ; le masque suit le recadrage et reste le même à l'aperçu et à l'export | Images de masque, plus lourdes et liées à une résolution |
| Formes : dégradés linéaire et radial, pinceau, plage de luminance ; ajout, soustraction, intersection | Première tranche du plan ; la plage intersectée avec une zone donne les tons de cette zone | Plage de couleur et masques issus d'un modèle, reportés |
| La plage lit l'image avant toute retouche | Le masque ne change pas quand on bouge un curseur, et ne se nourrit pas de son propre effet | Lire l'image à l'étage courant |
| Exposition et balance : cartes ramenées aux pixels de la source | Ces réglages agissent sur le signal caméra, avant la géométrie | Déplacer la géométrie, ce qui rompt le choix validé en B1 |
| Sans masque actif, aucune carte, aucune clé de cache, aucune section dans la recette | Le rendu et les fichiers existants ne changent pas d'un bit | Toujours écrire la section |
| Masques conservés par la photo cible au collage et au lot | Une zone est dessinée pour une image, comme un cadrage | Les copier avec les retouches |
| Pinceau : distances exactes, tracé réduit aux points qui le façonnent (écart sous 0,05 px), segments filtrés par ligne | Même zone à toute résolution, coût presque indépendant du nombre de points | Transformée de distance : 2 fois plus rapide sur le pinceau seul, mais 0,09 d'écart de couverture entre résolutions, refusé |
| Dernières cartes gardées tant que les masques et le cadre ne changent pas | Bouger un autre curseur ne redessine pas les masques | Redessiner à chaque rendu : plus de 250 ms avec 16 masques |
| Valeur locale de réduction du bruit de luminance : après le profil, même en développement signé | Le chemin avant profil n'a pas de carte, et n'en aura pas tant que la bêta le garde hybride | Lui ajouter une carte maintenant |

## Mesures

Preuve : [`b2-masks-audit-v001-20261103T131500Z.json`](../raw/b2-masks-audit-v001-20261103T131500Z.json), mires en mémoire,
tolérances fixées avant la campagne, dans l'outil `iprMasksAudit`.

| Mesure | Tolérance | Résultat |
| --- | --- | --- |
| Milieu d'un dégradé et bord d'un radial, 2048 et 6000 px | 0,002 du côté | Sous 1e-7 |
| Même zone entre résolutions : dégradé, radial, pinceau | 0,02 de couverture | 0,0002, 0,00002, 0,006 |
| Un masque, par rendu, aperçu de 2048 px | 40 ms | 22 ms |

Coût de la rastérisation seule, au moment où les masques changent :

| Masques | 2048 px | 6000 px |
| --- | --- | --- |
| Un radial | 21 ms | 144 ms |
| Un dégradé | 17 ms | 154 ms |
| Un trait de 200 points | 21 ms | 178 ms |
| Un trait de 2 000 points | 26 ms | 259 ms |
| 4 radiaux | 67 ms | 681 ms |
| 16 radiaux | 250 ms | 2682 ms |

Ces temps ont été pris pendant que la parité occupait la machine : ils majorent le coût.

Les tests (`localAdjustmentsTests`, `editorMasksTests`) vérifient en plus, avec et sans signal
caméra :

- un masque couvrant toute l'image à +X rend comme le réglage global à +X, sur les 16 réglages,
  à 1e-3 au plus ;
- un masque inactif (caché, opacité 0, sans valeur ou sans forme) ne change aucun bit ;
- l'ordre des masques ne change aucun bit, et leurs valeurs s'additionnent entre elles et à la
  valeur globale ;
- une zone agit dedans et pas dehors, y compris après rotation, quart de tour, miroir et
  perspective ;
- une recette se relit à l'identique, une forme inconnue est refusée, un réglage d'une version
  future est ignoré, et une recette ancienne n'a pas de masque.

Trois fautes injectées volontairement ont été détectées : cartes laissées sur le canevas,
clarté ignorant les masques, clé de cache oubliant les masques. La troisième a montré que le test
du cache ne couvrait rien : il modifiait un réglage situé après l'étage conservé. Il a été corrigé.

La parité sans masque est mesurée sur tout le corpus :
[`masks-neutral-parity-v001-20261103T130108Z.json`](../raw/masks-neutral-parity-v001-20261103T130108Z.json), 29 RAW en 33 états, comparaison exacte des octets contre `dev` : **957 rendus, 957 identiques**, sources et dossier inchangés. Elle mesure `1ffe0f9` ; le commit suivant n'ajoute que la plage, le pinceau plus rapide et le cache des cartes, tous inactifs sans masque.

## Panneau

Quatre défauts réels ont été trouvés par `masksQmlTests` avant tout essai manuel : une relecture
des curseurs qui lisait l'ancienne valeur ; une liste reconstruite à chaque valeur, qui détruisait
le bouton en cours de clic ; l'éditeur qui ne signalait pas les masques après une annulation ; des
poignées recréées à chaque mouvement, qui perdaient le geste au relâchement.

## Limites

- Le panneau ne dessine pas de poignée d'angle pour le dégradé radial : l'angle se règle au curseur.
- La réduction du bruit de luminance locale agit après le profil, même en développement signé.
- La plage de luminance n'a pas de variante de couleur ; pas de masque issu d'un modèle.
- 16 masques radiaux coûtent plus de 250 ms au moment où ils changent, ce qui dépasse la cible
  sur ce geste ; bouger un autre curseur ne les redessine pas.
- Les modules ajoutent leur propre coût par carte présente, non mesuré ici sur photo.

## 12 novembre 2026 — Cônes triangulaires et trapézoïdaux

Deux formes vectorielles supplémentaires dans le panneau Masques : un triangle
partant d’une pointe et un trapèze à deux largeurs. Les extrémités règlent
l’orientation et la longueur ; les poignées latérales et champs numériques règlent
les largeurs, relatives au grand côté du canevas. Le contour progressif est une
transition douce intérieure aux bords ; aucun calcul de lumière diffusée ou
modèle volumétrique n’est revendiqué. L’effet lumineux vient des réglages locaux,
notamment de l’exposition, et reste neutre si les réglages du masque sont nuls.

Le rasteriseur calcule la distance signée normalisée aux faces du polygone
convexe. Le triangle ignore sa largeur de départ ; un trapèze de largeur initiale
nulle donne la même couverture. Les axes dégénérés donnent une couverture vide.
Les coordonnées restent dans le canevas avant recadrage ; les contrôles existants
d’ajout, soustraction, intersection et inversion s’appliquent aussi à ces formes.

Sept suites CTest ciblées passent sous MSVC/Qt 6.11.2 : géométrie, feather,
résolution, inversion, cache, persistance, commandes/historique et gestes QML.
Le déplacement d’une poignée de largeur est prévisualisé pendant le geste et
transmis une seule fois au relâchement. Le format sidecar devient 11 pour les
nouvelles formes ; la lecture et la migration des versions antérieures sont
conservées. Le build de contrôle garde ONNX Runtime et le modèle épinglé de dev.

Preuve canonique : [`mask-cones-validation-v001-20261112T200452Z.json`](../raw/mask-cones-validation-v001-20261112T200452Z.json). Les assertions ne sont ni une nouvelle comparaison de tout le corpus RAW,
ni un benchmark pleine résolution. Le guide utilisateur français est mis à jour
à la source LaTeX ; son PDF n’est pas reconstruit dans cette passe.

### Conclusion actuelle et prochaine validation

Extension prête pour un contrôle visuel : faisceau étroit/large, portrait et
paysage, recadrage, exposition positive et négative, contour progressif et
superposition. Validation partielle rattachée à l’[issue #24](https://github.com/KimntexX/IPR/issues/24).
Aucune fusion dans dev, publication ou création de tag n’est effectuée.

## 12 novembre 2026 — Ordre des masques dans la liste

Le clic droit sur une ligne propose **Passer devant** (une place vers le haut)
et **Passer derrière** (une place vers le bas). La commande impossible à une
extrémité est grisée. La poignée pointillée à gauche permet de glisser vers une
autre ligne ; la destination se colore et aucun changement de recette n’est
envoyé avant le relâchement. Le masque sélectionné conserve ses formes et ses
réglages, même quand son indice change.

La commande `ImageEditor::moveLocalMask` ne fait que réordonner la recette et
utilise le chemin existant de validation, historique et sauvegarde. Les indices
invalides sont refusés ; un déplacement sur place n’ajoute aucune étape.
L’ordre étant déjà enregistré dans le tableau des masques, le format reste 11.
L’ordre est organisationnel : les réglages locaux restent additifs, sans nouveau
mode de fusion ni recouvrement opaque des calques.

Les sept suites ciblées passent sous MSVC/Qt 6.11.2. Les nouveaux contrôles
couvrent le geste souris réel et le menu contextuel, la sélection, une seule
étape par déplacement, annulation/rétablissement, limites et persistance des
données. Le build de l’application et son lancement de contrôle hors écran
passent également ; le test QML ne signale aucun avertissement.

Preuve canonique : [mask-order-validation-v001-20261112T202454Z.json](../raw/mask-order-validation-v001-20261112T202454Z.json).
Le guide français est mis à jour dans sa source LaTeX ; son PDF n’est pas
reconstruit. Ces contrôles ne valent pas nouvelle comparaison du corpus RAW.

### Conclusion actuelle

Build `build-patch_masks/Release/appIPR.exe` prêt pour validation visuelle du
clic droit et du déplacement par la poignée. Validation partielle suivie par
l’[issue #24](https://github.com/KimntexX/IPR/issues/24). Aucune fusion,
publication ou création de tag.

## 12 novembre 2026 — Diffusion axiale d’un spot

Les cônes disposent d’un réglage **Diffusion du spot**, de 0 à 100 %. L’effet
est plus intense près du départ et s’atténue vers l’arrivée. Avec la position
axiale normalisée `t` et la diffusion `d`, la couverture est multipliée par
`1 - d * (3*t*t - 2*t*t*t)`, sur `[0,1]`. À zéro, pas d’atténuation axiale ;
à 100 %, la couverture décroît jusqu’à zéro à la base. Le préréglage des
nouveaux cônes utilise 70 %. L’effet lumineux nécessite toujours une exposition
locale positive ; la géométrie seule ne change pas l’image.

Le contour utilise désormais la largeur locale du faisceau, au lieu de la
distance aux faces normalisée au centre du polygone. Cela conserve une pointe
triangulaire intense malgré les bords doux. L’arrivée reste adoucie ; le départ
d’un trapèze de largeur non nulle l’est aussi. Cette règle remplace le contour
précédent pour tous les cônes, sans second moteur. Il s’agit d’un masque
artistique, pas d’une simulation photométrique ou volumétrique.

La diffusion participe à la recette, à son égalité et à la clé de cache. Le
format sidecar devient 12 ; les fichiers plus anciens restent lisibles, avec
diffusion absente égale à zéro. Cela conserve leur absence d’atténuation axiale,
mais pas l’ancienne loi de contour. Le curseur est propre à chaque forme et
utilise les commandes existantes d’historique et de sauvegarde.

Sept suites ciblées passent, ainsi que le build et le lancement de contrôle.
Les assertions couvrent décroissance monotone, pointe avec contour doux,
rotation, résolution, inversion, bornes/finitude, cache, persistance et réglage
QML. Preuve : [mask-spot-validation-v001-20261112T213730Z.json](../raw/mask-spot-validation-v001-20261112T213730Z.json).
Pas de nouvelle comparaison du corpus RAW ; le PDF du guide n’est pas reconstruit.

### Conclusion actuelle

Prêt pour un contrôle visuel sur `build-patch_masks/Release/appIPR.exe` :
choisir un nouveau cône ou régler la diffusion d’un cône existant à 70–100 %,
puis augmenter son exposition. Validation visuelle en attente, suivie dans
l’[issue #24](https://github.com/KimntexX/IPR/issues/24). Aucune publication,
fusion ou création de tag.

## 13 novembre 2026 — Faisceau à élargissement rectiligne

Retour utilisateur : la diffusion du cône paraît encore circulaire plutôt que
dirigée de la pointe vers une base de plus en plus large. Le calcul ne passe pas
par le masque radial, mais la multiplication des trois rampes (côtés, atténuation
axiale, contour de la base) arrondit les courbes de couverture constante. Une
illustration analytique des équations confirme ce comportement ; ce n’est pas
une capture du rendu d’une photo.

La loi courante est remplacée : l’atténuation sur l’axe devient linéaire,
`1 - diffusion * t`. Les rampes latérales, axiales et des extrémités sont combinées
par leur minimum, au lieu de leur produit. À couverture donnée, les côtés restent
ainsi droits et la fin du faisceau est transversale, sans fermeture en lobe.
La largeur géométrique reste interpolée de la pointe à la base. Le triangle et
le trapèze utilisent la même règle ; la rotation suit leur axe, pas l’écran.
Le contour latéral reste doux et relatif à la largeur locale. Aucun nouveau
paramètre ou format de recette : le format 12 est conservé et la loi précédente
n’est pas maintenue comme moteur parallèle.

Les nouveaux contrôles mesurent les demi-largeurs du contour à couverture 0,5
avec diffusion 0,7 et contour 0,5 sur un canevas de 400 × 300 pixels. Triangle :
12, 24 et 36 pixels contre 12,2, 24,2 et 36,2 attendus ; trapèze : 35 et 43 pixels
contre 34,94 et 43,34 attendus. Tous respectent la tolérance de 1 pixel fixée
dans le test. Les points sur l’axe suivent maintenant une décroissance linéaire.
Les sept suites ciblées passent, ainsi que le build et le lancement de contrôle.

Preuve canonique : [mask-linear-cone-validation-v001-20261113T125944Z.json](../raw/mask-linear-cone-validation-v001-20261113T125944Z.json).
L’illustration locale `build-patch_masks/cone-linear-comparison.png` compare les
équations avant/après ; son empreinte est dans la preuve. Pas de nouvelle
comparaison du corpus RAW ni de reconstruction du PDF du guide.

### Conclusion actuelle

Le 13 novembre 2026, après la correction `352ec21`, l’utilisateur confirme :
« ok, le cône me convient ». Le rendu du cône est donc accepté visuellement
dans son état courant. Les scènes et valeurs exactes de ce contrôle ne sont
pas précisées ; ne pas lui attribuer les valeurs suggérées dans la procédure.
Cette validation ne vaut pas validation des autres fonctions du panneau,
notamment du réordonnancement, ni qualification de tout le corpus.
Le groupe Masques reste partiellement validé, suivi par
l’[issue #24](https://github.com/KimntexX/IPR/issues/24).
L’état accepté reste sur `patch_masks` ; aucune publication, fusion ou création
de tag n’est effectuée par cette consignation.

## 13 novembre 2026 — Qualification de livraison

L’utilisateur autorise la publication de `patch_masks` et sa fusion dans `dev`.
La branche part de `b7a4b14`, toujours tête de `dev` à la vérification distante.
Le périmètre regroupe trois lots fonctionnels : cônes triangle/trapèze,
réordonnancement de la liste et diffusion linéaire du spot. Les corrections
visuelles de diffusion appartiennent au troisième lot ; les commits de preuves
et d’accord utilisateur ne sont pas de nouveaux patchs fonctionnels.

La construction complète Release sous MSVC/Qt 6.11.2 réussit, en conservant
ONNX Runtime et le modèle neuronal épinglé. Les **80 tests natifs** passent,
sans test ignoré ni échec (189,69 s), suivis de **355 tests Python** réussis
(23,072 s), dont les fixtures d’intégration générateur/serveur de rendu.
Le contrôle documentaire retourne zéro erreur et un avertissement préexistant :
le rapport de restauration neuronale est sur sa branche de recherche, pas dans
ce worktree. Aucune nouvelle campagne photographique n’est attribuée à ces tests.

Preuve canonique : [mask-delivery-validation-v001-20261113T132439Z.json](../raw/mask-delivery-validation-v001-20261113T132439Z.json).
La publication de la branche déclenche la CI GitHub ; ses résultats multi-OS
ne sont pas encore connus. La fusion autorisée est préparée sans squash dans
un worktree d’intégration isolé, sans modifier les espaces des autres agents.
Le tag de validation reste à poser après la CI de livraison verte. L’issue #24
reste ouverte : cet ajout ne clôt ni les limites du groupe ni la validation
visuelle propre au réordonnancement.

## 13 novembre 2026 — Retour de validation des parcours

Après la livraison et les CI vertes, l’utilisateur confirme que le reste de la
recette de validation convient : clic droit/glissement, annulation/rétablissement,
réouverture et export. La combinaison cône et autre masque révèle en revanche
deux défauts : rotation très lente, au point de faire croire à une absence
d’action, et masques qui ne suivent pas le changement géométrique de la photo.
Aucune scène, valeur, position de zoom ou durée chronométrée n’est fournie ;
ces éléments ne sont pas déduits du retour.

L’inspection du code confirme l’ancrage au **canevas transformé**, explicitement
décrit dans `LocalAdjustments.h` et l’ADR-0021. `commitGeometry` change la
géométrie sans rattacher les formes à la source. Les tests antérieurs vérifient
qu’un masque agit dans la zone du canevas après transformation, pas qu’il reste
attaché au même objet quand la géométrie est modifiée après sa création. Ils ne
couvrent donc pas le comportement attendu dans cette validation utilisateur.

Pour la lenteur, la géométrie fait varier les clés de `maskValues` et de
`stageKeys`, ce qui oblige à recalculer les cartes et les tons. Avec une exposition
locale RAW, `renderTones` peut repasser par le profil caméra ; au zoom détaillé,
un rendu pleine résolution peut ensuite être demandé. Ce sont des pistes issues
du code, pas un partage de coût mesuré ni une preuve de la cause dominante.

Prochain correctif à qualifier : ancrage des masques au contenu de la photo,
transformations cohérentes des poignées/gestes et de l’export, conservation des
recettes existantes. Ne pas simplement déplacer deux extrémités : pinceau,
ellipse, perspective et largeurs doivent suivre la même transformation. Une
modification géométrique et ses effets sur les masques doivent rester cohérents
dans une seule étape d’historique. Mesurer séparément cartes, profil, filtres,
affichage et détail, sans/après masques, avant de choisir cache ou aperçu
provisoire pendant le geste. Ne pas modifier le rendu export pour accélérer
un aperçu.

Retour conservé dans [mask-user-validation-v001-20261113T144707Z.json](../raw/mask-user-validation-v001-20261113T144707Z.json).
Cette passe n’ajoute aucun changement de calcul photographique. Le cône reste
accepté ; les parcours hors géométrie sont acceptés ; le groupe reste partiel,
avec défauts de suivi et de réactivité liés aux issues #24 et #22. Ces défauts
ne sont pas annoncés corrigés.

## 13 novembre 2026 — Correctif local : ancrage source et geste géométrique

Les masques conservent désormais leur **canevas de création** dans `Mask::frame`.
Le rendu projette la couverture complète de ce cadre vers la géométrie courante ;
il ne déplace pas seulement les extrémités du cône. Pinceau, ellipse, largeurs,
inversion et combinaisons restent définis dans le même repère. Le cadre est fixe
par rapport à la source orientée/corrigée optiquement. L’affichage des poignées,
de l’ellipse et du cercle du pinceau utilise la projection directe ; les gestes
utilisent son inverse. Les coordonnées ne sont pas réécrites à chaque rotation,
ce qui évite une dérive cumulative.

La recette devient **format 13**. Sans cadre enregistré, la migration utilise
la géométrie sauvegardée comme cadre du masque : le placement à l’ouverture est
conservé, puis le masque suit les transformations suivantes. Ce choix ne prétend
pas retrouver automatiquement un objet sélectionné avant le changement de
contrat. Une étape d’historique de géométrie suffit à retrouver le rendu et ses
poignées ; le cadre fait partie de la recette et du round-trip.

Le profil caméra et les corrections précoces ne sont plus relancés à chaque
rotation : une entrée de cache RGB supplémentaire, bornée à un résultat avant
géométrie, porte une clé source/échelle/réglages caméra/masques. Source ou échelle
différente, purge et modification des réglages l’invalident. Le contrôle compare
le résultat avec et sans ce cache **au bit près**. L’ancien trajet des cartes
du canevas courant vers la source et ses tests spécifiques sont retirés ; les
cartes caméra sont calculées directement depuis le repère fixe des masques.

Pendant un geste de géométrie, l’éditeur utilise un proxy plafonné à 1024 pixels
sur son grand côté, explicitement signalé par « Aperçu géométrique rapide ».
Les dimensions de l’image affichée restent stables. Au relâchement, le proxy
provisoire est libéré et le rendu normal est redemandé, y compris si l’angle
revient à la valeur initiale. L’export reste sur le master pleine définition.
La construction des proxies est déplacée hors d’`ImageEditor`, dans le module
`RenderChain::editProxy` ; l’éditeur reste le cadenceur.

### Qualification et portée des mesures

Build Release complet et lancement de contrôle réussis : **80/80 tests natifs**
et **355 tests Python** passent. Les nouveaux contrôles couvrent les repères à
travers rotation, miroir, quart de tour, perspective, aspect/décalage, la création
d’un masque après rotation puis remise à zéro, les gestes QML inverses, la
migration et les entrées invalides, l’historique et les dimensions de l’affichage
pendant/après le mode rapide.

Banc synthétique caméra de 1024 × 682 pixels, cône exposition +1 EV et radial
ombres +25, quatre rotations (2, 4, 6 et 8°), caches préparés : médiane du rendu
`workingImage` **72,52 ms sans cache caméra**, **51,13 ms avec**, et **17,33 ms**
sur un proxy de 512 × 341 pixels représentant la réduction par deux pendant le
geste. La comparaison du cache conserve exactement les pixels ; le proxy réduit
est un aperçu provisoire distinct. Ces chiffres excluent temporisation IHM,
histogramme, redimensionnement d’affichage, décodage et disque ; ce n’est ni
un benchmark sur RAW réel ni une mesure de l’attente vécue par l’utilisateur.

Preuve canonique : [mask-geometry-following-v001-20261113T151522Z.json](../raw/mask-geometry-following-v001-20261113T151522Z.json).
Le guide français et l’ADR-0021 décrivent le contrat révisé ; PDF non reconstruit.

### Conclusion actuelle

Correctif disponible localement dans `build-patch_masks/Release/appIPR.exe`.
À recontrôler : cône et autre masque sur un objet, rotation puis miroir/quart de
tour/perspective, déplacement des poignées, relâchement du curseur, annulation,
réouverture et export. Le confort réel reste à confirmer par l’utilisateur.
Les zones de retouche B3 restent ancrées au canevas : ce lot corrige les masques,
pas la suppression d’objet. Les changements d’optique ne sont pas qualifiés ici.
Aucune publication, fusion ou création de tag de ce correctif.
