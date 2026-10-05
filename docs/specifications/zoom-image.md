# Zoom, navigation et comparaison dans l'aperçu

## Comportement

L'image dispose d'un zoom d'affichage, sans modification des réglages
photographiques ni des dimensions d'export. La barre en haut à droite de
l'aperçu affiche le pourcentage et les boutons **−**, **+**, **100 %** et
**Ajuster**, ainsi que **Avant/après**. Elle n'apparaît qu'après ouverture
d'une image.

| Commande | Effet |
| --- | --- |
| Clic gauche sur l'image | Alterner vue ajustée / 100 % de l'aperçu ; si l'aperçu tient déjà à 100 %, agrandir à 200 %. Les marges vides ne déclenchent pas de zoom. |
| Glisser avec le bouton gauche | Déplacer l'image agrandie. Le relâchement après un déplacement ne déclenche pas un nouveau zoom. |
| Boutons − / + ou Ctrl − / Ctrl + | Réduire / agrandir par un facteur 1,25 autour du centre de la vue. Ctrl = et Ctrl Maj + sont également reconnus. |
| Ctrl + molette au-dessus de l'image | Agrandir / réduire autour du pointeur. Un cran de 120 unités correspond au facteur 1,25. |
| Bouton 100 % ou Ctrl 1 | Afficher à 100 % de l'aperçu. |
| Bouton Ajuster ou Ctrl 0 | Recentrer et adapter à la surface disponible. |
| Molette sans Ctrl | Déplacer verticalement une image agrandie, sans changer le zoom. Le défilement horizontal et Maj + molette permettent un déplacement horizontal. |
| Bouton Avant/après ou Ctrl Maj C | Activer / désactiver la comparaison origine à gauche, actuel à droite. |
| Glisser la séparation verticale | Modifier la répartition sans déplacer l'image ou changer son zoom. |

Les raccourcis sont actifs dans la fenêtre lorsque l'aperçu est visible et
prêt. Les contrôles de zoom sont désactivés pendant son chargement et aux
bornes appropriées. La vue ajustée ne suragrandit pas une petite image.

Un changement de réglage conserve le zoom et le déplacement : la nouvelle
URL de rendu n'est pas assimilée à une nouvelle photographie. L'ouverture
d'une autre photo rétablit la vue ajustée, identifiée par son dossier et son
nom. Le redimensionnement de la fenêtre recalcule la vue ajustée ; en zoom
manuel, il conserve l'échelle et borne le déplacement. L'état du zoom est
transitoire : il n'est enregistré ni dans le sidecar `.ipr`, ni dans l'INI.

## Comparaison origine / actuel

La séparation démarre au milieu de la vue et peut être déplacée jusqu'à
ses bords, permettant d'inspecter un rendu entier. Les deux images suivent
la même transformation (échelle et déplacement), y compris après zoom à
la molette sur la séparation. Deux clips complémentaires empêchent de
superposer leurs pixels, notamment lorsque la source contient de l'alpha.
Les légendes identifient le rendu de chaque côté.

**Origine** signifie le rendu décodé par IPR avant les réglages de l'éditeur :
le développement RAW, la balance des blancs du décodage et les transformations
de profils sont déjà appliqués. Ce n'est ni le RAW brut, ni son JPEG intégré,
ni un rendu Lightroom. Aucun réglage d'exposition, de couleur, de bruit,
de netteté ou de courbe provenant de l'IHM ou d'un sidecar n'y est appliqué.
**Actuel** contient l'ensemble des réglages en cours, y compris ceux restaurés
depuis un sidecar.

L'origine reste fixe pendant les changements de réglages. Un nouveau
décodage, notamment le changement de balance des blancs automatique qui
recharge actuellement les RAW, remplace la référence par cette nouvelle
source décodée. Cette comparaison ne permet donc pas d'isoler ce changement
de décodage. Le mode et la séparation sont conservés pendant les rendus
usuels, et réinitialisés au changement de photo ou à la libération de la
source. Ils ne sont pas persistés. Le bouton n'est actif que lorsque les
deux aperçus sont prêts et de mêmes dimensions.

La référence sRGB 8 bits est encodée en tâche de fond avec le premier rendu
accepté de la source. Elle n'est pas recalculée à chaque réglage ni à chaque
activation de la comparaison. `ImageEditor::originalImageSource` expose une
URL stable `image://editor/original?revision=…` ; `ImageProvider` conserve
la référence et le rendu courant sous verrou de lecture/écriture, avec
publication de la paire avant notification QML. Un rendu périmé n'est pas
publié. Le passage à une autre source efface les deux URL et libère les
références du provider pour ne pas présenter l'origine de la photo précédente.

Le stockage CPU supplémentaire du provider est borné par un aperçu de
2048×2048×4 octets au maximum, soit 16 Mio de pixels ; Qt Quick conserve
également ses propres ressources d'affichage. La référence n'entre pas dans
le LRU d'images décodées. Aucun fichier supplémentaire n'est écrit pour la
comparaison et les réglages, histogramme courant et exports restent inchangés.

## Limite de résolution à connaître

