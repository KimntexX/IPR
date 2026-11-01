# ADR-0017 — `ImageEditor` cadenceur de modules de traitement

- **Date :** 2026-10-19
- **Statut :** Accepté ; première étape appliquée sur `patch_light`
- **Décideurs :** utilisateur ; application sur `patch_light`

## Contexte

`imageeditor.cpp` mêlait l'état de l'interface, le cache, le chargement et
le traitement d'image : environ 2 900 lignes, dont des boucles par pixel et une
quinzaine de lambdas de traitement dans `renderWorkingImage`. La fonction de
transfert sRGB y était écrite en double, comme dans `ColorPipeline.cpp` et
`CameraProfileEngine.cpp`. Le module des réglages tonals tenait entièrement
dans un en-tête : toute modification du modèle recompilait l'éditeur. Les
gardes d'inclusion mélangeaient `#pragma once` et des noms sans convention.

L'utilisateur prévoit d'autres panneaux, comme dans Lightroom : calques et
filtres, géométrie, suppression sur l'image. Le code doit sortir harmonieux de
chaque validation pour rester maintenable.

## Décision

- `ImageEditor` devient un **cadenceur** : il porte l'état de l'interface, le
  cache, le chargement et l'enchaînement des traitements, sans traitement
  d'image. `renderWorkingImage` appelle des modules dans un ordre fixe.
- Chaque traitement est un **module** sous `src/core/` : interface dans le `.h`,
  implémentation dans le `.cpp`. Une boucle par pixel et les fonctions qu'elle
  appelle vivent dans le même `.cpp`, ce qui leur permet d'être insérées en
  ligne sans optimisation à l'édition de liens.
- Sans état ni ressource à porter, un module expose des **fonctions libres**
  dans un espace de noms ; une classe seulement pour un état ou un invariant
  (par exemple `ToneRecovery::Curve`, `ImageCacheController`).
- Les lambdas liés à Qt (slots, tâches `QtConcurrent`, `qScopeGuard`) et aux
  algorithmes `std` restent des lambdas ; les autres deviennent des fonctions.
- Gardes d'inclusion standard `IPR_<CHEMIN>_<FICHIER>_H`, sans `#pragma once`.

Ces règles sont inscrites dans [AGENTS.md](../../AGENTS.md) (règle du 19 octobre 2026).

### Modules issus de la première étape

| Module | Contenu |
| --- | --- |
| `ToneRecovery` (+ `ToneStatistics`) | ombres, noirs, HL, blancs ; mesure des statistiques par image |
| `Contrast` | courbe de contraste |
| `ColorAdjustments` | niveaux de gris, saturation et vibrance, mélangeur et étalonnage |
| `DetailProcessing` | bruit et netteté (existants), texture, clarté, voile, grain |
| `ToneCurve` | courbes de tonalité et leur type de point |
| `Histogram` | histogramme de l'aperçu |
| `ImageExporter` | formats, noms libres, DNG linéaire, écriture puis publication |
| `Sidecar` (+ `EditRecipe.h`) | lecture et écriture du `.ipr`, données de l'édition |
| `WorkingColor`, `ImageMath` | transfert sRGB unique, luminance, `smoothStep` |

## Conséquences

- Bénéfices : `imageeditor.cpp` passe de 2 938 à environ 2 100 lignes ;
  `renderWorkingImage` ne fait plus que cadencer ; chaque module se teste et se
  recompile seul ; le modèle généré des tons n'est inclus que par
  `ToneRecovery.cpp`.
- Coûts et risques : un rendu différent au bit près aurait été invisible aux
  tests fonctionnels. Mitigation : mode `equivalence` de `openingLatencyProbe`,
  empreintes SHA-256 du master, de l'aperçu et de l'histogramme pour 22 jeux de
  réglages sur 6V1A0043 et 6V1A0524, identiques avant et après chaque étape
  (rendu déterministe vérifié par deux passages).
- Compatibilité / migration : aucune. Format `.ipr` inchangé (version 6),
  messages d'export inchangés, API QML inchangée ; `ImageEditor` garde ses
  types par alias (`Settings`, `RawDevelopment`, `UserImageMetadata`,
  `ExportResult`, `ToneCurvePoint`).
- Validation requise : aucune visuelle (rendu identique). Restent hors de cette
  étape les lambdas de traitement des autres fichiers (`CameraProfileEngine`,
  `raw/`, `cache/`) et la sortie d'autres responsabilités d'`ImageEditor`
  (chargement, cache, sélection), à reprendre au fil des évolutions.

## Liens

- Code : `src/core/`, `src/editor/imageeditor.cpp`
- Documentation : [rendu `renderWorkingImage`](../imageeditor-render-working-image.md)
- Tests / protocole : suite de 27 tests ; `openingLatencyProbe equivalence`
- ADR liés : [ADR-0001](ADR-0001-sidecar-ipr.md) (sidecar), [ADR-0009](ADR-0009-espace-travail-rec2020-lineaire.md) (espace de travail)
