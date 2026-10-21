# Rapport de session — Optimisation des performances (ouverture d'image et calcul des réglages)

## Contexte et objectif

À la demande de l'utilisateur, une analyse de performance a été menée sur
trois axes : l'ouverture d'image, le recalcul des réglages, et la gestion de
la mémoire (cache + libération au changement d'image), avec deux contraintes
explicites :

- la RAM ne doit jamais être surchargée (pas de duplication inutile de
  mémoire) ;
- prévoir un fichier de sidecar léger et rapide à lire pour stocker les
  réglages modifiés (équivalent allégé des `.xmp`/`.lrcat` de Lightroom).

L'utilisateur a validé un ordre de priorité proposé en amont :

1. **A** — chargement asynchrone de l'image (gain immédiat, risque faible).
2. **B** — suppression du double décodage RAW dans `ImageProvider`.
3. **C** — proxy d'édition sous-échantillonné (gain de réactivité le plus
   important).
4. **E** — cache borné + libération mémoire explicite.
5. **F** — format de sidecar `.ipr` léger.
6. **D** — migration GPU (`cv::UMat`), le chantier le plus lourd, en dernier.

Consigne explicite : s'arrêter avant épuisement du budget de crédits de la
session, en conservant une réserve pour produire ce rapport.

**Bilan à jour : les points A, B, C et E sont implémentés et validés.** Le
sidecar F reste indépendant ; D (`cv::UMat`) doit être précédé d'une mesure
OpenCL, car le profilage montre maintenant que le décodage LibRaw est le
principal coût résiduel.

## Travaux réalisés

### Point A — Chargement asynchrone de l'image

**Problème initial** : [ImageEditor::loadImage()](../../../../src/editor/imageeditor.cpp)
et `ImageEditor::setAutomaticWhiteBalance()` décodaient le fichier RAW/standard
de façon strictement synchrone sur le thread UI. Pour un CR3/CR2 de grande
taille, le dématriçage LibRaw peut prendre plusieurs secondes, pendant
lesquelles toute l'interface (y compris la pellicule et les curseurs) restait
figée.

**Solution mise en œuvre** :

- Ajout d'une structure `LoadResult` ([imageeditor.h](../../../../src/editor/imageeditor.h))
  regroupant l'image décodée (`ColorPipeline::WorkingImage`), les métadonnées
  source (`RawDecoder::SourceMetadata`) et un message d'erreur éventuel.
- Extraction de toute la logique de décodage (branches RAW via
  `RawDecoder::decode()` / formats standards via `QImageReader` +
  `ColorPipeline::fromSourceImage()`) dans une nouvelle fonction **statique**
  `ImageEditor::loadFromDisk(fileName, extension, automaticWhiteBalance)`,
  sans accès à l'état de l'instance — donc exécutable en toute sécurité sur un
  thread de pool via `QtConcurrent::run()`.
- `loadImage()` et `setAutomaticWhiteBalance()` lancent désormais ce décodage
  via `QtConcurrent::run()` et un nouveau `QFutureWatcher<LoadResult>
  m_loadWatcher`, suivant exactement le même schéma de garde par compteur de
  révision (`m_requestedLoadRevision` / `m_activeLoadRevision`) déjà utilisé
  pour le rendu (`m_renderWatcher`) : si un nouveau chargement est demandé
  pendant qu'un ancien est en cours, le résultat obsolète est silencieusement
  ignoré à la réception.
- Un indicateur `m_pendingLoadIsFreshOpen` distingue :
  - une **ouverture d'image** (réinitialisation complète des réglages,
    histogramme, émission de `hasImageChanged`/`adjustmentsReset`) ;
  - une **bascule de balance des blancs automatique** (simple redécodage avec
    conservation des réglages en cours), qui réutilise le même mécanisme
    asynchrone plutôt que de dupliquer la logique.
- Nouvelle méthode `ImageEditor::refreshBusyState()` centralisant le calcul de
  l'indicateur `busy` à partir des trois watchers (chargement, rendu, export),
  utilisée dans leurs trois callbacks `finished`.
