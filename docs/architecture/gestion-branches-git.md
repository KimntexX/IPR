# Gestion des branches Git

Règle adoptée le 10 octobre 2026.

## Organisation

`dev` rassemble les groupes de fonctionnalités validés. Chaque groupe a une
branche permanente issue de `dev` : la branche existante `patch_light` suit les
Lumières (exposition, contraste, ombres, noirs, blancs et hautes lumières).
Les prochains groupes auront chacun leur branche, par exemple `patch_color`
pour la couleur ou `patch_noise` pour le bruit ; ces noms sont des exemples,
pas des branches déjà créées.

Une branche de groupe est conservée après livraison. Les commits de fusion
matérialisent les validations dans le graphe Git ; les tags identifient les états
figés. `main` reste distinct et sa mise à jour nécessite une livraison explicite.

## Créer ou reprendre un groupe

Exécuter les commandes avec un état de travail compatible avec le changement de
branche. Préserver les modifications locales sans rapport avec le groupe ; ne
pas les ajouter automatiquement ni les effacer.

Pour un nouveau groupe, actualiser `dev`, puis créer sa branche :

```bash
git switch dev
git pull --ff-only origin dev
git switch -c patch_nom_du_groupe
git push -u origin patch_nom_du_groupe
```

Pour reprendre un groupe existant, actualiser `dev`, puis fusionner celle-ci dans
la branche de groupe. Si plusieurs postes contribuent, actualiser également la
branche de groupe avant la fusion :

```bash
git switch dev
git pull --ff-only origin dev
git switch patch_light
git pull --ff-only origin patch_light
git merge dev
```

En cas de divergence ou de conflit, examiner et résoudre les différences ; ne
pas forcer un pull, réinitialiser l'historique ou écraser une branche distante.
Les commits propres à la branche distante `dev` doivent aussi être conservés.

## Livrer après validation

Valider les tests adaptés aux zones modifiées, les résultats et l'accord de
l'utilisateur avant la livraison. Consigner les preuves et limites dans le
rapport unique du groupe, avec les données JSON versionnées et horodatées.

Après avoir commité et publié les changements du groupe :

```bash
git push origin patch_light
git switch dev
git pull --ff-only origin dev
git merge --no-ff patch_light -m "merge: integrate validated lighting changes into dev"
git push origin dev
```

Ne supprimer ni la branche locale ni la branche distante `patch_light`. Pour la
prochaine évolution, reprendre cette même branche et y intégrer `dev` à jour.
Ne pas utiliser de squash, de rebase de commits publiés ou de push forcé : le
graphe des fusions sert de suivi par arborescence. Les tags de validation ne sont
ni déplacés ni réutilisés.

## Première intégration

La version Lumières approuvée visuellement, commit `35d7c82`, identifiée par le
tag `validation-ombres-hl-2026-10-10`, a été fusionnée dans `dev` par le commit
`8e8165b`. La branche `patch_light` est conservée. Cette validation est limitée
au corpus actuel ; des évolutions restent possibles après des essais avec
d'autres boîtiers et/ou objectifs.
