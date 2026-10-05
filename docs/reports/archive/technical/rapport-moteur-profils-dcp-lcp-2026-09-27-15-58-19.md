# Rapport de fin de session — moteur de profils DCP/LCP

**Date :** 27 septembre 2026, 15:58  
**Périmètre :** compléter le moteur DCP/LCP, le relier au décodage RAW, vérifier les corrections couleur et optiques, documenter les fonctions et valider les configurations WSL.

## Synthèse

Le moteur interne sélectionne les profils d'après les métadonnées du boîtier et de l'objectif, prend en charge des tables DCP secondaires, les matrices ColorMatrix comme solution de repli, les modèles optiques LCP multiples, le vignettage et l'aberration chromatique. Les corrections sur l'encodage des tables DCP et la convention des canaux RGB pour la CA ont été testées par des cas synthétiques.

La validation par compilation et CTest a réussi sous WSL avec LibRaw activé et désactivé (3/3 tests dans chaque configuration). Ces résultats valident les chemins de code synthétiques, mais ne certifient ni la conformité complète DNG/LCP ni la qualité colorimétrique avec des profils et RAW réels.

Aucun profil appareil/objectif n'a été inventé. L'utilisateur fournira des profils DCP/LCP officiels ou calibrés avant la validation de profils spécifiques aux appareils Canon et aux objectifs Canon/Sigma demandés.

## Problèmes identifiés et corrections

### 1. Canaux de correction d'aberration chromatique

**Problème :** les pixels exposés par ce pipeline sont RGB, mais la correction rouge-vert était associée à l'indice 2 (bleu) et la correction bleu-vert à l'indice 0 (rouge).

**Correction :** application du modèle rouge-vert au canal 0 et du modèle bleu-vert au canal 2 dans `applyLcp()`.

**Validation :** un test isolé construit une image dont le canal vert est constant et dont le rouge et le bleu varient spatialement. Le test vérifie que seul le rouge et le bleu sont remappés, tandis que le vert reste inchangé.

### 2. Valeurs LCP stockées comme éléments XML texte

**Problème :** les paramètres XML des profils LCP peuvent être fournis en attributs ou en contenu d'élément. Le parcours texte avait besoin d'une validation spécifique.

**Correction/validation :** ajout d'un profil synthétique utilisant `<RadialDistortParam1>…</RadialDistortParam1>` pour la CA rouge/bleu ; le profil est chargé et les canaux attendus sont remappés.

### 3. Encodage des tables Hue/Sat et Look DCP

**Problème :** la valeur d'encodage d'une table décrit l'espace d'entrée linéaire ou sRGB, et non une unité de décalage de teinte. Utiliser cette valeur comme échelle de HueShift produisait des résultats erronés pour les tables encodées en sRGB.

**Correction :** les tables sRGB sont désormais traitées après encodage sRGB et reconverties vers le linéaire ; les HueShift restent en degrés, quelle que soit l'option d'encodage. Les encodages autres que linéaire/sRGB sont refusés explicitement.

**Validation :** profils synthétiques distincts pour encodage linéaire et sRGB ; le test vérifie qu'ils produisent des rendus différents pour une entrée non neutre.

### 4. Interpolation circulaire des HueShift

**Problème :** une interpolation arithmétique ordinaire entre deux angles proches de +180° et -180° passe par 0°, soit un grand écart de teinte au lieu d'une interpolation locale.

**Correction :** interpolation circulaire par le chemin de plus courte amplitude, avec normalisation du delta de teinte.

**Validation :** un DCP synthétique dont les HueShift passent de +179° à -179° est testé entre les deux illuminants ; l'image interpolée reste proche du résultat d'extrémité.

### 5. Centres des modèles LCP interpolés

**Problème :** lorsque deux calibrations LCP proches sont combinées, les coefficients étaient interpolés, mais certains centres optiques ne l'étaient pas.

**Correction :** interpolation des centres de vignettage et des modèles CA rouge/bleu en plus de leurs coefficients. Lorsqu'un seul modèle possède une correction donnée, son centre est copié avec ses coefficients.

## Architecture livrée

```text
Fichier CR2/CR3
    │
    ▼
RawDecoder (registre par extension) + LibRaw
    ├─ métadonnées boîtier/objectif
    ├─ focale et ouverture disponibles
    ├─ sélection DCP caméra et LCP optique
    └─ sortie RGB 16 bits à gamma linéaire
          │
          ├─ DCP sélectionné : LCP optique, puis DCP couleur
          └─ sans DCP : rendu habituel LibRaw, puis éventuel LCP
          │
          ▼
ColorPipeline (image de travail sRGB linéaire flottante)
          │
          ├─ rendu aperçu
          └─ rendu export
```

