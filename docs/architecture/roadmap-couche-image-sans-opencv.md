# Roadmap — couche image sans OpenCV

**Décision :** [ADR-0018](../decisions/ADR-0018-independance-decodage-raw-et-couche-image.md),
20 octobre 2026, et volonté réaffirmée par l'utilisateur le 5 novembre 2026 : être le plus indépendant
possible. **État :** plan ; aucun code. Suivi : [issue 29](https://github.com/KimntexX/IPR/issues/29).
Chantier distinct des [décodeurs RAW](roadmap-decodeurs-raw.md), sur sa propre branche durable,
`patch_image_core`, après le palier E1 de la [refonte d'`ImageEditor`](plan-imageeditor-orchestrateur.md).

## 1. Usage mesuré le 5 novembre 2026

Mesuré sur `patch_ui_object_removal` (`82b0a4e`). **Le recensement du 19 octobre, 286 occurrences dans 28 fichiers, ne
tient plus** : l'usage a beaucoup grandi avec l'empilement, le panorama, les masques et la retouche, et le chantier
grandit tant qu'il n'est pas lancé.

| Mesure | Valeur |
| --- | --- |
| Fichiers de `src/` concernés | **90 sur 139** |
| Occurrences de `cv::` | **2 256**, dont 1 890 de types, 269 d'algorithmes, 97 de constantes |
| Modules | `core` (49 inclusions), `imgproc` (42), `imgcodecs` lié mais **jamais utilisé** |
| Tests C++ concernés | 54 fichiers |
| Scripts Python avec `cv2` | 28, d'analyse, hors du produit |
| Poids dans le paquet Windows | **252 Mo sur 343 Mo de DLL** |

**Les fichiers les plus exposés :** `PanoramaFill` (196), `PanoramaGeometry` (183), `DetailProcessing` (163),
`StackAlignment` (142), `CameraProfileEngine` (95), `Geometry` (94), `LocalAdjustments` (88), `Retouch` (87), `StackJob` (84).

### Ce qu'il faut remplacer

| Famille | Fonctions | Occurrences | Difficulté | Comparaison attendue |
| --- | --- | --- | --- | --- |
| Type image | `Mat` | 631 | **Point dur** : comptage de références, vues, alias | Au bit près |
| Petits calculs | `Vec*`, `Matx*`, `Point*`, `Rect*`, `Size`, `Range`, `Scalar` | environ 1 250 | Simple, mécanique | Au bit près |
| Parallélisme | `parallel_for_` | 50 | Simple, mais à concevoir | Au bit près |
| Flous | `GaussianBlur`, `blur`, `boxFilter`, `getGaussianKernel` | 40 | **Performance** : le plus sensible | Tolérance |
| Rééchantillonnage | `resize`, `remap`, `warpPerspective`, `pyrDown` | 29 | **Lanczos, cubique et aire**, performance | Tolérance |
| Dérivées, filtres | `Sobel`, `Laplacian`, `filter2D`, `medianBlur` | 14 | Moyen | Tolérance ou au bit près selon le cas |
| Morphologie | `erode`, `dilate`, `getStructuringElement` | 11 | Simple | Au bit près |
| Algèbre linéaire | `SVD`, `eigen`, `solve`, `determinant` | 10 | **Délicat** : stabilité numérique, géométrie du panorama | Tolérance sur les résultats |
| Mise en correspondance | `matchTemplate` | 3 | Moyen : coefficient normalisé, performance | Tolérance |
| Élémentaire | `split`, `merge`, `cvtColor`, `multiply`, `divide`, `flip`, `rotate`, `checkRange`, `countNonZero`, `minMaxLoc`, `norm`, `mean`, etc. | environ 112 | Trivial | Au bit près |

Rien n'est difficile en soi : **il y a un point dur, le type image, et deux enjeux de performance, flous et
rééchantillonnage**. Tout le reste est du volume.

## 2. Pourquoi c'est aussi une question de fiabilité

L'indépendance n'est pas qu'un principe. Constaté ces derniers jours :

- **OpenCV 4 en CI, OpenCV 5 en local.** `goodFeaturesToTrack` et `boundingRect` ont quitté `imgproc` en version 5, et
  `cv::DIST_L2` et `approxPolyDP` ne s'utilisent pas de la même façon : du code écrit pour l'un casse sur l'autre. Les
  résultats peuvent aussi différer d'une version à l'autre sans erreur.
- **Paquets macOS** ([issue #20](https://github.com/KimntexX/IPR/issues/20)) : une partie des échecs tient aux bibliothèques
  OpenCV à relocaliser.
- **Une dépendance énorme pour peu de chose** : `opencv_world` embarque tout, IPR se sert de deux modules.

## 3. Gains immédiats, sans attendre

**O0 : à faire dès maintenant.**

| Action | Gain | Risque |
| --- | --- | --- |
| Ne plus copier `opencv_world500d.dll`, version de débogage, ni `opencv_videoio_ffmpeg500_64.dll` dans le paquet | **171 Mo** | Nul : jamais chargées. À vérifier aussi sur les paquets de CI |
| Retirer `imgcodecs` de `CMakeLists.txt` | Une dépendance | Nul : aucun `imread` ni `imwrite` |
| Retirer `jpeg`, `png` et `tiff` de `vcpkg.json` | Taille du build | Nul |
| Construire OpenCV réduit à `core` et `imgproc`, pour remplacer `opencv_world` de 80 Mo | Environ à mesurer | Faible : CI à adapter |

## 4. Paliers

| Palier | Travail | Critère de passage |
| --- | --- | --- |
| **O0** | Gains immédiats ci-dessus | Build et CI verts, rendu identique au bit près |
| **O1** | Audit du partage de données de `cv::Mat` : copies légères, `clone()`, vues, écritures sur tampon partagé | Liste des alias, budget mémoire du poste 16 Go |
| **O2** | `src/image/` : type image et petits calculs, avec passerelle sans copie vers `cv::Mat` | Tests, y compris ASan et UBSan ; empreintes inchangées à l'échange |
| **O3** | Noyaux, chacun contre OpenCV comme oracle, sur images réelles et synthétiques | Tolérances chiffrées **avant** mesure ; performances au moins égales sur 8 threads |
| **O4** | Migration module par module, des feuilles vers le centre | Pour chaque module : parité, puis retrait de la passerelle |
| **O5** | Tests C++ et outils ; retrait d'OpenCV de CMake, CI, vcpkg, paquets et documentation | Build complet sans OpenCV sur toutes les plateformes |
| **O6** | Revalidation photographique | Lumières, détail, optique, géométrie, empilement, panorama ; **validation de l'utilisateur** |

**Ordre de O4 :** `ToneCurve`, `Grain`, `ToneRecovery`, `PerceptualColour`, puis `ColorPipeline`, `Geometry`, `LocalContrast`,
`Dehaze`, `DetailProcessing`, puis `Demosaic`, `RawDevelopment`, `CameraDenoise`, `CameraProfileEngine`, puis `StackMerge`, `StackAlignment`,
`StackJob`, `PanoramaFeatures`, `PanoramaGeometry`, `PanoramaBlend`, `PanoramaFill`, `Retouch`, `LocalAdjustments`, enfin l'éditeur.
La passerelle reste active pendant O4 ; il n'y a jamais deux implémentations **d'un même module** en même temps,
conformément à la règle d'absence de legacy, et elle disparaît à la fin.

## 5. Principes de conception

- **Noyaux sans Qt.** `src/image/` ne dépend que du C++ standard, pour ne pas remplacer une dépendance lourde par une autre,
  et pour garder la possibilité d'en faire autant avec Qt plus tard. Le pool de threads en fait partie, branché sur le budget
  adaptatif de 8 threads déjà décidé.
- **Le partage de données est explicite.** Une vue et une copie sont deux choses différentes dans le code.
- **Déterminisme.** Mêmes pixels sur Windows, Linux et macOS, quel que soit le nombre de threads, autant que possible.
  Ce serait un gain réel sur la situation actuelle, à tester en CI.
- **OpenCV sert d'oracle jusqu'au bout.** Il reste dans les tests de O3 pour valider chaque noyau, et ne part qu'à O5.

## 6. Risques

| Risque | Parade |
| --- | --- |
| **Performance :** OpenCV est très optimisé (AVX2, IPP selon la construction) | Mesurer à chaque noyau ; décision de principe en §7 |
| **Égalité impossible** sur flous et rééchantillonnage : bords et arrondis diffèrent | Tolérances avant mesure. **Toute validation photographique s'en trouve à refaire**, d'où O6 |
| **Alias révélés** par un type différent | O1 avant tout échange, ASan |
| **Algèbre linéaire instable** : le panorama dépend de la qualité du calcul sur 9×9 et sur les matrices normales | Tests sur matrices mal conditionnées ; géométrie retrouvée sur les séries existantes |
| **Campagnes de référence** figées sur le comportement d'OpenCV | Les JSON restent immuables ; une nouvelle campagne en crée de nouveaux |
| **Course avec les fonctionnalités :** chaque lot ajoute du `cv::` | Tout nouveau module utilise les types IPR dès que O2 est livré |
| **Effort sans gain visible** | O0 donne un gain immédiat ; le reste s'étale entre les lots |

**L'estimation initiale de 4 à 8 semaines ne vaut plus** : elle portait sur huit fois moins d'occurrences. Elle sera
refaite après O2, sur la vitesse réellement mesurée, plutôt qu'annoncée maintenant.

## 7. Décisions à prendre

1. **Instructions vectorielles.** AGENTS.md demande du C++ standard, sans extension de compilateur. Sans intrinsèques, on
   dépend de la vectorisation automatique. **Je propose de garder le C++ standard au départ**, de mesurer sur les flous et le
   rééchantillonnage, et de ne revenir sur cette règle que si l'écart est sensible, avec ton accord.
2. **Algèbre linéaire.** Écrire les petits solveurs nécessaires, ou prendre une bibliothèque en en-têtes, comme Eigen. **Je
   propose d'écrire** ce qu'il faut : les tailles sont petites et connues, et c'est ton objectif d'indépendance.
3. **Scripts d'analyse Python.** Ils utilisent `cv2` hors du produit. **Je propose de les laisser** : ce n'est pas une
   dépendance d'IPR. À migrer vers NumPy et Pillow seulement au fil de l'eau.
4. **Quand lancer O0.** Il ne dépend de rien. **Je propose tout de suite**, sur accord.
