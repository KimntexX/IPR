# Registre de migration documentaire

**Statut :** migration achevée le 3 octobre 2026. Les fichiers ont été déplacés sans modification de leur contenu, hormis les liens locaux devenus nécessaires après changement de profondeur.

| Famille source `docs/rapport/` | Destination | Résultat |
| --- | --- | --- |
| `rapport-session-*.md` | [sessions/](sessions/) | 8 journaux historiques migrés ; les nouvelles sessions utilisent `session-YYYY-MM-DD.md`. |
| `protocole-validation-*.md` | [../validation/protocols/](../validation/protocols/) | 7 protocoles migrés. |
| `analyse-*.md` | [../validation/analyses/](../validation/analyses/) | 11 analyses migrées. |
| `mesure-*-brut-*.txt` | [../validation/raw/](../validation/raw/) | 16 sorties brutes transférées sans altération. |
| `rapport-*.md` techniques | [archive/technical/](archive/technical/) | 11 rapports historiques migrés. |
| `export-codex-*.md` | [archive/exports/](archive/exports/) | 1 export de travail migré. |
| dépendances, DCP et profils Adobe | [../architecture/references/](../architecture/references/) | 3 références d'architecture migrées. |
| recherches théoriques | [../scholar/references/](../scholar/references/) | 1 étude et sa bibliographie migrées. |

## Convention pour la suite

- Source de référence : nom fonctionnel sans date, par exemple `reglages-photographiques.md`.
- Événement : `session-YYYY-MM-DD.md`.
- Protocole : `protocole-<fonction>-vN.md`.
- Analyse : `analyse-<fonction>-YYYY-MM-DD.md`.
- Mesure brute : `mesure-<fonction>-YYYY-MM-DD.<ext>`.
- ADR : `ADR-NNNN-sujet-kebab-case.md`.

## Traçabilité des liens historiques

Trois rapports décrivaient l'ancien `Cr3Decoder.cpp`. Lors de la migration, leurs liens ont été corrigés vers `RawDecoder.cpp`, qui a repris ce backend. Les chemins absolus du journal du 25 septembre ont aussi été remplacés par des liens relatifs portables.
