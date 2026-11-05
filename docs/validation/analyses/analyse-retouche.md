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
- **La zone suit le canevas, pas la photo** : comme un masque, elle ne suit pas l'image si le
  redressement change ensuite. Faire la géométrie d'abord.
- **Pas de clic sur objet** : c'est le lot B6, après la détection B5.
- La portée de l'échantillonnage est la composition courante, une fois les tons posés ; un masque ou
  un autre réglage local peut donc être recopié avec sa source.
- Pas encore essayé sur la grille de validation de la feuille de route : poussières, objets sur
  lignes, contours fins, bord de photo, grandes zones.
- Le rendu n'a été regardé à l'œil que par l'agent, sur une photo.
