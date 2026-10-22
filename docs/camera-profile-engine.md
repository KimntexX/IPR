# Moteur de profils caméra et optiques DCP/LCP

## Objet et périmètre

`CameraProfileEngine` charge les profils DCP (couleur) et LCP (corrections d'objectif), puis expose la recherche des profils et l'application des corrections au pipeline RAW. Il s'agit d'un moteur C++ interne ; le SDK Adobe DNG n'est pas une dépendance de production.

Le moteur contient les implémentations décrites ci-dessous, sans certification de conformité à l'intégralité des spécifications DNG/LCP. Les tests unitaires utilisent des profils synthétiques ; ils sont complétés par les rejeux de RAW Canon et de leurs DCP Camera Neutral dans le rapport de base colorimétrique. Ces contrôles sur un corpus limité ne prouvent ni une fidélité universelle au rendu Adobe ni la prise en charge de tous les boîtiers.

## Architecture

La directive [ADR-0008](decisions/ADR-0008-separation-travail-affichage-export.md)
sépare développement RAW, affichage ICC et export. Le profil écran ne change
pas la calibration ou le DCP. Le master cible choisi est désormais Rec.2020
linéaire D65 ([ADR-0009](decisions/ADR-0009-espace-travail-rec2020-lineaire.md)),
introduit le 15 octobre 2026 et intégré dans `dev` ; relecture du présent contrat le 22 octobre 2026. Les tables DCP conservent leur domaine
ProPhoto ; leur raccordement au master a été porté selon le
[plan d'intégration](architecture/integration-rec2020.md).

La [roadmap ouverture RAW multi-boîtiers](architecture/roadmap-ouverture-raw.md)
ordonne les prochains essais HL, la validation du rendu, les caches de profils
et la généralisation des calibrations. Les futurs R5 (CR3) et 6D Mark II (CR2)
compléteront le corpus ; leur annonce ne vaut pas validation.

### Politique courante de calibration — relecture du 22 octobre 2026

La calibration RAW est **active par défaut** pour une nouvelle recette.
Son interrupteur et le choix du manifeste sont dans les préférences avancées
RAW, pas dans les réglages ordinaires. Le choix explicite est mémorisé par image.
Voir [ADR-0007](decisions/ADR-0007-calibration-raw-par-defaut.md).

Une activation demandée ne garantit pas une application : le catalogue doit
reconnaître le format, le décodeur, le boîtier, le profil et le mode capteur.
Les règles courantes n'imposent plus une liste de cinq empreintes de RAW ni
d'ISO. Un boîtier inconnu n'hérite pas silencieusement de la calibration Canon.
Voir [ADR-0011](decisions/ADR-0011-regles-raw-par-mode-et-configuration-vivante.md)
et [ADR-0012](decisions/ADR-0012-formats-extensibles-et-caracterisations.md).

**Orientation à préparer (8 octobre 2026) :** rendre le backend RAW
remplaçable si les limitations de LibRaw persistent ou si des problèmes
de stabilité sont confirmés. Séparer lecture du conteneur/métadonnées,
développement de la mosaïque et rendu DCP/LCP IPR, avec des contrats
explicites sur les noirs/blancs, la WB et les gains déjà appliqués.
Le registre de décodeurs existant est un point d'extension, pas encore
une abstraction complète du développement RAW. Aucun remplacement
immédiat n'est décidé et aucune instabilité générale de LibRaw n'est
démontrée par les mesures actuelles. Voir l'[orientation et les critères
de validation, §24.7](validation/analyses/analyse-base-colorimetrique.md).

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
ColorPipeline : RGB(A) flottant Rec.2020 linéaire D65
        │
        ├─ aperçu QML
        └─ export
```

`RawDecoder` ouvre les CR2, CR3 et DNG par LibRaw, puis transmet au moteur le fabricant et le modèle du boîtier, le fabricant et le modèle d'objectif, la focale et l'ouverture. La distance de mise au point n'est actuellement pas renseignée par le backend LibRaw et reste à zéro. `CameraProfileEngine` fournit :

**Lecture permanente des noirs (20 octobre 2026, [ADR-0020](decisions/ADR-0020-lecture-permanente-des-noirs.md))** : `RawDecoder` mesure désormais le noir optique de **chaque** RAW qui en a (zones Canon déclarées, zones masquées du format pour les autres marques, marges en dernier recours pour Canon seulement) et le compare à LibRaw : la valeur de LibRaw est gardée si elle concorde (écart ≤ max(8, blanc/1000) codes), remplacée sinon. Les paragraphes suivants décrivent le cas d'origine.

Pour certains CR3 Canon, LibRaw expose un niveau noir nul ou de petits niveaux incohérents alors que les pixels masqués conservent un piédestal. Lorsque les gardes existantes autorisent le repli, `RawDecoder` estime une médiane séparée pour les quatre canaux CFA avant dématriçage. Depuis le correctif du 7 octobre 2026, une zone `LeftOpticalBlack` Canon valide est préférée ; à défaut, `UpperOpticalBlack` est utilisée. Leurs bornes inclusives sont contrôlées contre les dimensions RAW et converties en rectangles de mesure. La gauche est privilégiée car le corpus ISO 3200 montre un léger décalage sur la zone supérieure. Le pas mémoire `raw_pitch` est respecté. Si aucune zone déclarée n'est valide, le repli historique sur les marges est conservé, avec ses limites connues ; les marges du recadrage photographique ne sont pas toutes masquées.

Les métadonnées de noir qui concordent avec la mesure ne sont pas remplacées, quel que soit le format. Les gardes de géométrie, CFA, nombre d'échantillons et niveaux compris entre 2 % et 25 % du maximum restent actives. Sur les six RAW audités, les nouvelles médianes sont 512 pour cinq scènes et 2048 pour 9421 (contre 2052–2053 avec les marges mélangées). Aucun noir fixe par modèle n'est ajouté. La sélection incorrecte de disposition ColorData version 66 dans LibRaw 0.22.2 n'est pas corrigée dans la bibliothèque externe par ce changement : le moteur contourne le noir invalide par mesure, sans réinterpréter les niveaux blancs ni les gains WB. Voir le [rapport évolutif, §14–15](validation/analyses/analyse-base-colorimetrique.md).

L'estimateur est implémenté séparément dans `RawBlackLevelEstimator` afin de tester ses gardes et le calcul médian sans ouvrir un RAW réel. Les tests couvrent notamment la décision (noir concordant gardé, absent ou déviant remplacé, motif spatial gardé), les autres marques sans zone déclarée, CFA/géométrie invalide, marges avec trop peu d'échantillons et valeurs estimées hors plage. Le corpus local de validation et son manifeste de métadonnées sont décrits dans [`tests/corpus/raw-black-level/`](../tests/corpus/raw-black-level/). Il ne contient pas de copie des photographies.

Sur la paire `6V1A1236.CR3` / `6V1A1236.dng`, les valeurs mosaïque décodées par LibRaw sont identiques pixel à pixel. Le DNG porte un motif de noir de 2048 codes, tandis que LibRaw ne signale aucun noir dans le CR3 ; les médianes des marges du CR3 sont de 2053 à 2054. Après application du repli, les moyennes RGB linéaires sans profil passent de `0.1367` à `0.0153` pour le CR3, contre `0.0182` pour le DNG. Les profils DCP/LCP sont les mêmes et restent appliqués. Le DNG indique également `BaselineExposure = +0.28 EV`, ce qui rend son niveau moyen plus élevé d'environ 21 % ; aucune compensation arbitraire de cette valeur n'est appliquée au CR3.

- `defaultProfileDirectories()` : chemins de recherche par défaut ;
- `findProfiles(...)` : sélection du DCP d'après le boîtier et du LCP d'après le boîtier et l'objectif ;
- `applyDcp(...)` : application du profil couleur à une image `CV_32FC3` RGB caméra ;
- `applyLcp(...)` : sélection et application d'un modèle optique à une image `CV_32FC3`.

Les racines de profils sont, dans l'ordre construit par `defaultProfileDirectories()` :
`profiles` près de l'exécutable, `../Resources/profiles` (bundle macOS),
`../share/IPR/profiles` (installation), les répertoires `IPR/profiles` trouvés
via Qt `GenericDataLocation`, `profiles` du répertoire courant, puis
`AppDataLocation/profiles` pour les profils personnels. Les sous-dossiers
DCP/LCP sont parcourus dans ces racines. Sous Windows, la racine Adobe permet
aussi de rechercher les emplacements suivants :

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
| HueSatMapEncoding / ProfileLookTableEncoding | 51107/51108 | Encodage linéaire ou sRGB de l'axe Valeur HSV des tables 3D |
| BaselineExposureOffset | 51109 | Compensation en stops |

Le parseur vérifie les tailles, dimensions, types numériques, limites de valeurs et la présence d'au moins une matrice exploitable. La taille maximale de fichier est limitée à 64 Mio.

### Application et ordre de traitement

1. Interpolation entre les deux illuminants à partir de la température transmise par le décodeur, sur une échelle de température réciproque. Les codes d'illuminant reconnus sont convertis en températures de référence approximatives.
2. Interpolation élément par élément des matrices ForwardMatrix. Si elles sont absentes, le moteur interpole les ColorMatrix, les inverse et adapte le blanc vers D50 avec Bradford.
3. Transformation de l'image RGB caméra vers XYZ D50 puis vers ProPhoto linéaire pour le traitement du profil.
4. Compensation de l'offset **du profil** `BaselineExposureOffset`, sous forme de facteur `2^stops`. Ce champ ne représente pas à lui seul l'exposition de base du négatif.
5. Interpolation des tables Hue/Sat par illuminant si leurs dimensions correspondent. Les décalages de teinte suivent le chemin circulaire le plus court.
6. Application successive de la Hue/Sat Map puis de la LookTable dans ProPhoto. Pour une table 3D encodée en sRGB, seule la composante Valeur HSV est encodée avant interpolation puis décodée après correction ; teinte et saturation restent calculées sur les canaux linéaires. L'encodage est ignoré pour les tables à une seule division Valeur (2,5D). HueShift est exprimé en degrés.
7. Application de la courbe tonale si elle est présente, dans ProPhoto, selon l'interpolation par canal existante.
8. Conversion ProPhoto D50 vers Rec.2020 linéaire D65, sans gamut mapping à cette étape. Le raccordement PCS/D50 déjà validé est composé directement vers les nouvelles primaires pour conserver les neutres. En l'absence de tables et de courbe, le chemin matriciel XYZ vers Rec.2020 direct est conservé. Les retours de changement de profil et WB caméra utilisent le même contrat ; aucun tampon sRGB intermédiaire n'est créé.

Correctif du 7 octobre 2026 : les tables étaient auparavant appliquées à tort dans sRGB. Les coefficients ProPhoto/XYZ sont des définitions d'espace colorimétrique, pas des gains empiriques du boîtier. Le traitement des tables reste limité au domaine SDR [0,1] ; ce changement ne constitue pas une prise en charge HDR exhaustive. L'algorithme d'encodage de l'axe Valeur est décrit par le [SDK DNG Adobe](https://android.googlesource.com/platform/external/dng_sdk/+/refs/heads/main/source/dng_tag_values.h).

Les tables sont interpolées trilinéairement dans leurs axes teinte, saturation et valeur. Si les dimensions des tables des deux illuminants diffèrent, l'implémentation choisit la table de l'illuminant le plus proche au lieu de rééchantillonner.

### Limites DCP

**Contrat d'exposition à compléter (investigation du 8 octobre 2026).**
`BaselineExposure` du négatif et `BaselineExposureOffset` du DCP sont
distincts. Les six copies de diagnostic produites par Adobe DNG Converter
18.6 annoncent +0,26 EV ou +0,28 EV pour le premier, et −0,5 EV pour le
profil Camera Neutral. Dans le chemin CR3 audité, LibRaw fournit −999
pour le premier champ : cela signifie inconnu, pas zéro mesuré ni −999 EV.
L'expérience indépendante améliore les cinq comparaisons Neutral en
ajoutant la valeur DNG, mais conserve un résidu et des risques de clipping.
Aucune constante n'a été intégrée : normalisation, WB, CameraCalibration
et Stage3Gain doivent être réconciliés pour éviter de compter deux fois
un gain. Adobe DNG Converter reste un témoin local, pas une dépendance
d'ouverture. Voir le [rapport évolutif, §22](validation/analyses/analyse-base-colorimetrique.md#22-ordre-dcp-exposition-de-base-adobe-et-gamut--reprise-du-8-octobre-2026).

**Complément de diagnostic, §23 :** les blancs des six DNG concordent avec
le candidat Canon `specular` des décodages A/B indépendants. Ce blanc,
combiné à l'exposition de base, ramène l'écart de luminance moyenne sous
0,009 EV sur les cinq références Neutral. La courbe préservant les teintes
améliore ensuite les cinq ΔE00 moyens, mais 9421 conserve des résidus
localisés. La WB LibRaw concorde déjà avec les DNG ; la calibration
diagonale de ce lot ne justifie pas un gain bleu supplémentaire.
Ces mesures précèdent l'intégration contrôlée décrite ci-dessous ; elles
ne définissent pas une règle universelle de lecture Canon. Stage3Gain
n'a pas été mesuré par exécution du SDK.
Voir le [rapport évolutif, §23](validation/analyses/analyse-base-colorimetrique.md).

- La sélection de température de balance des blancs fournie par LibRaw est une estimation à partir de `cam_mul` et de l'inverse de `cam_xyz` (approximation de McCamy), pas la température EXIF native ni une valeur garantie.
- L'interpolation des ForwardMatrix est un modèle simplifié. La prise en charge de `CameraCalibration`, `AnalogBalance`, `AsShotNeutral`, `ReductionMatrix`, des matrices de profil multiples et de tous les états du pipeline DNG n'est pas complète.
- Le repli ColorMatrix + Bradford est une approximation et non une implémentation exhaustive du pipeline DNG.
- Les profils Adobe Standard et Camera Neutral réels ont été comparés aux TIFF Lightroom au pas 8. La proximité Neutral est améliorée sur le lot contrôlé, mais la fidélité absolue et la parité Lightroom restent non validées.
- La courbe tonale est interpolée par segments linéaires ; sa fidélité à la méthode de rendu du logiciel ayant créé le profil n'est pas garantie.

### Résolution de la calibration RAW/DCP

`RawNormalization::resolve()` ne dépend pas de LibRaw pour ses tests. Il
interroge un catalogue JSON de caractérisation borné à 1 Mio et 256 règles.
Le schéma courant est **4** ; les règles par modèle/profil et mode sont
séparées des photographies de validation. Les SHA-256 restent utiles à la
traçabilité des ressources et des campagnes, pas comme liste des seules
photos autorisées à s'ouvrir.

Le chemin du manifeste arrive par `RawDecoder::Options::normalizationManifest`.
L'IHM résout sa configuration externe ou la ressource embarquée ; elle ne
recherche pas un ancien dossier de build. Les recettes des outils de test
fixent explicitement le manifeste et ignorent l'activation par environnement.
Une règle invalide, ambiguë ou incompatible est refusée sans empêcher
l'ouverture non calibrée, avec diagnostic de l'application effective.

Le blanc sélectionné est installé avant `dcraw_process()`, dans les deux
structures couleur LibRaw, avec `adjust_maximum_thr = 0`. Le gain de base
caméra est transmis séparément de l'offset du profil. La compensation empirique
par profil/modèle reste distincte de `BaselineExposureOffset` lu dans le DCP :
ne pas compter deux fois un gain ni présenter une compensation de rendu
comme une mesure physique du capteur.

Le catalogue est une configuration vivante : ses nouveaux octets sont validés,
son digest invalide les entrées de cache correspondantes et la recette résolue
est actualisée. Les DCP/LCP gardent leurs contrôles d'identité indépendants.
Changer les niveaux de développement peut nécessiter un redécodage. La courbe
préservant la teinte est activée selon la règle de rendu ; ce n'est pas une
garantie de teinte Lab constante ni une prise en charge HDR exhaustive.

Les anciennes restrictions par fichiers, schémas et versions LibRaw,
ainsi que les mesures de cinq puis dix-neuf RAW, sont conservées dans le
[rapport RAW/profils](validation/analyses/analyse-base-colorimetrique.md),
notamment les campagnes des §24 et suivants. Elles ne définissent plus le
contrat courant et ne sont pas recalculées par cette mise à jour documentaire.

### Persistance et commandes optiques (9 octobre 2026)

Le sidecar **v6** ajoute chemin/SHA-256 du LCP, état de résolution (y compris
l'absence de profil), activation distorsion/vignettage et activation de
l'aberration chromatique latérale. Le lecteur accepte v1–v6 ; v1–v5 conservent
la politique antérieure, sélection automatique et deux corrections activées.
Les anciens exécutables ne lisent pas v6.

Le LCP résolu est figé lors du premier chargement réussi. À la restauration,
une ressource absente/modifiée n'est pas remplacée silencieusement : la recette
reste identifiable, les corrections LCP sont suspendues et un avertissement
est affiché. La relocalisation nécessite même nom et SHA-256 sous `profiles/`
de l'exécutable. Un profil existant doit aussi correspondre au boîtier/objectif.
La copie des retouches par lot conserve les choix optiques de chaque cible.

Deux cases indépendantes sont disponibles dans le groupe **Corrections optiques**, immédiatement avant Export :

- **Activer la correction de l'objectif** : géométrie et vignettage disponibles.
- **Supprimer l'aberration chromatique** : recalage latéral rouge/bleu, uniquement
  si ces modèles sont présents dans le LCP ; pas de détection automatique sans
  profil ni suppression des franges longitudinales violettes/vertes.

Le statut distingue les cases cochées des corrections effectivement appliquées.
Les choix sont restaurés avant décodage, inclus dans la clé du cache, et appliqués
à la base pleine résolution utilisée par l'export. Les retouches restent
conservées. Changer les options optiques nécessite un redécodage si l'état n'est
pas en cache : le tampon rapide DCP est déjà corrigé par le LCP et ne peut pas
être réutilisé avec d'autres options optiques. Les valeurs de ce tampon doivent
correspondre aux activations et à l'empreinte LCP courantes.

La précision des modèles optiques et le comportement aux bords restent à
vérifier visuellement sur le corpus ; les tests synthétiques ne certifient
pas une parité Lightroom.

### Sélection dans l'IHM

Le groupe **Développement RAW / Profil** propose les DCP compatibles et la
WB automatique. La calibration est dans **Préférences / Avancé · RAW** ;
objectif et aberration chromatique sont dans **Corrections optiques**.

Camera Neutral est préféré lors d'une nouvelle ouverture si un profil
compatible est disponible. Le choix résolu et les options RAW/optique sont
restaurés avant décodage depuis le sidecar **v6**. Le statut doit indiquer
la ressource réellement appliquée ; ni le libellé commercial ni la case
cochée ne suffisent à établir l'état du rendu.

Le changement de DCP réutilise le signal caméra lorsqu'il est compatible,
sans vider l'aperçu, réinitialiser le zoom ou effacer les retouches.
WB de décodage, niveaux de calibration ou options optiques peuvent exiger
un redécodage. Le cache de pixels géré par `ImageCacheController` peut retenir
le proxy et sa source caméra réduite ; l'export reconstruit la pleine définition
si nécessaire. Ce cache est distinct du cache borné des profils préparés.

Le bouton général réinitialise les retouches, pas tous les choix RAW.
La réinitialisation profil/calibration est distincte et dans les préférences
avancées. Le LCP résolu est bien enregistré par image : il n'est pas remplacé
silencieusement par une autre sélection automatique à la restauration.

Les chemins locaux de profils et l'INI portable restent indépendants du
répertoire des photos. Voir [la distribution Windows](architecture/build-windows-standalone.md),
[les préférences](architecture/ui-preferences.md),
[le contrôle mémoire](architecture/controle-memoire-images.md)
et [ADR-0006](decisions/ADR-0006-restauration-raw-et-retouche-profil.md).

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

Pour évaluer la base colorimétrique avant les réglages, suivre le
[protocole v1](validation/protocols/protocole-base-colorimetrique-v1.md) :
diagnostic sur six scènes existantes, profils et WB explicités, puis charte
physique pour une mesure de fidélité. Les nuanciers de prises d'escalade
permettent une identification, pas une référence Lab garantie de la prise
photographiée. Comparaison Lightroom, conformité DCP et fidélité physique
doivent recevoir des conclusions distinctes.

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

## Retouche température/nuance avant DCP — 11 octobre 2026

Un RAW développé avec DCP conserve désormais un `DcpWorkingContext` immuable :
RGB capteur après balance LibRaw et corrections optiques, blanc capteur issu
des inverses des multiplicateurs effectivement employés, chemin DCP, température
source et options de rendu. Les réglages manuels ne redécodent pas le RAW.
L'auto WB et la normalisation modifient toujours le décodage amont.

Le pipeline est : source capteur → gains température/nuance → DCP (matrices,
tables et courbe) → réglages photographiques → gamut/encodage de sortie.
Pour un TIFF/JPEG, sans source capteur ni DCP, la balance reste une adaptation
dans l'espace RGB linéaire du fichier : il s'agit d'un autre domaine d'entrée,
pas d'un mode de compatibilité avec l'ancien traitement RAW.

La `ColorMatrix` interpolée en température réciproque transforme XYZ vers le
capteur. Son inverse appliqué au blanc capteur donne un blanc XYZ de référence.
On conserve l'écart de ce blanc au lieu planckien en CIE 1960 `uv`, puis on
déplace le blanc selon la température cible et la normale au lieu pour la
nuance. La distance vaut `-nuance/3000`, selon la convention de distance du
[DNG SDK, `dng_temperature.cpp`](https://android.googlesource.com/platform/external/dng_sdk/+/refs/heads/android14-prebuilt-test/source/dng_temperature.cpp).
IPR utilise une approximation continue de Krystek, pas la table Robertson ni
le modèle propriétaire de Lightroom. Le 11 octobre 2026, l'utilisateur a
accepté visuellement le rendu, légèrement plus violet à 2000 K et plus orangé
à 50000 K que Lightroom. La généralisation attend des retours sur d'autres
boîtiers, objectifs et profils ; aucune parité numérique n'est revendiquée.

Pour chaque canal, `gain = neutre_source / neutre_cible`, puis les gains sont
normalisés sur le vert. Aucun poids ne dépend du nom commercial du profil.
Nuance 0 est relative au blanc enregistré, pas une attribution universelle
de la valeur +5 donnée par Lightroom sur le corpus courant. Le rendu au
réglage source arrondi/nuance 0 retourne l'image d'origine sans transformation.
Une matrice absente, singulière ou un blanc cible à composantes capteur non
positives provoque un échec explicite ; aucun retour silencieux au traitement
RAW post-DCP. Les combinaisons extrêmes température/nuance hors du domaine
de caractérisation restent une limite à tester, sans revendication de parité LR.

Le proxy redimensionne aussi le RGB capteur à sa propre géométrie. Le cache
LRU comptabilise ce tampon, en dédupliquant les allocations partagées. Le
changement de profil conserve le tampon et les options, et remplace seulement
le DCP associé. L'empreinte moteur de restauration passe à
`ipr-render-2026-10-11-wb-capteur-v3` pour distinguer les rendus.

## Compensation de rendu par profil — 16 octobre 2026

Le [graphe actuel / cible](architecture/chemin-ouverture-raw.md) situe les
responsabilités dans le code et le repli lorsqu'une calibration est refusée.

**Politique courante — ADR-0011/0012 :** calibration demandée par défaut, manifeste
schéma 4 suivi automatiquement et règles par format/décodeur/modèle/DCP/mode capteur, sans
empreinte RAW ni liste d'ISO. Les paragraphes décrivant l'intégration initiale
limitée au corpus ci-dessous sont des antécédents ; la portée actuelle est
définie dans l'[ADR-0011](decisions/ADR-0011-regles-raw-par-mode-et-configuration-vivante.md).
Les nouveaux R6 III/R7 compatibles peuvent être ouverts depuis n'importe
quel dossier ou une carte SD ; cela ne certifie pas leurs ISO/modes visuellement.

L'[ADR-0010](decisions/ADR-0010-compensation-rendu-par-profil-dcp.md) décrit le
contrat et les futurs DCP IPR. Le manifeste existant contient un bloc
`profile_rendering`, distinct de `entries` qui conserve les niveaux sources.
Le R7 Camera Neutral audité reçoit −0,02 EV avec calibration active sur les
trois RAW couverts à ISO 20000/32000 ; base source +0,35, base effective +0,33.
Le R6 reçoit zéro compensation. Le SHA-256 du DCP, et non son nom de fichier,
fait partie de la sélection ; un nouveau DCP ne récupère pas cette règle.

Chemin : **RAW et métadonnées → calibration exacte → paramètres boîtier/DCP
→ gain composé avant tables/courbe → master Rec.2020 → retouches → sortie**.
Les options immuables du contexte DCP transportent la compensation pendant
les retouches WB. À la réouverture, le manifeste et le profil persistés
permettent de la recalculer ; leur empreinte et l'identité moteur invalident
les anciens pixels. Le statut IHM l'affiche séparément. Aucun nouveau
décodage ni nouvelle passe pixel n'est ajouté par le coefficient lui-même.
Le changement de profil avec calibration conserve néanmoins son besoin
actuel de redécodage : ne pas réutiliser un tampon calibré pour un autre DCP.

Le premier lot de transition introduit `CameraCharacterization` : sélection
du mode natif sans lecture RAW/DCP, catalogue borné et cache de huit documents
parsés par contenu. Le registre de formats route également les vignettes vers
le callback de l'adaptateur ; une extension future n'est pas envoyée implicitement
à LibRaw. L'[ADR-0012](decisions/ADR-0012-formats-extensibles-et-caracterisations.md)
compare DCP/JSON/SQLite et distingue ce lot de la future séparation complète
du signal caméra et des caractérisations physiques.

Après validation visuelle du premier lot par l'utilisateur, le deuxième lot
extrait `CameraSignal` : RGB caméra linéaire float32, WB appliquée, identité et
neutre explicites. L'ouverture LibRaw avec DCP et le changement rapide de
profil passent par son rendu commun. Le tampon caméra est transféré au
contexte immuable, puis une copie est rendue ; le gain composé et le DCP ne
sont pas accumulés. Un signal sRGB, non fini ou sans WB appliquée est refusé.
Le repli sans DCP, les règles R6/R7 et le besoin de redécodage lors d'un
changement de profil calibré ne sont pas modifiés par ce lot.

## Profils préparés partagés — 17 octobre 2026

Les appels de compatibilité, recherche, rendu DCP, WB caméra et optique LCP
utilisent maintenant `PreparedProfileCache`. Le LRU commun est limité par défaut
à 64 Mio comptabilisés et 16 entrées. Une entrée contient un profil parsé
immuable, jamais la WB, les gains ou les pixels d'une photo. La même clé en
cours de préparation est partagée entre lecteurs ; les clés distinctes restent
préparables en parallèle. Les rendus ne tiennent pas le mutex du magasin.

L'identité repose sur type/révision du parseur et empreinte des octets relus.
La modification d'un profil de même taille/date est détectée ; un fichier
supprimé n'est pas servi depuis une entrée ancienne. Le cache supprime le
reparsage répété mais conserve le coût de lecture/empreinte. Les objets actifs
après éviction et les buffers de lecture/rendu ne sont pas bornés par ce seul
budget : voir les limites et tests dans l'[ADR-0014](decisions/ADR-0014-cache-profils-prepares.md).
Les caches de sélection de chemins et le catalogue de caractérisations sont distincts.

Un futur DCP IPR peut porter son offset via **BaselineExposureOffset (51109)**,
déjà lu par `CameraProfileEngine`. Les autres paramètres de rendu pourront
être décrits au niveau de la bibliothèque de profils. Ne pas dupliquer un
même offset dans le DCP et la compensation externe. Les profils Adobe restent
inchangés ; les R7 de modes compatibles sont ouverts à l'essai suivant le
catalogue, sans garantie de fidélité à tous les ISO et modes.

## Tests automatisés

La cible `cameraProfileEngineTests` génère des DCP/LCP de petite taille dans un répertoire temporaire. Elle couvre notamment :

- ForwardMatrix et repli sans ForwardMatrix ;
- interpolation multi-illuminants de matrices et tables secondaires ;
- circularité des HueShift, rotation de teinte dans ProPhoto, encodage de l'axe Valeur des tables 3D et neutralité de l'encodage des tables 2,5D ;
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

Sur UNIX avec `BUILD_TESTING`, LibRaw activé et ses dépendances trouvées,
CMake construit `rawProfileIntegrationProbe`. Cette cible n'est pas déclarée
pour Windows par le bloc CMake courant ; ne pas la présenter comme universelle. Il prend le chemin du RAW et les répertoires de profils et affiche les noms DCP/LCP sélectionnés, les métadonnées LibRaw et un écart pixel moyen entre le rendu profilé et un développement de référence sans profil :

```sh
./build/rawProfileIntegrationProbe image.cr3 \
  /mnt/c/ProgramData/Adobe/CameraRaw/CameraProfiles \
  /mnt/c/ProgramData/Adobe/CameraRaw/LensProfiles
```

Le probe retourne une erreur si les profils attendus ne sont pas sélectionnés, si la comparaison ne montre aucune différence mesurable ou si le décodage échoue. Il n'applique pas les réglages de l'interface ; une ouverture sans sidecar part de valeurs neutres, tandis qu'un sidecar
valide restaure les choix de l'image. La sonde ne constitue pas un test de
cette restauration.

Le paramètre optionnel `--camera-wb` remplace la balance des blancs automatique par celle enregistrée par le boîtier. Il peut être ajouté après le chemin d'aperçu, ou seul, pour comparer les deux modes :

```sh
./build/rawProfileIntegrationProbe image.cr3 --default aperçu.png --camera-wb
```
