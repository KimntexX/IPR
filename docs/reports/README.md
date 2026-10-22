*[Français](README.md) | [English](README_en.md)*

# Rapports et archives

Les rapports sont des preuves historiques, pas la référence fonctionnelle du produit.

État documentaire : [audit du 22 octobre 2026](documentation-audit-2026-10-22.md),
corrections françaises, limites de couverture et préalable à la traduction anglaise.

- `sessions/` reçoit tous les nouveaux rapports de séance, nommés `session-YYYY-MM-DD.md`.
- `archive/` reçoit les journaux devenus inactifs ou consolidés.
- Les campagnes de mesure et leurs protocoles appartiennent à `../validation/`.
- Les inventaires et nettoyages du corpus appartiennent à `storage/` ; voir le [bilan du 9 octobre](storage/cleanup-2026-10-09/README.md).
- Le [nettoyage Lightroom du 9 octobre](storage/lightroom-cleanup-2026-10-09/README.md) retire les références Adobe Standard et conserve Camera Neutral.
- Le [nettoyage build du 10 octobre](storage/build-cleanup-2026-10-10/README.md) retire cinq dossiers inutiles et conserve les builds actifs et diagnostics utiles.
- Les familles historiques ont été migrées le 3 octobre 2026 ; leur correspondance est conservée dans [migration-registry.md](migration-registry.md).

Avant de clore une session, mettre à jour une spécification, la matrice de validation et un ADR lorsque la séance a produit une décision durable.

La [consolidation du 8 octobre](consolidation-2026-10-08.csv) conserve les
empreintes des sept rapports bruit/mélangeur regroupés, leurs destinations
et ancres de campagne. Les anciens chemins sont des renvois. Les journaux
datés restent historiques : un compteur de tests ancien n'est pas une anomalie
à remplacer rétroactivement par le compteur actuel.
