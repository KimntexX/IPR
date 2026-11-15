*[Français](README.md) | [English](README_en.md)*

# Nettoyage local et préparation bêta — 31 octobre 2026

Nettoyage demandé explicitement par l'utilisateur, par exception ponctuelle à
la conservation habituelle des branches de groupe. Aucun nettoyage distant.

## Actions réalisées

Trois branches locales inactives supprimées avec `git branch -d`, après
vérification de zéro commit dans `dev..branche` et de l'absence de worktree associé :

| Branche | Ancienne tête, conservée dans l'historique de dev |
| --- | --- |
| `patch_color` | `27d7293` |
| `patch_in_memory_analysis` | `b6b3f28` |
| `patch_raw_opening` | `49112bf` |

Les branches GitHub n'ont pas été supprimées. Les références locales peuvent
être recréées depuis ces commits ; aucun commit n'a été effacé de `dev`.

Suppression de **413 fichiers `.obj`, 58 608 199 octets (environ56 Mio)**,
uniquement dans `.worktrees/patch_raw_bench/build-patch_raw_bench`.
Le chemin absolu de chaque cible a été vérifié avant suppression. Ces fichiers
de compilation sont régénérables par rebuild ; pas de sauvegarde de ces objets.
Captures intermédiaires, exécutables, rapports, sources, images et JSON conservés.

## Espaces conservés volontairement

- `patch_documentation` : traductions encore en cours ;8 commits hors `dev`
  au début de l'inventaire. Le lot documentaire n'est pas déclaré terminé.
- `patch_raw_bench` :23 commits hors `dev`, dont des outils de banc à conserver ;
  copie de certaines preuves dans la documentation ne vaut pas intégration des outils.
- Tous les worktrees actifs des autres agents et leurs branches, ainsi que les
  fichiers non suivis du worktree principal : aucune modification.
- Branches distantes, tags, `dev` et `main` : aucune publication ou fusion.

Ce nettoyage est volontairement ciblé, pas un audit exhaustif de tous les builds
du dépôt. Aucune suppression automatique de corpus ni de preuves d'analyse.

## Suite

Le [lot B0 du plan bêta](../../../architecture/roadmap-beta.md) décrit les premières
actions de socle. Préparer la bêta ne dispense pas de qualifier les réglages alpha
restants ; aucune version bêta n'est déclarée par ce nettoyage.
