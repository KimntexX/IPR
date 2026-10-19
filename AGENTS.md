# Règles du projet IPR

## Prototypage, versionnage et absence de legacy — règle du 11 octobre 2026

- Le projet est actuellement en prototypage **alpha**. Chaque fusion validée
  d'une branche de groupe dans `dev` porte un tag `v0.1.X-alpha`, où `X` est le
  compteur cumulatif de patchs intégrés : `X_nouveau = X_précédent + nombre de
  patchs livrés`. Ne pas confondre ce compteur avec le nombre de commits.
- Poser le tag sur le commit de fusion validé dans `dev`, jamais sur un état
  non validé ; les tags existants restent immuables. Leur publication nécessite
  l'autorisation de livraison habituelle.
- Pendant le prototypage, ne pas conserver de legacy, y compris dans les tests :
  supprimer les chemins remplacés et leurs tests spécifiques lors de leur
  remplacement ; maintenir les tests de la seule implémentation courante.
  Ne pas ajouter d'option de compatibilité `--legacy` ni de double implémentation.
  Cela n'autorise pas à effacer l'historique Git ou les preuves JSON des analyses.
- Le passage en **beta** intervient après validation de toutes les fonctionnalités
  de réglages implémentées. La première série beta est `v0.2.X-beta` ; `X` suit
  le compteur de patchs de cette série. La version mineure évolue à chaque
  intégration d'un autre panneau de fonctionnalités (`0.3`, `0.4`, etc.).
- Le passage en version **stable `v1.Y.X`** intervient après validation de tous
  les panneaux de fonctionnalités sur les boîtiers que l'utilisateur possède
  actuellement. Consigner les modèles couverts et les preuves de validation ;
  cette validation ne vaut pas pour tous les boîtiers du marché. `Y` représente
  la version mineure et `X` la version de patch. Ne pas déclarer une version
  stable avant satisfaction de ce critère et validation de l'utilisateur.

## Génération des images de tests — règle du 11 octobre 2026

- `iprGenerateTests` et son lanceur acceptent seulement `all` ou un groupe
  photographique. Lire automatiquement les images à la racine de
  `tests/image/base`, sans parcours récursif ni liste d'entrées fournie.
- Utiliser les dossiers de fonctionnalités actuels, partagés avec Lightroom
  et les autres logiciels. Créer seulement les sous-dossiers manquants ; aucun
  dossier de campagne daté ni arborescence parallèle par image.
- Produire uniquement les images TIFF : pas de manifeste, mapping, journal
  ou autre fichier auxiliaire de génération. Le réglage et sa valeur figurent
  dans le nom. Les JSON de mesures des analyseurs restent la mémoire canonique.
- Les images sont supprimées manuellement par l'utilisateur après analyse.
  Le générateur ne supprime ni n'écrase les fichiers existants, sources ou
  références. Aucun ancien mode de génération ni test de compatibilité legacy.

## Code C++ — règle du 19 octobre 2026

- Respecter le C++ standard, sans extension propre à un compilateur. Les en-têtes
  utilisent une garde d'inclusion standard, sans `#pragma once` :
  `#ifndef IPR_<CHEMIN>_<FICHIER>_H`, `#define IPR_<CHEMIN>_<FICHIER>_H`,
  `#endif // IPR_<CHEMIN>_<FICHIER>_H`. Le nom suit le chemin sous `src/`, en
  majuscules : `src/cache/ImageCacheController.h` donne
  `IPR_CACHE_IMAGECACHECONTROLLER_H`. Les en-têtes existants sont mis en
  conformité lors de la refactorisation d'harmonisation.
- Sans état, invariant ou ressource à porter, écrire des fonctions libres dans un
  espace de noms plutôt qu'une classe (approche procédurale). Réserver les classes
  aux objets qui encapsulent un état, un invariant ou la durée de vie d'une
  ressource : cache, contrôleur, objet Qt exposé au QML.
- Séparer l'interface (`.h`) de l'implémentation (`.cpp`). Garder en ligne dans
  l'en-tête seulement de petites fonctions utilitaires ; une boucle de traitement
  par pixel et les fonctions qu'elle appelle vivent dans le même `.cpp`.
- `ImageEditor` évolue vers un simple cadenceur des modules de traitement : ne pas
  y ajouter de traitement d'image, mais un module derrière une interface courte.

## Branches Git — règle du 10 octobre 2026

- `dev` est la branche d'intégration maintenue à jour après chaque validation.
- Chaque groupe de fonctionnalités dispose d'une branche durable, créée depuis
  `dev` à jour. `patch_light` est la branche du groupe Lumières.
- Travailler sur la branche du groupe concerné. Après validation de l'utilisateur,
  fusionner cette branche dans `dev` avec un commit de fusion (`--no-ff`), puis
  publier `dev` sur GitHub dans le cadre de la livraison autorisée.
- Conserver les branches de groupe après fusion : elles servent au suivi des
  évolutions et à la lecture de l'arborescence. Ne pas les supprimer automatiquement.
- Avant de reprendre une branche de groupe existante, y fusionner `dev` à jour
  pour intégrer les autres changements validés. Ne pas recréer la branche.
- Préserver le graphe des fusions : pas de squash, de rebase de l'historique publié
  ni de push forcé pour ce workflow. Conserver les tags de validation immuables.
- Une validation sur `dev` n'autorise pas une fusion ni une publication sur `main`.
- Préserver les modifications locales sans rapport avec la livraison ; ne pas les
  inclure dans un commit de fusion ou de règles documentaires.

La procédure détaillée est dans
[Gestion des branches Git](docs/architecture/gestion-branches-git.md).
Les travaux sous `docs/` suivent également les règles de [docs/AGENTS.md](docs/AGENTS.md).