Le composant agrandit **l'aperçu existant**, dont le moteur plafonne le plus
grand côté à **2048 pixels**, et non le RAW pleine résolution. Ici, 100 %
signifie un pixel d'aperçu par unité logique de l'interface Qt, pas un pixel
du fichier original par pixel physique d'écran. La mise à l'échelle Windows
peut encore changer ce rapport physique.

Le zoom ne relance ni décodage RAW, ni traitement pleine résolution. Il ne
change pas le cache du moteur, ses paramètres de rendu ou l'export. Il ne
doit donc pas être utilisé seul pour valider le bruit ou la netteté à 100 %
du fichier source : pour cette recette, examiner les exports pleine
résolution. Un aperçu détaillé à la demande, éventuellement par tuiles,
reste une évolution distincte avec un budget mémoire et une invalidation
sur changement de réglage à définir.

## Implantation et modèle géométrique

Le composant autonome [ImageViewport.qml](../../src/qml/ImageViewport.qml)
remplace l'ancien `Image` central dans
[Main.qml](../../src/qml/Main.qml). Le comparatif ajoute une conversion de
la source neutre via le pipeline sRGB existant, mais aucun nouvel algorithme
de réglage ni nouvelle dépendance.

Pour un aperçu de dimensions `(Iw, Ih)` et une vue `(Vw, Vh)`, l'échelle
ajustée est `sf = min(1, Vw/Iw, Vh/Ih)`. Le zoom manuel est borné entre
`min(sf, 0,05)` et `8`. Les limites de déplacement sont
`max(0, (Iw*s − Vw)/2)` et `max(0, (Ih*s − Vh)/2)` autour du centre.

Pour garder le pixel sous le pointeur `p` au même endroit lors d'un zoom :

1. Calculer sa position dans l'aperçu : `u = (p − positionImage)/s`.
2. Borner la nouvelle échelle `s'`.
3. Recalculer le décalage : `offset' = p − centreVue + (tailleAperçu/2 − u)*s'`.
4. Borner ce décalage aux limites ci-dessus.

Cette formule s'applique séparément aux deux axes. Aux bords ou lorsque
l'image tient entièrement dans la vue, le centrage/les limites de déplacement
priment sur l'ancrage exact au pointeur. Pour la molette Ctrl, le facteur est
`1,25^(delta/120)` ; si la souris ne fournit pas de delta angulaire, son delta
en pixels est utilisé. Le déplacement à la souris respecte le seuil de
glisser défini par Qt avant de distinguer clic et déplacement.

Les dimensions du dernier aperçu prêt sont conservées pendant un
rafraîchissement du rendu afin de ne pas effacer le cadrage transitoirement.

## Validation

La cible [imageViewportQmlTests](../../tests/image_viewport_qml_test.cpp)
exerce des événements réels de clic, glisser, molette et clavier sur une
image synthétique. Elle vérifie également les boutons, bornes de zoom et
de déplacement, les marges vides, le redimensionnement, la conservation sur
rafraîchissement et le retour ajusté sur changement de photo.
La comparaison est vérifiée par capture de rendu (origine à gauche,
actuel à droite), avec sources opaques et translucides, déplacement réel
de la séparation, raccourci, zoom sur la séparation et alignement géométrique.
`uiPreferencesQmlTests` vérifie que la fenêtre complète charge le composant.
`imageExportTests` vérifie la référence neutre, sa stabilité après réglage,
la restauration d'un sidecar édité, le reset, la libération au changement de
source et des dimensions identiques aux aperçus plafonnés.

Une modification du composant sélectionne ces deux tests ; une modification
de son seul test C++ sélectionne uniquement cette cible. Une modification
CMake ou des règles déclenche une suite complète, selon le
[contrat de build](../architecture/tests-cibles-et-build-windows.md).

Recette manuelle restante sous Windows :

1. Ouvrir une photo paysage, puis portrait ; vérifier cadrage et pourcentage.
2. Cliquer, déplacer jusqu'aux quatre bords, puis revenir à Ajuster.
3. Tester les raccourcis avec le clavier habituel, ainsi que Ctrl + molette
   et un pavé tactile si disponible.
4. Changer exposition/couleur en zoom manuel ; vérifier la conservation du
   cadrage, puis changer de photo et vérifier le retour ajusté.
5. Redimensionner et tester les échelles Windows 100/150/200 %, avec et sans
   panneaux latéraux. Aucun changement de dimensions d'export n'est attendu.
6. Activer Avant/après, déplacer la séparation et comparer exposition, couleur,
   bruit et netteté ; vérifier le retour au rendu courant en désactivant le
   bouton. Tester également après restauration d'un sidecar, ainsi qu'avec
   une source transparente si disponible.

## Références techniques

- [Qt — MouseArea](https://doc.qt.io/qt-6/qml-qtquick-mousearea.html) : événements souris/molette et gestion du glisser.
- [Qt — Shortcut](https://doc.qt.io/qt-6/qml-qtquick-shortcut.html) : séquences et portée des raccourcis.
- [Proxy de travail IPR](../../src/editor/imageeditor.cpp) : `ImageEditor::buildEditProxy`, plafond de 2048 pixels et export depuis l'original.
