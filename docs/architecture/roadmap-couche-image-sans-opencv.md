# Roadmap — couche image sans OpenCV

**Décision :** [ADR-0018](../decisions/ADR-0018-independance-decodage-raw-et-couche-image.md),
20 octobre 2026, et volonté réaffirmée par l'utilisateur le 5 novembre 2026 : être le plus indépendant
possible. **État :** O0 commencé, O1 et O2 faits, O3 commencé par le flou gaussien, le 5 novembre 2026, intégré à `dev`.
**Plus aucun module n'appelle le flou, le redimensionnement, les déformations, les rotations, les symétries, les bordures, la
pyramide ni les petits filtres d'OpenCV** : la couche image les fournit. Restent l'arithmétique et la couleur, l'analyse
(corrélations, valeurs propres, systèmes), le parallélisme et le type `cv::Mat` lui-même.
Suivi : [issue 29](https://github.com/KimntexX/IPR/issues/29).
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

**O0 : commencé le 5 novembre 2026, à la demande de l'utilisateur.**

| Action | Gain | État |
| --- | --- | --- |
| Ne plus copier les DLL de débogage ni les modules vidéo d'OpenCV dans le paquet | **172 Mo de DLL en moins ; archive de 122,4 à 74,8 Mo** | Fait sur `patch_image_core` |
| Retirer `imgcodecs` de `CMakeLists.txt` | Une dépendance | Fait |
| Retirer `jpeg`, `png` et `tiff` de `vcpkg.json` | Taille du build | Fait |
| Construire OpenCV réduit à `core` et `imgproc`, pour remplacer `opencv_world` de 80 Mo | À mesurer | À faire : demande une construction propre d'OpenCV sous Windows, et la CI à adapter |

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

### Résultat de O1 : où `cv::Mat` partage ses pixels

Inventaire statique de `src/`, sur `patch_render_chain` (`3f89026`) : 27 `clone`, 5 `copyTo`, 7 vues par lignes ou colonnes,
3 paramètres par valeur, 2 images sur une mémoire externe, 81 sorties par référence modifiable, 132 membres `cv::Mat`,
56 lancements de calcul parallèle. Il donne des candidats et ne prouve rien ; les cas ci-dessous ont été lus un par un.

| Cas | Où | État |
| --- | --- | --- |
| Une recette neutre rend le tampon de la source | `RenderChain::workingImage` ; les trois surimpressions de `preview` clonent avant d'écrire | Sûr, couvert par un test |
| Une fonction peut rendre son entrée | `Geometry::warpRgb` (`Geometry.cpp:226`), `Geometry.cpp:89`, `RawDevelopment::orient` | Sûr tel quel : l'appelant écrit sur une entrée qui lui appartient. **À rendre explicite** |
| Source et destination sur le même tampon | `CameraProfileEngine.cpp:2041` : `geometryCorrected = linearRgb`, puis `cv::remap(linearRgb, geometryCorrected, ...)` | **À traiter à la migration.** Ne fonctionne que si OpenCV copie sa source dans ce cas, comportement interne que je n'ai pas pu lire ici |
| Sortie réutilisée en silence | Les 81 références modifiables : `create` garde le tampon s'il a la bonne taille, même partagé | **Le vrai risque.** Un noyau IPR rendra une nouvelle image ou écrira dans une image dont il a la garde |
| `const` qui ne protège pas | `shared_ptr<const DcpWorkingContext>`, copié ensuite en `denoised = *source.dcpContext` | Sûr par contrat : `CameraDenoise` publie un nouveau tampon, sous test. Rien ne l'imposait dans le type |
| Mémoire externe | `LibRawInput.cpp:17`, `RawDecoder.cpp:740`, `ColorPipeline.cpp:140` (avec `const_cast`) | Sûr : lus sur place ou clonés aussitôt |
| Caches | `RenderStageCache` clone en entrant et en sortant | Sûr ; sa copie se paie à chaque interaction |

**Budget mémoire, poste de 16 Go chargé :** un maître du R6 III, 6 959 × 4 639 en `CV_32FC4`, pèse 516 Mo, et un aperçu de
2 048 pixels 45 Mo. La passerelle ne copie rien : pendant O4, migrer un module n'ajoute pas de tampon.

### Résultat de O2 : le type image

`src/image/`, en C++ standard, sans Qt ni OpenCV, sauf `OpenCvBridge` qui disparaîtra avec lui. **Le type rend impossibles les
erreurs relevées en O1, au lieu de les documenter :**

- pas de copie par affectation : `Image a = b` ne compile pas. Il faut écrire `clone`, `share`, `view` ou `mutableView` ;
- **une image partagée ne change plus jamais** : `share` gèle les pixels pour tous leurs détenteurs, définitivement, et demander
  à les écrire lève une exception, au lieu de modifier ce qu'un autre thread est peut-être en train de lire ;
- une tuile à écrire est une `mutableView`, qui garde l'image vivante ;
- une ligne hors de l'image lève une exception, et ce contrôle par ligne ne coûte rien dans une boucle par pixel.

**Mesuré :** tests C++ sous MSVC, puis g++ 13 sous WSL avec ASan et UBSan, et TSan, tous verts. La passerelle ne copie aucun pixel
dans les deux sens, et un flou gaussien d'OpenCV donne le même résultat au bit près à travers elle, y compris écrit dans nos
pixels. **Limite :** ni ASan ni TSan n'ont tourné sur la passerelle, qui dépend d'OpenCV sous Windows, et aucun module n'est
encore migré, si bien que le coût réel à l'usage reste à mesurer.

### Premier noyau d'O3 : le flou gaussien, le plus sensible

[Preuve](../validation/raw/image-kernel-gaussian-v001-20261105.json), révision `b026746`, tolérance fixée dans l'outil avant toute
mesure : 1e-5, sous un pas de 16 bits. Médiane de trois essais à 8 threads, 3 canaux, sur un i7-12700K occupé par ailleurs ;
image de synthèse, sans module réel derrière.

| Taille | Sigma | OpenCV | IPR | Rapport | IPR seul sur 1 thread | Plus grand écart |
| --- | --- | --- | --- | --- | --- | --- |
| Aperçu, 2 048 × 1 365 | 1 | 3,1 ms | 7,7 ms | ×2,5 | 19 ms | 2,4e-7 |
| | 3 | 4,0 ms | 8,8 ms | ×2,2 | 32 ms | 3,0e-7 |
| | 10 | 13,8 ms | 19,9 ms | ×1,4 | 114 ms | 3,0e-7 |
| | 30 | 79,4 ms | 87,5 ms | ×1,1 | 529 ms | 3,0e-7 |
| Maître, 6 960 × 4 640 | 1 | 32 ms | 75 ms | ×2,3 | 225 ms | 3,0e-7 |
| | 3 | 47 ms | 102 ms | ×2,2 | 386 ms | 3,0e-7 |
| | 10 | 137 ms | 435 ms | ×3,2 | 1 071 ms | 3,6e-7 |
| | 30 | 796 ms | 1 930 ms | ×2,4 | 4 432 ms | 3,6e-7 |

- **Précision : acquise,** près de 30 fois sous la tolérance, y compris sur les bords et les très petites images.
- **Vitesse : le critère d'O3, « au moins égale », n'est pas tenu.** Sur l'aperçu, le flou reste loin de la cible de 250 ms ; sur le
  maître, à l'export, il coûte deux à trois fois plus. Multiplié par les flous d'une recette, ce n'est pas négligeable à l'export.
- **Ce n'est pas une question d'instructions vectorielles.** Le même fichier compilé en `/arch:AVX2`, drapeau vérifié dans le journal
  de compilation, ne gagne que 0 à 10 %. Traiter la ligne par blocs tenant dans le cache a gagné bien plus, jusqu'à un facteur 2.
- **Sur le maître, 8 threads ne donnent que 2,3 à 3,8 fois un seul thread.** La cause n'est pas établie : le débit mémoire est mon
  hypothèse, non mesurée. L'optimisation peut donc continuer sans intrinsèques.
- **Estimation du chantier :** toujours pas de chiffre sérieux. Ce noyau a pris peu de temps grâce à la spécification d'OpenCV et à
  son rôle d'oracle, mais un seul noyau et un seul module ne disent rien des 90 fichiers.

### Premier module migré : le contraste local

Texture et clarté, soit le plan de luminance flouté avec le bord répété. La couche image gagne ce second bord et l'écriture dans un
tampon existant ; [le noyau reste dans la tolérance](../validation/raw/image-kernel-gaussian-v002-20261105.json), à 3,6e-7 au plus.
[Preuve de l'export](../validation/raw/local-contrast-export-v001-20261105.json) : texture 50, clarté 30, TIFF 16 bits en pleine
résolution, médiane de trois exports par le vrai éditeur.

| Photo | Export avec OpenCV | Export avec la couche image | Plus grand écart, sur 65 535 | Valeurs qui diffèrent |
| --- | --- | --- | --- | --- |
| `6V1A0007.CR3` | 2 294 ms | 2 349 ms | 1 | 0,07 % |
| `6V1A0032.CR3` | 2 195 ms | 2 214 ms | 1 | 0,04 % |
| `IMG_6858.CR2` | 1 283 ms | 1 263 ms | 1 | 0,02 % |

- **Sur ce module, le coût à l'export est dans le bruit de la mesure,** alors que le noyau seul reste 2,1 à 2,7 fois plus lent sur une
  photo entière : il ne pèse qu'une petite part de l'export, car les grands rayons passent déjà par une image réduite.
- **Les pixels ne bougent pas à l'œil :** un niveau sur 65 535 au plus, sans modifier les exports neutres.
- **Ce n'est pas une règle générale.** Ces flous ont des sigmas sous 4 ; les modules qui floutent large en pleine résolution, là où le
  noyau atteint ×3,2, restent à mesurer un par un. L'aperçu n'a pas été chronométré. Les redimensionnements de ce module passent
  encore par OpenCV.
- **L'utilisateur a essayé Texture et Clarté le 5 novembre 2026** dans le paquet de test : rien de choquant sur la latence.

### Deuxième module : le curseur « Flou gaussien »

Le cas défavorable, car ce flou large porte sur toute l'image en pleine résolution. [Preuve](../validation/raw/gaussian-blur-slider-v001-20261105.json) :
curseur au maximum, soit un sigma de 10 pixels, TIFF 16 bits, médiane de trois.

| Photo | Export avec OpenCV | Avec la couche image | Aperçu avec OpenCV | Avec la couche image | Plus grand écart, sur 65 535 | Valeurs qui diffèrent |
| --- | --- | --- | --- | --- | --- | --- |
| `6V1A0007.CR3` | 1 365 ms | 1 554 ms | 191 ms | 200 ms | 1 | 0,10 % |
| `6V1A0032.CR3` | 1 196 ms | 1 393 ms | 190 ms | 191 ms | 1 | 0,05 % |
| `IMG_6858.CR2` | 708 ms | 873 ms | 185 ms | 190 ms | 1 | 0,02 % |

- **Ici le coût se voit à l'export :** 165 à 197 ms de plus, soit 14 à 23 %, curseur au maximum.
- **L'aperçu reste sous 250 ms,** mesuré du changement à la nouvelle image, tout le rendu compris.
- **Les pixels ne bougent pas à l'œil :** un niveau sur 65 535 au plus.

### Modules de rendu : voile, grain, détail, bruit, retouche

La couche image gagne le troisième bord, le dernier pixel répété. **Une erreur de ma part est corrigée au passage :** pour des sigmas
fractionnaires, le rayon pouvait différer d'un pixel de celui d'OpenCV, sans effet visible ; il suit maintenant sa règle exacte, et
[le noyau reste dans la tolérance](../validation/raw/image-kernel-gaussian-v003-20261105.json).
[Preuve](../validation/raw/image-layer-render-modules-v001-20261105.json) : voile 40, grain 30, accentuation 60, bruit 40 et 40, médiane de trois.

| Photo | Export avec OpenCV | Avec la couche image | Aperçu avec OpenCV | Avec la couche image | Plus grand écart, sur 65 535 | Valeurs qui diffèrent |
| --- | --- | --- | --- | --- | --- | --- |
| `6V1A0007.CR3` | 5 349 ms | 5 738 ms | 785 ms | 825 ms | 1 | 0,27 % |
| `IMG_6858.CR2` | 3 099 ms | 3 149 ms | 610 ms | 611 ms | 1 | 0,18 % |

Ces réglages cumulés dépassent déjà 250 ms à l'aperçu avec OpenCV, comme l'utilisateur l'a accepté pour le voile et le bruit de couleur.
La retouche n'est tenue que par ses propres tests, pas par cet export.

### Derniers flous : dématriçage, géométrie, empilement, panorama

La couche image gagne un sigma par axe, pour l'anticrénelage de la géométrie.
[Preuve](../validation/raw/image-layer-remaining-modules-v001-20261105.json).

| Mesure | Avec OpenCV | Avec la couche image |
| --- | --- | --- |
| Empilement, 8 vues : recalage, écart résiduel de la 1re vue | 175 points, 0,45630 px | 175 points, 0,45631 px |
| Empilement : bruit restant, vert | 0,350394 | 0,350392 |
| Empilement : fusion | 16,2 s | 16,9 s |
| Panorama, 6 vues : écart moyen, pire paire | 1,8459 px, 3,7795 px | identiques au millionième |
| Panorama : assemblage | 8,1 s | 7,4 s |
| Export avec échelle 70 et aspect −40 | 1 864 et 984 ms | 1 651 et 924 ms |

- **Le recalage et l'assemblage ne changent pas :** mêmes points, mêmes écarts au millionième près, même cadre.
- **Les pixels presque pas :** l'export avec géométrie s'écarte de 6 niveaux sur 65 535 au plus, sur moins de 0,07 % des valeurs.
- **Le dématriçage bouge un peu plus :** jusqu'à 20 niveaux sur 65 535, sur 0,006 % des valeurs au plus. Son poids de zone plate
  amplifie de minuscules écarts du flou, ce qui reste une hypothèse.
- **L'empilement aussi, sur de rares pixels :** jusqu'à 213 niveaux dans un extrait éclairci de deux diaphragmes, soit environ 53 à
  l'exposition réelle, sur 0,06 % des pixels au plus. Le rejet des pixels en mouvement, par seuil, bascule sur quelques pixels. Un
  extrait de comparaison change de place, car l'outil choisit lui-même les zones les plus texturées.
- **Une seule mesure de durée par outil :** les écarts de temps ne départagent pas les deux versions.

### Redimensionnement

Bilinéaire et par zone, comme OpenCV les calcule en flottants, agrandissement par zone et échelles données compris.
[Noyau](../validation/raw/image-kernel-resize-v001-20261105.json) : 6e-8 au plus du code d'OpenCV. **Intel IPP**, qu'OpenCV appelait
pour certains agrandissements bilinéaires, s'écarte lui-même de ce code jusqu'à 6e-5, soit 4 niveaux sur 65 535 ; l'outil mesure les
deux. [Modules](../validation/raw/image-layer-resize-v001-20261105.json) :

- **Exports avec voile, texture et clarté : identiques au bit près**, et aussi rapides.
- **Empilement et panorama : identiques au bit près**, recalage, bruit, cadre et images compris.

**Threads.** Chaque appel du flou lançait jusqu'à sept threads, y compris depuis les tuiles du dématriçage déjà réparties sur des
threads. Ils partagent désormais un seul budget, celui des huit threads ; les pixels ne changent pas.

**Une erreur de mesure évitée.** Deux mesures ont semblé révéler un recul. Le décodage du panorama paraissait passer de 2,5 à
9,7 s : l'outil d'audit gardait en cache les vues déjà décodées, et la première mesure n'en décodait qu'une. Un export paraissait
s'écarter d'un niveau : l'éditeur écrit un `.ipr` à côté des copies, et une mesure partait de ce qu'une autre avait laissé. L'outil
travaille désormais sur des copies neuves.

### Déformations, rotations, symétries, bordures et pyramide

[Noyaux](../validation/raw/image-kernel-warp-arrange-v001-20261105.json), [modules](../validation/raw/image-layer-warp-arrange-v001-20261105.json).

- **Lanczos** lit ses poids au 1/32 de pixel, comme OpenCV : **identique au bit près**, en remap comme en perspective. C'est lui
  qui sert l'export de la géométrie, l'empilement et le panorama ; ces exports sortent identiques au bit près.
- **Bilinéaire et bicubique** : OpenCV 5 ne les arrondit plus au 1/32 et calcule la perspective en flottants simples ; OpenCV 4
  arrondit au 1/32. Les deux ne s'accordent déjà pas. La couche image prend des positions exactes en double : en remap, 7e-7
  d'OpenCV 5 ; **en perspective, jusqu'à 2e-4 sur une texture de bruit, hors de la tolérance de 1e-5**. Cela touche l'aperçu de la
  géométrie, la carte des masques et l'alignement grossier de l'empilement. Choix : ne pas imiter une imprécision qui change d'une
  version d'OpenCV à l'autre.
- **Rotations, symétries, transposition, bordures : identiques au bit près**, aussi rapides qu'OpenCV après une optimisation par
  lignes entières. **La pyramide** : 2e-7.
- **Sur les modules** : recalage de l'empilement identique au millionième, rares pixels jusqu'à 134 niveaux sur 65 535 par
  l'alignement grossier ; panorama aux mêmes points, écart résiduel de 1,8459 à 1,8466 px par la pyramide.
- **Lenteur à reprendre** : Lanczos tourne environ trois fois plus lentement qu'OpenCV ; la fusion de l'empilement passe de 9,2 à
  15,1 s sur 8 vues.
- **Une erreur trouvée par la mesure** : la bordure d'une région se faisait en miroir au bord de la tuile, quand OpenCV lit
  l'image autour ; chaque jointure de tuile du dématriçage était fausse. Corrigée avant le commit ; l'oracle compare désormais une
  région aussi.
- **Suivi** : le choix des positions exactes et sa validation à revoir sont tracés dans l'[issue 31](https://github.com/KimntexX/IPR/issues/31).

### Petits filtres : Sobel, Laplacien, médian, moyenne en boîte, minimum et maximum

[Noyaux](../validation/raw/image-kernel-neighbourhood-v001-20261105.json), [modules](../validation/raw/image-layer-neighbourhood-v001-20261105.json).

- **Sobel à 5e-7 ; tous les autres identiques au bit près.** Le médian passe par des réseaux de sélection (19 comparaisons pour
  9 valeurs, 99 pour 25), vérifiés contre un tri complet, égalités comprises ; minimum et maximum lisent aussi les octets.
- **Sur les modules** : exports à un niveau sur 65 535 au plus, exports neutres identiques, empilement identique au bit près ;
  le panorama garde son cadre, son alignement bouge d'une fraction de pixel par les gradients de ses points.
- **La vitesse, mesurée sur l'export avant de conclure.** D'abord 29 % plus lent sur le débruitage de la couleur, les autres réglages
  inchangés. Après réécriture : 9 %. La moyenne en boîte va désormais plus vite qu'OpenCV.
- **Vectorisation vérifiée, et non supposée** (`97b4295`). J'avais attribué la lenteur du médian, du minimum et du maximum à la
  mémoire sans le mesurer ; l'utilisateur l'a mis en doute à raison. Le rapport du compilateur (`/Qvec-report:2`) montrait ces
  boucles laissées en scalaire, par crainte d'un recouvrement entre tableaux lus et écrits. Placées dans de petites fonctions sur
  des pointeurs distincts, elles sont vectorisées ; minimum et maximum parcourent en outre des bandes de colonnes avec un anneau de
  lignes, comme OpenCV, au lieu de repasser plusieurs fois sur l'image. Sobel va aussi vite qu'OpenCV ou plus, le médian 5 x 5
  1,2 fois son temps, minimum et maximum 1,2 à 3,5 fois. Résultats inchangés.

### Arithmétique des pixels

Canaux séparés et réunis, produits, quotients, mélanges, bornes, racines, transformations, masques en octets, et les réductions :
comptage, extrêmes avec leur place, moyenne et écart-type (`93e5ace`).
[Noyaux](../validation/raw/image-kernel-pixels-v001-20261106.json), [modules](../validation/raw/image-layer-pixels-v001-20261106.json).

- **Contre OpenCV : à un arrondi de flottant près, les réductions identiques.**
- **Sur les modules** : exports avec réglages à 5 niveaux sur 65 535 au plus, sur 0,07 % des valeurs ; exports neutres à 24 niveaux
  au plus sur quelques pixels (moins d'un millionième) ; empilement au même bruit à 1e-6 près ; panorama au même cadre, à 7/255 au
  plus sur 0,2 % des pixels.
- **Une écriture dans une région à préserver** : là où OpenCV écrivait dans une région d'une image plus grande, le résultat est
  recopié dans cette région ; la couverture du panorama en dépend.

### Analyse : corrélations, valeurs propres, résolution

Mise en correspondance de blocs, corrélation de phase et sa fenêtre de Hann, vecteurs propres symétriques, Cholesky, valeurs
singulières, déterminant et rotation la plus proche (`6bfeaa2`) : empilement, panorama, profil de l'appareil, grain.
[Noyaux](../validation/raw/image-kernel-analysis-v001-20261106.json), [modules](../validation/raw/image-layer-analysis-v001-20261106.json).

- **Fenêtre de Hann identique au bit près ; algèbre linéaire à 1e-14 ; scores de correspondance à 2e-5, aux mêmes pics.** La
  décomposition en valeurs singulières, par Jacobi, va beaucoup plus vite que celle d'OpenCV, qui calcule une matrice U complète.
- **Corrélation de phase : un critère changé après mesure, et je le signale.** La tolérance fixée d'avance, 1e-3 pixel contre
  OpenCV en flottants simples, ne pouvait pas tenir : sur une texture lisse, OpenCV en flottants simples s'écarte de son propre calcul
  en double de 0,2 à 0,3 pixel, ses hautes fréquences tombant sous la précision du flottant. La couche image calcule en double ; elle
  rejoint OpenCV en double à 1e-4 pixel sur une texture nette, et reste plus près d'OpenCV en flottants simples que lui-même en double.
  L'oracle mesure désormais ces deux écarts.
- **Sur les modules** : exports identiques au bit près ; empilement aux mêmes points et au même bruit, DNG à 38 niveaux au plus sur
  0,4 % des valeurs ; panorama aux mêmes écarts au dix-millionième, à 11/255 au plus dans l'image, jusqu'à 80/255 dans les bords
  remplis, que le remplissage invente et où de minuscules écarts d'entrée changent le motif choisi.
- **La vitesse** : la mise en correspondance est appelée environ 135 000 fois par empilement de 8 vues. Réécrite pour ajouter huit
  coefficients par passage le long d'une ligne, vectorisée d'après le rapport du compilateur et sans changer l'ordre des sommes, elle
  va 2 à 5 fois plus vite qu'OpenCV sur les petits blocs et 1,35 fois plus lentement sur les blocs de 65 pixels, où OpenCV passe par
  une transformée de Fourier. **L'empilement reste environ 40 % plus long** (14,0 et 14,5 s contre 10,0 et 10,5 s, mesures alternées
  sur une machine chargée) ; l'utilisateur l'a accepté le 5 novembre 2026, l'amélioration viendra plus tard.
- **Une fausse alerte de mesure.** Un premier essai montrait la Lune d'un empilement assombrie de 13 % : l'outil d'audit lit
  `config/raw-normalization.json` depuis le dossier courant, et cette mesure était lancée d'ailleurs. Les mesures partent désormais de
  la racine du dépôt.

### Fils d'exécution et petits types

- **Les 50 `cv::parallel_for_`** passent par `ipr::parallelRows`, qui partage un seul budget de threads avec les noyaux de la couche
  image (`99bffce`). Exports, DNG empilé et panorama : identiques au bit près.
- **Les petits types** (`Vec`, `Matx`, `Point`, `Rect`, `Size`, environ 1 250 usages) deviennent ceux de la couche image. Leur
  arithmétique est celle d'OpenCV, expression pour expression : mêmes conversions, même ordre des sommes, division par produit avec
  l'inverse, arrondi au plus proche avec égalité au pair, intersection de rectangles par sa formule, inverse par cofacteurs.
  [Oracle](../validation/raw/image-kernel-types-v002-20261106.json), [modules](../validation/raw/image-layer-types-v001-20261106.json) : chaque opération sur 20 000 valeurs aléatoires et limites,
  **bits identiques**. Seule différence voulue : au-delà de la plage des entiers, OpenCV rend `INT_MIN` quel que soit le signe, la
  couche image la borne la plus proche.
- **Une surprise lue dans le code d'OpenCV** : pour une matrice 3 × 3, `inv(DECOMP_CHOLESKY)` ignore la méthode et inverse par
  cofacteurs. Le débruitage de la caméra le demandait ; il garde exactement le même calcul.
- **Ce qui reste** : `cv::Mat` (environ 625 usages), ses constantes de type et les appels d'OpenCV des tests. Aux jonctions entre les
  petits types et `cv::Mat`, des conversions explicites `ipr::toCv` / `ipr::fromCv` signalent ce qui partira avec `cv::Mat`.

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

1. **Le critère de vitesse d'O3 : tranché par l'utilisateur le 5 novembre 2026,** après essai de Texture, Clarté et du curseur de flou :
   le coût lui convient, et la migration continue module par module, chacun mesuré. « Au moins égale » reste l'objectif.
2. **Algèbre linéaire.** Écrire les petits solveurs nécessaires, sans bibliothèque : **accepté le 5 novembre 2026.**
3. **Scripts d'analyse Python.** Ils gardent `cv2`, hors du produit : **accepté le 5 novembre 2026.**
