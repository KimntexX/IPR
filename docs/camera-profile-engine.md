# Moteur de profils caméra et optiques DCP/LCP

## Objet et périmètre

`CameraProfileEngine` charge les profils DCP (couleur) et LCP (corrections d'objectif), puis expose la recherche des profils et l'application des corrections au pipeline RAW. Il s'agit d'un moteur C++ interne ; le SDK Adobe DNG n'est pas une dépendance de production.

Le moteur contient maintenant des implémentations des fonctions décrites ci-dessous. Il n'est toutefois pas certifié conforme à l'intégralité des spécifications DNG/LCP et n'a pas encore été validé avec des profils Adobe officiels et des RAW de référence. Les tests actuels utilisent des profils synthétiques : ils valident les chemins de code et des propriétés mesurables, pas la fidélité à un rendu Adobe ou à un appareil particulier.

## Architecture

```text
Ouverture CR2/CR3/DNG
        │
        ▼
RawDecoder + LibRaw
  ├─ métadonnées boîtier/objectif
  ├─ focale et ouverture
  └─ RGB 16 bits, gamma linéaire
        │
        ├─ profil trouvé : LCP sur l'image, puis DCP
        └─ sans DCP : rendu couleur usuel LibRaw, puis LCP
        │
        ▼
ColorPipeline : RGB(A) flottant sRGB linéaire
        │
        ├─ aperçu QML
        └─ export
```

`RawDecoder` ouvre les CR2, CR3 et DNG par LibRaw, puis transmet au moteur le fabricant et le modèle du boîtier, le fabricant et le modèle d'objectif, la focale et l'ouverture. La distance de mise au point n'est actuellement pas renseignée par le backend LibRaw et reste à zéro. `CameraProfileEngine` fournit :

Pour certains CR3 Canon, LibRaw expose un niveau noir de zéro alors que les pixels de marge masquée conservent un piédestal. Si aucun niveau noir/motif n'est déjà fourni, `RawDecoder` estime alors le médian séparément pour les quatre positions CFA dans les marges hors zone active, et transmet ces niveaux à LibRaw avant le dématriçage. Les fichiers DNG avec motif noir explicite ne sont pas modifiés. Le repli est limité aux CR3 Canon et aux mesures comprises entre 2 % et 25 % du niveau blanc déclaré : la borne basse à 2 % a été abaissée après observation d'un niveau médian de 512 codes (3,1 %) sur des CR3 Canon ISO 100–200 du corpus. Si les dimensions, la CFA ou les marges ne sont pas valides, le décodage LibRaw reste inchangé. Le probe d'intégration affiche si ce repli a été utilisé.

L'estimateur est implémenté séparément dans `RawBlackLevelEstimator` afin de tester ses gardes et le calcul médian sans ouvrir un RAW réel. Les tests couvrent notamment CR2/DNG, fabricant non Canon, métadonnées de noir déjà présentes, CFA/géométrie invalide, marges avec trop peu d'échantillons et valeurs estimées hors plage. Le corpus local de validation et son manifeste de métadonnées sont décrits dans [`tests/corpus/raw-black-level/`](../tests/corpus/raw-black-level/). Il ne contient pas de copie des photographies.

Sur la paire `6V1A1236.CR3` / `6V1A1236.dng`, les valeurs mosaïque décodées par LibRaw sont identiques pixel à pixel. Le DNG porte un motif de noir de 2048 codes, tandis que LibRaw ne signale aucun noir dans le CR3 ; les médianes des marges du CR3 sont de 2053 à 2054. Après application du repli, les moyennes RGB linéaires sans profil passent de `0.1367` à `0.0153` pour le CR3, contre `0.0182` pour le DNG. Les profils DCP/LCP sont les mêmes et restent appliqués. Le DNG indique également `BaselineExposure = +0.28 EV`, ce qui rend son niveau moyen plus élevé d'environ 21 % ; aucune compensation arbitraire de cette valeur n'est appliquée au CR3.

- `defaultProfileDirectories()` : chemins de recherche par défaut ;
- `findProfiles(...)` : sélection du DCP d'après le boîtier et du LCP d'après le boîtier et l'objectif ;
- `applyDcp(...)` : application du profil couleur à une image `CV_32FC3` RGB caméra ;
- `applyLcp(...)` : sélection et application d'un modèle optique à une image `CV_32FC3`.

Les profils sont recherchés dans `profiles/dcp` et `profiles/lcp` sous le répertoire courant, puis dans le répertoire de données utilisateur Qt de l'application. Sous Windows, les emplacements Adobe suivants sont aussi parcourus directement :

- `%ProgramData%/Adobe/CameraRaw/CameraProfiles` pour les DCP ;
- `%ProgramData%/Adobe/CameraRaw/LensProfiles` pour les LCP.

Si la variable d'environnement `ProgramData` est absente sous Windows, le chemin `C:/ProgramData` est utilisé. Sous WSL, `/mnt/c/ProgramData/Adobe/CameraRaw` est détecté automatiquement s'il existe. La variable d'environnement `IPR_ADOBE_CAMERARAW_DIR` peut désigner une autre racine `CameraRaw` sur toute plateforme. Les recherches sont récursives pour les profils placés dans des sous-dossiers. Les fichiers Adobe restent dans leur emplacement d'installation et ne sont pas copiés ou modifiés par IPR. Pour utiliser des répertoires explicites dans un appel donné, `findProfiles(...)` et les options de `RawDecoder` les acceptent.

### Manifeste local rapide

Le répertoire `profiles/` versionne seulement sa structure et son fichier
`index.json`; les fichiers `.dcp` et `.lcp` placés dans `dcp/` et `lcp/` sont
ignorés par Git par défaut. Le manifeste évite la recherche récursive quand un
profil est connu : une entrée correspondante est prioritaire et le moteur ne
parcourt alors pas les répertoires Adobe. Les chemins sont relatifs à
`profiles/`, doivent rester dans ce répertoire après résolution canonique et
doivent désigner des fichiers existants ; une entrée invalide est ignorée et le
scan habituel reste disponible.

```json
{
  "version": 1,
  "entries": [
    {
      "cameraMake": "Canon",
      "cameraModel": "Canon EOS R6 Mark III",
      "lensMake": "Canon",
      "lensModel": "Canon RF 24-70mm F2.8 L IS USM",
      "dcp": "dcp/Canon-EOS-R6-Mark-III.dcp",
      "lcp": "lcp/Canon-RF-24-70mm-F2.8-L-IS-USM.lcp"
    }
  ]
}
```

Une entrée DCP doit renseigner `cameraModel`; une entrée LCP doit renseigner
`lensModel`. Les fabricants sont facultatifs mais recommandés. Les chaînes sont
comparées sans casse ni ponctuation. Les profils Adobe installés dans
`C:\ProgramData\Adobe\CameraRaw` ne doivent pas être ajoutés au dépôt sans une
autorisation de redistribution vérifiée ; consulter la note juridique associée.

La sélection par défaut est mise en cache par combinaison boîtier/objectif et par racines de recherche. Le premier niveau est en mémoire pour la session ; le second est un petit fichier INI dans le répertoire de cache Qt (`IPR-profile-selection-v1.ini`) pour éviter de rescanner les milliers de profils Adobe au lancement suivant. Chaque chemin DCP/LCP mémorisé est validé par sa présence, sa taille et sa date de modification. En cas de fichier absent, modifié, non inscriptible ou illisible, le moteur refait une recherche complète : le cache ne peut donc pas rendre un profil périmé obligatoire. Après l'installation ou le retrait de profils pendant que l'application est ouverte, redémarrer IPR pour rafraîchir la sélection.

Les restrictions du profil doivent correspondre aux métadonnées disponibles. Un LCP qui ne restreint pas le boîtier est admissible si le nom du fabricant/modèle d'objectif correspond ; lorsqu'une restriction caméra est présente, elle doit aussi correspondre. Un nom de profil ne remplace pas une restriction explicite, sauf pour le chemin de repli du DCP.

Lorsque plusieurs LCP correspondent au boîtier, IPR évalue tous les candidats et retient celui dont l'identifiant d'objectif (`Lens` ou `LensPrettyName`) correspond le plus précisément aux métadonnées RAW. Une égalité exacte est prioritaire ; sinon, l'identifiant doit partager à la fois des termes significatifs et des caractéristiques numériques avec l'objectif source. Un identifiant réduit à une seule plage focale, telle que `24-105mm`, n'est donc pas suffisant pour sélectionner un profil : il ne peut plus faire choisir le profil Sigma 24-105 mm f/4 pour un Canon RF 24-105 mm f/2,8. Les coefficients géométriques de haut ordre des LCP Adobe officiels sont acceptés jusqu'à une borne finie adaptée à ces profils, puis restent contrôlés avant l'application.

## DCP : couleur

### Lecture

Le lecteur reconnaît l'enveloppe TIFF DCP en ordre d'octets little-endian ou big-endian et extrait les champs suivants lorsqu'ils existent :

| Élément | Tags DNG | Utilisation |
|---|---:|---|
| ColorMatrix1/2 | 50721/50722 | Conversion de repli quand une ForwardMatrix manque |
| CalibrationIlluminant1/2 | 50778/50779 | Association approximative de l'illuminant à une température |
| HueSatMapDims / Data1 / Data2 | 50937–50939 | Table principale et table secondaire par illuminant |
| ProfileToneCurve | 50940 | Courbe tonale en points (x,y) |
| ForwardMatrix1/2 | 50964/50965 | Transformation caméra vers XYZ utilisée en priorité |
| ProfileLookTableDims / Data | 50981/50982 | Table Look |
| HueSatMapEncoding / ProfileLookTableEncoding | 51107/51108 | Choix entre valeurs d'entrée linéaires et sRGB |
| BaselineExposureOffset | 51109 | Compensation en stops |

Le parseur vérifie les tailles, dimensions, types numériques, limites de valeurs et la présence d'au moins une matrice exploitable. La taille maximale de fichier est limitée à 64 Mio.

### Application et ordre de traitement

1. Interpolation entre les deux illuminants à partir de la température transmise par le décodeur, sur une échelle de température réciproque. Les codes d'illuminant reconnus sont convertis en températures de référence approximatives.
2. Interpolation élément par élément des matrices ForwardMatrix. Si elles sont absentes, le moteur interpole les ColorMatrix, les inverse et adapte le blanc vers D50 avec Bradford.
3. Transformation de l'image RGB caméra vers XYZ puis vers sRGB linéaire.
4. Compensation de l'exposition de base, sous forme de facteur `2^stops`.
5. Interpolation des tables Hue/Sat par illuminant si leurs dimensions correspondent. Les décalages de teinte suivent le chemin circulaire le plus court.
6. Application successive de la Hue/Sat Map puis de la LookTable. Les tables sRGB sont évaluées après encodage sRGB et reconverties en linéaire ; les valeurs de HueShift sont traitées en degrés.
7. Application de la courbe tonale si elle est présente.

Les tables sont interpolées trilinéairement dans leurs axes teinte, saturation et valeur. Si les dimensions des tables des deux illuminants diffèrent, l'implémentation choisit la table de l'illuminant le plus proche au lieu de rééchantillonner.

### Limites DCP

- La sélection de température de balance des blancs fournie par LibRaw est une estimation à partir de `cam_mul` et de l'inverse de `cam_xyz` (approximation de McCamy), pas la température EXIF native ni une valeur garantie.
- L'interpolation des ForwardMatrix est un modèle simplifié. La prise en charge de `CameraCalibration`, `AnalogBalance`, `AsShotNeutral`, `ReductionMatrix`, des matrices de profil multiples et de tous les états du pipeline DNG n'est pas complète.
- Le repli ColorMatrix + Bradford est une approximation et non une implémentation exhaustive du pipeline DNG.
- Un DCP Adobe réel a été chargé et appliqué au CR3 du boîtier testé ; le résultat n'a pas encore été comparé à une sortie Adobe de référence pixel à pixel.
- La courbe tonale est interpolée par segments linéaires ; sa fidélité à la méthode de rendu du logiciel ayant créé le profil n'est pas garantie.

## LCP : corrections optiques

### Lecture et choix de calibration

Le parseur XML accepte les paramètres numériques sous forme d'attributs ou de texte d'élément. Il lit plusieurs éléments `PerspectiveModel` ou `FisheyeModel`, ainsi que les sous-modèles de géométrie, vignettage, aberration chromatique rouge-vert et bleu-vert. Les champs associés comprennent focale, distance de mise au point, ouverture, centres, échelle, distorsion radiale/tangentielle, coefficients de vignettage et paramètres CA.

Si un profil contient plusieurs modèles, le moteur évalue leur distance normalisée par rapport aux paramètres de capture disponibles, retient les deux plus proches puis interpole leurs coefficients lorsqu'ils décrivent la même correction. Les centres et l'échelle concernés sont interpolés avec les coefficients. Si une dimension de capture manque, elle n'entre pas dans le score. Si aucune dimension n'est disponible, le premier modèle est retenu.

### Application

1. Construction d'une carte de remappage géométrique radiale/tangentielle et application par interpolation bilinéaire.
2. Remappage des canaux rouge et bleu séparément avec leurs modèles respectifs ; le vert sert de référence. Les buffers de ce pipeline sont RGB, donc rouge = indice 0 et bleu = indice 2.
3. Compensation du vignettage en inversant l'atténuation radiale évaluée à partir des coefficients `VignetteModelParam1/2/3`. Le rayon est normalisé avec la focale en unités de 35 mm et la plus grande dimension de l'image ; le gain est borné pour éviter les explosions lorsque le modèle approche une atténuation nulle.

Le remappage LCP est appliqué avant le DCP lorsque ce dernier est sélectionné dans le décodeur RAW. Sans DCP, LibRaw fournit le RGB sRGB linéaire et le LCP est appliqué après conversion dans le tampon de travail.

### Performance et diagnostic

Les parcours indépendants par ligne de l'application DCP, de la construction des cartes LCP, de l'aberration chromatique et du vignettage sont parallélisés avec `cv::parallel_for_`. Les calculs par pixel, leur ordre et les bornes numériques restent identiques ; seule la répartition des lignes entre cœurs change. Le gain dépend donc du processeur et du nombre de profils effectivement appliqués.

Pour instrumenter un RAW sans modifier le comportement normal, définir `IPR_LOG_RAW_TIMING=1` puis lancer la cible optionnelle `rawLoadBenchmark` (CMake : `-DIPR_BUILD_RAW_BENCHMARK=ON`). Elle affiche les étapes `open_file`, `find_profiles`, `unpack_and_process`, `make_memory_image`, `color_and_lens_profiles` et le total. Ce diagnostic est volontairement désactivé par défaut.

### Limites LCP

- Les unités, conventions de calibration et équations de tous les profils LCP Adobe ne sont pas entièrement reproduites ; la formule radiale/tangentielle actuellement prise en charge est une interprétation restreinte des champs lus.
- L'interpolation porte uniquement sur les deux modèles les plus proches, sans reconstruction complète des grilles de calibration LCP ni interpolation multidimensionnelle normalisée Adobe.
- La distance de mise au point n'est pas alimentée par LibRaw ; certaines sélections utiliseront donc seulement focale et ouverture.
- Les profils avec métadonnées complexes, aliases de boîtier/objectif ou variantes de structure non couvertes par le parseur peuvent ne pas être sélectionnés ou appliqués correctement.
- Aucune calibration réelle de vignettage ou d'aberration chromatique n'a été fournie pour valider quantitativement le rendu.
- La normalisation du rayon de vignettage suppose un format 35 mm ; les facteurs de recadrage spécifiques ne sont pas encore tirés des métadonnées de l'appareil.

## Installation des profils et vérifications visuelles

Placer les fichiers autorisés dans :

```text
profiles/
  dcp/
    appareil.dcp
  lcp/
    objectif.lcp
```

Avant un déploiement, vérifier les métadonnées réellement retournées par LibRaw : les restrictions de modèle doivent correspondre aux chaînes caméra et objectif du RAW. Tester chaque profil avec des images représentatives, des scènes neutres et colorées, plusieurs focales/ouvertures et des crops de bords pour la CA et le vignettage. Comparer à un logiciel de référence en harmonisant balance des blancs, exposition, espace colorimétrique, courbe et options de rendu.

Le moteur ne fabrique pas de profils caméra ou optique à partir du nom de l'équipement. Un profil calibré nécessite une source autorisée, ou des données de calibration mesurées : RAW avec charte couleur pour le rendu caméra ; mires et prises de vue couvrant les réglages optiques pertinents pour l'objectif. Des fichiers de profil synthétiques ne doivent jamais être utilisés comme profils réels.

## Tests automatisés

La cible `cameraProfileEngineTests` génère des DCP/LCP de petite taille dans un répertoire temporaire. Elle couvre notamment :

- ForwardMatrix et repli sans ForwardMatrix ;
- interpolation multi-illuminants de matrices et tables secondaires ;
- circularité des HueShift et tables encodées en sRGB ;
- sélection de profil par restrictions caméra/objectif ;
- plusieurs calibrations optiques, éléments XML texte, géométrie, vignettage et CA ;
- remappage séparé des canaux rouge/bleu avec le vert inchangé.

Exécution sous Linux/WSL depuis la racine du dépôt :

```sh
cmake -S . -B build -G Ninja -DIPR_ENABLE_LIBRAW=ON -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Les succès des tests synthétiques et de compilation ne constituent pas une certification DNG, ni une preuve de rendu colorimétrique fidèle aux profils de production.

### Vérification manuelle d'un CR3 réel avec les profils Adobe

Quand LibRaw est activé, CMake construit `rawProfileIntegrationProbe`. Il prend le chemin du RAW et les répertoires de profils et affiche les noms DCP/LCP sélectionnés, les métadonnées LibRaw et un écart pixel moyen entre le rendu profilé et un développement de référence sans profil :

```sh
./build/rawProfileIntegrationProbe image.cr3 \
  /mnt/c/ProgramData/Adobe/CameraRaw/CameraProfiles \
  /mnt/c/ProgramData/Adobe/CameraRaw/LensProfiles
```

Le probe retourne une erreur si les profils attendus ne sont pas sélectionnés, si la comparaison ne montre aucune différence mesurable ou si le décodage échoue. Il n'applique pas les réglages de l'interface ; ceux-ci sont remis à zéro par `ImageEditor` à l'ouverture.

Le paramètre optionnel `--camera-wb` remplace la balance des blancs automatique par celle enregistrée par le boîtier. Il peut être ajouté après le chemin d'aperçu, ou seul, pour comparer les deux modes :

```sh
./build/rawProfileIntegrationProbe image.cr3 --default aperçu.png --camera-wb
```
