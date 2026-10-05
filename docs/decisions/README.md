# Architecture Decision Records (ADR)

Un ADR consigne un choix qui influe sur la compatibilité, le rendu, les données ou la maintenance. Il est court, daté et n'est pas réécrit pour effacer l'historique : une décision remplacée reçoit un nouvel ADR qui la référence.

| ADR | Décision | Statut |
| --- | --- | --- |
| [ADR-0001](ADR-0001-sidecar-ipr.md) | Persistance non destructive `.ipr` | Accepté |
| [ADR-0002](ADR-0002-pipeline-lineaire-et-calibration.md) | Pipeline linéaire et calibration de rendu | Accepté, à revalider sur changement de pipeline |
| [ADR-0003](ADR-0003-profils-dcp-lcp-locaux.md) | Bibliothèque locale DCP/LCP | Accepté sous contrainte de licence |
| [ADR-0004](ADR-0004-preferences-ihm-ini.md) | Préférences globales d’IHM dans un INI utilisateur | Accepté |

Utiliser [le modèle](../templates/adr-template.md) pour toute nouvelle décision.

[ADR-0005](ADR-0005-melangeur-huit-bandes-luminance-bornee.md) complète les ADR
0001/0002/0004 : huit bandes, protection des neutres, luminance bornée, sidecar
v4 et INI schéma 2 (4 octobre 2026).
