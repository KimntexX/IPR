# IPR — Image Processing Room

Application Qt Quick/QML en C++17 pour parcourir un dossier d'images, appliquer quelques réglages et exporter le résultat.

## Fonctions disponibles

- ouverture d'un dossier local et liste des fichiers JPG, JPEG, PNG, TIFF et RAW enregistrés ;
- chargement de JPG/PNG/TIFF avec respect de l'orientation EXIF ;
- import CR2/CR3/DNG lorsqu'elle est compilée avec **LibRaw** ;
- réglages non destructifs : exposition (±5 EV), contraste, ombres, noirs, hautes lumières, blancs, quatre courbes de tonalité (luminance, rouge, vert et bleu), histogramme RGBl, balance des blancs, température de 2000 à 50000 K (initialisée sur la balance des blancs détectée dans le fichier source, ou 6500 K à défaut), nuance de -150 à 150 (vert ↔ magenta), vibrance, saturation, texture, clarté, correction du voile, grain et flou gaussien ;
- rendu et export hors du thread de l'interface ;
- export JPEG, PNG, TIFF ou DNG linéaire rendu ; le dialogue propose par défaut `nom-source-IPR.<format>` dans le dossier de l’image.

Le pipeline de travail est une image `CV_32FC4` RGBA à alpha droit en sRGB linéaire ; son espace colorimétrique et la provenance du profil ou de la transformation source sont conservés avec les pixels. Les fichiers JPEG/PNG/TIFF passent par le profil ICC fourni par Qt lorsqu'il est valide ; en son absence, IPR interprète l'entrée comme sRGB et signale cette hypothèse. Les fichiers RAW sont développés par LibRaw en RGB 16 bits linéaire ; le DCP sélectionné, ou la matrice couleur de LibRaw en son absence, fournit la transformation vers sRGB linéaire. L'aperçu et tous les exports passent par la même compression de gamut sRGB : elle conserve la luminance et réduit la chroma vers un neutre de même luminance lorsqu'une couleur est hors gamut, plutôt que d'écrêter indépendamment les canaux. L’export DNG produit une image RGB linéaire 16 bits avec les réglages IPR intégrés, des balises DNG et un profil sRGB ; il ne recopie ni la mosaïque RAW, ni les métadonnées de prise de vue, ni la latitude de développement du fichier d’origine. Les transformations RAW et les réglages peuvent produire des valeurs flottantes hors de [0, 1] ; la compression de gamut de sortie les ramène dans sRGB, sans tone mapping HDR dédié. L'espace de travail reste sRGB linéaire : un gamut plus large attend l'adaptation et la validation de toute la chaîne DCP/réglages/sortie. La température couleur se règle entre 2000 K et 50000 K ; à l'ouverture d'une image, elle est initialisée sur la balance des blancs détectée dans le fichier source (estimée par LibRaw pour les RAW, ou lue dans les métadonnées disponibles pour les autres formats), afin d’éviter une mauvaise interprétation initiale des couleurs, avec un repli à 6500 K si aucune valeur fiable n'est disponible ; la nuance va de -150 (vert) à +150 (magenta). Ces réglages agissent après décodage et ne remplacent pas une balance des blancs RAW colorimétriquement complète.

Pour les CR3 Canon dont LibRaw ne fournit pas de niveau noir utilisable, le décodeur estime un niveau par photosite CFA à partir des marges masquées du capteur avant le développement. Cette correction de repli n'est appliquée que si aucun niveau noir DNG ou niveau de motif n'est présent et si les marges fournissent des mesures plausibles. L'algorithme est isolé dans `RawBlackLevelEstimator` et dispose de tests synthétiques ; le corpus RAW local documenté, sans copie des photos dans le dépôt, se trouve dans [`tests/corpus/raw-black-level/`](tests/corpus/raw-black-level/). Les décodeurs RAW sont enregistrés par extension, ce qui permet d'ajouter un backend propre à un format ou à un fabricant sans changer le pipeline de rendu. Les décodeurs peuvent aussi rapporter le format, le boîtier et l'objectif pour alimenter la sélection des profils ; ces métadonnées sont affichées avec les dimensions de l'image.

Les réglages restent non destructifs : l'image de travail dérive de l'original conservé en mémoire et ne modifie jamais le fichier source.

Les réglages s'ouvrent depuis le bouton ou le menu **Réglages**. Le module
d'histogramme et de courbe se trouve à la fin de la section **Lumière**. Les
pastilles gris, rouge, verte et bleue sélectionnent respectivement les courbes
Luminance, Rouge, Vert et Bleu. Un double-clic ajoute une poignée, un clic droit
supprime une poignée interne et les boutons de réinitialisation restaurent la
diagonale d'identité. La saturation agit uniformément sur la chrominance ;
la vibrance privilégie les couleurs peu saturées et préserve davantage les
couleurs déjà vives. La section **Effets** contient la texture (micro-détails),
la clarté (contraste local de rayon plus large), la correction du voile et un
grain monochrome stable entre deux rendus.

## Dépendances

- Qt 6.4 ou plus récent : modules Core, Gui, Quick, QML et Concurrent ;
- OpenCV : `core`, `imgproc`, `imgcodecs` ;
- LibTIFF : export DNG linéaire ;
- LibRaw 0.21+ est **facultatif** mais requis pour CR2/CR3/DNG.

Qt ne décode pas les RAW et OpenCV n'est pas un décodeur RAW. LibRaw fournit l'import CR2/CR3 et DNG ; son rendu de base convient à cette application mais il ne remplace pas un moteur RAW photo spécialisé.

