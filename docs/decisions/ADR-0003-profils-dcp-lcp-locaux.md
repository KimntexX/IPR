# ADR-0003 — Bibliothèque locale de profils DCP/LCP

- **Date :** 3 octobre 2026
- **Statut :** Accepté sous contrainte de provenance et de licence
- **Décideurs :** Projet IPR

## Contexte

Une recherche récursive de profils au chargement ralentit l'ouverture. Les profils appareil DCP et objectif LCP sont nécessaires à une partie de la fidélité de développement RAW, mais leur distribution peut être juridiquement limitée selon leur origine.

## Décision

Conserver les profils explicitement autorisés dans `profiles/dcp/` et `profiles/lcp/`, avec un index `profiles/index.json`. Utiliser l'index en priorité, puis les répertoires configurés comme repli. Ne pas ajouter au dépôt ni redistribuer de profils tiers dont la licence n'autorise pas ce droit.

## Conséquences

- La résolution des profils est plus rapide et déterministe.
- L'index doit être mis à jour lorsqu'un profil est ajouté ou retiré.
- Chaque profil distribué doit avoir une provenance, une licence et une décision de distribution vérifiables.
- La solution de long terme peut être des profils créés par IPR ou par une chaîne ouverte documentée, sans dépendre de profils Adobe.

## Validation et liens

- [Documentation DCP/LCP](../camera-profile-engine.md)
- [Cours RAW et profils](../scholar/cours-09-raw-profils-appareil.tex)
- [Note historique Adobe](../architecture/references/note-utilisation-profils-adobe-camera-raw-2026-10-01.md)
