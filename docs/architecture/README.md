# Architecture

Cette zone contient les documents de référence qui décrivent les composants, contrats de données et flux de rendu, sans résultats de campagne ni journal de séance.

## Références actuelles

- [Manuel technique LaTeX](../technical/latex/README.md) : diagrammes utilisateur, classes et séquences, pipeline et glossaire.
- [Moteur de rendu et image de travail](../imageeditor-render-working-image.md) : contrat `WorkingImage`, ordre des opérations et invariants colorimétriques.
- [Moteur de profils appareil](../camera-profile-engine.md) : DCP/LCP, sélection locale et limites.
- [Guide détaillé du moteur CPU](../scholar/moteur-graphique-ipr.tex) : explication des constantes, asynchronisme et sidecars.
- [Références architecture et profils](references/) : licences des dépendances, création de DCP et note d'utilisation des profils Adobe.
- [CI et build multiplateforme](ci-et-build-multiplateforme.md) : matrice Windows/macOS/Linux, prérequis, packaging et feuille de route GitHub.
- [Réduction de bruit et netteté](denoising-sharpening.md) : algorithmes, ordre de pipeline, limites IA/GPU et validation attendue.
- [Arborescence des sources](../../src/README.md) : domaines C++/QML et bibliothèque interne `iprEngine`.
- [Préférences de l’interface](ui-preferences.md) : INI utilisateur, géométrie, sections, couleurs et schéma extensible.

## Règle de mise à jour

Une modification des responsabilités de `ImageEditor`, `ColorPipeline`, `RawDecoder`, `CameraProfileEngine`, du cache ou du sidecar doit mettre à jour le manuel LaTeX et, si le choix est durable, créer ou modifier un ADR.
