# Fonctionnement de `ImageEditor::renderWorkingImage`

## Objet et contrat

`ImageEditor::renderWorkingImage()` construit une nouvelle image de travail à partir :

- d'une image source déjà développée et normalisée ;
- d'un instantané des réglages d'édition.

Sa signature est :

```cpp
static ColorPipeline::WorkingImage renderWorkingImage(
    const ColorPipeline::WorkingImage &source, const Settings &settings);
```

L'entrée est une image RGBA flottante en sRGB linéaire (`CV_32FC4`), avec alpha droit. La fonction renvoie la même représentation logique après les réglages, ainsi que son espace colorimétrique et la provenance de la transformation source. Elle ne charge pas le fichier, ne modifie pas le provider QML et n'écrit rien sur disque. Ces opérations appartiennent respectivement au chargement, au rendu d'aperçu et à l'export.

Une source vide donne immédiatement un `WorkingImage` vide (`return {}`). Une erreur d'application température/nuance est également signalée par un avertissement puis par une image vide. Les étapes suivantes de conversion doivent vérifier cette image vide ; l'aperçu signale alors l'échec et l'export renvoie un message d'erreur de conversion.

Lorsque tous les réglages sont neutres (y compris la température de référence),
la fonction retourne directement le `WorkingImage` source. Cette copie est
superficielle pour `cv::Mat` : aucun tampon RGB, aucune séparation/fusion de
canaux et aucune boucle par pixel ne sont alors alloués ou exécutés. Dès qu'un
réglage a un effet, seules les étapes concernées sont parcourues ; les étapes
neutres restent court-circuitées.

## Déroulement du rendu

Les étapes ci-dessous suivent l'ordre effectif de la fonction dans `imageeditor.cpp`. Les réglages sont non destructifs : `m_original` reste la base des rendus suivants.

### 1. Séparation alpha et création du tampon RGB

La fonction sépare les quatre canaux de `source.linearRgba`, puis fusionne uniquement R, G et B dans `adjusted`. L'alpha d'origine reste conservé à part et n'est donc pas modifié par les réglages tonals ou couleur.

`cv::split()` produit les matrices de canaux nécessaires au traitement. Le `cv::merge()` des trois canaux crée un tampon RGB sur lequel les transformations sont appliquées. La source est passée par référence constante et n'est pas modifiée intentionnellement.

### 2. Conversion en niveaux de gris (si demandée)

Si `settings.grayscale` est vrai, OpenCV convertit le RGB en luminance grise puis réplique cette composante dans les trois canaux RGB. L'alpha original est toujours conservé.

### 3. Exposition

L'image de travail est linéaire ; l'exposition est donc un gain multiplicatif exprimé directement en valeurs d'exposition (EV) :

```text
gain = 2 ^ brightness
RGB  = RGB * gain
```

Le setter `setBrightness()` borne `brightness` à `[-5.00, +5.00]`, ce qui correspond bien à une plage réelle de ±5 EV (un gain de `2^-5 ≈ 0,031×` à `2^+5 = 32×`). Les curseurs QML (`Main.qml` et `Setting.qml`) reflètent cette plage avec un pas de `0.01` EV et un champ de saisie à deux décimales.

### 4. Contraste

Le contraste emploie une courbe continue autour du gris linéaire de référence 18 %. Le facteur de contraste vaut toujours `1 + contrast / 100`, mais il devient l'exposant d'une courbe à deux segments :

```text
Y <= 0.18 : Y' = 0.18 * (Y / 0.18) ^ facteur
Y >  0.18 : courbe symétrique entre 0.18 et 1
```

La luminance est modifiée puis son rapport `Y' / Y` est appliqué aux trois canaux, ce qui conserve la chromie. Le pivot reste fixe, les hautes valeurs HDR gardent une extrapolation linéaire, et les ombres positives ne deviennent plus négatives avant l'encodage.

### 5. Température et nuance

La fonction délègue cette étape à `ColorPipeline::applyTemperatureTint()`, qui attend un tampon `CV_32FC3`. Cette fonction est appelée après l'exposition et le contraste. Les réglages changent les gains RGB, pas l'alpha.

Le contrat actuel du pipeline attend une température en kelvins dans `[2000, 50000]` et une nuance dans `[-150, 150]`. Une erreur entraîne un journal d'avertissement et l'abandon du rendu. Les gains précis sont définis dans `ColorPipeline.cpp` ; ils ne représentent pas nécessairement un calcul physique complet de balance des blancs depuis l'illuminant de la scène.