Pour éviter un scan récursif de milliers de profils, `profiles/index.json` peut associer directement un boîtier/objectif à un DCP/LCP local sous `profiles/dcp` ou `profiles/lcp`. Seuls le manifeste et la structure des dossiers sont versionnés : les profils eux-mêmes sont ignorés par Git tant que leur droit de redistribution n'a pas été établi.

Un moteur interne sélectionne les profils DCP caméra et LCP optiques dans `profiles/dcp` et `profiles/lcp`, sous le répertoire courant puis le répertoire de données utilisateur. Sous Windows, il recherche aussi directement les profils installés par Adobe dans `%ProgramData%\\Adobe\\CameraRaw\\CameraProfiles` et `%ProgramData%\\Adobe\\CameraRaw\\LensProfiles`. Sous WSL, il détecte `/mnt/c/ProgramData/Adobe/CameraRaw` si ce dossier est monté ; pour tout autre chemin, définir `IPR_ADOBE_CAMERARAW_DIR` vers la racine `CameraRaw`. Les fichiers restent à leur emplacement d'origine et ne sont pas copiés dans le projet. L'implémentation comprend matrices DCP ForwardMatrix avec repli ColorMatrix, interpolation simplifiée multi-illuminants, tables Hue/Sat et Look, courbe tonale et compensation d'exposition de base. Le parseur LCP prend en charge plusieurs modèles optiques, sélection/interpolation approximative selon les paramètres de capture, distorsion, vignettage et remappage CA rouge/bleu.

Ces fonctions ne constituent pas encore une implémentation certifiée de l'intégralité des spécifications DNG/LCP. Les températures de balance de blancs, l'interpolation de matrices, le repli ColorMatrix et la sélection optique ont des limites documentées ; la distance de mise au point n'est pas encore fournie par LibRaw. Les tests automatisés utilisent uniquement des profils synthétiques et aucun profil Canon/Sigma calibré n'est livré dans le dépôt. Voir la [documentation détaillée du moteur DCP/LCP](docs/camera-profile-engine.md) avant de déployer des profils réels.

### Windows : OpenCV et vcpkg

Consulter le guide complet [d'installation OpenCV sous Windows](docs/installation-opencv-windows.md). Il détaille les deux parcours compatibles :

- **Qt MSVC 64 bits + vcpkg `x64-windows`** ;
- **Qt MinGW 64 bits + compilation manuelle d'OpenCV avec le MinGW fourni par Qt**.

Ne mélangez pas des bibliothèques MSVC avec un exécutable MinGW. Le kit historique du projet étant Qt MinGW, le second parcours est le choix direct.

## Licences

L'inventaire des dépendances, les textes des licences et la comparaison des options de licence pour IPR sont dans [`licenses/`](licenses/README.md). Aucun fichier `LICENSE` à la racine n'a encore été choisi pour le code d'IPR ; la licence des dépendances ne détermine pas automatiquement celle du projet.

## Architecture

L’état global de l’interface est conservé dans un `ipr-ui.ini` utilisateur,
indépendant des sidecars photo. Le menu **Affichage → Fichier de préférences…**
affiche son emplacement. Voir le [contrat et la procédure de personnalisation](docs/architecture/ui-preferences.md)
et le [modèle embarqué](config/ipr-ui.ini).

`ImageEditor` possède l'image de travail linéaire, les métadonnées source et les paramètres. `RawDecoder` sélectionne un décodeur enregistré selon l'extension et fournit les métadonnées disponibles ; le backend LibRaw traite actuellement CR2 et CR3. `CameraProfileEngine` sélectionne les profils DCP/LCP, puis applique les transformations couleur et optiques au décodage RAW. `ColorPipeline` normalise les entrées et encode les sorties sRGB. Le rendu OpenCV est lancé en arrière-plan et partagé entre aperçu et export. `ImageProvider` expose l'aperçu sous l'URL QML `image://editor/preview?revision=…`. Le numéro de révision évite que le cache QML n'affiche un aperçu dépassé.

Le détail des transformations tonales et couleur, de la reconstruction RGBA, du modèle d'asynchronisme et du choix d'un retour `WorkingImage` par valeur est expliqué dans [la documentation de `ImageEditor::renderWorkingImage`](docs/imageeditor-render-working-image.md).

### Build Windows et tests ciblés

Depuis WSL ou Git Bash, application fermée :

```bash
bash scripts/build-windows.sh
```

Les tests sont sélectionnés depuis le dernier code validé, y compris les
modifications non commités. Premier passage, grosse modification et CI GitHub :
suite complète. Pour forcer celle-ci : `bash scripts/build-windows.sh --full-tests`.
Voir [les prérequis, règles et options](docs/architecture/tests-cibles-et-build-windows.md).

### Tests (Linux/WSL, validation complète)

```sh
cmake -S . -B build -G Ninja -DIPR_ENABLE_LIBRAW=ON -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Lorsque le corpus local `tests/image/6V1A9933.CR3` et son JPEG Lightroom
associé sont disponibles, CTest ajoute
`tonalReferenceComparisonProbe`. Cette sonde rend le CR3 avec
la base de référence fixe suivante : balance des blancs *As Shot*,
`exposition=-0,49`, `contraste=41` et les quatre réglages tonals à `-100`.
Elle compare aussi une variante automatique au JPEG et échoue si la variante
*As Shot* dépasse un écart RGB moyen de 25 niveaux. Les sorties de contrôle
restent dans le répertoire de build.
