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
| E | Efficacité (calcul ou entrée/sortie évitable) |
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
| C-01 | B | `src/editor/imageeditor.cpp:1845`, `src/core/DetailProcessing.cpp:449` | La netteté ne reçoit pas `pixelScale`, contrairement au contraste local, au voile, au grain et au flou : sur l'aperçu réduit (2048 px), un rayon de 1 px agit comme environ 3 px de l'image entière. L'aperçu ne montre donc pas la netteté exportée. Nuance de Codex : cela démontre un support spatial différent, pas une équivalence exacte après redimensionnement ; tester une mire de contours à plusieurs échelles avant correction. | **Corrigé** (`de1fca6`) : rayon en pixels de l'image entière. Mire de traits isolés et d'un contour : l'aperçu se rapproche de l'export réduit d'un facteur 1,1 à 8,4 (rayons 0,5 à 2, réductions 1/2 et 1/3) ; à 0,5 px l'effet disparaît à l'échelle de l'aperçu ; export identique au bit près. **Validé dans l'état par l'utilisateur (1er novembre 2026)** |
| C-02 | B | `src/core/DetailProcessing.cpp:152` | Le débruitage de luminance utilise des échelles fixes (σ 0,8 et 1,8 px) quelle que soit l'échelle ; le seuil suit le bruit mesuré, donc l'écart aperçu/export est atténué mais non nul. | Suspicion |
| C-03 | B | `src/core/ColorPipeline.cpp:112-114` | `QImage image(...)` n'est pas contrôlée avant `memcpy` dans `scanLine(y)` : un échec d'allocation (environ 190 Mo pour 24 MP en 16 bits, sur un poste déjà chargé) écrit à une adresse nulle. Le contrôle `isNull()` vient trop tard (l. 388). | **Corrigé** (`cddbfce`) : image contrôlée avant écriture, remplie directement ; sortie identique au bit près |
| C-04 | B | `src/core/ImageExporter.cpp:138` | `TIFFClose` écrit le répertoire du DNG sans retour d'erreur : un disque plein à ce moment donne un DNG tronqué annoncé comme réussi. `TIFFFlush` avant la fermeture permet de le détecter. La correction garde une fermeture unique et nettoie l'échec, sans prétendre garantir la persistance physique après coupure. À qualifier par injection d'erreur d'E/S. | **Corrigé** (`cddbfce`) : `TIFFFlush` vérifié avant la fermeture ; injection d'erreur d'E/S non testée |
| C-05 | B | `src/core/Sidecar.cpp:53-58, 87-99` ; usage `src/editor/imageeditor.cpp:2424` | `Sidecar::read` ne borne pas les valeurs lues (exposition non contrôlée comme finie, curseurs entiers hors plage, points de courbe ni triés ni bornés à [0, 1]) alors que les setters et `setToneCurve` les normalisent. Précision de Codex : la finitude des points et les plages bruit/netteté sont déjà contrôlées ; la spline (`ToneCurve.cpp:20`) écarte une abscisse décroissante, donc une courbe désordonnée perd des points en silence, sans division par zéro. Une exposition non finie ou démesurée atteint en revanche `exp2` au rendu. | **Corrigé** (`ee0a2a0`) : valeurs bornées comme les contrôles, courbes normalisées par `ToneCurve::normalized` (partagé avec l'éditeur de courbes), valeur non finie = fichier rejeté ; `sidecarTests` |
| C-06 | B | `src/core/ImageImporter.cpp:50` | `readMetadataText` accepte une clé qui *se termine* par le nom cherché : « Model » correspond à « LensModel », « Make » à « LensMake ». Selon l'ordre des clés, un JPEG ou TIFF peut afficher l'objectif comme boîtier, même si un champ Model existe. RAW non concernés. Tests : Model et LensModel distincts ; LensModel seul ; clés qualifiées EXIF. | **Corrigé** (`be95349`) : correspondance sur le nom du champ, sans espace de noms, dans l'ordre de préférence ; trois cas testés dans `imageImportTests` |
| C-07 | E | `src/editor/imageeditor.cpp:1191-1196` | `addPointColorAt` rend les tons sur le thread de l'IHM quand l'étage n'est pas en cache (proxy entier, débruitage caméra compris en mode expérimental) : gel possible de l'interface. | Suspicion ; à mesurer |
| C-08 | E | `src/editor/imageeditor.cpp:2056-2065, 2164-2167, 2238-2243, 2289` | À l'ouverture, les empreintes SHA-256 du DCP, du LCP et du manifeste sont recalculées plusieurs fois sur le thread de l'IHM (jusqu'à quatre fois par fichier). | Confirmé par lecture ; coût à mesurer |
| C-09 | E | `src/core/Histogram.cpp:27` | Trois `srgbDecode` (puissance) par pixel à chaque rendu d'aperçu ; une table de 256 valeurs suffit. | Confirmé par lecture |
| C-10 | E | `src/core/ColorPipeline.cpp:97-114, 377, 171, 181` | Conversion de sortie : tampon 16 bits intermédiaire puis copie dans la `QImage`, et trois passes `checkRange` sur la même image. | En partie corrigé (`cddbfce`) : tampon intermédiaire supprimé ; les trois passes `checkRange` restent |
| C-11 | N | `src/editor/imageeditor.cpp:1838, 1853, 1887, 2125` | `ImageEditor` porte encore du traitement d'image (rendu des tons, flou, séparation/fusion des canaux, boucle du proxy) : écart connu à la cible « cadenceur ». | Connu ; refonte prévue |
| C-12 | N | `src/core/WorkingColorSpace.h:194` | `WorkingColor::transform` est une boucle par pixel en ligne dans l'en-tête ; la règle la place dans un `.cpp`. | Confirmé |
| C-13 | N | `src/core/Sidecar.cpp:47, 59, 73-82, 104-121` | Lecture des versions 1 à 8 avec migration des bandes du mélangeur : chemin de compatibilité au sens de la règle « sans legacy ». Il protège les retouches déjà enregistrées. **Décision de l'utilisateur (1er novembre 2026) : conservé** ; la conversion des fichiers utilisateur n'est pas du legacy, une rupture ferait perdre l'historique des retouches. | Écarté (décision) |
| C-14 | N | `src/core/Histogram.cpp:26`, `src/core/ColorAdjustments.cpp:8`, `src/core/ToneRecovery.cpp:196` | Fonctions publiques qui supposent un format (4 octets par pixel, `CV_32FC3`) sans le vérifier, contrairement aux autres modules. | Confirmé |
| C-15 | D | `src/core/ColorMixer.cpp:11, 24, 69`, `src/core/Vibrance.cpp:11, 19, 84` | `bracket`, l'interpolation entre valeurs mesurées (`response`) et `DegreesPerRadian` sont dupliqués entre le mélangeur et la vibrance. | Confirmé |
| C-16 | D | `src/core/ColorPipeline.cpp:232, 252, 256` | Matrice de Bradford écrite deux fois ; matrice sRGB→XYZ réécrite au lieu de `WorkingColor::srgbToXyz()`. | Confirmé |
| C-17 | D | `src/core/ColorPipeline.cpp:70`, `src/core/Histogram.cpp:27`, `src/core/Dehaze.cpp:202, 238, 255` | Coefficients de luminance sRGB (0,2126 ; 0,7152 ; 0,0722) recopiés dans trois modules. | Confirmé |
| C-18 | D | `src/core/Grain.cpp:45-53` | `encode`/`decode` recopient `WorkingColor::srgbEncode`/`srgbDecode` (versions `float` existantes). | Confirmé ; vérifier l'égalité au bit près |
| C-19 | D | `src/core/ImageImporter.cpp:16-17` | Normalisation des clés réécrite au lieu d'appeler `normalizedMetadataKey`. | **Corrigé** (`be95349`) |
| C-20 | D | `src/cache/DecodedImageCache.cpp:14`, `src/core/Vibrance.cpp` (`applyPixel`), `src/core/PointColor.cpp` (`apply`) | Fonctions utilisées seulement par les tests. Remarque de Codex : ce n'est pas en soi du code mort, un point d'entrée de test unitaire peut être légitime ; à arbitrer selon le contrat voulu, sans réduire la testabilité. | À arbitrer |
| C-21 | D | `src/core/PerceptualColour.cpp:67-72, 81-86` | Le jeu de paramètres « avant la couleur du point » est construit deux fois. | Confirmé |
| C-22 | D | `src/editor/imageeditor.cpp:2226, 2423, 2272, 2280, 802-812, 819-834` | Le `.ipr` est lu deux fois à chaque ouverture ; `setBusy(true)` deux fois dans `beginLoad` ; lecture des points de courbe dupliquée entre `toneCurveValue` et `setToneCurve`. | En partie corrigé (`ee0a2a0`) : `setToneCurve` passe par `ToneCurve::normalized` ; double lecture du `.ipr` et `setBusy` restent |
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

Transmis le 1er novembre 2026 par le journal partagé ; lecture dans
`.worktrees/raw_code_audit` sur `28c01d9`, sans modification ni compilation.
Lecture ciblée de son périmètre, **pas une certification exhaustive** des
modules ni des tests ; Python non audité. Aucune panne par épuisement mémoire,
disque plein ou échec de création de thread n'a été provoquée.

**Points sains relus** : `Demosaic` a une garde de jointure des threads ; les
rappels du registre RAW s'exécutent hors verrou ; le cache de profils garde ses
lecteurs actifs par `shared_ptr` et attrape les exceptions du parseur ; `main`
détruit les racines QML avant `ImageEditor`.

| Id | Cat. | Emplacement | Constat | Statut |
|---|---|---|---|---|
| X-01 | B | `src/raw/RawInput.cpp:25-28`, `src/raw/RawDevelopment.cpp:70-81` | `valid()` contrôle seulement les gains R, G, B, alors que `develop()` divise par le minimum des **quatre** gains : avec {1, 1, 1, 0}, l'entrée est acceptée et le diviseur est nul ; un second vert infini ou négatif n'est pas contrôlé. L'adaptateur LibRaw remplace normalement un G2 nul par G : défaut du contrat public pour les futurs décodeurs, pas une régression des RAW actuels. | **Corrigé** (`4a045f9`) : les quatre gains contrôlés ; tests G2 nul, négatif, NaN, infini |
| X-02 | R | `src/tools/generate_test_images.cpp:118-121` | Si `emplace_back` échoue après le démarrage d'un thread, le vecteur est détruit avec des `std::thread` joignables : `std::terminate`. Pas de garde de jointure, contrairement à `Demosaic.cpp`. | **Corrigé** (`37b14b0`) : `ParallelJobs` avec jointure RAII ; test d'échec de création du deuxième thread |
| X-03 | R | `src/providers/ThumbnailProvider.cpp:37, 79-80` | `clear()` retire du pool les réponses en attente (`autoDelete` faux) : elles ne sont ni exécutées, ni détruites, ni terminées par `finished()`, ce que Qt exige même après annulation. | **Corrigé** (`4f9cef7`) : fermeture par drainage, chaque réponse terminée sans décodage |
| X-04 | B | `src/providers/ThumbnailProvider.cpp:40-45, 129-133` | Une exception du chargeur sort de `QRunnable::run` sans être traduite en erreur ni suivie de `finished()`. | **Corrigé** (`4f9cef7`) : exception traduite en erreur, `finished()` garanti ; test avec chargeur qui lève |
| X-05 | R | `src/raw/RawDecoder.cpp:788-825` | La vignette de `dcraw_make_mem_thumb` est libérée à la main : une exception avant la ligne 825 saute `dcraw_clear_mem`. Le chemin pleine image utilise déjà un `unique_ptr` avec suppresseur (l. 624-625). | **Corrigé** (`40ce464`) : tampon détenu par `unique_ptr` avec le suppresseur LibRaw ; vérifié par lecture |
| X-06 | R | `src/profiles/PreparedProfileCache.cpp:67, 94-99` | La garde protège le chargeur, pas l'insertion LRU : si elle lève après publication de l'attente, `done` reste faux et les autres lecteurs attendent sans fin. | **Corrigé** (`a20cf4c`) : complétion garantie par une garde ; test d'échec d'allocation au nœud LRU, huit lecteurs |
| X-07 | B | `src/profiles/CameraProfileEngine.cpp:707-722, 939-969` | Un tag de table DCP présent mais invalide ou tronqué est confondu avec un tag absent : le profil est accepté en perdant une partie de son rendu, sans erreur. | **Corrigé** (`c8d8d7d`) : table absente distinguée d'une table invalide ; fixtures DCP synthétiques |
| X-08 | B | `src/profiles/CameraProfileEngine.cpp:455-456, 764` | Des valeurs DCP sont converties en entier avant validation : 0,5 devient 0 et passe ; un double hors plage `int` est une conversion non représentable. | **Corrigé** (`c8d8d7d`) : domaine entier contrôlé avant conversion |
| X-09 | R | `tests/thumbnail_provider_test.cpp:109-110` | Le résultat du dernier `waitFor` est ignoré : après expiration, les réponses sont détruites alors que `run()` peut encore les utiliser. | **Corrigé** (`4f9cef7`) : nettoyage après déblocage et jointure, expiration finale contrôlée |
| X-10 | L | `src/raw/Demosaic.cpp:185`, `src/tools/render_server.cpp:213` | Avertissements `/W4` : C4996 (initialiseur OpenCV déprécié) et C4456 (masquage de `source`). Nettoyages, pas des défauts photographiques. | Confirmé |

Corrections de Codex sur `patch_code_audit_raw`, fusionnées dans `patch_code_audit` (`c0e36c6`) : onze cibles CTest ciblées, 60 répétitions des tests de cache, vignettes et threads sans échec, compilation `/W4 /permissive-` propre. Après fusion : 44 tests CTest réussis chez Claude. Banc de parité du rendu RAW (`tests/audit_raw_render_parity.py`, 29 RAW, sortie sRGB 16 bits comparée à l'état figé `28c01d9`) : **résultat en attente**.

**Priorités de Codex** : X-01, X-02, X-03, X-04 et X-06, puis les entrées
malformées des profils et les tests. Aucune suppression de chemin ni
correction photographique sur ces seuls constats.