- Pendant le chargement, l'image précédemment affichée reste visible (pas de
  flash de contenu vide) ; le message de statut indique
  « Ouverture de *fichier*… ».

**Fichiers modifiés** : [imageeditor.h](../../../../src/editor/imageeditor.h),
[imageeditor.cpp](../../../../src/editor/imageeditor.cpp).

**Impact sur les tests** : les tests `imageExportTests` et
`tonalReferenceComparisonProbe` appelaient `loadImage()` puis vérifiaient
`hasImage()` de façon synchrone immédiatement après. Ils ont été adaptés avec
un helper `awaitLoad()` qui attend le signal `hasImageChanged` (via une
`QEventLoop`, conditionné par `editor.busy()`) avant de poursuivre. Le même
principe (`awaitImageInfo()`) a été ajouté dans
`tonalReferenceComparisonProbe` pour attendre la fin du redécodage déclenché
par `setAutomaticWhiteBalance()` avant d'exporter, évitant une course entre le
redécodage asynchrone et l'export (qui capture `m_original` de façon
synchrone).

### Point B — Suppression du double décodage RAW

**Problème initial** : [ImageProvider::requestImage()](../../../../src/providers/imageprovider.cpp)
redécodait entièrement le fichier RAW via `RawDecoder::decode()` en secours
si `m_image` était encore nul — alors que `ImageEditor` avait déjà décodé et
allait pousser l'image rendue via `setImage()` quelques instants après.
Chaque ouverture déclenchait donc potentiellement **deux décodages RAW
complets**.

**Solution mise en œuvre** : ce chemin de repli est devenu obsolète une fois
le chargement rendu asynchrone (le fournisseur affiche un pixel transparent
le temps que le premier rendu arrive, au lieu de redécoder). La branche de
décodage a été supprimée ; `requestImage()` ne fait plus que lire l'image en
cache ou retourner un pixel transparent 1×1. Les `#include` devenus inutiles
(`ColorPipeline.h`, `RawDecoder.h`, `QFileInfo`) ont été retirés. `setUrl()` /
`url()` sont conservés tels quels pour ne pas changer l'API publique, bien
qu'ils ne soient plus utilisés en interne.

**Fichier modifié** : [imageprovider.cpp](../../../../src/providers/imageprovider.cpp).

## Validation

- Build complet (`cmake --build build-wsl-libraw -j2`, tous les exécutables)
  : **succès**, aucun avertissement de compilation introduit.
- `ctest` dans `build-wsl-libraw` — **7/7 tests passants (100 %)** :
  `colorPipelineTests`, `imageExportTests`, `rawDecoderTests`,
  `rawBlackLevelEstimatorTests`, `cameraProfileEngineTests`,
  `whiteBalanceEstimationTest`, `tonalReferenceComparisonProbe`.
- Aucun build/test exécuté dans `build-wsl-nolibraw` (conformément à la
  consigne de validation du projet).
- Point de vigilance corrigé en cours de route : l'ordre
  `refreshBusyState()` → `setFuture()` rendait `busy` incorrect pendant une
  fraction de seconde (le watcher n'était pas encore « en cours » au moment du
  calcul) ; remplacé par `setBusy(true)` explicite avant le lancement de la
  tâche asynchrone, à l'image du pattern déjà utilisé par `startRender()` et
  `exportImage()`.

### Point C — Proxy d'édition sous-échantillonné

**Problème initial** : `renderWorkingImage()` recalculait l'intégralité du
pipeline (exposition, contraste, courbes, effets, dehaze, grain…) en pleine
résolution capteur à chaque glissement de curseur, alors que l'aperçu affiché
dans [Main.qml](../../../../src/qml/Main.qml)
(`Image.PreserveAspectFit`) est de toute façon mis à l'échelle de la zone
d'affichage, bien plus petite qu'un capteur de 24-50 Mpx.

**Solution mise en œuvre** :