Le point neutre (le pivot autour duquel les gains rouge/bleu s'annulent) n'est plus une constante fixe à 6500 K : `applyTemperatureTint()` reçoit désormais une **température de référence** explicite, `source.referenceWhiteBalanceKelvin`, qui correspond à la balance des blancs détectée sur l'image source :

- Pour les fichiers RAW décodés par LibRaw, cette valeur est estimée par `RawDecoder::estimateWhiteBalanceKelvin()` à partir des multiplicateurs **`cam_mul`** (balance « telle que prise de vue », propre à chaque photo) et de **l'inverse** de la matrice `cam_xyz` (approximation de McCamy), et propagée dans `ColorPipeline::WorkingImage::referenceWhiteBalanceKelvin`.
  - `cam_xyz` exprime la réponse des canaux caméra à une couleur XYZ (`RVB_cam = cam_xyz × XYZ`), et non l'inverse : il faut donc inverser cette matrice 3×3 pour convertir un RVB neutre caméra en XYZ. Une ancienne version du code utilisait `cam_xyz` directement (sans inversion) et les multiplicateurs `pre_mul` (constante « lumière du jour » fixe par modèle d'appareil, indépendante de la prise de vue) au lieu de `cam_mul` (balance réellement mesurée/réglée pour la photo). Ce double défaut produisait une température quasi constante et anormalement élevée (~12 000-13 000 K) pour toutes les photos d'un même modèle d'appareil, au lieu de la température « As Shot » réelle (ex. ~5200 K rapportée par Lightroom). Le correctif inverse désormais la matrice et utilise `cam_mul` en priorité (`pre_mul` reste un repli défensif si `cam_mul` est indisponible), et est couvert par le test `whiteBalanceEstimationTest`.
- Pour les formats standards (JPEG/PNG/TIFF…), une estimation best-effort est tentée via les métadonnées textuelles exposées par `QImageReader` (`readWhiteBalanceKelvin()` dans `imageeditor.cpp`) ; en l'absence de métadonnée exploitable, la valeur reste indéterminée.
- Si aucune valeur n'est disponible, le rendu retombe sur `5200 K` comme référence neutre par défaut (`.value_or(5200.0)`). Ce repli d'interface n'est pas une calibration RAW ni une modification de la WB caméra.

`Settings::temperature` est initialisé, au chargement de l'image et lors de `resetAdjustments()`, à cette même référence détectée (arrondie à l'entier le plus proche), et non plus à une constante fixe (`5200` précédemment). Le signal `adjustmentsReset(int temperatureKelvin, int tint)` transmet cette valeur au QML afin que le curseur de température affiche d'emblée la balance des blancs réelle de la photo plutôt qu'un neutre arbitraire, ce qui évite une mauvaise interprétation initiale des couleurs.

Côté QML, `Setting.qml` possède **deux** mécanismes de réinitialisation distincts qui doivent rester synchronisés avec cette référence détectée : la réinitialisation globale (`Connections::onAdjustmentsReset`) et le bouton de réinitialisation individuel du curseur de température (`AdjustmentControl.resetValue`). Ce dernier utilisait auparavant une constante `6500` codée en dur au lieu de la référence réelle ; `Setting.qml` expose désormais `root.referenceTemperatureKelvin`, mise à jour par `onAdjustmentsReset` et utilisée comme `resetValue` du curseur, afin que les deux boutons de réinitialisation retombent sur la même valeur.

Le gain RGB appliqué pour une température/référence donnée n'est plus un gain diagonal dérivé d'un `log(Kelvin)` normalisé par toute la plage du curseur (`[2000, 50000]`) : cette ancienne formule diluait fortement l'effet perçu près des températures de prise de vue courantes (~5000-6500 K), rendant les réglages extrêmes (ex. 2000 K) insuffisamment saturés comparés à Lightroom/RawTherapee/DxO. `applyTemperatureTint()` calcule désormais une matrice 3×3 d'adaptation chromatique physiquement motivée :

1. La chromaticité du locus de Planck est calculée pour la référence et pour la température cible via l'approximation rationnelle de Krystek (1985), valide sur toute la plage `[2000, 50000]` K utilisée ici.
2. Ces chromaticités (x, y) sont converties en XYZ (Y = 1), puis en espace cônique LMS via la matrice de Bradford (adaptation chromatique standard des profils ICC).
3. Le gain par canal est `gain = LMS_référence / LMS_cible`, normalisé pour que le gain du canal M (vert/luminance) reste à 1 : le curseur de température ne redistribue donc l'énergie qu'entre rouge et bleu, le vert/magenta restant du ressort exclusif de la nuance (`tint`).
4. Les étapes 1 à 3 sont combinées en une seule matrice `netMatrix = XYZtoRGB × Bradford⁻¹ × diag(gain) × Bradford × RGBtoXYZ`, précalculée une fois par appel puis appliquée par un simple produit matrice-vecteur par pixel (coût identique au gain diagonal précédent).
5. Pour les seules cibles plus froides que la référence, une calibration empirique, issue du corpus Lightroom/IPR de huit scènes, est composée après Bradford. Son intensité augmente continûment en logarithme des kelvins jusqu'à 2000 K ; à cette borne, elle applique les coefficients RGB `(0,35 ; 1,19 ; 0,84)`. Ces valeurs v3 ont été ajustées après une première validation v2, car la composition matricielle et le rendu hors gamut atténuent l'effet des coefficients directs. Elles compensent la sous-atténuation rouge observée sans changer la branche chaude, déjà proche de Lightroom. Cette partie doit rester identifiée comme une calibration de rendu et être revalidée à chaque évolution du pipeline, à la différence de l'adaptation Bradford qui est le modèle colorimétrique de base.

