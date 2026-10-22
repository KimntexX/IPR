*[Français](gestion-branches-git.md) | [English](gestion-branches-git_en.md)*

# Gestion des branches Git

Règle adoptée le 10 octobre 2026.

## Langue des commits et pull requests

Depuis le 19 octobre 2026, tous les nouveaux messages de commit (fusions
comprises), titres et descriptions de PR sont rédigés en anglais, comme les
noms des branches. Ne pas réécrire l’historique publié pour traduire les
anciens messages. Cette règle ne change pas la langue des documents du projet.

## Organisation

`dev` rassemble les groupes de fonctionnalités validés. Chaque groupe a une
branche permanente issue de `dev` : la branche existante `patch_light` suit les
Lumières (exposition, contraste, ombres, noirs, blancs et hautes lumières).
`patch_color` suit la couleur, `patch_ci_cd` l'intégration/livraison continue
et `patch_documentation` la documentation ; ces branches existent et sont
conservées après intégration. Un prochain groupe pourra utiliser un nom tel
que `patch_noise`, qui reste ici un exemple et non une branche annoncée créée.

Depuis le 14 octobre 2026, `patch_raw_opening` est la branche locale dédiée
au rendu initial RAW/base colorimétrique, créée depuis `dev`. Le prototype
Couleur non validé reste sauvegardé sur `patch_color` (commit `8abcd12`),
sans fusion dans `dev` ni publication implicite.

Une branche de groupe est conservée après livraison. Les commits de fusion
matérialisent les validations dans le graphe Git ; les tags identifient les états
figés. `main` reste distinct et sa mise à jour nécessite une livraison explicite.

## Créer ou reprendre un groupe

Convention adoptée le 12 octobre 2026 : **noms de branches en anglais par
défaut**, y compris le groupe après le préfixe `patch_`. Par exemple :
`patch_color`, `patch_light`, `patch_noise`, `patch_documentation`.
Le groupe Couleur utilise `patch_color` ; `patch_couleur` est une ancienne
branche locale, pas une seconde branche de travail à reprendre.
Vérifier l'existence d'une branche avant d'en créer une autre pour le même
groupe. Les branches historiques ne sont pas renommées ni supprimées
automatiquement ; un tel changement nécessite un accord explicite et ne
doit pas réécrire l'historique publié.

Exécuter les commandes avec un état de travail compatible avec le changement de
branche. Préserver les modifications locales sans rapport avec le groupe ; ne
pas les ajouter automatiquement ni les effacer.

Pour un nouveau groupe, actualiser `dev`, puis créer sa branche :

```bash
git switch dev
git pull --ff-only origin dev
git switch -c patch_feature_group
git push -u origin patch_feature_group
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

### Versions de prototypage

Règle précisée le 11 octobre 2026 : le projet est en alpha. Les prochaines
fusions validées dans `dev` sont identifiées par `v0.1.X-alpha`. Le compteur
`X` augmente du nombre de patchs livrés, et non du nombre de commits. Examiner
les tags existants et identifier les patchs de la livraison avant de choisir
le prochain numéro ; ne pas créer de tag sur une fusion non validée.

Après validation de toutes les fonctionnalités de réglages implémentées, la
première série beta utilise `v0.2.X-beta`. La version mineure évolue ensuite
à chaque intégration d'un autre panneau de fonctionnalités : `v0.3.X-beta`,
`v0.4.X-beta`, etc.

Le passage en version stable **`v1.Y.X`** intervient après validation de tous
les panneaux de fonctionnalités sur les boîtiers que l'utilisateur possède
actuellement. Documenter les modèles couverts, les campagnes et les preuves
avant de déclarer ce jalon validé par l'utilisateur. Il ne constitue pas une
validation universelle pour les autres boîtiers. `Y` désigne la version mineure
et `X` la version de patch ; aucun calendrier de sortie n'est imposé.

Les tags historiques déjà publiés ne sont ni renommés ni déplacés.

Poser le tag de version sur le commit de fusion validé de `dev` ; publier ce
tag seulement dans le cadre d'une livraison autorisée. Cette règle documentaire
ne crée pas elle-même de tag et n'autorise aucune fusion ou publication.

Pendant le prototypage, aucun legacy n'est conservé, même dans les tests.
Un remplacement supprime l'ancienne implémentation, ses options de compatibilité
et les tests qui ne concernent que celle-ci ; les tests de l'implémentation
courante sont maintenus. L'historique Git et les preuves d'analyse JSON restent
conservés : ce sont des traces, pas des chemins d'exécution legacy.

### Procédure de livraison

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