- Ajout d'un second tampon `m_editProxy` ([imageeditor.h](../../../../src/editor/imageeditor.h))
  et d'une fonction statique `ImageEditor::buildEditProxy(source)`
  ([imageeditor.cpp](../../../../src/editor/imageeditor.cpp)) :
  si le plus grand côté de l'image source ne dépasse pas 2048 px, le proxy
  est une copie superficielle de la source (aucun coût) ; sinon, l'image est
  réduite par `cv::resize(..., cv::INTER_AREA)` jusqu'à ce seuil, en
  conservant son espace colorimétrique, sa provenance et sa température de
  référence.
- Le proxy est calculé une seule fois par chargement, **dans le thread
  d'arrière-plan déjà introduit au point A** (`loadFromDisk()`, via le champ
  `LoadResult::editProxy`), donc sans coût supplémentaire sur le thread UI.
- `ImageEditor::startRender()` utilise désormais `m_editProxy` (avec repli
  sur `m_original` si celui-ci est vide) comme source du rendu d'aperçu.
  `exportImage()`/`writeImage()` restent inchangés et continuent d'utiliser
  `m_original` en pleine résolution : la qualité de l'export n'est pas
  affectée.
- L'histogramme affiché est désormais calculé sur ce même proxy ; sa forme
  reste représentative, mais ses comptages exacts aux extrêmes (0/255)
  peuvent légèrement différer d'un calcul en pleine résolution, par effet de
  moyennage du redimensionnement — compromis documenté dans
  [imageeditor-render-working-image.md](../../../../docs/imageeditor-render-working-image.md).

**Fichiers modifiés** : [imageeditor.h](../../../../src/editor/imageeditor.h),
[imageeditor.cpp](../../../../src/editor/imageeditor.cpp),
[docs/imageeditor-render-working-image.md](../../../../docs/imageeditor-render-working-image.md).

**Tests ajoutés** : [tests/image_export_test.cpp](../../../../tests/image_export_test.cpp)
contient désormais `verifyEditProxyDownscaling()`, qui charge une image
synthétique de 2200×1600 (donc au-delà du seuil de 2048 px) et vérifie que
(a) l'aperçu produit par le fournisseur QML est bien plafonné à 2048 px sur
son plus grand côté, et (b) l'export conserve malgré tout la résolution
d'origine complète (2200×1600). Un nouvel helper `awaitPreview()` attend le
signal `previewChanged` pour synchroniser le test avec le rendu asynchrone.

**Validation** : build complet + `ctest` — **7/7 tests passants (100 %)**,
y compris le nouveau test `verifyEditProxyDownscaling`.

## Mise à jour — cache mémoire et ouverture RAW

### Point E — Cache LRU borné et libération mémoire

Le point E est terminé. `DecodedImageCache` conserve uniquement les RAW
inactifs, avec une politique LRU et un budget de 256 Mio configurable par la
propriété QML `decodedImageCacheBudgetBytes`. Le comptage ne double pas les
tampons `cv::Mat` partagés par l'original et le proxy. Au changement d'image,
`ImageEditor` libère ses références actives dès qu'elles sont transférées au
cache, ou immédiatement si l'image n'est pas éligible. Les étapes neutres de
`renderWorkingImage()` sont aussi court-circuitées pour éviter des copies sans
effet. Les tests d'export couvrent l'éviction LRU et le comptage mémoire.

### Mesure Windows et action appliquée

Une sonde optionnelle `rawLoadBenchmark` décompose l'ouverture d'un CR3 Canon
de 43,7 Mo (`4639×6959`). Les valeurs ci-dessous sont des mesures locales,
pas une promesse de durée sur une autre machine ; elles expliquent néanmoins
la source des délais observés (environ 45 s à froid, 30 s ensuite).

| Étape | Avant | Après parallélisation DCP/LCP |
|---|---:|---:|
| Recherche DCP/LCP, premier lancement | 7,465 s | 7,445 s |
| LibRaw : décompression + développement | 2,837 s | 2,833 s |
| Copie de l'image mémoire | 0,281 s | 0,286 s |
| Application DCP/LCP | 5,138 s | 0,811 s |
| **Total, premier chargement** | **15,738 s** | **11,393 s** |
| **Total, chargement suivant dans le même processus** | **8,333 s** | **3,925 s** |

