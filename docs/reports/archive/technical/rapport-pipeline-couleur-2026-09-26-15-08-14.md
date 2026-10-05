# Rapport de session — Pipeline couleur IPR

**Date :** 26 septembre 2026, 15:08  
**Sujet :** implémentation des orientations 1 à 6 du rapport d'architecture couleur.

## Résumé

Cette session a fait évoluer le pipeline d'image d'IPR vers un modèle explicite en flottants linéaires, avec des conversions couleur d'entrée et de sortie définies. Le décodeur RAW demande à LibRaw une sortie 16 bits linéaire ; les formats standards sont convertis depuis leur profil ICC par Qt. L'aperçu et l'export utilisent les mêmes étapes de rendu et d'encodage sRGB.

Le code compile et les tests ciblés passent sous WSL avec LibRaw activé et désactivé. Il reste à valider le rendu photographique avec des exemples réels. Une comparaison CR2/CR3 semblable, prévue pour la prochaine session, permettra d'examiner les différences liées au boîtier, à l'exposition et à la balance des blancs.

## Actions réalisées

### 1. Modèle d'image couleur explicite

Ajout de [ColorPipeline.h](../../../../src/core/ColorPipeline.h) et [ColorPipeline.cpp](../../../../src/core/ColorPipeline.cpp).

Le modèle `ColorPipeline::WorkingImage` contient :

- un `cv::Mat` RGBA flottant ;
- un espace de couleur déclaré, actuellement sRGB linéaire.

Les conversions des images standard :

- utilisent le profil `QColorSpace` lorsqu'il est présent ;
- convertissent vers sRGB linéaire ;
- supposent explicitement sRGB en l'absence de profil.

Les sorties d'erreur de conversion sont propagées, plutôt que remplacées silencieusement par une image de repli.

### 2. Décodage RAW 16 bits

Dans [RawDecoder.cpp](../../../../src/raw/RawDecoder.cpp), LibRaw est configuré pour produire du RGB 16 bits avec gamma linéaire et sans éclaircissement automatique. Les points principaux sont :

```cpp
raw.imgdata.params.no_auto_bright = 1;
raw.imgdata.params.output_bps = 16;
raw.imgdata.params.output_color = 1;
raw.imgdata.params.gamm[0] = 1.0;
raw.imgdata.params.gamm[1] = 1.0;
```

Les profils de balance CR2 et CR3 restent distincts. La sortie est convertie en flottants de travail avant la quantification, et le buffer temporaire de LibRaw est libéré après conversion.

### 3. Entrées profilées ICC

Les fichiers chargés par `QImageReader` passent maintenant par `ColorPipeline::fromSourceImage()`. Qt convertit leur profil ICC vers sRGB linéaire avant les réglages. Le cas sans profil est traité comme une entrée sRGB.

### 4. Traitements dans l'espace de travail

`ImageEditor` conserve maintenant une `WorkingImage` plutôt qu'une image 8 bits comme original de travail.

Dans `renderWorkingImage()` :

- l'exposition reste une multiplication linéaire ;
- le contraste pivote autour de 18 % de gris ;
- le réglage froid/chaud et la nuance vert/magenta sont appliqués au signal flottant ;
- les réglages ombres, noirs, hautes lumières et blancs utilisent leurs masques de luminance ;
- le flou, s'il est actif, est appliqué au buffer flottant.

Le contrôle de température a été réintroduit avec un setter dédié afin de ne pas le confondre avec l'exposition.

### 5. Conversion de sortie et export

`ColorPipeline::toSrgb()` vérifie l'espace de travail, encode en sRGB et produit l'une des sorties suivantes :

- aperçu : RGBA 8 bits ;
- JPEG : RGB 8 bits, sans alpha ;
- sortie haute profondeur : RGBA 16 bits pour PNG/TIFF.

Le pipeline de rendu est partagé entre aperçu et export. JPEG seul reçoit le réglage de qualité. Les erreurs d'encodage et d'écriture sont rapportées.

### 6. Tests

Ajout de [color_pipeline_test.cpp](../../../../tests/color_pipeline_test.cpp), couvrant :

