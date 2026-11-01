# Audit du code

Rapport évolutif de l'audit de propreté du code demandé par l'utilisateur
le 1er novembre 2026 : conformité au C++ standard et aux règles Qt,
redondances, fuites de mémoire ou de ressources, coquilles, et respect des
règles d'[AGENTS.md](../../../AGENTS.md), dont la règle « Structure et
testabilité du code » du 31 octobre 2026.

## Campagne 1 — novembre 2026

- **État audité** : `dev` à `28c01d9` (fusion des livraisons `v0.1.65-alpha`
  et `v0.1.68-alpha`), figé pour toute la campagne.
- **Branche** : `patch_code_audit`, worktree `.worktrees/patch_code_audit`,
  portée par Claude. Codex lit le même SHA dans son propre worktree et
  transmet ses constats par le journal partagé ; Claude les intègre ici.
- **Partage** :
  - Claude : `src/core`, `src/editor`, `src/cache` et leurs tests ;
  - Codex : `src/raw`, `src/profiles`, `src/tools`, `src/app`,
    `src/providers`, le QML et leurs tests ;
  - scripts Python dans un second temps.
- **Méthode** : lecture seule pendant l'inventaire, sans correction
  photographique, refonte ni suppression. Chaque constat porte une catégorie,
  un emplacement `fichier:ligne`, une preuve et ses limites. Une suspicion
  reste une suspicion tant qu'un chemin d'exécution ou un test ne la confirme
  pas. Les bugs et fuites graves sont relus par l'autre agent avant correction.
  Les corrections viennent ensuite sur cette branche, avec tests ; un
  changement photographique est qualifié au bit près ou mesuré.

### Catégories

| Code | Catégorie |
|---|---|
| B | Bug ou comportement incorrect |
| R | Fuite ou libération de ressource (mémoire, thread, verrou, fichier) |
| N | Non-conformité aux règles (C++ standard, Qt, AGENTS.md) |
| D | Redondance ou code mort |
| L | Lisibilité, coquille, commentaire faux |

Statuts : suspicion, confirmé, corrigé, écarté (avec la raison).

### Constats — Claude

Périmètre lu : `src/core` (hors tables générées, contrôlées par leur en-tête),
`src/editor`, `src/cache`. Outils : lecture, recherche ciblée, compilation
d'audit MSVC `/W4 /permissive- /Zc:__cplusplus` de tout le dépôt
(`build-audit`, non versionné).

**Ce qui est sain**

- Aucune extension propre à un compilateur ; toutes les gardes d'inclusion
  suivent la règle `IPR_<CHEMIN>_<FICHIER>_H`.
- En `/W4 /permissive-`, le code de ce périmètre ne produit qu'un seul type
  d'avertissement, dans une table générée (C-26).
- Durées de vie : `ImageEditor::~ImageEditor` attend toutes les tâches
  asynchrones ; les deux pools de threads sont des membres détruits après
  leurs tâches ; `ImageCacheController` libère les demandes en attente puis
  arrête et rejoint son thread. Chaque réservation mémoire est rendue par
  `qScopeGuard` sur tous les chemins (chargement, préchargement, rendu,
  export, export par lot). Pas de `new`/`delete` nu hors objets Qt parentés.
- Export : fichier temporaire à côté de la destination, publication sans
  écraser un nom occupé, nettoyage garanti par `qScopeGuard`.

**Constats**