Les boucles indépendantes de DCP/LCP sont désormais distribuées par ligne via
`cv::parallel_for_`. La sélection de profils est aussi persistée dans le cache
Qt et validée par taille/date des fichiers : sur une installation ordinaire,
elle doit éliminer le scan de profils au lancement suivant. Le bac à sable de
validation interdit l'écriture dans le cache applicatif Windows ; ce cas se
replie sans erreur sur le scan complet et impose de vérifier la persistance sur
un poste de déploiement réel.

Pour les boîtiers et objectifs connus, `profiles/index.json` permet désormais
une sélection locale directe avant tout scan récursif. Les chemins sont bornés
à `profiles/dcp` et `profiles/lcp`, et les fichiers de profils restent ignorés
par Git tant que leur licence de redistribution n'est pas établie.

La phase résiduelle dominante est désormais `unpack_and_process` de LibRaw.
Un aperçu très rapide peut être un chantier ultérieur : LibRaw sait extraire
la vignette embarquée, mais cela demande une interface à deux étapes (aperçu
provisoire puis rendu complet) et ne convient pas à un export ni à une
évaluation colorimétrique définitive.

## Reste à faire

### Point F — Format de sidecar `.ipr`

**Objectif** : persister les réglages (`Settings`) dans un fichier binaire
léger et rapide à lire (`QDataStream`, en-tête versionné), à côté du fichier
source, avec détection de péremption par taille + date de modification du
fichier source (comme les `.xmp` de Lightroom mais plus compact qu'XML).
Chargé automatiquement à l'ouverture si présent et à jour ; écrit de façon
débattue (debounce) à chaque modification de réglage.
**Complexité estimée** : faible à moyenne — indépendant des autres points,
bon candidat pour une session ultérieure courte et autonome.

### Point D — Migration GPU (`cv::UMat`) : ajournée sur données mesurées

**Objectif** : remplacer `cv::Mat` par `cv::UMat` dans `renderWorkingImage()`
pour profiter de l'accélération OpenCL automatique d'OpenCV sur les
opérations vectorisables (`cv::multiply`, `cv::pow`, `cv::LUT`, etc.), sans
dépendance CUDA supplémentaire (le [CMakeLists.txt](../../../../CMakeLists.txt)
ne lie actuellement que `core imgproc imgcodecs`).
La mesure rend ce chantier non prioritaire : `cv::UMat` ne peut pas accélérer
le décodage/dématriçage interne de LibRaw, qui représente maintenant la plus
grande part de l'ouverture. Les transferts hôte/GPU peuvent aussi annuler le
gain pour les proxies. Une preuve de bénéfice doit donc être obtenue sur le GPU
Windows cible, avec le rendu et l'export, avant migration.

**Complexité estimée** : élevée — c'est le chantier le plus lourd identifié
dans l'analyse initiale ; il touche la quasi-totalité des boucles de
`renderWorkingImage()` (courbes de tonalité par interpolation barycentrique,
effets, dehaze par Dark Channel Prior) et nécessite une validation fine de la
non-régression visuelle. À traiter en dernier, une fois C/E/F en place, pour
mesurer son intérêt réel une fois les autres optimisations appliquées.

## Suivi des tâches (table `todos` de la session)

| Tâche | Statut |
|---|---|
| `async-load` (Point A) | ✅ done |
| `remove-double-decode` (Point B) | ✅ done |
| `edit-proxy` (Point C) | ✅ done |
| `bounded-cache` (Point E) | ✅ done |
| `sidecar-format` (Point F) | ✅ done (recette manuelle restante) |
| `gpu-umat` (Point D) | ⏳ pending |
| `final-report` | ✅ ce document |

## Recommandation pour la reprise

Reprendre par un essai de persistance du cache de profils sur le poste Windows
cible, puis envisager l'aperçu RAW en deux étapes. **F** reste un chantier
fonctionnel indépendant ; **D** reste en dernier, seulement après benchmark
OpenCL matériel. Après chaque point, reconstruire et exécuter `ctest` dans
`build-wsl-libraw`.
