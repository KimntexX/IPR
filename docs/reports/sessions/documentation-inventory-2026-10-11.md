# Ajout des documents non suivis — 11 octobre 2026

Branche : `patch_documentation`, synchronisée avec `dev` (`v0.1.17-alpha`).

## Périmètre

129 fichiers auparavant non suivis sont ajoutés à cette branche, environ
70,9 Mo : versions anglaises déjà présentes, sources LaTeX, capture de l'IHM,
protocoles consolidés, rapports, CSV et courbes SVG dérivés, journaux de
nettoyage. 19 autres fichiers non suivis dans le worktree principal étaient
déjà versionnés dans `dev` et n'ont pas été écrasés.

Les copies ont été vérifiées par SHA-256 avant ajout. Aucune traduction,
réécriture de source, modification de JSON publié ni suppression n'est effectuée.
Les modifications locales de documents **déjà suivis** ne sont pas incluses,
pas plus que les changements de code, générateurs et analyseurs en cours.
Les fichiers ignorés par Git (builds, images privées, PDF de build) ne font pas
partie de l'inventaire des fichiers non suivis à versionner.

## Validation et limites

Lecture UTF-8 et parsing JSON : aucune erreur. Le contrôle en lecture seule
préparé sur `patch_ci_cd` relève 39 avertissements de liens locaux manquants :
archives avec chemins absolus, images/captures de builds privées ou supprimées,
PDF non versionné, rapport de nettoyage ignoré et scripts de nettoyage encore
non suivis dans le worktree principal. Ce sont des dettes, pas des liens réparés.
Les documents anglais ont été ajoutés tels quels ; leur traduction et validation
linguistique relèvent de l'autre agent, comme demandé par l'utilisateur.

Les versions JSON immuables restent la source de vérité ; les CSV/SVG ajoutés
sont des dérivés historiques. L'ajout ne prétend pas les recalculer ni valider
de nouvelles fonctionnalités photographiques. Aucune compilation LaTeX globale.

Ce regroupement est isolé sur la branche documentaire ; aucune fusion dans
`dev` ou `main`, aucun tag de validation n'est effectué par cette opération.
