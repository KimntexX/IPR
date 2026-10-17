*[Français](README.md) | [English](README_en.md)*

# Architecture Decision Records (ADR)

Un ADR consigne un choix qui influe sur la compatibilité, le rendu, les données ou la maintenance. Il est court, daté et n'est pas réécrit pour effacer l'historique : une décision remplacée reçoit un nouvel ADR qui la référence.

| ADR | Décision | Statut |
| --- | --- | --- |
| [ADR-0001](ADR-0001-sidecar-ipr.md) | Persistance non destructive `.ipr` | Accepté |
| [ADR-0002](ADR-0002-pipeline-lineaire-et-calibration.md) | Pipeline linéaire et calibration de rendu | Accepté, à revalider sur changement de pipeline |
| [ADR-0003](ADR-0003-profils-dcp-lcp-locaux.md) | Bibliothèque locale DCP/LCP | Accepté sous contrainte de licence |
| [ADR-0004](ADR-0004-preferences-ihm-ini.md) | Préférences globales d’IHM dans un INI utilisateur | Accepté |
| [ADR-0006](ADR-0006-restauration-raw-et-retouche-profil.md) | Sidecar v5 et changement DCP sans réouverture visuelle | Accepté ; recette visuelle à compléter |
| [ADR-0007](ADR-0007-calibration-raw-par-defaut.md) | Calibration RAW active par défaut ; désactivation dans les futurs paramètres avancés | Défaut actif implémenté ; panneau avancé restant |
| [ADR-0008](ADR-0008-separation-travail-affichage-export.md) | Travail indépendant de l'écran, affichage ICC et export séparés | Directive acceptée ; implémentation à venir |
| [ADR-0009](ADR-0009-espace-travail-rec2020-lineaire.md) | Master Rec.2020 linéaire D65 flottant | Port local implémenté ; qualification visuelle et multi-OS à compléter |
| [ADR-0010](ADR-0010-compensation-rendu-par-profil-dcp.md) | Compensation de rendu séparée de la baseline source, liée au boîtier/DCP | Intégré ; portée étendue par ADR-0011 |
| [ADR-0011](ADR-0011-regles-raw-par-mode-et-configuration-vivante.md) | Règles par modèle/mode capteur, sans liste de photos ; configuration suivie automatiquement | Accepté, intégré localement ; ouverture des nouveaux ISO à l'essai |
| [ADR-0012](ADR-0012-formats-extensibles-et-caracterisations.md) | Adaptateurs de formats/vignettes et catalogue JSON de caractérisations distinct du rôle DCP | Premier lot local ; contrats caméra et caractérisation physique restants |

Utiliser [le modèle](../templates/adr-template.md) pour toute nouvelle décision.

[ADR-0014](ADR-0014-cache-profils-prepares.md) : cache LRU partagé DCP/LCP,
identité de contenu, préparation concurrente et limites de mémoire.

[ADR-0015](ADR-0015-controle-memoire-et-prechargement.md) : contrôleur dédié,
seuil global 80/75 %, réservations, cache d’images adaptatif et préchargement.

[ADR-0016](ADR-0016-minimum-qt-6-8.md) : Qt 6.8 minimum pour tous les modes,
CI Linux sur SDK Qt 6.8.3, `.deb` sur Debian 13 et plus de chemin Qt < 6.8.

[ADR-0013](ADR-0013-contrat-import-commun.md) : frontière d'import commune aux
RAW et aux images développées, métadonnées indépendantes et contrôles partagés.

[ADR-0005](ADR-0005-melangeur-huit-bandes-luminance-bornee.md) complète les ADR
0001/0002/0004 : huit bandes, protection des neutres, luminance bornée, sidecar
v4 et INI schéma 2 (4 octobre 2026).
