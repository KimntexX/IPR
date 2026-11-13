# Analyse — Géométrie

Rapport évolutif du panneau Géométrie (lot B1, [ADR-0021](../../decisions/ADR-0021-socle-edition-beta.md)).
Les JSON sous `docs/validation/raw/` font autorité ; ce rapport les interprète. Suivi
des validations partielles : [issue #22](https://github.com/KimntexX/IPR/issues/22).

État au 2 novembre 2026 : **validé visuellement par l'utilisateur** sur photo réelle,
intégré à `dev` (`v0.3.100-beta`). Restent ouverts la latence d'un geste avec retouches
et le masque après transformation.

## Choix techniques

Pris pendant une absence de l'utilisateur, à sa demande, pour relecture avec lui.

| Choix | Raison | Solution écartée |
| --- | --- | --- |
| Passe unique après le signal caméra, avant tous les tons | Tous les étages, donc les futurs masques, travaillent dans le même espace redressé | Après la couleur : un geste de rotation réutiliserait tons et couleur, mais les masques devraient être rastérisés dans deux espaces |
| Une seule matrice, bicubique à l'aperçu et Lanczos à l'export | Décision de l'ADR ; cumuler les réglages ne dégrade pas davantage | Passes successives |
| Quarts de tour et miroirs par copie exacte | Aucun pixel dégradé | Passage par le filtre |
| Hors de la source, les couleurs répètent le bord, et une couverture à part rend ces pixels transparents | Un filtre de voisinage ne voit pas de faux contour, donc pas de halo sombre au bord | Noir hors de la source, qui assombrit texture, clarté et voile près du bord |
| Zones vides : transparentes en PNG et TIFF, noires en JPEG et DNG, signalées par le message d'export | Rien n'est inventé, conformément à la feuille de route | Remplissage automatique |
| Contrainte optionnelle, qui réduit le cadre autour de son centre | Geste simple et prévisible | Plus grand rectangle inscrit quelconque, qui déplacerait le cadre |
| Préfiltre gaussien avant une réduction, selon l'échelle et l'aspect | La campagne d'essai montrait 79 % d'amplitude repliée à 50 % | Filtre seul |
| Échelle limitée à [50, 150] %, rotation à [-45, 45]°, perspective bornée | Aucun coin de l'image n'atteint l'horizon | Bornes libres |
| Géométrie conservée par la photo cible au collage et au lot | Un cadrage n'a pas de sens d'une photo à l'autre | La copier avec les retouches |
| Rectangle de recadrage local pendant le glissement, envoyé au relâchement | Une seule étape d'historique, aucun rendu inutile | Envoi à chaque mouvement |
| Onglet Géométrie : aperçu du cadre entier, sans étape d'historique | Maquette A et ADR : le recadrage s'affiche comme un cadre | Outil de recadrage séparé |
| Grille d'alignement dans le cadre, mailles de 40 px environ à l'écran, tant qu'un curseur de transformation est tenu | Demande de l'utilisateur, comme Lightroom : on vérifie les lignes pendant le geste, sans charger l'image au repos | Grille fixe sur tout le canevas, cachée sauf case cochée |

La réinitialisation générale des réglages retire aussi la géométrie, comme les autres
retouches.

## Campagne numérique

Preuve : [`b1-geometry-audit-v001-20261102T170514Z.json`](../raw/b1-geometry-audit-v001-20261102T170514Z.json),
outil `iprGeometryAudit`, mires synthétiques en mémoire. Tolérances fixées avant la
campagne, dans l'outil.

| Mesure | Tolérance | Résultat |
| --- | --- | --- |
| Position de repères connus, transformations rigides | 0,10 px | 0,06 px au pire |
| Même mesure en perspective | 0,25 px | Moins de 0,06 px, sur les 48 états au total |
| Écart aperçu/export | 0,50 px d'aperçu | 0,128 px au pire |
| Transformation inverse | 1e-9 | Dans la tolérance |
| Montée 10-90 % d'un bord | 2,0 px | 1,25 à 1,38 px |
| Dépassement d'un bord | 15 % | 7,6 % au pire |
| Repliement d'une mire fine réduite à 50 et 75 % | 35 % | 0,5 à 33 % |
| Rééchantillonnage, 2048 x 1365 | Latence | 15 ms, plus 6 ms de couverture |
| Rééchantillonnage, 6000 x 4000 | Export | 213 ms, plus 81 ms de couverture |

La première passe a échoué sur deux points : le repliement, corrigé par le préfiltre,
et la position en perspective, due à la mesure (le centroïde d'un point large dérive
sous une perspective) et corrigée en réduisant les points, sans changer la tolérance.

Les tests ont aussi attrapé un défaut grave avant toute intégration : un demi-tour
écrivait dans le tampon de la source par alias. Le test vérifie désormais que la source
reste intacte pour chaque orientation.

## Parité sans géométrie

Preuve : [`b1-geometry-neutral-parity-v001-20261102T170539Z.json`](../raw/b1-geometry-neutral-parity-v001-20261102T170539Z.json),
29 RAW du corpus, 30 états en pleine définition (neutre, chaque famille de réglages à
une ou deux valeurs, développement signé à zéro), comparaison exacte des octets, sans
tolérance : **870 rendus, 870 identiques à `dev`**. Sources et dossier inchangés.

Une géométrie neutre et la conversion sRGB en parallèle ne changent donc aucun pixel.
Portée de cette preuve :

- Elle mesure 788f907. Depuis, seuls des QML, des tests et des documents ont changé.
- Elle compare les pixels, pas l'histogramme : celui-ci est couvert par des tests sur
  pixels connus, dont l'exclusion des zones vides.
- Le développement signé n'y figure qu'à zéro, sans réduction du bruit.

Deux champs du JSON, immuable, se lisent ainsi. `baseline_revision` indique `291f58f`,
alors que l'exécutable de référence a été construit à `64bcbd5` : les deux ne diffèrent
que par des QML, leurs déclarations CMake et un include de `TaskRunner.h`, sans aucun
fichier de calcul. `candidate_dirty` vaut vrai parce que la mesure a démarré avec des
fichiers non commités dans le worktree, dont le JSON d'audit ; aucun fichier moteur n'a
changé depuis 788f907, et l'arbre était propre à 05eaec2.

## Latence

Sur la machine de l'utilisateur, CR3 de 32 Mpx, aperçu de 2048 px, temporisation de
80 ms comprise, par étape d'un glissement de rotation :

| Retouches déjà présentes | Avant optimisation | Après |
| --- | --- | --- |
| Aucune | 845 ms | **210 ms** |
| Tons et couleur | 1 150 ms | 523 ms |
| Avec clarté et netteté | 1 225 ms | 587 ms |
| Avec réduction du bruit | 1 480 ms | 842 ms |

Le rééchantillonnage ne représentait que 24 ms. La conversion sRGB (250 ms) et
l'histogramme (138 ms) étaient calculés sur un seul fil ; ils passent en parallèle, ce
qui profite à **tous les curseurs**. L'image d'origine (300 ms) attend désormais le
relâchement du geste. Avec des retouches, un geste de géométrie recalcule les tons et
dépasse la cible de 250 ms : conséquence assumée du choix de place, à revoir si la gêne
est réelle (par exemple un aperçu approché pendant le geste).

## Limites

- Écart restant à la maquette A : un seul bouton de quart de tour (à droite), et des
  boutons de commande plutôt que des icônes.
- Mires synthétiques : la validation sur photos réelles reste celle de l'utilisateur.
- Une homographie ne corrige pas la distorsion d'un objectif ; les profils LCP restent
  dans leur domaine.
- Le préfiltre suit l'échelle et l'aspect, pas la réduction locale d'une perspective.
- Une source avec sa propre transparence peut laisser un liseré : l'alpha droit est
  interpolé tel quel.
- L'aperçu et l'export ont des proportions arrondies différemment : l'écart reste sous
  0,13 px d'aperçu, mesuré.
- Masque après transformation : non vérifiable tant que les masques n'existent pas (B2).
- Non implémentés : redressement guidé par lignes, suggestions automatiques (B5),
  vignettage après recadrage.

## 13 novembre 2026 — Geste géométrique avec masques, correctif local

Le retour utilisateur signale une rotation très lente et des masques restés au
canevas au lieu de suivre la photo. Le correctif `patch_masks` fixe un repère de
création par masque et projette rendu, poignées et gestes avec la géométrie.
La reprise des recettes antérieures utilise leur géométrie enregistrée comme
repère, dans le format 13.

Une entrée de cache caméra avant géométrie évite de recalculer le profil pendant
la rotation. Un proxy de geste à 1024 pixels maximum, signalé sur l’image, est
remplacé par l’aperçu normal au relâchement ; taille affichée et export pleine
définition sont conservés. Cela ne modifie pas l’ancrage des zones de retouche.

Build/lancement, 80 tests natifs et 355 Python passent. Les mesures synthétiques
du [rapport des masques](analyse-masques.md) isolent `workingImage` et ne sont pas
comparables aux latences IHM du tableau historique ci-dessus. [Preuve](../raw/mask-geometry-following-v001-20261113T151522Z.json).
Le 13 novembre 2026, l’utilisateur confirme après `4d2ab6e` que les masques suivent
la géométrie et que celle-ci est plus rapide. Ce retour valide le suivi et le
gain de réactivité à l’usage, sans chronométrage ni détail par transformation.
Le correctif reste local, non publié.