La Nuance emploie des gains symétriques sur R/B (magenta) et opposés sur G (vert). Avant application, ils sont normalisés par leur luminance Rec. 709 sur un gris neutre (`0,2126 R + 0,7152 G + 0,0722 B`). Le curseur reste donc chromatique : à nuance négative il va vers le vert, à nuance positive vers le magenta, sans provoquer le décalage d'exposition qui affectait auparavant les deux extrêmes.

Cas particulier : si la température demandée est strictement égale à la référence détectée et que la nuance est nulle, la fonction retourne immédiatement sans passer par la matrice (évite un bruit flottant négligeable mais non nul introduit par l'aller-retour via l'inverse de Bradford, et garantit un résultat bit-à-bit identique à l'image source dans ce cas neutre).

Ce nouveau modèle a été validé numériquement par comparaison avec la table de calibration réelle du capteur (`raw.imgdata.color.WBCT_Coeffs`, exposée par LibRaw) et par rendu visuel de `6V1A0027.CR3` : à 2000 K, l'image devient fortement bleue (comportement cohérent avec Lightroom à ce réglage extrême) ; à 50000 K, elle devient fortement orangée — les deux extrêmes du curseur produisent désormais un effet net, là où l'ancienne formule restait quasi neutre même en butée.

Pour les RAW, l'ouverture et la réinitialisation utilisent la balance boîtier
(*As Shot*) par défaut, comme la référence Lightroom du corpus. La balance
automatique reste un choix explicite dans l'interface ; elle redécode le RAW et
peut modifier sensiblement un ciel ou une scène dont les zones lumineuses ne
sont pas neutres.

### 6. Masques tonals et réglages des tons

Pour chaque pixel, la fonction calcule une luminance pondérée :

```text
Y = max(0, 0.2126 R + 0.7152 G + 0.0722 B)
```

Des fonctions `smoothStep` déterminent ensuite quatre masques graduels, afin que chaque réglage agisse principalement sur sa plage tonale au lieu de créer une frontière abrupte :

| Réglage | Masque | Zone principalement affectée |
|---|---|---|
| Ombres | masque `1 - smoothStep(0.10, 0.56, Y)` ; courbes distinctes pour relèvement et assombrissement | tons sombres et intermédiaires bas |
| Noirs | `1 - smoothStep(0.00, 0.18, Y)` | extrémité noire |
| Hautes lumières | récupération : `smoothStep(0.08, 0.70, Y)` ; levée : `smoothStep(0.02, 0.50, Y)` | tons moyens supérieurs et clairs |
| Blancs | récupération : `smoothStep(0.10, 0.65, max(R,G,B))` ; levée : `smoothStep(0.02, 0.50, max(R,G,B))` | point blanc et canaux lumineux |

Les noirs et les ombres possèdent des courbes séparées, sans cible de luminance fixe :

- **Noirs positifs** utilisent une cloche dans le pied (`0 < Y < 0,14`) ; le noir mathématique et les tons moyens restent inchangés. Les valeurs négatives réduisent directement ce même pied afin de définir un point noir plus profond.
- **Ombres positives** ajoutent une quantité bornée proportionnelle à `1 - sqrt(clamp(Y / 0,45))`, pondérée par le masque. L'action est donc maximale dans les très basses lumières non nulles, décroît dans les tons moyens, puis s'éteint avant les hautes lumières.
- **Ombres négatives** emploient la même décroissance avec une amplitude maximale de `0,57`, au lieu d'une multiplication uniforme. Elles préservent ainsi davantage les tons moyens et les hautes lumières que le noir.

Quand Noirs et Ombres sont simultanément négatifs, seule la réduction locale la plus forte est appliquée ; elles ne se multiplient pas entre elles. Même à `-100`, un détail non nul reste donc non nul avant l'encodage.

Les hautes lumières et les blancs utilisent des courbes indépendantes :

- **Hautes lumières** rapproche le ton du blanc avec une amplitude maximale de
  `0,70` à `+100` et un masque de levée plus étendu (`0,02–0,50`) ; sa
  récupération négative conserve le masque v2 et réduit la luminance de
  `52 %` au plus.
- **Blancs** applique son propre déplacement du point blanc (amplitude `0,95`
  et masque de levée `0,02–0,50` à `+100`) ; sa récupération négative reste
  plus retenue (`22 %` au plus, masque v2).

Les deux réglages peuvent donc se composer lorsqu'ils sont volontairement
utilisés ensemble, sans que Blancs serve de simple atténuateur à Hautes
lumières. Le masque Blancs est calculé avec `max(R,G,B)` avant l'encodage, car
un canal peut être lumineux alors que la luminance pondérée reste modérée. La
luminance cible est reconvertie en un rapport appliqué aux trois canaux, ce qui
préserve la teinte.
L'ordre d'application est donc exposition, contraste, température/nuance, puis
les quatre réglages tonals ; les masques sont calculés sur l'image déjà modifiée
par les étapes précédentes.

### Contraste

Le contraste est une courbe en S asymétrique autour du gris linéaire `0,18`.
Les tons moyens proches du pivot restent stables. Dans le pied, l'intensité
maximale est limitée à un facteur exponentiel `0,75` pour `-100` et `1,40`
pour `+100`, puis s'annule progressivement avant le pivot. Au-dessus du pivot,
l'épaule utilise respectivement `0,60` et `2,30`. Cette séparation évite que
`-100` transforme toute l'image en gris moyen ou que `+100` écrase les tons
moyens pour renforcer l'épaule.

### 7. Vibrance et saturation

Après les réglages tonals, le rendu calcule la chrominance relative de chaque
pixel à partir de ses canaux RGB linéaires. Les deux réglages sont bornés à
`[-100, +100]` :

- **Saturation** applique uniformément un facteur de chrominance autour de la
  luminance ; `-100` produit un gris de même luminance et `+100` double
  l'écart aux gris.
- **Vibrance** module son action en fonction de la chrominance déjà présente :
  les couleurs proches du gris sont davantage modifiées que les couleurs déjà
  saturées. Cela permet de renforcer une image sans pousser aussi fortement
  les couleurs les plus vives.

Les deux facteurs sont combinés, bornés pour ne pas inverser les composantes
couleur autour de la luminance, puis repris par la compression de gamut sRGB
de sortie. Ils sont appliqués avant les courbes afin que les courbes RGB
restent la dernière correction colorimétrique utilisateur.

### 8. Effets de détails et d'atmosphère

Avant les effets locaux, `DetailProcessing::reduceNoise()` traite le RGB
linéaire si l'un des deux bruits est non nul. Depuis le correctif consolidé
le 5 octobre, le modèle emploie Y Rec.709 et Co/Cg, des seuils Haar/MAD
proportionnels au curseur, des guides de structure préfiltrés, un rééquilibrage
des changements pour conserver la moyenne et une compression vers le gris
préservant Y si la reconstruction produit des canaux négatifs. Pas de clamp
HDR supérieur ; à bruit 0/0, la fonction ne touche pas le tampon.
La netteté masquée est appliquée plus tard et reste inchangée.
Les [équations et constantes](architecture/denoising-sharpening.md) et
le [correctif/tests](validation/analyses/correctif-bruit-2026-10-05.md)
précisent les limites ; les anciens exports bruit ne valident pas cette version.

Les effets sont appliqués en RGB linéaire après vibrance/saturation :

- **Texture** utilise une séparation locale à petit rayon (`sigma = 1,2`) et
  augmente ou diminue les micro-variations de luminance, sans modifier
  directement les rapports RGB.
- **Clarté** emploie la même famille de contraste local à un rayon plus large
  (`sigma = 8`), avec un masque qui protège les ombres profondes et les hautes
  lumières afin de limiter les halos.
- **Correction du voile** combine deux modèles physiques classiques de
  dévoilement atmosphérique :
  1. le **Dark Channel Prior** (He, Sun & Tang, 2009) estime la carte de voile
     locale à partir du minimum RGB érodé sur un voisinage, puis la lumière
     atmosphérique `A` à partir du 99ᵉ percentile des pixels les plus
     lumineux échantillonnés dans l'image ;
  2. la **loi de diffusion atmosphérique de Koschmieder** (`I = J·t + A·(1-t)`)
     est inversée (`J = (I - A) / t + A`) pour restituer la radiance « sans
     voile » `J` à partir de l'image observée `I`, d'une transmission `t`
     bornée et de `A`.
  Une valeur positive restaure le contraste perdu dans le voile ; une valeur
  négative en ajoute en réduisant artificiellement `t`. Ce modèle global est
  volontairement borné : il ne remplace pas un algorithme de profondeur ou une
  sélection locale, et l'estimation de `A`/`t` reste une approximation globale
  par image plutôt qu'une carte de profondeur réelle.
- **Grain** est une perturbation monochrome déterministe, ajoutée après le flou
  pour rester nette. Son amplitude est plus marquée dans les ombres et les
  coordonnées de l'image servent de graine : l'aperçu et l'export ne
  scintillent pas entre deux rendus.

Texture, clarté et correction du voile acceptent `[-100, +100]`. Le grain est
intentionnellement limité à `[0, 100]`.

### 9. Courbes de tonalité et histogramme

Après les réglages tonals, le rendu applique quatre courbes à points :

1. une courbe **Luminance** ; elle calcule une luminance cible puis applique un
   ratio RGB commun, ce qui préserve la chromaticité ;
2. les courbes **Rouge**, **Vert** et **Bleu**, appliquées dans cet ordre aux
   composantes linéaires.

Les courbes passent exactement par toutes les poignées avec un polynôme global
de **Lagrange**, évalué sous sa **forme barycentrique** (Berrut & Trefethen,
*Barycentric Lagrange Interpolation*, 2004) plutôt que sous la forme classique
à coefficients monomiaux :

```text
p(x) = [ Σᵢ wᵢ/(x - xᵢ) · yᵢ ] / [ Σᵢ wᵢ/(x - xᵢ) ]
```

où `(xᵢ, yᵢ)` sont les poignées (extrémités fixes `(0,0)`/`(1,1)` incluses) et
`wᵢ` les poids barycentriques classiques du polynôme interpolateur. Cette
formulation reste numériquement stable quel que soit le nombre de poignées,
contrairement à la résolution directe d'un système de Vandermonde qui devient
mal conditionnée dès que le degré augmente. Les extrémités fixes `(0,0)` et
`(1,1)` forment la courbe affine de degré 1 sans poignée interne ; chaque
poignée interne ajoute un degré : une poignée donne un polynôme de degré 2,
deux poignées un degré 3, etc. Il n'y a donc ni cassure de pente ni raccord
entre segments dans l'intervalle `[0,1]`.

Les poignées internes sont triées et gardent un écart minimal de `0,001` sur
l'axe des entrées ; leurs sorties restent bornées à `[0,1]`. Les valeurs HDR
supérieures à un sont extrapolées avec la dérivée exacte du polynôme au dernier
point, ce qui conserve une tangente continue au-delà de la plage éditable. Une
courbe vide est la diagonale d'identité. Cela évite de supprimer les hautes
valeurs linéaires avant la compression de gamut. Des poignées placées avec des
variations très fortes peuvent toutefois produire une courbe raide : la
continuité mathématique ne remplace pas une correction tonale modérée.

L'interface permet de sélectionner l’une des quatre courbes, d'ajouter une
poignée par double-clic, de la déplacer, de supprimer une poignée interne par
clic droit et de réinitialiser une courbe ou l’ensemble. Le Canvas échantillonne
le même polynôme afin que la courbe affichée ne soit pas une approximation par
segments. Les changements sont non destructifs, déclenchent le même rendu
asynchrone que l’aperçu et sont repris par les exports.

Ce même rendu produit un histogramme de 256 bins pour la luminance, le rouge,
le vert et le bleu. Les bins utilisent des valeurs encodées sRGB pour rester
lisibles face à l’aperçu ; ils décrivent les pixels après les courbes et avant
la quantification finale.

### Compression de gamut de sortie

Les transformations restent en sRGB linéaire sans écrêtage intermédiaire. Avant
l'aperçu et les exports JPEG/PNG/TIFF/DNG, `ColorPipeline::mapToSrgbGamut()`
ramène les couleurs hors gamut dans le cube sRGB. Il conserve d'abord la
luminance puis réduit la chroma vers le gris de même luminance jusqu'à ce que les
trois canaux soient représentables. Contrairement à un `clamp` indépendant de
R, G et B, cette compression préserve la direction de teinte autant que le
gamut le permet et évite les franges colorées dans les blancs très lumineux.

### 10. Flou gaussien (si demandé)

Pour `blur > 0`, un flou gaussien est appliqué au tampon RGB avec `sigma = blur / 2`. OpenCV choisit la taille du noyau (`Size(0, 0)`). L'alpha n'est pas flouté, car il est réintégré depuis les canaux source à la fin.

### 11. Reconstruction RGBA et retour

Les canaux RGB traités sont séparés, puis l'alpha source est ajouté et les quatre canaux sont fusionnés dans un nouveau tampon. Le retour contient :

```cpp
return {rgbaOutput, source.colorSpace, source.sourceProfile,
        source.referenceWhiteBalanceKelvin};
```

Les pixels sont nouveaux ; les métadonnées de couleur/provenance restent celles de l'image source. La fonction ne réencode pas l'image en sRGB gamma ni en 8 bits.

## Partage entre aperçu et export

La fonction est commune aux deux destinations, ce qui empêche les réglages de diverger :

```text
aperçu QML :
  renderWorkingImage(source, settings)
  → ColorPipeline::toSrgb(..., Preview8Bit)
  → ImageProvider / QML

export :
  renderWorkingImage(source, settings)
  ├─ JPEG/PNG/TIFF : ColorPipeline::toSrgb(...)
  │                  → QImageWriter
  └─ DNG : RGB linéaire 16 bits
           → LibTIFF + balises DNG
```

Les conversions finales et la quantification dépendent du format d'affichage ou d'export ; les transformations de développement et les réglages sont partagés. Le DNG contient un rendu RGB linéaire 16 bits avec les réglages intégrés : il n'est pas une copie du RAW mosaïque source et ne conserve donc pas sa latitude de développement ni ses métadonnées de prise de vue. Le dialogue de sauvegarde est prérempli avec `nom-source-IPR.<format>` dans le dossier de la source. Le backend accepte les URLs `file:` et les chemins absolus natifs que certains dialogues Windows retournent sous la forme `C:/…`. L'appel d'export capture l'image source et les réglages avant le travail asynchrone, de sorte que le rendu utilise un instantané cohérent même si l'interface change ensuite.

### Précision du transfert sRGB de sortie — correctif du 7 octobre 2026

`ColorPipeline::toSrgb()` conserve le gamut mapping existant puis applique
la fonction de transfert sRGB directement aux canaux RGB flottants :
`12,92 × C` pour `C ≤ 0,0031308`, sinon `1,055 × C^(1/2,4) − 0,055`.
La quantification 16 bits intervient ensuite, une seule fois. Alpha reste
linéaire et non prémultiplié. L'image encodée est marquée ICC sRGB ; la
conversion ICC des images sources reste inchangée. Cette opération est
commune à l'aperçu et aux sorties JPEG/PNG/TIFF, pas à la branche DNG linéaire.

L'ancien chemin quantifiait d'abord en linéaire 16 bits puis utilisait une
conversion entière Qt à précision de table réduite. Dans notre environnement,
les codes linéaires 1–15 devenaient zéro. Le nouveau chemin rend 13 pour
le code 1 et 194 pour le code 15. Une rampe exhaustive de 65 536 entrées
vérifie monotonie, extrêmes, neutralité RGB et conservation alpha, avec une
tolérance maximale d'un code face à la formule analytique.

### Proxy d'édition pour l'aperçu interactif

`renderWorkingImage()` recalcule l'intégralité du pipeline à chaque modification de réglage ; son coût est donc proportionnel au nombre de pixels traités. Pour l'aperçu, `ImageEditor::startRender()` ne part pas de l'image source pleine résolution (`m_original`) mais d'un second tampon, `m_editProxy`, produit par `ImageEditor::buildEditProxy()` :

- si le plus grand côté de l'image source ne dépasse pas 2048 px, le proxy est une copie superficielle de la source (aucun redimensionnement, aucune allocation supplémentaire) ;
- sinon, l'image est réduite par `cv::resize(..., cv::INTER_AREA)` jusqu'à ce que son plus grand côté atteigne 2048 px, en conservant son espace colorimétrique, sa provenance et sa température de référence.

Le proxy est calculé une seule fois par chargement d'image (dans `loadFromDisk()`, donc déjà hors du thread UI) et réutilisé pour tous les rendus d'aperçu suivants, jusqu'au prochain chargement. L'export (`exportImage()`/`writeImage()`) continue d'utiliser `m_original` sans aucune dégradation : seule la réactivité de l'aperçu est affectée, pas la qualité du résultat final. L'histogramme affiché à l'écran est calculé sur ce même proxy ; sa forme reste représentative de l'image, mais ses comptages exacts (en particulier les pixels en butée à 0 ou 255) peuvent légèrement différer de ceux d'un calcul en pleine résolution, par effet de moyennage du redimensionnement.

### Comparaison origine / actuel et zoom d'affichage

`ImageViewport.qml` gère le zoom et la comparaison sans modifier les
réglages photo. Son bouton Avant/après (Ctrl Maj C) affiche l'origine à
gauche et le rendu actuel à droite, séparés par une frontière mobile.
Les deux images partagent la même transformation et sont clipsées dans
des régions complémentaires pour ne pas doubler la composition alpha.
Le zoom reste celui du proxy, pas un affichage du RAW pleine résolution.

`renderPreview(source, settings, includeOriginal)` encode également la
source neutre en sRGB 8 bits lorsque la référence n'est pas encore prête.
Cette conversion contourne `renderWorkingImage()` et donc les réglages
de l'éditeur/sidecar, mais conserve le développement, les profils et la
balance des blancs déjà appliqués au décodage. Elle est exécutée avec le
premier rendu accepté, pas à chaque mouvement de curseur. L'histogramme
continue de représenter uniquement le rendu actuel.

`ImageProvider::setImages()` publie les deux QImage sous le même verrou.
`originalImageSource` utilise `image://editor/original?revision=…`, stable
pendant les réglages ; l'URL courante continue d'être révisée pour chaque
rendu. Les résultats périmés ne sont pas publiés. Au changement de source,
les deux URL et les références du provider sont effacées ; le prochain
premier rendu recrée l'origine, y compris après un redécodage automatique
de balance des blancs. Le provider conserve au maximum un aperçu neutre
supplémentaire de 16 Mio de pixels CPU (2048×2048×4), hors ressources Qt Quick.
Cette référence n'est ni persistée ni conservée dans le LRU de RAW.

Le [contrat de zoom/comparaison](specifications/zoom-image.md) détaille
les raccourcis, les limites et la recette. Les exports ne sont pas modifiés.

### Cache mémoire des RAW inactifs

Au changement d'image, `ImageEditor` transfère l'ancien RAW décodé et son proxy
dans `DecodedImageCache`, puis libère immédiatement ses références actives
`m_original` et `m_editProxy` avant un nouveau décodage. Le cache est un LRU
borné à **256 Mio** par défaut ; son budget est configurable via la propriété
`decodedImageCacheBudgetBytes`. Sa clé inclut le chemin canonique, la taille,
la date de modification et le mode de balance des blancs automatique, ce qui
évite de réutiliser une version modifiée du fichier ou des pixels développés
avec un autre mode RAW. Une consultation remet l'entrée en tête du LRU et le
dépassement du budget libère les entrées les moins récemment utilisées.

Les copies de `cv::Mat` restent superficielles. En particulier, pour une image
qui ne nécessite pas de réduction, l'original et le proxy partagent le même
tampon et ne sont comptés qu'une fois dans le budget du cache.

## Pondération des réglages tonals

L'exposition reste volontairement globale : une valeur de `-0,49 EV` multiplie chaque pixel linéaire par `2^-0,49`. En revanche, les réglages **noirs**, **ombres**, **hautes lumières** et **blancs** sont évalués à partir de la luminance du pixel. Des masques continus et chevauchants répartissent leur influence entre les zones tonales ; ils ne créent donc pas de frontière nette entre deux plages.

Les couples noirs/ombres et hautes lumières/blancs sont fusionnés avec une composition bornée plutôt que d'appliquer deux multiplicateurs successifs. Ainsi, régler noirs et ombres à `-100` conserve une partie du signal au lieu d'assombrir deux fois le même pixel. La courbe finale est calculée sur la luminance puis appliquée une seule fois aux composantes RGB par un ratio de luminance. Cette étape préserve la chromaticité dans les zones compressées et évite que les réglages tonals ne désaturent les couleurs.

## Pourquoi retourner `ColorPipeline::WorkingImage` par valeur ?

### Le type de retour décrit exactement le résultat

Le résultat ne se limite pas aux pixels. `WorkingImage` regroupe :

- le `cv::Mat` RGBA linéaire ;
- le `QColorSpace` associé ;
- la chaîne de provenance du profil ou de la transformation.

Retourner cette structure évite de séparer les pixels de leurs métadonnées, et rend visible à l'appelant que le résultat est une image de travail conforme au contrat colorimétrique.

### La valeur exprime la propriété et la durée de vie

La fonction fabrique un nouveau tampon `rgbaOutput`. Retourner un objet valeur signifie que le résultat appartient à l'appelant et reste valide après la sortie de la fonction. Il n'est pas nécessaire de gérer manuellement `new`/`delete`, ni de se demander si un pointeur désigne une variable locale.

Avec un pointeur vers `WorkingImage`, il faudrait préciser :

- qui alloue et détruit l'objet ;
- si le pointeur peut être nul ;
- si la fonction remplace le contenu existant ou le modifie ;
- combien de temps l'objet reste valide ;
- si l'appelant doit fournir un objet initialisé.

Un pointeur vers un `cv::Mat` local serait particulièrement incorrect : sa durée de vie se terminerait à la sortie de la fonction. Un pointeur de sortie vers un objet possédé par l'appelant serait possible, mais ajouterait un protocole de mutation et de durée de vie sans bénéfice ici.

### Le retour par valeur n'implique pas une copie complète des pixels

`cv::Mat` utilise une gestion de mémoire à comptage de références. Sa copie ordinaire est généralement superficielle : les copies partagent le stockage et incrémentent le compteur de références ; la mémoire reste valide tant qu'une instance la possède. En C++17, la construction du résultat temporaire est en outre généralement élidée. Même si le petit objet `cv::Mat`/`QString`/`QColorSpace` est déplacé ou copié, cela ne signifie pas qu'on recopie toute l'image pixel par pixel.

Dans cette fonction, `rgbaOutput` est un nouveau résultat produit par `cv::merge()` ; le `WorkingImage` retourné en conserve la propriété de manière sûre. La représentation par valeur ne dispense toutefois pas de tenir compte du partage de stockage de `cv::Mat` : modifier une autre copie superficielle du même tampon peut affecter les pixels partagés. Ici, le résultat est construit séparément de la source avant d'être rendu au code appelant.

### Fonctionnement avec les tâches asynchrones

Le rendu d'aperçu copie `m_original` et `Settings` avant de lancer le travail avec `QtConcurrent`. Copier `WorkingImage` transmet les métadonnées par valeur et copie le handle `cv::Mat` avec son comptage de références ; le tampon source reste vivant pendant le travail asynchrone. La fonction de rendu lit la source et produit un nouveau tampon : elle ne modifie donc pas `m_original`.

Ce modèle évite d'exposer un pointeur brut entre le thread UI, le worker et le résultat. Il est simple à composer et rend les durées de vie plus sûres. Il ne rend pas automatiquement les écritures concurrentes sur une même matrice sûres : la règle reste de ne pas modifier simultanément un tampon `cv::Mat` partagé.

### Valeur vide comme échec

`WorkingImage` dispose de `isNull()`. Un retour vide (`{}`) permet d'exprimer l'absence de résultat sans valeur de pixel fictive, et les appelants contrôlent explicitement cet état. Pour un traitement futur qui exige de différencier plusieurs classes d'erreurs, un type résultat dédié (valeur + erreur) pourrait rendre le contrat encore plus explicite ; un pointeur brut ne fournirait pas à lui seul cette information.

## Persistance `.ipr` et application par lot

Le sidecar courant v6 est un flux `QDataStream` binaire placé à côté du fichier source sous le nom `source.ext.ipr`. Son en-tête contient la signature `IPR1`, la version du format, la taille et la date de modification du source. Cette signature empêche d'appliquer silencieusement des réglages devenus incohérents après le remplacement d'une image.

La v5 ajoute DCP, manifeste, leurs SHA-256, activation de normalisation et version du moteur/LibRaw ; la v6 ajoute identité/SHA-256 du LCP, résolution explicite et activations objectif/CA latérale. V1 à v6 restent lues. Les options RAW et la WB automatique sont restaurées avant le décodage. Le LCP résolu est figé, et les corrections sont suspendues si ses octets sont absents/modifiés. Le changement DCP utilise un unique tampon caméra actif, plafonné à 384 Mio, lorsque WB/calibration et options/empreinte optiques le permettent ; sinon un redécodage conserve le rendu visible. Voir [ADR-0006](decisions/ADR-0006-restauration-raw-et-retouche-profil.md) et le [contrat optique](camera-profile-engine.md#persistance-et-commandes-optiques-9-octobre-2026).

L'écriture est différée de 450 ms puis atomique via `QSaveFile`. Le payload stocke les réglages du rendu, les courbes, les annotations, le mélangeur huit bandes et le grading trois voies. À l'ouverture, un sidecar valide remplace les réglages neutres et synchronise l'interface QML. Le ruban peut appliquer les réglages courants à plusieurs images : seules leurs sidecars sont écrites, jamais les sources, et les annotations existantes de la cible sont conservées.

Les v1/v2/v3 sont migrées : l'ancien dernier triplet est magenta (7), jamais
violet (6). Les nouvelles bandes restent neutres. Le calcul sélectif est isolé
dans `ColorMixer::apply`, après saturation/vibrance globales et avant grading :
masques triangulaires sur H source, protection smoothstep de S=0 à 0,05,
commandes cumulées, normalisation à Y source et gain de luminance borné ±0,5 EV.
Il n'écrête pas le RGB flottant ; les canaux négatifs sont laissés intacts.
Le rendu tous réglages HSL à zéro reste inchangé, mais les anciens réglages
non nuls peuvent produire un autre rendu. Détails, constantes et compatibilité :
[ADR-0005](decisions/ADR-0005-melangeur-huit-bandes-luminance-bornee.md).

## Limites et points de vigilance

- Les opérations colorées sont des ajustements empiriques du RGB de travail ; la température ne constitue pas à elle seule une balance des blancs RAW physiquement complète.
- Les masques d'ombres/noirs/hautes lumières/blancs sont définis dans cette fonction et appliqués sur la luminance issue du rendu courant. Leur définition et leur ordre font partie du comportement visible de l'éditeur.
- Le clipping n'est pas réalisé dans `renderWorkingImage()` ; il intervient lors de la quantification/conversion de sortie. L'image flottante peut donc conserver des valeurs hors de `[0, 1]`.
- Le retour par valeur sécurise la propriété de l'objet résultat, mais `cv::Mat` copie ses données de façon superficielle lors d'une copie ordinaire. Il faut utiliser `clone()` si une copie indépendante des pixels est nécessaire.
- Vérifier les valeurs par défaut des réglages lors de leur modification : la température neutre définie dans la fonction couleur doit coïncider avec la valeur initiale et la valeur de réinitialisation de `Settings` et du QML.
## Correctif Ombres/HL positifs — 10 octobre 2026

Les branches positives du rendu utilisent désormais
[`ToneRecovery.h`](../src/core/ToneRecovery.h). Ombres :
`deltaY = 3*a*Y*(1-smoothstep(0.10,0.56,Y))/(1+Y/0.025)^2`, borné à
90 % de la marge RGB. Le gain maximum est ×4 (+2 EV), le noir reste noir,
et la rampe ne présente plus l'inversion de tons de l'ancien ajout inverse-racine.
`0.025` est le genou de la courbe, `3` la borne du gain supplémentaire :
ce sont des choix initiaux à recetter, pas des constantes physiques ni une
calibration universelle Lightroom.

HL positives : `deltaY = 0.70*a*masqueHL*max(0,Y/maxRGB-Y_courant)` pour
`0<maxRGB<1`. Un canal déjà hors gamut/HDR n'est ni relevé ni écrêté par
cette branche. 0.70 conserve 30 % de marge vers la frontière RGB. Une
luminance cible inférieure à 1 ne suffisait pas à protéger une couleur saturée.
Le facteur RGB commun conserve les rapports des canaux avant les traitements
suivants ; un mélange avec Blancs, saturation ou grading peut encore déborder.

EV, Noirs, Blancs et branches négatives restent inchangés. À zéro, les deux
nouveaux opérateurs sont identitaires. Les sidecars gardent leurs valeurs,
mais un ancien réglage positif Ombres/HL produit désormais un rendu différent.
Ce correctif n'ajoute pas de débruitage ni de traitement spatial : ouvrir les
ombres peut toujours révéler le bruit du capteur. Il ne récupère pas des
informations écrêtées dans le RAW. Voir le
[rapport Lumières](validation/analyses/analyse-validation-lumieres-base-2026-10-02.md)
pour l'investigation, les preuves et la recette de conformité sur d'autres matériels.
