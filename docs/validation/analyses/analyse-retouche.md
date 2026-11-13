# Analyse — Retouche

Rapport évolutif de la suppression d'objets manuelle (lot B3 de la
[feuille de route bêta](../../architecture/roadmap-beta.md)). Les JSON sous `docs/validation/raw/`
font autorité ; ce rapport les interprète. Suivi des validations partielles :
[issue #28](https://github.com/KimntexX/IPR/issues/28).

État au 5 novembre 2026 : moteur sur `patch_object_removal`, panneau sur `patch_ui_object_removal`,
non fusionnés. Parcours vérifié par l'agent dans la vraie fenêtre, hors écran, sur un CR2. **Version
acceptée par l'utilisateur dans l'application le 5 novembre 2026** ; la grille de cas de la feuille de
route, l'écart entre aperçu et export et la latence propre restent ouverts dans l'issue.

## Ce qui existe

Une retouche est une zone peinte sur le canevas normalisé, comme celle d'un masque. Ce qui la remplit
est recalculé à chaque rendu : **la photo n'est jamais modifiée**, et une retouche peut être masquée,
changée ou retirée à tout moment, dans l'historique et le compagnon `.ipr`.

| Action | Ce qu'elle fait | Source |
| --- | --- | --- |
| **Effacer** | Invente la zone à partir de ce qui l'entoure, avec le moteur de remplissage du panorama | Le voisinage de la zone |
| **Corriger** | Prend la texture d'un autre endroit, ramenée à la lumière des alentours de la zone | Proposée, déplaçable |
| **Tampon** | Copie un autre endroit tel quel | Proposée, déplaçable |

Le premier coup de pinceau d'une correction ou d'un tampon propose la source dont les alentours
ressemblent le plus à ceux de la zone, sans jamais prendre la zone elle-même ni un pixel vide.

La retouche agit sur le canevas, une fois les tons posés : les réglages suivants la traitent comme le
reste de l'image. Sans retouche, la recette ne contient pas de section `retouches`, et ni la clé de
cache ni le rendu ne changent.

Une retouche impossible, zone trop grande ou source hors de l'image, **laisse l'image telle quelle et
le message le dit**, conformément à la feuille de route : avertir plutôt que promettre.

## Mesures sur une vraie photo

`_MG_7607.CR2`, 60D, 5 202 × 3 464 pixels, dans la vraie fenêtre, sur l'aperçu de 2 048 pixels
([JSON](../raw/b3-retouch-audit-v001-20261105T113028Z.json)).

| Geste | Temps total jusqu'à l'image |
| --- | --- |
| Effacer une maison, grande zone | 403 ms |
| Effacer une petite tache | 419 ms |
| Corriger, tampon | 437 et 460 ms |
| Contraste après les retouches | 731 ms, puis 404 ms au retour |
| Saturation après les retouches | 425 ms, puis 206 ms au retour |
| Export TIFF en pleine résolution, quatre retouches | 1,4 s |

Ces temps comprennent l'attente de regroupement des gestes et le rendu de l'aperçu ; ils dépassent la
cible de 250 ms sans que la part de la retouche soit isolée ici. Le ressenti reste à juger.

**Aperçu et export** : la recherche se fait à la même échelle quelle que soit la résolution, pour que
l'export retrouve ce que montre l'aperçu. Dans la zone, ils diffèrent de 11,9 niveaux sur 255 en
moyenne, contre 1,0 ailleurs : la structure est la même, une antenne recopiée tombe au même endroit,
et l'écart vient du détail fin, plus net à l'export. Ils ne sont donc pas identiques.

**Vu à l'œil par l'agent** : le toit et la cheminée disparaissent et la forêt continue, de façon
crédible à l'échelle de l'image. À 100 %, une antenne a été recopiée dans la zone inventée, et la
transition est un peu plus floue. La partie de la maison non peinte reste, évidemment.

## Tests

`retouchTests` : objet retiré sans rien changer hors de la zone, même résultat deux fois, opacité,
aperçu et export comparables, tampon exact, correction ramenée au niveau de la zone, source hors de
l'image, source proposée au bon endroit, pixels vides respectés, zone trop grande refusée, bornes.
`editorRetouchTests` : commandes, historique, réglage après une retouche, sauvegarde, collage qui garde
les retouches de la photo cible, recette avec un genre inconnu refusée, fichier source intact.
`retouchQmlTests` : le panneau dans une fenêtre, avec le vrai éditeur.

## Limites

- **Pas de reconstruction intelligente** : Effacer recopie l'image et ne sait pas ce qu'elle montre.
  Un objet sur une ligne ou une structure ne sera pas prolongé proprement, et de petits objets peuvent
  être recopiés. Le U-Net génératif reste un besoin futur ([issue #27](https://github.com/KimntexX/IPR/issues/27)).
- **Limite de la version livrée avant le correctif du 13 novembre** : la zone
  suivait le canevas, pas la photo. Le correctif local décrit ci-dessous change
  cet ancrage ; sa validation visuelle reste à faire.
- **Pas de clic sur objet** : c'est le lot B6, après la détection B5.
- La portée de l'échantillonnage est la composition courante, une fois les tons posés ; un masque ou
  un autre réglage local peut donc être recopié avec sa source.
- Pas encore essayé sur la grille de validation de la feuille de route : poussières, objets sur
  lignes, contours fins, bord de photo, grandes zones.
- Le rendu n'a été regardé à l'œil que par l'agent, sur une photo.

## 13 novembre 2026 — Suivi géométrique refusé par l’utilisateur

Après la validation du suivi géométrique des masques, l’utilisateur essaye la
suppression d’objet et confirme : « bon, la suppression ne suit pas la géométrie ».
Ce parcours de retouche est donc en défaut, indépendamment de la validation
antérieure de la qualité de suppression. Aucune photo précise, valeur de
transformation ou durée n’est attribuée à ce retour.

Le contrat actuel le confirme : `Retouch::Operation` conserve les traits et le
décalage source du tampon/correcteur dans le canevas courant, sans cadre de
création. La projection ajoutée aux masques n’est pas appliquée aux retouches.
La fonction de suppression est déjà intégrée dans `dev` ; les mentions
historiques « non fusionnée » décrivent son état avant livraison.

Correctif à préparer sur le groupe Retouche : attacher cible et source à la
photo, projeter aussi les poignées et les gestes, reprendre les recettes
existantes sans déplacer leur placement initial et garder une seule étape
d’annulation pour une transformation. Le cas Effacer ne suffit pas à qualifier
Tampon et Correcteur : leur source doit suivre la même transformation que la
cible. Préserver les pixels hors de la zone corrigée et comparer aperçu/export.

Inutile de poursuivre la recette complète sur le comportement actuel ; reprendre
rotation, miroir/quart de tour, recadrage, historique, réouverture et export après
le correctif. Aucun code photographique n’est modifié par cette consignation.
La validation reste partielle, suivie par l’[issue #28](https://github.com/KimntexX/IPR/issues/28).

## 13 novembre 2026 — Correctif local du suivi géométrique

Branche `patch_object_removal`, créée depuis `dev` `97841ff` dans un worktree
isolé. Chaque opération conserve sa géométrie de création dans
`Retouch::Operation::frame`. Traits et décalage de la source restent dans ce
cadre ; les transformations suivantes projettent ensemble cible, source,
couverture et poignées. Les gestes du pinceau et de la source passent par la
transformation inverse, déjà utilisée par les masques.

La retouche reste après les tons. Lorsque le cadre diffère de la géométrie
courante, l’image est lue dans le cadre de création et l’opération y est calculée.
Seul le delta RGB de correction est ensuite reprojeté et ajouté à l’image
courante ; le reste de l’image n’est pas remplacé par une copie rééchantillonnée.
Les pixels hors de la couverture projetée restent inchangés. Cette reprojection
peut adoucir le détail corrigé après transformation : ce point reste à juger
visuellement, sans revendiquer une identité de pixels dans la zone interpolée.
La primitive partagée `Geometry::reframe` remplace le helper privé des masques,
sans changer leur calcul. Une source de tampon/correcteur vide est refusée,
plutôt que copiée comme une tache noire.

Le format du compagnon devient **14**. Une opération sans cadre enregistré
reprend la géométrie sauvegardée ; son placement initial est conservé à la
migration. Une ancienne suppression déjà enregistrée avec un décalage ne peut
pas retrouver automatiquement son objet initial et doit être recréée.

### Qualification

Build complet Release MSVC/Qt 6.11.2 et lancement de contrôle réussis, avec le
modèle neuronal et ONNX Runtime conservés. **80 tests natifs et 355 Python**
passent. Le test éditeur est ensuite renforcé et rejoué seul, sans modification
du code de production : après transformation, export PNG identique aux pixels
de l’aperçu sur la fixture de 400 × 300 pixels, réouverture identique, puis
annulation cohérente de la géométrie et de la correction.

Les tests synthétiques couvrent Effacer, Corriger et Tampon sur une cible hors
du centre, en rotation, miroir, quart de tour, perspective et changement
d’aspect. La couleur au centre transformé est comparée à la correction de
référence transformée (tolérance fixée à 0,04 par canal linéaire). Hors de la
zone projetée, l’écart maximal exigé est zéro. Sont également contrôlés :
création après rotation puis remise à zéro, refus d’un cadre non fini,
source indisponible, invalidation de cache, persistance/migration, et
transformation de la poignée source dans le panneau QML.

Preuve canonique : [retouch-geometry-following-v001-20261113T162729Z.json](../raw/retouch-geometry-following-v001-20261113T162729Z.json).
Il ne s’agit pas d’une campagne sur de nouvelles photos RAW ni d’une mesure
de latence IHM. Le guide français et l’ADR sont actualisés à la source, sans
reconstruction du PDF.

### Conclusion actuelle

Correctif disponible localement dans
`build-patch_object_removal/Release/appIPR.exe`. Créer une **nouvelle** suppression
d’une petite prise, puis rotation, miroir/quart de tour, recadrage, annulation,
réouverture et export. Vérifier aussi le déplacement de la source sur un tampon
ou correcteur. Les anciens fichiers déjà décalés ne constituent pas un test
de récupération automatique de leur cible.

Validation visuelle en attente ; l’[issue #28](https://github.com/KimntexX/IPR/issues/28)
reste ouverte, notamment pour la qualité de reconstruction et le détail
aperçu/export sur photo réelle. Aucune publication, fusion dans `dev` ou tag.