| Id | Cat. | Emplacement | Constat | Statut |
|---|---|---|---|---|
| C-01 | B | `src/editor/imageeditor.cpp:1845`, `src/core/DetailProcessing.cpp:449` | La netteté ne reçoit pas `pixelScale`, contrairement au contraste local, au voile, au grain et au flou : sur l'aperçu réduit (2048 px), un rayon de 1 px agit comme environ 3 px de l'image entière. L'aperçu ne montre donc pas la netteté exportée. | Confirmé par le chemin d'exécution ; ampleur visuelle à mesurer |
| C-02 | B | `src/core/DetailProcessing.cpp:152` | Le débruitage de luminance utilise des échelles fixes (σ 0,8 et 1,8 px) quelle que soit l'échelle ; le seuil suit le bruit mesuré, donc l'écart aperçu/export est atténué mais non nul. | Suspicion |
| C-03 | B | `src/core/ColorPipeline.cpp:112-114` | `QImage image(...)` n'est pas contrôlée avant `memcpy` dans `scanLine(y)` : un échec d'allocation (environ 190 Mo pour 24 MP en 16 bits, sur un poste déjà chargé) écrit à une adresse nulle. Le contrôle `isNull()` vient trop tard (l. 388). | Confirmé par lecture ; non reproduit |
| C-04 | B | `src/core/ImageExporter.cpp:138` | `TIFFClose` écrit le répertoire du DNG sans retour d'erreur : un disque plein à ce moment donne un DNG tronqué annoncé comme réussi. `TIFFFlush` avant la fermeture permet de le détecter. | Confirmé par lecture |
| C-05 | B | `src/core/Sidecar.cpp:53-58, 87-99` ; usage `src/editor/imageeditor.cpp:2424` | `Sidecar::read` ne borne pas les valeurs lues (exposition non contrôlée comme finie, curseurs entiers hors plage, points de courbe ni triés ni bornés à [0, 1]) alors que les setters et `setToneCurve` les normalisent. La spline suppose des abscisses croissantes. Un `.ipr` corrompu ou modifié à la main produit un rendu faux ou non fini. | Confirmé par lecture |
| C-06 | B | `src/core/ImageImporter.cpp:50` | `readMetadataText` accepte une clé qui *se termine* par le nom cherché : « Model » correspond à « LensModel », « Make » à « LensMake ». Selon l'ordre des clés, un JPEG ou TIFF peut afficher l'objectif comme boîtier. RAW non concernés. | Confirmé par lecture ; à couvrir par un test |
| C-07 | E | `src/editor/imageeditor.cpp:1191-1196` | `addPointColorAt` rend les tons sur le thread de l'IHM quand l'étage n'est pas en cache (proxy entier, débruitage caméra compris en mode expérimental) : gel possible de l'interface. | Suspicion ; à mesurer |
| C-08 | E | `src/editor/imageeditor.cpp:2056-2065, 2164-2167, 2238-2243, 2289` | À l'ouverture, les empreintes SHA-256 du DCP, du LCP et du manifeste sont recalculées plusieurs fois sur le thread de l'IHM (jusqu'à quatre fois par fichier). | Confirmé par lecture ; coût à mesurer |
| C-09 | E | `src/core/Histogram.cpp:27` | Trois `srgbDecode` (puissance) par pixel à chaque rendu d'aperçu ; une table de 256 valeurs suffit. | Confirmé par lecture |
| C-10 | E | `src/core/ColorPipeline.cpp:97-114, 377, 171, 181` | Conversion de sortie : tampon 16 bits intermédiaire puis copie dans la `QImage`, et trois passes `checkRange` sur la même image. | Confirmé par lecture |
| C-11 | N | `src/editor/imageeditor.cpp:1838, 1853, 1887, 2125` | `ImageEditor` porte encore du traitement d'image (rendu des tons, flou, séparation/fusion des canaux, boucle du proxy) : écart connu à la cible « cadenceur ». | Connu ; refonte prévue |
| C-12 | N | `src/core/WorkingColorSpace.h:194` | `WorkingColor::transform` est une boucle par pixel en ligne dans l'en-tête ; la règle la place dans un `.cpp`. | Confirmé |
| C-13 | N | `src/core/Sidecar.cpp:47, 59, 73-82, 104-121` | Lecture des versions 1 à 8 avec migration des bandes du mélangeur : chemin de compatibilité au sens de la règle « sans legacy ». Il protège les retouches déjà enregistrées. **Décision de l'utilisateur (1er novembre 2026) : conservé** ; la conversion des fichiers utilisateur n'est pas du legacy, une rupture ferait perdre l'historique des retouches. | Écarté (décision) |
| C-14 | N | `src/core/Histogram.cpp:26`, `src/core/ColorAdjustments.cpp:8`, `src/core/ToneRecovery.cpp:196` | Fonctions publiques qui supposent un format (4 octets par pixel, `CV_32FC3`) sans le vérifier, contrairement aux autres modules. | Confirmé |
| C-15 | D | `src/core/ColorMixer.cpp:11, 24, 69`, `src/core/Vibrance.cpp:11, 19, 84` | `bracket`, l'interpolation entre valeurs mesurées (`response`) et `DegreesPerRadian` sont dupliqués entre le mélangeur et la vibrance. | Confirmé |
| C-16 | D | `src/core/ColorPipeline.cpp:232, 252, 256` | Matrice de Bradford écrite deux fois ; matrice sRGB→XYZ réécrite au lieu de `WorkingColor::srgbToXyz()`. | Confirmé |
| C-17 | D | `src/core/ColorPipeline.cpp:70`, `src/core/Histogram.cpp:27`, `src/core/Dehaze.cpp:202, 238, 255` | Coefficients de luminance sRGB (0,2126 ; 0,7152 ; 0,0722) recopiés dans trois modules. | Confirmé |
| C-18 | D | `src/core/Grain.cpp:45-53` | `encode`/`decode` recopient `WorkingColor::srgbEncode`/`srgbDecode` (versions `float` existantes). | Confirmé ; vérifier l'égalité au bit près |
| C-19 | D | `src/core/ImageImporter.cpp:16-17` | Normalisation des clés réécrite au lieu d'appeler `normalizedMetadataKey`. | Confirmé |
| C-20 | D | `src/cache/DecodedImageCache.cpp:14`, `src/core/Vibrance.cpp` (`applyPixel`), `src/core/PointColor.cpp` (`apply`) | Fonctions utilisées seulement par les tests. | Confirmé ; garder ou tester via l'API réelle, à décider |
| C-21 | D | `src/core/PerceptualColour.cpp:67-72, 81-86` | Le jeu de paramètres « avant la couleur du point » est construit deux fois. | Confirmé |
| C-22 | D | `src/editor/imageeditor.cpp:2226, 2423, 2272, 2280, 802-812, 819-834` | Le `.ipr` est lu deux fois à chaque ouverture ; `setBusy(true)` deux fois dans `beginLoad` ; lecture des points de courbe dupliquée entre `toneCurveValue` et `setToneCurve`. | Confirmé |
| C-23 | D | `src/core/ImageExporter.cpp:112, 131, 276` | `writeLinearDng` supprime le fichier en échec, puis la garde de `write` le supprime à nouveau. | Confirmé, sans effet |
| C-24 | L | `src/editor/imageeditor.cpp:1864` | Commentaire de `renderTones` placé au-dessus de `perceptualParameters`. | Confirmé |
| C-25 | L | `src/editor/imageeditor.cpp:1912-1915` | En échec de `CameraDenoise::apply`, l'avertissement affiche une erreur vide (seul `applyLcp` la renseigne). | Confirmé |
| C-26 | L | `src/core/ColorMixerModel.h` (164 avertissements C4305) | Littéraux `double` dans des tableaux `float` : le générateur `scripts/build_mixer_model.py` devrait écrire le suffixe `f`. | Confirmé |
| C-27 | L | `src/core/ImageExporter.cpp:57, 114, 133` | Messages d'erreur du DNG non traduisibles (`QStringLiteral`), contrairement au reste du module. | Confirmé |
| C-28 | L | `tests/` (hors périmètre de code, signalé pour Codex) | Avertissements `/W4` dans les tests : masquage de variables (C4456, `color_mixer_qml_test.cpp:118`, `local_contrast_test.cpp:143, 153`), API OpenCV dépréciée (C4996, `raw_development_test.cpp:172-178`), valeur `[[nodiscard]]` ignorée (C4834, `ui_preferences_test.cpp:94, 103`). | Confirmé |

**Priorités proposées** : C-01, C-03, C-04, C-05 et C-06 d'abord (comportement
visible ou robustesse), puis C-07 à C-10 après mesure, puis les redondances.
C-13 est tranché : conservé. C-11 relève de la refonte
« cadenceur » déjà prévue.

### Constats — Codex

En attente de transmission.
