*[Français](README.md) | [English](README_en.md)*

# Architecture

Cette zone contient les documents de référence qui décrivent les composants, contrats de données et flux de rendu, sans résultats de campagne ni journal de séance.

## Références actuelles

- [Manuel technique LaTeX](../technical/latex/README.md) : diagrammes utilisateur, classes et séquences, pipeline et glossaire.
- [Moteur de rendu et image de travail](../imageeditor-render-working-image.md) : contrat `WorkingImage`, ordre des opérations et invariants colorimétriques.
- [Moteur de profils appareil](../camera-profile-engine.md) : DCP/LCP, sélection locale et limites.
- [Graphe du chemin RAW → DCP → rendu](chemin-ouverture-raw.md) : code actuel, refus de calibration et architecture cible sans liste de photos.
- [Formats extensibles et stockage des caractérisations](../decisions/ADR-0012-formats-extensibles-et-caracterisations.md) : point d'entrée des futurs adaptateurs, DCP/JSON/SQLite, premier lot et transition restante.
- [Roadmap ouverture RAW](roadmap-ouverture-raw.md) : HL, contextes multi-boîtiers, caches et corpus R5/6D Mark II prévu.
- [Roadmap Lumières](roadmap-lumieres.md) : état des six réglages, plan A en cours, plan B adaptatif tracé, points reportés.
- [Travail, affichage ICC et export](gestion-couleurs-affichage.md) : séparation cible, distincte du code actuel.
- [Intégration Rec.2020 linéaire](integration-rec2020.md) : master porté, audit initial, contrats et qualification restant à compléter.
- [Recette RAW et changement de profil](../decisions/ADR-0006-restauration-raw-et-retouche-profil.md) : sidecar v5, tampon pré-DCP et réinitialisations distinctes.
- [Guide détaillé du moteur CPU](../scholar/moteur-graphique-ipr.tex) : explication des constantes, asynchronisme et sidecars.
- [Références architecture et profils](references/) : licences des dépendances, création de DCP et note d'utilisation des profils Adobe.
- [CI et build multiplateforme](ci-et-build-multiplateforme.md) : matrice Windows/macOS/Linux, prérequis, packaging et feuille de route GitHub.
- [Réduction de bruit et netteté](denoising-sharpening.md) : algorithmes, ordre de pipeline, limites IA/GPU et validation attendue.
- [Arborescence des sources](../../src/README.md) : domaines C++/QML et bibliothèque interne `iprEngine`.
- [Préférences de l’interface](ui-preferences.md) : INI utilisateur, géométrie, sections, couleurs et schéma extensible.
- [Build Windows autonome](build-windows-standalone.md) : configuration portable, dépendances et restrictions sur les profils tiers.
- [Mesures et courbes](../validation/metrics/README.md) : provenance des CSV et limites de comparaison.

[Import commun des formats](import-images.md) : adaptateurs RAW/raster,
contrat Rec.2020, métadonnées, capacités et validation du cache/des profils.

[Cache des profils préparés](../decisions/ADR-0014-cache-profils-prepares.md) :
LRU partagé DCP/LCP, 64 Mio/16 entrées, concurrence et invalidation par contenu.

## Règle de mise à jour

[Suivi des raccordements IHM](suivi-raccordements-ihm.md) : chantier isolé
`patch_ui`, demandes d’interfaces moteur et validation des connexions.

[Contrôle mémoire et navigation](controle-memoire-images.md) : propriété du LRU,
réservations et préchargement annulable, avec limites du seuil global de RAM.

Une modification des responsabilités de `ImageEditor`, `ColorPipeline`, `RawDecoder`, `CameraProfileEngine`, du cache ou du sidecar doit mettre à jour le manuel LaTeX et, si le choix est durable, créer ou modifier un ADR.