- linéarisation d'une valeur sRGB grise ;
- préservation de l'alpha ;
- aller-retour linéaire sRGB vers l'aperçu sRGB ;
- conversion d'une entrée Adobe RGB vers l'espace de travail ;
- normalisation d'un échantillon RAW RGB 16 bits ;
- sorties JPEG RGB 8 bits et PNG RGBA 16 bits ;
- relecture PNG/JPEG et vérification de leur profil sRGB incorporé.

Le [CMakeLists.txt](../../../../CMakeLists.txt) enregistre le test sous CTest sur Linux.

La documentation [README.md](../../../../README.md) a été actualisée pour décrire le pipeline réel et la commande de test.

## Architecture obtenue

```text
Source
  ├── CR2/CR3 -> LibRaw, paramètres RAW propres au format
  │               -> RGB 16 bits linéaire
  └── JPEG/PNG/TIFF -> QImageReader + profil ICC
                  -> conversion vers sRGB linéaire
                         │
                         ▼
         ColorPipeline::WorkingImage
             RGBA float / sRGB linéaire
                         │
                         ▼
             ImageEditor::renderWorkingImage
       balance/nuance, exposition, tons, flou
                    ┌────┴────┐
                    ▼         ▼
           Aperçu sRGB 8b   Export sRGB
                            JPEG 8b RGB
                            PNG/TIFF 16b RGBA
```

`ImageProvider` expose l'aperçu final à QML. La conversion d'espace et l'encodage vers sRGB sont centralisés dans `ColorPipeline`, et non répétés dans le provider et l'exporteur.

## Validation effectuée

Deux constructions complètes WSL/Ninja ont réussi :

1. `IPR_ENABLE_LIBRAW=OFF`, `BUILD_TESTING=ON` ;
2. `IPR_ENABLE_LIBRAW=ON`, `BUILD_TESTING=ON` avec LibRaw 0.21.2.

Dans chaque configuration :

```text
1/1 test colorPipelineTests passed
```

L'analyse statique des fichiers C++, QML et du test ne signale pas d'erreur.

## Limites connues

- Aucun fichier CR2 ou CR3 représentatif n'était disponible pendant cette session : le rendu colorimétrique réel n'a donc pas été comparé visuellement.
- Le pipeline RAW utilise la matrice caméra fournie par LibRaw ; les profils DCP caméra ne sont pas intégrés.
- L'entrée RAW est désormais demandée en 16 bits gamma linéaire, mais la plage de sortie LibRaw reste bornée par son rendu RGB 16 bits ; ce n'est pas un accès direct au mosaïque RAW non dématricé.
- L'espace de travail est sRGB linéaire, pas un espace large-gamut. Les valeurs sont flottantes pendant le rendu, mais la conversion de sortie quantifie en sRGB 8 ou 16 bits et limite les canaux à `[0, 1]`.
- Le traitement de nuance reste un ajustement simple des canaux RGB, pas un contrôle calibré dans un espace perceptuel.
- Le test ne constitue pas une validation visuelle des effets d'exposition, de contraste, de température et de nuance sur des scènes photographiques.

## Prochaine session : comparaison CR2/CR3

Des fichiers RAW similaires en CR2 et CR3 permettront de séparer autant que possible les effets de format de ceux de la scène. Pour rendre l'analyse utile, fournir si possible :

- des fichiers issus du même boîtier ou, à défaut, de boîtiers Canon différents clairement identifiés ;
- une scène, un éclairage, une exposition, un objectif et une balance des blancs aussi proches que possible ;
- une charte grise ou ColorChecker, des tons chair, des ombres et des hautes lumières ;
- les métadonnées de prise de vue (boîtier, ISO, vitesse, ouverture, objectif, balance des blancs) ;
- si disponible, un rendu de référence du même RAW dans RawTherapee.

L'analyse comparera :

1. les métadonnées RAW et les dimensions décodées ;
2. les paramètres LibRaw par format ;
3. les niveaux moyens/noirs/blancs et les histogrammes par canal ;
4. les écarts des zones neutres et des tons chair ;
5. le voile, le contraste, l'écrêtage et les dominantes verte/magenta ;
6. l'aperçu IPR, l'export IPR et le rendu de référence.

Ces échantillons permettront ensuite d'ajuster les profils CR2/CR3 à partir d'éléments mesurables, plutôt que de gains couleur supposés.

