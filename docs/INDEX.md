*[Français](INDEX.md) | [English](INDEX_en.md)*

# Index documentaire IPR

Ce fichier est le point d'entrée unique de la documentation versionnée. Il distingue la documentation qui définit le produit, les preuves qui la valident et les journaux qui expliquent son évolution.

| Domaine | Rôle | Point d'entrée |
| --- | --- | --- |
| Architecture | Structure durable, contrats et diagrammes | [architecture/README.md](architecture/README.md) |
| Spécifications | Comportement attendu, indépendamment d'une séance de travail | [specifications/README.md](specifications/README.md) |
| Décisions | Choix d'architecture stables et leurs conséquences | [decisions/README.md](decisions/README.md) |
| Validation | Protocoles, matrice de statut et mesures reproductibles | [validation/README.md](validation/README.md) |
| Rapports | Journaux de séance et historiques datés | [reports/README.md](reports/README.md) |
| Technique | Manuel LaTeX de référence et sources compilables | [technical/latex/README.md](technical/latex/README.md) |
| Scholar | Cours sur les modèles utilisés par IPR | [scholar/README.md](scholar/README.md) |

## Règles de gouvernance

Mise en forme des documents : [conventions LaTeX communes](AGENTS.md).

Cours de fond : [architecture des images numériques — PDF](scholar/cours-11-architecture-images.pdf)
et [travaux pratiques associés](scholar/README.md#cours-11--architecture-des-images).

Guide pratique : [tester les fonctionnalités en autonomie](validation/procedure-tests-autonomes.md).

Suivi : [CSV et courbes](validation/metrics/README.md),
[reste à faire priorisé](reports/sessions/session-2026-10-08.md).

1. Un rapport de séance enregistre des faits datés ; il ne devient pas une spécification par accumulation.
2. Une fonctionnalité terminée met à jour sa spécification, la matrice de validation et, si nécessaire, un ADR.
3. Les fichiers bruts de mesure restent immuables et sont séparés de leurs conclusions.
4. Une décision qui impacte la compatibilité, le rendu ou le format de données reçoit un ADR numéroté.
5. Les sources Markdown/LaTeX sont versionnées. Les sorties `build/`, caches et fichiers synchronisés sont générés localement et ignorés par Git. Les PDF Scholar constituent l'exception : ils sont produits à la racine de `scholar/` pour une consultation directe.

## Migration achevée

Le contenu historique, auparavant regroupé dans `docs/rapport/`, a été distribué le 3 octobre 2026 dans les zones de responsabilité appropriées. Cet ancien dossier a ensuite été supprimé. Le registre conserve la correspondance source-destination : [reports/migration-registry.md](reports/migration-registry.md).
