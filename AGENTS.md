# Règles du projet IPR

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