- **Recherche :** profils installés sous `profiles/dcp` et `profiles/lcp`, dans le répertoire courant et le répertoire de données utilisateur de Qt.
- **DCP :** ForwardMatrix en priorité ; inversion/adaptation Bradford de ColorMatrix en repli ; interpolation simplifiée multi-illuminants ; HueSatMap primaire/secondaire, LookTable, courbe tonale et BaselineExposureOffset.
- **LCP :** parsing des paramètres numériques en attributs ou texte XML ; modèles optiques multiples avec choix/interpolation des deux plus proches d'après la focale, l'ouverture et, si renseignée, la distance ; correction de géométrie, vignettage, CA rouge et CA bleue.
- **Profilage RAW :** avec un DCP correspondant, le décodeur désactive le rendu matrice standard LibRaw et fournit les pixels caméra au moteur afin d'éviter une application séquentielle des deux matrices couleur.

Les détails de formats, tags, ordre des corrections, conventions et limites se trouvent dans la [documentation détaillée DCP/LCP](../../../camera-profile-engine.md).

## Documentation et fichiers modifiés

- `CameraProfileEngine.cpp` : prise en charge d'encodage de tables sRGB, HueShift circulaires, CA RGB et centres LCP interpolés.
- `tests/camera_profile_engine_test.cpp` : couverture des cas synthétiques d'encodage DCP, interpolation circulaire, XML texte LCP et séparation effective des canaux CA.
- `README.md` : capacités et limites réalignées sur l'implémentation actuelle.
- `docs/camera-profile-engine.md` : architecture et fonctionnement détaillés, choix techniques, limites et procédure de test.

## Vérifications effectuées

Commandes exécutées depuis WSL Ubuntu :

```sh
cmake --build build-wsl-libraw -j2
ctest --test-dir build-wsl-libraw --output-on-failure
cmake --build build-wsl-nolibraw -j2
ctest --test-dir build-wsl-nolibraw --output-on-failure
```

| Configuration | Compilation | CTest |
|---|---|---|
| `build-wsl-libraw` | Réussie | 3/3 tests réussis |
| `build-wsl-nolibraw` | Réussie | 3/3 tests réussis |

Les tests sont `colorPipelineTests`, `rawDecoderTests` et `cameraProfileEngineTests`.

## Profils appareil/objectif : état et données attendues

Aucun profil Canon ou Sigma spécifique n'est livré par cette session. Un profil fiable ne peut pas être produit seulement à partir du nom d'un boîtier ou d'un objectif. Générer des matrices de démonstration en les présentant comme calibrées serait trompeur et pourrait empirer les dérives de couleur ou la correction optique.

L'utilisateur a choisi de fournir des profils officiels ou calibrés. À réception, il faudra :

1. déposer les fichiers dans les répertoires DCP/LCP attendus et contrôler leurs droits de redistribution ;
2. comparer leurs restrictions caméra/objectif aux chaînes réellement rapportées par LibRaw ;
3. tester plusieurs RAW réels, illuminants et réglages de prise de vue ;
4. comparer au logiciel de référence avec les mêmes paramètres de balance des blancs et de rendu ;
5. corriger toute incompatibilité constatée avant d'annoncer un profil pris en charge.

Les noms exacts des boîtiers « Canon D » restent à préciser. Pour un profil LCP objectif, les variantes de monture et la désignation complète du modèle devront également être confirmées.

## Travaux restant à faire

1. Valider DCP/LCP sur des profils réels autorisés et des RAW Canon/Sigma représentatifs ; examiner les valeurs caméra/objectif exactes fournies par LibRaw.
2. Évaluer la conformité du moteur sur un jeu d'images de référence : profils DCP avec CameraCalibration/AnalogBalance, toutes les fonctions de profil requises, différentes versions de XML LCP, géométrie, CA et vignettage.
3. Compléter le transport des métadonnées de capture, notamment la distance de mise au point lorsque le conteneur et le backend la fournissent.
4. Comparer l'interpolation des grilles optiques à la définition des profils LCP ; l'algorithme présent ne remplace pas nécessairement l'interpolation complète attendue par les profils.
5. Après validation sur données réelles, décider si le moteur interne limité suffit ou s'il faut intégrer un SDK externe ; toute dépendance devra être évaluée séparément pour licence, redistribution et compatibilité avec l'objectif LGPL.
6. Les autres chantiers du projet restent indépendants de ce moteur : gestion du bruit, courbes de tonalité utilisateur et correctif/validation de l'export.

