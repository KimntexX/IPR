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

## Corrections et publication — 12 octobre 2026

À la demande de l'utilisateur, les 39 liens locaux manquants relevés lors de
l'inventaire ont été traités : liens absolus de sources convertis vers `src/`,
artefacts privés/non versionnés et tests remplacés identifiés explicitement
sans liens de téléchargement fictifs, et audit de nettoyage des builds ajouté
avec ses sept fichiers réels (copies vérifiées par SHA-256).

Une exception précise de `.gitignore` permet de conserver cet audit sous
`docs/reports/storage/build-cleanup-2026-10-10/`, sans versionner les builds
du projet. Les scripts de nettoyage locaux ne sont pas ajoutés ni exécutés.
Les métadonnées historiques JSON/CSV et verdicts d'analyse ne sont pas modifiés.
Les noms de branches existantes et le statut historique de la proposition CI
ont été rectifiés ; espaces de fin de ligne et lignes finales superflues
ont été retirés dans les quatre documents concernés. Aucune traduction créée.

Le nouveau contrôle documentaire donne **0 erreur et 0 avertissement de lien
local manquant**. Ce contrôle n'analyse pas les ancres Markdown, sites externes
ou PDF compilés. Les artefacts locaux restent non garantis ; les références
de corpus n'ont pas été recréées artificiellement pour faire passer le contrôle.
La publication porte sur `patch_documentation` seulement ; aucune fusion dans
`dev`/`main` ni nouveau tag de version n'est autorisé par cette passe.

Le contrôle des CSV existants a aussi révélé des empreintes périmées. L'exporteur
documentaire calcule maintenant `sha256-text-lf-v1`, explicite dans les CSV,
pour ne pas confondre conversion CRLF/LF et modification de preuve. Les autres
octets restent contrôlés, y compris les changements de valeurs. Les CSV dérivés
ont été régénérés à partir des preuves disponibles dans cette branche :
114 sources inventoriées ; le contrôle relit 138 empreintes, 286 635 valeurs
JSON exactes et 39 SVG. Le champ `bytes` de l'inventaire reste la taille physique
du fichier dans le checkout utilisé, pas une taille garantie sur tous les OS.
Les JSON de mesures déjà suivis sont inchangés, vérifié par diff Git.

Tests : 151 tests Python réussis, dont sept tests documentaires. Trois nouveaux
cas vérifient la portabilité CRLF/LF, la détection d'une donnée modifiée et
la conservation exacte d'un entier de 19 chiffres. Aucun mode de compatibilité
de l'ancien contrat CSV n'est maintenu : les dérivés se régénèrent.
La [preuve de contrôle](../../validation/raw/documentation/audit-documentation-v001-20261012T074724Z.json)
consigne ces limites ; aucun verdict photographique n'est réévalué.
