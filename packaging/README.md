# Paquets IPR

Deux modes : `standalone` embarque les bibliothèques et les modules QML ;
`system` nécessite les versions compatibles installées sur le système.
Sur Windows, leurs DLL doivent être accessibles dans PATH ; sur macOS, utiliser
les dépendances Homebrew de même architecture ; sur Linux, préférer le paquet
natif de sa distribution. Un binaire Linux n'est pas universel entre distributions.

Les profils DCP/LCP et les calibrations privées ne sont jamais distribués par la
CI. Les dossiers de profils sont intentionnellement vides. Ajouter uniquement
des profils dont on dispose légalement, sans les republier par défaut.

Les archives contiennent une mémoire JSON de construction, un inventaire SHA-256,
les notices de licences et un test de démarrage hors du dépôt. Ce test n'est pas
une validation photographique ni un test sur une machine vierge.
Les applications ne sont pas signées par un certificat de distribution ; macOS
reçoit seulement une signature ad hoc. Aucun installateur ni notarisation.

Le standalone Linux conserve les bibliothèques de base du système (glibc,
chargeur, pilotes). Ubuntu 24.04 est sa cible initiale ; pas une garantie de
compatibilité avec Debian 12 ou une version antérieure de glibc.

Sources : https://github.com/KimntexX/IPR
Instructions : docs/architecture/ci-et-build-multiplateforme.md
Avant une publication publique, vérifier licences, obligations LGPL et accès
aux sources correspondantes des dépendances ; le manifeste n'est pas un SBOM
juridique complet. Le dépôt ne déclare pas automatiquement une licence du projet.
