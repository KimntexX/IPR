*[Français](README.md) | [English](README_en.md)*

# IPR — Image Processing Room

IPR est une application de développement et de retouche photographique non destructive,
écrite en **C++17 avec Qt Quick/QML**. Elle permet de parcourir un dossier d'images,
développer des RAW, ajuster le rendu et exporter une image sans modifier l'original.

Le projet est en développement actif. **Fonction disponible ne signifie pas qualité
entièrement validée** : les résultats et limites sont suivis dans la
[matrice de validation](docs/validation/matrice-validation.md).

## Aperçu de l'interface

![Interface d'IPR : informations à gauche, aperçu au centre, réglages à droite et ruban de vignettes en bas](docs/asset/ipr-interface.png)

*Écran d'accueil sans image ouverte. Les réglages sont regroupés en sections
repliables ; l'histogramme reste en haut du panneau de droite.*

## Fonctionnalités

### Navigation et consultation

- Ouverture d'une image et accès à son dossier ; parcours des fichiers JPEG, PNG, TIFF et RAW pris en charge.
- Ruban horizontal de vignettes avec chargement autour de la zone visible et libération des éléments éloignés.
- Panneau d'informations : nom, dimensions et métadonnées disponibles du boîtier, de l'objectif et de la prise de vue.
- Notation par étoiles, libellés de couleur et marquage par drapeau.
- Zoom, déplacement dans l'aperçu et comparaison **origine / actuel**.
- Multisélection et application de réglages aux images sélectionnées.

La comparaison « origine » utilise la base décodée et profilée avant les retouches :
ce n'est ni la mosaïque RAW ni nécessairement le JPEG intégré du boîtier.
Le zoom porte sur le proxy d'aperçu, pas sur une vue RAW pleine résolution.
Voir le [contrat de navigation et de comparaison](docs/specifications/zoom-image.md).

### Développement RAW et optique

- Import **CR2, CR3 et DNG** via le backend facultatif LibRaw.
- Sélection et activation de profils caméra **DCP**, dont Camera Neutral lorsque le profil compatible est disponible.
- Balance des blancs du fichier source et option de balance automatique.
- Profils optiques **LCP** : commandes indépendantes de correction objectif
  (distorsion/vignettage) et d'aberration chromatique latérale.
- Mémorisation des profils, de leurs empreintes et des options de développement dans les sidecars.
- Normalisation RAW expérimentale, désactivée par défaut et limitée aux cas audités.

Les profils ICC des images rendues servent à interpréter leurs couleurs ;
les DCP interviennent dans le développement RAW et les LCP dans les corrections optiques.
La correction des franges longitudinales et la correction CA sans modèle LCP
ne sont pas implémentées. Voir le [moteur de profils](docs/camera-profile-engine.md).

### Réglages photographiques

| Ensemble | Réglages disponibles |
| --- | --- |
| Lumière | Exposition (±5 EV), contraste, ombres, noirs, blancs et hautes lumières |
| Courbe de tonalité | Courbes luminance, rouge, vert et bleu, indépendantes de l'histogramme |
| Couleur | Température (2 000–50 000 K), nuance vert/magenta (−150 à +150), vibrance et saturation |
| Mélangeur de couleurs | Teinte, saturation et luminance sur huit bandes : rouge, orange, jaune, vert, cyan, bleu, violet et magenta |
| Color grading | Roues chromatiques pour ombres, tons moyens et hautes lumières |
| Effets | Texture, clarté, correction du voile, grain et flou |
| Réduction du bruit | Traitements classiques de luminance et de chrominance |
| Netteté | Quantité, rayon et masquage |

Les champs numériques et les curseurs sont liés. La température neutre dépend
de la référence détectée à l'ouverture, pas d'une température universelle appliquée
à toutes les photos. Les méthodes, paramètres et limites sont détaillés dans les
[spécifications des réglages](docs/specifications/reglages-photographiques.md).

### Sauvegarde, préférences et export

- Retouches non destructives conservées dans des **sidecars `.ipr`** ; lecture des versions historiques prises en charge.
- Préférences d'interface dans **`ipr-ui.ini`** : état de fenêtre, sections et paramètres d'affichage.
- Rendu et export en arrière-plan pour conserver une interface réactive.
- Export de l'image active en **JPEG, PNG, TIFF ou DNG linéaire rendu**.
- Dialogue adapté au format choisi et numérotation automatique pour éviter d'écraser un export existant.

**L'application de réglages par lot n'est pas un export par lot** :
la file d'export multiple reste à implémenter. Le DNG exporté contient un rendu
RGB linéaire avec les retouches intégrées, pas la mosaïque RAW originale ;
sa compatibilité Lightroom n'est pas encore validée.

Le menu **Affichage → Fichier de préférences…** indique l'emplacement du fichier INI.
Voir les [préférences d'IHM](docs/architecture/ui-preferences.md)
et le [modèle de configuration](config/ipr-ui.ini).

## État du projet et prochaines étapes

Le rendu Camera Neutral a été accepté visuellement sur le corpus étudié,
sans certification de fidélité physique ni de parité Lightroom.
L'exposition a été revalidée ; l'amplitude positive des ombres et la saturation
des blancs/hautes lumières restent à affiner. La qualité du bruit, de la netteté
et des autres réglages suit des campagnes séparées.

Les prochaines évolutions comprennent le recadrage et la géométrie manuelle,
les masques et retouches locales, l'intégration d'IA et l'export par lot.
Elles ne doivent pas être interprétées comme des fonctionnalités déjà livrées.

Pour l'état précis, consulter la [matrice de validation](docs/validation/matrice-validation.md)
et le [rapport Lumières](docs/validation/analyses/analyse-validation-lumieres-base-2026-10-02.md).

## Compilation

### Dépendances

| Dépendance | Usage |
| --- | --- |
| Qt ≥ 6.4 | Interface Quick/QML, Core, Gui et Concurrent |
| OpenCV | Calculs image : `core`, `imgproc`, `imgcodecs` |
| LibTIFF | Écriture du DNG linéaire |
| LibRaw ≥ 0.21, facultatif | Décodage CR2/CR3/DNG ; option `IPR_ENABLE_LIBRAW` |
| CMake ≥ 3.21 et compilateur C++17 | Configuration et compilation |

Qt et OpenCV ne remplacent pas un décodeur RAW. Sans LibRaw, les formats rendus
restent utilisables, mais le backend RAW actuel n'est pas disponible.

### Windows

Depuis WSL ou Git Bash, application fermée :

```bash
bash scripts/build-windows.sh
```

Le lanceur prépare le build et sélectionne les tests concernés par les changements.
Le premier passage, les modifications transversales et la validation CI imposent
une suite complète. Pour la demander explicitement :

```bash
bash scripts/build-windows.sh --full-tests
```

- [Prérequis, options et tests ciblés](docs/architecture/tests-cibles-et-build-windows.md).
- [Installation OpenCV : MSVC/vcpkg ou MinGW](docs/installation-opencv-windows.md).
- [Packaging Windows autonome](docs/architecture/build-windows-standalone.md).

Ne pas mélanger bibliothèques MSVC et MinGW. Choisir les dépendances compatibles
avec le kit Qt utilisé. Le packaging autonome exige également les DLL et plugins Qt.

### Linux / WSL

Avec les dépendances de développement installées et Ninja disponible :

```sh
cmake -S . -B build -G Ninja -DIPR_ENABLE_LIBRAW=ON -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Les tests utilisant un corpus photographique local ne sont disponibles que
lorsque leurs fichiers sont présents ; les images de test ne sont pas livrées
dans le dépôt. La sonde optionnelle `tonalReferenceComparisonProbe` compare
un développement de 9933 à son JPEG de référence : ce diagnostic historique
ne certifie pas le rendu de toutes les images.

Les prérequis et la stratégie Windows/macOS/Linux sont décrits dans le
[plan multi-OS et CI](docs/architecture/ci-et-build-multiplateforme.md) ;
ce plan ne vaut pas certification de chaque plateforme.

## Profils locaux et redistribution

`profiles/index.json` associe les boîtiers/objectifs aux ressources de
`profiles/dcp/` et `profiles/lcp/` afin de limiter les recherches coûteuses.
Seuls le manifeste et la structure sont versionnés ; les profils Adobe ne
sont pas fournis avec le dépôt.

Le moteur peut aussi rechercher les profils installés dans Adobe Camera Raw,
notamment sous `%ProgramData%/Adobe/CameraRaw` sur Windows et son montage
`/mnt/c/ProgramData/Adobe/CameraRaw` sous WSL. La variable
`IPR_ADOBE_CAMERARAW_DIR` permet de préciser une autre racine.

Un profil installé localement n'est pas automatiquement redistribuable.
Les transformations DCP/LCP sont partielles et non certifiées pour l'intégralité
de leurs spécifications ; les tests de profils utilisent des ressources synthétiques.
Consulter le [contrat DCP/LCP](docs/camera-profile-engine.md) avant déploiement.

## Architecture et documentation

Le code est regroupé sous `src/` : interface QML, application, éditeur,
pipeline couleur/détail, décodeurs RAW, profils, caches et fournisseurs d'images.

Le moteur utilise une image RGBA flottante **sRGB linéaire**.
Les entrées rendues passent par leur ICC valide, avec hypothèse sRGB signalée
en son absence. Le développement RAW fournit la base profilée.
Aperçu et export partagent le moteur de rendu et la compression de gamut sRGB ;
cela ne constitue pas un tone mapping HDR dédié.

| Ressource | Contenu |
| --- | --- |
| [Index documentaire](docs/INDEX.md) | Point d'entrée de la documentation |
| [Architecture](docs/architecture/README.md) | Composants, contrats et décisions techniques |
| [Moteur de rendu](docs/imageeditor-render-working-image.md) | Transformations, mathématiques et asynchronisme |
| [Manuel technique LaTeX](docs/technical/latex/README.md) | Documentation technique et diagrammes |
| [Cours Scholar](docs/scholar/README.md) | Fondements mathématiques et physiques |
| [Tests en autonomie](docs/validation/procedure-tests-autonomes.md) | Génération des images, audits et analyses |

Le build produit aussi `iprGenerateTests.exe`, qui génère des variantes de
validation sans écrire les sidecars. Voir le [guide du générateur](docs/validation/tools/generateur-images-tests.md)
et `scripts/generate-test-images.ps1`.

Depuis PowerShell : `./scripts/generate-test-images.ps1 couleur` (ou `all`).
Le générateur lit les images à la racine de `tests/image/base` et écrit les
TIFF dans les dossiers existants partagés avec Lightroom, sans dossiers datés
ni fichiers auxiliaires. Les images sont supprimées manuellement après analyse.

Chaque ensemble de fonctionnalités possède un rapport d'analyse évolutif.
Les **JSON versionnés et horodatés font autorité sur les mesures** ; CSV et
courbes en sont dérivés. Les images sont temporaires et leur suppression reste
à l'initiative de l'utilisateur. Voir le [contrat de mémoire des tests](docs/validation/data/README.md).

## Licences

Les CI vérifient les branches de travail ; les archives téléchargeables sont
réservées à `dev` et `main`, sur un commit validé, en modes standalone et
bibliothèques système. Voir le [contrat CI/CD](docs/architecture/ci-et-build-multiplateforme.md).

L'inventaire et les textes des licences des dépendances sont dans
[licenses/](licenses/README.md). Aucune licence du code IPR n'a encore été
choisie à la racine ; les licences des dépendances ne déterminent pas
automatiquement celle du projet.
