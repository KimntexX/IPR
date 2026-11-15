*[Français](ADR-0002-pipeline-lineaire-et-calibration.md) | [English](ADR-0002-pipeline-lineaire-et-calibration_en.md)*

# ADR-0002 — Pipeline linéaire et calibration de rendu

- **Date :** 3 octobre 2026
- **Statut :** Accepté ; les paramètres empiriques sont à revalider à chaque changement de pipeline
- **Décideurs :** Projet IPR

## Contexte

**Portée historique :** la décision sRGB ci-dessous date du3octobre. Le master
courant est Rec.2020 linéaire depuis l'[ADR-0009](ADR-0009-espace-travail-rec2020-lineaire.md).
Ne pas lire les constantes Rec.709 de cette décision initiale comme celles
du master actuel ; les domaines intermédiaires restent explicites par module.

Les réglages photométriques, filtres et gains doivent être appliqués dans un espace où les valeurs sont proportionnelles à l'énergie. Les comparaisons de validation ont aussi montré qu'une adaptation chromatique purement théorique ne reproduit pas parfaitement les références aux températures froides.

## Décision

Utiliser `WorkingImage` en RGBA flottant sRGB linéaire. Effectuer le rendu avant l'encodage sRGB, puis compresser le gamut avant la quantification. Employer Krystek + Bradford pour la température et conserver une compensation froide empirique, explicitement documentée et limitée à la branche sous la référence de prise de vue.

## Conséquences

- Les gains d'exposition, contraste local et opérations de voile ont une base mathématique cohérente.
- Les constantes Rec.709, matrices sRGB et Bradford sont des références de modèle ; elles ne doivent pas être modifiées pour ajuster une image.
- Les amplitudes tonales, sigmas, facteurs de grading et compensation froide sont des calibrations ; elles nécessitent un corpus, une mesure et une mise à jour de la matrice de validation.
- Toute migration vers un espace perceptuel ou une nouvelle stratégie de gamut mapping requiert une comparaison de compatibilité de rendu des sidecars.

## Validation et liens

- [Spécification des réglages](../specifications/reglages-photographiques.md)
- [Cours et guide des constantes](../scholar/README.md)
- [Matrice de validation](../validation/matrice-validation.md)
