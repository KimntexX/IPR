# Audit du code

Rapport évolutif de l'audit de propreté du code demandé par l'utilisateur
le 1er novembre 2026 : conformité au C++ standard et aux règles Qt,
redondances, fuites de mémoire ou de ressources, coquilles, et respect des
règles d'[AGENTS.md](../../../AGENTS.md), dont la règle « Structure et
testabilité du code » du 31 octobre 2026.

## Campagne 1 — novembre 2026

- **État audité** : `dev` à `28c01d9` (fusion des livraisons `v0.1.65-alpha`
  et `v0.1.68-alpha`), figé pour toute la campagne.
- **Branche** : `patch_code_audit`, worktree `.worktrees/patch_code_audit`,
  portée par Claude. Codex lit le même SHA dans son propre worktree et
  transmet ses constats par le journal partagé ; Claude les intègre ici.
- **Partage** :
  - Claude : `src/core`, `src/editor`, `src/cache` et leurs tests ;
  - Codex : `src/raw`, `src/profiles`, `src/tools`, `src/app`,
    `src/providers`, le QML et leurs tests ;
  - scripts Python dans un second temps.
- **Méthode** : lecture seule pendant l'inventaire, sans correction
  photographique, refonte ni suppression. Chaque constat porte une catégorie,
  un emplacement `fichier:ligne`, une preuve et ses limites. Une suspicion
  reste une suspicion tant qu'un chemin d'exécution ou un test ne la confirme
  pas. Les bugs et fuites graves sont relus par l'autre agent avant correction.
  Les corrections viennent ensuite sur cette branche, avec tests ; un
  changement photographique est qualifié au bit près ou mesuré.

### Catégories

| Code | Catégorie |
|---|---|
| B | Bug ou comportement incorrect |
| R | Fuite ou libération de ressource (mémoire, thread, verrou, fichier) |
| N | Non-conformité aux règles (C++ standard, Qt, AGENTS.md) |
| D | Redondance ou code mort |
| L | Lisibilité, coquille, commentaire faux |

Statuts : suspicion, confirmé, corrigé, écarté (avec la raison).

### Constats — Claude

Inventaire en cours.

### Constats — Codex

En attente de transmission.
